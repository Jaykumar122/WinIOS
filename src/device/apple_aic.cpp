#include "device/apple_aic.h"

#include <algorithm>
#include <cstring>

namespace winios::device {

AppleAIC::AppleAIC(std::uint32_t device_id,
                   std::uint64_t mmio_base,
                   std::uint64_t mmio_size,
                   std::size_t num_cores)
    : VirtualDevice(device_id, "AppleAIC2", VirtualDeviceType::GenericMmio, mmio_base, mmio_size),
      num_cores_(std::clamp<std::size_t>(num_cores, 1, kMaxCores)) {
    reset();
}

void AppleAIC::reset() noexcept {
    std::lock_guard<std::mutex> lock(lock_);
    aic_enabled_ = true;
    current_core_ = 0;

    // By default on Apple Silicon, all IRQs are masked (1) until driver unmasks them
    mask_bits_.fill(0xFFFFFFFFu);
    hw_pending_bits_.fill(0u);
    sw_pending_bits_.fill(0u);

    // Default target affinity: Core 0 (0x01)
    target_cores_.fill(0x01u);

    for (std::size_t i = 0; i < kMaxCores; ++i) {
        cores_[i].ipi_pending = 0u;
        cores_[i].ipi_mask = 0u;
        cores_[i].timer_pending = false;
        cores_[i].active_event = kEventNone;
        cores_[i].irq_line_asserted = false;
    }
}

void AppleAIC::set_core_count(std::size_t cores) noexcept {
    std::lock_guard<std::mutex> lock(lock_);
    num_cores_ = std::clamp<std::size_t>(cores, 1, kMaxCores);
    if (current_core_ >= num_cores_) {
        current_core_ = 0;
    }
    update_core_irq_lines();
}

void AppleAIC::set_hw_irq(std::uint32_t irq, bool asserted) noexcept {
    if (irq >= kMaxIrqs) return;
    std::lock_guard<std::mutex> lock(lock_);

    const std::size_t word = irq / 32;
    const std::uint32_t bit = 1u << (irq % 32);

    if (asserted) {
        hw_pending_bits_[word] |= bit;
    } else {
        hw_pending_bits_[word] &= ~bit;
    }

    update_core_irq_lines();
}

bool AppleAIC::is_hw_irq_asserted(std::uint32_t irq) const noexcept {
    if (irq >= kMaxIrqs) return false;
    std::lock_guard<std::mutex> lock(lock_);
    const std::size_t word = irq / 32;
    const std::uint32_t bit = 1u << (irq % 32);
    return (hw_pending_bits_[word] & bit) != 0;
}

bool AppleAIC::is_hw_irq_masked(std::uint32_t irq) const noexcept {
    if (irq >= kMaxIrqs) return true;
    std::lock_guard<std::mutex> lock(lock_);
    const std::size_t word = irq / 32;
    const std::uint32_t bit = 1u << (irq % 32);
    return (mask_bits_[word] & bit) != 0;
}

void AppleAIC::mask_hw_irq(std::uint32_t irq) noexcept {
    if (irq >= kMaxIrqs) return;
    std::lock_guard<std::mutex> lock(lock_);
    const std::size_t word = irq / 32;
    mask_bits_[word] |= (1u << (irq % 32));
    update_core_irq_lines();
}

void AppleAIC::unmask_hw_irq(std::uint32_t irq) noexcept {
    if (irq >= kMaxIrqs) return;
    std::lock_guard<std::mutex> lock(lock_);
    const std::size_t word = irq / 32;
    mask_bits_[word] &= ~(1u << (irq % 32));
    update_core_irq_lines();
}

void AppleAIC::set_irq_target(std::uint32_t irq, std::uint32_t core_mask) noexcept {
    if (irq >= kMaxIrqs) return;
    std::lock_guard<std::mutex> lock(lock_);
    target_cores_[irq] = core_mask;
    update_core_irq_lines();
}

std::uint32_t AppleAIC::get_irq_target(std::uint32_t irq) const noexcept {
    if (irq >= kMaxIrqs) return 0;
    std::lock_guard<std::mutex> lock(lock_);
    return target_cores_[irq];
}

void AppleAIC::send_ipi(std::uint32_t target_core_mask) noexcept {
    std::lock_guard<std::mutex> lock(lock_);
    for (std::size_t c = 0; c < num_cores_; ++c) {
        if ((target_core_mask & (1u << c)) != 0) {
            cores_[c].ipi_pending |= 0x01u; // Assert IPI bit 0
        }
    }
    update_core_irq_lines();
}

std::uint32_t AppleAIC::get_pending_ipi(std::size_t core_id) const noexcept {
    if (core_id >= num_cores_) return 0;
    std::lock_guard<std::mutex> lock(lock_);
    return cores_[core_id].ipi_pending;
}

void AppleAIC::clear_ipi(std::size_t core_id, std::uint32_t ipi_mask) noexcept {
    if (core_id >= num_cores_) return;
    std::lock_guard<std::mutex> lock(lock_);
    cores_[core_id].ipi_pending &= ~ipi_mask;
    update_core_irq_lines();
}

void AppleAIC::set_timer_irq(std::size_t core_id, bool asserted) noexcept {
    if (core_id >= num_cores_) return;
    std::lock_guard<std::mutex> lock(lock_);
    cores_[core_id].timer_pending = asserted;
    update_core_irq_lines();
}

bool AppleAIC::is_timer_irq_asserted(std::size_t core_id) const noexcept {
    if (core_id >= num_cores_) return false;
    std::lock_guard<std::mutex> lock(lock_);
    return cores_[core_id].timer_pending;
}

void AppleAIC::set_sw_irq(std::uint32_t irq, bool asserted) noexcept {
    if (irq >= kMaxIrqs) return;
    std::lock_guard<std::mutex> lock(lock_);
    const std::size_t word = irq / 32;
    const std::uint32_t bit = 1u << (irq % 32);

    if (asserted) {
        sw_pending_bits_[word] |= bit;
    } else {
        sw_pending_bits_[word] &= ~bit;
    }
    update_core_irq_lines();
}

std::uint32_t AppleAIC::evaluate_highest_event(std::size_t core_id) const noexcept {
    if (core_id >= num_cores_ || !aic_enabled_) {
        return kEventNone;
    }

    const auto& c = cores_[core_id];

    // Priority 1: Architectural Timer Interrupt
    if (c.timer_pending) {
        return (kEventTypeTimer << 16) | 0x0000u;
    }

    // Priority 2: IPI
    if ((c.ipi_pending & ~c.ipi_mask) != 0) {
        return (kEventTypeIpi << 16) | 0x0001u;
    }

    // Priority 3: Hardware & Software IRQs
    const std::uint32_t core_mask = (1u << static_cast<std::uint32_t>(core_id));
    for (std::size_t w = 0; w < kNumIrqWords; ++w) {
        std::uint32_t active = (hw_pending_bits_[w] | sw_pending_bits_[w]) & ~mask_bits_[w];
        if (active != 0) {
            for (std::uint32_t b = 0; b < 32; ++b) {
                if ((active & (1u << b)) != 0) {
                    const std::uint32_t irq = static_cast<std::uint32_t>(w * 32 + b);
                    if ((target_cores_[irq] & core_mask) != 0) {
                        return (kEventTypeHw << 16) | irq;
                    }
                }
            }
        }
    }

    return kEventNone;
}

void AppleAIC::update_core_irq_lines() {
    for (std::size_t c = 0; c < num_cores_; ++c) {
        const std::uint32_t ev = evaluate_highest_event(c);
        cores_[c].irq_line_asserted = (ev != kEventNone);
    }
}

bool AppleAIC::has_pending_interrupt(std::size_t core_id) const noexcept {
    if (core_id >= num_cores_) return false;
    std::lock_guard<std::mutex> lock(lock_);
    return cores_[core_id].irq_line_asserted;
}

std::uint32_t AppleAIC::pop_event(std::size_t core_id) noexcept {
    if (core_id >= num_cores_) return kEventNone;
    std::lock_guard<std::mutex> lock(lock_);

    const std::uint32_t ev = evaluate_highest_event(core_id);
    if (ev != kEventNone) {
        cores_[core_id].active_event = ev;
    }
    return ev;
}

void AppleAIC::acknowledge_eoi(std::size_t core_id, std::uint32_t event_code) noexcept {
    if (core_id >= num_cores_) return;
    std::lock_guard<std::mutex> lock(lock_);

    const std::uint32_t ev_type = (event_code >> 16) & 0xFFFFu;
    const std::uint32_t ev_vector = event_code & 0xFFFFu;

    if (ev_type == kEventTypeTimer) {
        cores_[core_id].timer_pending = false;
    } else if (ev_type == kEventTypeIpi) {
        cores_[core_id].ipi_pending &= ~0x01u;
    } else if (ev_type == kEventTypeHw) {
        if (ev_vector < kMaxIrqs) {
            const std::size_t w = ev_vector / 32;
            const std::uint32_t bit = 1u << (ev_vector % 32);
            sw_pending_bits_[w] &= ~bit;
        }
    }

    cores_[core_id].active_event = kEventNone;
    update_core_irq_lines();
}

std::uint64_t AppleAIC::read_register(std::uint64_t offset, unsigned width) {
    std::lock_guard<std::mutex> lock(lock_);
    (void)width; // Width handled or standard 32-bit register read

    // 1. Global registers
    if (offset == kRegInfo) {
        // [31:24] = nr_cpus, [23:16] = nr_dies (1), [15:0] = nr_irq (512)
        return (static_cast<std::uint64_t>(num_cores_) << 24) |
               (1ULL << 16) |
               static_cast<std::uint64_t>(kMaxIrqs);
    }
    if (offset == kRegConfig) {
        return aic_enabled_ ? 0x01ULL : 0x00ULL;
    }
    if (offset == kRegIpiSend) {
        return 0ULL;
    }

    // 2. Per-core registers: 0x2000 .. 0x2400
    if (offset >= kRegCoreBase && offset < kRegTargetBase) {
        const std::uint64_t rel = offset - kRegCoreBase;
        std::size_t core = rel / kCoreStride;
        if (core >= num_cores_) {
            core = current_core_;
        }
        const std::uint64_t core_off = rel % kCoreStride;

        switch (core_off) {
        case kCoreOffWhoAmI:
            return static_cast<std::uint64_t>(core);
        case kCoreOffEvent:
            return evaluate_highest_event(core);
        case kCoreOffIpiAck:
            return static_cast<std::uint64_t>(cores_[core].ipi_pending);
        case kCoreOffIpiMask:
            return static_cast<std::uint64_t>(cores_[core].ipi_mask);
        default:
            return 0ULL;
        }
    }

    // 3. Target Routing: 0x4000 .. 0x47FF (4 bytes per IRQ)
    if (offset >= kRegTargetBase && offset < kRegTargetBase + (kMaxIrqs * 4)) {
        const std::uint32_t irq = static_cast<std::uint32_t>((offset - kRegTargetBase) / 4);
        return static_cast<std::uint64_t>(target_cores_[irq]);
    }

    // 4. Hardware IRQ Tables (16 words each)
    if (offset >= kRegMaskSetBase && offset < kRegMaskSetBase + (kNumIrqWords * 4)) {
        const std::size_t w = (offset - kRegMaskSetBase) / 4;
        return static_cast<std::uint64_t>(mask_bits_[w]);
    }
    if (offset >= kRegMaskClrBase && offset < kRegMaskClrBase + (kNumIrqWords * 4)) {
        const std::size_t w = (offset - kRegMaskClrBase) / 4;
        return static_cast<std::uint64_t>(mask_bits_[w]);
    }
    if (offset >= kRegHwPendingBase && offset < kRegHwPendingBase + (kNumIrqWords * 4)) {
        const std::size_t w = (offset - kRegHwPendingBase) / 4;
        return static_cast<std::uint64_t>(hw_pending_bits_[w] & ~mask_bits_[w]);
    }
    if (offset >= kRegHwStateBase && offset < kRegHwStateBase + (kNumIrqWords * 4)) {
        const std::size_t w = (offset - kRegHwStateBase) / 4;
        return static_cast<std::uint64_t>(hw_pending_bits_[w]);
    }
    if (offset >= kRegSwSetBase && offset < kRegSwSetBase + (kNumIrqWords * 4)) {
        const std::size_t w = (offset - kRegSwSetBase) / 4;
        return static_cast<std::uint64_t>(sw_pending_bits_[w]);
    }
    if (offset >= kRegSwClrBase && offset < kRegSwClrBase + (kNumIrqWords * 4)) {
        const std::size_t w = (offset - kRegSwClrBase) / 4;
        return static_cast<std::uint64_t>(sw_pending_bits_[w]);
    }

    return 0ULL;
}

void AppleAIC::write_register(std::uint64_t offset, std::uint64_t value, unsigned width) {
    std::lock_guard<std::mutex> lock(lock_);
    (void)width;
    const auto val32 = static_cast<std::uint32_t>(value);

    // 1. Global registers
    if (offset == kRegConfig) {
        aic_enabled_ = (val32 & 0x01u) != 0;
        update_core_irq_lines();
        return;
    }
    if (offset == kRegIpiSend) {
        for (std::size_t c = 0; c < num_cores_; ++c) {
            if ((val32 & (1u << c)) != 0) {
                cores_[c].ipi_pending |= 0x01u;
            }
        }
        update_core_irq_lines();
        return;
    }

    // 2. Per-core registers: 0x2000 .. 0x2400
    if (offset >= kRegCoreBase && offset < kRegTargetBase) {
        const std::uint64_t rel = offset - kRegCoreBase;
        std::size_t core = rel / kCoreStride;
        if (core >= num_cores_) {
            core = current_core_;
        }
        const std::uint64_t core_off = rel % kCoreStride;

        switch (core_off) {
        case kCoreOffWhoAmI:
            current_core_ = (val32 < num_cores_) ? val32 : 0;
            break;
        case kCoreOffEvent:
            // Writing to event register signals EOI
            {
                const std::uint32_t ev_type = (val32 >> 16) & 0xFFFFu;
                const std::uint32_t ev_vector = val32 & 0xFFFFu;
                if (ev_type == kEventTypeTimer) {
                    cores_[core].timer_pending = false;
                } else if (ev_type == kEventTypeIpi) {
                    cores_[core].ipi_pending &= ~0x01u;
                } else if (ev_type == kEventTypeHw && ev_vector < kMaxIrqs) {
                    const std::size_t w = ev_vector / 32;
                    sw_pending_bits_[w] &= ~(1u << (ev_vector % 32));
                }
                cores_[core].active_event = kEventNone;
                update_core_irq_lines();
            }
            break;
        case kCoreOffIpiAck:
            cores_[core].ipi_pending &= ~val32;
            update_core_irq_lines();
            break;
        case kCoreOffIpiMask:
            cores_[core].ipi_mask = val32;
            update_core_irq_lines();
            break;
        default:
            break;
        }
        return;
    }

    // 3. Target Routing: 0x4000 .. 0x47FF
    if (offset >= kRegTargetBase && offset < kRegTargetBase + (kMaxIrqs * 4)) {
        const std::uint32_t irq = static_cast<std::uint32_t>((offset - kRegTargetBase) / 4);
        target_cores_[irq] = val32;
        update_core_irq_lines();
        return;
    }

    // 4. Hardware IRQ Masking & Control
    if (offset >= kRegMaskSetBase && offset < kRegMaskSetBase + (kNumIrqWords * 4)) {
        const std::size_t w = (offset - kRegMaskSetBase) / 4;
        mask_bits_[w] |= val32; // Write 1 sets mask bit (masks)
        update_core_irq_lines();
        return;
    }
    if (offset >= kRegMaskClrBase && offset < kRegMaskClrBase + (kNumIrqWords * 4)) {
        const std::size_t w = (offset - kRegMaskClrBase) / 4;
        mask_bits_[w] &= ~val32; // Write 1 clears mask bit (unmasks)
        update_core_irq_lines();
        return;
    }
    if (offset >= kRegSwSetBase && offset < kRegSwSetBase + (kNumIrqWords * 4)) {
        const std::size_t w = (offset - kRegSwSetBase) / 4;
        sw_pending_bits_[w] |= val32;
        update_core_irq_lines();
        return;
    }
    if (offset >= kRegSwClrBase && offset < kRegSwClrBase + (kNumIrqWords * 4)) {
        const std::size_t w = (offset - kRegSwClrBase) / 4;
        sw_pending_bits_[w] &= ~val32;
        update_core_irq_lines();
        return;
    }
}

std::vector<std::uint8_t> AppleAIC::serialize_state() const {
    std::lock_guard<std::mutex> lock(lock_);
    std::vector<std::uint8_t> out;
    out.reserve(2048);

    const std::uint32_t magic = 0x41494332u; // 'AIC2'
    const std::uint32_t version = 1u;
    const std::uint32_t cores_u32 = static_cast<std::uint32_t>(num_cores_);

    auto append_u32 = [&](std::uint32_t v) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
        out.insert(out.end(), p, p + 4);
    };

    append_u32(magic);
    append_u32(version);
    append_u32(cores_u32);
    append_u32(aic_enabled_ ? 1u : 0u);

    for (std::uint32_t w : mask_bits_) append_u32(w);
    for (std::uint32_t w : hw_pending_bits_) append_u32(w);
    for (std::uint32_t w : sw_pending_bits_) append_u32(w);
    for (std::uint32_t t : target_cores_) append_u32(t);

    for (std::size_t c = 0; c < kMaxCores; ++c) {
        append_u32(cores_[c].ipi_pending);
        append_u32(cores_[c].ipi_mask);
        append_u32(cores_[c].timer_pending ? 1u : 0u);
        append_u32(cores_[c].active_event);
    }

    return out;
}

bool AppleAIC::deserialize_state(const std::uint8_t* data, std::size_t size) {
    if (!data || size < 16) return false;
    std::lock_guard<std::mutex> lock(lock_);

    const auto* p = data;
    auto read_u32 = [&]() -> std::uint32_t {
        std::uint32_t v = 0;
        std::memcpy(&v, p, 4);
        p += 4;
        return v;
    };

    const std::uint32_t magic = read_u32();
    const std::uint32_t version = read_u32();
    if (magic != 0x41494332u || version != 1u) return false;

    const std::uint32_t cores_u32 = read_u32();
    aic_enabled_ = (read_u32() != 0);
    num_cores_ = std::clamp<std::size_t>(cores_u32, 1, kMaxCores);

    for (std::size_t i = 0; i < kNumIrqWords; ++i) mask_bits_[i] = read_u32();
    for (std::size_t i = 0; i < kNumIrqWords; ++i) hw_pending_bits_[i] = read_u32();
    for (std::size_t i = 0; i < kNumIrqWords; ++i) sw_pending_bits_[i] = read_u32();
    for (std::size_t i = 0; i < kMaxIrqs; ++i) target_cores_[i] = read_u32();

    for (std::size_t c = 0; c < kMaxCores; ++c) {
        cores_[c].ipi_pending = read_u32();
        cores_[c].ipi_mask = read_u32();
        cores_[c].timer_pending = (read_u32() != 0);
        cores_[c].active_event = read_u32();
    }

    update_core_irq_lines();
    return true;
}

} // namespace winios::device

#include "device/apple_pmgr.h"

#include <algorithm>
#include <cstring>

namespace winios::device {

ApplePMGR::ApplePMGR(std::uint32_t device_id,
                     std::uint64_t mmio_base,
                     std::uint64_t mmio_size,
                     std::uint32_t chip_id)
    : VirtualDevice(device_id, "ApplePMGR", VirtualDeviceType::GenericMmio, mmio_base, mmio_size),
      chip_id_(chip_id) {
    reset();
}

void ApplePMGR::initialize_default_domains() {
    domain_states_.clear();

    // Default power-on state: essential SoC blocks powered on (0xF = Full)
    domain_states_[kDomainCpuCluster0] = kPowerStateFull;
    domain_states_[kDomainCpuCluster1] = kPowerStateFull;
    domain_states_[kDomainAic]         = kPowerStateFull;
    domain_states_[kDomainUart0]       = kPowerStateFull;
    domain_states_[kDomainDisplay]     = kPowerStateFull;
    domain_states_[kDomainGpu]         = kPowerStateActive;
    domain_states_[kDomainAne]         = kPowerStateActive;
    domain_states_[kDomainMedia]       = kPowerStateActive;
}

void ApplePMGR::reset() noexcept {
    std::lock_guard<std::mutex> lock(lock_);
    sys_status_ = 1u; // System Active
    reset_requested_ = false;
    initialize_default_domains();
}

bool ApplePMGR::is_domain_powered(std::uint64_t offset) const noexcept {
    std::lock_guard<std::mutex> lock(lock_);
    auto it = domain_states_.find(offset);
    if (it == domain_states_.end()) {
        return false;
    }
    return it->second > kPowerStateOff;
}

std::uint32_t ApplePMGR::get_domain_power_state(std::uint64_t offset) const noexcept {
    std::lock_guard<std::mutex> lock(lock_);
    auto it = domain_states_.find(offset);
    if (it == domain_states_.end()) {
        return kPowerStateOff;
    }
    return it->second;
}

void ApplePMGR::set_domain_power_state(std::uint64_t offset, std::uint32_t state) noexcept {
    std::lock_guard<std::mutex> lock(lock_);
    domain_states_[offset] = (state & 0x0Fu);
}

std::uint64_t ApplePMGR::read_register(std::uint64_t offset, unsigned width) {
    std::lock_guard<std::mutex> lock(lock_);
    (void)width;

    if (offset == kRegChipId) {
        return static_cast<std::uint64_t>(chip_id_);
    }
    if (offset == kRegRevision) {
        return static_cast<std::uint64_t>(kPmgrRevision);
    }
    if (offset == kRegSysStatus) {
        return static_cast<std::uint64_t>(sys_status_);
    }
    if (offset == kRegResetTrigger) {
        return reset_requested_ ? 1ULL : 0ULL;
    }

    // Power Gate registers:
    auto it = domain_states_.find(offset);
    if (it != domain_states_.end()) {
        const std::uint32_t state = it->second & 0x0Fu;
        // bits [3:0] = target state
        // bits [7:4] = current actual state (matches target immediately in VM)
        // bit [8]    = clock stable / PLL lock flag (1 if powered, 0 if off)
        const std::uint32_t reg_val = state | (state << 4) | ((state != 0) ? 0x100u : 0u);
        return static_cast<std::uint64_t>(reg_val);
    }

    // Unmapped / generic PMGR space reads as 0
    return 0ULL;
}

void ApplePMGR::write_register(std::uint64_t offset, std::uint64_t value, unsigned width) {
    std::lock_guard<std::mutex> lock(lock_);
    (void)width;
    const auto val32 = static_cast<std::uint32_t>(value);

    if (offset == kRegResetTrigger) {
        // Darwin triggers reboot by writing non-zero or reset magic (0xDEADBEEF)
        if (val32 != 0) {
            reset_requested_ = true;
        }
        return;
    }

    if (offset == kRegSysStatus) {
        sys_status_ = val32;
        return;
    }

    // Power Gate write: software updates desired power state in bits [3:0]
    if (offset >= 0x0100u && offset < 0x8000u) {
        const std::uint32_t target_state = val32 & 0x0Fu;
        domain_states_[offset] = target_state;
        return;
    }
}

std::vector<std::uint8_t> ApplePMGR::serialize_state() const {
    std::lock_guard<std::mutex> lock(lock_);
    std::vector<std::uint8_t> out;
    out.reserve(512);

    const std::uint32_t magic = 0x504D4752u; // 'PMGR'
    const std::uint32_t version = 1u;

    auto append_u32 = [&](std::uint32_t v) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
        out.insert(out.end(), p, p + 4);
    };
    auto append_u64 = [&](std::uint64_t v) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
        out.insert(out.end(), p, p + 8);
    };

    append_u32(magic);
    append_u32(version);
    append_u32(chip_id_);
    append_u32(sys_status_);
    append_u32(reset_requested_ ? 1u : 0u);

    // Write domain count and entries
    const auto domain_count = static_cast<std::uint32_t>(domain_states_.size());
    append_u32(domain_count);

    for (const auto& [off, state] : domain_states_) {
        append_u64(off);
        append_u32(state);
    }

    return out;
}

bool ApplePMGR::deserialize_state(const std::uint8_t* data, std::size_t size) {
    if (!data || size < 24) return false;
    std::lock_guard<std::mutex> lock(lock_);

    const auto* p = data;
    const auto* end = data + size;

    auto read_u32 = [&]() -> std::uint32_t {
        std::uint32_t v = 0;
        std::memcpy(&v, p, 4);
        p += 4;
        return v;
    };
    auto read_u64 = [&]() -> std::uint64_t {
        std::uint64_t v = 0;
        std::memcpy(&v, p, 8);
        p += 8;
        return v;
    };

    const std::uint32_t magic = read_u32();
    const std::uint32_t version = read_u32();
    if (magic != 0x504D4752u || version != 1u) return false;

    chip_id_ = read_u32();
    sys_status_ = read_u32();
    reset_requested_ = (read_u32() != 0);

    const std::uint32_t count = read_u32();
    domain_states_.clear();

    for (std::uint32_t i = 0; i < count; ++i) {
        if (p + 12 > end) return false;
        std::uint64_t off = read_u64();
        std::uint32_t state = read_u32();
        domain_states_[off] = state;
    }

    return true;
}

} // namespace winios::device

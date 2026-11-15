#include "memory/unified_memory.h"
#include "cpu/mmu.h"
#include "cpu/smp_exclusive_monitor.h"

#include <algorithm>
#include <cstring>

namespace winios::memory {

UnifiedMemoryView::UnifiedMemoryView(Memory& memory,
                                     mmu::MMU* mmu,
                                     unsigned current_el,
                                     bool architectural,
                                     std::uint64_t instruction_address,
                                     std::uint32_t instruction,
                                     bool is_atomic)
    : memory_(memory),
      mmu_(mmu),
      current_el_(current_el),
      architectural_(architectural),
      instruction_address_(instruction_address),
      instruction_(instruction),
      is_atomic_(is_atomic) {}

std::uint64_t UnifiedMemoryView::page_mask_for_va(std::uint64_t va) const noexcept {
    if (!mmu_) {
        return ~4095ULL;
    }
    const bool s1 = mmu_->is_stage1_enabled();
    const bool s2 = mmu_->is_stage2_enabled(current_el_);
    if (!s1 || s2) {
        return ~4095ULL;
    }
    const auto granule = ((va >> 55u) & 1ULL) != 0
        ? mmu_->get_tg1_granule()
        : mmu_->get_tg0_granule();
    return ~(mmu::MMU::get_granule_size(granule) - 1ULL);
}

void UnifiedMemoryView::check_address_size_and_range(std::uint64_t address,
                                                    unsigned check_bytes,
                                                    AccessType access) const {
    if (address >= kPhysicalAddressLimit) {
        fault(address, access, FaultStatus::AddressSizeLevel0, 0);
    }
    const std::uint64_t size = static_cast<std::uint64_t>(memory_.size());
    if (address > size || static_cast<std::uint64_t>(check_bytes) > size - address) {
        fault(address, access, FaultStatus::SynchronousExternal, 0);
    }
}

std::uint64_t UnifiedMemoryView::translate_or_fault(std::uint64_t address,
                                                    unsigned alignment_width,
                                                    unsigned check_bytes,
                                                    AccessType access,
                                                    bool require_natural_align,
                                                    bool unaligned_override,
                                                    bool reject_device) const {
    if (!architectural_) {
        return address;
    }
    if (!mmu_ || (!mmu_->is_stage1_enabled() && !mmu_->is_stage2_enabled(current_el_))) {
        if (alignment_width > 1u && (address & (std::uint64_t{alignment_width} - 1u)) != 0u) {
            fault(address, access, FaultStatus::Alignment, alignment_width);
        }
        check_address_size_and_range(address, check_bytes, access);
        return address;
    }

    const bool is_unaligned = unaligned_override ||
        (alignment_width > 1u && (address & (std::uint64_t{alignment_width} - 1u)) != 0u);
    if ((require_natural_align || mmu_->is_alignment_check_enabled()) && is_unaligned) {
        fault(address, access, FaultStatus::Alignment, alignment_width);
    }

    mmu::TranslationFault mmu_fault;
    bool is_device = false;
    auto pa_opt = mmu_->translate(
        memory_, address,
        (access == AccessType::Write) ? mmu::AccessKind::Write : mmu::AccessKind::Read,
        current_el_,
        &mmu_fault,
        &is_device);
    if (!pa_opt.has_value()) {
        if (mmu_fault.is_stage2) {
            SynchronousException e = make_data_abort(
                false /* from lower EL */, access, static_cast<FaultStatus>(mmu_fault.fsc()),
                address, instruction_address_, alignment_width);
            e.has_fault_ipa = true;
            e.fault_ipa = mmu_fault.fault_ipa;
            e.target_el = 2;
            throw SynchronousExceptionSignal{e};
        }
        fault(address, access, static_cast<FaultStatus>(mmu_fault.fsc()), alignment_width);
    }

    if (is_device && (reject_device || is_unaligned)) {
        fault(address, access, FaultStatus::Alignment, alignment_width);
    }
    const std::uint64_t pa = pa_opt.value();
    const std::uint64_t size = static_cast<std::uint64_t>(memory_.size());
    if (pa > size || static_cast<std::uint64_t>(check_bytes) > size - pa) {
        fault(address, access, FaultStatus::SynchronousExternal, alignment_width);
    }
    return pa;
}

void UnifiedMemoryView::translate_access_pages(std::uint64_t address, unsigned width,
                                               AccessType access, bool enforce_alignment,
                                               bool require_natural_align, bool reject_device,
                                               std::uint64_t& pa1, unsigned& first_bytes,
                                               std::uint64_t& pa2, unsigned& second_bytes) const {
    if (!architectural_) {
        pa1 = address;
        first_bytes = width;
        pa2 = 0;
        second_bytes = 0;
        return;
    }
    if (!mmu_ || (!mmu_->is_stage1_enabled() && !mmu_->is_stage2_enabled(current_el_))) {
        pa1 = translate_or_fault(address, enforce_alignment ? width : 1u, width, access,
                                 require_natural_align, false, reject_device);
        first_bytes = width;
        pa2 = 0;
        second_bytes = 0;
        return;
    }
    const std::uint64_t page_mask = page_mask_for_va(address);
    const std::uint64_t page1 = address & page_mask;
    const std::uint64_t page2 = (width > 1u) ? ((address + width - 1u) & page_mask) : page1;
    if (page1 == page2) {
        pa1 = translate_or_fault(address, enforce_alignment ? width : 1u, width, access,
                                 require_natural_align, false, reject_device);
        first_bytes = width;
        pa2 = 0;
        second_bytes = 0;
        return;
    }
    first_bytes = static_cast<unsigned>(page2 - address);
    second_bytes = width - first_bytes;
    const unsigned align_w = enforce_alignment ? width : 1u;
    pa1 = translate_or_fault(address, align_w, first_bytes, access,
                             require_natural_align, false, reject_device);
    const bool orig_unaligned = enforce_alignment && width > 1u &&
        (address & (std::uint64_t{width} - 1u)) != 0u;
    pa2 = translate_or_fault(page2, align_w, second_bytes, access,
                             false, orig_unaligned, reject_device);
}

void UnifiedMemoryView::check_cache_range(std::uint64_t va, std::size_t size,
                                          AccessType access, bool reject_device) const {
    if (!architectural_) {
        return;
    }
    std::uint64_t pa1 = 0;
    std::uint64_t pa2 = 0;
    unsigned first_bytes = 0;
    unsigned second_bytes = 0;
    translate_access_pages(va, static_cast<unsigned>(size), access, false,
                           false, reject_device, pa1, first_bytes, pa2, second_bytes);
}

void UnifiedMemoryView::cache_zero_block(std::uint64_t block_va, std::size_t size) {
    // ARM DDI 0487 C6.2.69 / D8.2.3: DC ZVA to Device memory faults with Alignment.
    std::uint64_t pa1 = 0;
    std::uint64_t pa2 = 0;
    unsigned first_bytes = 0;
    unsigned second_bytes = 0;
    translate_access_pages(block_va, static_cast<unsigned>(size), AccessType::Write, false,
                           false, true, pa1, first_bytes, pa2, second_bytes);

    std::unique_lock<std::recursive_mutex> lock;
    if (store_mutex_) {
        lock = std::unique_lock<std::recursive_mutex>(*store_mutex_);
    }

    if (second_bytes == 0u) {
        for (std::size_t offset = 0; offset < size; offset += 8u) {
            memory_.write_u64(static_cast<std::size_t>(pa1 + offset), 0ULL);
        }
        if (monitor_) {
            monitor_->invalidate_reservations(core_id_, pa1, size);
        }
    } else {
        for (std::size_t offset = 0; offset < first_bytes; ++offset) {
            memory_.write_u8(static_cast<std::size_t>(pa1 + offset), 0u);
        }
        for (std::size_t offset = 0; offset < second_bytes; ++offset) {
            memory_.write_u8(static_cast<std::size_t>(pa2 + offset), 0u);
        }
        if (monitor_) {
            monitor_->invalidate_reservations(core_id_, pa1, first_bytes);
            monitor_->invalidate_reservations(core_id_, pa2, second_bytes);
        }
    }
}

std::uint8_t UnifiedMemoryView::read_u8(std::uint64_t va) const {
    const std::uint64_t pa = translate_or_fault(va, 1u, 1u, AccessType::Read);
    return memory_.read_u8(static_cast<std::size_t>(pa));
}

std::uint16_t UnifiedMemoryView::read_u16(std::uint64_t va) const {
    std::uint64_t pa1 = 0, pa2 = 0;
    unsigned first_bytes = 0, second_bytes = 0;
    translate_access_pages(va, 2u, AccessType::Read, true, false, false,
                           pa1, first_bytes, pa2, second_bytes);
    if (second_bytes == 0u) {
        return memory_.read_u16(static_cast<std::size_t>(pa1));
    }
    std::uint16_t val = 0;
    val |= static_cast<std::uint16_t>(memory_.read_u8(static_cast<std::size_t>(pa1)));
    val |= static_cast<std::uint16_t>(memory_.read_u8(static_cast<std::size_t>(pa2))) << 8u;
    return val;
}

std::uint32_t UnifiedMemoryView::read_u32(std::uint64_t va) const {
    std::uint64_t pa1 = 0, pa2 = 0;
    unsigned first_bytes = 0, second_bytes = 0;
    translate_access_pages(va, 4u, AccessType::Read, true, false, false,
                           pa1, first_bytes, pa2, second_bytes);
    if (second_bytes == 0u) {
        return memory_.read_u32(static_cast<std::size_t>(pa1));
    }
    std::uint32_t val = 0;
    for (unsigned i = 0; i < first_bytes; ++i) {
        val |= static_cast<std::uint32_t>(memory_.read_u8(static_cast<std::size_t>(pa1 + i))) << (i * 8u);
    }
    for (unsigned i = 0; i < second_bytes; ++i) {
        val |= static_cast<std::uint32_t>(memory_.read_u8(static_cast<std::size_t>(pa2 + i))) << ((first_bytes + i) * 8u);
    }
    return val;
}

std::uint64_t UnifiedMemoryView::read_u64(std::uint64_t va) const {
    std::uint64_t pa1 = 0, pa2 = 0;
    unsigned first_bytes = 0, second_bytes = 0;
    translate_access_pages(va, 8u, AccessType::Read, true, false, false,
                           pa1, first_bytes, pa2, second_bytes);
    if (second_bytes == 0u) {
        return memory_.read_u64(static_cast<std::size_t>(pa1));
    }
    std::uint64_t val = 0;
    for (unsigned i = 0; i < first_bytes; ++i) {
        val |= static_cast<std::uint64_t>(memory_.read_u8(static_cast<std::size_t>(pa1 + i))) << (i * 8u);
    }
    for (unsigned i = 0; i < second_bytes; ++i) {
        val |= static_cast<std::uint64_t>(memory_.read_u8(static_cast<std::size_t>(pa2 + i))) << ((first_bytes + i) * 8u);
    }
    return val;
}

void UnifiedMemoryView::read_bytes(std::uint64_t va, std::uint8_t* dst, std::size_t size) const {
    if (!dst || size == 0) return;
    for (std::size_t i = 0; i < size; ++i) {
        dst[i] = read_u8(va + i);
    }
}

void UnifiedMemoryView::write_u8(std::uint64_t va, std::uint8_t value) {
    const std::uint64_t pa = translate_or_fault(va, 1u, 1u, AccessType::Write);
    memory_.write_u8(static_cast<std::size_t>(pa), value);
    if (monitor_) {
        monitor_->invalidate_reservations(core_id_, pa, 1);
    }
}

void UnifiedMemoryView::write_u16(std::uint64_t va, std::uint16_t value) {
    std::uint64_t pa1 = 0, pa2 = 0;
    unsigned first_bytes = 0, second_bytes = 0;
    translate_access_pages(va, 2u, AccessType::Write, true, false, false,
                           pa1, first_bytes, pa2, second_bytes);
    if (second_bytes == 0u) {
        memory_.write_u16(static_cast<std::size_t>(pa1), value);
        if (monitor_) {
            monitor_->invalidate_reservations(core_id_, pa1, 2);
        }
    } else {
        memory_.write_u8(static_cast<std::size_t>(pa1), static_cast<std::uint8_t>(value & 0xFFu));
        memory_.write_u8(static_cast<std::size_t>(pa2), static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
        if (monitor_) {
            monitor_->invalidate_reservations(core_id_, pa1, first_bytes);
            monitor_->invalidate_reservations(core_id_, pa2, second_bytes);
        }
    }
}

void UnifiedMemoryView::write_u32(std::uint64_t va, std::uint32_t value) {
    std::uint64_t pa1 = 0, pa2 = 0;
    unsigned first_bytes = 0, second_bytes = 0;
    translate_access_pages(va, 4u, AccessType::Write, true, false, false,
                           pa1, first_bytes, pa2, second_bytes);
    if (second_bytes == 0u) {
        memory_.write_u32(static_cast<std::size_t>(pa1), value);
        if (monitor_) {
            monitor_->invalidate_reservations(core_id_, pa1, 4);
        }
    } else {
        for (unsigned i = 0; i < first_bytes; ++i) {
            memory_.write_u8(static_cast<std::size_t>(pa1 + i), static_cast<std::uint8_t>((value >> (i * 8u)) & 0xFFu));
        }
        for (unsigned i = 0; i < second_bytes; ++i) {
            memory_.write_u8(static_cast<std::size_t>(pa2 + i), static_cast<std::uint8_t>((value >> ((first_bytes + i) * 8u)) & 0xFFu));
        }
        if (monitor_) {
            monitor_->invalidate_reservations(core_id_, pa1, first_bytes);
            monitor_->invalidate_reservations(core_id_, pa2, second_bytes);
        }
    }
}

void UnifiedMemoryView::write_u64(std::uint64_t va, std::uint64_t value) {
    std::uint64_t pa1 = 0, pa2 = 0;
    unsigned first_bytes = 0, second_bytes = 0;
    translate_access_pages(va, 8u, AccessType::Write, true, false, false,
                           pa1, first_bytes, pa2, second_bytes);
    if (second_bytes == 0u) {
        memory_.write_u64(static_cast<std::size_t>(pa1), value);
        if (monitor_) {
            monitor_->invalidate_reservations(core_id_, pa1, 8);
        }
    } else {
        for (unsigned i = 0; i < first_bytes; ++i) {
            memory_.write_u8(static_cast<std::size_t>(pa1 + i), static_cast<std::uint8_t>((value >> (i * 8u)) & 0xFFu));
        }
        for (unsigned i = 0; i < second_bytes; ++i) {
            memory_.write_u8(static_cast<std::size_t>(pa2 + i), static_cast<std::uint8_t>((value >> ((first_bytes + i) * 8u)) & 0xFFu));
        }
        if (monitor_) {
            monitor_->invalidate_reservations(core_id_, pa1, first_bytes);
            monitor_->invalidate_reservations(core_id_, pa2, second_bytes);
        }
    }
}

void UnifiedMemoryView::write_bytes(std::uint64_t va, const std::uint8_t* src, std::size_t size) {
    if (!src || size == 0) return;
    for (std::size_t i = 0; i < size; ++i) {
        write_u8(va + i, src[i]);
    }
}

std::uint8_t UnifiedMemoryView::read_phys_u8(std::uint64_t pa) const {
    return memory_.read_u8(static_cast<std::size_t>(pa));
}

std::uint16_t UnifiedMemoryView::read_phys_u16(std::uint64_t pa) const {
    return memory_.read_u16(static_cast<std::size_t>(pa));
}

std::uint32_t UnifiedMemoryView::read_phys_u32(std::uint64_t pa) const {
    return memory_.read_u32(static_cast<std::size_t>(pa));
}

std::uint64_t UnifiedMemoryView::read_phys_u64(std::uint64_t pa) const {
    return memory_.read_u64(static_cast<std::size_t>(pa));
}

void UnifiedMemoryView::read_phys_bytes(std::uint64_t pa, std::uint8_t* dst, std::size_t size) const {
    if (!dst || size == 0) return;
    for (std::size_t i = 0; i < size; ++i) {
        dst[i] = memory_.read_u8(static_cast<std::size_t>(pa + i));
    }
}

void UnifiedMemoryView::write_phys_u8(std::uint64_t pa, std::uint8_t value) {
    memory_.write_u8(static_cast<std::size_t>(pa), value);
    if (monitor_) {
        monitor_->invalidate_reservations(core_id_, pa, 1);
    }
}

void UnifiedMemoryView::write_phys_u16(std::uint64_t pa, std::uint16_t value) {
    memory_.write_u16(static_cast<std::size_t>(pa), value);
    if (monitor_) {
        monitor_->invalidate_reservations(core_id_, pa, 2);
    }
}

void UnifiedMemoryView::write_phys_u32(std::uint64_t pa, std::uint32_t value) {
    memory_.write_u32(static_cast<std::size_t>(pa), value);
    if (monitor_) {
        monitor_->invalidate_reservations(core_id_, pa, 4);
    }
}

void UnifiedMemoryView::write_phys_u64(std::uint64_t pa, std::uint64_t value) {
    memory_.write_u64(static_cast<std::size_t>(pa), value);
    if (monitor_) {
        monitor_->invalidate_reservations(core_id_, pa, 8);
    }
}

void UnifiedMemoryView::write_phys_bytes(std::uint64_t pa, const std::uint8_t* src, std::size_t size) {
    if (!src || size == 0) return;
    for (std::size_t i = 0; i < size; ++i) {
        memory_.write_u8(static_cast<std::size_t>(pa + i), src[i]);
    }
    if (monitor_) {
        monitor_->invalidate_reservations(core_id_, pa, size);
    }
}

void UnifiedMemoryView::invalidate_reservation(std::size_t core_id, std::uint64_t pa, std::size_t size) noexcept {
    if (monitor_) {
        monitor_->invalidate_reservations(core_id, pa, size);
    }
}

void UnifiedMemoryView::fault(std::uint64_t address, AccessType access,
                             FaultStatus status, unsigned width) const {
    const bool same_el = current_el_ != 0;
    throw SynchronousExceptionSignal{make_data_abort(
        same_el, access, status, address, instruction_address_, width)};
}

} // namespace winios::memory

#include "cpu/mmu.h"
#include "memory/memory.h"
#include <algorithm>

namespace mmu {

MMU::MMU() {
    reset();
}

void MMU::reset() noexcept {
    std::lock_guard<std::recursive_mutex> lock(tlb_mutex_);
    sctlr_el1_ = 0x00C50838ULL;
    tcr_el1_   = 0;
    ttbr0_el1_ = 0;
    ttbr1_el1_ = 0;
    mair_el1_  = 0;
    par_el1_   = 0;
    hcr_el2_   = 0;
    vttbr_el2_ = 0;
    vtcr_el2_  = 0x80003550ULL;
    invalidate_tlb_all();
}

void MMU::set_hcr_el2(std::uint64_t val) noexcept {
    hcr_el2_ = val;
    invalidate_tlb_all();
}

void MMU::set_vttbr_el2(std::uint64_t val) noexcept {
    vttbr_el2_ = val;
    invalidate_tlb_all();
}

void MMU::set_vtcr_el2(std::uint64_t val) noexcept {
    vtcr_el2_ = val;
    invalidate_tlb_all();
}

void MMU::set_sctlr_el1(std::uint64_t val) noexcept {
    sctlr_el1_ = val;
    invalidate_tlb_all();
}

void MMU::set_tcr_el1(std::uint64_t val) noexcept {
    tcr_el1_ = val;
    invalidate_tlb_all();
}

void MMU::set_ttbr0_el1(std::uint64_t val) noexcept {
    ttbr0_el1_ = val;
    invalidate_tlb_all();
}

void MMU::set_ttbr1_el1(std::uint64_t val) noexcept {
    ttbr1_el1_ = val;
    invalidate_tlb_all();
}

void MMU::set_mair_el1(std::uint64_t val) noexcept {
    mair_el1_ = val;
}

void MMU::set_par_el1(std::uint64_t val) noexcept {
    par_el1_ = val;
}

std::uint8_t MMU::get_mair_attribute(std::uint8_t attr_indx) const noexcept {
    return static_cast<std::uint8_t>((mair_el1_ >> ((attr_indx & 0x7u) * 8u)) & 0xFFu);
}

bool MMU::is_device_attribute(std::uint8_t attr_byte) const noexcept {
    // In ARMv8, Device memory encodings have upper nibble 0000:
    // 0x00 = Device-nGnRnE, 0x04 = Device-nGnRE, 0x08 = Device-nGRE, 0x0C = Device-GRE
    return (attr_byte & 0xF0u) == 0x00u;
}

bool MMU::validate_descriptor(
    unsigned level,
    std::uint64_t desc,
    bool is_stage2,
    DescriptorKind* out_kind,
    MmuFaultType* out_fault) const noexcept {

    // Bit 0: Valid bit. If 0, descriptor is invalid -> Translation fault
    if ((desc & 0x1ULL) == 0) {
        if (out_kind) *out_kind = DescriptorKind::Invalid;
        if (out_fault) *out_fault = MmuFaultType::Translation;
        return false;
    }

    const std::uint64_t type_bits = desc & 0x3ULL;

    // In ARMv8 48-bit PA architecture, physical address bits [51:48] must be 0
    if ((desc & 0x000F000000000000ULL) != 0) {
        if (out_kind) *out_kind = DescriptorKind::Invalid;
        if (out_fault) *out_fault = MmuFaultType::AddressSize;
        return false;
    }

    switch (level) {
    case 0:
        // At Level 0 for 4KB granule, only Table descriptors (0b11) are valid.
        // Block descriptors (0b01) are reserved at Level 0 in 4KB granule.
        if (type_bits == 0x3ULL) {
            if (out_kind) *out_kind = DescriptorKind::Table;
            return true;
        }
        if (out_kind) *out_kind = DescriptorKind::Invalid;
        if (out_fault) *out_fault = MmuFaultType::Translation;
        return false;

    case 1:
        if (type_bits == 0x1ULL) {
            if (out_kind) *out_kind = DescriptorKind::Block;
            return true;
        }
        if (type_bits == 0x3ULL) {
            if (out_kind) *out_kind = DescriptorKind::Table;
            return true;
        }
        if (out_kind) *out_kind = DescriptorKind::Invalid;
        if (out_fault) *out_fault = MmuFaultType::Translation;
        return false;

    case 2:
        if (type_bits == 0x1ULL) {
            if (out_kind) *out_kind = DescriptorKind::Block;
            return true;
        }
        if (type_bits == 0x3ULL) {
            if (out_kind) *out_kind = DescriptorKind::Table;
            return true;
        }
        if (out_kind) *out_kind = DescriptorKind::Invalid;
        if (out_fault) *out_fault = MmuFaultType::Translation;
        return false;

    case 3:
        if (is_stage2) {
            if ((desc & 0x1ULL) != 0) {
                if (out_kind) *out_kind = DescriptorKind::Page;
                return true;
            }
        } else {
            // At Level 3 Stage 1, only Page descriptors (0b11) are valid.
            // 0b01 is reserved at Level 3 Stage 1.
            if (type_bits == 0x3ULL) {
                if (out_kind) *out_kind = DescriptorKind::Page;
                return true;
            }
        }
        if (out_kind) *out_kind = DescriptorKind::Invalid;
        if (out_fault) *out_fault = MmuFaultType::Translation;
        return false;

    default:
        if (out_kind) *out_kind = DescriptorKind::Invalid;
        if (out_fault) *out_fault = MmuFaultType::Translation;
        return false;
    }
}

void MMU::invalidate_tlb_all() noexcept {
    std::lock_guard<std::recursive_mutex> lock(tlb_mutex_);
    tlb_.clear();
}

void MMU::invalidate_tlb_va(std::uint64_t va, std::uint16_t asid, bool /*last_level_only*/) noexcept {
    std::lock_guard<std::recursive_mutex> lock(tlb_mutex_);
    const std::uint64_t vpn = va >> 12u;
    for (auto it = tlb_.begin(); it != tlb_.end();) {
        if (it->valid && it->non_global && it->asid == asid && (it->virtual_page == (vpn & (it->page_mask >> 12u)))) {
            it = tlb_.erase(it);
        } else {
            ++it;
        }
    }
}

void MMU::invalidate_tlb_asid(std::uint16_t asid) noexcept {
    std::lock_guard<std::recursive_mutex> lock(tlb_mutex_);
    for (auto it = tlb_.begin(); it != tlb_.end();) {
        if (it->valid && it->non_global && it->asid == asid) {
            it = tlb_.erase(it);
        } else {
            ++it;
        }
    }
}

void MMU::invalidate_tlb_vaa(std::uint64_t va, bool /*last_level_only*/) noexcept {
    std::lock_guard<std::recursive_mutex> lock(tlb_mutex_);
    const std::uint64_t vpn = va >> 12u;
    for (auto it = tlb_.begin(); it != tlb_.end();) {
        if (it->valid && (it->virtual_page == (vpn & (it->page_mask >> 12u)))) {
            it = tlb_.erase(it);
        } else {
            ++it;
        }
    }
}

void MMU::invalidate_tlb_vmid(std::uint16_t vmid) noexcept {
    std::lock_guard<std::recursive_mutex> lock(tlb_mutex_);
    for (auto it = tlb_.begin(); it != tlb_.end();) {
        if (it->valid && it->vmid == vmid) {
            it = tlb_.erase(it);
        } else {
            ++it;
        }
    }
}

void MMU::invalidate_tlb_ipas2(std::uint64_t ipa, std::uint16_t vmid) noexcept {
    std::lock_guard<std::recursive_mutex> lock(tlb_mutex_);
    const std::uint64_t vpn = ipa >> 12u;
    for (auto it = tlb_.begin(); it != tlb_.end();) {
        if (it->valid && it->is_stage2 && it->vmid == vmid && (it->virtual_page == (vpn & (it->page_mask >> 12u)))) {
            it = tlb_.erase(it);
        } else {
            ++it;
        }
    }
}

std::size_t MMU::get_tlb_entry_count() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(tlb_mutex_);
    return tlb_.size();
}

std::vector<TlbEntry> MMU::get_tlb_entries_copy() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(tlb_mutex_);
    return tlb_;
}

std::optional<std::uint64_t> MMU::translate(
    const Memory& memory,
    std::uint64_t virtual_address,
    AccessKind access,
    unsigned current_el,
    TranslationFault* out_fault,
    bool* out_is_device) {

    const bool s1_enabled = is_stage1_enabled();
    const bool s2_enabled = is_stage2_enabled(current_el);

    if (out_is_device) {
        *out_is_device = false;
    }

    if (!s1_enabled && !s2_enabled) {
        return virtual_address;
    }

    std::lock_guard<std::recursive_mutex> tlb_lock(tlb_mutex_);

    // Determine effective VA according to TBI0/TBI1
    const bool is_ttbr1_req = (tcr_el1_ & ((1ULL << 37u) | (1ULL << 38u)))
        ? (((virtual_address >> 55u) & 1u) != 0)
        : ((virtual_address >> 63u) != 0);
    std::uint64_t eff_va = virtual_address;
    if (!is_ttbr1_req && (tcr_el1_ & (1ULL << 37u))) {
        eff_va &= 0x00FFFFFFFFFFFFFFULL;
    } else if (is_ttbr1_req && (tcr_el1_ & (1ULL << 38u))) {
        eff_va |= 0xFF00000000000000ULL;
    }

    std::uint64_t ipa = eff_va;
    if (s1_enabled) {
        bool s1_hit = false;
        if (tlb_enabled_) {
            // ASID extraction: TCR_EL1.A1 selects TTBR1 vs TTBR0 ASID
            const std::uint16_t current_asid = static_cast<std::uint16_t>(
                ((tcr_el1_ & (1ULL << 22u)) ? (ttbr1_el1_ >> 48u) : (ttbr0_el1_ >> 48u)) & 0xFFFFu);
            const std::uint16_t current_vmid = static_cast<std::uint16_t>((vttbr_el2_ >> 48u) & 0xFFFFu);

            for (const auto& entry : tlb_) {
                if (!entry.valid || entry.is_stage2) continue;
                if (s2_enabled && entry.vmid != current_vmid) continue; // VMID isolation

                const std::uint64_t vpn = eff_va & entry.page_mask;
                const std::uint64_t entry_vpn = (entry.virtual_page << 12u) & entry.page_mask;

                if (vpn == entry_vpn && (!entry.non_global || entry.asid == current_asid)) {
                    if (current_el == 0 && !entry.el0_accessible) {
                        if (out_fault) {
                            out_fault->type = MmuFaultType::Permission;
                            out_fault->level = 3;
                            out_fault->virtual_address = virtual_address;
                            out_fault->access = access;
                            out_fault->is_write = (access == AccessKind::Write);
                            out_fault->is_instruction = (access == AccessKind::Execute);
                            out_fault->is_stage2 = false;
                        }
                        return std::nullopt;
                    }
                    if (access == AccessKind::Write && !entry.writable) {
                        if (out_fault) {
                            out_fault->type = MmuFaultType::Permission;
                            out_fault->level = 3;
                            out_fault->virtual_address = virtual_address;
                            out_fault->access = access;
                            out_fault->is_write = true;
                            out_fault->is_instruction = false;
                            out_fault->is_stage2 = false;
                        }
                        return std::nullopt;
                    }
                    if (access == AccessKind::Execute) {
                        const bool exec_ok = (current_el == 0) ? entry.executable_el0 : entry.executable_el1;
                        if (!exec_ok) {
                            if (out_fault) {
                                out_fault->type = MmuFaultType::Permission;
                                out_fault->level = 3;
                                out_fault->virtual_address = virtual_address;
                                out_fault->access = access;
                                out_fault->is_write = false;
                                out_fault->is_instruction = true;
                                out_fault->is_stage2 = false;
                            }
                            return std::nullopt;
                        }
                    }
                    const std::uint64_t offset = eff_va & ~entry.page_mask;
                    ipa = (entry.physical_page << 12u) | offset;
                    if (out_is_device) {
                        *out_is_device = entry.is_device;
                    }
                    s1_hit = true;
                    break;
                }
            }
        }
        if (!s1_hit) {
            auto s1_res = walk_stage1(memory, virtual_address, access, current_el, out_fault, out_is_device);
            if (!s1_res.has_value()) {
                if (out_fault) {
                    out_fault->is_stage2 = false;
                    out_fault->virtual_address = virtual_address;
                }
                return std::nullopt;
            }
            ipa = s1_res.value();
        }
    }

    if (!s2_enabled) {
        return ipa;
    }

    // Stage 2 translation: IPA -> PA with Stage-2 TLB caching
    bool s2_hit = false;
    std::uint64_t final_pa = ipa;
    if (tlb_enabled_) {
        const std::uint16_t current_vmid = static_cast<std::uint16_t>((vttbr_el2_ >> 48u) & 0xFFFFu);
        for (const auto& entry : tlb_) {
            if (!entry.valid || !entry.is_stage2) continue;
            if (entry.vmid != current_vmid) continue;

            const std::uint64_t ipa_vpn = ipa & entry.page_mask;
            const std::uint64_t entry_vpn = (entry.virtual_page << 12u) & entry.page_mask;

            if (ipa_vpn == entry_vpn) {
                if (access == AccessKind::Write && !entry.writable) {
                    if (out_fault) {
                        out_fault->type = MmuFaultType::Permission;
                        out_fault->level = 3;
                        out_fault->fault_ipa = ipa;
                        out_fault->virtual_address = virtual_address;
                        out_fault->access = access;
                        out_fault->is_write = true;
                        out_fault->is_instruction = false;
                        out_fault->is_stage2 = true;
                    }
                    return std::nullopt;
                }
                if (access == AccessKind::Execute && !entry.executable_el0) {
                    if (out_fault) {
                        out_fault->type = MmuFaultType::Permission;
                        out_fault->level = 3;
                        out_fault->fault_ipa = ipa;
                        out_fault->virtual_address = virtual_address;
                        out_fault->access = access;
                        out_fault->is_write = false;
                        out_fault->is_instruction = true;
                        out_fault->is_stage2 = true;
                    }
                    return std::nullopt;
                }
                const std::uint64_t offset = ipa & ~entry.page_mask;
                final_pa = (entry.physical_page << 12u) | offset;
                s2_hit = true;
                break;
            }
        }
    }

    if (!s2_hit) {
        auto s2_res = walk_stage2(memory, ipa, access, out_fault);
        if (!s2_res.has_value()) {
            if (out_fault) {
                out_fault->is_stage2 = true;
                out_fault->fault_ipa = ipa;
                out_fault->virtual_address = virtual_address;
            }
            return std::nullopt;
        }
        final_pa = s2_res.value();
    }
    return final_pa;
}

std::optional<std::uint64_t> MMU::walk_stage1(
    const Memory& memory,
    std::uint64_t virtual_address,
    AccessKind access,
    unsigned current_el,
    TranslationFault* out_fault,
    bool* out_is_device) {

    auto make_fault = [&](MmuFaultType type, unsigned level) {
        if (out_fault) {
            out_fault->type = type;
            out_fault->level = level;
            out_fault->virtual_address = virtual_address;
            out_fault->access = access;
            out_fault->is_write = (access == AccessKind::Write);
            out_fault->is_instruction = (access == AccessKind::Execute);
            out_fault->is_stage2 = false;
        }
        return std::nullopt;
    };

    const bool is_ttbr1 = (tcr_el1_ & ((1ULL << 37u) | (1ULL << 38u)))
        ? (((virtual_address >> 55u) & 1u) != 0)
        : ((virtual_address >> 63u) != 0);

    // Top Byte Ignore (TBI0 / TBI1)
    std::uint64_t eff_va = virtual_address;
    if (!is_ttbr1 && (tcr_el1_ & (1ULL << 37u))) {
        eff_va &= 0x00FFFFFFFFFFFFFFULL;
    } else if (is_ttbr1 && (tcr_el1_ & (1ULL << 38u))) {
        eff_va |= 0xFF00000000000000ULL;
    }

    // Check EPD0 / EPD1 (Translation Table walk disable)
    if (!is_ttbr1 && (tcr_el1_ & (1ULL << 7u))) {
        return make_fault(MmuFaultType::Translation, 0);
    }
    if (is_ttbr1 && (tcr_el1_ & (1ULL << 23u))) {
        return make_fault(MmuFaultType::Translation, 0);
    }

    // T0SZ and T1SZ virtual address size & canonical bounds check
    if (!is_ttbr1) {
        unsigned t0sz = static_cast<unsigned>(tcr_el1_ & 0x3Fu);
        if (t0sz == 0) t0sz = 16u; // Default 48-bit address space
        const unsigned va_bits = (t0sz >= 16u && t0sz <= 39u) ? (64u - t0sz) : 48u;
        if ((eff_va >> va_bits) != 0ULL) {
            return make_fault(MmuFaultType::AddressSize, 0);
        }
    } else {
        unsigned t1sz = static_cast<unsigned>((tcr_el1_ >> 16u) & 0x3Fu);
        if (t1sz == 0) t1sz = 16u; // Default 48-bit address space
        const unsigned va_bits = (t1sz >= 16u && t1sz <= 39u) ? (64u - t1sz) : 48u;
        const std::uint64_t expected_top = (~0ULL) << va_bits;
        if ((eff_va & expected_top) != expected_top) {
            return make_fault(MmuFaultType::AddressSize, 0);
        }
    }

    // ASID selection: TCR_EL1.A1 (bit 22)
    const std::uint16_t asid = static_cast<std::uint16_t>(
        ((tcr_el1_ & (1ULL << 22u)) ? (ttbr1_el1_ >> 48u) : (ttbr0_el1_ >> 48u)) & 0xFFFFu);
    const std::uint16_t vmid = static_cast<std::uint16_t>((vttbr_el2_ >> 48u) & 0xFFFFu);

    const std::uint64_t ttbr = is_ttbr1 ? ttbr1_el1_ : ttbr0_el1_;

    // Extract Level 0 base table physical address from TTBR
    std::uint64_t table_pa = ttbr & 0x0000FFFFFFFFF000ULL;
    const std::size_t mem_size = memory.size();

    std::uint8_t ap_table = 0;
    bool uxn_table = false;
    bool pxn_table = false;

    // --- LEVEL 0 ---
    const std::uint64_t l0_idx = (eff_va >> 39u) & 0x1FFu;
    const std::uint64_t l0_entry_addr = table_pa + l0_idx * 8u;
    if (l0_entry_addr + 8u > mem_size || l0_entry_addr < table_pa) {
        return make_fault(MmuFaultType::ExternalWalk, 0);
    }
    const std::uint64_t l0_desc = memory.read_u64(static_cast<std::size_t>(l0_entry_addr));

    DescriptorKind l0_kind = DescriptorKind::Invalid;
    MmuFaultType l0_fault = MmuFaultType::Translation;
    if (!validate_descriptor(0, l0_desc, false, &l0_kind, &l0_fault)) {
        return make_fault(l0_fault, 0);
    }

    // Accumulate hierarchical table permissions
    ap_table  |= static_cast<std::uint8_t>((l0_desc >> 61u) & 0x3u);
    uxn_table |= ((l0_desc >> 60u) & 0x1u) != 0;
    pxn_table |= ((l0_desc >> 59u) & 0x1u) != 0;
    table_pa   = l0_desc & 0x0000FFFFFFFFF000ULL;

    // --- LEVEL 1 ---
    const std::uint64_t l1_idx = (eff_va >> 30u) & 0x1FFu;
    const std::uint64_t l1_entry_addr = table_pa + l1_idx * 8u;
    if (l1_entry_addr + 8u > mem_size || l1_entry_addr < table_pa) {
        return make_fault(MmuFaultType::ExternalWalk, 1);
    }
    const std::uint64_t l1_desc = memory.read_u64(static_cast<std::size_t>(l1_entry_addr));

    DescriptorKind l1_kind = DescriptorKind::Invalid;
    MmuFaultType l1_fault = MmuFaultType::Translation;
    if (!validate_descriptor(1, l1_desc, false, &l1_kind, &l1_fault)) {
        return make_fault(l1_fault, 1);
    }

    // Level 1 Block (1GB)
    if (l1_kind == DescriptorKind::Block) {
        const unsigned level = 1;
        if (((l1_desc >> 10u) & 0x1u) == 0) {
            return make_fault(MmuFaultType::AccessFlag, level);
        }
        std::uint8_t ap = static_cast<std::uint8_t>((l1_desc >> 6u) & 0x3u);
        if (ap_table & 0x1u) ap |= 0x2u;
        if (ap_table & 0x2u) ap &= ~0x1u;

        const bool el0_acc = (ap & 0x1u) != 0;
        const bool read_only = (ap & 0x2u) != 0;

        if (current_el == 0 && !el0_acc) {
            return make_fault(MmuFaultType::Permission, level);
        }
        if (access == AccessKind::Write && read_only) {
            return make_fault(MmuFaultType::Permission, level);
        }

        bool uxn = ((l1_desc >> 54u) & 0x1u) != 0 || uxn_table;
        bool pxn = ((l1_desc >> 53u) & 0x1u) != 0 || pxn_table;
        if (is_wxn_enabled() && !read_only) {
            uxn = true;
            pxn = true;
        }

        if (access == AccessKind::Execute) {
            if (current_el == 0 && uxn) return make_fault(MmuFaultType::Permission, level);
            if (current_el == 1 && pxn) return make_fault(MmuFaultType::Permission, level);
        }

        const std::uint8_t attr_idx = static_cast<std::uint8_t>((l1_desc >> 2u) & 7u);
        const std::uint8_t attr_byte = get_mair_attribute(attr_idx);
        const bool is_dev = is_device_attribute(attr_byte);
        if (out_is_device) *out_is_device = is_dev;

        const std::uint64_t block_pa = (l1_desc & 0x0000FFFFC0000000ULL) | (eff_va & 0x3FFFFFFFULL);
        if (tlb_enabled_) {
            TlbEntry entry;
            entry.valid = true;
            entry.virtual_page = (eff_va & 0xFFFFFFFFC0000000ULL) >> 12u;
            entry.physical_page = (block_pa & 0xFFFFFFFFC0000000ULL) >> 12u;
            entry.page_mask = 0xFFFFFFFFC0000000ULL;
            entry.asid = asid;
            entry.vmid = vmid;
            entry.el0_accessible = el0_acc;
            entry.writable = !read_only;
            entry.executable_el0 = !uxn;
            entry.executable_el1 = !pxn;
            entry.non_global = ((l1_desc >> 11u) & 1u) != 0;
            entry.attr_indx = attr_idx;
            entry.shareability = static_cast<std::uint8_t>((l1_desc >> 8u) & 3u);
            entry.is_device = is_dev;
            if (tlb_.size() >= kTlbSize) tlb_.erase(tlb_.begin());
            tlb_.push_back(entry);
        }
        return block_pa;
    }

    ap_table  |= static_cast<std::uint8_t>((l1_desc >> 61u) & 0x3u);
    uxn_table |= ((l1_desc >> 60u) & 0x1u) != 0;
    pxn_table |= ((l1_desc >> 59u) & 0x1u) != 0;
    table_pa   = l1_desc & 0x0000FFFFFFFFF000ULL;

    // --- LEVEL 2 ---
    const std::uint64_t l2_idx = (eff_va >> 21u) & 0x1FFu;
    const std::uint64_t l2_entry_addr = table_pa + l2_idx * 8u;
    if (l2_entry_addr + 8u > mem_size || l2_entry_addr < table_pa) {
        return make_fault(MmuFaultType::ExternalWalk, 2);
    }
    const std::uint64_t l2_desc = memory.read_u64(static_cast<std::size_t>(l2_entry_addr));

    DescriptorKind l2_kind = DescriptorKind::Invalid;
    MmuFaultType l2_fault = MmuFaultType::Translation;
    if (!validate_descriptor(2, l2_desc, false, &l2_kind, &l2_fault)) {
        return make_fault(l2_fault, 2);
    }

    // Level 2 Block (2MB)
    if (l2_kind == DescriptorKind::Block) {
        const unsigned level = 2;
        if (((l2_desc >> 10u) & 0x1u) == 0) {
            return make_fault(MmuFaultType::AccessFlag, level);
        }
        std::uint8_t ap = static_cast<std::uint8_t>((l2_desc >> 6u) & 0x3u);
        if (ap_table & 0x1u) ap |= 0x2u;
        if (ap_table & 0x2u) ap &= ~0x1u;

        const bool el0_acc = (ap & 0x1u) != 0;
        const bool read_only = (ap & 0x2u) != 0;

        if (current_el == 0 && !el0_acc) {
            return make_fault(MmuFaultType::Permission, level);
        }
        if (access == AccessKind::Write && read_only) {
            return make_fault(MmuFaultType::Permission, level);
        }

        bool uxn = ((l2_desc >> 54u) & 0x1u) != 0 || uxn_table;
        bool pxn = ((l2_desc >> 53u) & 0x1u) != 0 || pxn_table;
        if (is_wxn_enabled() && !read_only) {
            uxn = true;
            pxn = true;
        }

        if (access == AccessKind::Execute) {
            if (current_el == 0 && uxn) return make_fault(MmuFaultType::Permission, level);
            if (current_el == 1 && pxn) return make_fault(MmuFaultType::Permission, level);
        }

        const std::uint8_t attr_idx = static_cast<std::uint8_t>((l2_desc >> 2u) & 7u);
        const std::uint8_t attr_byte = get_mair_attribute(attr_idx);
        const bool is_dev = is_device_attribute(attr_byte);
        if (out_is_device) *out_is_device = is_dev;

        const std::uint64_t block_pa = (l2_desc & 0x0000FFFFFFE00000ULL) | (eff_va & 0x1FFFFFULL);
        if (tlb_enabled_) {
            TlbEntry entry;
            entry.valid = true;
            entry.virtual_page = (eff_va & 0xFFFFFFFFFFE00000ULL) >> 12u;
            entry.physical_page = (block_pa & 0xFFFFFFFFFFE00000ULL) >> 12u;
            entry.page_mask = 0xFFFFFFFFFFE00000ULL;
            entry.asid = asid;
            entry.vmid = vmid;
            entry.el0_accessible = el0_acc;
            entry.writable = !read_only;
            entry.executable_el0 = !uxn;
            entry.executable_el1 = !pxn;
            entry.non_global = ((l2_desc >> 11u) & 1u) != 0;
            entry.attr_indx = attr_idx;
            entry.shareability = static_cast<std::uint8_t>((l2_desc >> 8u) & 3u);
            entry.is_device = is_dev;
            if (tlb_.size() >= kTlbSize) tlb_.erase(tlb_.begin());
            tlb_.push_back(entry);
        }
        return block_pa;
    }

    ap_table  |= static_cast<std::uint8_t>((l2_desc >> 61u) & 0x3u);
    uxn_table |= ((l2_desc >> 60u) & 0x1u) != 0;
    pxn_table |= ((l2_desc >> 59u) & 0x1u) != 0;
    table_pa   = l2_desc & 0x0000FFFFFFFFF000ULL;

    // --- LEVEL 3 ---
    const std::uint64_t l3_idx = (eff_va >> 12u) & 0x1FFu;
    const std::uint64_t l3_entry_addr = table_pa + l3_idx * 8u;
    if (l3_entry_addr + 8u > mem_size || l3_entry_addr < table_pa) {
        return make_fault(MmuFaultType::ExternalWalk, 3);
    }
    const std::uint64_t l3_desc = memory.read_u64(static_cast<std::size_t>(l3_entry_addr));

    DescriptorKind l3_kind = DescriptorKind::Invalid;
    MmuFaultType l3_fault = MmuFaultType::Translation;
    if (!validate_descriptor(3, l3_desc, false, &l3_kind, &l3_fault)) {
        return make_fault(l3_fault, 3);
    }

    // 4KB Page mapping
    const unsigned level = 3;
    if (((l3_desc >> 10u) & 0x1u) == 0) {
        return make_fault(MmuFaultType::AccessFlag, level);
    }
    std::uint8_t ap = static_cast<std::uint8_t>((l3_desc >> 6u) & 0x3u);
    if (ap_table & 0x1u) ap |= 0x2u;
    if (ap_table & 0x2u) ap &= ~0x1u;

    const bool el0_acc = (ap & 0x1u) != 0;
    const bool read_only = (ap & 0x2u) != 0;

    if (current_el == 0 && !el0_acc) {
        return make_fault(MmuFaultType::Permission, level);
    }
    if (access == AccessKind::Write && read_only) {
        return make_fault(MmuFaultType::Permission, level);
    }

    bool uxn = ((l3_desc >> 54u) & 0x1u) != 0 || uxn_table;
    bool pxn = ((l3_desc >> 53u) & 0x1u) != 0 || pxn_table;
    if (is_wxn_enabled() && !read_only) {
        uxn = true;
        pxn = true;
    }

    if (access == AccessKind::Execute) {
        if (current_el == 0 && uxn) return make_fault(MmuFaultType::Permission, level);
        if (current_el == 1 && pxn) return make_fault(MmuFaultType::Permission, level);
    }

    const std::uint8_t attr_idx = static_cast<std::uint8_t>((l3_desc >> 2u) & 7u);
    const std::uint8_t attr_byte = get_mair_attribute(attr_idx);
    const bool is_dev = is_device_attribute(attr_byte);
    if (out_is_device) *out_is_device = is_dev;

    const std::uint64_t page_pa = (l3_desc & 0x0000FFFFFFFFF000ULL) | (eff_va & 0xFFFULL);
    if (tlb_enabled_) {
        TlbEntry entry;
        entry.valid = true;
        entry.virtual_page = (eff_va & 0xFFFFFFFFFFFFF000ULL) >> 12u;
        entry.physical_page = (page_pa & 0xFFFFFFFFFFFFF000ULL) >> 12u;
        entry.page_mask = 0xFFFFFFFFFFFFF000ULL;
        entry.asid = asid;
        entry.vmid = vmid;
        entry.el0_accessible = el0_acc;
        entry.writable = !read_only;
        entry.executable_el0 = !uxn;
        entry.executable_el1 = !pxn;
        entry.non_global = ((l3_desc >> 11u) & 1u) != 0;
        entry.attr_indx = attr_idx;
        entry.shareability = static_cast<std::uint8_t>((l3_desc >> 8u) & 3u);
        entry.is_device = is_dev;
        if (tlb_.size() >= kTlbSize) tlb_.erase(tlb_.begin());
        tlb_.push_back(entry);
    }
    return page_pa;
}

std::optional<std::uint64_t> MMU::walk_stage2(
    const Memory& memory,
    std::uint64_t ipa,
    AccessKind access,
    TranslationFault* out_fault) {

    auto make_fault = [&](MmuFaultType type, unsigned level) {
        if (out_fault) {
            out_fault->type = type;
            out_fault->level = level;
            out_fault->fault_ipa = ipa;
            out_fault->access = access;
            out_fault->is_write = (access == AccessKind::Write);
            out_fault->is_instruction = (access == AccessKind::Execute);
            out_fault->is_stage2 = true;
        }
        return std::nullopt;
    };

    // VTCR_EL2.T0SZ input IPA size check
    unsigned s2_t0sz = static_cast<unsigned>(vtcr_el2_ & 0x3Fu);
    if (s2_t0sz == 0) s2_t0sz = 16u; // Default 48-bit IPA space
    const unsigned ipa_bits = (s2_t0sz >= 16u && s2_t0sz <= 39u) ? (64u - s2_t0sz) : 48u;
    if ((ipa >> ipa_bits) != 0ULL) {
        return make_fault(MmuFaultType::AddressSize, 0);
    }

    const std::uint64_t mem_size = static_cast<std::uint64_t>(memory.size());
    std::uint64_t table_pa = vttbr_el2_ & 0x0000FFFFFFFFF000ULL;
    const std::uint16_t vmid = static_cast<std::uint16_t>((vttbr_el2_ >> 48u) & 0xFFFFu);

    // --- LEVEL 1 ---
    const std::uint64_t l1_idx = (ipa >> 30u) & 0x1FFu;
    const std::uint64_t l1_entry_addr = table_pa + l1_idx * 8u;
    if (l1_entry_addr + 8u > mem_size || l1_entry_addr < table_pa) {
        return make_fault(MmuFaultType::ExternalWalk, 1);
    }
    const std::uint64_t l1_desc = memory.read_u64(static_cast<std::size_t>(l1_entry_addr));

    DescriptorKind l1_kind = DescriptorKind::Invalid;
    MmuFaultType l1_fault = MmuFaultType::Translation;
    if (!validate_descriptor(1, l1_desc, true, &l1_kind, &l1_fault)) {
        return make_fault(l1_fault, 1);
    }

    // Level 1 Block (1GB)
    if (l1_kind == DescriptorKind::Block) {
        const unsigned level = 1;
        if (((l1_desc >> 10u) & 0x1u) == 0) {
            return make_fault(MmuFaultType::AccessFlag, level);
        }
        const std::uint8_t s2ap = static_cast<std::uint8_t>((l1_desc >> 6u) & 0x3u);
        if (access == AccessKind::Write && ((s2ap & 0x2u) == 0)) {
            return make_fault(MmuFaultType::Permission, level);
        }
        if (access == AccessKind::Read && ((s2ap & 0x1u) == 0)) {
            return make_fault(MmuFaultType::Permission, level);
        }
        const bool xn = ((l1_desc >> 54u) & 0x1u) != 0;
        if (access == AccessKind::Execute && xn) {
            return make_fault(MmuFaultType::Permission, level);
        }
        const std::uint64_t block_pa = (l1_desc & 0x0000FFFFC0000000ULL) | (ipa & 0x3FFFFFFFULL);
        if (tlb_enabled_) {
            TlbEntry entry;
            entry.valid = true;
            entry.is_stage2 = true;
            entry.virtual_page = (ipa & 0xFFFFFFFFC0000000ULL) >> 12u;
            entry.physical_page = (block_pa & 0xFFFFFFFFC0000000ULL) >> 12u;
            entry.page_mask = 0xFFFFFFFFC0000000ULL;
            entry.vmid = vmid;
            entry.writable = (s2ap & 0x2u) != 0;
            entry.el0_accessible = true;
            entry.executable_el0 = !xn;
            entry.executable_el1 = !xn;
            if (tlb_.size() >= kTlbSize) tlb_.erase(tlb_.begin());
            tlb_.push_back(entry);
        }
        return block_pa;
    }

    table_pa = l1_desc & 0x0000FFFFFFFFF000ULL;

    // --- LEVEL 2 ---
    const std::uint64_t l2_idx = (ipa >> 21u) & 0x1FFu;
    const std::uint64_t l2_entry_addr = table_pa + l2_idx * 8u;
    if (l2_entry_addr + 8u > mem_size || l2_entry_addr < table_pa) {
        return make_fault(MmuFaultType::ExternalWalk, 2);
    }
    const std::uint64_t l2_desc = memory.read_u64(static_cast<std::size_t>(l2_entry_addr));

    DescriptorKind l2_kind = DescriptorKind::Invalid;
    MmuFaultType l2_fault = MmuFaultType::Translation;
    if (!validate_descriptor(2, l2_desc, true, &l2_kind, &l2_fault)) {
        return make_fault(l2_fault, 2);
    }

    // Level 2 Block (2MB)
    if (l2_kind == DescriptorKind::Block) {
        const unsigned level = 2;
        if (((l2_desc >> 10u) & 0x1u) == 0) {
            return make_fault(MmuFaultType::AccessFlag, level);
        }
        const std::uint8_t s2ap = static_cast<std::uint8_t>((l2_desc >> 6u) & 0x3u);
        if (access == AccessKind::Write && ((s2ap & 0x2u) == 0)) {
            return make_fault(MmuFaultType::Permission, level);
        }
        if (access == AccessKind::Read && ((s2ap & 0x1u) == 0)) {
            return make_fault(MmuFaultType::Permission, level);
        }
        const bool xn = ((l2_desc >> 54u) & 0x1u) != 0;
        if (access == AccessKind::Execute && xn) {
            return make_fault(MmuFaultType::Permission, level);
        }
        const std::uint64_t block_pa = (l2_desc & 0x0000FFFFFFE00000ULL) | (ipa & 0x1FFFFFULL);
        if (tlb_enabled_) {
            TlbEntry entry;
            entry.valid = true;
            entry.is_stage2 = true;
            entry.virtual_page = (ipa & 0xFFFFFFFFFFE00000ULL) >> 12u;
            entry.physical_page = (block_pa & 0xFFFFFFFFFFE00000ULL) >> 12u;
            entry.page_mask = 0xFFFFFFFFFFE00000ULL;
            entry.vmid = vmid;
            entry.writable = (s2ap & 0x2u) != 0;
            entry.el0_accessible = true;
            entry.executable_el0 = !xn;
            entry.executable_el1 = !xn;
            if (tlb_.size() >= kTlbSize) tlb_.erase(tlb_.begin());
            tlb_.push_back(entry);
        }
        return block_pa;
    }

    table_pa = l2_desc & 0x0000FFFFFFFFF000ULL;

    // --- LEVEL 3 ---
    const std::uint64_t l3_idx = (ipa >> 12u) & 0x1FFu;
    const std::uint64_t l3_entry_addr = table_pa + l3_idx * 8u;
    if (l3_entry_addr + 8u > mem_size || l3_entry_addr < table_pa) {
        return make_fault(MmuFaultType::ExternalWalk, 3);
    }
    const std::uint64_t l3_desc = memory.read_u64(static_cast<std::size_t>(l3_entry_addr));

    DescriptorKind l3_kind = DescriptorKind::Invalid;
    MmuFaultType l3_fault = MmuFaultType::Translation;
    if (!validate_descriptor(3, l3_desc, true, &l3_kind, &l3_fault)) {
        return make_fault(l3_fault, 3);
    }

    // Level 3 Page (4KB)
    const unsigned level = 3;
    if (((l3_desc >> 10u) & 0x1u) == 0) {
        return make_fault(MmuFaultType::AccessFlag, level);
    }
    const std::uint8_t s2ap = static_cast<std::uint8_t>((l3_desc >> 6u) & 0x3u);
    if (access == AccessKind::Write && ((s2ap & 0x2u) == 0)) {
        return make_fault(MmuFaultType::Permission, level);
    }
    if (access == AccessKind::Read && ((s2ap & 0x1u) == 0)) {
        return make_fault(MmuFaultType::Permission, level);
    }
    const bool xn = ((l3_desc >> 54u) & 0x1u) != 0;
    if (access == AccessKind::Execute && xn) {
        return make_fault(MmuFaultType::Permission, level);
    }

    const std::uint64_t page_pa = (l3_desc & 0x0000FFFFFFFFF000ULL) | (ipa & 0xFFFULL);
    if (tlb_enabled_) {
        TlbEntry entry;
        entry.valid = true;
        entry.is_stage2 = true;
        entry.virtual_page = (ipa & 0xFFFFFFFFFFFFF000ULL) >> 12u;
        entry.physical_page = (page_pa & 0xFFFFFFFFFFFFF000ULL) >> 12u;
        entry.page_mask = 0xFFFFFFFFFFFFF000ULL;
        entry.vmid = vmid;
        entry.writable = (s2ap & 0x2u) != 0;
        entry.el0_accessible = true;
        entry.executable_el0 = !xn;
        entry.executable_el1 = !xn;
        if (tlb_.size() >= kTlbSize) tlb_.erase(tlb_.begin());
        tlb_.push_back(entry);
    }
    return page_pa;
}

} // namespace mmu

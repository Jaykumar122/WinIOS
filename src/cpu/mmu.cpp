#include "cpu/mmu.h"

#include <iostream>
#include "memory/memory.h"

namespace mmu {

MMU::MMU() {
    reset();
}

void MMU::reset() noexcept {
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

void MMU::invalidate_tlb_vmid(std::uint16_t vmid) noexcept {
    for (auto it = tlb_.begin(); it != tlb_.end();) {
        if (it->valid && it->vmid == vmid) {
            it = tlb_.erase(it);
        } else {
            ++it;
        }
    }
}

void MMU::set_sctlr_el1(std::uint64_t val) noexcept {
    // Bits [28:20], [18:16], [12:0] etc.
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

void MMU::invalidate_tlb_all() noexcept {
    tlb_.clear();
}

void MMU::invalidate_tlb_va(std::uint64_t va, std::uint16_t asid, bool /*last_level_only*/) noexcept {
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
    for (auto it = tlb_.begin(); it != tlb_.end();) {
        if (it->valid && it->non_global && it->asid == asid) {
            it = tlb_.erase(it);
        } else {
            ++it;
        }
    }
}

void MMU::invalidate_tlb_vaa(std::uint64_t va, bool /*last_level_only*/) noexcept {
    const std::uint64_t vpn = va >> 12u;
    for (auto it = tlb_.begin(); it != tlb_.end();) {
        if (it->valid && (it->virtual_page == (vpn & (it->page_mask >> 12u)))) {
            it = tlb_.erase(it);
        } else {
            ++it;
        }
    }
}

std::optional<std::uint64_t> MMU::translate(
    const Memory& memory,
    std::uint64_t virtual_address,
    AccessKind access,
    unsigned current_el,
    TranslationFault* out_fault) {

    const bool s1_enabled = is_stage1_enabled();
    const bool s2_enabled = is_stage2_enabled(current_el);

    if (!s1_enabled && !s2_enabled) {
        return virtual_address;
    }

    std::uint64_t ipa = virtual_address;
    if (s1_enabled) {
        bool s1_hit = false;
        if (tlb_enabled_) {
            const std::uint16_t current_asid = static_cast<std::uint16_t>(
                ((virtual_address >> 63u) ? (ttbr1_el1_ >> 48u) : (ttbr0_el1_ >> 48u)) & 0xFFFFu);

            for (const auto& entry : tlb_) {
                if (!entry.valid || entry.is_stage2) continue;
                const std::uint64_t vpn = virtual_address & entry.page_mask;
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
                    const std::uint64_t offset = virtual_address & ~entry.page_mask;
                    ipa = (entry.physical_page << 12u) | offset;
                    s1_hit = true;
                    break;
                }
            }
        }
        if (!s1_hit) {
            auto s1_res = walk_stage1(memory, virtual_address, access, current_el, out_fault);
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

    // Stage 2 translation: IPA -> PA
    auto s2_res = walk_stage2(memory, ipa, access, out_fault);
    if (!s2_res.has_value()) {
        if (out_fault) {
            out_fault->is_stage2 = true;
            out_fault->fault_ipa = ipa;
            out_fault->virtual_address = virtual_address;
        }
        return std::nullopt;
    }
    return s2_res.value();
}

std::optional<std::uint64_t> MMU::walk_stage1(
    const Memory& memory,
    std::uint64_t virtual_address,
    AccessKind access,
    unsigned current_el,
    TranslationFault* out_fault) {

    auto make_fault = [&](MmuFaultType type, unsigned level) {
        if (out_fault) {
            out_fault->type = type;
            out_fault->level = level;
            out_fault->virtual_address = virtual_address;
            out_fault->access = access;
            out_fault->is_write = (access == AccessKind::Write);
            out_fault->is_instruction = (access == AccessKind::Execute);
        }
        return std::nullopt;
    };

    const bool is_ttbr1 = (virtual_address >> 63u) != 0;
    const std::uint64_t ttbr = is_ttbr1 ? ttbr1_el1_ : ttbr0_el1_;

    // Check EPD0 / EPD1 (Translation Table walk disable)
    if (!is_ttbr1 && (tcr_el1_ & (1ULL << 7u))) {
        return make_fault(MmuFaultType::Translation, 0);
    }
    if (is_ttbr1 && (tcr_el1_ & (1ULL << 23u))) {
        return make_fault(MmuFaultType::Translation, 0);
    }

    // 48-bit Virtual Address bounds check
    // TTBR0 range: bits [63:48] must be 0.
    // TTBR1 range: bits [63:48] must be 0xFFFF.
    const std::uint64_t top_bits = (virtual_address >> 48u) & 0xFFFFu;
    if (!is_ttbr1 && top_bits != 0u) {
        return make_fault(MmuFaultType::AddressSize, 0);
    }
    if (is_ttbr1 && top_bits != 0xFFFFu) {
        return make_fault(MmuFaultType::AddressSize, 0);
    }

    // Extract Level 0 base table physical address from TTBR
    std::uint64_t table_pa = ttbr & 0x0000FFFFFFFFF000ULL;
    const std::size_t mem_size = memory.size();

    std::uint8_t ap_table = 0;
    bool uxn_table = false;
    bool pxn_table = false;

    // --- LEVEL 0 ---
    const std::uint64_t l0_idx = (virtual_address >> 39u) & 0x1FFu;
    const std::uint64_t l0_entry_addr = table_pa + l0_idx * 8u;
    if (l0_entry_addr + 8u > mem_size || l0_entry_addr < table_pa) {
        return make_fault(MmuFaultType::ExternalWalk, 0);
    }
    const std::uint64_t l0_desc = memory.read_u64(static_cast<std::size_t>(l0_entry_addr));
    if ((l0_desc & 0x1ULL) == 0) {
        return make_fault(MmuFaultType::Translation, 0);
    }
    // At Level 0 for 4KB granule, only table descriptors (bits[1:0] == 0b11) are valid
    if ((l0_desc & 0x3ULL) != 0x3ULL) {
        return make_fault(MmuFaultType::Translation, 0);
    }
    // Update hierarchical permissions
    ap_table  |= static_cast<std::uint8_t>((l0_desc >> 61u) & 0x3u);
    uxn_table |= ((l0_desc >> 60u) & 0x1u) != 0;
    pxn_table |= ((l0_desc >> 59u) & 0x1u) != 0;
    table_pa   = l0_desc & 0x0000FFFFFFFFF000ULL;

    // --- LEVEL 1 ---
    const std::uint64_t l1_idx = (virtual_address >> 30u) & 0x1FFu;
    const std::uint64_t l1_entry_addr = table_pa + l1_idx * 8u;
    if (l1_entry_addr + 8u > mem_size || l1_entry_addr < table_pa) {
        return make_fault(MmuFaultType::ExternalWalk, 1);
    }
    const std::uint64_t l1_desc = memory.read_u64(static_cast<std::size_t>(l1_entry_addr));
    if ((l1_desc & 0x1ULL) == 0) {
        return make_fault(MmuFaultType::Translation, 1);
    }

    // Check if Level 1 is a 1GB Block Descriptor (bits[1:0] == 0b01)
    if ((l1_desc & 0x3ULL) == 0x1ULL) {
        // 1GB Block mapping
        const unsigned level = 1;
        // AF check (bit 10)
        if (((l1_desc >> 10u) & 0x1u) == 0) {
            return make_fault(MmuFaultType::AccessFlag, level);
        }
        // Permission check
        std::uint8_t ap = static_cast<std::uint8_t>((l1_desc >> 6u) & 0x3u);
        if (ap_table & 0x1u) ap |= 0x2u;  // APTable[0]: force read-only
        if (ap_table & 0x2u) ap &= ~0x1u; // APTable[1]: remove EL0 access

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
            if (current_el == 0 && uxn) {
                return make_fault(MmuFaultType::Permission, level);
            }
            if (current_el == 1 && pxn) {
                return make_fault(MmuFaultType::Permission, level);
            }
        }

        const std::uint64_t block_pa = (l1_desc & 0x0000FFFFC0000000ULL) | (virtual_address & 0x3FFFFFFFULL);
        if (tlb_enabled_) {
            TlbEntry entry;
            entry.valid = true;
            entry.virtual_page = (virtual_address & 0xFFFFFFFFC0000000ULL) >> 12u;
            entry.physical_page = (block_pa & 0xFFFFFFFFC0000000ULL) >> 12u;
            entry.page_mask = 0xFFFFFFFFC0000000ULL;
            entry.asid = static_cast<std::uint16_t>((ttbr >> 48u) & 0xFFFFu);
            entry.el0_accessible = el0_acc;
            entry.writable = !read_only;
            entry.executable_el0 = !uxn;
            entry.executable_el1 = !pxn;
            entry.non_global = ((l1_desc >> 11u) & 1u) != 0;
            entry.attr_indx = static_cast<std::uint8_t>((l1_desc >> 2u) & 7u);
            entry.shareability = static_cast<std::uint8_t>((l1_desc >> 8u) & 3u);
            if (tlb_.size() >= kTlbSize) tlb_.erase(tlb_.begin());
            tlb_.push_back(entry);
        }
        return block_pa;
    }

    if ((l1_desc & 0x3ULL) != 0x3ULL) {
        return make_fault(MmuFaultType::Translation, 1);
    }
    ap_table  |= static_cast<std::uint8_t>((l1_desc >> 61u) & 0x3u);
    uxn_table |= ((l1_desc >> 60u) & 0x1u) != 0;
    pxn_table |= ((l1_desc >> 59u) & 0x1u) != 0;
    table_pa   = l1_desc & 0x0000FFFFFFFFF000ULL;

    // --- LEVEL 2 ---
    const std::uint64_t l2_idx = (virtual_address >> 21u) & 0x1FFu;
    const std::uint64_t l2_entry_addr = table_pa + l2_idx * 8u;
    if (l2_entry_addr + 8u > mem_size || l2_entry_addr < table_pa) {
        return make_fault(MmuFaultType::ExternalWalk, 2);
    }
    const std::uint64_t l2_desc = memory.read_u64(static_cast<std::size_t>(l2_entry_addr));
    if ((l2_desc & 0x1ULL) == 0) {
        return make_fault(MmuFaultType::Translation, 2);
    }

    // Check if Level 2 is a 2MB Block Descriptor (bits[1:0] == 0b01)
    if ((l2_desc & 0x3ULL) == 0x1ULL) {
        // 2MB Block mapping
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
            if (current_el == 0 && uxn) {
                return make_fault(MmuFaultType::Permission, level);
            }
            if (current_el == 1 && pxn) {
                return make_fault(MmuFaultType::Permission, level);
            }
        }

        const std::uint64_t block_pa = (l2_desc & 0x0000FFFFFFE00000ULL) | (virtual_address & 0x1FFFFFULL);
        if (tlb_enabled_) {
            TlbEntry entry;
            entry.valid = true;
            entry.virtual_page = (virtual_address & 0xFFFFFFFFFFE00000ULL) >> 12u;
            entry.physical_page = (block_pa & 0xFFFFFFFFFFE00000ULL) >> 12u;
            entry.page_mask = 0xFFFFFFFFFFE00000ULL;
            entry.asid = static_cast<std::uint16_t>((ttbr >> 48u) & 0xFFFFu);
            entry.el0_accessible = el0_acc;
            entry.writable = !read_only;
            entry.executable_el0 = !uxn;
            entry.executable_el1 = !pxn;
            entry.non_global = ((l2_desc >> 11u) & 1u) != 0;
            entry.attr_indx = static_cast<std::uint8_t>((l2_desc >> 2u) & 7u);
            entry.shareability = static_cast<std::uint8_t>((l2_desc >> 8u) & 3u);
            if (tlb_.size() >= kTlbSize) tlb_.erase(tlb_.begin());
            tlb_.push_back(entry);
        }
        return block_pa;
    }

    if ((l2_desc & 0x3ULL) != 0x3ULL) {
        return make_fault(MmuFaultType::Translation, 2);
    }
    ap_table  |= static_cast<std::uint8_t>((l2_desc >> 61u) & 0x3u);
    uxn_table |= ((l2_desc >> 60u) & 0x1u) != 0;
    pxn_table |= ((l2_desc >> 59u) & 0x1u) != 0;
    table_pa   = l2_desc & 0x0000FFFFFFFFF000ULL;

    // --- LEVEL 3 ---
    const std::uint64_t l3_idx = (virtual_address >> 12u) & 0x1FFu;
    const std::uint64_t l3_entry_addr = table_pa + l3_idx * 8u;
    if (l3_entry_addr + 8u > mem_size || l3_entry_addr < table_pa) {
        return make_fault(MmuFaultType::ExternalWalk, 3);
    }
    const std::uint64_t l3_desc = memory.read_u64(static_cast<std::size_t>(l3_entry_addr));
    if ((l3_desc & 0x1ULL) == 0) {
        return make_fault(MmuFaultType::Translation, 3);
    }
    // At Level 3, only page descriptors (bits[1:0] == 0b11) are valid
    if ((l3_desc & 0x3ULL) != 0x3ULL) {
        return make_fault(MmuFaultType::Translation, 3);
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
        if (current_el == 0 && uxn) {
            return make_fault(MmuFaultType::Permission, level);
        }
        if (current_el == 1 && pxn) {
            return make_fault(MmuFaultType::Permission, level);
        }
    }

    const std::uint64_t page_pa = (l3_desc & 0x0000FFFFFFFFF000ULL) | (virtual_address & 0xFFFULL);
    if (tlb_enabled_) {
        TlbEntry entry;
        entry.valid = true;
        entry.virtual_page = (virtual_address & 0xFFFFFFFFFFFFF000ULL) >> 12u;
        entry.physical_page = (page_pa & 0xFFFFFFFFFFFFF000ULL) >> 12u;
        entry.page_mask = 0xFFFFFFFFFFFFF000ULL;
        entry.asid = static_cast<std::uint16_t>((ttbr >> 48u) & 0xFFFFu);
        entry.el0_accessible = el0_acc;
        entry.writable = !read_only;
        entry.executable_el0 = !uxn;
        entry.executable_el1 = !pxn;
        entry.non_global = ((l3_desc >> 11u) & 1u) != 0;
        entry.attr_indx = static_cast<std::uint8_t>((l3_desc >> 2u) & 7u);
        entry.shareability = static_cast<std::uint8_t>((l3_desc >> 8u) & 3u);
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

    // 48-bit IPA check
    if ((ipa >> 48u) != 0) {
        return make_fault(MmuFaultType::AddressSize, 0);
    }

    const std::uint64_t mem_size = static_cast<std::uint64_t>(memory.size());
    std::uint64_t table_pa = vttbr_el2_ & 0x0000FFFFFFFFF000ULL;

    // --- LEVEL 1 ---
    const std::uint64_t l1_idx = (ipa >> 30u) & 0x1FFu;
    const std::uint64_t l1_entry_addr = table_pa + l1_idx * 8u;
    if (l1_entry_addr + 8u > mem_size || l1_entry_addr < table_pa) {
        return make_fault(MmuFaultType::ExternalWalk, 1);
    }
    const std::uint64_t l1_desc = memory.read_u64(static_cast<std::size_t>(l1_entry_addr));
    if ((l1_desc & 0x1ULL) == 0) {
        return make_fault(MmuFaultType::Translation, 1);
    }

    // Level 1 Block (1GB, bits[1:0] == 0b01)
    if ((l1_desc & 0x3ULL) == 0x1ULL) {
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
        if (access == AccessKind::Execute && ((l1_desc >> 54u) & 0x1u)) {
            return make_fault(MmuFaultType::Permission, level);
        }
        return (l1_desc & 0x0000FFFFC0000000ULL) | (ipa & 0x3FFFFFFFULL);
    }

    if ((l1_desc & 0x3ULL) != 0x3ULL) {
        return make_fault(MmuFaultType::Translation, 1);
    }
    table_pa = l1_desc & 0x0000FFFFFFFFF000ULL;

    // --- LEVEL 2 ---
    const std::uint64_t l2_idx = (ipa >> 21u) & 0x1FFu;
    const std::uint64_t l2_entry_addr = table_pa + l2_idx * 8u;
    if (l2_entry_addr + 8u > mem_size || l2_entry_addr < table_pa) {
        return make_fault(MmuFaultType::ExternalWalk, 2);
    }
    const std::uint64_t l2_desc = memory.read_u64(static_cast<std::size_t>(l2_entry_addr));
    if ((l2_desc & 0x1ULL) == 0) {
        return make_fault(MmuFaultType::Translation, 2);
    }

    // Level 2 Block (2MB, bits[1:0] == 0b01)
    if ((l2_desc & 0x3ULL) == 0x1ULL) {
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
        if (access == AccessKind::Execute && ((l2_desc >> 54u) & 0x1u)) {
            return make_fault(MmuFaultType::Permission, level);
        }
        return (l2_desc & 0x0000FFFFFFE00000ULL) | (ipa & 0x1FFFFFULL);
    }

    if ((l2_desc & 0x3ULL) != 0x3ULL) {
        return make_fault(MmuFaultType::Translation, 2);
    }
    table_pa = l2_desc & 0x0000FFFFFFFFF000ULL;

    // --- LEVEL 3 ---
    const std::uint64_t l3_idx = (ipa >> 12u) & 0x1FFu;
    const std::uint64_t l3_entry_addr = table_pa + l3_idx * 8u;
    if (l3_entry_addr + 8u > mem_size || l3_entry_addr < table_pa) {
        return make_fault(MmuFaultType::ExternalWalk, 3);
    }
    const std::uint64_t l3_desc = memory.read_u64(static_cast<std::size_t>(l3_entry_addr));
    if ((l3_desc & 0x1ULL) == 0) {
        return make_fault(MmuFaultType::Translation, 3);
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
    if (access == AccessKind::Execute && ((l3_desc >> 54u) & 0x1u)) {
        return make_fault(MmuFaultType::Permission, level);
    }

    return (l3_desc & 0x0000FFFFFFFFF000ULL) | (ipa & 0xFFFULL);
}

} // namespace mmu

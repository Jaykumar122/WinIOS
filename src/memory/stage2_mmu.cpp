#include "memory/stage2_mmu.h"

namespace winios::mmu {

void Stage2TranslationEngine::reset() noexcept {
    hcr_el2_ = 0;
    vttbr_el2_ = 0;
    vtcr_el2_ = 0x80003550ULL; // Default 48-bit IPA, 4KB granule
}

Stage2Granule Stage2TranslationEngine::granule() const noexcept {
    const unsigned tg0 = static_cast<unsigned>((vtcr_el2_ >> 14u) & 0x3u);
    if (tg0 == 0b10) return Stage2Granule::Granule16KB;
    if (tg0 == 0b01) return Stage2Granule::Granule64KB;
    return Stage2Granule::Granule4KB;
}

unsigned Stage2TranslationEngine::ipa_size_bits() const noexcept {
    unsigned t0sz = static_cast<unsigned>(vtcr_el2_ & 0x3Fu);
    if (t0sz < 16u || t0sz > 39u) {
        t0sz = 16u; // Default 48-bit IPA space
    }
    return 64u - t0sz;
}

unsigned Stage2TranslationEngine::start_level() const noexcept {
    const unsigned sl0 = static_cast<unsigned>((vtcr_el2_ >> 6u) & 0x3u);
    if (granule() == Stage2Granule::Granule16KB) {
        if (sl0 == 0b10) return 1u;
        return 2u;
    }
    // 4KB granule
    if (sl0 == 0b10) return 0u;
    if (sl0 == 0b01) return 1u;
    return 2u;
}

std::optional<Stage2TranslationResult> Stage2TranslationEngine::translate(
    const Memory& memory,
    std::uint64_t ipa,
    Stage2AccessType access,
    Stage2Fault* out_fault) const {

    if (!is_enabled()) {
        // Flat identity translation when Stage-2 translation is disabled (HCR_EL2.VM == 0)
        Stage2TranslationResult res;
        res.physical_address = ipa;
        res.page_size = (granule() == Stage2Granule::Granule16KB) ? 16384ULL : 4096ULL;
        res.readable = true;
        res.writable = true;
        res.executable = true;
        res.mem_attr = 0xFu;
        res.is_device = false;
        return res;
    }

    // Input IPA size check against VTCR_EL2.T0SZ
    const unsigned ipa_bits = ipa_size_bits();
    if (ipa_bits < 64u && (ipa >> ipa_bits) != 0ULL) {
        if (out_fault) {
            out_fault->type = ::mmu::MmuFaultType::AddressSize;
            out_fault->level = 0;
            out_fault->ipa = ipa;
            out_fault->access = access;
            out_fault->is_write = (access == Stage2AccessType::Write);
            out_fault->is_execute = (access == Stage2AccessType::Execute);
        }
        return std::nullopt;
    }

    if (granule() == Stage2Granule::Granule16KB) {
        return walk_16k(memory, ipa, access, out_fault);
    }
    return walk_4k(memory, ipa, access, out_fault);
}

std::optional<Stage2TranslationResult> Stage2TranslationEngine::walk_16k(
    const Memory& memory,
    std::uint64_t ipa,
    Stage2AccessType access,
    Stage2Fault* out_fault) const {

    auto make_fault = [&](::mmu::MmuFaultType type, unsigned level) {
        if (out_fault) {
            out_fault->type = type;
            out_fault->level = level;
            out_fault->ipa = ipa;
            out_fault->access = access;
            out_fault->is_write = (access == Stage2AccessType::Write);
            out_fault->is_execute = (access == Stage2AccessType::Execute);
        }
        return std::nullopt;
    };

    const std::uint64_t mem_size = static_cast<std::uint64_t>(memory.size());
    std::uint64_t table_pa = translation_table_base();

    // Check table base alignment to 16KB
    if ((table_pa & 0x3FFFULL) != 0) {
        return make_fault(::mmu::MmuFaultType::Alignment, 0);
    }

    unsigned current_level = start_level();

    // --- Level 1 (if starting at level 1) ---
    if (current_level == 1) {
        const std::uint64_t l1_idx = (ipa >> 36u) & 0x7FFu; // 11 bits: [46:36]
        const std::uint64_t l1_entry_addr = table_pa + l1_idx * 8u;
        if (l1_entry_addr + 8u > mem_size || l1_entry_addr < table_pa) {
            return make_fault(::mmu::MmuFaultType::ExternalWalk, 1);
        }

        const std::uint64_t l1_desc = memory.read_u64(static_cast<std::size_t>(l1_entry_addr));
        if ((l1_desc & 0x1u) == 0) {
            return make_fault(::mmu::MmuFaultType::Translation, 1);
        }

        // In 16KB granule, Level 1 block maps 64GB
        if ((l1_desc & 0x2u) == 0) {
            // Block descriptor
            if (((l1_desc >> 10u) & 0x1u) == 0) {
                return make_fault(::mmu::MmuFaultType::AccessFlag, 1);
            }
            const std::uint8_t s2ap = static_cast<std::uint8_t>((l1_desc >> 6u) & 0x3u);
            if (access == Stage2AccessType::Write && ((s2ap & 0x2u) == 0)) {
                return make_fault(::mmu::MmuFaultType::Permission, 1);
            }
            if (access == Stage2AccessType::Read && ((s2ap & 0x1u) == 0)) {
                return make_fault(::mmu::MmuFaultType::Permission, 1);
            }
            const bool xn = ((l1_desc >> 54u) & 0x1u) != 0;
            if (access == Stage2AccessType::Execute && xn) {
                return make_fault(::mmu::MmuFaultType::Permission, 1);
            }

            Stage2TranslationResult res;
            res.physical_address = (l1_desc & 0x0000FFFC00000000ULL) | (ipa & 0x00000003FFFFFFFFULL);
            res.page_size = 64ULL * 1024 * 1024 * 1024;
            res.readable = (s2ap & 0x1u) != 0;
            res.writable = (s2ap & 0x2u) != 0;
            res.executable = !xn;
            res.mem_attr = static_cast<std::uint8_t>((l1_desc >> 2u) & 0xFu);
            res.is_device = (res.mem_attr <= 0x3u);
            return res;
        }

        table_pa = l1_desc & 0x0000FFFFFFFFC000ULL;
        current_level = 2;
    }

    // --- Level 2 (32MB blocks or table to Level 3) ---
    if (current_level == 2) {
        const std::uint64_t l2_idx = (ipa >> 25u) & 0x7FFu; // 11 bits: [35:25]
        const std::uint64_t l2_entry_addr = table_pa + l2_idx * 8u;
        if (l2_entry_addr + 8u > mem_size || l2_entry_addr < table_pa) {
            return make_fault(::mmu::MmuFaultType::ExternalWalk, 2);
        }

        const std::uint64_t l2_desc = memory.read_u64(static_cast<std::size_t>(l2_entry_addr));
        if ((l2_desc & 0x1u) == 0) {
            return make_fault(::mmu::MmuFaultType::Translation, 2);
        }

        // In 16KB granule, Level 2 block maps 32MB
        if ((l2_desc & 0x2u) == 0) {
            // Block descriptor
            if (((l2_desc >> 10u) & 0x1u) == 0) {
                return make_fault(::mmu::MmuFaultType::AccessFlag, 2);
            }
            const std::uint8_t s2ap = static_cast<std::uint8_t>((l2_desc >> 6u) & 0x3u);
            if (access == Stage2AccessType::Write && ((s2ap & 0x2u) == 0)) {
                return make_fault(::mmu::MmuFaultType::Permission, 2);
            }
            if (access == Stage2AccessType::Read && ((s2ap & 0x1u) == 0)) {
                return make_fault(::mmu::MmuFaultType::Permission, 2);
            }
            const bool xn = ((l2_desc >> 54u) & 0x1u) != 0;
            if (access == Stage2AccessType::Execute && xn) {
                return make_fault(::mmu::MmuFaultType::Permission, 2);
            }

            Stage2TranslationResult res;
            res.physical_address = (l2_desc & 0x0000FFFFFFE00000ULL) | (ipa & 0x0000000001FFFFFFULL);
            res.page_size = 32ULL * 1024 * 1024;
            res.readable = (s2ap & 0x1u) != 0;
            res.writable = (s2ap & 0x2u) != 0;
            res.executable = !xn;
            res.mem_attr = static_cast<std::uint8_t>((l2_desc >> 2u) & 0xFu);
            res.is_device = (res.mem_attr <= 0x3u);
            return res;
        }

        table_pa = l2_desc & 0x0000FFFFFFFFC000ULL;
        current_level = 3;
    }

    // --- Level 3 (16KB Pages) ---
    const std::uint64_t l3_idx = (ipa >> 14u) & 0x7FFu; // 11 bits: [24:14]
    const std::uint64_t l3_entry_addr = table_pa + l3_idx * 8u;
    if (l3_entry_addr + 8u > mem_size || l3_entry_addr < table_pa) {
        return make_fault(::mmu::MmuFaultType::ExternalWalk, 3);
    }

    const std::uint64_t l3_desc = memory.read_u64(static_cast<std::size_t>(l3_entry_addr));
    if ((l3_desc & 0x1u) == 0) {
        return make_fault(::mmu::MmuFaultType::Translation, 3);
    }

    if (((l3_desc >> 10u) & 0x1u) == 0) {
        return make_fault(::mmu::MmuFaultType::AccessFlag, 3);
    }
    const std::uint8_t s2ap = static_cast<std::uint8_t>((l3_desc >> 6u) & 0x3u);
    if (access == Stage2AccessType::Write && ((s2ap & 0x2u) == 0)) {
        return make_fault(::mmu::MmuFaultType::Permission, 3);
    }
    if (access == Stage2AccessType::Read && ((s2ap & 0x1u) == 0)) {
        return make_fault(::mmu::MmuFaultType::Permission, 3);
    }
    const bool xn = ((l3_desc >> 54u) & 0x1u) != 0;
    if (access == Stage2AccessType::Execute && xn) {
        return make_fault(::mmu::MmuFaultType::Permission, 3);
    }

    Stage2TranslationResult res;
    res.physical_address = (l3_desc & 0x0000FFFFFFFFC000ULL) | (ipa & 0x3FFFULL);
    res.page_size = 16384ULL;
    res.readable = (s2ap & 0x1u) != 0;
    res.writable = (s2ap & 0x2u) != 0;
    res.executable = !xn;
    res.mem_attr = static_cast<std::uint8_t>((l3_desc >> 2u) & 0xFu);
    res.is_device = (res.mem_attr <= 0x3u);
    return res;
}

std::optional<Stage2TranslationResult> Stage2TranslationEngine::walk_4k(
    const Memory& memory,
    std::uint64_t ipa,
    Stage2AccessType access,
    Stage2Fault* out_fault) const {

    auto make_fault = [&](::mmu::MmuFaultType type, unsigned level) {
        if (out_fault) {
            out_fault->type = type;
            out_fault->level = level;
            out_fault->ipa = ipa;
            out_fault->access = access;
            out_fault->is_write = (access == Stage2AccessType::Write);
            out_fault->is_execute = (access == Stage2AccessType::Execute);
        }
        return std::nullopt;
    };

    const std::uint64_t mem_size = static_cast<std::uint64_t>(memory.size());
    std::uint64_t table_pa = translation_table_base();

    // Check table base alignment to 4KB
    if ((table_pa & 0xFFFULL) != 0) {
        return make_fault(::mmu::MmuFaultType::Alignment, 0);
    }

    // --- Level 1 (1GB Blocks or Table to Level 2) ---
    const std::uint64_t l1_idx = (ipa >> 30u) & 0x1FFu;
    const std::uint64_t l1_entry_addr = table_pa + l1_idx * 8u;
    if (l1_entry_addr + 8u > mem_size || l1_entry_addr < table_pa) {
        return make_fault(::mmu::MmuFaultType::ExternalWalk, 1);
    }

    const std::uint64_t l1_desc = memory.read_u64(static_cast<std::size_t>(l1_entry_addr));
    if ((l1_desc & 0x1u) == 0) {
        return make_fault(::mmu::MmuFaultType::Translation, 1);
    }

    // Level 1 Block (1GB)
    if ((l1_desc & 0x2u) == 0) {
        if (((l1_desc >> 10u) & 0x1u) == 0) {
            return make_fault(::mmu::MmuFaultType::AccessFlag, 1);
        }
        const std::uint8_t s2ap = static_cast<std::uint8_t>((l1_desc >> 6u) & 0x3u);
        if (access == Stage2AccessType::Write && ((s2ap & 0x2u) == 0)) {
            return make_fault(::mmu::MmuFaultType::Permission, 1);
        }
        if (access == Stage2AccessType::Read && ((s2ap & 0x1u) == 0)) {
            return make_fault(::mmu::MmuFaultType::Permission, 1);
        }
        const bool xn = ((l1_desc >> 54u) & 0x1u) != 0;
        if (access == Stage2AccessType::Execute && xn) {
            return make_fault(::mmu::MmuFaultType::Permission, 1);
        }

        Stage2TranslationResult res;
        res.physical_address = (l1_desc & 0x0000FFFFC0000000ULL) | (ipa & 0x3FFFFFFFULL);
        res.page_size = 1024ULL * 1024 * 1024;
        res.readable = (s2ap & 0x1u) != 0;
        res.writable = (s2ap & 0x2u) != 0;
        res.executable = !xn;
        res.mem_attr = static_cast<std::uint8_t>((l1_desc >> 2u) & 0xFu);
        res.is_device = (res.mem_attr <= 0x3u);
        return res;
    }

    table_pa = l1_desc & 0x0000FFFFFFFFF000ULL;

    // --- Level 2 (2MB Blocks or Table to Level 3) ---
    const std::uint64_t l2_idx = (ipa >> 21u) & 0x1FFu;
    const std::uint64_t l2_entry_addr = table_pa + l2_idx * 8u;
    if (l2_entry_addr + 8u > mem_size || l2_entry_addr < table_pa) {
        return make_fault(::mmu::MmuFaultType::ExternalWalk, 2);
    }

    const std::uint64_t l2_desc = memory.read_u64(static_cast<std::size_t>(l2_entry_addr));
    if ((l2_desc & 0x1u) == 0) {
        return make_fault(::mmu::MmuFaultType::Translation, 2);
    }

    // Level 2 Block (2MB)
    if ((l2_desc & 0x2u) == 0) {
        if (((l2_desc >> 10u) & 0x1u) == 0) {
            return make_fault(::mmu::MmuFaultType::AccessFlag, 2);
        }
        const std::uint8_t s2ap = static_cast<std::uint8_t>((l2_desc >> 6u) & 0x3u);
        if (access == Stage2AccessType::Write && ((s2ap & 0x2u) == 0)) {
            return make_fault(::mmu::MmuFaultType::Permission, 2);
        }
        if (access == Stage2AccessType::Read && ((s2ap & 0x1u) == 0)) {
            return make_fault(::mmu::MmuFaultType::Permission, 2);
        }
        const bool xn = ((l2_desc >> 54u) & 0x1u) != 0;
        if (access == Stage2AccessType::Execute && xn) {
            return make_fault(::mmu::MmuFaultType::Permission, 2);
        }

        Stage2TranslationResult res;
        res.physical_address = (l2_desc & 0x0000FFFFFFE00000ULL) | (ipa & 0x1FFFFFULL);
        res.page_size = 2ULL * 1024 * 1024;
        res.readable = (s2ap & 0x1u) != 0;
        res.writable = (s2ap & 0x2u) != 0;
        res.executable = !xn;
        res.mem_attr = static_cast<std::uint8_t>((l2_desc >> 2u) & 0xFu);
        res.is_device = (res.mem_attr <= 0x3u);
        return res;
    }

    table_pa = l2_desc & 0x0000FFFFFFFFF000ULL;

    // --- Level 3 (4KB Pages) ---
    const std::uint64_t l3_idx = (ipa >> 12u) & 0x1FFu;
    const std::uint64_t l3_entry_addr = table_pa + l3_idx * 8u;
    if (l3_entry_addr + 8u > mem_size || l3_entry_addr < table_pa) {
        return make_fault(::mmu::MmuFaultType::ExternalWalk, 3);
    }

    const std::uint64_t l3_desc = memory.read_u64(static_cast<std::size_t>(l3_entry_addr));
    if ((l3_desc & 0x1u) == 0) {
        return make_fault(::mmu::MmuFaultType::Translation, 3);
    }

    if (((l3_desc >> 10u) & 0x1u) == 0) {
        return make_fault(::mmu::MmuFaultType::AccessFlag, 3);
    }
    const std::uint8_t s2ap = static_cast<std::uint8_t>((l3_desc >> 6u) & 0x3u);
    if (access == Stage2AccessType::Write && ((s2ap & 0x2u) == 0)) {
        return make_fault(::mmu::MmuFaultType::Permission, 3);
    }
    if (access == Stage2AccessType::Read && ((s2ap & 0x1u) == 0)) {
        return make_fault(::mmu::MmuFaultType::Permission, 3);
    }
    const bool xn = ((l3_desc >> 54u) & 0x1u) != 0;
    if (access == Stage2AccessType::Execute && xn) {
        return make_fault(::mmu::MmuFaultType::Permission, 3);
    }

    Stage2TranslationResult res;
    res.physical_address = (l3_desc & 0x0000FFFFFFFFF000ULL) | (ipa & 0xFFFULL);
    res.page_size = 4096ULL;
    res.readable = (s2ap & 0x1u) != 0;
    res.writable = (s2ap & 0x2u) != 0;
    res.executable = !xn;
    res.mem_attr = static_cast<std::uint8_t>((l3_desc >> 2u) & 0xFu);
    res.is_device = (res.mem_attr <= 0x3u);
    return res;
}

bool Stage2TranslationEngine::map_page_16k(
    Memory& memory,
    std::uint64_t table_base_pa,
    std::uint64_t ipa,
    std::uint64_t pa,
    bool readable,
    bool writable,
    bool executable,
    std::uint8_t mem_attr) {

    // Construct simple L2 -> L3 mapping in 16KB granule
    // L2 index: bits [35:25]
    // L3 index: bits [24:14]
    const std::uint64_t l2_idx = (ipa >> 25u) & 0x7FFu;
    const std::uint64_t l2_entry_addr = table_base_pa + l2_idx * 8u;

    std::uint64_t l3_table_pa = table_base_pa + 16384ULL; // allocate next 16KB for L3 table
    std::uint64_t l2_desc = memory.read_u64(static_cast<std::size_t>(l2_entry_addr));
    if ((l2_desc & 0x3u) == 0x3u) {
        l3_table_pa = l2_desc & 0x0000FFFFFFFFC000ULL;
    } else {
        // Table descriptor pointing to L3 table
        l2_desc = (l3_table_pa & 0x0000FFFFFFFFC000ULL) | 0x3u;
        memory.write_u64(static_cast<std::size_t>(l2_entry_addr), l2_desc);
    }

    const std::uint64_t l3_idx = (ipa >> 14u) & 0x7FFu;
    const std::uint64_t l3_entry_addr = l3_table_pa + l3_idx * 8u;

    std::uint64_t l3_desc = (pa & 0x0000FFFFFFFFC000ULL) | 0x3u; // Valid page
    l3_desc |= (1ULL << 10u); // Access Flag (AF) = 1

    std::uint8_t s2ap = 0;
    if (readable && writable) s2ap = 0b11;
    else if (writable) s2ap = 0b10;
    else if (readable) s2ap = 0b01;
    l3_desc |= (static_cast<std::uint64_t>(s2ap) << 6u);

    l3_desc |= (static_cast<std::uint64_t>(mem_attr & 0xFu) << 2u);

    if (!executable) {
        l3_desc |= (1ULL << 54u); // XN = 1
    }

    memory.write_u64(static_cast<std::size_t>(l3_entry_addr), l3_desc);
    return true;
}

bool Stage2TranslationEngine::map_page_4k(
    Memory& memory,
    std::uint64_t table_base_pa,
    std::uint64_t ipa,
    std::uint64_t pa,
    bool readable,
    bool writable,
    bool executable,
    std::uint8_t mem_attr) {

    // Simple L1 -> L2 -> L3 mapping in 4KB granule
    const std::uint64_t l1_idx = (ipa >> 30u) & 0x1FFu;
    const std::uint64_t l1_entry_addr = table_base_pa + l1_idx * 8u;

    std::uint64_t l2_table_pa = table_base_pa + 4096ULL;
    std::uint64_t l1_desc = memory.read_u64(static_cast<std::size_t>(l1_entry_addr));
    if ((l1_desc & 0x3u) == 0x3u) {
        l2_table_pa = l1_desc & 0x0000FFFFFFFFF000ULL;
    } else {
        l1_desc = (l2_table_pa & 0x0000FFFFFFFFF000ULL) | 0x3u;
        memory.write_u64(static_cast<std::size_t>(l1_entry_addr), l1_desc);
    }

    const std::uint64_t l2_idx = (ipa >> 21u) & 0x1FFu;
    const std::uint64_t l2_entry_addr = l2_table_pa + l2_idx * 8u;

    std::uint64_t l3_table_pa = l2_table_pa + 4096ULL;
    std::uint64_t l2_desc = memory.read_u64(static_cast<std::size_t>(l2_entry_addr));
    if ((l2_desc & 0x3u) == 0x3u) {
        l3_table_pa = l2_desc & 0x0000FFFFFFFFF000ULL;
    } else {
        l2_desc = (l3_table_pa & 0x0000FFFFFFFFF000ULL) | 0x3u;
        memory.write_u64(static_cast<std::size_t>(l2_entry_addr), l2_desc);
    }

    const std::uint64_t l3_idx = (ipa >> 12u) & 0x1FFu;
    const std::uint64_t l3_entry_addr = l3_table_pa + l3_idx * 8u;

    std::uint64_t l3_desc = (pa & 0x0000FFFFFFFFF000ULL) | 0x3u;
    l3_desc |= (1ULL << 10u); // AF = 1

    std::uint8_t s2ap = 0;
    if (readable && writable) s2ap = 0b11;
    else if (writable) s2ap = 0b10;
    else if (readable) s2ap = 0b01;
    l3_desc |= (static_cast<std::uint64_t>(s2ap) << 6u);

    l3_desc |= (static_cast<std::uint64_t>(mem_attr & 0xFu) << 2u);

    if (!executable) {
        l3_desc |= (1ULL << 54u); // XN = 1
    }

    memory.write_u64(static_cast<std::size_t>(l3_entry_addr), l3_desc);
    return true;
}

std::vector<std::uint8_t> Stage2TranslationEngine::serialize() const {
    std::vector<std::uint8_t> buf(24, 0);
    std::memcpy(buf.data() + 0, &vttbr_el2_, 8);
    std::memcpy(buf.data() + 8, &vtcr_el2_, 8);
    std::memcpy(buf.data() + 16, &hcr_el2_, 8);
    return buf;
}

bool Stage2TranslationEngine::deserialize(const void* data, std::size_t size) {
    if (!data || size < 24) return false;
    std::memcpy(&vttbr_el2_, static_cast<const std::uint8_t*>(data) + 0, 8);
    std::memcpy(&vtcr_el2_, static_cast<const std::uint8_t*>(data) + 8, 8);
    std::memcpy(&hcr_el2_, static_cast<const std::uint8_t*>(data) + 16, 8);
    return true;
}

} // namespace winios::mmu

#include "memory/virtual_page_table.h"

namespace winios::memory {

std::uint64_t VirtualPageTable::make_table_descriptor(std::uint64_t next_table_pa) {
    // ARM64 Level 2 Table descriptor (16KB granule)
    // Bits [47:14]: Next-level table physical address (16KB aligned)
    // Bits [1:0]: 0b11 (Table)
    return (next_table_pa & 0x0000FFFFFFFFC000ULL) | 0x3ULL;
}

std::uint64_t VirtualPageTable::make_page_descriptor(
    std::uint64_t physical_page_addr,
    PagePermissions perms,
    PageMemoryType mem_type) {
    // ARM64 Level 3 Page descriptor (16KB page)
    // Bits [47:14]: Output physical address
    // Bits [1:0]: 0b11 (Page)
    // Bit 10: AF = 1
    // Bits [9:8]: SH = 3 (Inner Shareable)
    std::uint64_t desc = (physical_page_addr & 0x0000FFFFFFFFC000ULL) | 0x3ULL;
    desc |= (1ULL << 10); // AF = 1
    desc |= (3ULL << 8);  // SH = 3

    // Memory attribute index: 0 = Normal, 1 = Device
    const std::uint64_t attr_idx = (mem_type == PageMemoryType::NormalMemory) ? 0ULL : 1ULL;
    desc |= (attr_idx << 2);

    // Permission encoding:
    // AP[2:1] in bits [7:6]:
    //   AP[1] = EL0 accessibility (0 = EL1 only, 1 = EL0+EL1)
    //   AP[2] = Read-only (0 = Read/Write, 1 = Read-only)
    // PXN in bit 53
    // UXN in bit 54
    switch (perms) {
        case PagePermissions::KernelRead:
            desc |= (2ULL << 6); // AP = 0b10 (RO, EL1 only)
            desc |= (1ULL << 53); // PXN = 1
            desc |= (1ULL << 54); // UXN = 1
            break;
        case PagePermissions::KernelReadWrite:
            desc |= (0ULL << 6); // AP = 0b00 (RW, EL1 only)
            desc |= (1ULL << 53); // PXN = 1
            desc |= (1ULL << 54); // UXN = 1
            break;
        case PagePermissions::KernelExecute:
            desc |= (2ULL << 6); // AP = 0b10 (RO, EL1 only)
            desc |= (0ULL << 53); // PXN = 0 (Exec allowed at EL1)
            desc |= (1ULL << 54); // UXN = 1
            break;
        case PagePermissions::UserRead:
            desc |= (3ULL << 6); // AP = 0b11 (RO, EL0+EL1)
            desc |= (1ULL << 11); // nG = 1 (non-global, tagged with ASID)
            desc |= (1ULL << 53); // PXN = 1
            desc |= (1ULL << 54); // UXN = 1
            break;
        case PagePermissions::UserReadWrite:
            desc |= (1ULL << 6); // AP = 0b01 (RW, EL0+EL1)
            desc |= (1ULL << 11); // nG = 1 (non-global, tagged with ASID)
            desc |= (1ULL << 53); // PXN = 1
            desc |= (1ULL << 54); // UXN = 1
            break;
        case PagePermissions::UserExecute:
            desc |= (3ULL << 6); // AP = 0b11 (RO, EL0+EL1)
            desc |= (1ULL << 11); // nG = 1 (non-global, tagged with ASID)
            desc |= (1ULL << 53); // PXN = 1
            desc |= (0ULL << 54); // UXN = 0 (Exec allowed at EL0)
            break;
    }

    return desc;
}

std::uint64_t VirtualPageTable::make_block_descriptor(
    std::uint64_t physical_block_addr,
    PagePermissions perms,
    PageMemoryType mem_type) {
    // ARM64 Level 2 Block descriptor (32MB block)
    // Bits [47:25]: Output physical address
    // Bits [1:0]: 0b01 (Block)
    std::uint64_t desc = (physical_block_addr & 0x0000FFFFFE000000ULL) | 0x1ULL;
    desc |= (1ULL << 10); // AF = 1
    desc |= (3ULL << 8);  // SH = 3

    const std::uint64_t attr_idx = (mem_type == PageMemoryType::NormalMemory) ? 0ULL : 1ULL;
    desc |= (attr_idx << 2);

    switch (perms) {
        case PagePermissions::KernelRead:
            desc |= (2ULL << 6);
            desc |= (1ULL << 53);
            desc |= (1ULL << 54);
            break;
        case PagePermissions::KernelReadWrite:
            desc |= (0ULL << 6);
            desc |= (1ULL << 53);
            desc |= (1ULL << 54);
            break;
        case PagePermissions::KernelExecute:
            desc |= (2ULL << 6);
            desc |= (0ULL << 53);
            desc |= (1ULL << 54);
            break;
        case PagePermissions::UserRead:
            desc |= (3ULL << 6);
            desc |= (1ULL << 11);
            desc |= (1ULL << 53);
            desc |= (1ULL << 54);
            break;
        case PagePermissions::UserReadWrite:
            desc |= (1ULL << 6);
            desc |= (1ULL << 11);
            desc |= (1ULL << 53);
            desc |= (1ULL << 54);
            break;
        case PagePermissions::UserExecute:
            desc |= (3ULL << 6);
            desc |= (1ULL << 11);
            desc |= (1ULL << 53);
            desc |= (0ULL << 54);
            break;
    }

    return desc;
}

PageDescriptorFields VirtualPageTable::parse_descriptor(std::uint64_t desc, unsigned level) {
    PageDescriptorFields f;
    const std::uint8_t type = static_cast<std::uint8_t>(desc & 0x3ULL);
    if (type == 0) {
        f.valid = false;
        return f;
    }
    f.valid = true;

    if (level == 2) {
        if (type == 1) {
            f.is_block = true;
            f.physical_address = desc & 0x0000FFFFFE000000ULL;
        } else if (type == 3) {
            f.is_table = true;
            f.physical_address = desc & 0x0000FFFFFFFFC000ULL;
        }
    } else if (level == 3) {
        if (type == 3) {
            f.is_page = true;
            f.physical_address = desc & 0x0000FFFFFFFFC000ULL;
        }
    }

    const std::uint8_t ap = static_cast<std::uint8_t>((desc >> 6u) & 0x3u);
    f.el0_accessible = (ap & 1u) != 0;
    f.writable = (ap & 2u) == 0;
    f.privileged_execute = ((desc >> 53u) & 1u) == 0;
    f.user_execute = ((desc >> 54u) & 1u) == 0;
    f.attr_index = static_cast<std::uint8_t>((desc >> 2u) & 7u);
    f.shareability = static_cast<std::uint8_t>((desc >> 8u) & 3u);
    return f;
}

bool VirtualPageTable::map_page(
    std::uint64_t root_l2_table_pa,
    std::uint64_t virtual_addr,
    std::uint64_t physical_addr,
    PagePermissions perms,
    PageMemoryType mem_type,
    PhysicalPageManager* ppm) {
    if ((virtual_addr % kPhysicalPageSize16KB) != 0 || (physical_addr % kPhysicalPageSize16KB) != 0) {
        return false;
    }

    const std::uint64_t l2_idx = (virtual_addr >> 25u) & 0x7FFu;
    const std::uint64_t l2_entry_addr = root_l2_table_pa + (l2_idx * 8u);
    if (l2_entry_addr + 8u > mem_.size()) {
        return false;
    }

    std::uint64_t l2_desc = mem_.read_u64(static_cast<std::size_t>(l2_entry_addr));
    std::uint64_t l3_table_pa = 0;

    if ((l2_desc & 0x3u) == 0x3u) {
        // Valid L2 table descriptor pointing to L3 table
        l3_table_pa = l2_desc & 0x0000FFFFFFFFC000ULL;
    } else {
        // Need to allocate an L3 table page
        if (ppm == nullptr) {
            return false;
        }
        l3_table_pa = ppm->allocate_page();
        if (l3_table_pa == 0) {
            return false;
        }

        // Zero out the newly allocated L3 table
        for (std::size_t i = 0; i < kPhysicalPageSize16KB; ++i) {
            mem_.write_u8(static_cast<std::size_t>(l3_table_pa + i), 0);
        }

        // Install L2 table descriptor
        l2_desc = make_table_descriptor(l3_table_pa);
        mem_.write_u64(static_cast<std::size_t>(l2_entry_addr), l2_desc);
    }

    // Install L3 page descriptor
    const std::uint64_t l3_idx = (virtual_addr >> 14u) & 0x7FFu;
    const std::uint64_t l3_entry_addr = l3_table_pa + (l3_idx * 8u);
    if (l3_entry_addr + 8u > mem_.size()) {
        return false;
    }

    const std::uint64_t l3_desc = make_page_descriptor(physical_addr, perms, mem_type);
    mem_.write_u64(static_cast<std::size_t>(l3_entry_addr), l3_desc);
    return true;
}

bool VirtualPageTable::map_range(
    std::uint64_t root_l2_table_pa,
    std::uint64_t start_va,
    std::uint64_t start_pa,
    std::size_t num_pages,
    PagePermissions perms,
    PageMemoryType mem_type,
    PhysicalPageManager* ppm) {
    for (std::size_t i = 0; i < num_pages; ++i) {
        const std::uint64_t va = start_va + (i * kPhysicalPageSize16KB);
        const std::uint64_t pa = start_pa + (i * kPhysicalPageSize16KB);
        if (!map_page(root_l2_table_pa, va, pa, perms, mem_type, ppm)) {
            return false;
        }
    }
    return true;
}

bool VirtualPageTable::unmap_page(std::uint64_t root_l2_table_pa, std::uint64_t virtual_addr) {
    const std::uint64_t l2_idx = (virtual_addr >> 25u) & 0x7FFu;
    const std::uint64_t l2_entry_addr = root_l2_table_pa + (l2_idx * 8u);
    if (l2_entry_addr + 8u > mem_.size()) {
        return false;
    }

    const std::uint64_t l2_desc = mem_.read_u64(static_cast<std::size_t>(l2_entry_addr));
    if ((l2_desc & 0x3u) != 0x3u) {
        return false; // No L3 table present
    }

    const std::uint64_t l3_table_pa = l2_desc & 0x0000FFFFFFFFC000ULL;
    const std::uint64_t l3_idx = (virtual_addr >> 14u) & 0x7FFu;
    const std::uint64_t l3_entry_addr = l3_table_pa + (l3_idx * 8u);
    if (l3_entry_addr + 8u > mem_.size()) {
        return false;
    }

    mem_.write_u64(static_cast<std::size_t>(l3_entry_addr), 0); // Clear descriptor
    return true;
}

std::uint64_t VirtualPageTable::translate(
    std::uint64_t root_l2_table_pa,
    std::uint64_t virtual_addr,
    PageDescriptorFields* out_fields) const {
    const std::uint64_t l2_idx = (virtual_addr >> 25u) & 0x7FFu;
    const std::uint64_t l2_entry_addr = root_l2_table_pa + (l2_idx * 8u);
    if (l2_entry_addr + 8u > mem_.size()) {
        return 0;
    }

    const std::uint64_t l2_desc = mem_.read_u64(static_cast<std::size_t>(l2_entry_addr));
    const auto l2_fields = parse_descriptor(l2_desc, 2);
    if (!l2_fields.valid) {
        return 0;
    }

    if (l2_fields.is_block) {
        if (out_fields != nullptr) *out_fields = l2_fields;
        return (l2_desc & 0x0000FFFFFE000000ULL) | (virtual_addr & 0x01FFFFFFULL);
    }

    if (l2_fields.is_table) {
        const std::uint64_t l3_table_pa = l2_desc & 0x0000FFFFFFFFC000ULL;
        const std::uint64_t l3_idx = (virtual_addr >> 14u) & 0x7FFu;
        const std::uint64_t l3_entry_addr = l3_table_pa + (l3_idx * 8u);
        if (l3_entry_addr + 8u > mem_.size()) {
            return 0;
        }

        const std::uint64_t l3_desc = mem_.read_u64(static_cast<std::size_t>(l3_entry_addr));
        const auto l3_fields = parse_descriptor(l3_desc, 3);
        if (!l3_fields.valid || !l3_fields.is_page) {
            return 0;
        }
        if (out_fields != nullptr) *out_fields = l3_fields;
        return (l3_desc & 0x0000FFFFFFFFC000ULL) | (virtual_addr & 0x3FFFULL);
    }

    return 0;
}

std::vector<std::uint32_t> VirtualPageTable::emit_guest_map_routine() {
    // ARM64 routine: installs descriptor raw (X2) into L3 table (X0) for VA (X1)
    return {
        0xD34EFC23u, // [00] lsr x3, x1, #14 (extract page frame number)
        0x92402863u, // [04] and x3, x3, #0x7FF (l3 index)
        0xD37DF063u, // [08] lsl x3, x3, #3 (offset in bytes = l3_idx * 8)
        0xF8236802u, // [0c] str x2, [x0, x3] (write descriptor to table in RAM)
        0xD2800000u, // [10] mov x0, #0
        0xD65F03C0u  // [14] ret
    };
}

} // namespace winios::memory

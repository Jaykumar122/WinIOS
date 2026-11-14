#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "memory/memory.h"
#include "memory/physical_page_manager.h"

namespace winios::memory {

enum class PagePermissions {
    KernelRead,        // EL1 Read-only (AP=0b10, PXN=1, UXN=1)
    KernelReadWrite,   // EL1 Read-Write (AP=0b00, PXN=1, UXN=1)
    KernelExecute,     // EL1 Read+Execute (AP=0b10, PXN=0, UXN=1)
    UserRead,          // EL0+EL1 Read-only (AP=0b11, PXN=1, UXN=1)
    UserReadWrite,     // EL0+EL1 Read-Write (AP=0b01, PXN=1, UXN=1)
    UserExecute        // EL0+EL1 Execute (AP=0b11, PXN=1, UXN=0)
};

enum class PageMemoryType {
    NormalMemory,      // AttrIndx 0 (Normal Write-Back Cacheable)
    DeviceMemory       // AttrIndx 1 (Device nGnRnE)
};

struct PageDescriptorFields {
    std::uint64_t physical_address{0};
    bool valid{false};
    bool is_table{false};
    bool is_block{false};
    bool is_page{false};
    bool el0_accessible{false};
    bool writable{false};
    bool privileged_execute{false};
    bool user_execute{false};
    std::uint8_t attr_index{0};
    std::uint8_t shareability{0};
};

class VirtualPageTable {
public:
    explicit VirtualPageTable(Memory& mem) : mem_(mem) {}
    ~VirtualPageTable() = default;

    // Descriptors builders
    static std::uint64_t make_table_descriptor(std::uint64_t next_table_pa);
    static std::uint64_t make_page_descriptor(
        std::uint64_t physical_page_addr,
        PagePermissions perms,
        PageMemoryType mem_type = PageMemoryType::NormalMemory);
    static std::uint64_t make_block_descriptor(
        std::uint64_t physical_block_addr,
        PagePermissions perms,
        PageMemoryType mem_type = PageMemoryType::NormalMemory);

    // Descriptor parser / walker helper
    static PageDescriptorFields parse_descriptor(std::uint64_t desc, unsigned level);

    // Page mapping in guest memory
    bool map_page(
        std::uint64_t root_l2_table_pa,
        std::uint64_t virtual_addr,
        std::uint64_t physical_addr,
        PagePermissions perms,
        PageMemoryType mem_type = PageMemoryType::NormalMemory,
        PhysicalPageManager* ppm = nullptr);

    bool map_range(
        std::uint64_t root_l2_table_pa,
        std::uint64_t start_va,
        std::uint64_t start_pa,
        std::size_t num_pages,
        PagePermissions perms,
        PageMemoryType mem_type = PageMemoryType::NormalMemory,
        PhysicalPageManager* ppm = nullptr);

    bool unmap_page(std::uint64_t root_l2_table_pa, std::uint64_t virtual_addr);

    // Walk translation table in guest memory and resolve virtual address
    [[nodiscard]] std::uint64_t translate(
        std::uint64_t root_l2_table_pa,
        std::uint64_t virtual_addr,
        PageDescriptorFields* out_fields = nullptr) const;

    // Emits guest ARM64 EL1 code that installs a page table entry directly in guest memory
    // Input: X0 = root_table_pa, X1 = virtual_addr, X2 = physical_addr, X3 = descriptor_raw
    [[nodiscard]] static std::vector<std::uint32_t> emit_guest_map_routine();

private:
    Memory& mem_;
};

} // namespace winios::memory

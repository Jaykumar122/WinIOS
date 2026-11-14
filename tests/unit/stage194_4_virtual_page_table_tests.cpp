// ============================================================================
// Stage 194.4: Guest Virtual Memory Manager (Page Table Walker & Descriptor Writer) Tests
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/virtual_page_table.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::memory;

// -----------------------------------------------------------------------------
// Test 1: Descriptor Construction, Bit Patterns, and Permission Parsing
// -----------------------------------------------------------------------------
static bool test_descriptor_construction_and_parsing() {
    // 1. Table Descriptor
    const std::uint64_t next_table_pa = 0x80000ULL;
    const std::uint64_t table_desc = VirtualPageTable::make_table_descriptor(next_table_pa);
    CHECK((table_desc & 0x3) == 0x3, "Table descriptor bits 1..0 must be 0b11");
    CHECK((table_desc & 0x0000FFFFFFFFC000ULL) == next_table_pa, "Next table address must match");

    auto table_fields = VirtualPageTable::parse_descriptor(table_desc, 2);
    CHECK(table_fields.valid, "Table descriptor must be valid");
    CHECK(table_fields.is_table, "Descriptor must be recognized as table");
    CHECK(table_fields.physical_address == next_table_pa, "Extracted physical address must match");

    // 2. Kernel Read-Write Page Descriptor
    const std::uint64_t page_pa = 0x40000ULL;
    const std::uint64_t krw_desc = VirtualPageTable::make_page_descriptor(
        page_pa, PagePermissions::KernelReadWrite, PageMemoryType::NormalMemory);
    CHECK((krw_desc & 0x3) == 0x3, "Page descriptor bits 1..0 must be 0b11");
    CHECK(((krw_desc >> 10) & 1) == 1, "Access Flag (AF) must be 1");
    CHECK(((krw_desc >> 8) & 3) == 3, "Shareability (SH) must be 3");

    auto krw_fields = VirtualPageTable::parse_descriptor(krw_desc, 3);
    CHECK(krw_fields.valid && krw_fields.is_page, "Page descriptor must be valid page");
    CHECK(!krw_fields.el0_accessible, "Kernel page must not be EL0 accessible");
    CHECK(krw_fields.writable, "KernelReadWrite page must be writable");
    CHECK(!krw_fields.privileged_execute, "KernelReadWrite page must not be executable (PXN=1)");

    // 3. User Read-Write Page Descriptor
    const std::uint64_t urw_desc = VirtualPageTable::make_page_descriptor(
        page_pa, PagePermissions::UserReadWrite, PageMemoryType::NormalMemory);
    auto urw_fields = VirtualPageTable::parse_descriptor(urw_desc, 3);
    CHECK(urw_fields.el0_accessible, "User page must be EL0 accessible");
    CHECK(urw_fields.writable, "UserReadWrite page must be writable");

    // 4. Kernel Execute Page Descriptor
    const std::uint64_t kexec_desc = VirtualPageTable::make_page_descriptor(
        page_pa, PagePermissions::KernelExecute, PageMemoryType::NormalMemory);
    auto kexec_fields = VirtualPageTable::parse_descriptor(kexec_desc, 3);
    CHECK(!kexec_fields.el0_accessible, "Kernel execute page must not be EL0 accessible");
    CHECK(!kexec_fields.writable, "Kernel execute page must not be writable (W^X)");
    CHECK(kexec_fields.privileged_execute, "Kernel execute page must be privileged executable");

    std::cout << "[PASS] test_descriptor_construction_and_parsing\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Dynamic Page Table Hierarchy Construction and Translation
// -----------------------------------------------------------------------------
static bool test_page_table_hierarchy_and_translation() {
    Memory mem(0x400000ULL); // 4 MB guest RAM
    VirtualPageTable vpt(mem);

    // Root L2 table placed at 0x40000 (16KB aligned)
    const std::uint64_t root_l2_pa = 0x40000ULL;
    for (std::size_t i = 0; i < kPhysicalPageSize16KB; ++i) {
        mem.write_u8(static_cast<std::size_t>(root_l2_pa + i), 0);
    }

    // Physical Page Manager for L3 tables and backing physical pages
    // Managed RAM: 0x100000 .. 0x400000
    PhysicalPageManager ppm(mem, 0x100000ULL, 0x300000ULL, 0x10000ULL);
    CHECK(ppm.initialize(false), "PPM init");

    // Map Virtual Address 0x10000000 to Physical Address 0x200000
    const std::uint64_t target_va = 0x10000000ULL;
    const std::uint64_t target_pa = 0x200000ULL;
    CHECK(vpt.map_page(root_l2_pa, target_va, target_pa, PagePermissions::KernelReadWrite,
                       PageMemoryType::NormalMemory, &ppm),
          "Mapping single page must succeed");

    // Verify translation of page base
    PageDescriptorFields fields{};
    const std::uint64_t resolved_pa = vpt.translate(root_l2_pa, target_va, &fields);
    CHECK(resolved_pa == target_pa, "Resolved physical address must match target PA");
    CHECK(fields.valid && fields.is_page, "Resolved descriptor must be valid page");
    CHECK(fields.writable, "Resolved descriptor must be writable");

    // Verify translation preserves within-page offset
    const std::uint64_t offset_va = target_va + 0x1234ULL;
    const std::uint64_t resolved_offset = vpt.translate(root_l2_pa, offset_va);
    CHECK(resolved_offset == target_pa + 0x1234ULL, "Within-page offset must be preserved");

    // Unmapped address translates to 0
    CHECK(vpt.translate(root_l2_pa, 0x20000000ULL) == 0, "Unmapped address must translate to 0");

    std::cout << "[PASS] test_page_table_hierarchy_and_translation\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Contiguous Range Mapping and Page Unmapping
// -----------------------------------------------------------------------------
static bool test_range_mapping_and_unmapping() {
    Memory mem(0x400000ULL);
    VirtualPageTable vpt(mem);

    const std::uint64_t root_l2_pa = 0x40000ULL;
    for (std::size_t i = 0; i < kPhysicalPageSize16KB; ++i) {
        mem.write_u8(static_cast<std::size_t>(root_l2_pa + i), 0);
    }

    PhysicalPageManager ppm(mem, 0x100000ULL, 0x300000ULL, 0x10000ULL);
    CHECK(ppm.initialize(false), "PPM init");

    // Map 4 contiguous pages: 0x20000000 -> 0x300000 (4 * 16KB)
    const std::uint64_t start_va = 0x20000000ULL;
    const std::uint64_t start_pa = 0x300000ULL;
    CHECK(vpt.map_range(root_l2_pa, start_va, start_pa, 4, PagePermissions::KernelReadWrite,
                        PageMemoryType::NormalMemory, &ppm),
          "Mapping range must succeed");

    for (std::size_t i = 0; i < 4; ++i) {
        const std::uint64_t va = start_va + (i * kPhysicalPageSize16KB);
        const std::uint64_t expected = start_pa + (i * kPhysicalPageSize16KB);
        CHECK(vpt.translate(root_l2_pa, va) == expected, "Page in mapped range must translate correctly");
    }

    // Unmap page 2 (index 2: 0x20008000)
    const std::uint64_t unmap_va = start_va + (2 * kPhysicalPageSize16KB);
    CHECK(vpt.unmap_page(root_l2_pa, unmap_va), "Unmapping page must succeed");
    CHECK(vpt.translate(root_l2_pa, unmap_va) == 0, "Unmapped page must resolve to 0");

    // Pages 0, 1, and 3 must remain intact
    CHECK(vpt.translate(root_l2_pa, start_va) == start_pa, "Page 0 still mapped");
    CHECK(vpt.translate(root_l2_pa, start_va + (1 * kPhysicalPageSize16KB)) == start_pa + (1 * kPhysicalPageSize16KB), "Page 1 still mapped");
    CHECK(vpt.translate(root_l2_pa, start_va + (3 * kPhysicalPageSize16KB)) == start_pa + (3 * kPhysicalPageSize16KB), "Page 3 still mapped");

    std::cout << "[PASS] test_range_mapping_and_unmapping\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Guest ARM64 EL1 Machine Code Page Mapping Routine
// -----------------------------------------------------------------------------
static bool test_guest_arm64_map_routine() {
    Memory mem(0x200000ULL);
    CPU cpu;
    cpu.set_architectural_exceptions(true);

    const std::uint64_t code_addr = 0x8000ULL;
    const std::uint64_t l3_table_pa = 0x60000ULL; // 16KB aligned L3 table
    for (std::size_t i = 0; i < kPhysicalPageSize16KB; ++i) {
        mem.write_u8(static_cast<std::size_t>(l3_table_pa + i), 0);
    }

    // Emit map routine into guest RAM
    auto map_code = VirtualPageTable::emit_guest_map_routine();
    for (std::size_t i = 0; i < map_code.size(); ++i) {
        mem.write_u32(static_cast<std::size_t>(code_addr + (i * 4)), map_code[i]);
    }

    // Target mapping parameters
    const std::uint64_t target_va = 0x00010000ULL; // page 4 (idx 4)
    const std::uint64_t target_pa = 0x000A0000ULL;
    const std::uint64_t desc = VirtualPageTable::make_page_descriptor(
        target_pa, PagePermissions::KernelReadWrite, PageMemoryType::NormalMemory);

    // Configure CPU at EL1 to run map routine
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(code_addr);
    cpu.set_stack_pointer(0x70000ULL);
    cpu.write_register(0, l3_table_pa);
    cpu.write_register(1, target_va);
    cpu.write_register(2, desc);
    cpu.write_register(30, 0x9000ULL); // LR = halt address

    std::size_t steps = 0;
    while (cpu.get_program_counter() != 0x9000ULL && steps < 50) {
        cpu.step(mem);
        ++steps;
    }

    CHECK(cpu.get_program_counter() == 0x9000ULL, "Routine must return to LR");
    CHECK(cpu.read_register(0) == 0, "Return value in X0 must be 0");

    // Verify descriptor written into L3 entry in guest RAM
    const std::uint64_t l3_idx = (target_va >> 14u) & 0x7FFu;
    const std::uint64_t written_desc = mem.read_u64(static_cast<std::size_t>(l3_table_pa + l3_idx * 8u));
    CHECK(written_desc == desc, "Written descriptor in guest memory must match expected descriptor");

    std::cout << "[PASS] test_guest_arm64_map_routine (Retired " << steps << " instructions)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Full Stage-1 MMU Hardware Translation Integration
// -----------------------------------------------------------------------------
static bool test_stage1_mmu_hardware_translation_integration() {
    Memory mem(0x400000ULL); // 4 MB guest RAM
    VirtualPageTable vpt(mem);

    const std::uint64_t root_l2_pa = 0x40000ULL;
    for (std::size_t i = 0; i < kPhysicalPageSize16KB; ++i) {
        mem.write_u8(static_cast<std::size_t>(root_l2_pa + i), 0);
    }

    PhysicalPageManager ppm(mem, 0x100000ULL, 0x300000ULL, 0x10000ULL);
    CHECK(ppm.initialize(false), "PPM init");

    // Map VA 0x10000000 -> PA 0x80000 as KernelReadWrite NormalMemory
    const std::uint64_t test_va = 0x10000000ULL;
    const std::uint64_t test_pa = 0x80000ULL;
    CHECK(vpt.map_page(root_l2_pa, test_va, test_pa, PagePermissions::KernelReadWrite,
                       PageMemoryType::NormalMemory, &ppm),
          "Mapping test page must succeed");

    // Write a sentinel payload into physical memory
    const std::uint64_t magic_payload = 0xCAFEBABE12345678ULL;
    mem.write_u64(static_cast<std::size_t>(test_pa), magic_payload);

    // Setup guest code at physical address 0x2000 (we'll identity-map it so CPU can fetch insns!)
    // Map VA 0x00000000 .. 0x00040000 to PA 0x00000000 .. 0x00040000 as KernelExecute
    CHECK(vpt.map_range(root_l2_pa, 0x0ULL, 0x0ULL, 4, PagePermissions::KernelExecute,
                        PageMemoryType::NormalMemory, &ppm),
          "Identity mapping code pages");

    // Guest code at 0x2000:
    // 0x2000: MOV X1, #0x10000000 (test_va)
    //         movz x1, #0x1000, lsl #16 -> 0xD2A20001
    // 0x2004: LDR X0, [X1]              -> 0xF9400020
    // 0x2008: B .                       -> 0x14000000
    const std::uint64_t code_pa = 0x2000ULL;
    mem.write_u32(static_cast<std::size_t>(code_pa + 0u), 0xD2A20001u); // MOVZ X1, #0x1000, LSL #16
    mem.write_u32(static_cast<std::size_t>(code_pa + 4u), 0xF9400020u); // LDR X0, [X1]
    mem.write_u32(static_cast<std::size_t>(code_pa + 8u), 0x14000000u); // B .

    CPU cpu;
    cpu.set_architectural_exceptions(true);

    // Configure MMU registers for 16KB Granule Stage-1 translation
    mmu::MMU& mmu = cpu.get_mmu();
    mmu.set_ttbr0_el1(root_l2_pa);
    // TCR_EL1: TG0 = 0b10 (16KB granule), T0SZ = 28 (36-bit VA) -> (0b10 << 14) | 28 = 0x801C
    mmu.set_tcr_el1((2ULL << 14) | 28ULL);
    // MAIR_EL1: Attr0 = 0xFF (Normal WB), Attr1 = 0x04 (Device nGnRE)
    mmu.set_mair_el1(0x00000000000004FFULL);
    // SCTLR_EL1: Enable MMU (M bit = 1)
    mmu.set_sctlr_el1(mmu.get_sctlr_el1() | 1ULL);

    // Boot CPU into EL1
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(code_pa);
    cpu.set_stack_pointer(0x70000ULL);

    // Step 1: Execute MOVZ X1, #0x1000, LSL #16
    cpu.step(mem);
    CHECK(cpu.read_register(1) == test_va, "X1 must hold virtual address 0x10000000");

    // Step 2: Execute LDR X0, [X1] (MMU translates VA 0x10000000 -> PA 0x80000)
    cpu.step(mem);
    CHECK(cpu.read_register(0) == magic_payload,
          "X0 must contain the magic payload translated through Stage-1 MMU");

    std::cout << "[PASS] test_stage1_mmu_hardware_translation_integration (Read 0x"
              << std::hex << cpu.read_register(0) << std::dec << " via MMU walk)\n";
    return true;
}

int main() {
    std::cout << "Running Stage 194.4 Guest Virtual Page Table Tests...\n";

    if (!test_descriptor_construction_and_parsing()) return 1;
    if (!test_page_table_hierarchy_and_translation()) return 1;
    if (!test_range_mapping_and_unmapping()) return 1;
    if (!test_guest_arm64_map_routine()) return 1;
    if (!test_stage1_mmu_hardware_translation_integration()) return 1;

    std::cout << "\nAll Stage 194.4 Guest Virtual Page Table tests PASSED successfully.\n";
    return 0;
}

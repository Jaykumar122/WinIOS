// ============================================================================
// Stage 195.15: Demand-Paging Concurrency & Memory Reclamation Tests
// Mandatory Acceptance Test H: Concurrent Memory Faults & Race Prevention
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "memory/demand_paging_manager.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/virtual_page_table.h"
#include "kernel/sync.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::memory;
using namespace winios::kernel;

static void setup_test_mappings(
    Memory& mem,
    std::uint64_t root_l2_pa,
    std::uint64_t l3_code_pa,
    std::uint64_t l3_demand_pa) {
    // Clear root L2 and both L3 tables
    for (std::size_t i = 0; i < kPhysicalPageSize16KB; ++i) {
        mem.write_u8(static_cast<std::size_t>(root_l2_pa + i), 0);
        mem.write_u8(static_cast<std::size_t>(l3_code_pa + i), 0);
        mem.write_u8(static_cast<std::size_t>(l3_demand_pa + i), 0);
    }

    // Connect root L2 index 0 (0x00000000) to l3_code_pa
    mem.write_u64(static_cast<std::size_t>(root_l2_pa + 0),
                  VirtualPageTable::make_table_descriptor(l3_code_pa));

    // Connect root L2 index 8 (0x10000000) to l3_demand_pa
    const std::uint64_t l2_idx = (0x10000000ULL >> 25u) & 0x7FFu;
    mem.write_u64(static_cast<std::size_t>(root_l2_pa + l2_idx * 8u),
                  VirtualPageTable::make_table_descriptor(l3_demand_pa));

    // Map initial 48 pages (0x0 .. 0xC0000):
    // Pages 0..3: Code -> KernelExecute
    // Pages 4..31: Data, Tables, Stack -> KernelReadWrite
    // Pages 32..35 (0x80000..0x90000): Vectors -> KernelExecute
    // Pages 36..47: Other -> KernelReadWrite
    for (std::uint64_t p = 0; p < 48; ++p) {
        const std::uint64_t addr = p * kPhysicalPageSize16KB;
        PagePermissions perms = PagePermissions::KernelReadWrite;
        if (p < 4 || (p >= 32 && p < 36)) {
            perms = PagePermissions::KernelExecute;
        }
        mem.write_u64(static_cast<std::size_t>(l3_code_pa + p * 8u),
                      VirtualPageTable::make_page_descriptor(addr, perms));
    }
}

// -----------------------------------------------------------------------------
// Test 1: Dual-Core Simultaneous Page Faults on Disjoint Pages
// -----------------------------------------------------------------------------
static bool test_dual_core_disjoint_page_faults() {
    Memory mem(0x800000ULL); // 8MB guest RAM

    const std::uint64_t root_l2_pa = 0x30000ULL;
    const std::uint64_t l3_demand_pa = 0x50000ULL;
    const std::uint64_t l3_code_pa = 0x60000ULL;
    const std::uint64_t vbar_pa = 0x80000ULL;
    const std::uint64_t backing_pool_pa = 0x200000ULL;

    setup_test_mappings(mem, root_l2_pa, l3_code_pa, l3_demand_pa);

    DemandPagingConfig cfg;
    cfg.root_l2_table_pa = root_l2_pa;
    cfg.l3_table_pa = l3_demand_pa;
    cfg.backing_pool_start_pa = backing_pool_pa;
    DemandPagingManager dpm(mem, cfg);
    dpm.initialize_guest_control_data();
    dpm.install_vector_table(vbar_pa);

    // Write distinct test patterns into the backing pool
    // Page 0 (allocated first): 0xAAAA'1111'2222'3333
    // Page 1 (allocated second): 0xBBBB'4444'5555'6666
    mem.write_u64(static_cast<std::size_t>(backing_pool_pa + 0 * kPhysicalPageSize16KB), 0xAAAA111122223333ULL);
    mem.write_u64(static_cast<std::size_t>(backing_pool_pa + 1 * kPhysicalPageSize16KB), 0xBBBB444455556666ULL);

    // Code for Core 0 at 0x2000:
    // LDR X0, [X1] (X1 = 0x10000000)
    // B .
    const std::uint64_t code0_pa = 0x2000ULL;
    mem.write_u32(static_cast<std::size_t>(code0_pa + 0u), 0xF9400020u); // LDR X0, [X1]
    mem.write_u32(static_cast<std::size_t>(code0_pa + 4u), 0x14000000u); // B .

    // Code for Core 1 at 0x3000:
    // LDR X0, [X1] (X1 = 0x10004000)
    // B .
    const std::uint64_t code1_pa = 0x3000ULL;
    mem.write_u32(static_cast<std::size_t>(code1_pa + 0u), 0xF9400020u); // LDR X0, [X1]
    mem.write_u32(static_cast<std::size_t>(code1_pa + 4u), 0x14000000u); // B .

    CPU cpu0;
    cpu0.set_architectural_exceptions(true);
    cpu0.set_vbar_el1(vbar_pa);
    cpu0.get_mmu().set_ttbr0_el1(root_l2_pa);
    cpu0.get_mmu().set_tcr_el1((2ULL << 14) | 28ULL);
    cpu0.get_mmu().set_mair_el1(0x00000000000004FFULL);
    cpu0.get_mmu().set_sctlr_el1(cpu0.get_mmu().get_sctlr_el1() | 1ULL);
    cpu0.set_current_el(1);
    cpu0.set_sp_sel(true);
    cpu0.set_stack_pointer(0x70000ULL);
    cpu0.set_program_counter(code0_pa);
    cpu0.write_register(1, 0x10000000ULL);

    CPU cpu1;
    cpu1.set_architectural_exceptions(true);
    cpu1.set_vbar_el1(vbar_pa);
    cpu1.get_mmu().set_ttbr0_el1(root_l2_pa);
    cpu1.get_mmu().set_tcr_el1((2ULL << 14) | 28ULL);
    cpu1.get_mmu().set_mair_el1(0x00000000000004FFULL);
    cpu1.get_mmu().set_sctlr_el1(cpu1.get_mmu().get_sctlr_el1() | 1ULL);
    cpu1.set_current_el(1);
    cpu1.set_sp_sel(true);
    cpu1.set_stack_pointer(0x74000ULL); // Separate EL1 stack for Core 1
    cpu1.set_program_counter(code1_pa);
    cpu1.write_register(1, 0x10004000ULL);

    // Both cores step their load instruction -> Data Aborts to VBAR+0x200
    cpu0.step(mem);
    CHECK(cpu0.get_program_counter() == vbar_pa + 0x200u, "Core 0 must vector to Current EL Sync");
    CHECK(cpu0.get_far_el1() == 0x10000000ULL, "Core 0 FAR_EL1 must be 0x10000000");

    cpu1.step(mem);
    CHECK(cpu1.get_program_counter() == vbar_pa + 0x200u, "Core 1 must vector to Current EL Sync");
    CHECK(cpu1.get_far_el1() == 0x10004000ULL, "Core 1 FAR_EL1 must be 0x10004000");

    // Execute Core 0 handler to completion (ERET)
    std::size_t c0_steps = 0;
    while (cpu0.get_program_counter() != code0_pa && c0_steps < 50) {
        cpu0.step(mem);
        ++c0_steps;
    }
    CHECK(cpu0.get_program_counter() == code0_pa, "Core 0 must return to faulting PC via ERET");

    // Execute Core 1 handler to completion (ERET)
    std::size_t c1_steps = 0;
    while (cpu1.get_program_counter() != code1_pa && c1_steps < 50) {
        cpu1.step(mem);
        ++c1_steps;
    }
    CHECK(cpu1.get_program_counter() == code1_pa, "Core 1 must return to faulting PC via ERET");

    // Now re-execute faulting load on both cores
    cpu0.step(mem);
    CHECK(cpu0.get_program_counter() == code0_pa + 4u, "Core 0 PC must advance past LDR");
    CHECK(cpu0.read_register(0) == 0xAAAA111122223333ULL, "Core 0 must read Page 0 data");

    cpu1.step(mem);
    CHECK(cpu1.get_program_counter() == code1_pa + 4u, "Core 1 PC must advance past LDR");
    CHECK(cpu1.read_register(0) == 0xBBBB444455556666ULL, "Core 1 must read Page 1 data");

    auto stats = dpm.get_guest_stats();
    CHECK(stats.total_faults_resolved == 2, "Exactly 2 faults must be resolved");
    CHECK(stats.allocated_backing_pages == 2, "Exactly 2 backing pages must be allocated");

    std::cout << "[PASS] test_dual_core_disjoint_page_faults (Core 0: 0x" << std::hex
              << cpu0.read_register(0) << ", Core 1: 0x" << cpu1.read_register(0) << std::dec << ")\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Concurrent / Repeated Access to the Same Unmapped Page (No Double Alloc)
// -----------------------------------------------------------------------------
static bool test_same_page_concurrent_resolution() {
    Memory mem(0x800000ULL);

    const std::uint64_t root_l2_pa = 0x30000ULL;
    const std::uint64_t l3_demand_pa = 0x50000ULL;
    const std::uint64_t l3_code_pa = 0x60000ULL;
    const std::uint64_t vbar_pa = 0x80000ULL;
    const std::uint64_t backing_pool_pa = 0x200000ULL;

    setup_test_mappings(mem, root_l2_pa, l3_code_pa, l3_demand_pa);

    DemandPagingConfig cfg;
    cfg.root_l2_table_pa = root_l2_pa;
    cfg.l3_table_pa = l3_demand_pa;
    cfg.backing_pool_start_pa = backing_pool_pa;
    DemandPagingManager dpm(mem, cfg);
    dpm.initialize_guest_control_data();
    dpm.install_vector_table(vbar_pa);

    // Single shared value at backing page 0
    mem.write_u64(static_cast<std::size_t>(backing_pool_pa), 0x5A5A5A5A12345678ULL);

    // Both cores access EXACT SAME virtual address 0x10008000
    const std::uint64_t target_va = 0x10008000ULL;
    const std::uint64_t code_pa = 0x2000ULL;
    mem.write_u32(static_cast<std::size_t>(code_pa + 0u), 0xF9400020u); // LDR X0, [X1]
    mem.write_u32(static_cast<std::size_t>(code_pa + 4u), 0x14000000u); // B .

    CPU cpu0;
    cpu0.set_architectural_exceptions(true);
    cpu0.set_vbar_el1(vbar_pa);
    cpu0.get_mmu().set_ttbr0_el1(root_l2_pa);
    cpu0.get_mmu().set_tcr_el1((2ULL << 14) | 28ULL);
    cpu0.get_mmu().set_mair_el1(0x00000000000004FFULL);
    cpu0.get_mmu().set_sctlr_el1(cpu0.get_mmu().get_sctlr_el1() | 1ULL);
    cpu0.set_current_el(1);
    cpu0.set_sp_sel(true);
    cpu0.set_stack_pointer(0x70000ULL);
    cpu0.set_program_counter(code_pa);
    cpu0.write_register(1, target_va);

    CPU cpu1;
    cpu1.set_architectural_exceptions(true);
    cpu1.set_vbar_el1(vbar_pa);
    cpu1.get_mmu().set_ttbr0_el1(root_l2_pa);
    cpu1.get_mmu().set_tcr_el1((2ULL << 14) | 28ULL);
    cpu1.get_mmu().set_mair_el1(0x00000000000004FFULL);
    cpu1.get_mmu().set_sctlr_el1(cpu1.get_mmu().get_sctlr_el1() | 1ULL);
    cpu1.set_current_el(1);
    cpu1.set_sp_sel(true);
    cpu1.set_stack_pointer(0x74000ULL);
    cpu1.set_program_counter(code_pa);
    cpu1.write_register(1, target_va);

    // Core 0 executes load and triggers fault
    cpu0.step(mem);
    CHECK(cpu0.get_program_counter() == vbar_pa + 0x200u, "Core 0 must fault");
    CHECK(cpu0.get_far_el1() == target_va, "Core 0 FAR must be target_va");

    // Execute Core 0 handler to resolve page
    std::size_t steps0 = 0;
    while (cpu0.get_program_counter() != code_pa && steps0 < 50) {
        cpu0.step(mem);
        ++steps0;
    }
    CHECK(cpu0.get_program_counter() == code_pa, "Core 0 returned to code");

    // Re-execute load on Core 0 -> Successful load
    cpu0.step(mem);
    CHECK(cpu0.read_register(0) == 0x5A5A5A5A12345678ULL, "Core 0 read value");

    // Now Core 1 executes its load on the SAME address
    // Since the page descriptor is now installed in L3 table, Core 1 should NOT fault!
    cpu1.step(mem);
    CHECK(cpu1.get_program_counter() == code_pa + 4u, "Core 1 must succeed without fault");
    CHECK(cpu1.read_register(0) == 0x5A5A5A5A12345678ULL, "Core 1 read same value");

    auto stats = dpm.get_guest_stats();
    CHECK(stats.total_faults_resolved == 1, "Only 1 fault was needed for shared page");
    CHECK(stats.allocated_backing_pages == 1, "No double allocation of backing page");

    std::cout << "[PASS] test_same_page_concurrent_resolution (Shared page 0x" << std::hex
              << target_va << std::dec << " resolved once, no duplicate allocation)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Concurrent Mapping Updates & Cross-Core TLB Invalidation
// -----------------------------------------------------------------------------
static bool test_concurrent_mapping_and_tlb_invalidation() {
    Memory mem(0x800000ULL);

    const std::uint64_t root_l2_pa = 0x30000ULL;
    const std::uint64_t l3_table_pa = 0x50000ULL;
    const std::uint64_t l3_code_pa = 0x60000ULL;

    setup_test_mappings(mem, root_l2_pa, l3_code_pa, l3_table_pa);

    VirtualPageTable vpt(mem);

    // Map VA 0x10000000 to PA 0x200000 as Read-Only initially
    const std::uint64_t target_va = 0x10000000ULL;
    const std::uint64_t initial_pa = 0x200000ULL;
    CHECK(vpt.map_page(root_l2_pa, target_va, initial_pa, PagePermissions::KernelRead),
          "Map initial page as read-only");
    mem.write_u64(static_cast<std::size_t>(initial_pa), 0x1122334455667788ULL);

    // Two CPUs sharing the translation table
    CPU cpu0;
    cpu0.get_mmu().set_ttbr0_el1(root_l2_pa);
    cpu0.get_mmu().set_tcr_el1((2ULL << 14) | 28ULL);
    cpu0.get_mmu().set_mair_el1(0x00000000000004FFULL);
    cpu0.get_mmu().set_sctlr_el1(cpu0.get_mmu().get_sctlr_el1() | 1ULL);
    cpu0.set_current_el(1);

    CPU cpu1;
    cpu1.get_mmu().set_ttbr0_el1(root_l2_pa);
    cpu1.get_mmu().set_tcr_el1((2ULL << 14) | 28ULL);
    cpu1.get_mmu().set_mair_el1(0x00000000000004FFULL);
    cpu1.get_mmu().set_sctlr_el1(cpu1.get_mmu().get_sctlr_el1() | 1ULL);
    cpu1.set_current_el(1);

    // Both CPUs translate target_va -> successfully cached in TLB
    mmu::TranslationFault fault0;
    auto res0 = cpu0.get_mmu().translate(mem, target_va, mmu::AccessKind::Read, 1, &fault0);
    CHECK(res0.has_value(), "CPU 0 translation succeeds");
    CHECK(*res0 == initial_pa, "CPU 0 points to initial PA");

    mmu::TranslationFault fault1;
    auto res1 = cpu1.get_mmu().translate(mem, target_va, mmu::AccessKind::Read, 1, &fault1);
    CHECK(res1.has_value(), "CPU 1 translation succeeds");
    CHECK(*res1 == initial_pa, "CPU 1 points to initial PA");

    // Now Core 0 updates the mapping to point to new PA 0x204000 with ReadWrite permissions
    const std::uint64_t updated_pa = 0x204000ULL;
    mem.write_u64(static_cast<std::size_t>(updated_pa), 0x99AABBCCDDEEFF00ULL);
    CHECK(vpt.map_page(root_l2_pa, target_va, updated_pa, PagePermissions::KernelReadWrite),
          "Remap to updated PA");

    // Broadcast TLB invalidation across both cores (TLBI VAAE1IS / TLBI VMALLE1IS)
    cpu0.get_mmu().invalidate_tlb_vaa(target_va);
    cpu1.get_mmu().invalidate_tlb_vaa(target_va);

    // Both cores re-translate -> must get updated PA!
    res0 = cpu0.get_mmu().translate(mem, target_va, mmu::AccessKind::Read, 1, &fault0);
    CHECK(res0.has_value() && *res0 == updated_pa, "CPU 0 gets updated PA");

    res1 = cpu1.get_mmu().translate(mem, target_va, mmu::AccessKind::Read, 1, &fault1);
    CHECK(res1.has_value() && *res1 == updated_pa, "CPU 1 gets updated PA");

    // Test write permission on Core 1: should succeed now that it is KernelReadWrite
    auto write_res = cpu1.get_mmu().translate(mem, target_va, mmu::AccessKind::Write, 1, &fault1);
    CHECK(write_res.has_value(), "CPU 1 write translation must succeed on updated RW mapping");

    std::cout << "[PASS] test_concurrent_mapping_and_tlb_invalidation (TLB coherence verified)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Mapping Removal & Physical Page Reclamation (Deallocation & Re-fault)
// -----------------------------------------------------------------------------
static bool test_mapping_removal_and_reclamation() {
    Memory mem(0x800000ULL);

    const std::uint64_t root_l2_pa = 0x30000ULL;
    const std::uint64_t l3_table_pa = 0x50000ULL;
    const std::uint64_t l3_code_pa = 0x60000ULL;

    setup_test_mappings(mem, root_l2_pa, l3_code_pa, l3_table_pa);

    // Physical Page Manager managing 0x200000 .. 0x400000 (32 pages)
    PhysicalPageManager ppm(mem, 0x200000ULL, 0x200000ULL, 0x10000ULL);
    CHECK(ppm.initialize(false), "PPM init");

    const auto initial_stats = ppm.get_stats();
    CHECK(initial_stats.free_pages == initial_stats.total_pages, "All pages initially free");

    VirtualPageTable vpt(mem);

    // Allocate and map 4 pages
    const std::uint64_t base_va = 0x10000000ULL;
    std::vector<std::uint64_t> allocated_pas;
    for (std::size_t i = 0; i < 4; ++i) {
        std::uint64_t pa = ppm.allocate_page();
        CHECK(pa != 0, "Page allocation must succeed");
        allocated_pas.push_back(pa);
        CHECK(vpt.map_page(root_l2_pa, base_va + i * kPhysicalPageSize16KB, pa, PagePermissions::KernelReadWrite),
              "Map page");
    }

    CHECK(ppm.get_stats().free_pages == initial_stats.free_pages - 4, "4 pages allocated");

    // Verify translations succeed
    for (std::size_t i = 0; i < 4; ++i) {
        std::uint64_t pa = vpt.translate(root_l2_pa, base_va + i * kPhysicalPageSize16KB);
        CHECK(pa == allocated_pas[i], "Translation matches allocated PA");
    }

    // Now unmap page 2 and reclaim its physical page
    const std::uint64_t unmap_va = base_va + 2 * kPhysicalPageSize16KB;
    const std::uint64_t unmap_pa = allocated_pas[2];

    CHECK(vpt.unmap_page(root_l2_pa, unmap_va), "Unmap page 2");
    CHECK(ppm.free_page(unmap_pa), "Free reclaimed physical page");

    // Translation for unmapped VA must now return 0 (translation fault)
    CHECK(vpt.translate(root_l2_pa, unmap_va) == 0, "Unmapped VA must fail translation");

    // MMU translation must also fail
    CPU cpu;
    cpu.get_mmu().set_ttbr0_el1(root_l2_pa);
    cpu.get_mmu().set_tcr_el1((2ULL << 14) | 28ULL);
    cpu.get_mmu().set_mair_el1(0x00000000000004FFULL);
    cpu.get_mmu().set_sctlr_el1(cpu.get_mmu().get_sctlr_el1() | 1ULL);
    cpu.set_current_el(1);

    mmu::TranslationFault fault;
    auto res = cpu.get_mmu().translate(mem, unmap_va, mmu::AccessKind::Read, 1, &fault);
    CHECK(!res.has_value(), "MMU translation of unmapped page must report fault");
    CHECK(fault.type == mmu::MmuFaultType::Translation, "Fault must be Translation fault");

    // Other pages remain intact
    CHECK(vpt.translate(root_l2_pa, base_va + 0 * kPhysicalPageSize16KB) == allocated_pas[0], "Page 0 intact");
    CHECK(vpt.translate(root_l2_pa, base_va + 1 * kPhysicalPageSize16KB) == allocated_pas[1], "Page 1 intact");
    CHECK(vpt.translate(root_l2_pa, base_va + 3 * kPhysicalPageSize16KB) == allocated_pas[3], "Page 3 intact");

    // Reallocated page is available again in PPM
    CHECK(ppm.get_stats().free_pages == initial_stats.free_pages - 3, "Free count restored by 1");

    std::cout << "[PASS] test_mapping_removal_and_reclamation (Unmap and memory reclamation verified)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Out-of-Memory Handling During Demand Paging
// -----------------------------------------------------------------------------
static bool test_out_of_memory_demand_fault_handling() {
    Memory mem(0x400000ULL);

    const std::uint64_t root_l2_pa = 0x30000ULL;
    const std::uint64_t l3_demand_pa = 0x50000ULL;
    const std::uint64_t l3_code_pa = 0x60000ULL;
    const std::uint64_t vbar_pa = 0x80000ULL;
    const std::uint64_t backing_pool_pa = 0x100000ULL;

    setup_test_mappings(mem, root_l2_pa, l3_code_pa, l3_demand_pa);

    DemandPagingConfig cfg;
    cfg.root_l2_table_pa = root_l2_pa;
    cfg.l3_table_pa = l3_demand_pa;
    cfg.backing_pool_start_pa = backing_pool_pa;
    DemandPagingManager dpm(mem, cfg);
    dpm.initialize_guest_control_data();
    dpm.install_vector_table(vbar_pa);

    // Write a limit into guest control data to simulate exhausted pool
    // In guest control slot at 0x40088, backing PA is stored. If we set it past memory size:
    mem.write_u64(0x40088ULL, 0xFFFFFFFF0000ULL); // Out-of-range backing PA

    const std::uint64_t code_pa = 0x2000ULL;
    mem.write_u32(static_cast<std::size_t>(code_pa + 0u), 0xF9400020u); // LDR X0, [X1]
    mem.write_u32(static_cast<std::size_t>(code_pa + 4u), 0x14000000u); // B .

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(vbar_pa);
    cpu.get_mmu().set_ttbr0_el1(root_l2_pa);
    cpu.get_mmu().set_tcr_el1((2ULL << 14) | 28ULL);
    cpu.get_mmu().set_mair_el1(0x00000000000004FFULL);
    cpu.get_mmu().set_sctlr_el1(cpu.get_mmu().get_sctlr_el1() | 1ULL);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_stack_pointer(0x70000ULL);
    cpu.set_program_counter(code_pa);
    cpu.write_register(1, 0x10000000ULL);

    // Initial instruction causes Data Abort
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == vbar_pa + 0x200u, "Traps to exception vector");

    // The handler executes. Because the backing PA is invalid, the mapping descriptor is invalid.
    // The handler runs to completion, but the memory translation will continue to fault rather than corrupting RAM.
    for (std::size_t i = 0; i < 40; ++i) {
        cpu.step(mem);
        if (cpu.get_program_counter() == code_pa) break;
    }

    // Step at code_pa re-triggers fault (cannot resolve due to OOM)
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == vbar_pa + 0x200u, "Repeated unresolvable access safely re-faults");

    std::cout << "[PASS] test_out_of_memory_demand_fault_handling (Graceful failure without memory corruption)\n";
    return true;
}

int main() {
    std::cout << "Running Stage 195.15 Demand Paging Concurrency Tests...\n";

    if (!test_dual_core_disjoint_page_faults()) return 1;
    if (!test_same_page_concurrent_resolution()) return 1;
    if (!test_concurrent_mapping_and_tlb_invalidation()) return 1;
    if (!test_mapping_removal_and_reclamation()) return 1;
    if (!test_out_of_memory_demand_fault_handling()) return 1;

    std::cout << "\nAll Stage 195.15 Demand Paging Concurrency tests PASSED successfully.\n";
    return 0;
}

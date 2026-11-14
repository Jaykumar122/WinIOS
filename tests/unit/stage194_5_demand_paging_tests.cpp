// ============================================================================
// Stage 194.5: Demand Paging and Page-Fault Resolution Tests
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

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::memory;

static void setup_standard_mappings(
    Memory& mem,
    std::uint64_t root_l2_pa,
    std::uint64_t l3_code_pa,
    std::uint64_t l3_demand_pa,
    bool user_mode = false) {
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

    // Map 48 pages (0x0 .. 0xC0000):
    // Pages 0..3 (0x00000..0x10000): Code -> Executable
    // Pages 4..31 (0x10000..0x80000): Data, Tables, Stack -> ReadWrite
    // Pages 32..35 (0x80000..0x90000): Vectors -> KernelExecute
    // Pages 36..47 (0x90000..0xC0000): Console & Buffers -> ReadWrite
    for (std::uint64_t p = 0; p < 48; ++p) {
        const std::uint64_t addr = p * kPhysicalPageSize16KB;
        PagePermissions perms = PagePermissions::KernelReadWrite;
        if (p < 4) {
            perms = user_mode ? PagePermissions::UserExecute : PagePermissions::KernelExecute;
        } else if (p >= 32 && p < 36) {
            perms = PagePermissions::KernelExecute; // Vectors always run at EL1
        } else {
            perms = user_mode ? PagePermissions::UserReadWrite : PagePermissions::KernelReadWrite;
        }
        mem.write_u64(static_cast<std::size_t>(l3_code_pa + p * 8u),
                      VirtualPageTable::make_page_descriptor(addr, perms));
    }
}

// -----------------------------------------------------------------------------
// Test 1: Vector Table and Control Data Slot Initialization
// -----------------------------------------------------------------------------
static bool test_vector_table_and_control_initialization() {
    Memory mem(0x200000ULL);
    DemandPagingConfig cfg;
    cfg.root_l2_table_pa = 0x30000ULL;
    cfg.l3_table_pa = 0x50000ULL;
    cfg.backing_pool_start_pa = 0x100000ULL;
    cfg.config_data_pa = 0x40080ULL;
    cfg.fault_log_pa = 0x40010ULL;

    DemandPagingManager dpm(mem, cfg);
    CHECK(dpm.initialize_guest_control_data(), "Control data initialization must succeed");

    const std::uint64_t vbar = 0x80000ULL;
    CHECK(dpm.install_vector_table(vbar), "Vector table installation must succeed");

    CHECK(mem.read_u64(0x40080) == cfg.l3_table_pa, "L3 table address in RAM must match");
    CHECK(mem.read_u64(0x40088) == cfg.backing_pool_start_pa, "Backing pool PA in RAM must match");
    CHECK(mem.read_u64(0x40010) == 0, "Initial fault address must be 0");
    CHECK(mem.read_u64(0x40018) == 0, "Initial fault count must be 0");

    const std::uint32_t first_spx_insn = mem.read_u32(static_cast<std::size_t>(vbar + 0x200u));
    CHECK(first_spx_insn == 0xA9BF07E0u, "First instruction at VBAR+0x200 must be STP X0, X1, [SP, #-16]!");

    const std::uint32_t first_lower_insn = mem.read_u32(static_cast<std::size_t>(vbar + 0x400u));
    CHECK(first_lower_insn == 0xA9BF07E0u, "First instruction at VBAR+0x400 must be STP X0, X1, [SP, #-16]!");

    CHECK(mem.read_u32(static_cast<std::size_t>(vbar + 0x000u)) == 0x14000000u, "Unhandled vector must be b .");

    std::cout << "[PASS] test_vector_table_and_control_initialization\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Single Demand Page Fault Resolution and Transparent Re-execution
// -----------------------------------------------------------------------------
static bool test_single_demand_page_fault_resolution() {
    Memory mem(0x400000ULL);

    const std::uint64_t root_l2_pa = 0x30000ULL;
    const std::uint64_t l3_demand_pa = 0x50000ULL;
    const std::uint64_t l3_code_pa = 0x60000ULL;
    const std::uint64_t vbar_pa = 0x80000ULL;
    const std::uint64_t backing_pool_pa = 0x100000ULL;

    setup_standard_mappings(mem, root_l2_pa, l3_code_pa, l3_demand_pa, false);

    DemandPagingConfig cfg;
    cfg.root_l2_table_pa = root_l2_pa;
    cfg.l3_table_pa = l3_demand_pa;
    cfg.backing_pool_start_pa = backing_pool_pa;
    DemandPagingManager dpm(mem, cfg);
    dpm.initialize_guest_control_data();
    dpm.install_vector_table(vbar_pa);

    const std::uint64_t expected_data = 0x55AA112233445566ULL;
    mem.write_u64(static_cast<std::size_t>(backing_pool_pa), expected_data);

    // Guest code at 0x2000:
    // 0x2000: MOVZ X1, #0x1000, LSL #16
    // 0x2004: LDR X0, [X1]
    // 0x2008: B .
    const std::uint64_t code_pa = 0x2000ULL;
    mem.write_u32(static_cast<std::size_t>(code_pa + 0u), 0xD2A20001u); // MOVZ X1, #0x1000, LSL #16
    mem.write_u32(static_cast<std::size_t>(code_pa + 4u), 0xF9400020u); // LDR X0, [X1]
    mem.write_u32(static_cast<std::size_t>(code_pa + 8u), 0x14000000u); // B .

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(vbar_pa);

    mmu::MMU& mmu = cpu.get_mmu();
    mmu.set_ttbr0_el1(root_l2_pa);
    mmu.set_tcr_el1((2ULL << 14) | 28ULL); // 16KB granule, 36-bit VA
    mmu.set_mair_el1(0x00000000000004FFULL);
    mmu.set_sctlr_el1(mmu.get_sctlr_el1() | 1ULL); // MMU On

    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(code_pa);
    cpu.set_stack_pointer(0x70000ULL);

    // Step 1: MOVZ X1, #0x1000, LSL #16
    cpu.step(mem);
    CHECK(cpu.read_register(1) == 0x10000000ULL, "X1 must be 0x10000000");

    // Step 2: LDR X0, [X1] -> Generates Data Abort! Vectors to VBAR + 0x200
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == vbar_pa + 0x200u, "Must vector to Current EL Sync");
    CHECK(cpu.get_far_el1() == 0x10000000ULL, "FAR_EL1 must hold faulting address 0x10000000");
    CHECK((cpu.get_esr_el1() >> 26u) == 0x25u, "ESR_EL1 must report Data Abort (EC=0x25)");

    // Execute the guest EL1 demand page-fault resolution handler until ERET returns
    std::size_t handler_steps = 0;
    while (cpu.get_program_counter() != code_pa + 4u && handler_steps < 50) {
        cpu.step(mem);
        ++handler_steps;
    }
    CHECK(cpu.get_program_counter() == code_pa + 4u, "ERET must return to the faulting instruction");

    auto stats = dpm.get_guest_stats();
    CHECK(stats.total_faults_resolved == 1, "Guest fault counter must be 1");
    CHECK(stats.last_fault_address == 0x10000000ULL, "Last fault address in RAM must be 0x10000000");

    // Step 3: Faulting instruction LDR X0, [X1] re-executes successfully!
    cpu.step(mem);
    CHECK(cpu.read_register(0) == expected_data, "X0 must hold value read from backing page");
    CHECK(cpu.get_program_counter() == code_pa + 8u, "PC must advance past LDR instruction");

    std::cout << "[PASS] test_single_demand_page_fault_resolution (Read 0x"
              << std::hex << cpu.read_register(0) << std::dec
              << " after transparent guest resolution)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Multi-Page Sequential Demand Page Fault Resolution
// -----------------------------------------------------------------------------
static bool test_multi_page_sequential_fault_resolution() {
    Memory mem(0x400000ULL);

    const std::uint64_t root_l2_pa = 0x30000ULL;
    const std::uint64_t l3_demand_pa = 0x50000ULL;
    const std::uint64_t l3_code_pa = 0x60000ULL;
    const std::uint64_t vbar_pa = 0x80000ULL;
    const std::uint64_t backing_pool_pa = 0x100000ULL;

    setup_standard_mappings(mem, root_l2_pa, l3_code_pa, l3_demand_pa, false);

    DemandPagingConfig cfg;
    cfg.root_l2_table_pa = root_l2_pa;
    cfg.l3_table_pa = l3_demand_pa;
    cfg.backing_pool_start_pa = backing_pool_pa;
    DemandPagingManager dpm(mem, cfg);
    dpm.initialize_guest_control_data();
    dpm.install_vector_table(vbar_pa);

    mem.write_u64(static_cast<std::size_t>(backing_pool_pa + 0 * kPhysicalPageSize16KB), 0x1111111111111111ULL);
    mem.write_u64(static_cast<std::size_t>(backing_pool_pa + 1 * kPhysicalPageSize16KB), 0x2222222222222222ULL);
    mem.write_u64(static_cast<std::size_t>(backing_pool_pa + 2 * kPhysicalPageSize16KB), 0x3333333333333333ULL);

    const std::uint64_t code_pa = 0x2000ULL;
    mem.write_u32(static_cast<std::size_t>(code_pa + 0x00), 0xD2A20001u); // MOVZ X1, #0x1000, LSL #16
    mem.write_u32(static_cast<std::size_t>(code_pa + 0x04), 0xF940002Au); // LDR X10, [X1]
    mem.write_u32(static_cast<std::size_t>(code_pa + 0x08), 0x91401021u); // ADD X1, X1, #0x4000
    mem.write_u32(static_cast<std::size_t>(code_pa + 0x0C), 0xF940002Bu); // LDR X11, [X1]
    mem.write_u32(static_cast<std::size_t>(code_pa + 0x10), 0x91401021u); // ADD X1, X1, #0x4000
    mem.write_u32(static_cast<std::size_t>(code_pa + 0x14), 0xF940002Cu); // LDR X12, [X1]
    mem.write_u32(static_cast<std::size_t>(code_pa + 0x18), 0x14000000u); // B .

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(vbar_pa);

    mmu::MMU& mmu = cpu.get_mmu();
    mmu.set_ttbr0_el1(root_l2_pa);
    mmu.set_tcr_el1((2ULL << 14) | 28ULL);
    mmu.set_mair_el1(0x00000000000004FFULL);
    mmu.set_sctlr_el1(mmu.get_sctlr_el1() | 1ULL);

    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(code_pa);
    cpu.set_stack_pointer(0x70000ULL);

    std::size_t total_steps = 0;
    while (cpu.get_program_counter() != code_pa + 0x18 && total_steps < 200) {
        cpu.step(mem);
        ++total_steps;
    }

    CHECK(cpu.get_program_counter() == code_pa + 0x18, "Must reach completion loop");
    CHECK(cpu.read_register(10) == 0x1111111111111111ULL, "X10 must hold data from page 0");
    CHECK(cpu.read_register(11) == 0x2222222222222222ULL, "X11 must hold data from page 1");
    CHECK(cpu.read_register(12) == 0x3333333333333333ULL, "X12 must hold data from page 2");

    auto stats = dpm.get_guest_stats();
    CHECK(stats.total_faults_resolved == 3, "Exactly 3 faults must be resolved");
    CHECK(stats.allocated_backing_pages == 3, "Exactly 3 backing pages must be allocated");

    std::cout << "[PASS] test_multi_page_sequential_fault_resolution (Resolved 3 page faults in "
              << total_steps << " steps)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Write-Triggered Demand Page Fault
// -----------------------------------------------------------------------------
static bool test_write_triggered_demand_page_fault() {
    Memory mem(0x400000ULL);

    const std::uint64_t root_l2_pa = 0x30000ULL;
    const std::uint64_t l3_demand_pa = 0x50000ULL;
    const std::uint64_t l3_code_pa = 0x60000ULL;
    const std::uint64_t vbar_pa = 0x80000ULL;
    const std::uint64_t backing_pool_pa = 0x100000ULL;

    setup_standard_mappings(mem, root_l2_pa, l3_code_pa, l3_demand_pa, false);

    DemandPagingConfig cfg;
    cfg.root_l2_table_pa = root_l2_pa;
    cfg.l3_table_pa = l3_demand_pa;
    cfg.backing_pool_start_pa = backing_pool_pa;
    DemandPagingManager dpm(mem, cfg);
    dpm.initialize_guest_control_data();
    dpm.install_vector_table(vbar_pa);

    // Guest code writes to unmapped address 0x1000C000:
    // 0x2000: MOVZ X1, #0x1000, LSL #16
    // 0x2004: MOVK X1, #0xC000, LSL #0
    // 0x2008: MOVZ X2, #0xCAFE, LSL #16
    // 0x200C: STR X2, [X1]
    // 0x2010: B .
    const std::uint64_t code_pa = 0x2000ULL;
    mem.write_u32(static_cast<std::size_t>(code_pa + 0x00), 0xD2A20001u); // MOVZ X1, #0x1000, LSL #16
    mem.write_u32(static_cast<std::size_t>(code_pa + 0x04), 0xF2980001u); // MOVK X1, #0xC000, LSL #0
    mem.write_u32(static_cast<std::size_t>(code_pa + 0x08), 0xD2B95FC2u); // MOVZ X2, #0xCAFE, LSL #16
    mem.write_u32(static_cast<std::size_t>(code_pa + 0x0C), 0xF9000022u); // STR X2, [X1]
    mem.write_u32(static_cast<std::size_t>(code_pa + 0x10), 0x14000000u); // B .

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(vbar_pa);

    mmu::MMU& mmu = cpu.get_mmu();
    mmu.set_ttbr0_el1(root_l2_pa);
    mmu.set_tcr_el1((2ULL << 14) | 28ULL);
    mmu.set_mair_el1(0x00000000000004FFULL);
    mmu.set_sctlr_el1(mmu.get_sctlr_el1() | 1ULL);

    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(code_pa);
    cpu.set_stack_pointer(0x70000ULL);

    std::size_t steps = 0;
    while (cpu.get_program_counter() != code_pa + 0x10 && steps < 100) {
        cpu.step(mem);
        ++steps;
    }

    CHECK(cpu.get_program_counter() == code_pa + 0x10, "Execution must complete store");
    const std::uint64_t written_val = mem.read_u64(static_cast<std::size_t>(backing_pool_pa));
    CHECK(written_val == 0xCAFE0000ULL, "Physical memory must contain written value");

    std::cout << "[PASS] test_write_triggered_demand_page_fault\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Lower EL (EL0 User Mode) Demand Fault Resolution
// -----------------------------------------------------------------------------
static bool test_lower_el_demand_page_fault() {
    Memory mem(0x400000ULL);

    const std::uint64_t root_l2_pa = 0x30000ULL;
    const std::uint64_t l3_demand_pa = 0x50000ULL;
    const std::uint64_t l3_code_pa = 0x60000ULL;
    const std::uint64_t vbar_pa = 0x80000ULL;
    const std::uint64_t backing_pool_pa = 0x100000ULL;

    setup_standard_mappings(mem, root_l2_pa, l3_code_pa, l3_demand_pa, true);

    DemandPagingConfig cfg;
    cfg.root_l2_table_pa = root_l2_pa;
    cfg.l3_table_pa = l3_demand_pa;
    cfg.backing_pool_start_pa = backing_pool_pa;
    DemandPagingManager dpm(mem, cfg);
    dpm.initialize_guest_control_data();
    dpm.install_vector_table(vbar_pa);

    mem.write_u64(static_cast<std::size_t>(backing_pool_pa), 0x9988776655443322ULL);

    // User code at 0x2000 running in EL0:
    // 0x2000: MOVZ X1, #0x1000, LSL #16
    // 0x2004: LDR X0, [X1]
    // 0x2008: B .
    const std::uint64_t code_pa = 0x2000ULL;
    mem.write_u32(static_cast<std::size_t>(code_pa + 0u), 0xD2A20001u);
    mem.write_u32(static_cast<std::size_t>(code_pa + 4u), 0xF9400020u);
    mem.write_u32(static_cast<std::size_t>(code_pa + 8u), 0x14000000u);

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(vbar_pa);

    mmu::MMU& mmu = cpu.get_mmu();
    mmu.set_ttbr0_el1(root_l2_pa);
    mmu.set_tcr_el1((2ULL << 14) | 28ULL);
    mmu.set_mair_el1(0x00000000000004FFULL);
    mmu.set_sctlr_el1(mmu.get_sctlr_el1() | 1ULL);

    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_program_counter(code_pa);
    cpu.set_sp_el0(0x80000ULL);
    cpu.set_sp_el1(0x70000ULL);

    std::size_t steps = 0;
    while (cpu.get_program_counter() != code_pa + 8u && steps < 100) {
        cpu.step(mem);
        ++steps;
    }

    CHECK(cpu.get_current_el() == 0, "After resolution, CPU must return to EL0 via ERET");
    CHECK(cpu.read_register(0) == 0x9988776655443322ULL, "User mode X0 must hold read data");

    std::cout << "[PASS] test_lower_el_demand_page_fault\n";
    return true;
}

int main() {
    std::cout << "Running Stage 194.5 Demand Paging Tests...\n";

    if (!test_vector_table_and_control_initialization()) return 1;
    if (!test_single_demand_page_fault_resolution()) return 1;
    if (!test_multi_page_sequential_fault_resolution()) return 1;
    if (!test_write_triggered_demand_page_fault()) return 1;
    if (!test_lower_el_demand_page_fault()) return 1;

    std::cout << "\nAll Stage 194.5 Demand Paging tests PASSED successfully.\n";
    return 0;
}

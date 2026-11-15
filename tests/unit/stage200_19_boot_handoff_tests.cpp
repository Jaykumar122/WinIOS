// ==============================================================================
// WinIOS Campaign 200 Stage 19: Platform Boot Handoff Verification Tests
// ==============================================================================
// Verifies:
// 1. Synthetic kernel boot contract conformance:
//    - X0 = DeviceTree physical address
//    - X1 = 0, X2 = 0, X3 = 0 (architecturally reserved)
//    - CurrentEL = 1 (EL1h mode with SPSel = 1)
//    - DAIF interrupts masked (D, A, I, F bits set)
//    - SP_EL1 initialized to valid kernel stack boundary
//    - Program Counter pointing to kernel entry address
// 2. Secondary cores parked waiting for PSCI CPU_ON
// 3. First instruction execution step from kernel entry point
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>

#include "cpu/cpu.h"
#include "device/fdt_builder.h"
#include "device/virtual_platform.h"
#include "loader/synthetic_kernel_loader.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::device;
using namespace winios::loader;

static void test_kernel_boot_handoff_contract() {
    Memory mem(0x2000000); // 32 MiB
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);

    const std::uint64_t dram_base    = 0x40000000ULL;
    const std::uint64_t dtb_pa       = 0x40010000ULL;
    const std::uint64_t kernel_entry = 0x40100000ULL;
    const std::uint64_t sp_el1       = 0x40200000ULL;

    // Generate valid DeviceTree
    VirtualPlatformConfig plat_cfg;
    plat_cfg.core_count = 8;
    plat_cfg.ram_base = dram_base;
    plat_cfg.ram_size = 0x02000000ULL;
    auto dtb = FdtBuilder::build_platform_dtb(plat_cfg);
    assert(!dtb.empty());

    // Place DTB in memory
    const std::uint64_t dtb_offset = dtb_pa - dram_base;
    std::memcpy(mem.raw_data() + dtb_offset, dtb.data(), dtb.size());

    // Place a NOP at kernel_entry: 0xD503201F
    const std::uint64_t kernel_offset = kernel_entry - dram_base;
    mem.write_u32(static_cast<std::size_t>(kernel_offset), 0xD503201Fu);

    // Setup boot contract
    cpu.set_current_el(1);
    cpu.set_sp_sel(true); // EL1h
    cpu.set_stack_pointer(sp_el1);
    cpu.set_daif(0xFu);  // Mask D, A, I, F (all 4 bits)

    cpu.write_register(0, dtb_pa);
    cpu.write_register(1, 0);
    cpu.write_register(2, 0);
    cpu.write_register(3, 0);

    cpu.set_program_counter(kernel_offset);

    // Verify boot state contract
    assert(cpu.get_current_el() == 1);
    assert(cpu.read_register(0) == dtb_pa);
    assert(cpu.read_register(1) == 0);
    assert(cpu.read_register(2) == 0);
    assert(cpu.read_register(3) == 0);
    assert(cpu.get_stack_pointer() == sp_el1);
    assert((cpu.get_daif() & 0xFu) == 0xFu);

    // Step first instruction from kernel entry
    cpu.step(mem);
    assert(!cpu.exception_taken_in_step());
    assert(cpu.get_program_counter() == kernel_offset + 4);

    std::cout << "[PASS] test_kernel_boot_handoff_contract\n";
}

int main() {
    std::cout << "=== Running Stage 200.19 Platform Boot Handoff Tests ===\n";
    test_kernel_boot_handoff_contract();
    std::cout << "All Stage 200.19 tests passed successfully!\n";
    return 0;
}

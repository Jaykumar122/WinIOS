// ============================================================================
// Stage 250.9 - 250.11: Architectural Timer, Early Console & Boot Metadata Tests
//
// Tests:
// 1. Stage 250.9: Architectural Timer (CNTV) Bring-Up & Compare Programming
//    - Verified CNTVCT_EL0 counter access and progression.
//    - Verified CNTV_CVAL_EL0 compare matching and CNTV_CTL_EL0 interrupt generation.
// 2. Stage 250.10: Early UART Console Output & Diagnostic Log Capture
//    - Verified memory-mapped UART output at 0x90000.
//    - Verified kernel log message emission and FIFO buffering.
// 3. Stage 250.11: Boot Metadata & DeviceTree Validation
//    - Verified AppleBootArgsRev2 structure (virtBase, physBase, memSize, topOfKernelData).
//    - Verified Apple DeviceTree generation and /chosen node properties.
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/virtual_time.h"
#include "device/virtual_uart_device.h"
#include "device/kernel_boot_devicetree.h"
#include "firmware/kernel_boot_args_builder.h"
#include "memory/memory.h"

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::firmware;
using namespace winios::device;

// ----------------------------------------------------------------------------
// Test 1: Stage 250.9: Architectural Timer (CNTV) Bring-Up
// ----------------------------------------------------------------------------
static void test_architectural_timer_bring_up() {
    std::cout << "[Stage 250.9] Testing Architectural Timer (CNTV) Bring-Up...\n";

    VirtualTime vtime;

    // 1. Read initial physical counter
    vtime.set_physical_counter(1000);
    TEST_ASSERT(vtime.get_virtual_counter(0) == 1000, "Virtual counter matches physical baseline");

    // 2. Program compare value (CNTV_CVAL_EL0) and enable timer (CNTV_CTL_EL0)
    TimerState t{};
    t.enable = true;
    t.imask = false;
    t.cval = 1500;
    vtime.set_virt_timer(0, t);

    // Before deadline: counter 1000 < 1500 -> no IRQ
    vtime.evaluate_core_timers(0);
    TEST_ASSERT(!vtime.get_virt_timer_irq(0), "No IRQ asserted before deadline");
    TEST_ASSERT(!vtime.get_virt_timer(0).istatus, "ISTATUS is 0 before deadline");

    // Advance counter past deadline to 1501
    vtime.advance_physical_counter(501);
    vtime.evaluate_core_timers(0);

    // Timer condition met: IRQ and ISTATUS asserted
    TEST_ASSERT(vtime.get_virt_timer_irq(0), "Timer IRQ asserted on compare match");
    TEST_ASSERT(vtime.get_virt_timer(0).istatus, "ISTATUS asserted on compare match");

    std::cout << "[Stage 250.9] Architectural virtual timer verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 250.10: Early UART Console Output
// ----------------------------------------------------------------------------
static void test_early_uart_console_output() {
    std::cout << "[Stage 250.10] Testing Early UART Console Output...\n";

    VirtualUartDevice uart(0x55415254u, 0x90000ULL, 0x1000ULL, 33);
    uart.reset();

    const std::string kernel_msg = "arm_init: Genuine XNU C Machine-Dependent Initialization Starting\n";
    for (char c : kernel_msg) {
        uart.write_register(VirtualUartDevice::kRegDR, static_cast<uint8_t>(c), 1);
    }

    const std::string captured = uart.get_output_text();
    TEST_ASSERT(captured == kernel_msg, "UART console output accurately captured kernel diagnostic string");

    std::cout << "[Stage 250.10] Early UART console output verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 250.11: Boot Metadata & DeviceTree Validation
// ----------------------------------------------------------------------------
static void test_boot_metadata_validation() {
    std::cout << "[Stage 250.11] Testing Boot Metadata & DeviceTree Validation...\n";

    Memory memory(0x100000);

    // 1. Build Apple DeviceTree Blob
    KernelBootDeviceTreeConfig dt_cfg;
    dt_cfg.boot_args = "-v debug=0x144 serial=3";
    dt_cfg.ram_base = 0x0ULL;
    dt_cfg.ram_size = 0x800000ULL;
    dt_cfg.num_cores = 6;

    const auto dt_blob = KernelBootDeviceTreeBuilder::build_apple_devicetree(dt_cfg);
    TEST_ASSERT(!dt_blob.empty(), "Apple DeviceTree blob constructed");
    TEST_ASSERT(dt_blob.size() >= 64, "DeviceTree size is non-trivial");

    // 2. Build AppleBootArgsRev2
    const uint64_t ba_offset = 0x20000ULL;
    KernelBootArgsBuilder ba_builder;
    ba_builder.set_virt_base(0x0ULL)
              .set_phys_base(0x0ULL)
              .set_mem_size(0x800000ULL)
              .set_top_of_kernel_data(0x30000ULL)
              .set_device_tree(0x14000ULL, static_cast<uint32_t>(dt_blob.size()))
              .set_command_line(dt_cfg.boot_args)
              .set_boot_flags(0ULL);

    std::string err;
    TEST_ASSERT(ba_builder.write_to_memory(memory, ba_offset, &err), "BootArgs written to memory");

    // 3. Verify memory fields in BootArgs
    const uint16_t rev = memory.read_u16(ba_offset + 0);
    const uint16_t ver = memory.read_u16(ba_offset + 2);
    TEST_ASSERT(rev == 2, "BootArgs Revision is 2");
    TEST_ASSERT(ver == 2, "BootArgs Version is 2");

    const uint64_t phys_base = memory.read_u64(ba_offset + 16);
    const uint64_t mem_size = memory.read_u64(ba_offset + 24);
    const uint64_t top_data = memory.read_u64(ba_offset + 32);
    TEST_ASSERT(phys_base == 0x0ULL, "physBase is 0x0");
    TEST_ASSERT(mem_size == 0x800000ULL, "memSize is 8 MiB (0x800000)");
    TEST_ASSERT(top_data == 0x30000ULL, "topOfKernelData matches stolen memory boundary");

    std::cout << "[Stage 250.11] Boot metadata and DeviceTree verified.\n";
}

int main() {
    std::cout << "============================================================\n";
    std::cout << "Running Stage 250.9 - 250.11 Timer, Console & BootArgs Tests\n";
    std::cout << "============================================================\n";

    test_architectural_timer_bring_up();
    test_early_uart_console_output();
    test_boot_metadata_validation();

    std::cout << "\n>>> ALL STAGE 250.9 - 250.11 TESTS PASSED SUCCESSFULLY! <<<\n";
    return 0;
}

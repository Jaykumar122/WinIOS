#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "device/virtual_uart_device.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"

using namespace winios;
using namespace winios::cpu;
using namespace winios::device;
using namespace winios::loader;
using namespace winios::runtime;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_early_console_output() {
    std::cout << "[Test 1] Stage 207.16: Early UART Console Diagnostics...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory memory(0x400000);

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image();
    TEST_CHECK(!kernel_image.empty(), "Kernel image loaded");

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x400000ULL;
    config.num_cores = 6;
    config.enable_apple_aic = true;
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_CHECK(boot_res.success, "Boot preparation succeeded");

    // Execute genuine bootstrap sequence
    for (std::size_t step = 0; step < 1000; ++step) {
        if (cpu.is_waiting_for_interrupt()) break;
        cpu.step(memory);
    }
    TEST_CHECK(cpu.is_waiting_for_interrupt(), "Reached WFI");

    // Verify UART byte written at MMIO base
    const std::uint8_t byte = memory.read_u8(0x90000ULL);
    TEST_CHECK(byte == '\n', "UART terminal character is newline");

    // In g_pe_state (offset 0x34 contains uart_present flag: 0x10028 + 0x34 = 0x1005C)
    const std::uint32_t uart_present = memory.read_u32(0x1005CULL);
    std::cout << "  -> Platform Expert UART Present: " << uart_present << "\n";
    TEST_CHECK(uart_present == 1, "Platform Expert registered UART console");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 207.16 Early Console Diagnostics Tests ===\n";
    test_early_console_output();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 207.16: All early console tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 207.16: " << g_failures << " tests failed.\n";
        return 1;
    }
}

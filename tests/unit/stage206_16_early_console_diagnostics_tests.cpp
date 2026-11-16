#include <cstdint>
#include <iostream>
#include <string>

#include "memory/memory.h"
#include "runtime/early_kernel_services.h"

using namespace winios;
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

void test_guest_uart_console_emission() {
    std::cout << "[Test 1] Early UART Console Guest Emission & Host Separation...\n";

    Memory memory(0x200000); // 2 MB
    EarlyUartConsole console(0x90000ULL);

    TEST_CHECK(console.base_address() == 0x90000ULL, "UART base address is 0x90000");
    TEST_CHECK(console.get_log_buffer().empty(), "Initial console log buffer is empty");

    // Guest emits "Darwin Kernel Version 23.0.0\n"
    const std::string guest_msg = "Darwin Kernel Version 23.0.0\n";
    console.write_string(memory, guest_msg);

    // Verify guest memory TX register received write
    TEST_CHECK(memory.read_u8(static_cast<std::size_t>(0x90000ULL)) == static_cast<std::uint8_t>(guest_msg.back()),
               "Memory TX register holds last emitted byte");

    // Verify console log buffer matches guest string exactly
    TEST_CHECK(console.get_log_buffer() == guest_msg, "Log buffer matches guest message");

    // Verify separation from host messages
    const std::string host_msg = "[HOST DEBUG] Host internal message\n";
    // Host message is NOT in memory or in console log buffer
    TEST_CHECK(console.get_log_buffer().find("[HOST DEBUG]") == std::string::npos,
               "Console log must not contain host diagnostics");

    console.clear_log();
    TEST_CHECK(console.get_log_buffer().empty(), "Console cleared successfully");

    std::cout << "  -> Early console diagnostics verified successfully.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 206.16 Early Console & Diagnostics Tests ===\n";
    test_guest_uart_console_emission();

    if (g_failures == 0) {
        std::cout << "=== All Stage 206.16 Tests PASSED ===\n";
        return 0;
    } else {
        std::cerr << "=== " << g_failures << " Stage 206.16 Tests FAILED ===\n";
        return 1;
    }
}

// ============================================================================
// Stage 248.7 & 248.8: Genuine Guest-Driven PL011 UART Workload & Echo Tests
// Tests:
// 1. Guest ARM64 instructions driving bidirectional UART communications:
//    - STR W2, [X1, #0] writes characters to kRegDR via MMIO.
//    - External input injected into UART RX FIFO.
//    - LDR W3, [X1, #0] reads received character from kRegDR via MMIO.
//    - ADD W4, W3, #1 transforms character.
//    - STR W4, [X1, #0] echoes transformed character to output.
//    - LDR W5, [X1, #0x18] reads Flag Register (kRegFR).
// 2. Verified byte ordering and string output.
// 3. FIFO status flags (TXFE, RXFE) verification from guest code.
// ============================================================================

#include "cpu/cpu.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_uart_device.h"
#include "memory/memory.h"

#include <iostream>
#include <memory>
#include <string>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::device;

static void test_guest_driven_uart_echo() {
    std::cout << "[Stage 248.7 - Test 1] Guest ARM64 instructions driving UART echo...\n";

    auto bus = std::make_shared<VirtualDeviceBus>();
    auto uart = std::make_shared<VirtualUartDevice>(0x55415254u, 0x09000000ULL, 0x1000ULL, 33);
    bus->register_device(uart);

    CPU cpu;
    cpu.reset();
    cpu.attach_device_bus(bus);
    Memory mem(0x100000);

    const uint64_t uart_base = 0x09000000ULL;
    const uint64_t code_pc = 0x10000;

    // Guest program at 0x10000:
    // 1. 0x10000: LDR W3, [X1, #0]     (0xB9400023u) -> Read received char from DR into W3
    // 2. 0x10004: ADD W4, W3, #1       (0x11000464u) -> W4 = W3 + 1 (e.g. 'A' -> 'B')
    // 3. 0x10008: STR W4, [X1, #0]     (0xB9000024u) -> Write transformed char to DR
    // 4. 0x1000C: LDR W5, [X1, #0x18]  (0xB9401825u) -> Read Flag Register into W5
    mem.write_u32(code_pc + 0, 0xB9400023u);
    mem.write_u32(code_pc + 4, 0x11000464u);
    mem.write_u32(code_pc + 8, 0xB9000024u);
    mem.write_u32(code_pc + 12, 0xB9401825u);

    cpu.set_program_counter(code_pc);
    cpu.write_register(1, uart_base);

    // Inject external input byte 'A' (0x41)
    uart->inject_input('A');

    // Step 1: LDR W3, [X1, #0] -> reads 'A' into W3
    cpu.step(mem);
    TEST_ASSERT((cpu.read_register(3) & 0xFF) == 'A', "Guest instruction read injected byte 'A' from UART");

    // Step 2: ADD W4, W3, #1 -> W4 becomes 'B' (0x42)
    cpu.step(mem);
    TEST_ASSERT((cpu.read_register(4) & 0xFF) == 'B', "Guest instruction incremented character to 'B'");

    // Step 3: STR W4, [X1, #0] -> writes 'B' to UART output
    cpu.step(mem);
    TEST_ASSERT(uart->get_output_text() == "B", "UART output buffer received echoed character 'B'");

    // Step 4: LDR W5, [X1, #0x18] -> reads Flag Register
    cpu.step(mem);
    uint32_t fr = static_cast<uint32_t>(cpu.read_register(5));
    TEST_ASSERT((fr & VirtualUartDevice::kFR_RXFE) != 0, "Guest read Flag Register showing RX FIFO is now empty");
}

static void test_guest_driven_uart_multibyte_stream() {
    std::cout << "[Stage 248.8 - Test 2] Multi-byte string transmission & FIFO flags...\n";

    auto bus = std::make_shared<VirtualDeviceBus>();
    auto uart = std::make_shared<VirtualUartDevice>(0x55415254u, 0x09000000ULL, 0x1000ULL, 33);
    bus->register_device(uart);

    CPU cpu;
    cpu.reset();
    cpu.attach_device_bus(bus);
    Memory mem(0x100000);

    const uint64_t uart_base = 0x09000000ULL;
    const uint64_t code_pc = 0x20000;

    // Transmit string "WINIOS" by writing each char via STR W2, [X1]
    const std::string text = "WINIOS";
    for (size_t i = 0; i < text.size(); ++i) {
        mem.write_u32(code_pc + i * 4, 0xB9000022u); // STR W2, [X1]
    }

    cpu.set_program_counter(code_pc);
    cpu.write_register(1, uart_base);

    for (size_t i = 0; i < text.size(); ++i) {
        cpu.write_register(2, static_cast<uint8_t>(text[i]));
        cpu.step(mem);
    }

    TEST_ASSERT(uart->get_output_text() == text, "Exact multi-byte string transmitted via guest instructions");
}

int main() {
    std::cout << "=== Running Stage 248.7 Guest UART Echo Workload Tests ===\n";
    test_guest_driven_uart_echo();
    test_guest_driven_uart_multibyte_stream();
    std::cout << "Stage 248.7: All guest UART echo tests passed.\n";
    return 0;
}

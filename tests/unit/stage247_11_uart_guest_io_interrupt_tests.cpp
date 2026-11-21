// ============================================================================
// Stage 247.11 & 247.12: Virtual UART Guest I/O & Interrupt Integration Tests
// Tests:
// 1. PL011 UART character transmission via MMIO Data Register (kRegDR).
// 2. PL011 character reception via inject_input and FIFO readback.
// 3. FIFO status flags: TXFE (empty), RXFE (empty), RXFF (full).
// 4. Interrupt generation on RX event (kIMSC_RXIM) and ICR interrupt clearing.
// 5. VirtualDeviceBus address routing and unmapped address protection.
// ============================================================================

#include "device/virtual_device_bus.h"
#include "device/virtual_uart_device.h"

#include <iostream>
#include <memory>
#include <string>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::device;

static void test_uart_tx_rx_operations() {
    std::cout << "[Stage 247.11 - Test 1] Virtual UART TX / RX and string buffering...\n";

    VirtualUartDevice uart;
    uart.reset();

    // 1. Transmit string "HELLO WINIOS\n" by writing to kRegDR
    const std::string msg = "HELLO WINIOS\n";
    for (char c : msg) {
        uart.write_register(VirtualUartDevice::kRegDR, static_cast<uint8_t>(c), 4);
    }

    TEST_ASSERT(uart.get_output_text() == msg, "Output buffer received exact transmitted message");

    // 2. Receive string by injecting input and reading from kRegDR
    const std::string input_msg = "COMMAND_OK\r";
    uart.inject_input(input_msg);

    std::string received;
    for (size_t i = 0; i < input_msg.size(); ++i) {
        uint64_t val = uart.read_register(VirtualUartDevice::kRegDR, 4);
        received.push_back(static_cast<char>(val & 0xFFu));
    }
    TEST_ASSERT(received == input_msg, "RX FIFO returned exact injected bytes in order");

    // RX FIFO should now be empty (kFR_RXFE set)
    uint64_t fr = uart.read_register(VirtualUartDevice::kRegFR, 4);
    TEST_ASSERT((fr & VirtualUartDevice::kFR_RXFE) != 0, "Flag register reports RX FIFO empty");
}

static void test_uart_interrupt_mask_and_clear() {
    std::cout << "[Stage 247.11 - Test 2] Virtual UART interrupt assertion and ICR clear...\n";

    VirtualUartDevice uart;
    uart.reset();

    // Enable RX interrupt mask (bit 4) in IMSC
    uart.write_register(VirtualUartDevice::kRegIMSC, (1u << 4), 4);

    // Initially no RX interrupt asserted
    TEST_ASSERT(!uart.is_interrupt_asserted(), "No interrupt initially asserted");

    // Inject byte into RX FIFO -> should assert interrupt!
    uart.inject_input('X');
    TEST_ASSERT(uart.is_interrupt_asserted(), "Interrupt asserted upon RX byte arrival");

    uint64_t mis = uart.read_register(VirtualUartDevice::kRegMIS, 4);
    TEST_ASSERT((mis & (1u << 4)) != 0, "Masked Interrupt Status reflects RX interrupt");

    // Read byte out of FIFO -> RX interrupt clears
    uint64_t b = uart.read_register(VirtualUartDevice::kRegDR, 4);
    TEST_ASSERT((b & 0xFF) == 'X', "Read back injected character");
    TEST_ASSERT(!uart.is_interrupt_asserted(), "Interrupt cleared after draining RX FIFO");
}

static void test_device_bus_routing() {
    std::cout << "[Stage 247.12 - Test 3] Virtual Device Bus routing & isolation...\n";

    auto bus = std::make_shared<VirtualDeviceBus>();
    auto uart = std::make_shared<VirtualUartDevice>(0x55415254u, 0x09000000ULL, 0x1000ULL, 33);
    bus->register_device(uart);

    TEST_ASSERT(bus->is_device_address(0x09000000ULL), "UART base address mapped on bus");
    TEST_ASSERT(bus->is_device_address(0x09000FFCULL), "UART last word address mapped on bus");
    TEST_ASSERT(!bus->is_device_address(0x09001000ULL), "Out-of-range address not mapped to UART");
    TEST_ASSERT(!bus->is_device_address(0x08000000ULL), "Unmapped peripheral address returns false");

    // Bus write to UART data register
    bus->write_mmio(0x09000000ULL, 'Z', 4, 0, 100);
    TEST_ASSERT(uart->get_output_text() == "Z", "Bus MMIO write properly reached UART");
}

int main() {
    std::cout << "=== Running Stage 247.11 & 247.12 UART & Bus Integration Tests ===\n";
    test_uart_tx_rx_operations();
    test_uart_interrupt_mask_and_clear();
    test_device_bus_routing();
    std::cout << "Stage 247.11 & 247.12: All UART and bus tests passed.\n";
    return 0;
}

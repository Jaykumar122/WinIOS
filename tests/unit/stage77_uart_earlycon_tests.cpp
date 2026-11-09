#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "device/virtual_platform.h"
#include "device/virtual_uart_device.h"

#define STAGE77_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::device;

namespace {

// -----------------------------------------------------------------------------
// Suite 1: Earlycon Polling TX Stream
// -----------------------------------------------------------------------------
bool test_earlycon_polling_tx_stream() {
    std::cout << "[1] test_earlycon_polling_tx_stream...\n";

    VirtualUartDevice uart;

    const std::string banner = "[    0.000000] Linux version 6.6.0 (gcc 13.2.0) #1 SMP PREEMPT\n"
                               "[    0.000000] earlycon: pl011 at MMIO 0x0000000009000000 (options '')\n"
                               "[    0.000000] bootconsole [pl011] enabled\n";

    // Simulate Linux earlycon_write loop:
    // for each char: poll FR until TXFF == 0, write DR
    for (char c : banner) {
        std::uint64_t fr = uart.read_register(VirtualUartDevice::kRegFR, 4);
        STAGE77_ASSERT((fr & VirtualUartDevice::kFR_TXFF) == 0, "TX FIFO must not be full");
        uart.write_register(VirtualUartDevice::kRegDR, static_cast<std::uint8_t>(c), 4);
    }

    std::string captured = uart.get_output_text();
    STAGE77_ASSERT(captured == banner, "Captured earlycon banner text mismatch");

    std::cout << "[PASS] Earlycon Polling TX Stream\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: PL011 Flag Register Status Bits
// -----------------------------------------------------------------------------
bool test_pl011_flag_register_status_bits() {
    std::cout << "[2] test_pl011_flag_register_status_bits...\n";

    VirtualUartDevice uart;

    // Initially: TX FIFO empty, RX FIFO empty
    std::uint64_t fr_init = uart.read_register(VirtualUartDevice::kRegFR, 4);
    STAGE77_ASSERT((fr_init & VirtualUartDevice::kFR_TXFE) != 0, "TXFE must be set initially");
    STAGE77_ASSERT((fr_init & VirtualUartDevice::kFR_RXFE) != 0, "RXFE must be set initially");
    STAGE77_ASSERT((fr_init & VirtualUartDevice::kFR_RXFF) == 0, "RXFF must be 0 initially");

    // Inject input
    uart.inject_input('K');
    std::uint64_t fr_data = uart.read_register(VirtualUartDevice::kRegFR, 4);
    STAGE77_ASSERT((fr_data & VirtualUartDevice::kFR_RXFE) == 0, "RXFE must be cleared when RX has data");

    // Read byte back from DR
    std::uint64_t dr = uart.read_register(VirtualUartDevice::kRegDR, 4);
    STAGE77_ASSERT((dr & 0xFFu) == 'K', "Read byte mismatch");

    // Now RX should be empty again
    std::uint64_t fr_empty = uart.read_register(VirtualUartDevice::kRegFR, 4);
    STAGE77_ASSERT((fr_empty & VirtualUartDevice::kFR_RXFE) != 0, "RXFE must be set again after read");

    std::cout << "[PASS] PL011 Flag Register Status Bits\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: AMBA PrimeCell ID Registers
// -----------------------------------------------------------------------------
bool test_amba_primecell_id_registers() {
    std::cout << "[3] test_amba_primecell_id_registers...\n";

    VirtualUartDevice uart;

    // Peripheral ID: 0x11, 0x10, 0x34, 0x00
    STAGE77_ASSERT(uart.read_register(0xFE0, 4) == 0x11u, "UARTPeriphID0 must be 0x11");
    STAGE77_ASSERT(uart.read_register(0xFE4, 4) == 0x10u, "UARTPeriphID1 must be 0x10");
    STAGE77_ASSERT(uart.read_register(0xFE8, 4) == 0x34u, "UARTPeriphID2 must be 0x34");
    STAGE77_ASSERT(uart.read_register(0xFEC, 4) == 0x00u, "UARTPeriphID3 must be 0x00");

    // PrimeCell ID: 0x0D, 0xF0, 0x05, 0xB1 ('\xB1\x05\xF0\x0D')
    STAGE77_ASSERT(uart.read_register(0xFF0, 4) == 0x0Du, "UARTPCellID0 must be 0x0D");
    STAGE77_ASSERT(uart.read_register(0xFF4, 4) == 0xF0u, "UARTPCellID1 must be 0xF0");
    STAGE77_ASSERT(uart.read_register(0xFF8, 4) == 0x05u, "UARTPCellID2 must be 0x05");
    STAGE77_ASSERT(uart.read_register(0xFFC, 4) == 0xB1u, "UARTPCellID3 must be 0xB1");

    std::cout << "[PASS] AMBA PrimeCell ID Registers\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Line Control and Baud Rate Registers
// -----------------------------------------------------------------------------
bool test_line_control_and_baud_rate_registers() {
    std::cout << "[4] test_line_control_and_baud_rate_registers...\n";

    VirtualUartDevice uart;

    // Standard 115200 baud configuration in Linux:
    // IBRD = 26, FBRD = 3
    uart.write_register(VirtualUartDevice::kRegIBRD, 26, 4);
    uart.write_register(VirtualUartDevice::kRegFBRD, 3, 4);
    STAGE77_ASSERT(uart.read_register(VirtualUartDevice::kRegIBRD, 4) == 26, "IBRD mismatch");
    STAGE77_ASSERT(uart.read_register(VirtualUartDevice::kRegFBRD, 4) == 3, "FBRD mismatch");

    // LCRH: 8-bit word length, FIFO enabled (WLEN=3, FEN=1 -> 0x70)
    uart.write_register(VirtualUartDevice::kRegLCRH, 0x70u, 4);
    STAGE77_ASSERT(uart.read_register(VirtualUartDevice::kRegLCRH, 4) == 0x70u, "LCRH mismatch");

    std::cout << "[PASS] Line Control and Baud Rate Registers\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: UART Control Register Enable Sequence
// -----------------------------------------------------------------------------
bool test_uart_control_register_enable_sequence() {
    std::cout << "[5] test_uart_control_register_enable_sequence...\n";

    VirtualUartDevice uart;

    // Disable UART
    uart.write_register(VirtualUartDevice::kRegCR, 0, 4);
    STAGE77_ASSERT(uart.read_register(VirtualUartDevice::kRegCR, 4) == 0, "CR disabled");

    // Enable UART, TX, and RX (bits 0, 8, 9)
    std::uint32_t cr_val = VirtualUartDevice::kCR_UARTEN | VirtualUartDevice::kCR_TXE | VirtualUartDevice::kCR_RXE;
    uart.write_register(VirtualUartDevice::kRegCR, cr_val, 4);
    STAGE77_ASSERT(uart.read_register(VirtualUartDevice::kRegCR, 4) == cr_val, "CR enabled mismatch");

    std::cout << "[PASS] UART Control Register Enable Sequence\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: RX Interrupt Generation and Masking
// -----------------------------------------------------------------------------
bool test_rx_interrupt_generation_and_masking() {
    std::cout << "[6] test_rx_interrupt_generation_and_masking...\n";

    VirtualUartDevice uart;

    // Mask is initially 0 (interrupts masked)
    STAGE77_ASSERT(uart.read_register(VirtualUartDevice::kRegIMSC, 4) == 0, "Initial IMSC must be 0");
    STAGE77_ASSERT(!uart.is_interrupt_asserted(), "IRQ line must not be asserted");

    // Injects data
    uart.inject_input('A');

    // Raw interrupt status (RIS) should show RX interrupt (bit 4)
    std::uint64_t ris = uart.read_register(VirtualUartDevice::kRegRIS, 4);
    STAGE77_ASSERT((ris & (1u << 4)) != 0, "RX RIS bit must be set");

    // Masked interrupt status (MIS) should still be 0 because IMSC is 0
    std::uint64_t mis = uart.read_register(VirtualUartDevice::kRegMIS, 4);
    STAGE77_ASSERT((mis & (1u << 4)) == 0, "RX MIS bit must be 0 when unmasked");
    STAGE77_ASSERT(!uart.is_interrupt_asserted(), "IRQ line not asserted while masked");

    // Unmask RX interrupt via IMSC
    uart.write_register(VirtualUartDevice::kRegIMSC, (1u << 4), 4);
    std::uint64_t mis_unmasked = uart.read_register(VirtualUartDevice::kRegMIS, 4);
    STAGE77_ASSERT((mis_unmasked & (1u << 4)) != 0, "RX MIS bit must be set when unmasked");
    STAGE77_ASSERT(uart.is_interrupt_asserted(), "IRQ line must now be asserted");

    // Read byte from DR -> clears RX FIFO and RX interrupt
    uart.read_register(VirtualUartDevice::kRegDR, 4);
    STAGE77_ASSERT(!uart.is_interrupt_asserted(), "IRQ line deasserted after draining FIFO");

    std::cout << "[PASS] RX Interrupt Generation and Masking\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: TX Interrupt Lifecycle and ICR Clear
// -----------------------------------------------------------------------------
bool test_tx_interrupt_lifecycle() {
    std::cout << "[7] test_tx_interrupt_lifecycle...\n";

    VirtualUartDevice uart;

    // Enable TX interrupt in IMSC (bit 5)
    uart.write_register(VirtualUartDevice::kRegIMSC, (1u << 5), 4);

    // Write to DR -> triggers TX interrupt
    uart.write_register(VirtualUartDevice::kRegDR, 'X', 4);
    STAGE77_ASSERT(uart.is_interrupt_asserted(), "TX interrupt asserted after DR write");

    // Clear TX interrupt via ICR (bit 5)
    uart.write_register(VirtualUartDevice::kRegICR, (1u << 5), 4);
    STAGE77_ASSERT(!uart.is_interrupt_asserted(), "TX interrupt cleared via ICR");

    std::cout << "[PASS] TX Interrupt Lifecycle and ICR Clear\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Platform Device Bus MMIO Integration
// -----------------------------------------------------------------------------
bool test_platform_bus_mmio_integration() {
    std::cout << "[8] test_platform_bus_mmio_integration...\n";

    VirtualPlatformConfig config;
    VirtualPlatform platform(config);
    auto bus = platform.device_bus();

    // Write string through bus at 0x09000000 (DR offset 0x00)
    std::string msg = "Booting Linux on WinIOS...\n";
    for (char c : msg) {
        bus->write_mmio(0x09000000ULL, static_cast<std::uint8_t>(c), 4);
    }

    std::string captured = platform.uart()->get_output_text();
    STAGE77_ASSERT(captured == msg, "Bus routed UART output mismatch");

    std::cout << "[PASS] Platform Device Bus MMIO Integration\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: IRQ Routing to GIC SPI 33
// -----------------------------------------------------------------------------
bool test_irq_routing_to_gic_spi33() {
    std::cout << "[9] test_irq_routing_to_gic_spi33...\n";

    VirtualPlatformConfig config;
    VirtualPlatform platform(config);
    GICv3& gic = platform.gic();
    auto uart = platform.uart();

    // Configure GIC
    gic.write_mmio(0x08000000ULL, 0x2ULL, 4);  // EnableGrp1NS in GICD
    gic.write_icc_pmr(0xF0, 0);
    gic.write_icc_igrpen1(1, 0);

    // Unmask RX interrupt on UART
    uart->write_register(VirtualUartDevice::kRegIMSC, (1u << 4), 4);

    // Inject input -> asserts UART IRQ (SPI 33)
    uart->inject_input('!');
    STAGE77_ASSERT(uart->is_interrupt_asserted(), "UART IRQ must be active");

    // Assert level line to GIC for SPI 33
    gic.set_spi_level(33, true);

    // Evaluate GIC pending signal
    auto sig = gic.evaluate(1, 0, 0);
    STAGE77_ASSERT(sig.has_interrupt, "GIC must signal interrupt");
    STAGE77_ASSERT(sig.intid == 33, "Interrupt INTID must be 33 (UART SPI)");

    // Clean up
    gic.set_spi_level(33, false);
    uart->read_register(VirtualUartDevice::kRegDR, 4);

    std::cout << "[PASS] IRQ Routing to GIC SPI 33\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: Console Stress Throughput and State Serialization
// -----------------------------------------------------------------------------
bool test_console_stress_and_state_serialization() {
    std::cout << "[10] test_console_stress_and_state_serialization...\n";

    VirtualUartDevice uart;

    // Stream 10,000 characters
    constexpr std::size_t kTotalChars = 10000;
    for (std::size_t i = 0; i < kTotalChars; ++i) {
        char c = static_cast<char>('A' + (i % 26));
        uart.write_register(VirtualUartDevice::kRegDR, c, 4);
    }
    STAGE77_ASSERT(uart.output_bytes_count() == kTotalChars, "Must buffer 10,000 chars");

    // Inject some RX input
    uart.inject_input("LOGIN");

    // Serialize state
    std::vector<std::uint8_t> state = uart.serialize_state();
    STAGE77_ASSERT(!state.empty(), "State must not be empty");

    // Reset device
    uart.reset();
    STAGE77_ASSERT(uart.output_bytes_count() == 0, "Buffer cleared on reset");

    // Deserialize state
    bool ok = uart.deserialize_state(state.data(), state.size());
    STAGE77_ASSERT(ok, "State deserialization must succeed");
    STAGE77_ASSERT(uart.output_bytes_count() == kTotalChars, "Restored buffer size match");

    // Read restored RX byte
    std::uint64_t c0 = uart.read_register(VirtualUartDevice::kRegDR, 4);
    STAGE77_ASSERT((c0 & 0xFFu) == 'L', "First restored RX byte must be 'L'");

    std::cout << "[PASS] Console Stress Throughput and State Serialization\n\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=== WinIOS Stage 77: PL011 UART & Early Console Tests ===\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*test_fn)(), const char* name) {
        if (test_fn()) {
            passed++;
        } else {
            failed++;
            std::cerr << "[FAIL] " << name << "\n";
        }
    };

    run_test(test_earlycon_polling_tx_stream, "test_earlycon_polling_tx_stream");
    run_test(test_pl011_flag_register_status_bits, "test_pl011_flag_register_status_bits");
    run_test(test_amba_primecell_id_registers, "test_amba_primecell_id_registers");
    run_test(test_line_control_and_baud_rate_registers, "test_line_control_and_baud_rate_registers");
    run_test(test_uart_control_register_enable_sequence, "test_uart_control_register_enable_sequence");
    run_test(test_rx_interrupt_generation_and_masking, "test_rx_interrupt_generation_and_masking");
    run_test(test_tx_interrupt_lifecycle, "test_tx_interrupt_lifecycle");
    run_test(test_platform_bus_mmio_integration, "test_platform_bus_mmio_integration");
    run_test(test_irq_routing_to_gic_spi33, "test_irq_routing_to_gic_spi33");
    run_test(test_console_stress_and_state_serialization, "test_console_stress_and_state_serialization");

    std::cout << "\nStage 77 Summary: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}

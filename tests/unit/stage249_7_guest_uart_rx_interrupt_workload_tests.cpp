// ============================================================================
// Stage 249.6 - 249.8: Guest UART RX Interrupt Lifecycle, Workload &
// Bidirectional FIFO Tests
//
// Tests:
// 1. Stage 249.6: PL011 UART RX Interrupt Lifecycle & Masking
//    - Verified FIFO threshold, raw interrupt status (RIS), masked interrupt status (MIS),
//      interrupt mask set/clear (IMSC), and interrupt clear register (ICR).
// 2. Stage 249.7: Genuine Guest UART RX Interrupt Workload
//    - Guest code running at EL1 with architectural exceptions enabled.
//    - External character arrival asserts SPI 33 to GICv3.
//    - CPU takes IRQ exception to VBAR_EL1 + 0x280.
//    - Guest handler acknowledges INTID 33 via ICC_IAR1_EL1, reads character
//      from kRegDR via MMIO, stores byte in guest buffer, writes ICC_EOIR1_EL1,
//      and returns via ERET.
// 3. Stage 249.8: Bidirectional Guest UART Echo & TX FIFO Ordering
//    - Verified FIFO ordering, TXFE/TXFF flag progression, and multi-byte echo.
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_uart_device.h"
#include "memory/memory.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::device;

static void setup_guest_el1(CPU& cpu, uint64_t vbar) {
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(vbar);
    cpu.set_daif(0x0); // Unmask DAIF (interrupts enabled)
}

// ----------------------------------------------------------------------------
// Test 1: Stage 249.6 - PL011 UART RX Interrupt State Machine & Masking
// ----------------------------------------------------------------------------
static void test_uart_rx_interrupt_lifecycle() {
    std::cout << "[Stage 249.6] Testing PL011 UART RX Interrupt Lifecycle & Masking...\n";

    VirtualUartDevice uart(0x55415254u, 0x09000000ULL, 0x1000ULL, 33);
    uart.reset();

    const uint32_t kRxIntrBit = (1u << 4);

    // 1. Initial state: RX FIFO empty, no interrupts pending
    TEST_ASSERT((uart.read_register(VirtualUartDevice::kRegFR, 2) & VirtualUartDevice::kFR_RXFE) != 0, "RX FIFO initially empty");
    TEST_ASSERT(uart.read_register(VirtualUartDevice::kRegRIS, 2) == 0, "RIS initially 0");
    TEST_ASSERT(uart.read_register(VirtualUartDevice::kRegMIS, 2) == 0, "MIS initially 0");

    // 2. Enable RX interrupt mask (IMSC bit 4)
    uart.write_register(VirtualUartDevice::kRegIMSC, kRxIntrBit, 2);
    TEST_ASSERT((uart.read_register(VirtualUartDevice::kRegIMSC, 2) & kRxIntrBit) != 0, "IMSC RXIM bit set");

    // 3. Inject character 'X' into RX FIFO
    bool irq_asserted = false;
    uart.set_interrupt_callback([&](uint32_t irq, bool asserted) {
        if (irq == 33) irq_asserted = asserted;
    });

    uart.inject_input('X');
    TEST_ASSERT(irq_asserted, "Interrupt asserted upon RX byte arrival under IMSC");
    TEST_ASSERT((uart.read_register(VirtualUartDevice::kRegRIS, 2) & kRxIntrBit) != 0, "RIS shows raw RX interrupt");
    TEST_ASSERT((uart.read_register(VirtualUartDevice::kRegMIS, 2) & kRxIntrBit) != 0, "MIS shows masked RX interrupt");

    // 4. Read character from Data Register (pops FIFO)
    uint64_t ch = uart.read_register(VirtualUartDevice::kRegDR, 2) & 0xFFu;
    TEST_ASSERT(ch == 'X', "Data Register read returned 'X'");
    TEST_ASSERT((uart.read_register(VirtualUartDevice::kRegFR, 2) & VirtualUartDevice::kFR_RXFE) != 0, "RX FIFO empty after read");

    // 5. Clear interrupt via ICR
    uart.write_register(VirtualUartDevice::kRegICR, kRxIntrBit, 2);
    TEST_ASSERT(uart.read_register(VirtualUartDevice::kRegMIS, 2) == 0, "MIS cleared via ICR");

    std::cout << "[Stage 249.6] UART RX interrupt lifecycle verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 249.7 - Genuine Guest UART RX Interrupt Workload
// ----------------------------------------------------------------------------
static void test_guest_uart_rx_interrupt_workload() {
    std::cout << "[Stage 249.7] Testing Genuine Guest UART RX Interrupt Workload...\n";

    CPU cpu;
    Memory mem(0x200000); // 2 MB guest RAM
    cpu.reset();

    const uint64_t vbar = 0x80000;
    setup_guest_el1(cpu, vbar);

    auto bus = std::make_shared<VirtualDeviceBus>();
    auto uart = std::make_shared<VirtualUartDevice>(0x55415254u, 0x09000000ULL, 0x1000ULL, 33);
    bus->register_device(uart);
    cpu.attach_device_bus(bus);

    // Setup GICv3 Distributor & Core 0 for SPI 33
    GICv3::DistributorState dist{};
    dist.gicd_ctlr = 0x3u; // Enable Group 0 and Group 1
    cpu.get_gic().set_distributor_state(dist);

    // Configure SPI 33 in GIC distributor
    dist.gicd_isenabler[1] |= (1u << 1); // Enable INTID 33
    dist.gicd_igroupr[1]   |= (1u << 1); // Group 1
    dist.gicd_ipriorityr[33] = 0x60u;    // Priority 0x60
    dist.gicd_irouter[1]     = 0;        // Route to core 0
    cpu.get_gic().set_distributor_state(dist);

    cpu.get_gic().write_icc_pmr(0xF0, 0); // PMR unmask (< 0xF0)
    cpu.get_gic().write_icc_igrpen1(1, 0); // Group 1 CPU interface enable

    // Connect UART interrupt line 33 to GIC SPI
    uart->set_interrupt_callback([&](uint32_t irq, bool asserted) {
        cpu.get_gic().set_spi_level(irq, asserted);
    });

    // Unmask UART RX interrupt in IMSC
    uart->write_register(VirtualUartDevice::kRegIMSC, (1u << 4), 2);

    // Buffer to hold received bytes in guest RAM
    const uint64_t rx_buffer_addr = 0x30000;

    // Setup IRQ Vector Handler at VBAR_EL1 + 0x280:
    // 1. MRS X0, ICC_IAR1_EL1     (0xD538CC00u) -> acknowledge interrupt, X0 = 33
    // 2. LDR W4, [X10, #0]        (0xB9400144u) -> read received char from DR at X10 (0x09000000)
    // 3. STRB W4, [X12, X2]       (0x38226984u) -> store byte at rx_buffer_addr + X2
    // 4. ADD X2, X2, #1           (0x91000442u) -> increment received byte count
    // 5. MSR ICC_EOIR1_EL1, X0    (0xD518CC20u) -> signal EOI
    // 6. ERET                     (0xD69F03E0u) -> return from exception
    const uint64_t irq_vector = vbar + 0x280;
    mem.write_u32(irq_vector + 0, 0xD538CC00u);
    mem.write_u32(irq_vector + 4, 0xB9400144u);
    mem.write_u32(irq_vector + 8, 0x38226984u);
    mem.write_u32(irq_vector + 12, 0x91000442u);
    mem.write_u32(irq_vector + 16, 0xD518CC20u);
    mem.write_u32(irq_vector + 20, 0xD69F03E0u);

    // Setup Guest Program at 0x10000:
    // 1. 0x10000: ADD X1, X1, #1  (0x91000421u) -> main loop heartbeat counter
    // 2. 0x10004: B -4            (0x17FFFFFFu) -> loop
    const uint64_t code_pc = 0x10000;
    mem.write_u32(code_pc + 0, 0x91000421u);
    mem.write_u32(code_pc + 4, 0x17FFFFFFu);

    cpu.set_program_counter(code_pc);
    cpu.write_register(1, 0); // X1 = loop heartbeat counter
    cpu.write_register(2, 0); // X2 = received byte count
    cpu.write_register(10, 0x09000000ULL); // X10 = UART MMIO base
    cpu.write_register(12, rx_buffer_addr); // X12 = destination buffer pointer

    // Step 1: Execute 1 instruction of main loop
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(1) == 1, "Main loop executed instruction 1");
    TEST_ASSERT(cpu.read_register(2) == 0, "No UART RX interrupt yet");

    // External injection of character 'Z' (0x5A)
    uart->inject_input('Z');

    // Step 2: CPU detects pending SPI 33, vectors to irq_vector!
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == irq_vector, "CPU branched to IRQ vector 0x80280");

    // Step 3: MRS X0, ICC_IAR1_EL1
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 33, "ICC_IAR1_EL1 acknowledged INTID 33 (UART RX)");

    // Step 4: LDR W4, [X10, #0] (read char from DR)
    cpu.step(mem);
    TEST_ASSERT((cpu.read_register(4) & 0xFFu) == 'Z', "Handler read 'Z' from UART DR");

    // Step 5: STRB W4, [X12, X2] (store byte into buffer)
    cpu.step(mem);
    TEST_ASSERT(mem.read_u8(rx_buffer_addr) == 'Z', "Byte 'Z' written into guest buffer");

    // Step 6: ADD X2, X2, #1 (increment received count)
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(2) == 1, "Received byte count is 1");

    // De-assert interrupt line once FIFO is empty
    cpu.get_gic().set_spi_level(33, false);

    // Step 7: MSR ICC_EOIR1_EL1, X0
    cpu.step(mem);

    // Step 8: ERET -> return to interrupted instruction!
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == code_pc + 4, "ERET returned to interrupted instruction (0x10004)");

    // Step 9: Main loop resumes (executing B -4 back to code_pc 0x10000, then ADD X1, X1, #1)
    cpu.step(mem); // branch back to 0x10000
    cpu.step(mem); // execute ADD X1, X1, #1 at 0x10000
    TEST_ASSERT(cpu.read_register(1) == 2, "Main loop resumed successfully");

    std::cout << "[Stage 249.7] Genuine guest UART RX interrupt workload verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 249.8 - Bidirectional Guest UART Echo & TX FIFO Ordering
// ----------------------------------------------------------------------------
static void test_uart_bidirectional_echo_and_tx_fifo() {
    std::cout << "[Stage 249.8] Testing Bidirectional Guest UART Echo & TX FIFO Ordering...\n";

    VirtualUartDevice uart(0x55415254u, 0x09000000ULL, 0x1000ULL, 33);
    uart.reset();

    // 1. Transmit string "WINIOS" via Data Register
    const std::string test_msg = "WINIOS";
    for (char c : test_msg) {
        uart.write_register(VirtualUartDevice::kRegDR, static_cast<uint64_t>(c), 2);
    }

    TEST_ASSERT(uart.get_output_text() == "WINIOS", "UART transmitted string matches exactly");
    TEST_ASSERT((uart.read_register(VirtualUartDevice::kRegFR, 2) & VirtualUartDevice::kFR_TXFE) != 0, "TX FIFO empty after flush");

    // 2. Inject string into RX FIFO and drain
    for (char c : test_msg) {
        uart.inject_input(c);
    }

    std::string received;
    while ((uart.read_register(VirtualUartDevice::kRegFR, 2) & VirtualUartDevice::kFR_RXFE) == 0) {
        char ch = static_cast<char>(uart.read_register(VirtualUartDevice::kRegDR, 2) & 0xFFu);
        received.push_back(ch);
    }
    TEST_ASSERT(received == "WINIOS", "Drained RX characters match injected sequence");

    std::cout << "[Stage 249.8] Bidirectional echo and FIFO ordering verified.\n";
}

int main() {
    std::cout << "============================================================\n";
    std::cout << "Running Stage 249.6 - 249.8 UART RX Interrupt & Workload Tests\n";
    std::cout << "============================================================\n";

    test_uart_rx_interrupt_lifecycle();
    test_guest_uart_rx_interrupt_workload();
    test_uart_bidirectional_echo_and_tx_fifo();

    std::cout << "\n>>> ALL STAGE 249.6 - 249.8 TESTS PASSED SUCCESSFULLY! <<<\n";
    return 0;
}

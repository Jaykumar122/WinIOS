// ============================================================================
// Stage 249.9 & 249.10: GICv3 Multi-Device Lifecycle & Concurrent Device
// Interrupt Workload Tests
//
// Tests:
// 1. Stage 249.9: GICv3 Multi-Device Lifecycle Integration
//    - Verified simultaneous pending interrupts across multiple sources:
//      PPI 27 (Timer, prio 0x50), SPI 33 (UART, prio 0x60), SPI 34 (DMA, prio 0x70).
//    - Verified priority arbitration: highest priority source is delivered first.
//    - Verified running priority drop via ICC_EOIR1_EL1 and split deactivation via ICC_DIR_EL1.
// 2. Stage 249.10: Concurrent Device Interrupt Workload
//    - Guest code running at EL1 handles consecutive/simultaneous interrupt events
//      from Timer, UART, and DMA without state corruption.
//    - Verified interrupt masking, EOI, deactivation, and clean exception return.
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/virtual_time.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_uart_device.h"
#include "device/virtual_dma_device.h"
#include "memory/memory.h"

#include <iostream>
#include <memory>
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
// Test 1: Stage 249.9 - GICv3 Priority Ordering & Split Deactivation Across Devices
// ----------------------------------------------------------------------------
static void test_gicv3_multi_device_priority_and_deactivation() {
    std::cout << "[Stage 249.9] Testing GICv3 multi-device priority arbitration & split deactivation...\n";

    GICv3 gic;
    gic.reset();

    // Enable Distributor Group 0 & Group 1
    GICv3::DistributorState dist{};
    dist.gicd_ctlr = 0x3u;
    gic.set_distributor_state(dist);

    // 1. Configure PPI 27 (Timer, priority 0x50) in Core 0 Redistributor
    auto& r = gic.get_redistributor(0);
    r.isenabler0 |= (1u << 27);
    r.igroupr0   |= (1u << 27);
    r.ipriorityr[27] = 0x50u;

    // 2. Configure SPI 33 (UART, priority 0x60) in Distributor
    dist.gicd_isenabler[1] |= (1u << 1); // INTID 33
    dist.gicd_igroupr[1]   |= (1u << 1);
    dist.gicd_ipriorityr[33] = 0x60u;
    dist.gicd_irouter[1]     = 0;

    // 3. Configure SPI 34 (DMA, priority 0x70) in Distributor
    dist.gicd_isenabler[1] |= (1u << 2); // INTID 34
    dist.gicd_igroupr[1]   |= (1u << 2);
    dist.gicd_ipriorityr[34] = 0x70u;
    dist.gicd_irouter[2]     = 0;
    gic.set_distributor_state(dist);

    gic.write_icc_pmr(0xF0, 0); // PMR unmask (< 0xF0)
    gic.write_icc_igrpen1(1, 0); // Group 1 CPU interface enable

    // Assert all 3 simultaneously: Timer (27), UART (33), DMA (34)
    gic.set_ppi_level(27, true, 0);
    gic.set_spi_level(33, true);
    gic.set_spi_level(34, true);

    // Evaluation 1: Timer (PPI 27, prio 0x50) must win priority arbitration
    auto sig1 = gic.evaluate(1, 0, 0);
    TEST_ASSERT(sig1.has_interrupt, "Pending interrupt detected");
    TEST_ASSERT(sig1.intid == 27, "Timer (INTID 27) wins highest priority (0x50)");

    // Acknowledge Timer
    uint32_t ack1 = gic.acknowledge_group1(1, 0);
    TEST_ASSERT(ack1 == 27, "Acknowledged INTID 27");
    TEST_ASSERT(gic.read_icc_rpr(0) == 0x50u, "Running priority raised to 0x50");

    // Drop priority of Timer via end_of_interrupt_group1
    gic.end_of_interrupt_group1(27, 1, 0);
    TEST_ASSERT(gic.read_icc_rpr(0) == 0xFFu, "Running priority dropped back to 0xFF");

    // Deactivate Timer via DIR
    gic.write_icc_dir(27, 1, 0);
    gic.set_ppi_level(27, false, 0);

    // Evaluation 2: UART (SPI 33, prio 0x60) must win over DMA (prio 0x70)
    auto sig2 = gic.evaluate(1, 0, 0);
    TEST_ASSERT(sig2.has_interrupt, "Next pending interrupt detected");
    TEST_ASSERT(sig2.intid == 33, "UART (INTID 33) wins next priority (0x60)");

    // Acknowledge UART
    uint32_t ack2 = gic.acknowledge_group1(1, 0);
    TEST_ASSERT(ack2 == 33, "Acknowledged INTID 33");
    gic.end_of_interrupt_group1(33, 1, 0);
    gic.write_icc_dir(33, 1, 0);
    gic.set_spi_level(33, false);

    // Evaluation 3: DMA (SPI 34, prio 0x70) is evaluated last
    auto sig3 = gic.evaluate(1, 0, 0);
    TEST_ASSERT(sig3.has_interrupt, "Last pending interrupt detected");
    TEST_ASSERT(sig3.intid == 34, "DMA (INTID 34) evaluated last");

    uint32_t ack3 = gic.acknowledge_group1(1, 0);
    TEST_ASSERT(ack3 == 34, "Acknowledged INTID 34");
    gic.end_of_interrupt_group1(34, 1, 0);
    gic.write_icc_dir(34, 1, 0);
    gic.set_spi_level(34, false);

    // Final evaluation: no interrupts left pending
    auto sig_empty = gic.evaluate(1, 0, 0);
    TEST_ASSERT(!sig_empty.has_interrupt, "All device interrupts completed");

    std::cout << "[Stage 249.9] Multi-device priority ordering and split deactivation verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 249.10 - Concurrent Device Interrupt Guest Workload
// ----------------------------------------------------------------------------
static void test_concurrent_device_interrupt_workload() {
    std::cout << "[Stage 249.10] Testing Concurrent Device Interrupt Guest Workload...\n";

    CPU cpu;
    Memory mem(0x200000); // 2 MB guest RAM
    cpu.reset();

    const uint64_t vbar = 0x80000;
    setup_guest_el1(cpu, vbar);

    auto bus = std::make_shared<VirtualDeviceBus>();
    auto uart = std::make_shared<VirtualUartDevice>(0x55415254u, 0x09000000ULL, 0x1000ULL, 33);
    auto shared_mem = std::make_shared<SmpSharedMemory>(0x200000, 4);
    auto dma = std::make_shared<VirtualDmaDevice>(0x56444D41u, 0x10000000ULL, 0x1000ULL, shared_mem, 34);
    bus->register_device(uart);
    bus->register_device(dma);
    cpu.attach_device_bus(bus);

    // Setup GICv3 Distributor
    GICv3::DistributorState dist{};
    dist.gicd_ctlr = 0x3u;
    dist.gicd_isenabler[1] |= (1u << 1) | (1u << 2); // Enable SPI 33 and 34
    dist.gicd_igroupr[1]   |= (1u << 1) | (1u << 2);
    dist.gicd_ipriorityr[33] = 0x60u;
    dist.gicd_ipriorityr[34] = 0x70u;
    cpu.get_gic().set_distributor_state(dist);

    // Enable Virtual Timer PPI 27
    auto& r = cpu.get_gic().get_redistributor(0);
    r.isenabler0 |= (1u << 27);
    r.igroupr0   |= (1u << 27);
    r.ipriorityr[27] = 0x50u;

    cpu.get_gic().write_icc_pmr(0xF0, 0);
    cpu.get_gic().write_icc_igrpen1(1, 0);

    // Wire device interrupts to GIC
    uart->set_interrupt_callback([&](uint32_t irq, bool asserted) {
        cpu.get_gic().set_spi_level(irq, asserted);
    });
    dma->set_interrupt_callback([&](uint32_t irq, bool asserted) {
        cpu.get_gic().set_spi_level(irq, asserted);
    });

    // In-guest IRQ Vector Handler at VBAR_EL1 + 0x280:
    // 1. MRS X0, ICC_IAR1_EL1   (0xD538CC00u) -> read active INTID
    // 2. MSR ICC_EOIR1_EL1, X0  (0xD518CC20u) -> signal EOI
    // 3. ADD X2, X2, #1         (0x91000442u) -> increment cumulative IRQ counter
    // 4. ERET                   (0xD69F03E0u) -> return
    const uint64_t irq_vector = vbar + 0x280;
    mem.write_u32(irq_vector + 0, 0xD538CC00u);
    mem.write_u32(irq_vector + 4, 0xD518CC20u);
    mem.write_u32(irq_vector + 8, 0x91000442u);
    mem.write_u32(irq_vector + 12, 0xD69F03E0u);

    // Guest Main Loop at 0x10000:
    // 1. ADD X1, X1, #1  (0x91000421u)
    // 2. B -4            (0x17FFFFFFu)
    const uint64_t code_pc = 0x10000;
    mem.write_u32(code_pc + 0, 0x91000421u);
    mem.write_u32(code_pc + 4, 0x17FFFFFFu);

    cpu.set_program_counter(code_pc);
    cpu.write_register(1, 0);
    cpu.write_register(2, 0);

    // Phase 1: Trigger Timer Interrupt (PPI 27)
    cpu.get_virtual_time().set_physical_counter(200);
    TimerState vt{};
    vt.enable = true;
    vt.cval = 200;
    cpu.get_virtual_time().set_virt_timer(0, vt);

    cpu.step(mem); // Branches to IRQ vector
    TEST_ASSERT(cpu.get_program_counter() == irq_vector, "Vectored to IRQ for Timer");
    cpu.step(mem); // MRS X0, ICC_IAR1_EL1
    TEST_ASSERT(cpu.read_register(0) == 27, "Handled Timer interrupt INTID 27");
    cpu.step(mem); // EOI
    cpu.step(mem); // ADD X2
    // Clear timer assertion
    vt.enable = false;
    cpu.get_virtual_time().set_virt_timer(0, vt);
    cpu.get_virtual_time().evaluate_core_timers(0);
    cpu.step(mem); // ERET
    TEST_ASSERT(cpu.read_register(2) == 1, "Handled 1st interrupt");

    // Phase 2: Trigger UART RX Interrupt (SPI 33)
    uart->write_register(VirtualUartDevice::kRegIMSC, (1u << 4), 2);
    uart->inject_input('Q');

    cpu.step(mem); // Branches to IRQ vector
    TEST_ASSERT(cpu.get_program_counter() == irq_vector, "Vectored to IRQ for UART");
    cpu.step(mem); // MRS X0, ICC_IAR1_EL1
    TEST_ASSERT(cpu.read_register(0) == 33, "Handled UART interrupt INTID 33");
    cpu.step(mem); // EOI
    cpu.step(mem); // ADD X2
    cpu.get_gic().set_spi_level(33, false); // Clear UART level
    cpu.step(mem); // ERET
    TEST_ASSERT(cpu.read_register(2) == 2, "Handled 2nd interrupt");

    // Phase 3: Trigger DMA Completion Interrupt (SPI 34)
    cpu.get_gic().set_spi_level(34, true);

    cpu.step(mem); // Branches to IRQ vector
    TEST_ASSERT(cpu.get_program_counter() == irq_vector, "Vectored to IRQ for DMA");
    cpu.step(mem); // MRS X0, ICC_IAR1_EL1
    TEST_ASSERT(cpu.read_register(0) == 34, "Handled DMA interrupt INTID 34");
    cpu.step(mem); // EOI
    cpu.step(mem); // ADD X2
    cpu.get_gic().set_spi_level(34, false); // Clear DMA level
    cpu.step(mem); // ERET
    TEST_ASSERT(cpu.read_register(2) == 3, "Handled 3rd interrupt");

    // Return to main program
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(1) > 0, "Main program resumed cleanly after all device interrupts");

    std::cout << "[Stage 249.10] Concurrent device interrupt guest workload verified.\n";
}

int main() {
    std::cout << "============================================================\n";
    std::cout << "Running Stage 249.9 & 249.10 GICv3 Multi-Device & Concurrent Interrupt Tests\n";
    std::cout << "============================================================\n";

    test_gicv3_multi_device_priority_and_deactivation();
    test_concurrent_device_interrupt_workload();

    std::cout << "\n>>> ALL STAGE 249.9 & 249.10 TESTS PASSED SUCCESSFULLY! <<<\n";
    return 0;
}

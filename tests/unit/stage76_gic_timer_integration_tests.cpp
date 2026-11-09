#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/virtual_time.h"
#include "device/virtual_platform.h"
#include "memory/memory.h"

#define STAGE76_ASSERT(cond, msg) \
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
// Suite 1: CNTFRQ_EL0 Linux Clocksource Calibration
// -----------------------------------------------------------------------------
bool test_cntfrq_el0_linux_clocksource_calibration() {
    std::cout << "[1] test_cntfrq_el0_linux_clocksource_calibration...\n";

    VirtualPlatformConfig config;
    VirtualPlatform platform(config);
    VirtualTime& vtime = platform.virtual_time();

    // Default frequency must be non-zero (Linux uses it for loops_per_jiffy and clocksource)
    std::uint64_t cntfrq = vtime.get_frequency();
    STAGE76_ASSERT(cntfrq > 0, "CNTFRQ must be non-zero for Linux boot");
    STAGE76_ASSERT(cntfrq >= 1000000ULL, "CNTFRQ should be at least 1 MHz");

    // Write a standard 62.5 MHz frequency and verify persistence
    vtime.set_frequency(62500000ULL);
    std::uint64_t updated_frq = vtime.get_frequency();
    STAGE76_ASSERT(updated_frq == 62500000ULL, "CNTFRQ write persistence mismatch");

    std::cout << "[PASS] CNTFRQ_EL0 Linux Clocksource Calibration\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Physical Timer CVAL and TVAL Expiration
// -----------------------------------------------------------------------------
bool test_physical_timer_cval_and_tval() {
    std::cout << "[2] test_physical_timer_cval_and_tval...\n";

    VirtualPlatformConfig config;
    VirtualPlatform platform(config);
    VirtualTime& vtime = platform.virtual_time();

    vtime.set_physical_counter(1000);
    STAGE76_ASSERT(vtime.get_physical_counter() == 1000, "Counter init");

    // Configure core 0 physical timer: CVAL = 2000, Enable = true, Imask = false
    TimerState t{};
    t.enable = true;
    t.imask = false;
    t.cval = 2000;
    vtime.set_phys_timer(0, t);

    // Counter at 1000 < CVAL: IRQ not asserted
    vtime.evaluate_core_timers(0);
    STAGE76_ASSERT(!vtime.get_phys_timer_irq(0), "Physical timer must not assert IRQ before deadline");

    // Advance to 2000
    vtime.advance_physical_counter(1000);
    STAGE76_ASSERT(vtime.get_physical_counter() == 2000, "Counter advanced to 2000");
    vtime.evaluate_core_timers(0);
    STAGE76_ASSERT(vtime.get_phys_timer_irq(0), "Physical timer must assert IRQ at deadline");

    // Verify TVAL reading
    std::int32_t tval = static_cast<std::int32_t>(vtime.read_phys_tval(0));
    STAGE76_ASSERT(tval == 0, "TVAL must be 0 when counter == CVAL");

    std::cout << "[PASS] Physical Timer CVAL and TVAL Expiration\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Virtual Timer CVAL and TVAL Expiration
// -----------------------------------------------------------------------------
bool test_virtual_timer_cval_and_tval() {
    std::cout << "[3] test_virtual_timer_cval_and_tval...\n";

    VirtualPlatformConfig config;
    VirtualPlatform platform(config);
    VirtualTime& vtime = platform.virtual_time();

    vtime.set_physical_counter(5000);
    STAGE76_ASSERT(vtime.get_virtual_counter(0) == 5000, "Virtual counter init");

    // Configure core 0 virtual timer: CVAL = 6000, Enable = true, Imask = false
    TimerState t{};
    t.enable = true;
    t.imask = false;
    t.cval = 6000;
    vtime.set_virt_timer(0, t);

    vtime.evaluate_core_timers(0);
    STAGE76_ASSERT(!vtime.get_virt_timer_irq(0), "Virtual timer must not assert before deadline");

    vtime.advance_physical_counter(1000);
    STAGE76_ASSERT(vtime.get_virtual_counter(0) == 6000, "Virtual counter at 6000");
    vtime.evaluate_core_timers(0);
    STAGE76_ASSERT(vtime.get_virt_timer_irq(0), "Virtual timer must assert at deadline");

    std::int32_t tval = static_cast<std::int32_t>(vtime.read_virt_tval(0));
    STAGE76_ASSERT(tval == 0, "Virtual TVAL must be 0 when counter == CVAL");

    std::cout << "[PASS] Virtual Timer CVAL and TVAL Expiration\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Timer PPI Signaling to GIC
// -----------------------------------------------------------------------------
bool test_timer_ppi_signaling_to_gic() {
    std::cout << "[4] test_timer_ppi_signaling_to_gic...\n";

    VirtualPlatformConfig config;
    VirtualPlatform platform(config);
    GICv3& gic = platform.gic();
    VirtualTime& vtime = platform.virtual_time();

    // Configure GIC CPU interface: PMR unmasked, Group 1 enabled
    gic.write_icc_pmr(0xF0, 0);
    gic.write_icc_igrpen1(1, 0);

    // Initial PPI level deasserted
    gic.set_ppi_level(27, false, 0);

    // Set virtual timer to expire
    vtime.set_physical_counter(100);
    TimerState t{};
    t.enable = true;
    t.imask = false;
    t.cval = 200;
    vtime.set_virt_timer(0, t);

    vtime.advance_physical_counter(100);
    vtime.evaluate_core_timers(0);
    bool virt_irq = vtime.get_virt_timer_irq(0);
    STAGE76_ASSERT(virt_irq, "Virtual timer IRQ condition must be active");

    // Signal PPI 27 to GIC
    gic.set_ppi_level(27, virt_irq, 0);

    // Evaluate GIC pending state
    auto sig = gic.evaluate(1, 0, 0);
    STAGE76_ASSERT(sig.has_interrupt, "GIC must signal pending interrupt");
    STAGE76_ASSERT(sig.intid == 27, "Pending interrupt INTID must be 27 (CNTV PPI)");

    std::cout << "[PASS] Timer PPI Signaling to GIC\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: GICv3 ICC_SRE_EL1 Conformance
// -----------------------------------------------------------------------------
bool test_gicv3_icc_sre_el1_conformance() {
    std::cout << "[5] test_gicv3_icc_sre_el1_conformance...\n";

    VirtualPlatformConfig config;
    VirtualPlatform platform(config);
    GICv3& gic = platform.gic();

    // Linux kernel checks ICC_SRE_EL1 bit 0 (SRE) to ensure system registers are enabled
    std::uint32_t sre = gic.read_icc_sre_el1(0);
    STAGE76_ASSERT((sre & 0x1u) != 0, "ICC_SRE_EL1 bit 0 (SRE) must be 1 for Linux ARM64");

    std::cout << "[PASS] GICv3 ICC_SRE_EL1 Conformance\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: GICv3 PMR and IGRPEN1 Unmasking
// -----------------------------------------------------------------------------
bool test_gicv3_pmr_and_igrpen1_unmasking() {
    std::cout << "[6] test_gicv3_pmr_and_igrpen1_unmasking...\n";

    VirtualPlatformConfig config;
    VirtualPlatform platform(config);
    GICv3& gic = platform.gic();

    // Initial PMR
    gic.write_icc_pmr(0x00, 0);
    STAGE76_ASSERT(gic.read_icc_pmr(0) == 0x00, "PMR initial 0x00");

    // Linux unmasks interrupts by writing 0xF0 to PMR
    gic.write_icc_pmr(0xF0, 0);
    STAGE76_ASSERT(gic.read_icc_pmr(0) == 0xF0, "PMR must be 0xF0");

    // Linux enables Group 1 interrupts
    gic.write_icc_igrpen1(1, 0);
    STAGE76_ASSERT(gic.read_icc_igrpen1(0) == 1, "IGRPEN1 must be 1");
    STAGE76_ASSERT(gic.is_enabled(0), "GIC must be enabled after PMR/IGRPEN1 write");

    std::cout << "[PASS] GICv3 PMR and IGRPEN1 Unmasking\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Timer Interrupt Acknowledge and EOI Lifecycle
// -----------------------------------------------------------------------------
bool test_timer_interrupt_ack_and_eoi_lifecycle() {
    std::cout << "[7] test_timer_interrupt_ack_and_eoi_lifecycle...\n";

    VirtualPlatformConfig config;
    VirtualPlatform platform(config);
    GICv3& gic = platform.gic();

    // Setup GIC
    gic.write_icc_pmr(0xF0, 0);
    gic.write_icc_igrpen1(1, 0);

    // Assert PPI 30 (Physical Timer)
    gic.set_ppi_level(30, true, 0);

    // Evaluate
    auto sig = gic.evaluate(1, 0, 0);
    STAGE76_ASSERT(sig.has_interrupt && sig.intid == 30, "Interrupt INTID 30 pending");

    // Linux acknowledges via ICC_IAR1_EL1
    std::uint32_t iar = gic.acknowledge_group1(1, 0);
    STAGE76_ASSERT(iar == 30, "Acknowledge must return INTID 30");

    // Deassert physical timer level line
    gic.set_ppi_level(30, false, 0);

    // Linux signals completion via ICC_EOIR1_EL1
    gic.end_of_interrupt_group1(30, 1, 0);

    // Evaluate again: must now be clear
    auto sig_after = gic.evaluate(1, 0, 0);
    STAGE76_ASSERT(!sig_after.has_interrupt, "Interrupt must be inactive after EOI");

    std::cout << "[PASS] Timer Interrupt Acknowledge and EOI Lifecycle\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: SGI / IPI Multicore Dispatch
// -----------------------------------------------------------------------------
bool test_sgi_ipi_multicore_dispatch() {
    std::cout << "[8] test_sgi_ipi_multicore_dispatch...\n";

    VirtualPlatformConfig config;
    config.core_count = 2;
    VirtualPlatform platform(config);
    GICv3& gic = platform.gic();

    // Enable Group 1 in GICD
    gic.write_mmio(0x08000000ULL, 0x3ULL, 4);

    // Configure SGIs and PPIs as Group 1 (Non-secure) on Core 1 via GICR_IGROUPR0
    // Core 1 SGI base: 0x080A0000 + 1 * 0x20000 + 0x10000 + 0x0080 = 0x080D0080
    gic.write_mmio(0x080D0080ULL, 0xFFFFFFFFu, 4);

    // Enable SGIs (INTID 0..15) on Core 1 via GICR_ISENABLER0
    // Core 1 SGI base: 0x080A0000 + 1 * 0x20000 + 0x10000 + 0x0100 = 0x080D0100
    gic.write_mmio(0x080D0100ULL, 0xFFFFu, 4);

    // Unmask on core 1
    gic.write_icc_pmr(0xF0, 1);
    gic.write_icc_igrpen1(1, 1);

    // Core 0 generates SGI 1 targeting Core 1:
    // ICC_SGI1R_EL1: TargetList bit 1 set (0x2), INTID = 1 (bits 27:24 = 1)
    std::uint64_t sgi_val = (1ULL << 24) | 0x2ULL; // INTID=1, Target=Core 1
    gic.send_sgi(0, sgi_val);

    // Check Core 1 pending signal
    auto sig1 = gic.evaluate(1, 0, 1);
    STAGE76_ASSERT(sig1.has_interrupt, "Core 1 must receive SGI interrupt");
    STAGE76_ASSERT(sig1.intid == 1, "Core 1 SGI INTID must be 1");

    // Core 1 acknowledges and EOIs
    std::uint32_t iar1 = gic.acknowledge_group1(1, 1);
    STAGE76_ASSERT(iar1 == 1, "Core 1 acknowledge must return INTID 1");

    gic.end_of_interrupt_group1(1, 1, 1);
    auto sig1_after = gic.evaluate(1, 0, 1);
    STAGE76_ASSERT(!sig1_after.has_interrupt, "Core 1 interrupt must be cleared after EOI");

    std::cout << "[PASS] SGI / IPI Multicore Dispatch\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: DAIF Masking During Interrupt Delivery
// -----------------------------------------------------------------------------
bool test_daif_masking_during_interrupt_delivery() {
    std::cout << "[9] test_daif_masking_during_interrupt_delivery...\n";

    VirtualPlatformConfig config;
    VirtualPlatform platform(config);
    CPU& cpu = platform.get_cpu(0);
    GICv3& gic = platform.gic();

    // Unmask GIC
    gic.write_icc_pmr(0xF0, 0);
    gic.write_icc_igrpen1(1, 0);

    // Assert SPI 33 (UART)
    gic.set_spi_level(33, true);

    // CPU DAIF.I is initially masked (D=bit 3, A=bit 2, I=bit 1, F=bit 0)
    cpu.set_daif(0xFu); // all masked
    STAGE76_ASSERT(cpu.get_daif() & (1 << 1), "IRQ mask bit must be set");

    // Evaluation on GIC level shows pending interrupt
    auto sig = gic.evaluate(1, 0, 0);
    STAGE76_ASSERT(sig.has_interrupt, "GIC has pending interrupt");

    // Unmask IRQ bit in CPU DAIF (clear bit 1)
    cpu.set_daif(static_cast<std::uint8_t>(cpu.get_daif() & ~(1 << 1)));
    STAGE76_ASSERT((cpu.get_daif() & (1 << 1)) == 0, "IRQ mask bit must be cleared");

    std::cout << "[PASS] DAIF Masking During Interrupt Delivery\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: GIC Distributor and Redistributor MMIO Linux Boot Sequence
// -----------------------------------------------------------------------------
bool test_gic_distributor_and_redistributor_mmio_linux_boot_sequence() {
    std::cout << "[10] test_gic_distributor_and_redistributor_mmio_linux_boot_sequence...\n";

    VirtualPlatformConfig config;
    config.core_count = 1;
    VirtualPlatform platform(config);
    GICv3& gic = platform.gic();

    // 1. Read GICD_TYPER (0x08000004)
    std::uint32_t typer = static_cast<std::uint32_t>(gic.read_mmio(0x08000004ULL, 4));
    STAGE76_ASSERT(typer != 0, "GICD_TYPER must return valid topology info");

    // 2. Enable GICD (0x08000000)
    gic.write_mmio(0x08000000ULL, 0x2ULL, 4); // EnableGrp1NS
    std::uint32_t ctlr = static_cast<std::uint32_t>(gic.read_mmio(0x08000000ULL, 4));
    STAGE76_ASSERT((ctlr & 0x2u) != 0, "GICD_CTLR EnableGrp1NS must be set");

    // 3. Redistributor wake up: GICR_WAKER (0x080A0014)
    // Clear ProcessorSleep bit (bit 1)
    gic.write_mmio(0x080A0014ULL, 0x0ULL, 4);
    std::uint32_t waker = static_cast<std::uint32_t>(gic.read_mmio(0x080A0014ULL, 4));
    STAGE76_ASSERT((waker & (1u << 1)) == 0, "ProcessorSleep must be cleared");

    // 4. Enable PPI 27 in GICR_ISENABLER0 (0x080B0100)
    gic.write_mmio(0x080B0100ULL, (1u << 27), 4);
    std::uint32_t isenabler = static_cast<std::uint32_t>(gic.read_mmio(0x080B0100ULL, 4));
    STAGE76_ASSERT((isenabler & (1u << 27)) != 0, "PPI 27 must be enabled in GICR_ISENABLER0");

    std::cout << "[PASS] GIC Distributor and Redistributor MMIO Linux Boot Sequence\n\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=== WinIOS Stage 76: Linux-Compatible Generic Interrupt & Timer Integration Tests ===\n";

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

    run_test(test_cntfrq_el0_linux_clocksource_calibration, "test_cntfrq_el0_linux_clocksource_calibration");
    run_test(test_physical_timer_cval_and_tval, "test_physical_timer_cval_and_tval");
    run_test(test_virtual_timer_cval_and_tval, "test_virtual_timer_cval_and_tval");
    run_test(test_timer_ppi_signaling_to_gic, "test_timer_ppi_signaling_to_gic");
    run_test(test_gicv3_icc_sre_el1_conformance, "test_gicv3_icc_sre_el1_conformance");
    run_test(test_gicv3_pmr_and_igrpen1_unmasking, "test_gicv3_pmr_and_igrpen1_unmasking");
    run_test(test_timer_interrupt_ack_and_eoi_lifecycle, "test_timer_interrupt_ack_and_eoi_lifecycle");
    run_test(test_sgi_ipi_multicore_dispatch, "test_sgi_ipi_multicore_dispatch");
    run_test(test_daif_masking_during_interrupt_delivery, "test_daif_masking_during_interrupt_delivery");
    run_test(test_gic_distributor_and_redistributor_mmio_linux_boot_sequence, "test_gic_distributor_and_redistributor_mmio_linux_boot_sequence");

    std::cout << "\nStage 76 Summary: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}

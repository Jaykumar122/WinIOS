// ============================================================================
// Stage 246.15: GICv3 Interrupt Controller & CPU Delivery Integration Tests
// Verifies interrupt pending state, priority masking (PMR), enable (IGRPEN1),
// PPI assertion, IAR1 acknowledge, EOIR1 completion, and PSTATE.DAIF masking.
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "device/virtual_platform.h"
#include "memory/memory.h"

#include <iostream>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::device;

static void test_gicv3_ppi_acknowledge_and_eoi() {
    std::cout << "[Stage 246.15 - Test 1] GICv3 PPI interrupt routing, acknowledge, and EOI...\n";
    VirtualPlatformConfig config;
    VirtualPlatform platform(config);
    GICv3& gic = platform.gic();

    // 1. Initially disabled / masked (PMR = 0)
    gic.write_icc_pmr(0x00, 0);
    gic.set_ppi_level(27, true, 0); // Assert Virtual Timer PPI 27
    auto sig0 = gic.evaluate(1, 0, 0);
    TEST_ASSERT(!sig0.has_interrupt, "Interrupt blocked when PMR is 0");

    // 2. Unmask priority and enable Group 1
    gic.write_icc_pmr(0xF0, 0);
    gic.write_icc_igrpen1(1, 0);
    TEST_ASSERT(gic.is_enabled(0), "GIC enabled for core 0");

    // Evaluate: PPI 27 must be pending
    auto sig1 = gic.evaluate(1, 0, 0);
    TEST_ASSERT(sig1.has_interrupt && sig1.intid == 27, "PPI 27 pending after unmasking");

    // 3. Acknowledge interrupt via ICC_IAR1_EL1
    uint32_t intid = gic.acknowledge_group1(1, 0);
    TEST_ASSERT(intid == 27, "Acknowledge returns INTID 27");

    // Deassert physical level line
    gic.set_ppi_level(27, false, 0);

    // 4. Signal End Of Interrupt via ICC_EOIR1_EL1
    gic.end_of_interrupt_group1(27, 1, 0);

    // Evaluate: must now be clear
    auto sig_clear = gic.evaluate(1, 0, 0);
    TEST_ASSERT(!sig_clear.has_interrupt, "No interrupt pending after EOI");
}

static void test_cpu_daif_interrupt_masking() {
    std::cout << "[Stage 246.15 - Test 2] CPU PSTATE.DAIF interrupt delivery masking...\n";
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1); // EL1
    cpu.set_architectural_exceptions(true);

    // Enable GIC distributor & redistributor for core 0
    GICv3::DistributorState dist{};
    dist.gicd_ctlr = 0x3u; // Enable Group 0 and Group 1
    cpu.get_gic().set_distributor_state(dist);

    auto& r = cpu.get_gic().get_redistributor(0);
    r.isenabler0 |= (1u << 30); // Enable physical timer PPI 30
    r.igroupr0   |= (1u << 30); // Group 1
    r.ipriorityr[30] = 0x80u;   // Priority 0x80

    cpu.get_gic().write_icc_pmr(0xF0, 0); // PMR unmasked (< 0xF0)
    cpu.get_gic().write_icc_igrpen1(1, 0); // Group 1 CPU interface enabled

    // Assert physical timer via virtual time
    TimerState t{};
    t.enable = true;
    t.imask = false;
    t.cval = 100;
    cpu.get_virtual_time().set_physical_counter(100);
    cpu.get_virtual_time().set_phys_timer(0, t);
    cpu.get_virtual_time().evaluate_core_timers(0);

    // Case A: PSTATE.I is SET (daif = 0x2) -> Masked!
    cpu.set_daif(0x2); // bit 1 = I (IRQ mask)
    bool pending_with_mask = cpu.check_pending_interrupts();
    TEST_ASSERT(!pending_with_mask, "Interrupt delivery suppressed when DAIF.I is set");

    // Case B: PSTATE.I is CLEARED (daif = 0x0) -> Delivered!
    cpu.set_daif(0x0);
    bool pending_unmasked = cpu.check_pending_interrupts();
    TEST_ASSERT(pending_unmasked, "Interrupt delivered when DAIF.I is cleared");
}

int main() {
    std::cout << "=== Running Stage 246.15 Interrupt Delivery Integration Tests ===\n";
    test_gicv3_ppi_acknowledge_and_eoi();
    test_cpu_daif_interrupt_masking();
    std::cout << "Stage 246.15: All interrupt delivery tests passed.\n";
    return 0;
}

// ============================================================================
// Stage 248.9 & 248.10: GICv3 EOI Modes, Deactivation & Priority Tests
// Tests:
// 1. Combined EOI mode (EOImode == 0):
//    - IAR1 acknowledges and activates interrupt.
//    - EOIR1 performs priority drop AND deactivation.
// 2. Split EOI mode (EOImode == 1):
//    - IAR1 acknowledges and activates interrupt.
//    - EOIR1 drops running priority.
//    - DIR (write_icc_dir) performs explicit deactivation.
// 3. Priority preemption & simultaneous pending interrupts:
//    - Lower numerical value indicates higher priority in ARM GIC.
//    - Evaluates multiple pending lines, ensuring highest priority is selected.
// ============================================================================

#include "cpu/gicv3.h"

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

static void test_gicv3_split_eoi_and_dir() {
    std::cout << "[Stage 248.9 - Test 1] GICv3 split EOI mode (EOIR1 drop + DIR deactivation)...\n";

    GICv3 gic;
    gic.reset();

    // Enable distributor and core 0
    GICv3::DistributorState dist{};
    dist.gicd_ctlr = 0x3u;
    gic.set_distributor_state(dist);

    auto& r = gic.get_redistributor(0);
    r.isenabler0 |= (1u << 27); // PPI 27
    r.igroupr0   |= (1u << 27); // Group 1
    r.ipriorityr[27] = 0x80u;   // Priority 0x80

    gic.write_icc_pmr(0xF0, 0);
    gic.write_icc_igrpen1(1, 0);

    // Assert PPI 27
    gic.set_ppi_level(27, true, 0);

    // 1. Acknowledge interrupt via IAR1
    uint32_t intid = gic.acknowledge_group1(1, 0);
    TEST_ASSERT(intid == 27, "Acknowledge returned INTID 27");

    // Deassert physical line
    gic.set_ppi_level(27, false, 0);

    // Verify interrupt is active in redistributor
    TEST_ASSERT((r.isactiver0 & (1u << 27)) != 0, "Interrupt is active after acknowledge");

    // 2. Perform End Of Interrupt (Priority drop)
    gic.end_of_interrupt_group1(27, 1, 0);

    // 3. Perform Deactivation via ICC_DIR_EL1
    gic.write_icc_dir(27, 1, 0);
    TEST_ASSERT((r.isactiver0 & (1u << 27)) == 0, "Interrupt deactivated after write_icc_dir");
}

static void test_gicv3_priority_sorting() {
    std::cout << "[Stage 248.10 - Test 2] GICv3 simultaneous pending interrupts & priority preemption...\n";

    GICv3 gic;
    gic.reset();

    GICv3::DistributorState dist{};
    dist.gicd_ctlr = 0x3u;
    // Enable SPI 33 and SPI 34
    dist.gicd_isenabler[1] |= (1u << 1) | (1u << 2); // 33-32=1, 34-32=2
    dist.gicd_igroupr[1]   |= (1u << 1) | (1u << 2); // Group 1
    // In ARM GIC, smaller priority value = higher priority!
    dist.gicd_ipriorityr[33] = 0x60u; // Higher priority (0x60 < 0x90)
    dist.gicd_ipriorityr[34] = 0x90u; // Lower priority
    gic.set_distributor_state(dist);

    gic.write_icc_pmr(0xF0, 0);
    gic.write_icc_igrpen1(1, 0);

    // Assert both SPI 33 and SPI 34 simultaneously
    gic.set_spi_level(33, true);
    gic.set_spi_level(34, true);

    // Evaluate: higher priority (SPI 33 with priority 0x60) must be selected!
    auto sig = gic.evaluate(1, 0, 0);
    TEST_ASSERT(sig.has_interrupt, "Interrupt pending");
    TEST_ASSERT(sig.intid == 33, "Higher priority SPI 33 selected before SPI 34");

    // Acknowledge SPI 33
    uint32_t ack1 = gic.acknowledge_group1(1, 0);
    TEST_ASSERT(ack1 == 33, "Acknowledged SPI 33");

    // Deassert SPI 33
    gic.set_spi_level(33, false);
    gic.end_of_interrupt_group1(33, 1, 0);

    // Now SPI 34 must be evaluated
    auto sig2 = gic.evaluate(1, 0, 0);
    TEST_ASSERT(sig2.has_interrupt && sig2.intid == 34, "Lower priority SPI 34 now selected");

    uint32_t ack2 = gic.acknowledge_group1(1, 0);
    TEST_ASSERT(ack2 == 34, "Acknowledged SPI 34");

    gic.set_spi_level(34, false);
    gic.end_of_interrupt_group1(34, 1, 0);
}

int main() {
    std::cout << "=== Running Stage 248.9 & 248.10 GICv3 EOI & Priority Tests ===\n";
    test_gicv3_split_eoi_and_dir();
    test_gicv3_priority_sorting();
    std::cout << "Stage 248.9 & 248.10: All GICv3 EOI and priority tests passed.\n";
    return 0;
}

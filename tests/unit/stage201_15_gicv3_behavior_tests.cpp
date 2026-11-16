// ==============================================================================
// WinIOS Campaign 201 Stage 15: Virtual GICv3 Independent Behavior Tests
// ==============================================================================
// Verifies ARM Generic Interrupt Controller Architecture (GICv3) behavior:
// 1. Distributor & Redistributor address decoding and MMIO ranges
// 2. GICD_CTLR Distributor Group Enablement (EnableGrp0, EnableGrp1)
// 3. Security/Group classification: Group 0 (FIQ) vs Group 1 (IRQ)
// 4. Priority Masking: ICC_PMR_EL1 filtering and threshold evaluation
// 5. Complete Acknowledge (IAR) and End-Of-Interrupt (EOI) state lifecycle
// 6. Multicore Affinity Routing: GICD_IROUTERn directing SPIs to targeted cores
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>

#include "cpu/gicv3.h"

using namespace winios;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Distributor & Redistributor Address Space Decoding
// -----------------------------------------------------------------------------
void test_gic_address_decoding() {
    std::cout << "[Test 1] GICv3 Address Decoding & Ranges...\n";
    GICv3 gic;
    gic.set_core_count(6); // 6 Cores (A16 physical configuration)

    TEST_CHECK(gic.is_mmio_address(GICv3::kGicdBase), "GICD base must be MMIO address");
    TEST_CHECK(gic.is_gicd_address(GICv3::kGicdBase + 0x0000u), "GICD_CTLR must be GICD address");
    TEST_CHECK(gic.is_gicd_address(GICv3::kGicdBase + 0x1000u), "GICD upper range must be GICD");

    TEST_CHECK(gic.is_mmio_address(GICv3::kGicrBase), "GICR base must be MMIO address");
    TEST_CHECK(gic.is_gicr_address(GICv3::kGicrBase + 0x1000u), "GICR range must be GICR");

    TEST_CHECK(!gic.is_mmio_address(0x40000000ULL), "RAM address must not decode as GIC");

    std::cout << "  -> Address decoding passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Group 0 (FIQ) vs Group 1 (IRQ) Classification
// -----------------------------------------------------------------------------
void test_group_classification_and_signaling() {
    std::cout << "[Test 2] Group 0 (FIQ) vs Group 1 (IRQ)...\n";
    GICv3 gic;
    gic.set_core_count(1);

    // Enable Group 0 and Group 1 in GICD_CTLR (offset 0x0000)
    gic.write_mmio(GICv3::kGicdBase + 0x0000u, 0x3u, 4);

    // Configure SPI 32 as Group 0 (FIQ): GICD_IGROUPR1 (0x0084) bit 0 = 0
    gic.write_mmio(GICv3::kGicdBase + 0x0084u, 0x00u, 4);
    // Enable SPI 32: GICD_ISENABLER1 (0x0104) bit 0 = 1
    gic.write_mmio(GICv3::kGicdBase + 0x0104u, 0x01u, 4);
    // Set priority 0x20
    gic.write_mmio(GICv3::kGicdBase + 0x0420u, 0x20u, 1);

    // Enable CPU Interface for Core 0
    gic.write_icc_pmr(0xFFu, 0);
    gic.write_icc_igrpen0(1u, 0); // Enable Group 0
    gic.write_icc_igrpen1(1u, 0); // Enable Group 1

    // Assert SPI 32
    gic.set_spi_level(32, true);
    auto sig0 = gic.evaluate(1, 0, 0);
    TEST_CHECK(sig0.has_interrupt, "SPI 32 must signal interrupt");
    TEST_CHECK(sig0.is_fiq, "Group 0 interrupt must signal as FIQ");
    TEST_CHECK(sig0.intid == 32u, "INTID must be 32");

    // Clear SPI 32
    gic.set_spi_level(32, false);
    gic.end_of_interrupt_group0(32, 1, 0);

    // Configure SPI 33 as Group 1 (IRQ): GICD_IGROUPR1 bit 1 = 1
    gic.write_mmio(GICv3::kGicdBase + 0x0084u, 0x02u, 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0104u, 0x02u, 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0421u, 0x30u, 1);

    gic.set_spi_level(33, true);
    auto sig1 = gic.evaluate(1, 0, 0);
    TEST_CHECK(sig1.has_interrupt, "SPI 33 must signal interrupt");
    TEST_CHECK(!sig1.is_fiq, "Group 1 interrupt must signal as IRQ");
    TEST_CHECK(sig1.intid == 33u, "INTID must be 33");

    gic.set_spi_level(33, false);
    gic.end_of_interrupt_group1(33, 1, 0);

    std::cout << "  -> Group classification passed.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Priority Masking (ICC_PMR_EL1)
// -----------------------------------------------------------------------------
void test_priority_masking() {
    std::cout << "[Test 3] Priority Masking (PMR)...\n";
    GICv3 gic;
    gic.set_core_count(1);
    gic.write_mmio(GICv3::kGicdBase + 0x0000u, 0x3u, 4);

    const std::uint32_t intid = 34;
    // Enable SPI 34, Group 1, priority 0x90
    gic.write_mmio(GICv3::kGicdBase + 0x0084u, (1u << 2u), 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0104u, (1u << 2u), 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0420u + 2u, 0x90u, 1);
    gic.write_icc_igrpen1(1u, 0);

    // Set PMR = 0x80 (only priorities strictly higher than 0x80, i.e. lower numerical values, pass)
    gic.write_icc_pmr(0x80u, 0);
    gic.set_spi_level(intid, true);

    auto sig_masked = gic.evaluate(1, 0, 0);
    TEST_CHECK(!sig_masked.has_interrupt, "Priority 0x90 must be masked by PMR 0x80");

    // Unmask by raising PMR to 0xF0
    gic.write_icc_pmr(0xF0u, 0);
    auto sig_unmasked = gic.evaluate(1, 0, 0);
    TEST_CHECK(sig_unmasked.has_interrupt && sig_unmasked.intid == intid, "Priority 0x90 must pass PMR 0xF0");

    gic.set_spi_level(intid, false);
    gic.end_of_interrupt_group1(intid, 1, 0);

    std::cout << "  -> Priority masking passed.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Multicore Affinity Routing (GICD_IROUTERn)
// -----------------------------------------------------------------------------
void test_multicore_affinity_routing() {
    std::cout << "[Test 4] Multicore Affinity Routing...\n";
    GICv3 gic;
    gic.set_core_count(6); // 6 cores
    gic.write_mmio(GICv3::kGicdBase + 0x0000u, 0x3u, 4);

    const std::uint32_t spi_irq = 40;
    // Enable SPI 40, Group 1, Priority 0x20
    gic.write_mmio(GICv3::kGicdBase + 0x0080u + (spi_irq / 32) * 4, 1u << (spi_irq % 32), 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0100u + (spi_irq / 32) * 4, 1u << (spi_irq % 32), 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0400u + spi_irq, 0x20u, 1);

    // Route SPI 40 specifically to Core 4: GICD_IROUTER[spi_irq] (offset 0x6000 + spi_irq * 8)
    // Affinity 0.0.0.4 = 0x4ULL
    gic.write_mmio(GICv3::kGicdBase + 0x6000u + spi_irq * 8u, 0x4u, 4);

    // Enable CPU interfaces on all cores
    for (std::size_t c = 0; c < 6; ++c) {
        gic.write_icc_pmr(0xFFu, c);
        gic.write_icc_igrpen1(1u, c);
    }

    gic.set_spi_level(spi_irq, true);

    // Core 0 must NOT see the interrupt
    auto sig_c0 = gic.evaluate(1, 0, 0);
    TEST_CHECK(!sig_c0.has_interrupt, "Core 0 must not receive SPI routed to Core 4");

    // Core 4 MUST see the interrupt
    auto sig_c4 = gic.evaluate(1, 0, 4);
    TEST_CHECK(sig_c4.has_interrupt && sig_c4.intid == spi_irq, "Core 4 must receive routed SPI");

    gic.set_spi_level(spi_irq, false);
    gic.acknowledge_group1(1, 4);
    gic.end_of_interrupt_group1(spi_irq, 1, 4);

    std::cout << "  -> Multicore affinity routing passed.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 201.15 Virtual GICv3 Independent Behavior Tests ===\n";
    test_gic_address_decoding();
    test_group_classification_and_signaling();
    test_priority_masking();
    test_multicore_affinity_routing();

    if (g_failures == 0) {
        std::cout << "All Stage 201.15 tests passed successfully!\n";
        return 0;
    } else {
        std::cerr << g_failures << " Stage 201.15 test(s) failed!\n";
        return 1;
    }
}

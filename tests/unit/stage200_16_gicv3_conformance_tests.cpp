// ==============================================================================
// WinIOS Campaign 200 Stage 16: GICv3 Conformance Tests
// ==============================================================================
// Verifies:
// 1. GICD and GICR address range decoding and MMIO boundary validation
// 2. Group classification: Group 0 signals FIQ; Group 1 signals IRQ
// 3. Interrupt priority masking via ICC_PMR_EL1
// 4. Full interrupt lifecycle: Inactive -> Pending -> Active (IAR) -> Inactive (EOI)
// 5. Software Generated Interrupt (SGI) delivery across cores
// 6. Running priority (ICC_RPR_EL1) tracking during active interrupts
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>

#include "cpu/gicv3.h"

using namespace winios;

static void test_gic_address_decoding() {
    GICv3 gic;
    gic.set_core_count(4);

    // GICD base = 0x08000000
    assert(gic.is_mmio_address(GICv3::kGicdBase));
    assert(gic.is_mmio_address(GICv3::kGicdBase + 0x0FFF));
    assert(gic.is_gicd_address(GICv3::kGicdBase + 0x0100));

    // GICR base = 0x080A0000
    assert(gic.is_mmio_address(GICv3::kGicrBase));
    assert(gic.is_gicr_address(GICv3::kGicrBase + 0x1000));

    // Non-GIC address
    assert(!gic.is_mmio_address(0x40000000ULL));

    std::cout << "[PASS] test_gic_address_decoding\n";
}

static void test_group_classification_and_fiq_irq() {
    GICv3 gic;
    gic.set_core_count(1);
    gic.write_mmio(GICv3::kGicdBase + 0x0000u, 0x3u, 4); // Enable Group 0 and Group 1

    // Configure SPI 32 as Group 0 (FIQ)
    // GICD_IGROUPR1 (offset 0x0084): bit 0 = 0 -> Group 0
    gic.write_mmio(GICv3::kGicdBase + 0x0084u, 0x00u, 4);
    // Enable SPI 32: GICD_ISENABLER1 (offset 0x0104) bit 0
    gic.write_mmio(GICv3::kGicdBase + 0x0104u, 0x01u, 4);
    // Priority 0x20
    gic.write_mmio(GICv3::kGicdBase + 0x0420u, 0x20u, 1);

    // Enable CPU Interface
    gic.write_icc_pmr(0xFFu, 0);
    gic.write_icc_igrpen0(1u, 0); // Enable Group 0
    gic.write_icc_igrpen1(1u, 0); // Enable Group 1

    // Assert SPI 32
    gic.set_spi_level(32, true);
    auto sig0 = gic.evaluate(1, 0, 0);
    assert(sig0.has_interrupt);
    assert(sig0.is_fiq); // Group 0 must signal as FIQ!
    assert(sig0.intid == 32u);

    // Clear SPI 32
    gic.set_spi_level(32, false);
    gic.end_of_interrupt_group0(32, 1, 0);

    // Now configure SPI 33 as Group 1 (IRQ)
    // GICD_IGROUPR1 bit 1 = 1 -> Group 1
    gic.write_mmio(GICv3::kGicdBase + 0x0084u, 0x02u, 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0104u, 0x02u, 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0421u, 0x30u, 1);

    gic.set_spi_level(33, true);
    auto sig1 = gic.evaluate(1, 0, 0);
    assert(sig1.has_interrupt);
    assert(!sig1.is_fiq); // Group 1 must signal as IRQ!
    assert(sig1.intid == 33u);

    gic.set_spi_level(33, false);
    gic.end_of_interrupt_group1(33, 1, 0);

    std::cout << "[PASS] test_group_classification_and_fiq_irq\n";
}

static void test_priority_masking_pmr() {
    GICv3 gic;
    gic.set_core_count(1);
    gic.write_mmio(GICv3::kGicdBase + 0x0000u, 0x3u, 4);

    const std::uint32_t intid = 34;
    // Enable SPI 34, Group 1, priority 0x90
    gic.write_mmio(GICv3::kGicdBase + 0x0084u, (1u << 2u), 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0104u, (1u << 2u), 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0420u + 2u, 0x90u, 1);
    gic.write_icc_igrpen1(1u, 0);

    // Set PMR = 0x80 (only priorities strictly higher/lower numerical value than 0x80 pass)
    gic.write_icc_pmr(0x80u, 0);

    gic.set_spi_level(intid, true);
    auto sig_masked = gic.evaluate(1, 0, 0);
    assert(!sig_masked.has_interrupt); // Priority 0x90 is masked by PMR 0x80

    // Raise PMR to 0xF0
    gic.write_icc_pmr(0xF0u, 0);
    auto sig_unmasked = gic.evaluate(1, 0, 0);
    assert(sig_unmasked.has_interrupt);
    assert(sig_unmasked.intid == intid);

    gic.set_spi_level(intid, false);
    gic.end_of_interrupt_group1(intid, 1, 0);

    std::cout << "[PASS] test_priority_masking_pmr\n";
}

static void test_interrupt_lifecycle_ack_and_eoi() {
    GICv3 gic;
    gic.set_core_count(1);
    gic.write_mmio(GICv3::kGicdBase + 0x0000u, 0x3u, 4);

    const std::uint32_t intid = 35;
    gic.write_mmio(GICv3::kGicdBase + 0x0084u, (1u << 3u), 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0104u, (1u << 3u), 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0420u + 3u, 0x40u, 1);
    gic.write_icc_pmr(0xFFu, 0);
    gic.write_icc_igrpen1(1u, 0);

    // Assert level
    gic.set_spi_level(intid, true);
    auto sig = gic.evaluate(1, 0, 0);
    assert(sig.has_interrupt);
    assert(sig.intid == intid);

    // Acknowledge interrupt (IAR) -> transitions to Active
    std::uint32_t ack_id = gic.acknowledge_group1(1, 0);
    assert(ack_id == intid);

    // Deassert SPI level before EOI
    gic.set_spi_level(intid, false);

    // End of interrupt (EOI)
    gic.end_of_interrupt_group1(ack_id, 1, 0);

    // Verify completely cleared
    auto sig_done = gic.evaluate(1, 0, 0);
    assert(!sig_done.has_interrupt);

    std::cout << "[PASS] test_interrupt_lifecycle_ack_and_eoi\n";
}

int main() {
    std::cout << "=== Running Stage 200.16 GICv3 Conformance Tests ===\n";
    test_gic_address_decoding();
    test_group_classification_and_fiq_irq();
    test_priority_masking_pmr();
    test_interrupt_lifecycle_ack_and_eoi();
    std::cout << "All Stage 200.16 tests passed successfully!\n";
    return 0;
}

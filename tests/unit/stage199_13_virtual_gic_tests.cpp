// ============================================================================
// Stage 199.13: Virtual Interrupt Controller Architecture Tests
// ============================================================================

#include <cstdint>
#include <iostream>
#include <cassert>

#include "cpu/gicv3.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

using namespace winios;

// 1. Validate Distributor and Redistributor core enumeration and memory mapping
static bool test_gic_multicore_layout_and_mmio() {
    GICv3 gic;
    gic.reset();
    gic.set_core_count(4);

    TEST_ASSERT(gic.get_core_count() == 4, "GIC core count must be 4");

    // Address verification
    TEST_ASSERT(gic.is_gicd_address(GICv3::kGicdBase), "GICD base must be identified");
    TEST_ASSERT(gic.is_gicd_address(GICv3::kGicdBase + 0xFFF0), "GICD end must be identified");
    TEST_ASSERT(!gic.is_gicd_address(GICv3::kGicdBase + GICv3::kGicdSize), "Beyond GICD must return false");

    for (std::size_t c = 0; c < 4; ++c) {
        std::uint64_t r_base = GICv3::kGicrBase + c * GICv3::kGicrCoreStride;
        TEST_ASSERT(gic.is_gicr_address(r_base), "GICR base for core must be identified");
        TEST_ASSERT(gic.is_gicr_address(r_base + GICv3::kGicrCoreStride - 1), "GICR end for core must be identified");
    }

    // MMIO read GICD_TYPER
    std::uint64_t typer = gic.read_mmio(GICv3::kGicdBase + 0x0004u, 4);
    TEST_ASSERT((typer & 0x1Fu) >= 3, "GICD_TYPER ITLinesNumber must cover at least 128 INTIDs");

    std::cout << "[PASS] test_gic_multicore_layout_and_mmio\n";
    return true;
}

// 2. Validate Group 0 (FIQ) vs Group 1 (IRQ) signaling and priority masking
static bool test_gic_group_classification_and_pmr() {
    GICv3 gic;
    gic.reset();
    gic.set_core_count(2);

    // Enable Group 0 and Group 1 in Distributor
    gic.write_mmio(GICv3::kGicdBase + 0x0000u, 0x3u, 4); // GICD_CTLR

    // Setup SPI 32 as Group 0 (FIQ), SPI 33 as Group 1 (IRQ)
    // GICD_IGROUPR1 (offset 0x0084): bit 0=0 (Grp0), bit 1=1 (Grp1)
    gic.write_mmio(GICv3::kGicdBase + 0x0084u, 0x2u, 4);

    // Enable both SPIs in GICD_ISENABLER1 (offset 0x0104)
    gic.write_mmio(GICv3::kGicdBase + 0x0104u, 0x3u, 4);

    // Set priority for SPI 32 = 0x40, SPI 33 = 0x50
    gic.write_mmio(GICv3::kGicdBase + 0x0420u, 0x40u, 1);
    gic.write_mmio(GICv3::kGicdBase + 0x0421u, 0x50u, 1);

    // Target both to Core 0 (default router)
    // Enable Group 0 and Group 1 in Core 0 CPU interface
    gic.write_icc_igrpen0(1, 0);
    gic.write_icc_igrpen1(1, 0);
    gic.write_icc_pmr(0xFFu, 0); // Unmask all

    // Assert SPI 32 (Group 0)
    gic.set_spi_level(32, true);
    auto sig0 = gic.evaluate(1, 0, 0);
    TEST_ASSERT(sig0.has_interrupt, "SPI 32 must signal interrupt");
    TEST_ASSERT(sig0.is_fiq, "SPI 32 in Group 0 must signal as FIQ");
    TEST_ASSERT(sig0.intid == 32u, "SPI 32 INTID mismatch");

    // Ack Group 0
    std::uint32_t ack0 = gic.acknowledge_group0(1, 0);
    TEST_ASSERT(ack0 == 32u, "Acknowledge Group 0 must return 32");

    // Deassert SPI 32 and EOI
    gic.set_spi_level(32, false);
    gic.end_of_interrupt_group0(32, 1, 0);

    // Now assert SPI 33 (Group 1)
    gic.set_spi_level(33, true);
    auto sig1 = gic.evaluate(1, 0, 0);
    TEST_ASSERT(sig1.has_interrupt, "SPI 33 must signal interrupt");
    TEST_ASSERT(!sig1.is_fiq, "SPI 33 in Group 1 must signal as IRQ");
    TEST_ASSERT(sig1.intid == 33u, "SPI 33 INTID mismatch");

    // Mask with PMR (set PMR lower/equal to SPI 33 priority 0x50)
    gic.write_icc_pmr(0x50u, 0);
    auto sig1_masked = gic.evaluate(1, 0, 0);
    TEST_ASSERT(!sig1_masked.has_interrupt, "SPI 33 with priority 0x50 must be masked by PMR 0x50");

    // Unmask with PMR 0x60
    gic.write_icc_pmr(0x60u, 0);
    auto sig1_unmasked = gic.evaluate(1, 0, 0);
    TEST_ASSERT(sig1_unmasked.has_interrupt, "SPI 33 must be unmasked with PMR 0x60");

    // Ack Group 1
    std::uint32_t ack1 = gic.acknowledge_group1(1, 0);
    TEST_ASSERT(ack1 == 33u, "Acknowledge Group 1 must return 33");
    gic.set_spi_level(33, false);
    gic.end_of_interrupt_group1(33, 1, 0);

    std::cout << "[PASS] test_gic_group_classification_and_pmr\n";
    return true;
}

// 3. Validate Virtual Interrupt injection via ICH List Registers and EL2 hypervisor controls
static bool test_gic_virtual_interrupt_injection() {
    GICv3 gic;
    gic.reset();
    gic.set_core_count(1);

    // Enable virtual CPU interface via ICH_HCR_EL2
    gic.write_ich_hcr(0x1u, 0); // En bit = 1
    TEST_ASSERT((gic.read_ich_hcr(0) & 0x1u) != 0, "ICH_HCR_EL2 En bit must be set");

    // Configure ICH_VMCR_EL2: VENG1 = bit 1, VENG0 = bit 0, VPMR = 0xFF in bits [31:24]
    std::uint64_t vmcr = (0xFFULL << 24u) | 0x3ULL;
    gic.write_ich_vmcr(vmcr, 0);
    TEST_ASSERT(gic.read_ich_vmcr(0) == vmcr, "ICH_VMCR_EL2 mismatch");

    // Inject virtual interrupt into ICH_LR0_EL2:
    // Bits [31:0] = vINTID (e.g. 42)
    // Bits [47:32] = pINTID (unused)
    // Bits [55:48] = Priority (e.g. 0x20)
    // Bits [61:60] = Group (1 = Group 1)
    // Bits [63:62] = State (01 = Pending) -> bit 62 = 1
    std::uint64_t lr_val = (1ULL << 62u) | (1ULL << 60u) | (0x20ULL << 48u) | 42ULL;
    gic.write_ich_lr(0, lr_val, 0);
    TEST_ASSERT(gic.read_ich_lr(0, 0) == lr_val, "ICH_LR0_EL2 mismatch");

    // Evaluate at EL1: should receive virtual interrupt
    auto sig = gic.evaluate(1, 0, 0);
    TEST_ASSERT(sig.has_interrupt, "Virtual interrupt must be signaled");
    TEST_ASSERT(sig.is_virtual, "Signal must be marked as virtual");
    TEST_ASSERT(sig.lr_index == 0, "Signal must indicate LR index 0");
    TEST_ASSERT(sig.intid == 42u, "Signal vINTID must be 42");
    TEST_ASSERT(sig.priority == 0x20u, "Signal priority must be 0x20");
    TEST_ASSERT(!sig.is_fiq, "Group 1 virtual interrupt must be IRQ");

    // If evaluating at EL2: hypervisor does not receive virtual interrupts directly
    auto sig_el2 = gic.evaluate(2, 0, 0);
    TEST_ASSERT(!sig_el2.has_interrupt, "EL2 must not receive guest virtual interrupt");

    // Virtual priority masking: lower VPMR below 0x20 -> e.g. 0x10
    std::uint64_t vmcr_masked = (0x10ULL << 24u) | 0x3ULL;
    gic.write_ich_vmcr(vmcr_masked, 0);
    auto sig_vmasked = gic.evaluate(1, 0, 0);
    TEST_ASSERT(!sig_vmasked.has_interrupt, "Virtual interrupt must be masked when VPMR is 0x10");

    std::cout << "[PASS] test_gic_virtual_interrupt_injection\n";
    return true;
}

// 4. Validate SGI generation and multi-core target delivery
static bool test_gic_sgi_dispatch() {
    GICv3 gic;
    gic.reset();
    gic.set_core_count(4);

    // Enable Group 1 in Distributor
    gic.write_mmio(GICv3::kGicdBase + 0x0000u, 0x2u, 4);

    // Enable Group 1 in Redistributors for Core 1, 2, 3
    for (std::size_t c = 0; c < 4; ++c) {
        gic.write_icc_igrpen1(1, c);
        gic.write_icc_pmr(0xFFu, c);
        // Enable SGI 5 in Redistributor ISENABLER0
        auto& rd = gic.get_redistributor(c);
        rd.isenabler0 |= (1u << 5u);
        rd.igroupr0 |= (1u << 5u); // Group 1
    }

    // Send SGI 5 from Core 0 targeting Core 1 and Core 3
    // ICC_SGI1R_EL1 format:
    // INTID in [27:24] = 5
    // TargetList in [15:0] = (1 << 1) | (1 << 3) = 0x000A
    std::uint64_t sgi1r = (5ULL << 24u) | 0x000AULL;
    gic.send_sgi(0, sgi1r);

    // Verify Core 0 did NOT receive SGI
    auto sig0 = gic.evaluate(1, 0, 0);
    TEST_ASSERT(!sig0.has_interrupt, "Core 0 must not receive SGI targeted to 1 and 3");

    // Verify Core 1 received SGI 5
    auto sig1 = gic.evaluate(1, 0, 1);
    TEST_ASSERT(sig1.has_interrupt && sig1.intid == 5u, "Core 1 must receive SGI 5");

    // Verify Core 2 did NOT receive SGI
    auto sig2 = gic.evaluate(1, 0, 2);
    TEST_ASSERT(!sig2.has_interrupt, "Core 2 must not receive SGI");

    // Verify Core 3 received SGI 5
    auto sig3 = gic.evaluate(1, 0, 3);
    TEST_ASSERT(sig3.has_interrupt && sig3.intid == 5u, "Core 3 must receive SGI 5");

    std::cout << "[PASS] test_gic_sgi_dispatch\n";
    return true;
}

int main() {
    std::cout << "--- Stage 199.13: Virtual Interrupt Controller Architecture Tests ---\n";
    if (!test_gic_multicore_layout_and_mmio()) return 1;
    if (!test_gic_group_classification_and_pmr()) return 1;
    if (!test_gic_virtual_interrupt_injection()) return 1;
    if (!test_gic_sgi_dispatch()) return 1;

    std::cout << "All Stage 199.13 tests passed successfully!\n";
    return 0;
}

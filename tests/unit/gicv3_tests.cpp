#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/smp_cluster.h"
#include "memory/memory.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__  \
                      << ": " #cond << std::endl;                              \
            std::exit(1);                                                      \
        }                                                                      \
    } while (false)

static void test_gicd_identification_and_registers() {
    winios::GICv3 gic;

    // GICD_PIDR2 at 0x0800FFE8: ArchRev in [7:4] must be 0x3 (GICv3)
    std::uint32_t pidr2 = static_cast<std::uint32_t>(gic.read_mmio(winios::GICv3::kGicdBase + 0xFFE8, 4));
    CHECK(((pidr2 >> 4u) & 0xFu) == 0x3u);

    // GICD_CTLR at 0x08000000: Default 0x50 (ARE_NS | DS)
    std::uint32_t ctlr = static_cast<std::uint32_t>(gic.read_mmio(winios::GICv3::kGicdBase + 0x0000, 4));
    CHECK((ctlr & 0x50u) == 0x50u);

    // Write EnableGrp0 and EnableGrp1NS
    gic.write_mmio(winios::GICv3::kGicdBase + 0x0000, 0x3u, 4);
    ctlr = static_cast<std::uint32_t>(gic.read_mmio(winios::GICv3::kGicdBase + 0x0000, 4));
    CHECK((ctlr & 0x3u) == 0x3u);

    // GICD_TYPER at 0x08000004
    std::uint32_t typer = static_cast<std::uint32_t>(gic.read_mmio(winios::GICv3::kGicdBase + 0x0004, 4));
    CHECK((typer & 0x1Fu) == 3u); // ITLinesNumber = 3 (128 INTIDs)

    // GICD_IGROUPR and GICD_IGRPMODR for SPIs (INTID 32..63, index 1)
    gic.write_mmio(winios::GICv3::kGicdBase + 0x0084, 0xAAAAAAAAu, 4);
    CHECK(gic.read_mmio(winios::GICv3::kGicdBase + 0x0084, 4) == 0xAAAAAAAAu);

    gic.write_mmio(winios::GICv3::kGicdBase + 0x0D04, 0x55555555u, 4);
    CHECK(gic.read_mmio(winios::GICv3::kGicdBase + 0x0D04, 4) == 0x55555555u);

    // GICD_IROUTER for SPI 32 (offset 0x6100)
    std::uint64_t router_val = 0x80000000ULL | 0x2ULL; // IRM=1, Aff0=2
    gic.write_mmio(winios::GICv3::kGicdBase + 0x6100, router_val, 8);
    CHECK(gic.read_mmio(winios::GICv3::kGicdBase + 0x6100, 8) == router_val);
}

static void test_gicr_identification_and_registers() {
    winios::GICv3 gic;
    gic.set_core_count(2);

    // Core 0 RD_base PIDR2 at 0x080AFFE8
    std::uint32_t pidr2_c0 = static_cast<std::uint32_t>(gic.read_mmio(0x080A0000 + 0xFFE8, 4));
    CHECK(((pidr2_c0 >> 4u) & 0xFu) == 0x3u);

    // Core 1 RD_base PIDR2 at 0x080CFFE8
    std::uint32_t pidr2_c1 = static_cast<std::uint32_t>(gic.read_mmio(0x080C0000 + 0xFFE8, 4));
    CHECK(((pidr2_c1 >> 4u) & 0xFu) == 0x3u);

    // Core 0 GICR_TYPER (0x080A0008): Last = 0, Aff0 = 0
    std::uint64_t typer0 = gic.read_mmio(0x080A0008, 8);
    CHECK((typer0 & (1ULL << 4)) == 0);
    CHECK(((typer0 >> 32) & 0xFF) == 0);

    // Core 1 GICR_TYPER (0x080C0008): Last = 1, Aff0 = 1
    std::uint64_t typer1 = gic.read_mmio(0x080C0008, 8);
    CHECK((typer1 & (1ULL << 4)) != 0);
    CHECK(((typer1 >> 32) & 0xFF) == 1);

    // SGI frame for Core 0 (0x080B0000): Set enable for INTID 10
    gic.write_mmio(0x080B0100, 1u << 10, 4);
    CHECK((gic.read_mmio(0x080B0100, 4) & (1u << 10)) != 0);
    // Core 1 SGI frame remains 0
    CHECK((gic.read_mmio(0x080D0100, 4) & (1u << 10)) == 0);

    // Clear enable
    gic.write_mmio(0x080B0180, 1u << 10, 4);
    CHECK((gic.read_mmio(0x080B0100, 4) & (1u << 10)) == 0);

    // GICR_IGRPMODR0 (0x080B0D00)
    gic.write_mmio(0x080B0D00, 0x12345678u, 4);
    CHECK(gic.read_mmio(0x080B0D00, 4) == 0x12345678u);
}

static void test_interrupt_four_state_lifecycle() {
    winios::GICv3 gic;
    gic.write_mmio(winios::GICv3::kGicdBase + 0x0000, 0x3u, 4); // Enable Grp0 and Grp1
    gic.write_icc_sre_el1(1, 0);
    gic.write_icc_igrpen1(1, 0);
    gic.write_icc_pmr(0xF0, 0);

    // INTID 5 (SGI 5): Configure Group 1, Priority 0x20
    gic.write_mmio(0x080B0080, 1u << 5, 4); // Group 1
    gic.write_mmio(0x080B0400 + 5, 0x20, 1); // Priority 0x20
    gic.write_mmio(0x080B0100, 1u << 5, 4); // Enable

    // State 1: Inactive -> Pending
    gic.write_mmio(0x080B0200, 1u << 5, 4); // Set pending
    CHECK((gic.read_mmio(0x080B0200, 4) & (1u << 5)) != 0); // Pending is set
    CHECK((gic.read_mmio(0x080B0300, 4) & (1u << 5)) == 0); // Active is clear

    auto sig = gic.evaluate(1, 0, 0);
    CHECK(sig.has_interrupt && sig.intid == 5u);

    // State 2: Pending -> Active (acknowledge)
    std::uint32_t iar = gic.acknowledge_group1(1, 0);
    CHECK(iar == 5u);
    CHECK((gic.read_mmio(0x080B0300, 4) & (1u << 5)) != 0); // Active is set
    CHECK((gic.read_mmio(0x080B0200, 4) & (1u << 5)) == 0); // Pending is cleared

    // State 3: Active -> Active & Pending (re-assert while active)
    gic.write_mmio(0x080B0200, 1u << 5, 4); // Set pending again
    CHECK((gic.read_mmio(0x080B0300, 4) & (1u << 5)) != 0); // Active is set
    CHECK((gic.read_mmio(0x080B0200, 4) & (1u << 5)) != 0); // Pending is set

    // State 4: Active & Pending -> Pending on EOI
    gic.end_of_interrupt_group1(5, 1, 0);
    CHECK((gic.read_mmio(0x080B0300, 4) & (1u << 5)) == 0); // Active is clear
    CHECK((gic.read_mmio(0x080B0200, 4) & (1u << 5)) != 0); // Pending remains set!

    // Finally acknowledge and EOI to transition Pending -> Active -> Inactive
    iar = gic.acknowledge_group1(1, 0);
    CHECK(iar == 5u);
    gic.end_of_interrupt_group1(5, 1, 0);
    CHECK((gic.read_mmio(0x080B0300, 4) & (1u << 5)) == 0); // Active is clear
    CHECK((gic.read_mmio(0x080B0200, 4) & (1u << 5)) == 0); // Pending is clear
}

static void test_priority_masking_and_binary_point_preemption() {
    winios::GICv3 gic;
    gic.write_mmio(winios::GICv3::kGicdBase + 0x0000, 0x3u, 4);
    gic.write_icc_sre_el1(1, 0);
    gic.write_icc_igrpen1(1, 0);

    // PMR = 0x80: Mask interrupts with priority >= 0x80
    gic.write_icc_pmr(0x80, 0);
    CHECK(gic.read_icc_pmr(0) == 0x80);

    // Setup INTID 3 (Priority 0x90 - masked) and INTID 4 (Priority 0x70 - unmasked)
    gic.write_mmio(0x080B0080, (1u << 3) | (1u << 4), 4); // Group 1
    gic.write_mmio(0x080B0400 + 3, 0x90, 1);
    gic.write_mmio(0x080B0400 + 4, 0x70, 1);
    gic.write_mmio(0x080B0100, (1u << 3) | (1u << 4), 4); // Enable both

    // Assert masked INTID 3
    gic.write_mmio(0x080B0200, 1u << 3, 4);
    auto sig = gic.evaluate(1, 0, 0);
    CHECK(!sig.has_interrupt); // Masked by PMR!

    // Assert unmasked INTID 4
    gic.write_mmio(0x080B0200, 1u << 4, 4);
    sig = gic.evaluate(1, 0, 0);
    CHECK(sig.has_interrupt && sig.intid == 4u);

    // Acknowledge INTID 4: Running priority becomes 0x70
    std::uint32_t iar = gic.acknowledge_group1(1, 0);
    CHECK(iar == 4u);
    CHECK((gic.read_icc_rpr(0) & 0xF0) == 0x70); // RPR reflects running priority

    // Now test nested preemption: INTID 6 with Priority 0x30 (strictly higher priority)
    gic.write_mmio(0x080B0080, 1u << 6, 4);
    gic.write_mmio(0x080B0400 + 6, 0x30, 1);
    gic.write_mmio(0x080B0100, 1u << 6, 4);
    gic.write_mmio(0x080B0200, 1u << 6, 4);

    sig = gic.evaluate(1, 0, 0);
    CHECK(sig.has_interrupt && sig.intid == 6u); // Preemption allowed!

    // Acknowledge nested preemption
    iar = gic.acknowledge_group1(1, 0);
    CHECK(iar == 6u);
    CHECK((gic.read_icc_rpr(0) & 0xF0) == 0x30); // RPR now 0x30

    // EOI INTID 6: Running priority drops back to 0x70
    gic.end_of_interrupt_group1(6, 1, 0);
    CHECK((gic.read_icc_rpr(0) & 0xF0) == 0x70);

    // EOI INTID 4: Running priority drops back to idle 0xFF
    gic.end_of_interrupt_group1(4, 1, 0);
    CHECK(gic.read_icc_rpr(0) == 0xFF);
}

static void test_eoi_mode_and_deactivation() {
    winios::GICv3 gic;
    gic.write_mmio(winios::GICv3::kGicdBase + 0x0000, 0x3u, 4);
    gic.write_icc_sre_el1(1, 0);
    gic.write_icc_igrpen1(1, 0);
    gic.write_icc_pmr(0xF0, 0);

    // Set EOImode = 1 (bit 1 of ICC_CTLR_EL1): EOIR drops priority only, DIR deactivates
    gic.write_icc_ctlr(0x2u, 0);
    CHECK(gic.read_icc_ctlr(0) == 0x2u);

    // Setup INTID 7 (Priority 0x40)
    gic.write_mmio(0x080B0080, 1u << 7, 4);
    gic.write_mmio(0x080B0400 + 7, 0x40, 1);
    gic.write_mmio(0x080B0100, 1u << 7, 4);
    gic.write_mmio(0x080B0200, 1u << 7, 4);

    std::uint32_t iar = gic.acknowledge_group1(1, 0);
    CHECK(iar == 7u);
    CHECK((gic.read_mmio(0x080B0300, 4) & (1u << 7)) != 0); // Active bit set

    // Call EOIR: Priority drops (RPR returns to 0xFF), but Active bit REMAINS set!
    gic.end_of_interrupt_group1(7, 1, 0);
    CHECK(gic.read_icc_rpr(0) == 0xFF); // Priority dropped
    CHECK((gic.read_mmio(0x080B0300, 4) & (1u << 7)) != 0); // Still active!

    // Call DIR: Now active bit is cleared
    gic.write_icc_dir(7, 1, 0);
    CHECK((gic.read_mmio(0x080B0300, 4) & (1u << 7)) == 0); // Deactivated!
}

static void test_group0_fiq_and_group1_irq() {
    winios::GICv3 gic;
    gic.write_mmio(winios::GICv3::kGicdBase + 0x0000, 0x3u, 4); // Enable Grp0 and Grp1
    gic.write_icc_sre_el1(1, 0);
    gic.write_icc_igrpen0(1, 0);
    gic.write_icc_igrpen1(1, 0);
    gic.write_icc_pmr(0xF0, 0);

    // Configure INTID 1 as Group 0 (FIQ) and INTID 2 as Group 1 (IRQ)
    gic.write_mmio(0x080B0080, 1u << 2, 4); // INTID 2 in Group 1, INTID 1 in Group 0
    gic.write_mmio(0x080B0400 + 1, 0x30, 1);
    gic.write_mmio(0x080B0400 + 2, 0x30, 1);
    gic.write_mmio(0x080B0100, (1u << 1) | (1u << 2), 4); // Enable both

    // Assert Group 0 (INTID 1)
    gic.write_mmio(0x080B0200, 1u << 1, 4);
    auto sig = gic.evaluate(1, 0, 0);
    CHECK(sig.has_interrupt && sig.is_fiq && sig.intid == 1u);

    // Group 0 acknowledge via ICC_IAR0_EL1
    std::uint32_t iar0 = gic.acknowledge_group0(1, 0);
    CHECK(iar0 == 1u);
    gic.end_of_interrupt_group0(1, 1, 0);

    // Assert Group 1 (INTID 2)
    gic.write_mmio(0x080B0200, 1u << 2, 4);
    sig = gic.evaluate(1, 0, 0);
    CHECK(sig.has_interrupt && !sig.is_fiq && sig.intid == 2u);

    // Group 1 acknowledge via ICC_IAR1_EL1
    std::uint32_t iar1 = gic.acknowledge_group1(1, 0);
    CHECK(iar1 == 2u);
    gic.end_of_interrupt_group1(2, 1, 0);

    // When none pending, IAR returns Spurious (1023)
    CHECK(gic.acknowledge_group0(1, 0) == winios::GICv3::kSpuriousIntId);
    CHECK(gic.acknowledge_group1(1, 0) == winios::GICv3::kSpuriousIntId);
}

static void test_sgi_generation_and_routing() {
    winios::GICv3 gic;
    gic.set_core_count(4);
    gic.write_mmio(winios::GICv3::kGicdBase + 0x0000, 0x3u, 4);

    for (std::size_t c = 0; c < 4; ++c) {
        gic.write_icc_sre_el1(1, c);
        gic.write_icc_igrpen1(1, c);
        gic.write_icc_pmr(0xF0, c);
        // Set INTID 8 in Group 1, Priority 0x20, Enabled for all cores
        const std::uint64_t base = 0x080B0000 + c * 0x20000;
        gic.write_mmio(base + 0x0080, 1u << 8, 4);
        gic.write_mmio(base + 0x0400 + 8, 0x20, 1);
        gic.write_mmio(base + 0x0100, 1u << 8, 4);
    }

    // Core 0 sends SGI INTID 8 to Core 2 only (TargetList bit 2 = 0x4)
    std::uint64_t sgi_targeted = (8ULL << 24) | 0x4ULL;
    gic.send_sgi(0, sgi_targeted);

    CHECK((gic.read_mmio(0x080B0000 + 0 * 0x20000 + 0x0200, 4) & (1u << 8)) == 0); // Core 0: No
    CHECK((gic.read_mmio(0x080B0000 + 1 * 0x20000 + 0x0200, 4) & (1u << 8)) == 0); // Core 1: No
    CHECK((gic.read_mmio(0x080B0000 + 2 * 0x20000 + 0x0200, 4) & (1u << 8)) != 0); // Core 2: Yes!
    CHECK((gic.read_mmio(0x080B0000 + 3 * 0x20000 + 0x0200, 4) & (1u << 8)) == 0); // Core 3: No

    // Clear Core 2 pending
    gic.write_mmio(0x080B0000 + 2 * 0x20000 + 0x0280, 1u << 8, 4);

    // Broadcast SGI from Core 1 (IRM=1, INTID 8): Cores 0, 2, 3 must receive; Core 1 must not!
    std::uint64_t sgi_bcast = (1ULL << 40) | (8ULL << 24);
    gic.send_sgi(1, sgi_bcast);

    CHECK((gic.read_mmio(0x080B0000 + 0 * 0x20000 + 0x0200, 4) & (1u << 8)) != 0); // Core 0: Yes!
    CHECK((gic.read_mmio(0x080B0000 + 1 * 0x20000 + 0x0200, 4) & (1u << 8)) == 0); // Core 1: No (sender)
    CHECK((gic.read_mmio(0x080B0000 + 2 * 0x20000 + 0x0200, 4) & (1u << 8)) != 0); // Core 2: Yes!
    CHECK((gic.read_mmio(0x080B0000 + 3 * 0x20000 + 0x0200, 4) & (1u << 8)) != 0); // Core 3: Yes!
}

static void test_ppi_and_spi_routing() {
    winios::GICv3 gic;
    gic.set_core_count(2);
    gic.write_mmio(winios::GICv3::kGicdBase + 0x0000, 0x3u, 4);

    for (std::size_t c = 0; c < 2; ++c) {
        gic.write_icc_sre_el1(1, c);
        gic.write_icc_igrpen1(1, c);
        gic.write_icc_pmr(0xF0, c);
    }

    // 1. Timer PPI (INTID 27 = CNTV) setup on Core 0
    gic.write_mmio(0x080B0080, 1u << 27, 4);
    gic.write_mmio(0x080B0400 + 27, 0x30, 1);
    gic.write_mmio(0x080B0100, 1u << 27, 4);

    // Level assertion on Core 0 CNTV PPI
    gic.set_ppi_level(27, true, 0);
    auto sig0 = gic.evaluate(1, 0, 0);
    CHECK(sig0.has_interrupt && sig0.intid == 27u);

    // Core 1 CNTV PPI must NOT be signaled
    auto sig1 = gic.evaluate(1, 0, 1);
    CHECK(!sig1.has_interrupt);

    // Clear PPI level
    gic.set_ppi_level(27, false, 0);

    // 2. SPI routing: SPI 40 (INTID 40) routed specifically to Core 1 via GICD_IROUTER
    // Offset for SPI 40 is 0x6100 + (40 - 32) * 8 = 0x6140
    gic.write_mmio(winios::GICv3::kGicdBase + 0x6140, 0x1ULL, 8); // Route to Aff0 = 1 (Core 1)
    gic.write_mmio(winios::GICv3::kGicdBase + 0x0084, 1u << (40 - 32), 4); // Group 1
    gic.write_mmio(winios::GICv3::kGicdBase + 0x0400 + 40, 0x30, 1); // Priority 0x30
    gic.write_mmio(winios::GICv3::kGicdBase + 0x0104, 1u << (40 - 32), 4); // Enable

    // Assert SPI 40
    gic.set_spi_level(40, true);

    // Core 0 must NOT receive SPI 40
    sig0 = gic.evaluate(1, 0, 0);
    CHECK(!sig0.has_interrupt);

    // Core 1 MUST receive SPI 40
    sig1 = gic.evaluate(1, 0, 1);
    CHECK(sig1.has_interrupt && sig1.intid == 40u);

    // Clear SPI level
    gic.set_spi_level(40, false);
}

static void test_virtual_interrupts_and_maintenance() {
    winios::GICv3 gic;
    gic.set_core_count(1);

    // Configure EL2 ICH registers
    // ICH_HCR_EL2: Enable virtual CPU interface (bit 0 = 1), UIE = 1 (bit 1)
    gic.write_ich_hcr(0x3u, 0);
    // ICH_VMCR_EL2: Enable vGrp1 (bit 1 = 1), VPMR = 0xF0 (bits [31:24])
    gic.write_ich_vmcr((0xF0ULL << 24) | 0x2ULL, 0);

    // Inject virtual interrupt INTID 100 into ICH_LR0_EL2
    // State = Pending (0b01 in [63:62]) -> (1ULL << 62)
    // Group 1 -> (1ULL << 60)
    // Priority = 0x20 in [55:48] -> (0x20ULL << 48)
    // EOI maintenance bit -> (1ULL << 41)
    // vINTID = 100 in [31:0]
    std::uint64_t lr0 = (1ULL << 62) | (1ULL << 60) | (0x20ULL << 48) | (1ULL << 41) | 100ULL;
    gic.write_ich_lr(0, lr0, 0);

    // When executing at EL1, virtual interrupt is evaluated
    auto sig = gic.evaluate(1, 0, 0);
    CHECK(sig.has_interrupt && sig.is_virtual && sig.intid == 100u);

    // Acknowledge virtual interrupt at EL1
    std::uint32_t iar = gic.acknowledge_group1(1, 0);
    CHECK(iar == 100u);

    // LR0 state should now be Active (0b10)
    lr0 = gic.read_ich_lr(0, 0);
    CHECK(((lr0 >> 62u) & 0x3u) == 0x2u);

    // EOI virtual interrupt at EL1
    gic.end_of_interrupt_group1(100, 1, 0);

    // LR0 state should now be Inactive (0b00)
    lr0 = gic.read_ich_lr(0, 0);
    CHECK(((lr0 >> 62u) & 0x3u) == 0x0u);

    // Maintenance interrupt checks:
    // 1. EISR: Bit 0 must be 1 (LR0 completed with EOI maintenance set)
    CHECK((gic.read_ich_eisr(0) & 0x1u) != 0);
    // 2. ELRSR: Bit 0 must be 1 (LR0 is empty)
    CHECK((gic.read_ich_elrsr(0) & 0x1u) != 0);
    // 3. MISR: Bit 0 (EOI) and Bit 1 (Underflow) must be set
    std::uint32_t misr = gic.read_ich_misr(0);
    CHECK((misr & 0x1u) != 0); // EOI maintenance interrupt
    CHECK((misr & 0x2u) != 0); // Underflow (all LRs empty, UIE=1)
}

static void test_multicore_concurrency_stress() {
    constexpr std::size_t kCores = 4;
    constexpr int kIterations = 1000;
    winios::GICv3 gic;
    gic.set_core_count(kCores);
    gic.write_mmio(winios::GICv3::kGicdBase + 0x0000, 0x3u, 4);

    for (std::size_t c = 0; c < kCores; ++c) {
        gic.write_icc_sre_el1(1, c);
        gic.write_icc_igrpen1(1, c);
        gic.write_icc_pmr(0xF0, c);
        const std::uint64_t base = 0x080B0000 + c * 0x20000;
        gic.write_mmio(base + 0x0080, 0xFFFFu, 4); // All SGIs in Group 1
        gic.write_mmio(base + 0x0100, 0xFFFFu, 4); // All SGIs enabled
        for (std::size_t i = 0; i < 16; ++i) {
            gic.write_mmio(base + 0x0400 + i, 0x40, 1);
        }
    }

    std::atomic<bool> start{false};
    std::vector<std::thread> workers;

    for (std::size_t c = 0; c < kCores; ++c) {
        workers.emplace_back([&, c]() {
            while (!start.load(std::memory_order_acquire)) {}
            for (int i = 0; i < kIterations; ++i) {
                // Send SGI to neighbor core
                std::size_t target_core = (c + 1) % kCores;
                std::uint64_t sgi = (static_cast<std::uint64_t>(i % 16) << 24) | (1ULL << target_core);
                gic.send_sgi(c, sgi);

                // Try evaluate and acknowledge on own core
                auto sig = gic.evaluate(1, 0, c);
                if (sig.has_interrupt) {
                    std::uint32_t iar = gic.acknowledge_group1(1, c);
                    if (iar != winios::GICv3::kSpuriousIntId) {
                        gic.end_of_interrupt_group1(iar, 1, c);
                    }
                }
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (auto& w : workers) {
        w.join();
    }
}

int main() {
    std::cout << "[GICv3 Unit Tests] Running Stage 1..11 test cases..." << std::endl;
    test_gicd_identification_and_registers();
    test_gicr_identification_and_registers();
    test_interrupt_four_state_lifecycle();
    test_priority_masking_and_binary_point_preemption();
    test_eoi_mode_and_deactivation();
    test_group0_fiq_and_group1_irq();
    test_sgi_generation_and_routing();
    test_ppi_and_spi_routing();
    test_virtual_interrupts_and_maintenance();
    test_multicore_concurrency_stress();
    std::cout << "[GICv3 Unit Tests] All 10 test suites passed successfully!" << std::endl;
    return 0;
}

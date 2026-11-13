// ============================================================================
// Stage 193.12: GIC Interrupt Delivery Architecture Tests
// ============================================================================

#include <cstdint>
#include <iostream>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "memory/memory.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

using namespace winios;

static bool test_distributor_and_redistributor_configuration() {
    GICv3 gic;
    gic.reset();
    gic.set_core_count(2);

    // 1. Enable Distributor Group 1 (bit 1 of GICD_CTLR)
    gic.write_mmio(GICv3::kGicdBase + 0x0000u, 0x2u, 4);
    const auto ctlr = gic.read_mmio(GICv3::kGicdBase + 0x0000u, 4);
    CHECK((ctlr & 0x2u) != 0, "GICD_CTLR must reflect enable Group 1");

    // 2. Enable SPI INTID 33 via GICD_ISENABLER1 (offset 0x0104)
    // Bit index for INTID 33 is 33 % 32 = 1
    gic.write_mmio(GICv3::kGicdBase + 0x0104u, (1u << 1u), 4);
    const auto isenabler = gic.read_mmio(GICv3::kGicdBase + 0x0104u, 4);
    CHECK((isenabler & (1u << 1u)) != 0, "GICD_ISENABLER1 must show INTID 33 enabled");

    // 3. Set Priority for INTID 33 via GICD_IPRIORITYR8 (offset 0x0420)
    // INTID 33 is byte 1 of word at offset 0x0420
    gic.write_mmio(GICv3::kGicdBase + 0x0420u + 1u, 0x40u, 1);
    const auto prio = gic.read_mmio(GICv3::kGicdBase + 0x0420u + 1u, 1);
    CHECK(prio == 0x40u, "INTID 33 priority must be 0x40");

    // 4. Assert SPI INTID 33
    gic.set_spi_level(33, true);

    // Verify pending state via GICD_ISPENDR1 (offset 0x0204)
    const auto ispendr = gic.read_mmio(GICv3::kGicdBase + 0x0204u, 4);
    CHECK((ispendr & (1u << 1u)) != 0, "GICD_ISPENDR1 must show INTID 33 pending");

    std::cout << "[PASS] test_distributor_and_redistributor_configuration\n";
    return true;
}

static bool test_cpu_interface_priority_masking() {
    GICv3 gic;
    gic.reset();
    gic.set_core_count(1);

    // Enable Group 0 and Group 1 in Distributor and CPU interface
    gic.write_mmio(GICv3::kGicdBase + 0x0000u, 0x3u, 4); // GICD_CTLR.EnableGrp0 | EnableGrp1
    gic.write_mmio(GICv3::kGicdBase + 0x0084u, 0xFFFFFFFFu, 4); // GICD_IGROUPR1: Set SPIs to Group 1
    gic.write_icc_igrpen1(1, 0);
    gic.write_icc_igrpen0(1, 0);

    // Set Priority Mask Register (ICC_PMR_EL1) to 0x80
    // Lower numerical value = higher priority.
    // Interrupts with priority >= PMR are masked!
    gic.write_icc_pmr(0x80u, 0);

    // Enable SPI INTID 34, set priority to 0x90 (lower priority than PMR -> masked)
    gic.write_mmio(GICv3::kGicdBase + 0x0104u, (1u << 2u), 4); // enable INTID 34
    gic.write_mmio(GICv3::kGicdBase + 0x0420u + 2u, 0x90u, 1); // priority 0x90
    gic.set_spi_level(34, true);

    // Evaluate: must be masked
    auto sig_masked = gic.evaluate(1, 0, 0);
    CHECK(!sig_masked.has_interrupt, "Interrupt with priority 0x90 must be masked by PMR 0x80");

    // Lower the numerical priority to 0x60 (higher priority than PMR 0x80)
    gic.write_mmio(GICv3::kGicdBase + 0x0420u + 2u, 0x60u, 1); // priority 0x60

    // Evaluate: must now be signaled
    auto sig_unmasked = gic.evaluate(1, 0, 0);
    CHECK(sig_unmasked.has_interrupt, "Interrupt with priority 0x60 must pass PMR 0x80");
    CHECK(sig_unmasked.intid == 34u, "Signaled INTID must be 34");

    std::cout << "[PASS] test_cpu_interface_priority_masking\n";
    return true;
}

static bool test_interrupt_lifecycle_acknowledge_and_eoi() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x400000ULL);

    GICv3& gic = cpu.get_gic();
    gic.reset();
    gic.set_core_count(1);

    // Enable Group 0 and Group 1 in Distributor and CPU interface
    gic.write_mmio(GICv3::kGicdBase + 0x0000u, 0x3u, 4); // GICD_CTLR.EnableGrp0 | EnableGrp1
    gic.write_mmio(GICv3::kGicdBase + 0x0084u, 0xFFFFFFFFu, 4); // GICD_IGROUPR1: Set SPIs to Group 1
    gic.write_icc_igrpen1(1, 0);
    gic.write_icc_igrpen0(1, 0);
    gic.write_icc_pmr(0xF0u, 0);

    // Enable SPI 35, Priority 0x20
    gic.write_mmio(GICv3::kGicdBase + 0x0104u, (1u << 3u), 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0420u + 3u, 0x20u, 1);
    gic.set_spi_level(35, true);

    // Verify interrupt is pending
    auto sig = gic.evaluate(1, 0, 0);
    CHECK(sig.has_interrupt, "SPI 35 must be pending");
    CHECK(sig.intid == 35u, "Pending INTID must be 35");

    // Guest EL1 code reads ICC_IAR1_EL1 (MRS X0, ICC_IAR1_EL1: 0xD530CC00u)
    const std::uint64_t code_va = 0x1000ULL;
    mem.write_u32(static_cast<std::size_t>(code_va), 0xD530CC00u); // MRS X0, ICC_IAR1_EL1
    // Guest EL1 code writes ICC_EOIR1_EL1 (MSR ICC_EOIR1_EL1, X0: 0xD510CC20u)
    mem.write_u32(static_cast<std::size_t>(code_va + 4u), 0xD510CC20u); // MSR ICC_EOIR1_EL1, X0

    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_daif(0xFu); // In interrupt handler: DAIF masked
    cpu.set_program_counter(code_va);
    cpu.write_register(0, 0);

    // Step 1: MRS X0, ICC_IAR1_EL1 (Acknowledge)
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == code_va + 4u, "PC advanced past MRS");
    CHECK(cpu.read_register(0) == 35u, "X0 must hold acknowledged INTID 35");

    // Second acknowledge before EOI must return spurious (1023) because interrupt is active
    const auto ack_again = gic.acknowledge_group1(1, 0);
    CHECK(ack_again == GICv3::kSpuriousIntId, "Second acknowledge must return spurious INTID (1023)");

    // Deassert the SPI line before EOI so it doesn't re-pend
    gic.set_spi_level(35, false);

    // Step 2: MSR ICC_EOIR1_EL1, X0 (End of Interrupt)
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == code_va + 8u, "PC advanced past MSR");

    // After EOI, no interrupt should be pending or active
    auto sig_after = gic.evaluate(1, 0, 0);
    CHECK(!sig_after.has_interrupt, "No interrupt should be pending after EOI");

    std::cout << "[PASS] test_interrupt_lifecycle_acknowledge_and_eoi\n";
    return true;
}

static bool test_per_core_interrupt_isolation() {
    GICv3 gic;
    gic.reset();
    gic.set_core_count(2);

    // Enable Group 0 and Group 1 in Distributor and CPU interfaces on Core 0 and Core 1
    gic.write_mmio(GICv3::kGicdBase + 0x0000u, 0x3u, 4); // GICD_CTLR.EnableGrp0 | EnableGrp1
    gic.write_icc_igrpen1(1, 0);
    gic.write_icc_pmr(0xF0u, 0);
    gic.write_icc_igrpen1(1, 1);
    gic.write_icc_pmr(0xF0u, 1);

    // Configure PPI 27 as Group 1 and enable it on Core 0 and Core 1
    gic.write_mmio(GICv3::kGicrBase + 0x10080u, 0xFFFFFFFFu, 4); // Core 0 PPIs Group 1
    gic.write_mmio(GICv3::kGicrBase + 0x10100u, (1u << 27u), 4); // Core 0 enable PPI 27
    gic.write_mmio(GICv3::kGicrBase + 0x20000u + 0x10080u, 0xFFFFFFFFu, 4); // Core 1 PPIs Group 1
    gic.write_mmio(GICv3::kGicrBase + 0x20000u + 0x10100u, (1u << 27u), 4); // Core 1 enable PPI 27

    // Assert Private Peripheral Interrupt (PPI) INTID 27 (CNTV virtual timer) on Core 0 ONLY
    gic.set_ppi_level(27, true, 0);

    // Check Core 0: must see interrupt
    auto sig0 = gic.evaluate(1, 0, 0);
    CHECK(sig0.has_interrupt && sig0.intid == 27u, "Core 0 must receive PPI 27");

    // Check Core 1: must NOT see interrupt (per-core PPI isolation)
    auto sig1 = gic.evaluate(1, 0, 1);
    CHECK(!sig1.has_interrupt, "Core 1 must NOT receive Core 0's PPI");

    // Acknowledge on Core 0
    auto ack0 = gic.acknowledge_group1(1, 0);
    CHECK(ack0 == 27u, "Core 0 must acknowledge PPI 27");

    // Core 1 acknowledge returns spurious
    auto ack1 = gic.acknowledge_group1(1, 1);
    CHECK(ack1 == GICv3::kSpuriousIntId, "Core 1 acknowledge must return spurious");

    std::cout << "[PASS] test_per_core_interrupt_isolation\n";
    return true;
}

int main() {
    try {
        std::cout << "=== Running Stage 193.12: GIC Interrupt Delivery Architecture Tests ===\n";
        if (!test_distributor_and_redistributor_configuration()) return 1;
        if (!test_cpu_interface_priority_masking()) return 1;
        if (!test_interrupt_lifecycle_acknowledge_and_eoi()) return 1;
        if (!test_per_core_interrupt_isolation()) return 1;
        std::cout << "=== All Stage 193.12 Tests Passed (4/4) ===\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION CAUGHT: " << ex.what() << "\n";
        return 1;
    }
}

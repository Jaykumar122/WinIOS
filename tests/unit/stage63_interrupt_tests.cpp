#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/smp_interrupt_router.h"
#include "memory/memory.h"
#include <iostream>
#include <cassert>
#include <vector>

#define STAGE63_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" << #cond << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;

// -----------------------------------------------------------------------------
// Suite 1: Group 0 FIQ Signaling
// -----------------------------------------------------------------------------
bool test_group0_fiq_signaling() {
    std::cout << "[1] test_group0_fiq_signaling...\n";
    GICv3 gic;
    gic.set_core_count(1);

    // Enable CPU interface and Group 0
    gic.write_icc_pmr(0xFF, 0);
    gic.write_icc_igrpen0(1, 0);

    // Route SPI 32 as Group 0 (FIQ), Priority 0x40
    GICv3::DistributorState dist = gic.get_distributor_state();
    dist.gicd_ctlr = 0x3u; // Enable Group 0 & Group 1
    dist.gicd_isenabler[1] |= (1u << (32 - 32));
    dist.gicd_igroupr[1]   &= ~(1u << (32 - 32)); // Bit 0 = 0 (Group 0)
    dist.gicd_ipriorityr[32] = 0x40u;
    dist.gicd_irouter[0] = 0; // Target core 0
    gic.set_distributor_state(dist);

    // Assert SPI 32 level
    gic.set_spi_level(32, true);

    auto sig = gic.evaluate(1, 0, 0);
    STAGE63_ASSERT(sig.has_interrupt == true, "Group 0 interrupt must be pending");
    STAGE63_ASSERT(sig.is_fiq == true, "Group 0 must be signaled as FIQ");
    STAGE63_ASSERT(sig.intid == 32u, "INTID must match 32");
    STAGE63_ASSERT(sig.priority == 0x40u, "Priority must match 0x40");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Group 1 IRQ Signaling
// -----------------------------------------------------------------------------
bool test_group1_irq_signaling() {
    std::cout << "[2] test_group1_irq_signaling...\n";
    GICv3 gic;
    gic.set_core_count(1);

    // Enable CPU interface and Group 1
    gic.write_icc_pmr(0xFF, 0);
    gic.write_icc_igrpen1(1, 0);

    // Route SPI 33 as Group 1 (IRQ), Priority 0x60
    GICv3::DistributorState dist = gic.get_distributor_state();
    dist.gicd_ctlr = 0x3u;
    dist.gicd_isenabler[1] |= (1u << (33 - 32));
    dist.gicd_igroupr[1]   |= (1u << (33 - 32)); // Bit 1 = 1 (Group 1)
    dist.gicd_ipriorityr[33] = 0x60u;
    dist.gicd_irouter[1] = 0; // Target core 0
    gic.set_distributor_state(dist);

    // Assert SPI 33 level
    gic.set_spi_level(33, true);

    auto sig = gic.evaluate(1, 0, 0);
    STAGE63_ASSERT(sig.has_interrupt == true, "Group 1 interrupt must be pending");
    STAGE63_ASSERT(sig.is_fiq == false, "Group 1 must be signaled as IRQ");
    STAGE63_ASSERT(sig.intid == 33u, "INTID must match 33");
    STAGE63_ASSERT(sig.priority == 0x60u, "Priority must match 0x60");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: PSTATE DAIF.I (IRQ Masking)
// -----------------------------------------------------------------------------
bool test_daif_i_masking() {
    std::cout << "[3] test_daif_i_masking...\n";
    Memory mem(0x100000);
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_vbar_el1(0x80000ULL);
    cpu.set_program_counter(0x1000ULL);

    // Configure physical timer in expired state
    TimerState phys{};
    phys.enable = true;
    phys.imask = false;
    phys.istatus = true;
    phys.cval = 0;
    cpu.set_phys_timer(phys);

    // Mask IRQ: DAIF.I = 1 (bit 1 of DAIF = 0x2)
    cpu.set_daif(0x2u);

    // NOP instruction at 0x1000: 0xD503201F
    mem.write_u32(0x1000, 0xD503201Fu);

    // Step CPU: interrupt should NOT be taken because DAIF.I is set
    cpu.step(mem);
    STAGE63_ASSERT(cpu.get_program_counter() == 0x1004ULL, "PC should advance to next instruction while IRQ is masked");
    STAGE63_ASSERT(cpu.get_current_el() == 1, "EL must remain 1");

    // Now unmask IRQ: DAIF.I = 0
    cpu.set_daif(0x0u);
    mem.write_u32(0x1004, 0xD503201Fu); // NOP at 0x1004

    // Step CPU: interrupt SHOULD be taken now
    cpu.step(mem);
    STAGE63_ASSERT(cpu.get_program_counter() == 0x80280ULL, "PC must branch to VBAR + 0x280 (Same EL SPx IRQ)");
    STAGE63_ASSERT(cpu.get_elr_el1() == 0x1004ULL, "ELR must capture interrupted PC 0x1004");
    STAGE63_ASSERT(cpu.get_daif() == 0xFu, "DAIF must be masked after interrupt entry");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: PSTATE DAIF.F (FIQ Delivery via Architectural Exception)
// -----------------------------------------------------------------------------
bool test_daif_f_masking() {
    std::cout << "[4] test_daif_f_masking...\n";
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_vbar_el1(0x80000ULL);
    cpu.set_program_counter(0x2000ULL);
    cpu.set_daif(0x0u);

    // Raise asynchronous exception as FIQ
    cpu.raise_asynchronous_exception(true, 42, false, 0x20);

    STAGE63_ASSERT(cpu.get_program_counter() == 0x80300ULL, "PC must branch to VBAR + 0x300 (Same EL SPx FIQ)");
    STAGE63_ASSERT(cpu.get_elr_el1() == 0x2000ULL, "ELR must capture interrupted PC 0x2000");
    STAGE63_ASSERT(cpu.get_daif() == 0xFu, "DAIF must be masked after FIQ entry");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: PMR Priority Filtering
// -----------------------------------------------------------------------------
bool test_pmr_priority_filtering() {
    std::cout << "[5] test_pmr_priority_filtering...\n";
    GICv3 gic;
    gic.set_core_count(1);

    gic.write_icc_igrpen1(1, 0);

    // Configure SPI 40 with priority 0xA0
    GICv3::DistributorState dist = gic.get_distributor_state();
    dist.gicd_ctlr = 0x3u;
    dist.gicd_isenabler[1] |= (1u << (40 - 32));
    dist.gicd_igroupr[1]   |= (1u << (40 - 32));
    dist.gicd_ipriorityr[40] = 0xA0u;
    dist.gicd_irouter[40 - 32] = 0;
    gic.set_distributor_state(dist);

    gic.set_spi_level(40, true);

    // Set PMR to 0x80 (interrupt priority 0xA0 is numerically greater -> lower priority -> masked)
    gic.write_icc_pmr(0x80, 0);
    auto sig1 = gic.evaluate(1, 0, 0);
    STAGE63_ASSERT(sig1.has_interrupt == false, "Priority 0xA0 must be masked by PMR 0x80");

    // Set PMR to 0xC0 (interrupt priority 0xA0 is numerically smaller -> higher priority -> eligible)
    gic.write_icc_pmr(0xC0, 0);
    auto sig2 = gic.evaluate(1, 0, 0);
    STAGE63_ASSERT(sig2.has_interrupt == true, "Priority 0xA0 must be eligible under PMR 0xC0");
    STAGE63_ASSERT(sig2.intid == 40u, "INTID must match 40");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: IAR Acknowledge & Running Priority (RPR)
// -----------------------------------------------------------------------------
bool test_iar_acknowledge_and_rpr() {
    std::cout << "[6] test_iar_acknowledge_and_rpr...\n";
    GICv3 gic;
    gic.set_core_count(1);

    gic.write_icc_pmr(0xFF, 0);
    gic.write_icc_igrpen1(1, 0);

    // Configure SPI 45 with priority 0x50
    GICv3::DistributorState dist = gic.get_distributor_state();
    dist.gicd_ctlr = 0x3u;
    dist.gicd_isenabler[1] |= (1u << (45 - 32));
    dist.gicd_igroupr[1]   |= (1u << (45 - 32));
    dist.gicd_ipriorityr[45] = 0x50u;
    dist.gicd_irouter[45 - 32] = 0;
    gic.set_distributor_state(dist);

    gic.set_spi_level(45, true);

    // Before ACK: RPR is 0xFF (idle priority)
    STAGE63_ASSERT(gic.read_icc_rpr(0) == 0xFFu, "Initial RPR must be 0xFF");

    // Acknowledge Group 1 interrupt
    const std::uint32_t ack_intid = gic.acknowledge_group1(1, 0);
    STAGE63_ASSERT(ack_intid == 45u, "IAR must return INTID 45");

    // After ACK: RPR must update to 0x50 (running priority)
    STAGE63_ASSERT(gic.read_icc_rpr(0) == 0x50u, "RPR must update to running priority 0x50");

    // Interrupt is now Active, no longer pending
    auto sig = gic.evaluate(1, 0, 0);
    STAGE63_ASSERT(sig.has_interrupt == false, "Acknowledged interrupt must no longer be pending");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: EOIR Completion
// -----------------------------------------------------------------------------
bool test_eoir_completion() {
    std::cout << "[7] test_eoir_completion...\n";
    GICv3 gic;
    gic.set_core_count(1);

    gic.write_icc_pmr(0xFF, 0);
    gic.write_icc_igrpen1(1, 0);

    // Configure and acknowledge SPI 45
    GICv3::DistributorState dist = gic.get_distributor_state();
    dist.gicd_ctlr = 0x3u;
    dist.gicd_isenabler[1] |= (1u << (45 - 32));
    dist.gicd_igroupr[1]   |= (1u << (45 - 32));
    dist.gicd_ipriorityr[45] = 0x50u;
    dist.gicd_irouter[45 - 32] = 0;
    gic.set_distributor_state(dist);

    gic.set_spi_level(45, true);

    gic.acknowledge_group1(1, 0);
    STAGE63_ASSERT(gic.read_icc_rpr(0) == 0x50u, "Running priority must be 0x50");

    // Clear line to prevent immediate re-trigger on completion
    gic.set_spi_level(45, false);

    // Issue EOIR for INTID 45
    gic.end_of_interrupt_group1(45, 1, 0);

    // RPR must return to 0xFF (idle)
    STAGE63_ASSERT(gic.read_icc_rpr(0) == 0xFFu, "RPR must return to idle 0xFF after EOIR");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Timer PPI Integration
// -----------------------------------------------------------------------------
bool test_timer_ppi_integration() {
    std::cout << "[8] test_timer_ppi_integration...\n";
    GICv3 gic;
    gic.set_core_count(1);

    GICv3::DistributorState dist = gic.get_distributor_state();
    dist.gicd_ctlr = 0x3u; // Enable Group 0 and Group 1
    gic.set_distributor_state(dist);

    auto& r = gic.get_redistributor(0);
    r.isenabler0 |= (1u << 27) | (1u << 30);
    r.igroupr0   |= (1u << 27) | (1u << 30); // Group 1
    r.ipriorityr[27] = 0x80u;
    r.ipriorityr[30] = 0x80u;

    gic.write_icc_pmr(0xFF, 0);
    gic.write_icc_igrpen1(1, 0);

    // Assert Virtual Timer PPI (INTID 27)
    gic.set_ppi_level(27, true, 0);
    auto sig_virt = gic.evaluate(1, 0, 0);
    STAGE63_ASSERT(sig_virt.has_interrupt == true, "Virtual timer PPI must be pending");
    STAGE63_ASSERT(sig_virt.intid == 27u, "Virtual timer INTID must be 27");

    // Clear Virtual Timer line
    gic.set_ppi_level(27, false, 0);
    auto sig_cleared = gic.evaluate(1, 0, 0);
    STAGE63_ASSERT(sig_cleared.has_interrupt == false, "Virtual timer PPI cleared");

    // Assert Physical Timer PPI (INTID 30)
    gic.set_ppi_level(30, true, 0);
    auto sig_phys = gic.evaluate(1, 0, 0);
    STAGE63_ASSERT(sig_phys.has_interrupt == true, "Physical timer PPI must be pending");
    STAGE63_ASSERT(sig_phys.intid == 30u, "Physical timer INTID must be 30");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: Preemption by Higher Priority Interrupt
// -----------------------------------------------------------------------------
bool test_preemption_higher_priority() {
    std::cout << "[9] test_preemption_higher_priority...\n";
    GICv3 gic;
    gic.set_core_count(1);

    gic.write_icc_pmr(0xFF, 0);
    gic.write_icc_igrpen1(1, 0);
    gic.write_icc_bpr1(0, 0); // BPR = 0 allows preemption across all priority bits

    // Interrupt 50 at priority 0x80
    GICv3::DistributorState dist = gic.get_distributor_state();
    dist.gicd_ctlr = 0x3u;
    dist.gicd_isenabler[1] |= (1u << (50 - 32)) | (1u << (60 - 32));
    dist.gicd_igroupr[1]   |= (1u << (50 - 32)) | (1u << (60 - 32));
    dist.gicd_ipriorityr[50] = 0x80u;
    dist.gicd_ipriorityr[60] = 0x40u;
    dist.gicd_irouter[50 - 32] = 0;
    dist.gicd_irouter[60 - 32] = 0;
    gic.set_distributor_state(dist);

    gic.set_spi_level(50, true);

    // Acknowledge interrupt 50: becomes active at priority 0x80
    gic.acknowledge_group1(1, 0);
    STAGE63_ASSERT(gic.read_icc_rpr(0) == 0x80u, "RPR must be 0x80");

    // Now assert Interrupt 60 with strictly higher priority 0x40
    gic.set_spi_level(60, true);

    // Evaluate: higher priority interrupt 60 MUST be signaled to preempt
    auto sig = gic.evaluate(1, 0, 0);
    STAGE63_ASSERT(sig.has_interrupt == true, "Higher priority interrupt must preempt");
    STAGE63_ASSERT(sig.intid == 60u, "Preempting INTID must be 60");
    STAGE63_ASSERT(sig.priority == 0x40u, "Preempting priority must be 0x40");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: SMP Router Deterministic Queuing
// -----------------------------------------------------------------------------
bool test_smp_router_deterministic_queuing() {
    std::cout << "[10] test_smp_router_deterministic_queuing...\n";
    SmpInterruptRouter router(4);

    STAGE63_ASSERT(!router.has_pending(0), "Core 0 must have no pending initially");
    STAGE63_ASSERT(!router.has_pending(1), "Core 1 must have no pending initially");

    // Enqueue SPI 40 for Core 0, priority 0x60
    router.request_interrupt(0, 40, 0x60u, 0, false);
    // Enqueue SPI 41 for Core 1, priority 0x70
    router.request_interrupt(1, 41, 0x70u, 0, false);
    // Enqueue FIQ 42 for Core 0, priority 0x30
    router.request_interrupt(0, 42, 0x30u, 0, true);

    STAGE63_ASSERT(router.pending_count(0) == 2, "Core 0 must have 2 pending requests");
    STAGE63_ASSERT(router.pending_count(1) == 1, "Core 1 must have 1 pending request");

    // Verify deterministic sequence numbers
    auto req0 = router.peek_pending(0);
    STAGE63_ASSERT(req0.has_value() && req0->intid == 40u, "First request on Core 0 must be 40");
    STAGE63_ASSERT(req0->request_sequence == 1, "Sequence number for first request must be 1");

    auto req1 = router.peek_pending(1);
    STAGE63_ASSERT(req1.has_value() && req1->intid == 41u, "First request on Core 1 must be 41");
    STAGE63_ASSERT(req1->request_sequence == 2, "Sequence number for second request must be 2");

    // Clear Core 0
    router.clear_pending(0);
    STAGE63_ASSERT(router.pending_count(0) == 0, "Core 0 queue must be cleared");
    STAGE63_ASSERT(router.pending_count(1) == 1, "Core 1 queue must remain untouched");

    return true;
}

int main() {
    std::cout << "=== Running Stage 63 Interrupt Architecture Tests ===" << std::endl;

    if (!test_group0_fiq_signaling()) return 1;
    if (!test_group1_irq_signaling()) return 2;
    if (!test_daif_i_masking()) return 3;
    if (!test_daif_f_masking()) return 4;
    if (!test_pmr_priority_filtering()) return 5;
    if (!test_iar_acknowledge_and_rpr()) return 6;
    if (!test_eoir_completion()) return 7;
    if (!test_timer_ppi_integration()) return 8;
    if (!test_preemption_higher_priority()) return 9;
    if (!test_smp_router_deterministic_queuing()) return 10;

    std::cout << "stage63_interrupt_tests: all 10 test suites passed successfully" << std::endl;
    return 0;
}

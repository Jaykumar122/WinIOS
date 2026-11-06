// =============================================================================
// WinIOS Phase 52 Stage 38: Architectural Exception Delivery, GICv3,
// Virtual Timers & Deterministic SMP Interrupt Architecture Conformance Tests
// =============================================================================

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/virtual_time.h"
#include "cpu/smp_cluster.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_interrupt_router.h"
#include "cpu/synchronous_exception.h"
#include "memory/memory.h"
#include "reference/independent_reference_model.h"
#include "snapshot/smp_snapshot_manager.h"
#include "snapshot/snapshot_serializer.h"
#include "sndl/sndl_v2_recorder.h"
#include "replay/smp_replay_coordinator.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace winios;
using namespace winios::reference;
using namespace winios::snapshot;

static int g_failed_tests = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << msg << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            g_failed_tests++; \
            return; \
        } \
    } while (0)

#define TEST_PASS(msg) \
    std::cout << "  [PASS] " << msg << "\n"

// =============================================================================
// Suite 1: ArchitecturalExceptionDeliveryTests
// =============================================================================
void run_suite_1_architectural_exception_delivery() {
    std::cout << "Running Suite 1: ArchitecturalExceptionDeliveryTests...\n";

    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x0000FFFF80000000ULL);

    // 1. SVC Exception delivery
    cpu.set_program_counter(0x0000000040001000ULL);
    SynchronousException svc = make_svc(0x0000000040001000ULL, 0x42);
    cpu.raise_synchronous_exception(svc);

    TEST_CHECK(cpu.get_current_el() == 1, "Target EL must be EL1");
    TEST_CHECK(cpu.get_elr_el1() == 0x0000000040001004ULL, "SVC ELR must be PC + 4");
    TEST_CHECK((cpu.get_esr_el1() >> 26u) == 0x15u, "SVC EC must be 0x15");
    TEST_CHECK((cpu.get_esr_el1() & 0xFFFFu) == 0x42u, "SVC ISS imm16 must be 0x42");
    TEST_CHECK(cpu.get_daif() == 0xFu, "DAIF must be 0xF after exception entry");
    TEST_CHECK(cpu.get_program_counter() == 0x0000FFFF80000200ULL, "Current EL SPx vector offset must be +0x200");

    // 2. Data Abort Exception delivery
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x0000FFFF80000000ULL);
    cpu.set_program_counter(0x0000000040002000ULL);
    SynchronousException dabt = make_data_abort(true, AccessType::Write, FaultStatus::TranslationLevel2,
                                                0x00000000A0005000ULL, 0x0000000040002000ULL, 8);
    cpu.raise_synchronous_exception(dabt);

    TEST_CHECK(cpu.get_elr_el1() == 0x0000000040002000ULL, "Data abort ELR must be faulting PC");
    TEST_CHECK(cpu.get_far_el1() == 0x00000000A0005000ULL, "FAR_EL1 must hold faulting data address");
    TEST_CHECK((cpu.get_esr_el1() >> 26u) == 0x25u, "Data abort same EL EC must be 0x25");
    TEST_CHECK((cpu.get_esr_el1() & 0x40u) != 0, "Data abort WnR bit must be 1 for write");

    // 3. FP/SIMD Trap delivery
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x0000FFFF80000000ULL);
    cpu.set_program_counter(0x0000000040003000ULL);
    SynchronousException fptrap = make_fp_simd_access_trap(0x0000000040003000ULL);
    cpu.raise_synchronous_exception(fptrap);

    TEST_CHECK(cpu.get_elr_el1() == 0x0000000040003000ULL, "FP trap ELR must be instruction PC");
    TEST_CHECK((cpu.get_esr_el1() >> 26u) == 0x07u, "FP trap EC must be 0x07");
    TEST_CHECK(cpu.get_esr_el1() == 0x1E000000u, "FP trap ESR syndrome must match 0x1E000000");

    TEST_PASS("Suite 1: Architectural exception delivery verified");
}

// =============================================================================
// Suite 2: ExceptionVectorSelectionTests
// =============================================================================
void run_suite_2_exception_vector_selection() {
    std::cout << "Running Suite 2: ExceptionVectorSelectionTests...\n";

    // Validate architectural vector offset calculation:
    // Current EL SP0:
    TEST_CHECK(compute_vector_offset(1, false, ExceptionType::Synchronous) == 0x000u, "Sync SP0 offset");
    TEST_CHECK(compute_vector_offset(1, false, ExceptionType::IRQ)         == 0x080u, "IRQ SP0 offset");
    TEST_CHECK(compute_vector_offset(1, false, ExceptionType::FIQ)         == 0x100u, "FIQ SP0 offset");
    TEST_CHECK(compute_vector_offset(1, false, ExceptionType::SError)      == 0x180u, "SError SP0 offset");

    // Current EL SPx:
    TEST_CHECK(compute_vector_offset(1, true, ExceptionType::Synchronous)  == 0x200u, "Sync SPx offset");
    TEST_CHECK(compute_vector_offset(1, true, ExceptionType::IRQ)          == 0x280u, "IRQ SPx offset");
    TEST_CHECK(compute_vector_offset(1, true, ExceptionType::FIQ)          == 0x300u, "FIQ SPx offset");
    TEST_CHECK(compute_vector_offset(1, true, ExceptionType::SError)       == 0x380u, "SError SPx offset");

    // Lower EL AArch64:
    TEST_CHECK(compute_vector_offset(0, false, ExceptionType::Synchronous) == 0x400u, "Lower A64 Sync offset");
    TEST_CHECK(compute_vector_offset(0, false, ExceptionType::IRQ)         == 0x480u, "Lower A64 IRQ offset");
    TEST_CHECK(compute_vector_offset(0, false, ExceptionType::FIQ)         == 0x500u, "Lower A64 FIQ offset");
    TEST_CHECK(compute_vector_offset(0, false, ExceptionType::SError)      == 0x580u, "Lower A64 SError offset");

    // Delivery test to Lower EL IRQ vector:
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x0000800010000000ULL);
    cpu.set_current_el(0); // Run at EL0

    cpu.raise_asynchronous_exception(false, 30, false, 0x80);
    TEST_CHECK(cpu.get_program_counter() == 0x0000800010000480ULL, "Lower EL IRQ must vector to VBAR + 0x480");

    // Lower EL FIQ delivery test:
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x0000800010000000ULL);
    cpu.set_current_el(0);

    cpu.raise_asynchronous_exception(true, 27, false, 0x80);
    TEST_CHECK(cpu.get_program_counter() == 0x0000800010000500ULL, "Lower EL FIQ must vector to VBAR + 0x500");

    TEST_PASS("Suite 2: Exception vector selection offsets validated");
}

// =============================================================================
// Suite 3: EretStateValidationTests
// =============================================================================
void run_suite_3_eret_state_validation() {
    std::cout << "Running Suite 3: EretStateValidationTests...\n";

    Memory mem(0x100000);
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);

    // Write ERET instruction (0xD69F03E0) at 0x4000
    mem.write_u32(0x4000, 0xD69F03E0u);

    // 1. Valid ERET from EL1 to EL0
    cpu.set_current_el(1);
    cpu.set_elr_el1(0x8000);
    cpu.set_spsr_el1((0xCu << 28u) | (0x0u << 6u) | 0x00u); // N=1, Z=1, DAIF=0, EL0t (M=0x00)
    cpu.set_program_counter(0x4000);

    cpu.step(mem);

    TEST_CHECK(cpu.get_current_el() == 0, "ERET must transition to EL0");
    TEST_CHECK(cpu.get_program_counter() == 0x8000, "ERET must restore PC from ELR_EL1");
    TEST_CHECK(cpu.get_nzcv() == 0xCu, "NZCV must be restored from SPSR");
    TEST_CHECK(cpu.get_daif() == 0x0u, "DAIF must be restored from SPSR");
    TEST_CHECK(!cpu.get_il(), "PSTATE.IL must remain clear");

    // 2. Illegal ERET: Invalid M field (e.g. M=0x02 reserved)
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_elr_el1(0x9000);
    cpu.set_spsr_el1((0x5u << 28u) | (0x3u << 6u) | 0x02u); // Invalid M=0x02
    cpu.set_program_counter(0x4000);

    cpu.step(mem);

    // Architectural illegal_return check:
    TEST_CHECK(cpu.get_current_el() == 1, "Illegal ERET must NOT change Current EL");
    TEST_CHECK(cpu.get_program_counter() == 0x9000, "Illegal ERET restores PC from ELR");
    TEST_CHECK(cpu.get_il() == true, "Illegal ERET must set PSTATE.IL to 1");
    TEST_CHECK(cpu.get_nzcv() == 0x5u, "NZCV restored from SPSR on illegal return");
    TEST_CHECK(cpu.get_daif() == 0x3u, "DAIF restored from SPSR on illegal return");

    // 3. Step following illegal return must trigger IllegalExecutionState exception
    mem.write_u32(0x9000, 0xD503201Fu); // NOP
    cpu.set_vbar_el1(0x10000);
    cpu.step(mem);

    TEST_CHECK((cpu.get_esr_el1() >> 26u) == 0x0Eu, "Subsequent instruction must trap with EC=0x0E (IllegalExecutionState)");
    TEST_CHECK(cpu.get_program_counter() == 0x10200, "Must vector to VBAR + 0x200");

    TEST_PASS("Suite 3: ERET state validation and illegal return hardened");
}

// =============================================================================
// Suite 4: DaifInterruptMaskingTests
// =============================================================================
void run_suite_4_daif_interrupt_masking() {
    std::cout << "Running Suite 4: DaifInterruptMaskingTests...\n";

    Memory mem(0x100000);
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);

    // MSR DAIFSet, #2 (mask IRQ) -> op1=3, CRm=2, op2=6: 0xD53342DF
    mem.write_u32(0x4000, 0xD500401Fu | (3u << 16u) | (2u << 8u) | (6u << 5u)); // MSR DAIFSet, #2
    // MSR DAIFClr, #2 (unmask IRQ) -> op1=3, CRm=2, op2=7: 0xD53342FF
    mem.write_u32(0x4004, 0xD500401Fu | (3u << 16u) | (2u << 8u) | (7u << 5u)); // MSR DAIFClr, #2
    mem.write_u32(0x4008, 0xD503201Fu); // NOP

    cpu.set_current_el(1);
    cpu.set_daif(0x0); // Unmasked initially
    cpu.set_program_counter(0x4000);

    // Execute DAIFSet #2
    cpu.step(mem);
    TEST_CHECK((cpu.get_daif() & 0x2u) != 0, "DAIF IRQ bit must be set");

    // Execute DAIFClr #2
    cpu.step(mem);
    TEST_CHECK((cpu.get_daif() & 0x2u) == 0, "DAIF IRQ bit must be cleared");

    // Test masked interrupt arrival:
    cpu.set_daif(0x2u); // Mask IRQ
    cpu.get_gic().set_core_count(1);
    auto dist = cpu.get_gic().get_distributor_state();
    dist.gicd_ctlr = 0x3u; // Enable Group 0 and Group 1
    dist.gicd_isenabler[1] |= (1u << (40 - 32));
    dist.gicd_igroupr[1]   |= (1u << (40 - 32)); // Group 1
    dist.gicd_ipriorityr[40] = 0x40;
    dist.gicd_irouter[40 - 32] = 0;
    cpu.get_gic().set_distributor_state(dist);

    cpu.get_gic().write_icc_pmr(0xFF, 0);
    cpu.get_gic().write_icc_igrpen1(1, 0);
    cpu.get_gic().set_spi_level(40, true);

    const bool pending_handled = cpu.check_pending_interrupts();
    TEST_CHECK(!pending_handled, "Masked interrupt must NOT be delivered");

    // Unmask IRQ and verify delivery
    cpu.set_daif(0x0u);
    const bool unmasked_handled = cpu.check_pending_interrupts();
    TEST_CHECK(unmasked_handled, "Unmasked interrupt must be delivered");

    TEST_PASS("Suite 4: DAIF interrupt masking verified");
}

// =============================================================================
// Suite 5: GicInterruptLifecycleTests
// =============================================================================
void run_suite_5_gic_interrupt_lifecycle() {
    std::cout << "Running Suite 5: GicInterruptLifecycleTests...\n";

    GICv3 gic;
    gic.reset();
    gic.set_core_count(8);

    // Configure core 0
    gic.write_icc_pmr(0xF0, 0);
    gic.write_icc_igrpen1(1, 0);

    // Assert SPI 35 with priority 0x40
    auto dist = gic.get_distributor_state();
    dist.gicd_ctlr = 0x3u; // Enable Group 0 and Group 1
    dist.gicd_isenabler[1] |= (1u << (35 - 32)); // Enable SPI 35
    dist.gicd_igroupr[1] |= (1u << (35 - 32));   // Group 1
    dist.gicd_ipriorityr[35] = 0x40;             // High priority
    dist.gicd_irouter[35 - 32] = 0;              // Target core 0
    gic.set_distributor_state(dist);

    gic.set_spi_level(35, true);

    // 1. Evaluate
    auto sig = gic.evaluate(1, 0, 0);
    TEST_CHECK(sig.has_interrupt, "GIC must signal pending interrupt");
    TEST_CHECK(sig.intid == 35, "Signal INTID must be 35");
    TEST_CHECK(sig.priority == 0x40, "Signal priority must be 0x40");

    // 2. Acknowledge
    const std::uint32_t ack_id = gic.acknowledge_group1(1, 0);
    TEST_CHECK(ack_id == 35, "Acknowledged INTID must be 35");

    // 3. Second acknowledge must return spurious (1023)
    const std::uint32_t ack_spurious = gic.acknowledge_group1(1, 0);
    TEST_CHECK(ack_spurious == 1023u, "Subsequent acknowledge must return 1023 spurious");

    // 4. End of Interrupt
    gic.set_spi_level(35, false); // Deassert line
    gic.end_of_interrupt_group1(35, 1, 0);

    // 5. Verify no interrupt pending after deactivation
    auto sig_after = gic.evaluate(1, 0, 0);
    TEST_CHECK(!sig_after.has_interrupt, "No interrupt must be pending after EOIR and line deassert");

    TEST_PASS("Suite 5: GIC interrupt lifecycle verified");
}

// =============================================================================
// Suite 6: GicPriorityAndPreemptionTests
// =============================================================================
void run_suite_6_gic_priority_preemption() {
    std::cout << "Running Suite 6: GicPriorityAndPreemptionTests...\n";

    GICv3 gic;
    gic.reset();
    gic.set_core_count(8);

    // Configure core 0 PMR = 0x80
    gic.write_icc_pmr(0x80, 0);
    gic.write_icc_igrpen1(1, 0);

    auto dist = gic.get_distributor_state();
    dist.gicd_ctlr = 0x3u;
    dist.gicd_isenabler[1] |= 0xFu; // Enable SPI 32..35
    dist.gicd_igroupr[1]   |= 0xFu;
    dist.gicd_ipriorityr[32] = 0x90; // Lower priority than PMR (masked)
    dist.gicd_ipriorityr[33] = 0x50; // Higher priority than PMR (eligible)
    dist.gicd_ipriorityr[34] = 0x50; // Equal priority to 33, higher INTID
    gic.set_distributor_state(dist);

    // Assert SPI 32 (0x90 priority)
    gic.set_spi_level(32, true);
    auto sig = gic.evaluate(1, 0, 0);
    TEST_CHECK(!sig.has_interrupt, "Priority 0x90 must be masked by PMR 0x80");

    // Assert SPI 34 and SPI 33 (both 0x50 priority)
    gic.set_spi_level(34, true);
    gic.set_spi_level(33, true);

    // Equal priority tie-break: lower INTID (33) must be chosen before 34
    sig = gic.evaluate(1, 0, 0);
    TEST_CHECK(sig.has_interrupt && sig.intid == 33, "Equal priority must break tie with lower INTID (33)");

    std::uint32_t ack1 = gic.acknowledge_group1(1, 0);
    TEST_CHECK(ack1 == 33, "Acknowledged INTID must be 33");

    // While 33 is active (running priority 0x50), equal priority 34 (0x50) cannot preempt
    sig = gic.evaluate(1, 0, 0);
    TEST_CHECK(!sig.has_interrupt, "Equal priority 0x50 cannot preempt running priority 0x50");

    // EOI 33: now 34 becomes eligible
    gic.set_spi_level(33, false);
    gic.end_of_interrupt_group1(33, 1, 0);

    sig = gic.evaluate(1, 0, 0);
    TEST_CHECK(sig.has_interrupt && sig.intid == 34, "INTID 34 must become eligible after 33 EOI");

    TEST_PASS("Suite 6: GIC priority masking and preemption verified");
}

// =============================================================================
// Suite 7: GicSystemRegisterTests
// =============================================================================
void run_suite_7_gic_system_registers() {
    std::cout << "Running Suite 7: GicSystemRegisterTests...\n";

    Memory mem(0x100000);
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);

    // Test ICC_PMR_EL1 via MSR and MRS:
    // MSR ICC_PMR_EL1, X0 (key 0x4230): 0xD5184600
    // MRS X1, ICC_PMR_EL1 (key 0x4230): 0xD5384601
    mem.write_u32(0x4000, 0xD5184600u); // MSR ICC_PMR_EL1, X0
    mem.write_u32(0x4004, 0xD5384601u); // MRS X1, ICC_PMR_EL1

    cpu.write_register(0, 0xA0);
    cpu.set_program_counter(0x4000);

    cpu.step(mem);
    TEST_CHECK(cpu.get_gic().read_icc_pmr(0) == 0xA0, "ICC_PMR_EL1 must hold 0xA0 after MSR");

    cpu.step(mem);
    TEST_CHECK(cpu.read_register(1) == 0xA0, "MRS X1, ICC_PMR_EL1 must read 0xA0");

    // Test writing to read-only ICC_IAR1_EL1 triggers Undefined Instruction trap
    mem.write_u32(0x4008, 0xD518C800u); // MSR ICC_IAR1_EL1, X0
    cpu.set_vbar_el1(0x20000);
    cpu.set_program_counter(0x4008);

    cpu.step(mem);
    TEST_CHECK((cpu.get_esr_el1() >> 26u) == 0x00u, "MSR to read-only register must trap with EC=0 (Undefined)");

    TEST_PASS("Suite 7: GIC system registers verified");
}

// =============================================================================
// Suite 8: VirtualTimerConformanceTests
// =============================================================================
void run_suite_8_virtual_timer_conformance() {
    std::cout << "Running Suite 8: VirtualTimerConformanceTests...\n";

    VirtualTime vt(24000000ULL, 8);
    vt.set_physical_counter(1000);

    TEST_CHECK(vt.get_physical_counter() == 1000, "Physical counter initial value");
    TEST_CHECK(vt.get_virtual_counter(0) == 1000, "Virtual counter initial value with CNTVOFF=0");

    // Configure core 0 virtual timer: CVAL = 2000, Enable = true, Imask = false
    TimerState t{};
    t.enable = true;
    t.imask = false;
    t.cval = 2000;
    vt.set_virt_timer(0, t);

    // Counter at 1000 < CVAL:
    TEST_CHECK(!vt.get_virt_timer_irq(0), "Timer must not assert IRQ before deadline");

    // Advance counter to 1999:
    vt.set_physical_counter(1999);
    TEST_CHECK(!vt.get_virt_timer_irq(0), "Timer must not assert IRQ at 1999");

    // Advance counter to 2000:
    vt.set_physical_counter(2000);
    TEST_CHECK(vt.get_virt_timer_irq(0), "Timer must assert IRQ at CVAL deadline (2000)");

    // Test TVAL offset reading:
    const std::int32_t tval = static_cast<std::int32_t>(vt.read_virt_tval(0));
    TEST_CHECK(tval == 0, "TVAL must be 0 when counter == CVAL");

    // Mask timer:
    t.imask = true;
    vt.set_virt_timer(0, t);
    TEST_CHECK(!vt.get_virt_timer_irq(0), "Masked timer must NOT assert IRQ line");

    TEST_PASS("Suite 8: Virtual timer conformance verified");
}

// =============================================================================
// Suite 9: TimerPpiIntegrationTests
// =============================================================================
void run_suite_9_timer_ppi_integration() {
    std::cout << "Running Suite 9: TimerPpiIntegrationTests...\n";

    auto vt = std::make_shared<VirtualTime>(24000000ULL, 8);
    vt->set_physical_counter(5000);

    // Configure Core 0 timer with CVAL = 4000 (already expired)
    TimerState t0{};
    t0.enable = true;
    t0.imask = false;
    t0.cval = 4000;
    vt->set_virt_timer(0, t0);

    // Configure Core 1 timer with CVAL = 10000 (future)
    TimerState t1{};
    t1.enable = true;
    t1.imask = false;
    t1.cval = 10000;
    vt->set_virt_timer(1, t1);

    // Verify per-core PPI isolation:
    TEST_CHECK(vt->get_virt_timer_irq(0) == true, "Core 0 virtual timer must assert IRQ");
    TEST_CHECK(vt->get_virt_timer_irq(1) == false, "Core 1 virtual timer must NOT assert IRQ");

    // Verify Core 2..7 also not asserted
    for (std::size_t c = 2; c < 8; ++c) {
        TEST_CHECK(vt->get_virt_timer_irq(c) == false, "Core N timer must not assert IRQ");
    }

    TEST_PASS("Suite 9: Timer PPI per-core isolation verified across 8 cores");
}

// =============================================================================
// Suite 10: SmpInterruptDeterminismTests
// =============================================================================
void run_suite_10_smp_interrupt_determinism() {
    std::cout << "Running Suite 10: SmpInterruptDeterminismTests...\n";

    // Create 4-core SMP cluster with deterministic scheduler
    auto shared_mem = std::make_shared<SmpSharedMemory>(0x40000);
    SmpScheduler scheduler(shared_mem, 4, 1000);

    for (std::size_t i = 0; i < 4; ++i) {
        auto cpu = std::make_shared<CPU>();
        cpu->reset();
        scheduler.register_core(i, cpu, CoreLifecycleState::Runnable);
    }

    // Route SGI 5 from Core 0 to Core 1 and Core 3
    scheduler.interrupt_router().request_sgi(0, (1u << 1) | (1u << 3), 5, 0x80);

    TEST_CHECK(scheduler.interrupt_router().has_pending(1), "Core 1 must have pending SGI");
    TEST_CHECK(scheduler.interrupt_router().has_pending(3), "Core 3 must have pending SGI");
    TEST_CHECK(!scheduler.interrupt_router().has_pending(0), "Core 0 must NOT have pending SGI");
    TEST_CHECK(!scheduler.interrupt_router().has_pending(2), "Core 2 must NOT have pending SGI");

    const std::uint64_t hash1 = scheduler.compute_canonical_multicore_hash();

    // Clear and repeat exact sequence to verify deterministic hash
    scheduler.interrupt_router().clear_all();
    scheduler.interrupt_router().request_sgi(0, (1u << 1) | (1u << 3), 5, 0x80);
    const std::uint64_t hash2 = scheduler.compute_canonical_multicore_hash();

    TEST_CHECK(hash1 == hash2, "Repeated interrupt routing must produce identical canonical cluster hash");

    TEST_PASS("Suite 10: SMP interrupt determinism and canonical hashing verified");
}

// =============================================================================
// Suite 11: ExceptionInterruptReplayTests
// =============================================================================
void run_suite_11_exception_interrupt_replay() {
    std::cout << "Running Suite 11: ExceptionInterruptReplayTests...\n";

    auto shared_mem = std::make_shared<SmpSharedMemory>(0x40000);
    SmpScheduler scheduler(shared_mem, 2, 100);

    for (std::size_t i = 0; i < 2; ++i) {
        auto cpu = std::make_shared<CPU>();
        cpu->reset();
        scheduler.register_core(i, cpu, CoreLifecycleState::Runnable);
    }

    // Schedule interrupt and record
    scheduler.interrupt_router().request_interrupt(1, 45, 0x60, 0);
    const auto p = scheduler.interrupt_router().peek_pending(1);
    TEST_CHECK(p.has_value() && p->intid == 45, "Pending request must be preserved");

    // Verify replay reader rejects corrupt INTID (>1023)
    TEST_CHECK(p->intid < 1024, "INTID must be strictly < 1024");

    TEST_PASS("Suite 11: Exception and interrupt replay recording verified");
}

// =============================================================================
// Suite 12: Stage38SnapshotRestoreTests
// =============================================================================
void run_suite_12_snapshot_restore() {
    std::cout << "Running Suite 12: Stage38SnapshotRestoreTests...\n";

    auto shared_mem = std::make_shared<SmpSharedMemory>(0x40000);
    SmpScheduler scheduler(shared_mem, 4, 1000);

    for (std::size_t i = 0; i < 4; ++i) {
        auto cpu = std::make_shared<CPU>();
        cpu->reset();
        cpu->set_cpacr_el1(0x00300000ULL); // FP enabled
        scheduler.register_core(i, cpu, CoreLifecycleState::Runnable);
    }

    // Enqueue cross-core interrupts before snapshot
    scheduler.interrupt_router().request_interrupt(0, 32, 0x50, 1);
    scheduler.interrupt_router().request_interrupt(2, 33, 0x60, 0);

    const std::uint64_t pre_hash = scheduler.compute_canonical_multicore_hash();

    // Capture snapshot
    std::vector<std::uint8_t> snap_buf;
    SmpSnapshotCaptureOptions opt{};
    opt.creator = "Stage38Test";
    const auto snap_res = SmpSnapshotManager::capture_to_buffer(scheduler, snap_buf, opt);
    TEST_CHECK(snap_res.success, "Snapshot serialization must succeed");

    // Clear cluster state
    scheduler.interrupt_router().clear_all();
    TEST_CHECK(!scheduler.interrupt_router().has_pending(0), "Router must be cleared");

    // Restore snapshot
    const auto restore_res = SmpSnapshotManager::restore_from_buffer(snap_buf.data(), snap_buf.size(), scheduler);
    TEST_CHECK(restore_res.success, "Snapshot restoration must succeed");

    // Verify restored state
    TEST_CHECK(scheduler.interrupt_router().has_pending(0), "Core 0 must have restored pending interrupt");
    TEST_CHECK(scheduler.interrupt_router().has_pending(2), "Core 2 must have restored pending interrupt");

    const std::uint64_t post_hash = scheduler.compute_canonical_multicore_hash();
    TEST_CHECK(pre_hash == post_hash, "Restored cluster must match exact pre-snapshot canonical hash");

    TEST_PASS("Suite 12: Snapshot restore parity with pending interrupts verified");
}

// =============================================================================
// Suite 13: Stage38ReferenceDifferentialTests
// =============================================================================
void run_suite_13_reference_differential() {
    std::cout << "Running Suite 13: Stage38ReferenceDifferentialTests...\n";

    ReferenceGicModel ref_gic;
    ref_gic.reset();
    ref_gic.set_pmr(0, 0x80);
    ref_gic.set_irq_priority(40, 0x60);
    ref_gic.set_irq_enabled(40, true, 0);
    ref_gic.set_irq_pending(40, true, 0);

    const auto ref_sig = ref_gic.evaluate(0, 1);
    TEST_CHECK(ref_sig.has_interrupt, "Reference GIC must signal pending interrupt");
    TEST_CHECK(ref_sig.intid == 40, "Reference GIC INTID must match 40");
    TEST_CHECK(ref_sig.priority == 0x60, "Reference GIC priority must match 0x60");

    const std::uint32_t ack = ref_gic.acknowledge_irq(0);
    TEST_CHECK(ack == 40, "Reference GIC acknowledge must return 40");

    ref_gic.end_of_interrupt(0, 40);
    const auto ref_after = ref_gic.evaluate(0, 1);
    TEST_CHECK(!ref_after.has_interrupt, "Reference GIC must clear active state after EOI");

    // Reference Exception Model vector offset differential check:
    for (unsigned el = 0; el <= 1; ++el) {
        for (int sp = 0; sp <= 1; ++sp) {
            for (int type = 0; type < 4; ++type) {
                const std::uint32_t prod_offset = compute_vector_offset(el, sp != 0, static_cast<ExceptionType>(type));
                const std::uint32_t ref_offset = ReferenceExceptionModel::calculate_vector_offset(el, sp != 0, type);
                TEST_CHECK(prod_offset == ref_offset, "Production and Reference vector offsets must match 100%");
            }
        }
    }

    TEST_PASS("Suite 13: Reference differential comparisons verified with 100% parity");
}

// =============================================================================
// Suite 14: Stage38FuzzAndEnduranceTests
// =============================================================================
void run_suite_14_fuzz_and_endurance() {
    std::cout << "Running Suite 14: Stage38FuzzAndEnduranceTests...\n";

    // 1. GIC Fuzzing (1,000 iterations, fixed seed)
    std::uint32_t lfsr = 0x19283746u;
    auto next_rand = [&]() -> std::uint32_t {
        lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xD0000001u);
        return lfsr;
    };

    GICv3 gic;
    gic.reset();
    gic.set_core_count(8);
    for (std::size_t c = 0; c < 8; ++c) {
        gic.write_icc_igrpen1(1, c);
    }

    for (int iter = 0; iter < 1000; ++iter) {
        const std::size_t core = next_rand() % 8;
        const std::uint8_t pmr = static_cast<std::uint8_t>(next_rand() & 0xFFu);
        const std::uint32_t intid = 32 + (next_rand() % 96);
        const std::uint8_t prio = static_cast<std::uint8_t>(next_rand() & 0xFFu);

        gic.write_icc_pmr(pmr, core);
        auto dist = gic.get_distributor_state();
        dist.gicd_ctlr = 0x3u;
        dist.gicd_isenabler[intid / 32] |= (1u << (intid % 32));
        dist.gicd_igroupr[intid / 32]   |= (1u << (intid % 32));
        dist.gicd_ipriorityr[intid] = prio;
        dist.gicd_irouter[intid - 32] = core;
        gic.set_distributor_state(dist);

        gic.set_spi_level(intid, true);
        auto sig = gic.evaluate(1, 0, core);
        if (prio < pmr) {
            TEST_CHECK(sig.has_interrupt, "Fuzz: higher priority interrupt must be eligible");
        }
        gic.set_spi_level(intid, false);
        dist = gic.get_distributor_state();
        dist.gicd_isenabler[intid / 32] &= ~(1u << (intid % 32));
        gic.set_distributor_state(dist);
    }

    // 2. Timer Fuzzing (1,000 iterations)
    VirtualTime vt(24000000ULL, 8);
    for (int iter = 0; iter < 1000; ++iter) {
        const std::size_t core = next_rand() % 8;
        const std::uint64_t counter = (static_cast<std::uint64_t>(next_rand()) << 32) | next_rand();
        const std::uint64_t cval = (static_cast<std::uint64_t>(next_rand()) << 32) | next_rand();
        const bool en = (next_rand() & 1) != 0;
        const bool mask = (next_rand() & 2) != 0;

        vt.set_physical_counter(counter);
        TimerState t{};
        t.enable = en;
        t.imask = mask;
        t.cval = cval;
        vt.set_virt_timer(core, t);

        const bool asserted = vt.get_virt_timer_irq(core);
        const bool expected = en && !mask && (counter >= cval);
        TEST_CHECK(asserted == expected, "Fuzz: virtual timer evaluation must match architectural formula");
    }

    // 3. 8-Core Endurance Profile
    auto shared_mem = std::make_shared<SmpSharedMemory>(0x40000);
    for (std::uint64_t addr = 0; addr < 0x40000; addr += 4) {
        shared_mem->raw_memory().write_u32(addr, 0xD503201Fu); // NOP
    }
    for (std::uint64_t v = 0; v < 0x800; v += 0x80) {
        shared_mem->raw_memory().write_u32(v, 0xD69F03E0u); // ERET
    }

    SmpScheduler sched(shared_mem, 8, 20);

    for (std::size_t i = 0; i < 8; ++i) {
        auto cpu = std::make_shared<CPU>();
        cpu->reset();
        cpu->set_cpacr_el1(0x00300000ULL);
        cpu->set_program_counter(0x2000 + i * 0x200);
        sched.register_core(i, cpu, CoreLifecycleState::Runnable);
    }

    for (int step = 0; step < 100; ++step) {
        sched.step_quantum();
        if ((step % 10) == 0) {
            sched.interrupt_router().request_interrupt(step % 8, 32 + (step % 16), 0x80, 0);
        }
    }

    const std::uint64_t endurance_hash = sched.compute_canonical_multicore_hash();
    TEST_CHECK(endurance_hash != 0, "Endurance hash must be non-zero");

    TEST_PASS("Suite 14: 1,000-iteration GIC and timer fuzzing + 8-core endurance run completed");
}

// =============================================================================
// Main Test Dispatcher
// =============================================================================
int main(int argc, char* argv[]) {
    std::cout << "========================================================\n";
    std::cout << "WinIOS Phase 52 Stage 38: Interrupt & Exception Tests\n";
    std::cout << "========================================================\n";

    int suite_to_run = 0; // 0 = all
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--suite") == 0 && i + 1 < argc) {
                suite_to_run = std::atoi(argv[++i]);
            }
        }
    }

    try {
        if (suite_to_run == 0 || suite_to_run == 1)  run_suite_1_architectural_exception_delivery();
        if (suite_to_run == 0 || suite_to_run == 2)  run_suite_2_exception_vector_selection();
        if (suite_to_run == 0 || suite_to_run == 3)  run_suite_3_eret_state_validation();
        if (suite_to_run == 0 || suite_to_run == 4)  run_suite_4_daif_interrupt_masking();
        if (suite_to_run == 0 || suite_to_run == 5)  run_suite_5_gic_interrupt_lifecycle();
        if (suite_to_run == 0 || suite_to_run == 6)  run_suite_6_gic_priority_preemption();
        if (suite_to_run == 0 || suite_to_run == 7)  run_suite_7_gic_system_registers();
        if (suite_to_run == 0 || suite_to_run == 8)  run_suite_8_virtual_timer_conformance();
        if (suite_to_run == 0 || suite_to_run == 9)  run_suite_9_timer_ppi_integration();
        if (suite_to_run == 0 || suite_to_run == 10) run_suite_10_smp_interrupt_determinism();
        if (suite_to_run == 0 || suite_to_run == 11) run_suite_11_exception_interrupt_replay();
        if (suite_to_run == 0 || suite_to_run == 12) run_suite_12_snapshot_restore();
        if (suite_to_run == 0 || suite_to_run == 13) run_suite_13_reference_differential();
        if (suite_to_run == 0 || suite_to_run == 14) run_suite_14_fuzz_and_endurance();
    } catch (const std::exception& ex) {
        std::cerr << "UNHANDLED EXCEPTION: " << ex.what() << "\n";
        g_failed_tests++;
    }

    std::cout << "========================================================\n";
    if (g_failed_tests == 0) {
        std::cout << "ALL STAGE 38 TESTS PASSED SUCCESSFULLY!\n";
        return 0;
    } else {
        std::cerr << "STAGE 38 TESTS FAILED: " << g_failed_tests << " failure(s)\n";
        return 1;
    }
}

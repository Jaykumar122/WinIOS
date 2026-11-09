#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/virtual_time.h"
#include "device/apple_aic.h"
#include "memory/memory.h"

#define TIMER_TEST_ASSERT(cond, msg) \
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
// Test 1: Apple A16 Bionic (T8120) 24 MHz Timebase Frequency Conformance
// -----------------------------------------------------------------------------
bool test_timer_frequency_conformance() {
    std::cout << "[RUN] Test 1: Apple 24 MHz Timebase Frequency Standard ... ";

    VirtualTime vt;
    TIMER_TEST_ASSERT(vt.get_frequency() == 24000000ULL, "Default frequency must be 24 MHz for Apple Silicon");

    CPU cpu;
    cpu.reset();
    cpu.bind_virtual_time(&vt, 0);

    TIMER_TEST_ASSERT(cpu.get_cntfrq_el0() == 24000000ULL, "CPU CNTFRQ_EL0 must return 24 MHz");

    // Execute MRS X0, CNTFRQ_EL0 (0xD53BE000)
    Memory mem(0x200000);
    mem.write_u32(0x1000ULL, 0xD53BE000u); // MRS X0, CNTFRQ_EL0
    cpu.set_program_counter(0x1000ULL);
    cpu.set_current_el(1);
    cpu.step(mem);

    TIMER_TEST_ASSERT(cpu.read_register(0) == 24000000ULL, "MRS X0, CNTFRQ_EL0 must load 24,000,000 into X0");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Physical Monotonic Counter (CNTPCT_EL0) Progression
// -----------------------------------------------------------------------------
bool test_physical_counter_progression() {
    std::cout << "[RUN] Test 2: Physical Counter (CNTPCT_EL0) Monotonicity ... ";

    VirtualTime vt;
    vt.set_physical_counter(100000ULL);

    TIMER_TEST_ASSERT(vt.get_physical_counter() == 100000ULL, "Initial counter value matches set value");

    // Advance clock ticks
    vt.advance_physical_counter(500ULL);
    TIMER_TEST_ASSERT(vt.get_physical_counter() == 100500ULL, "Counter must advance monotonically");

    vt.advance_physical_counter(12000000ULL); // Half a second at 24 MHz
    TIMER_TEST_ASSERT(vt.get_physical_counter() == 12100500ULL, "Counter correctly increments by 12M ticks");

    // Read via CPU MRS X1, CNTPCT_EL0 (0xD53BE021)
    CPU cpu;
    cpu.reset();
    cpu.bind_virtual_time(&vt, 0);
    cpu.set_current_el(1);

    Memory mem(0x200000);
    mem.write_u32(0x1000ULL, 0xD53BE021u); // MRS X1, CNTPCT_EL0
    cpu.set_program_counter(0x1000ULL);
    cpu.step(mem);

    TIMER_TEST_ASSERT(cpu.read_register(1) == 12100500ULL, "MRS X1, CNTPCT_EL0 must load current counter");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Read-Only Protection on Architectural Counters (MSR Traps)
// -----------------------------------------------------------------------------
bool test_counter_read_only_protection() {
    std::cout << "[RUN] Test 3: Counter Read-Only Protection (MSR Traps) ... ";

    VirtualTime vt;
    CPU cpu;
    cpu.reset();
    cpu.bind_virtual_time(&vt, 0);
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0xFFFFFE0008000000ULL);
    cpu.set_current_el(1);
    cpu.set_sp_sel(1);

    Memory mem(0x200000);
    // MSR CNTPCT_EL0, X0 (0xD51BE020) -> should raise undefined instruction exception
    mem.write_u32(0x2000ULL, 0xD51BE020u);
    cpu.set_program_counter(0x2000ULL);
    cpu.step(mem);

    TIMER_TEST_ASSERT(cpu.get_program_counter() == 0xFFFFFE0008000200ULL, "MSR CNTPCT_EL0 must trap to VBAR + 0x200");
    TIMER_TEST_ASSERT((cpu.get_esr_el1() >> 26u) == 0x00u, "ESR_EL1 must report Unknown/Undefined instruction");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Virtual Counter (CNTVCT_EL0) with CNTVOFF_EL2 Offset
// -----------------------------------------------------------------------------
bool test_virtual_counter_with_offset() {
    std::cout << "[RUN] Test 4: Virtual Counter (CNTVCT_EL0) with Virtual Offset ... ";

    VirtualTime vt;
    vt.set_physical_counter(50000000ULL);

    // Set virtual offset for Core 0: 10,000,000 ticks
    vt.set_cntvoff_el2(0, 10000000ULL);

    TIMER_TEST_ASSERT(vt.get_virtual_counter(0) == 40000000ULL, "CNTVCT must equal CNTPCT - CNTVOFF");

    // Core 1 has offset 0
    TIMER_TEST_ASSERT(vt.get_virtual_counter(1) == 50000000ULL, "Core 1 CNTVCT has no offset");

    // Read via CPU MRS X2, CNTVCT_EL0 (0xD53BE042)
    CPU cpu;
    cpu.reset();
    cpu.bind_virtual_time(&vt, 0);
    cpu.set_current_el(1);

    Memory mem(0x200000);
    mem.write_u32(0x1000ULL, 0xD53BE042u);
    cpu.set_program_counter(0x1000ULL);
    cpu.step(mem);

    TIMER_TEST_ASSERT(cpu.read_register(2) == 40000000ULL, "MRS X2, CNTVCT_EL0 must return virtual counter");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Physical Timer (CNTP) Programming via TVAL and CVAL
// -----------------------------------------------------------------------------
bool test_physical_timer_programming() {
    std::cout << "[RUN] Test 5: Physical Timer TVAL & CVAL Programming ... ";

    VirtualTime vt;
    vt.set_physical_counter(10000ULL);

    // Write TVAL = 5000 ticks on Core 0
    vt.write_phys_tval(0, 5000u);

    const auto& t = vt.get_phys_timer(0);
    TIMER_TEST_ASSERT(t.cval == 15000ULL, "CVAL must be set to Counter(10000) + TVAL(5000) = 15000");

    // Reading TVAL back should yield 5000
    TIMER_TEST_ASSERT(vt.read_phys_tval(0) == 5000, "TVAL readback must be 5000");

    // Advance clock by 2000 ticks
    vt.advance_physical_counter(2000ULL);
    TIMER_TEST_ASSERT(vt.read_phys_tval(0) == 3000, "TVAL must reflect remaining ticks (3000)");

    // Advance clock past expiration
    vt.advance_physical_counter(4000ULL); // Total 16000 ticks, CVAL is 15000
    TIMER_TEST_ASSERT(vt.read_phys_tval(0) == -1000, "TVAL after expiration is negative (-1000)");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Physical Timer Control Register (CNTP_CTL_EL0) & Interrupt Firing
// -----------------------------------------------------------------------------
bool test_physical_timer_ctl_and_irq() {
    std::cout << "[RUN] Test 6: Physical Timer Control (ENABLE, IMASK, ISTATUS) ... ";

    VirtualTime vt;
    vt.set_physical_counter(20000ULL);

    // Set CVAL to 25000
    TimerState t{};
    t.cval = 25000ULL;
    t.enable = true;
    t.imask = false;
    vt.set_phys_timer(0, t);

    // Initially Counter < CVAL -> no IRQ
    TIMER_TEST_ASSERT(!vt.get_phys_timer(0).istatus, "Timer not yet expired");
    TIMER_TEST_ASSERT(!vt.get_phys_timer_irq(0), "No IRQ should be asserted");

    // Advance to 25000 -> Expired & IRQ asserted
    vt.advance_physical_counter(5000ULL);
    TIMER_TEST_ASSERT(vt.get_phys_timer(0).istatus, "ISTATUS must be set when Counter >= CVAL");
    TIMER_TEST_ASSERT(vt.get_phys_timer_irq(0), "IRQ must be asserted when enabled and unmasked");

    // Set IMASK = true -> ISTATUS remains true, but IRQ is masked
    t = vt.get_phys_timer(0);
    t.imask = true;
    vt.set_phys_timer(0, t);
    TIMER_TEST_ASSERT(vt.get_phys_timer(0).istatus, "ISTATUS remains set");
    TIMER_TEST_ASSERT(!vt.get_phys_timer_irq(0), "IRQ must be masked when IMASK=1");

    // Clear IMASK -> IRQ reasserts
    t.imask = false;
    vt.set_phys_timer(0, t);
    TIMER_TEST_ASSERT(vt.get_phys_timer_irq(0), "IRQ reasserts when IMASK=0");

    // Disable timer -> IRQ cleared
    t.enable = false;
    vt.set_phys_timer(0, t);
    TIMER_TEST_ASSERT(!vt.get_phys_timer_irq(0), "IRQ deasserted when ENABLE=0");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Virtual Timer (CNTV) Lifecycle & TVAL Calculation
// -----------------------------------------------------------------------------
bool test_virtual_timer_lifecycle() {
    std::cout << "[RUN] Test 7: Virtual Timer (CNTV) Lifecycle & TVAL ... ";

    VirtualTime vt;
    vt.set_physical_counter(100000ULL);
    vt.set_cntvoff_el2(0, 20000ULL); // Virtual counter is 80000

    // Write TVAL = 10000 on virtual timer
    vt.write_virt_tval(0, 10000u);
    const auto& virt = vt.get_virt_timer(0);
    TIMER_TEST_ASSERT(virt.cval == 90000ULL, "Virtual CVAL must be VirtualCounter(80000) + TVAL(10000) = 90000");

    // Enable virtual timer
    TimerState t = virt;
    t.enable = true;
    t.imask = false;
    vt.set_virt_timer(0, t);

    TIMER_TEST_ASSERT(!vt.get_virt_timer_irq(0), "Virtual IRQ not yet asserted");

    // Advance physical counter by 10000 (virtual counter becomes 90000)
    vt.advance_physical_counter(10000ULL);
    TIMER_TEST_ASSERT(vt.get_virt_timer_irq(0), "Virtual IRQ must assert at virtual counter = 90000");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: AIC2 System Timer Interrupt Preemption & EOI Lifecycle
// -----------------------------------------------------------------------------
bool test_aic2_timer_integration() {
    std::cout << "[RUN] Test 8: AIC2 Timer Interrupt Preemption & Delivery ... ";

    AppleAIC aic(0x41494332u, AppleAIC::kDefaultAicBase, AppleAIC::kDefaultAicSize, 6);
    VirtualTime vt;
    vt.set_physical_counter(1000ULL);

    // Setup virtual timer expiring at 2000
    vt.write_virt_tval(0, 1000u);
    auto t = vt.get_virt_timer(0);
    t.enable = true;
    t.imask = false;
    vt.set_virt_timer(0, t);

    // Also assert a hardware peripheral IRQ (IRQ 33) and IPI on Core 0
    aic.unmask_hw_irq(33);
    aic.set_irq_target(33, 0x01);
    aic.set_hw_irq(33, true);
    aic.send_ipi(0x01);

    // Advance timer to expiration
    vt.advance_physical_counter(1000ULL);
    TIMER_TEST_ASSERT(vt.get_virt_timer_irq(0), "Virtual timer is asserted");

    // Signal AIC that timer interrupt is asserted on Core 0
    aic.set_timer_irq(0, vt.get_virt_timer_irq(0));
    TIMER_TEST_ASSERT(aic.has_pending_interrupt(0), "AIC must report pending interrupt");

    // Check that AIC pops Timer event (0x0002) first, preempting IPI and HW IRQ
    std::uint32_t ev = aic.pop_event(0);
    std::uint32_t ev_type = (ev >> 16) & 0xFFFFu;
    TIMER_TEST_ASSERT(ev_type == AppleAIC::kEventTypeTimer, "Timer interrupt must preempt all others");

    // Acknowledge EOI for Timer
    aic.acknowledge_eoi(0, ev);

    // Next popped event should be IPI
    ev = aic.pop_event(0);
    ev_type = (ev >> 16) & 0xFFFFu;
    TIMER_TEST_ASSERT(ev_type == AppleAIC::kEventTypeIpi, "Next event is IPI");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 9: Idle Clock Fast-Forward (WFI Earliest Deadline Advancement)
// -----------------------------------------------------------------------------
bool test_idle_deadline_advancement() {
    std::cout << "[RUN] Test 9: WFI Earliest Timer Deadline Fast-Forward ... ";

    VirtualTime vt;
    vt.set_physical_counter(1000ULL);

    // Configure Core 0 timer to expire at 50,000
    TimerState t0{};
    t0.cval = 50000ULL;
    t0.enable = true;
    t0.imask = false;
    vt.set_phys_timer(0, t0);

    // Configure Core 1 timer to expire at 30,000
    TimerState t1{};
    t1.cval = 30000ULL;
    t1.enable = true;
    t1.imask = false;
    vt.set_phys_timer(1, t1);

    std::vector<bool> active_mask = {true, true, false, false, false, false};

    // Earliest deadline across active cores should be 30,000
    const std::uint64_t earliest = vt.get_earliest_future_deadline(active_mask);
    TIMER_TEST_ASSERT(earliest == 30000ULL, "Earliest deadline must be 30,000 (Core 1)");

    // Advance to deadline
    TIMER_TEST_ASSERT(vt.advance_to_earliest_deadline(active_mask), "Advance to earliest deadline succeeds");
    TIMER_TEST_ASSERT(vt.get_physical_counter() == 30000ULL, "Physical counter jumped directly to 30,000");
    TIMER_TEST_ASSERT(vt.get_phys_timer_irq(1), "Core 1 timer must be expired now");
    TIMER_TEST_ASSERT(!vt.get_phys_timer_irq(0), "Core 0 timer has not expired yet");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 10: EL0 Trapping Policy (CNTKCTL_EL1)
// -----------------------------------------------------------------------------
bool test_el0_counter_trapping() {
    std::cout << "[RUN] Test 10: EL0 Counter Trapping Verification (CNTKCTL_EL1) ... ";

    VirtualTime vt;
    // Sysreg key for CNTPCT_EL0: (3, 3, 14, 0, 1) = 0x5701
    constexpr std::uint16_t kSysregCNTPCT = (1u << 14) | (3u << 11) | (14u << 7) | (0u << 3) | 1u;

    // By default CNTKCTL_EL1 has EL0PCTEN = 0 -> EL0 access should trap to EL1
    vt.set_cntkctl_el1(0, 0x00u);
    auto trap = vt.check_trap(0, 0 /* EL0 */, kSysregCNTPCT, true /* is_read */);
    TIMER_TEST_ASSERT(trap.trapped, "EL0 read to CNTPCT must trap when CNTKCTL_EL1.EL0PCTEN = 0");
    TIMER_TEST_ASSERT(trap.target_el == 1, "Trap target must be EL1");

    // Enable EL0PCTEN (bit 0 of CNTKCTL_EL1)
    vt.set_cntkctl_el1(0, 0x01u);
    trap = vt.check_trap(0, 0 /* EL0 */, kSysregCNTPCT, true /* is_read */);
    TIMER_TEST_ASSERT(!trap.trapped, "EL0 read must not trap when EL0PCTEN = 1");

    // EL1 access should never trap based on CNTKCTL_EL1
    vt.set_cntkctl_el1(0, 0x00u);
    trap = vt.check_trap(0, 1 /* EL1 */, kSysregCNTPCT, true /* is_read */);
    TIMER_TEST_ASSERT(!trap.trapped, "EL1 read never traps on CNTKCTL_EL1");

    std::cout << "PASS\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 110: System Timers & Monotonic Clock Conformance Suite   \n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int total = 10;

    if (test_timer_frequency_conformance()) ++passed;
    if (test_physical_counter_progression()) ++passed;
    if (test_counter_read_only_protection()) ++passed;
    if (test_virtual_counter_with_offset()) ++passed;
    if (test_physical_timer_programming()) ++passed;
    if (test_physical_timer_ctl_and_irq()) ++passed;
    if (test_virtual_timer_lifecycle()) ++passed;
    if (test_aic2_timer_integration()) ++passed;
    if (test_idle_deadline_advancement()) ++passed;
    if (test_el0_counter_trapping()) ++passed;

    std::cout << "=================================================================\n";
    std::cout << " Summary: " << passed << " / " << total << " tests passed.\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}

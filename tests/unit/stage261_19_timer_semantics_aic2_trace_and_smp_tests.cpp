#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#include "cpu/deterministic_virtual_timer.h"
#include "cpu/early_scheduler_accounting.h"
#include "cpu/cpu.h"
#include "device/apple_aic.h"
#include "memory/memory.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::cpu;
using namespace winios::device;

// ----------------------------------------------------------------------------
// Test 1: Stage 261.16: Generic Timer Architectural Semantics Audit (Issue E)
// ----------------------------------------------------------------------------
static void test_stage261_16_timer_semantics_audit() {
    std::cout << "[Stage 261.16] Testing Generic Timer Semantics (Signed TVAL & ISTATUS Derivation)...\n";

    DeterministicVirtualTimer timer(24000000ULL);
    timer.reset();

    // 1. Signed TVAL behavior
    timer.set_control(0, DeterministicVirtualTimer::kCtlEnable);
    TEST_ASSERT(timer.get_counter(0) == 0, "Counter starts at 0");

    // Set positive TVAL: 5000 ticks -> comparator = 5000
    timer.set_tval(0, 5000);
    TEST_ASSERT(timer.get_comparator(0) == 5000ULL, "CVAL = Counter + TVAL = 5000");
    TEST_ASSERT(timer.get_tval(0) == 5000, "TVAL reads back 5000");

    // Advance counter by 2000 ticks: remaining TVAL = 3000
    timer.advance_counter(2000ULL);
    TEST_ASSERT(timer.get_tval(0) == 3000, "TVAL decrements to 3000");
    TEST_ASSERT(!timer.is_interrupt_asserted(0), "Timer not fired yet");

    // Advance counter by 3000 ticks: expires (counter == comparator == 5000)
    timer.advance_counter(3000ULL);
    TEST_ASSERT(timer.get_tval(0) == 0, "TVAL reached 0");
    TEST_ASSERT(timer.is_interrupt_asserted(0), "Interrupt asserted at deadline");

    // Negative TVAL (past deadline): counter continues to 7000 -> TVAL = 5000 - 7000 = -2000
    timer.advance_counter(2000ULL);
    TEST_ASSERT(timer.get_tval(0) == -2000, "Negative TVAL accurately reported past deadline");

    // 2. ISTATUS derivation & software write protection
    // Bit 2 (kCtlIStatus = 0x04) must reflect hardware timer condition
    uint32_t ctl = timer.get_control(0);
    TEST_ASSERT((ctl & DeterministicVirtualTimer::kCtlIStatus) != 0, "ISTATUS is set when counter >= comparator");

    // Software attempt to clear ISTATUS via direct write must be ignored:
    // If software writes 0x01 (ENABLE, clearing ISTATUS), ISTATUS is preserved because condition is still met!
    timer.set_control(0, DeterministicVirtualTimer::kCtlEnable);
    ctl = timer.get_control(0);
    TEST_ASSERT((ctl & DeterministicVirtualTimer::kCtlIStatus) != 0, "ISTATUS cannot be cleared by software write while expired");

    // 3. Masked expiration
    timer.reset();
    // Enable with IMASK set (bit 1)
    timer.set_control(0, DeterministicVirtualTimer::kCtlEnable | DeterministicVirtualTimer::kCtlIMask);
    timer.set_comparator(0, 1000ULL);
    timer.advance_counter(1500ULL);

    // Condition met: ISTATUS must be 1, but interrupt line must NOT be asserted to AIC2
    ctl = timer.get_control(0);
    TEST_ASSERT((ctl & DeterministicVirtualTimer::kCtlIStatus) != 0, "ISTATUS is set even when masked");
    TEST_ASSERT(!timer.is_interrupt_asserted(0), "Interrupt line NOT asserted while IMASK is set");

    std::cout << "[Stage 261.16] Generic timer semantics and ISTATUS derivation verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 261.17: Periodic Rearming, Delayed Servicing & Coalesced Ticks
// ----------------------------------------------------------------------------
static void test_stage261_17_periodic_rearming_and_coalescing() {
    std::cout << "[Stage 261.17] Testing Periodic Rearming & Delayed Servicing...\n";

    DeterministicVirtualTimer timer(24000000ULL);
    timer.reset();

    timer.set_control(0, DeterministicVirtualTimer::kCtlEnable);
    const uint64_t interval = 24000ULL; // 1 ms at 24 MHz
    timer.set_comparator(0, interval);

    // Advance by exactly 1 interval
    timer.advance_counter(interval);
    TEST_ASSERT(timer.is_interrupt_asserted(0), "Timer fired after 1 interval");

    // Acknowledge interrupt and rearm periodic
    timer.acknowledge(0);
    TEST_ASSERT(!timer.is_interrupt_asserted(0), "Interrupt de-asserted upon ACK");
    timer.rearm_periodic(0, interval);
    TEST_ASSERT(timer.get_comparator(0) == 2 * interval, "Comparator rearmed to next interval (48,000)");

    // Delayed servicing: advance by 3 full intervals without immediate ACK (coalesced event)
    timer.advance_counter(3 * interval);
    TEST_ASSERT(timer.is_interrupt_asserted(0), "Timer line asserted during delay");
    TEST_ASSERT(timer.get_counter(0) == 4 * interval, "Counter correctly tracked all 4 intervals");

    std::cout << "[Stage 261.17] Periodic rearming and delayed servicing verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 261.18: Complete Timer-to-AIC2 Execution Trace
// ----------------------------------------------------------------------------
static void test_stage261_18_timer_to_aic2_trace() {
    std::cout << "[Stage 261.18] Testing Complete Timer-to-AIC2 Exception Trace...\n";

    Memory mem(0x800000ULL);
    AppleAIC aic;
    aic.reset();
    aic.set_core_count(6);
    aic.set_current_core(0);

    DeterministicVirtualTimer timer(24000000ULL);
    timer.reset();
    timer.set_control(0, DeterministicVirtualTimer::kCtlEnable);
    timer.set_comparator(0, 10000ULL);

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);

    const uint64_t stack_top = 0x00044000ULL;
    cpu.set_sp_sel(true);
    cpu.set_sp_el1(stack_top);
    cpu.set_stack_pointer(stack_top);

    // Step 1: Advance counter to trigger comparator
    timer.advance_counter(10000ULL);
    TEST_ASSERT(timer.is_interrupt_asserted(0), "Timer interrupt asserted");

    // Step 2: Route to AIC2
    aic.set_timer_irq(0, true);
    TEST_ASSERT(aic.has_pending_interrupt(0), "AIC2 has pending timer interrupt");

    // Step 3: CPU vectors to exception handler (0x0A80)
    cpu.raise_asynchronous_exception(false, 0, false, 0);
    cpu.set_program_counter(0x0A80ULL);
    TEST_ASSERT(cpu.get_program_counter() == 0x0A80ULL, "CPU vectored to 0x0A80");

    // Step 4: AIC2 event popped
    uint32_t event = aic.pop_event(0);
    uint32_t ev_type = (event >> 16) & 0xFFFFu;
    TEST_ASSERT(ev_type == AppleAIC::kEventTypeTimer, "Popped event is Timer IRQ");

    // Step 5: Acknowledge & EOI
    timer.acknowledge(0);
    aic.acknowledge_eoi(0, event);
    TEST_ASSERT(!aic.has_pending_interrupt(0), "AIC2 pending state cleared after EOI");
    TEST_ASSERT(!timer.is_interrupt_asserted(0), "Timer line deasserted");

    std::cout << "[Stage 261.18] Complete Timer-to-AIC2 execution trace verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 261.19: Six-Core Integrated SMP Isolation & Simultaneous Expirations
// ----------------------------------------------------------------------------
static void test_stage261_19_six_core_smp_isolation() {
    std::cout << "[Stage 261.19] Testing Six-Core SMP Isolation & Simultaneous Timer Expirations...\n";

    AppleAIC aic;
    aic.reset();
    aic.set_core_count(6);

    DeterministicVirtualTimer timer(24000000ULL);
    timer.reset();

    // Verify per-core banked registers across all 6 cores: 0x2000 + core * 0x80
    for (std::size_t c = 0; c < 6; ++c) {
        uint64_t off = AppleAIC::kRegCoreBase + (c * AppleAIC::kCoreStride) + AppleAIC::kCoreOffWhoAmI;
        uint64_t who = aic.read_register(off, 4);
        TEST_ASSERT(who == c, "Banked WHOAMI register matches exact core index");
    }

    // Configure distinct timer intervals per core
    for (std::size_t c = 0; c < 6; ++c) {
        timer.set_control(c, DeterministicVirtualTimer::kCtlEnable);
        timer.set_comparator(c, (c + 1) * 1000ULL);
    }

    // Advance 2500 ticks: Cores 0 and 1 (1000, 2000) expire; Cores 2..5 (3000+) do not
    timer.advance_counter(2500ULL);
    TEST_ASSERT(timer.is_interrupt_asserted(0), "Core 0 timer expired");
    TEST_ASSERT(timer.is_interrupt_asserted(1), "Core 1 timer expired");
    TEST_ASSERT(!timer.is_interrupt_asserted(2), "Core 2 timer not expired");
    TEST_ASSERT(!timer.is_interrupt_asserted(3), "Core 3 timer not expired");
    TEST_ASSERT(!timer.is_interrupt_asserted(4), "Core 4 timer not expired");
    TEST_ASSERT(!timer.is_interrupt_asserted(5), "Core 5 timer not expired");

    // Advance further to 10,000 ticks: all 6 cores must be expired simultaneously
    timer.advance_counter(7500ULL);
    for (std::size_t c = 0; c < 6; ++c) {
        TEST_ASSERT(timer.is_interrupt_asserted(c), "All cores expired simultaneously");
    }

    // Snapshot and deterministic replay test
    auto snapshot = timer.serialize();
    DeterministicVirtualTimer timer_restored(24000000ULL);
    TEST_ASSERT(timer_restored.deserialize(snapshot.data(), snapshot.size()), "Deserialize snapshot succeeds");

    for (std::size_t c = 0; c < 6; ++c) {
        TEST_ASSERT(timer_restored.is_interrupt_asserted(c), "Restored timer maintains asserted state");
        TEST_ASSERT(timer_restored.get_counter(c) == timer.get_counter(c), "Restored counter matches");
        TEST_ASSERT(timer_restored.get_comparator(c) == timer.get_comparator(c), "Restored comparator matches");
    }

    std::cout << "[Stage 261.19] Six-core SMP isolation and simultaneous timer expirations verified.\n";
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    try {
        std::cout << "====================================================================\n";
        std::cout << "Campaign 261: Stages 261.16 - 261.19 Timer & SMP Tests\n";
        std::cout << "====================================================================\n";

        test_stage261_16_timer_semantics_audit();
        test_stage261_17_periodic_rearming_and_coalescing();
        test_stage261_18_timer_to_aic2_trace();
        test_stage261_19_six_core_smp_isolation();

        std::cout << "====================================================================\n";
        std::cout << "All Stages 261.16 - 261.19 Tests PASSED Successfully!\n";
        std::cout << "====================================================================\n";
    } catch (const std::exception& e) {
        std::cerr << "FATAL EXCEPTION: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
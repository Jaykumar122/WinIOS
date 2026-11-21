// ============================================================================
// Stage 246.14: Virtual Timer Architecture & Counter State Machine Tests
// Verifies virtual timer counter progression, CNTV_CVAL compare match,
// ISTATUS expiration, IMASK interrupt masking, and IRQ generation.
// ============================================================================

#include "cpu/virtual_time.h"
#include "device/virtual_platform.h"

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
using namespace winios::device;

static void test_virtual_counter_progression_and_cval() {
    std::cout << "[Stage 246.14 - Test 1] Virtual counter progression and compare match...\n";
    VirtualPlatformConfig config;
    VirtualPlatform platform(config);
    VirtualTime& vtime = platform.virtual_time();

    // 1. Initial counter state
    vtime.set_physical_counter(1000);
    TEST_ASSERT(vtime.get_virtual_counter(0) == 1000, "Virtual counter matches physical baseline");

    // 2. Configure virtual timer for core 0: CVAL = 1500, Enable = true, Imask = false
    TimerState t{};
    t.enable = true;
    t.imask = false;
    t.cval = 1500;
    vtime.set_virt_timer(0, t);

    // Before deadline: counter 1000 < 1500 -> no IRQ
    vtime.evaluate_core_timers(0);
    TEST_ASSERT(!vtime.get_virt_timer_irq(0), "No IRQ asserted before deadline");
    TEST_ASSERT(!vtime.get_virt_timer(0).istatus, "ISTATUS is 0 before deadline");

    // Advance counter to 1499
    vtime.advance_physical_counter(499);
    vtime.evaluate_core_timers(0);
    TEST_ASSERT(!vtime.get_virt_timer_irq(0), "No IRQ at CVAL - 1");

    // Advance counter to 1500 (Deadline reached)
    vtime.advance_physical_counter(1);
    vtime.evaluate_core_timers(0);
    TEST_ASSERT(vtime.get_virt_timer(0).istatus, "ISTATUS asserted at CVAL");
    TEST_ASSERT(vtime.get_virt_timer_irq(0), "IRQ asserted at CVAL");

    // Advance counter past deadline to 2000: IRQ remains asserted
    vtime.advance_physical_counter(500);
    vtime.evaluate_core_timers(0);
    TEST_ASSERT(vtime.get_virt_timer_irq(0), "IRQ remains asserted past deadline");
}

static void test_virtual_timer_mask_semantics() {
    std::cout << "[Stage 246.14 - Test 2] Virtual timer IMASK interrupt masking...\n";
    VirtualPlatformConfig config;
    VirtualPlatform platform(config);
    VirtualTime& vtime = platform.virtual_time();

    vtime.set_physical_counter(5000);

    // Configure core 0 timer: CVAL = 5000 (already expired), Enable = true, IMASK = true
    TimerState t{};
    t.enable = true;
    t.imask = true; // Masked!
    t.cval = 5000;
    vtime.set_virt_timer(0, t);

    vtime.evaluate_core_timers(0);
    TEST_ASSERT(vtime.get_virt_timer(0).istatus, "ISTATUS is set when counter >= CVAL");
    TEST_ASSERT(!vtime.get_virt_timer_irq(0), "IRQ is suppressed when IMASK is true");

    // Unmask timer
    t.imask = false;
    vtime.set_virt_timer(0, t);
    vtime.evaluate_core_timers(0);
    TEST_ASSERT(vtime.get_virt_timer_irq(0), "IRQ fires immediately once unmasked");

    // Disable timer
    t.enable = false;
    vtime.set_virt_timer(0, t);
    vtime.evaluate_core_timers(0);
    TEST_ASSERT(!vtime.get_virt_timer_irq(0), "IRQ is cleared when timer is disabled");
}

int main() {
    std::cout << "=== Running Stage 246.14 Virtual Timer Architecture Tests ===\n";
    test_virtual_counter_progression_and_cval();
    test_virtual_timer_mask_semantics();
    std::cout << "Stage 246.14: All virtual timer tests passed.\n";
    return 0;
}

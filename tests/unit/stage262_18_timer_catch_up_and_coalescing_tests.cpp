#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#include "cpu/deterministic_virtual_timer.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::cpu;

// ----------------------------------------------------------------------------
// Test 1: Stage 262.16 - Periodic Timer Deterministic Catch-Up Invariant
// ----------------------------------------------------------------------------
static void test_stage262_16_timer_catch_up_algorithm() {
    std::cout << "[Stage 262.16] Testing Deterministic Timer Periodic Catch-Up Algorithm...\n";

    DeterministicVirtualTimer timer(24000000ULL);
    timer.reset();

    const std::size_t core = 0;
    const uint64_t period = 240000ULL; // 10 ms at 24 MHz

    // Configure core 0 timer: Enable = 1, IMask = 0
    timer.set_control(core, DeterministicVirtualTimer::kCtlEnable);
    timer.set_tval(core, static_cast<int32_t>(period));

    // Case 1: Service on time (C == D)
    uint64_t cval1 = timer.get_comparator(core);
    timer.advance_counter(cval1 - timer.get_counter(core));
    TEST_ASSERT(timer.is_interrupt_asserted(core), "Interrupt asserted at deadline");

    auto res1 = timer.rearm_periodic(core, period);
    TEST_ASSERT(res1.elapsed_periods == 1, "On-time rearm: elapsed_periods == 1");
    TEST_ASSERT(res1.missed_ticks == 0, "On-time rearm: missed_ticks == 0");
    TEST_ASSERT(res1.next_deadline == cval1 + period, "Next deadline advanced by exactly 1 period");
    TEST_ASSERT(timer.missed_ticks(core) == 0, "Total missed ticks is 0");
    TEST_ASSERT(timer.get_comparator(core) > timer.get_counter(core), "Next deadline strictly in the future");

    // Case 2: One period late (C == D + period)
    uint64_t d2 = timer.get_comparator(core);
    uint64_t target2 = d2 + period;
    timer.advance_counter(target2 - timer.get_counter(core));
    TEST_ASSERT(timer.is_interrupt_asserted(core), "Interrupt asserted when delayed");

    auto res2 = timer.rearm_periodic(core, period);
    TEST_ASSERT(res2.elapsed_periods == 2, "1 period late: elapsed_periods == 2");
    TEST_ASSERT(res2.missed_ticks == 1, "1 period late: missed_ticks == 1");
    TEST_ASSERT(res2.next_deadline == d2 + 2 * period, "Next deadline advanced by 2 periods");
    TEST_ASSERT(timer.missed_ticks(core) == 1, "Cumulative missed ticks == 1");
    TEST_ASSERT(timer.get_comparator(core) > timer.get_counter(core), "Next deadline strictly in the future");

    // Case 3: Multiple periods late (e.g. 5.5 periods late)
    uint64_t d3 = timer.get_comparator(core);
    uint64_t target3 = d3 + 5 * period + (period / 2);
    timer.advance_counter(target3 - timer.get_counter(core));

    auto res3 = timer.rearm_periodic(core, period);
    TEST_ASSERT(res3.elapsed_periods == 6, "5.5 periods late: elapsed_periods == 6");
    TEST_ASSERT(res3.missed_ticks == 5, "5.5 periods late: missed_ticks == 5");
    TEST_ASSERT(res3.next_deadline == d3 + 6 * period, "Next deadline advanced by 6 periods");
    TEST_ASSERT(timer.missed_ticks(core) == 6, "Cumulative missed ticks == 1 + 5 = 6");
    TEST_ASSERT(timer.get_comparator(core) > timer.get_counter(core), "Next deadline strictly in the future");

    // Case 4: Massive delay (e.g. 10,000 periods late)
    uint64_t d4 = timer.get_comparator(core);
    uint64_t target4 = d4 + 10000ULL * period;
    timer.advance_counter(target4 - timer.get_counter(core));

    auto res4 = timer.rearm_periodic(core, period);
    TEST_ASSERT(res4.elapsed_periods == 10001, "10000 periods late: elapsed_periods == 10001");
    TEST_ASSERT(res4.missed_ticks == 10000, "10000 periods late: missed_ticks == 10000");
    TEST_ASSERT(res4.next_deadline == d4 + 10001ULL * period, "Next deadline advanced by 10001 periods");
    TEST_ASSERT(timer.missed_ticks(core) == 10006ULL, "Cumulative missed ticks == 6 + 10000 = 10006");
    TEST_ASSERT(timer.get_comparator(core) > timer.get_counter(core), "Next deadline strictly in the future");

    std::cout << "[Stage 262.16] Deterministic timer catch-up algorithm verified across all delay ranges.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 262.17 - Interrupt Event Coalescing Semantics
// ----------------------------------------------------------------------------
static void test_stage262_17_timer_coalescing_semantics() {
    std::cout << "[Stage 262.17] Testing Timer Interrupt Event Coalescing Semantics...\n";

    DeterministicVirtualTimer timer(24000000ULL);
    timer.reset();

    const std::size_t core = 1;
    const uint64_t period = 1000ULL;

    timer.set_control(core, DeterministicVirtualTimer::kCtlEnable);
    timer.set_tval(core, static_cast<int32_t>(period));

    // Let 3 periods elapse without service
    timer.advance_counter(3 * period);

    // Interrupt line must be asserted
    TEST_ASSERT(timer.is_interrupt_asserted(core), "Interrupt line asserted");

    // Stepping counter further while asserted maintains single asserted level (coalesced)
    timer.advance_counter(2 * period);
    TEST_ASSERT(timer.is_interrupt_asserted(core), "Interrupt line remains asserted (level-sensitive coalesced)");

    // Service timer via rearm_periodic
    auto res = timer.rearm_periodic(core, period);
    TEST_ASSERT(res.elapsed_periods >= 5, "Total elapsed periods coalesced");
    TEST_ASSERT(res.missed_ticks >= 4, "Missed ticks recorded");

    // After rearm, deadline is in future, interrupt line deasserted until next deadline
    TEST_ASSERT(!timer.is_interrupt_asserted(core), "Interrupt line deasserted after rearm");
    TEST_ASSERT(timer.get_comparator(core) > timer.get_counter(core), "New deadline is ahead of counter");

    std::cout << "[Stage 262.17] Interrupt event coalescing semantics verified.\n";
}

int main() {
    std::cout << "=== Running Stage 262.18: Timer Catch-Up & Coalescing Tests ===\n";
    test_stage262_16_timer_catch_up_algorithm();
    test_stage262_17_timer_coalescing_semantics();
    std::cout << "=== ALL STAGE 262.18 TESTS PASSED ===\n";
    return 0;
}

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"

using namespace winios;
using namespace winios::cpu;
using namespace winios::loader;
using namespace winios::runtime;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_timer_initialization() {
    std::cout << "[Test 1] Stage 207.15: ARM Generic Timer & Apple Frequency Conformance...\n";

    CPU cpu;

    // Apple Silicon canonical timer frequency is 24 MHz (24,000,000 Hz)
    constexpr std::uint64_t kAppleTimerFrequency = 24000000ULL;
    cpu.set_cntfrq_el0(kAppleTimerFrequency);
    TEST_CHECK(cpu.get_cntfrq_el0() == kAppleTimerFrequency, "CNTFRQ_EL0 set to 24 MHz");

    // Guest counter increment
    const std::uint64_t initial_count = cpu.get_guest_counter();
    cpu.increment_guest_counter();
    TEST_CHECK(cpu.get_guest_counter() == initial_count + 1, "Counter increments deterministically");

    // Physical timer control and value
    TimerState pt{};
    pt.enable = true;
    pt.imask = false;
    pt.cval = initial_count + 100;
    cpu.set_phys_timer(pt);

    const auto& pt_read = cpu.get_phys_timer();
    TEST_CHECK(pt_read.enable == true, "Physical timer enabled");
    TEST_CHECK(pt_read.cval == initial_count + 100, "Physical timer compare value programmed");

    // Event register for WFE wakeups
    cpu.set_event_register(true);
    TEST_CHECK(cpu.get_event_register() == true, "Event register set");
    cpu.set_event_register(false);
    TEST_CHECK(cpu.get_event_register() == false, "Event register cleared");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 207.15 Timer Initialization Tests ===\n";
    test_timer_initialization();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 207.15: All timer initialization tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 207.15: " << g_failures << " tests failed.\n";
        return 1;
    }
}

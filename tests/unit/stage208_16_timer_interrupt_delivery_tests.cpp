#include <iostream>
#include <cstdint>

#include "cpu/cpu.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"

using namespace winios;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_timer_interrupt_delivery() {
    std::cout << "[Test 1] Stage 208.16: Timer Interrupt Delivery & Wakeup Tests...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x100000);

    // Unmask interrupts: DAIF = 0
    cpu.set_daif(0);
    cpu.set_current_el(1);

    // 1. Configure architectural timer
    // Virtual Timer: Enable timer (enable = true, imask = false), cval = now + 5 cycles
    const std::uint64_t now = cpu.get_guest_counter();
    cpu.set_virt_timer(winios::TimerState{true, false, false, now + 5ULL});

    // Put CPU into WFI state
    cpu.set_waiting_for_interrupt(true);
    TEST_CHECK(cpu.is_waiting_for_interrupt(), "CPU placed into WFI wait state");

    // Advance time past timer deadline
    for (int cycle = 0; cycle < 10; ++cycle) {
        cpu.increment_guest_counter();
        cpu.poll_wait_wakeup();
        if (!cpu.is_waiting_for_interrupt()) break;
    }

    std::cout << "  -> WFI wake state: " << (cpu.is_waiting_for_interrupt() ? "WAITING" : "WOKEN") << "\n";
    TEST_CHECK(!cpu.is_waiting_for_interrupt(), "Timer interrupt woke CPU cleanly from WFI");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 208.16 Timer Interrupt Delivery Tests ===\n";
    test_timer_interrupt_delivery();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 208.16: All timer interrupt delivery tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 208.16: " << g_failures << " tests failed.\n";
        return 1;
    }
}

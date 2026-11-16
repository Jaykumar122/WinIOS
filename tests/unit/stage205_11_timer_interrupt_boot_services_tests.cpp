#include <cstdint>
#include <iostream>
#include <string>

#include "cpu/gicv3.h"
#include "cpu/kernel_boot_timer_interrupt.h"
#include "cpu/virtual_time.h"

using namespace winios;
using namespace winios::cpu;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_initialization() {
    std::cout << "[Test 1] Timer and interrupt boot initialization..." << std::endl;

    GICv3 gic;
    VirtualTime vt(24000000ULL, 6);
    KernelBootTimerInterruptManager mgr(gic, vt);

    KernelBootTimerConfig tcfg;
    tcfg.frequency_hz = 24000000ULL;
    tcfg.timer_ppi = 27U;
    tcfg.initial_tick_us = 10000ULL; // 10ms
    tcfg.enable_timer = true;

    KernelBootInterruptConfig icfg;
    icfg.num_cores = 6;
    icfg.priority_mask = 0xFF;
    icfg.enable_group1 = true;

    mgr.initialize_boot_services(tcfg, icfg);

    TEST_CHECK(gic.get_core_count() == 6, "GIC core count is 6");
    TEST_CHECK(vt.get_frequency() == 24000000ULL, "VirtualTime freq is 24MHz");
    TEST_CHECK(gic.read_icc_pmr(0) == 0xFF, "ICC_PMR_EL1 unmasked");
    TEST_CHECK(gic.read_icc_igrpen1(0) == 1, "ICC_IGRPEN1_EL1 enabled");

    // Initially at tick 0, timer has not expired yet
    auto sig = mgr.poll_interrupt(0, 1, 0);
    TEST_CHECK(!sig.has_interrupt, "No interrupt at time 0");

    std::cout << "  -> Initialization verified." << std::endl;
}

void test_virtual_timer_countdown_and_fire() {
    std::cout << "[Test 2] Virtual timer countdown and PPI 27 firing..." << std::endl;

    GICv3 gic;
    VirtualTime vt(24000000ULL, 6);
    KernelBootTimerInterruptManager mgr(gic, vt);

    KernelBootTimerConfig tcfg;
    tcfg.frequency_hz = 24000000ULL;
    tcfg.timer_ppi = 27U;
    tcfg.initial_tick_us = 1000ULL; // 1 ms
    tcfg.enable_timer = true;

    KernelBootInterruptConfig icfg;
    icfg.num_cores = 6;
    icfg.priority_mask = 0xFF;
    icfg.enable_group1 = true;

    mgr.initialize_boot_services(tcfg, icfg);

    // Advance 500 us (halfway)
    mgr.advance_time_us(500, 0);
    auto sig1 = mgr.poll_interrupt(0, 1, 0);
    TEST_CHECK(!sig1.has_interrupt, "No interrupt at 500us");

    // Advance another 600 us (total 1100us > 1000us)
    mgr.advance_time_us(600, 0);
    auto sig2 = mgr.poll_interrupt(0, 1, 0);
    TEST_CHECK(sig2.has_interrupt, "Interrupt asserted at 1100us");
    TEST_CHECK(sig2.intid == 27U, "Interrupt ID is PPI 27 (CNTV)");
    TEST_CHECK(!sig2.is_fiq, "Interrupt signaled as IRQ (Group 1)");

    std::cout << "  -> Timer countdown and fire verified." << std::endl;
}

void test_interrupt_acknowledge_and_eoi() {
    std::cout << "[Test 3] Interrupt acknowledge (IAR) and EOI handshake..." << std::endl;

    GICv3 gic;
    VirtualTime vt(24000000ULL, 6);
    KernelBootTimerInterruptManager mgr(gic, vt);

    KernelBootTimerConfig tcfg;
    tcfg.frequency_hz = 24000000ULL;
    tcfg.timer_ppi = 27U;
    tcfg.initial_tick_us = 1000ULL; // 1 ms
    tcfg.enable_timer = true;

    KernelBootInterruptConfig icfg;
    icfg.num_cores = 6;
    icfg.priority_mask = 0xFF;
    icfg.enable_group1 = true;

    mgr.initialize_boot_services(tcfg, icfg);

    // Fire timer
    mgr.advance_time_us(1500, 0);
    auto sig = mgr.poll_interrupt(0, 1, 0);
    TEST_CHECK(sig.has_interrupt, "Interrupt pending");

    // Kernel Acknowledges IRQ
    std::uint32_t ack_intid = mgr.acknowledge_irq(0, 1);
    TEST_CHECK(ack_intid == 27U, "Acknowledged INTID is 27");

    // Re-arm timer for next interval (clears timer output)
    mgr.rearm_timer_us(1000, 0);

    // Kernel signals End of Interrupt (EOI)
    mgr.end_of_interrupt(ack_intid, 0, 1);

    // Verify interrupt is fully cleared
    auto sig_after = mgr.poll_interrupt(0, 1, 0);
    TEST_CHECK(!sig_after.has_interrupt, "Interrupt cleared after re-arm and EOI");

    std::cout << "  -> Acknowledge and EOI handshake verified." << std::endl;
}

void test_physical_timer_ppi30() {
    std::cout << "[Test 4] Physical timer PPI 30 support..." << std::endl;

    GICv3 gic;
    VirtualTime vt(24000000ULL, 6);
    KernelBootTimerInterruptManager mgr(gic, vt);

    KernelBootTimerConfig tcfg;
    tcfg.frequency_hz = 24000000ULL;
    tcfg.timer_ppi = 30U; // CNTP
    tcfg.initial_tick_us = 2000ULL; // 2 ms
    tcfg.enable_timer = true;

    KernelBootInterruptConfig icfg;
    icfg.num_cores = 6;
    icfg.priority_mask = 0xFF;
    icfg.enable_group1 = true;

    mgr.initialize_boot_services(tcfg, icfg);

    mgr.advance_time_us(1000, 0);
    TEST_CHECK(!mgr.poll_interrupt(0, 1, 0).has_interrupt, "No IRQ at 1000us");

    mgr.advance_time_us(1500, 0); // 2500us > 2000us
    auto sig = mgr.poll_interrupt(0, 1, 0);
    TEST_CHECK(sig.has_interrupt, "IRQ asserted at 2500us");
    TEST_CHECK(sig.intid == 30U, "Interrupt ID is PPI 30 (CNTP)");

    std::uint32_t ack = mgr.acknowledge_irq(0, 1);
    TEST_CHECK(ack == 30U, "Acknowledged PPI 30");
    mgr.rearm_timer_us(2000, 0);
    mgr.end_of_interrupt(ack, 0, 1);
    TEST_CHECK(!mgr.poll_interrupt(0, 1, 0).has_interrupt, "Cleared after EOI");

    std::cout << "  -> Physical timer PPI 30 verified." << std::endl;
}

} // namespace

int main() {
    std::cout << "=== Stage 205.11 Timer & Interrupt Boot Services Tests ===" << std::endl;

    test_initialization();
    test_virtual_timer_countdown_and_fire();
    test_interrupt_acknowledge_and_eoi();
    test_physical_timer_ppi30();

    if (g_failures > 0) {
        std::cerr << "FAILED: " << g_failures << " tests failed.\n";
        return 1;
    }

    std::cout << "All Stage 205.11 tests PASSED." << std::endl;
    return 0;
}

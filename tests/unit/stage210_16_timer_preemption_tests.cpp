#include <iostream>
#include <vector>
#include <cstdint>

#include "cpu/cpu.h"
#include "kernel/preemption_timer.h"
#include "kernel/scheduler.h"
#include "kernel/thread.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::kernel;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_timer_driven_preemption() {
    std::cout << "[Test 1] Stage 210.16: Timer-Driven Preemption & Quantum Expiration...\n";

    CPU cpu;
    Memory mem(0x100000);
    KernelThreadManager mgr;
    PriorityScheduler sched(mgr, mem);
    TEST_CHECK(sched.initialize(), "Scheduler initialized");

    auto* t1 = mgr.create_thread("worker1", 0x2000ULL, 0, 0x4000ULL, 0x2000ULL, 16);
    auto* t2 = mgr.create_thread("worker2", 0x3000ULL, 0, 0x6000ULL, 0x2000ULL, 16);

    sched.enqueue_thread(t1);
    sched.enqueue_thread(t2);

    // Dispatch first thread
    TEST_CHECK(sched.dispatch_next(cpu), "Dispatch next ready thread");
    TEST_CHECK(sched.current_thread() == t1, "t1 selected and running");
    TEST_CHECK(t1->time_slice() == 10, "Initial time slice is 10 ticks");

    // Advance 9 ticks: no preemption yet
    for (int i = 0; i < 9; ++i) {
        TEST_CHECK(sched.tick(cpu), "Scheduler tick " + std::to_string(i + 1));
        TEST_CHECK(sched.current_thread() == t1, "t1 still running");
    }
    TEST_CHECK(t1->time_slice() == 1, "Remaining time slice is 1 tick");

    // 10th tick: quantum expires! Preemption triggers context switch to t2
    TEST_CHECK(sched.tick(cpu), "10th scheduler tick expires quantum");
    TEST_CHECK(sched.current_thread() == t2, "t2 is now running after preemption");
    TEST_CHECK(sched.stats().total_preemptions == 1, "Preemption counter incremented to 1");
    TEST_CHECK(t1->time_slice() == 10, "t1 quantum replenished to 10 ticks for next round");
}

void test_preemption_timer_guest_irq_handler() {
    std::cout << "[Test 2] Stage 210.16: PreemptionTimer Machine-Code IRQ Handler & Vector Table...\n";

    Memory mem(0x100000);
    const auto irq_handler = PreemptionTimer::emit_guest_irq_handler();
    TEST_CHECK(!irq_handler.empty(), "IRQ handler emitted");
    TEST_CHECK(irq_handler.size() == 15, "IRQ handler has 15 instructions");

    // Last instruction must be ERET (0xD69F03E0)
    TEST_CHECK(irq_handler.back() == 0xD69F03E0u, "Last instruction is ERET (0xD69F03E0)");

    // Vector table setup
    const std::uint64_t vbar_pa = 0x8000ULL;
    const std::uint64_t handler_pa = 0x9000ULL;
    TEST_CHECK(PreemptionTimer::setup_vector_table(mem, vbar_pa, handler_pa), "Setup vector table at VBAR");

    // Check branch instruction at VBAR offset 0x280 (Current EL with SPx IRQ)
    const std::uint32_t branch_insn = mem.read_u32(vbar_pa + 0x280);
    TEST_CHECK((branch_insn & 0xFC000000u) == 0x14000000u, "Valid B (branch) instruction at VBAR + 0x280");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 210.16 Timer Preemption Tests ===\n";
    test_timer_driven_preemption();
    test_preemption_timer_guest_irq_handler();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 210.16: All timer preemption tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 210.16: " << g_failures << " tests failed.\n";
        return 1;
    }
}

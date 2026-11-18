#include <iostream>
#include <vector>
#include <cstdint>
#include <cassert>

#include "cpu/cpu.h"
#include "kernel/thread.h"
#include "kernel/bsd_process.h"
#include "kernel/multi_process_scheduler.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;

static void test_state_transitions_and_invariants() {
    std::cout << "[Test 1] Scheduler state machine transitions & invariant checks...\n";
    Memory mem(0x1000000ULL);
    KernelThreadManager th_mgr;
    BsdProcessManager proc_mgr;
    TEST_ASSERT(proc_mgr.initialize());

    MultiProcessScheduler scheduler(th_mgr, proc_mgr, mem);
    TEST_ASSERT(scheduler.check_invariants());

    KernelThread* t1 = th_mgr.create_thread("t1", 0x1000, 1, 0x70000, 0x10000, 16, true);
    KernelThread* t2 = th_mgr.create_thread("t2", 0x2000, 1, 0x60000, 0x10000, 16, true);

    scheduler.enqueue_thread(t1);
    scheduler.enqueue_thread(t2);
    TEST_ASSERT(scheduler.check_invariants());
    TEST_ASSERT(scheduler.ready_count() == 2);

    CPU cpu;
    cpu.reset();
    TEST_ASSERT(scheduler.schedule_next(cpu));
    TEST_ASSERT(scheduler.current_thread() == t1);
    TEST_ASSERT(t1->state() == ThreadState::Running);
    TEST_ASSERT(t2->state() == ThreadState::Ready);
    TEST_ASSERT(scheduler.check_invariants());

    // Block current thread
    TEST_ASSERT(scheduler.block_thread(t1, WaitReason::Mutex));
    TEST_ASSERT(t1->state() == ThreadState::Blocked);
    TEST_ASSERT(t1->wait_reason() == WaitReason::Mutex);
    TEST_ASSERT(scheduler.current_thread() == nullptr);
    TEST_ASSERT(scheduler.check_invariants());

    // Schedule next -> t2 runs
    TEST_ASSERT(scheduler.schedule_next(cpu));
    TEST_ASSERT(scheduler.current_thread() == t2);
    TEST_ASSERT(t2->state() == ThreadState::Running);

    // Unblock t1 -> placed in ready queue
    TEST_ASSERT(scheduler.unblock_thread(t1));
    TEST_ASSERT(t1->state() == ThreadState::Ready);
    TEST_ASSERT(scheduler.ready_count() == 1);
    TEST_ASSERT(scheduler.check_invariants());
}

static void test_round_robin_fairness() {
    std::cout << "[Test 2] Round-robin fairness with 3 threads (no starvation)...\n";
    Memory mem(0x1000000ULL);
    KernelThreadManager th_mgr;
    BsdProcessManager proc_mgr;
    TEST_ASSERT(proc_mgr.initialize());

    MultiProcessScheduler scheduler(th_mgr, proc_mgr, mem);

    KernelThread* tA = th_mgr.create_thread("A", 0x1000, 1, 0x70000, 0x10000);
    KernelThread* tB = th_mgr.create_thread("B", 0x2000, 1, 0x60000, 0x10000);
    KernelThread* tC = th_mgr.create_thread("C", 0x3000, 1, 0x50000, 0x10000);

    scheduler.enqueue_thread(tA);
    scheduler.enqueue_thread(tB);
    scheduler.enqueue_thread(tC);

    CPU cpu;
    cpu.reset();

    std::vector<KernelThread*> execution_order;
    // Dispatch initial thread
    TEST_ASSERT(scheduler.schedule_next(cpu));
    execution_order.push_back(scheduler.current_thread());

    // Perform 6 voluntary yields
    for (int i = 0; i < 6; ++i) {
        TEST_ASSERT(scheduler.yield(cpu));
        execution_order.push_back(scheduler.current_thread());
    }

    // Expected sequence: A -> B -> C -> A -> B -> C -> A
    TEST_ASSERT(execution_order.size() == 7);
    TEST_ASSERT(execution_order[0] == tA);
    TEST_ASSERT(execution_order[1] == tB);
    TEST_ASSERT(execution_order[2] == tC);
    TEST_ASSERT(execution_order[3] == tA);
    TEST_ASSERT(execution_order[4] == tB);
    TEST_ASSERT(execution_order[5] == tC);
    TEST_ASSERT(execution_order[6] == tA);
    std::cout << "  Confirmed strict A -> B -> C -> A -> B -> C -> A fairness.\n";
}

static void test_quantum_preemption_and_zombie_reap() {
    std::cout << "[Test 3] Configurable quantum preemption & zombie cleanup...\n";
    Memory mem(0x1000000ULL);
    KernelThreadManager th_mgr;
    BsdProcessManager proc_mgr;
    TEST_ASSERT(proc_mgr.initialize());

    MultiProcessScheduler scheduler(th_mgr, proc_mgr, mem);
    scheduler.set_time_slice_quantum(2); // 2-tick quantum
    TEST_ASSERT(scheduler.time_slice_quantum() == 2);

    KernelThread* t1 = th_mgr.create_thread("q1", 0x1000, 1, 0x70000, 0x10000);
    KernelThread* t2 = th_mgr.create_thread("q2", 0x2000, 1, 0x60000, 0x10000);
    scheduler.enqueue_thread(t1);
    scheduler.enqueue_thread(t2);

    CPU cpu;
    cpu.reset();
    TEST_ASSERT(scheduler.schedule_next(cpu));
    TEST_ASSERT(scheduler.current_thread() == t1);

    // Tick 1: quantum remains 1
    TEST_ASSERT(scheduler.tick(cpu));
    TEST_ASSERT(scheduler.current_thread() == t1);

    // Tick 2: quantum expires -> preemption to t2!
    TEST_ASSERT(scheduler.tick(cpu));
    TEST_ASSERT(scheduler.current_thread() == t2);
    TEST_ASSERT(scheduler.stats().total_preemptions == 1);

    // Terminate t1 and reap
    TEST_ASSERT(scheduler.terminate_thread(t1, 0));
    TEST_ASSERT(t1->state() == ThreadState::Terminated);
    TEST_ASSERT(scheduler.reap_zombies() == 0); // Not in ready queue
}

int main() {
    std::cout << "=== WinIOS Stage 220.7 Scheduler State Machine & Fairness Tests ===\n";
    test_state_transitions_and_invariants();
    test_round_robin_fairness();
    test_quantum_preemption_and_zombie_reap();
    std::cout << "[PASS] All Stage 220.7 Scheduler State Machine & Fairness tests passed.\n";
    return 0;
}

#include <iostream>
#include <vector>
#include <cstdint>
#include <set>
#include "cpu/cpu.h"
#include "kernel/scheduler.h"
#include "kernel/smp_scheduler.h"
#include "kernel/sync.h"
#include "kernel/thread.h"
#include "memory/memory.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::kernel;

// -----------------------------------------------------------------------------
// Test F1: WaitQueue Zero Lost Wake-ups & FIFO/Priority Ordering (Acceptance Test F)
// -----------------------------------------------------------------------------
static bool test_wait_queue_wakeups() {
    KernelThreadManager thread_mgr;
    Memory mem(0x10000);
    PriorityScheduler sched(thread_mgr, mem);

    // Create 3 threads with different priorities
    auto* t1 = thread_mgr.create_thread("T1", 0x1000, 0, 0x4000, 0x1000, 10);
    auto* t2 = thread_mgr.create_thread("T2", 0x1000, 0, 0x5000, 0x1000, 20);
    auto* t3 = thread_mgr.create_thread("T3", 0x1000, 0, 0x6000, 0x1000, 30);

    TEST_ASSERT(t1 && t2 && t3, "Threads created");

    // 1. FIFO WaitQueue
    WaitQueue fifo_queue(WakePolicy::Fifo);
    t1->block(WaitReason::WaitQueue);
    t2->block(WaitReason::WaitQueue);
    t3->block(WaitReason::WaitQueue);

    fifo_queue.add_waiter(t1);
    fifo_queue.add_waiter(t2);
    fifo_queue.add_waiter(t3);

    TEST_ASSERT(fifo_queue.count() == 3, "All 3 waiters queued");

    // Wake one in FIFO -> must be t1
    auto* woken1 = fifo_queue.wake_one(sched);
    TEST_ASSERT(woken1 == t1, "FIFO wake_one returned first waiter (T1)");
    TEST_ASSERT(t1->state() == ThreadState::Ready, "T1 transitioned to Ready");
    TEST_ASSERT(fifo_queue.count() == 2, "2 waiters remaining");

    // Wake all remaining -> must wake exactly 2 threads (t2, t3)
    std::size_t woken_count = fifo_queue.wake_all(sched);
    TEST_ASSERT(woken_count == 2, "wake_all woke remaining 2 threads");
    TEST_ASSERT(t2->state() == ThreadState::Ready, "T2 transitioned to Ready");
    TEST_ASSERT(t3->state() == ThreadState::Ready, "T3 transitioned to Ready");
    TEST_ASSERT(fifo_queue.empty(), "WaitQueue is empty (zero lost wake-ups)");

    // 2. Priority-ordered WaitQueue
    WaitQueue prio_queue(WakePolicy::PriorityOrder);
    t1->block(WaitReason::WaitQueue); // prio 10
    t2->block(WaitReason::WaitQueue); // prio 20
    t3->block(WaitReason::WaitQueue); // prio 30

    prio_queue.add_waiter(t1);
    prio_queue.add_waiter(t2);
    prio_queue.add_waiter(t3);

    // Highest priority first -> t3 (prio 30)
    auto* pw1 = prio_queue.wake_one(sched);
    TEST_ASSERT(pw1 == t3, "Priority wake_one returned highest priority waiter (T3)");
    // Next -> t2 (prio 20)
    auto* pw2 = prio_queue.wake_one(sched);
    TEST_ASSERT(pw2 == t2, "Priority wake_one returned next highest waiter (T2)");
    // Next -> t1 (prio 10)
    auto* pw3 = prio_queue.wake_one(sched);
    TEST_ASSERT(pw3 == t1, "Priority wake_one returned lowest waiter (T1)");
    TEST_ASSERT(prio_queue.empty(), "Priority queue empty (zero lost wake-ups)");
    return true;
}

// -----------------------------------------------------------------------------
// Test G1: Scheduler State-Machine Transitions (Acceptance Test G)
// -----------------------------------------------------------------------------
static bool test_scheduler_state_machine() {
    KernelThreadManager thread_mgr;
    auto* t = thread_mgr.create_thread("Worker", 0x1000, 0, 0x4000, 0x1000);
    TEST_ASSERT(t->state() == ThreadState::Ready, "Initial state is Ready");

    // Ready -> Running (legal)
    TEST_ASSERT(t->transition_state(ThreadState::Running), "Ready -> Running is legal");
    TEST_ASSERT(t->state() == ThreadState::Running, "State is Running");

    // Running -> Blocked (legal)
    TEST_ASSERT(t->transition_state(ThreadState::Blocked), "Running -> Blocked is legal");
    TEST_ASSERT(t->state() == ThreadState::Blocked, "State is Blocked");

    // Blocked -> Running (ILLEGAL: must go through Ready!)
    TEST_ASSERT(!t->transition_state(ThreadState::Running), "Blocked -> Running is illegal");
    TEST_ASSERT(t->state() == ThreadState::Blocked, "State remains Blocked after rejected transition");

    // Blocked -> Ready (legal)
    TEST_ASSERT(t->transition_state(ThreadState::Ready), "Blocked -> Ready is legal");

    // Ready -> Running (legal)
    TEST_ASSERT(t->transition_state(ThreadState::Running), "Ready -> Running is legal");

    // Running -> Terminated (legal)
    TEST_ASSERT(t->transition_state(ThreadState::Terminated), "Running -> Terminated is legal");
    TEST_ASSERT(t->state() == ThreadState::Terminated, "State is Terminated");

    // Terminated -> Running (ILLEGAL: cannot revive terminated thread!)
    TEST_ASSERT(!t->transition_state(ThreadState::Running), "Terminated -> Running is illegal");
    TEST_ASSERT(!t->transition_state(ThreadState::Ready), "Terminated -> Ready is illegal");

    // Verify history recording
    const auto& history = t->state_history();
    TEST_ASSERT(history.size() >= 5, "State transition history tracked");
    return true;
}

// -----------------------------------------------------------------------------
// Test H1: No Duplicate Task Execution Across Cores (Acceptance Test H)
// -----------------------------------------------------------------------------
static bool test_smp_no_duplicate_execution() {
    constexpr std::size_t kCores = 4;
    KernelThreadManager thread_mgr;
    Memory mem(2 * 1024 * 1024);
    SmpScheduler smp(thread_mgr, mem, kCores);
    TEST_ASSERT(smp.initialize(0x50000, 0x4000, 0x10000), "SmpScheduler initialized");

    // Create 8 worker threads with all-core affinity
    std::vector<KernelThread*> threads;
    for (std::size_t i = 0; i < 8; ++i) {
        auto* th = thread_mgr.create_thread("Worker" + std::to_string(i),
                                            0x1000, 0, 0x10000 + i * 0x2000, 0x1000, 16);
        th->set_cpu_affinity(0xFULL); // eligible on cores 0..3
        threads.push_back(th);
        smp.enqueue_thread(th);
    }

    CPU dummy_cpu;
    dummy_cpu.set_current_el(1);

    // Simulate 100 scheduling rounds
    for (int round = 0; round < 100; ++round) {
        for (std::size_t c = 0; c < kCores; ++c) {
            smp.tick(c, dummy_cpu);
        }

        if (round % 10 == 0) {
            smp.balance_load();
        }

        // INVARIANT CHECK (Acceptance Test H):
        // Collect all non-null, non-idle current threads.
        // No thread can be active on more than one core!
        std::set<std::uint64_t> active_tids;
        for (std::size_t c = 0; c < kCores; ++c) {
            auto* curr = smp.current_thread(c);
            if (curr != nullptr && curr != smp.idle_thread(c)) {
                auto [it, inserted] = active_tids.insert(curr->tid());
                TEST_ASSERT(inserted, "Duplicate thread execution detected across SMP cores!");
            }
        }
    }
    return true;
}

int main() {
    std::cout << "[STAGE 196.9 & 196.10] Running WaitQueue & Scheduler Integrity Tests (Acceptance Tests F, G, H)..." << std::endl;

    if (!test_wait_queue_wakeups()) {
        std::cerr << "test_wait_queue_wakeups FAILED" << std::endl;
        return 1;
    }
    if (!test_scheduler_state_machine()) {
        std::cerr << "test_scheduler_state_machine FAILED" << std::endl;
        return 1;
    }
    if (!test_smp_no_duplicate_execution()) {
        std::cerr << "test_smp_no_duplicate_execution FAILED" << std::endl;
        return 1;
    }

    std::cout << "[STAGE 196.9 & 196.10] All WaitQueue & Scheduler Integrity Tests PASSED (Acceptance Tests F, G, H VERIFIED)." << std::endl;
    return 0;
}

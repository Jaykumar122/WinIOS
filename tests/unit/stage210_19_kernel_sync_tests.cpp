#include <iostream>
#include <vector>
#include <cstdint>

#include "cpu/cpu.h"
#include "kernel/scheduler.h"
#include "kernel/sync.h"
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

void test_kernel_spinlock() {
    std::cout << "[Test 1] Stage 210.19: KernelSpinlock Acquire, Release & Deadlock Detection...\n";

    Memory mem(0x10000);
    const std::uint64_t lock_addr = 0x1000ULL;
    KernelSpinlock lock(mem, lock_addr);
    lock.initialize();

    TEST_CHECK(!lock.is_locked(), "Initial lock state is unlocked");
    TEST_CHECK(lock.owner_tid() == 0, "Initial owner TID is 0");

    // Acquire lock by TID 10
    bool deadlock = false;
    TEST_CHECK(lock.try_acquire_host(10, &deadlock), "Acquire lock by TID 10");
    TEST_CHECK(lock.is_locked(), "Lock is locked");
    TEST_CHECK(lock.owner_tid() == 10, "Owner TID is 10");

    // Recursive acquire attempt by TID 10 (deadlock detection)
    TEST_CHECK(!lock.try_acquire_host(10, &deadlock), "Recursive acquisition rejected");
    TEST_CHECK(deadlock, "Deadlock flag set to true");

    // Contended acquire attempt by TID 20
    deadlock = false;
    TEST_CHECK(!lock.try_acquire_host(20, &deadlock), "Contended acquisition rejected");
    TEST_CHECK(!deadlock, "Not a deadlock for distinct TID");
    TEST_CHECK(lock.contention_count() == 1, "Contention count is 1");

    // Release lock by TID 10
    TEST_CHECK(lock.release_host(10), "Release lock by owner TID 10");
    TEST_CHECK(!lock.is_locked(), "Lock is now unlocked");

    // Releasing unlocked lock returns false
    TEST_CHECK(!lock.release_host(10), "Releasing unlocked lock rejected");
}

void test_irq_safe_spinlock() {
    std::cout << "[Test 2] Stage 210.19: IRQ-Safe Spinlock Guard (DAIF Masking)...\n";

    CPU cpu;
    Memory mem(0x10000);
    KernelSpinlock lock(mem, 0x2000ULL);
    lock.initialize();

    cpu.set_daif(0x0); // IRQs unmasked
    {
        IrqSafeSpinlockGuard guard(lock, cpu, 5);
        TEST_CHECK(guard.is_acquired(), "IrqSafeSpinlock acquired");
        TEST_CHECK(lock.is_locked(), "Lock is locked");
        TEST_CHECK((cpu.get_daif() & 0x2) != 0, "DAIF IRQ bit masked during critical section");
    }
    // After guard exit, lock is released and DAIF is restored
    TEST_CHECK(!lock.is_locked(), "Lock released after guard destruction");
    TEST_CHECK((cpu.get_daif() & 0x2) == 0, "DAIF IRQ bit unmasked back to original state");
}

void test_wait_queue_fifo_and_priority() {
    std::cout << "[Test 3] Stage 210.19: WaitQueue FIFO & Priority Wake Ordering...\n";

    Memory mem(0x100000);
    KernelThreadManager mgr;
    PriorityScheduler sched(mgr, mem);
    sched.initialize();

    auto* t1 = mgr.create_thread("t1", 0x1000ULL, 0, 0x4000ULL, 0x2000ULL, 10);
    auto* t2 = mgr.create_thread("t2", 0x1000ULL, 0, 0x6000ULL, 0x2000ULL, 25);
    auto* t3 = mgr.create_thread("t3", 0x1000ULL, 0, 0x8000ULL, 0x2000ULL, 15);

    // FIFO queue
    WaitQueue fifo_wq(WakePolicy::Fifo);
    fifo_wq.add_waiter(t1);
    fifo_wq.add_waiter(t2);
    fifo_wq.add_waiter(t3);
    TEST_CHECK(fifo_wq.count() == 3, "FIFO queue has 3 waiters");

    auto woken = fifo_wq.wake_one(sched);
    TEST_CHECK(woken == t1, "FIFO wakes t1 first");

    // Priority queue
    WaitQueue prio_wq(WakePolicy::PriorityOrder);
    prio_wq.add_waiter(t1);
    prio_wq.add_waiter(t2); // prio 25
    prio_wq.add_waiter(t3); // prio 15

    woken = prio_wq.wake_one(sched);
    TEST_CHECK(woken == t2, "Priority queue wakes highest priority thread (t2, prio 25) first");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 210.19 Kernel Sync Tests ===\n";
    test_kernel_spinlock();
    test_irq_safe_spinlock();
    test_wait_queue_fifo_and_priority();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 210.19: All kernel sync tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 210.19: " << g_failures << " tests failed.\n";
        return 1;
    }
}

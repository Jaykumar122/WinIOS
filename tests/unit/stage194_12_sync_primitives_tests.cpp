#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/scheduler.h"
#include "kernel/sync.h"
#include "kernel/thread.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::kernel;

static int g_pass = 0;
static int g_fail = 0;

#define EXPECT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #cond << "\n"; \
            ++g_fail; \
        } else { \
            ++g_pass; \
        } \
    } while (0)

#define EXPECT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #a " != " #b \
                      << " (" << (a) << " != " << (b) << ")\n"; \
            ++g_fail; \
        } else { \
            ++g_pass; \
        } \
    } while (0)

void test_spinlock_initialization() {
    std::cout << "Running test_spinlock_initialization...\n";
    EXPECT_EQ(sizeof(GuestSpinlockData), 16u);

    Memory mem(0x10000);
    const std::uint64_t lock_addr = 0x1000;
    KernelSpinlock lock(mem, lock_addr);

    EXPECT_TRUE(!lock.is_locked());
    EXPECT_EQ(lock.owner_tid(), 0u);
    EXPECT_EQ(lock.acquire_count(), 0u);
    EXPECT_EQ(lock.contention_count(), 0u);
}

void test_spinlock_mutual_exclusion_and_contention() {
    std::cout << "Running test_spinlock_mutual_exclusion_and_contention...\n";
    Memory mem(0x10000);
    const std::uint64_t lock_addr = 0x2000;
    KernelSpinlock lock(mem, lock_addr);

    // Thread 100 acquires
    bool acquired = lock.try_acquire_host(100);
    EXPECT_TRUE(acquired);
    EXPECT_TRUE(lock.is_locked());
    EXPECT_EQ(lock.owner_tid(), 100u);
    EXPECT_EQ(lock.acquire_count(), 1u);
    EXPECT_EQ(lock.contention_count(), 0u);

    // Thread 200 tries to acquire -> must fail and increment contention
    bool acquired_2 = lock.try_acquire_host(200);
    EXPECT_TRUE(!acquired_2);
    EXPECT_TRUE(lock.is_locked());
    EXPECT_EQ(lock.owner_tid(), 100u);
    EXPECT_EQ(lock.contention_count(), 1u);

    // Thread 200 cannot release lock held by Thread 100
    bool released_bad = lock.release_host(200);
    EXPECT_TRUE(!released_bad);
    EXPECT_TRUE(lock.is_locked());

    // Thread 100 releases
    bool released = lock.release_host(100);
    EXPECT_TRUE(released);
    EXPECT_TRUE(!lock.is_locked());
    EXPECT_EQ(lock.owner_tid(), 0u);

    // Thread 200 can now acquire
    bool acquired_2_now = lock.try_acquire_host(200);
    EXPECT_TRUE(acquired_2_now);
    EXPECT_TRUE(lock.is_locked());
    EXPECT_EQ(lock.owner_tid(), 200u);
    EXPECT_EQ(lock.acquire_count(), 2u);
}

void test_spinlock_recursive_deadlock_detection() {
    std::cout << "Running test_spinlock_recursive_deadlock_detection...\n";
    Memory mem(0x10000);
    const std::uint64_t lock_addr = 0x3000;
    KernelSpinlock lock(mem, lock_addr);

    bool acquired = lock.try_acquire_host(55);
    EXPECT_TRUE(acquired);

    // Re-acquire by same thread
    bool deadlock = false;
    bool acquired_again = lock.try_acquire_host(55, &deadlock);
    EXPECT_TRUE(!acquired_again);
    EXPECT_TRUE(deadlock);
    EXPECT_TRUE(lock.is_locked());
    EXPECT_EQ(lock.owner_tid(), 55u);
}

void test_guest_arm64_spinlock_execution() {
    std::cout << "Running test_guest_arm64_spinlock_execution...\n";
    Memory mem(0x20000);
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);

    const std::uint64_t lock_addr = 0x4000;
    const std::uint64_t try_acquire_code_addr = 0x1000;
    const std::uint64_t release_code_addr = 0x1100;

    KernelSpinlock spinlock(mem, lock_addr);

    // Emit try_acquire instructions into guest memory
    auto try_acquire_code = KernelSpinlock::emit_guest_spinlock_try_acquire();
    for (std::size_t i = 0; i < try_acquire_code.size(); ++i) {
        mem.write_u32(try_acquire_code_addr + i * 4, try_acquire_code[i]);
    }

    // Emit release instructions
    auto release_code = KernelSpinlock::emit_guest_spinlock_release();
    for (std::size_t i = 0; i < release_code.size(); ++i) {
        mem.write_u32(release_code_addr + i * 4, release_code[i]);
    }

    // 1. Thread 42 executes try_acquire (should succeed)
    cpu.set_program_counter(try_acquire_code_addr);
    cpu.write_register(0, lock_addr);
    cpu.write_register(1, 42ULL); // TID = 42
    cpu.write_register(30, 0x9000ULL); // Aligned return PC

    while (cpu.get_program_counter() != 0x9000ULL) {
        cpu.step(mem);
    }

    // X0 should be 0 (success)
    EXPECT_EQ(cpu.read_register(0), 0ULL);
    EXPECT_TRUE(spinlock.is_locked());
    EXPECT_EQ(spinlock.owner_tid(), 42u);

    // 2. Thread 99 executes try_acquire (should fail because lock is held)
    cpu.set_program_counter(try_acquire_code_addr);
    cpu.write_register(0, lock_addr);
    cpu.write_register(1, 99ULL);
    cpu.write_register(30, 0x9000ULL);

    while (cpu.get_program_counter() != 0x9000ULL) {
        cpu.step(mem);
    }

    // X0 should be 1 (failure)
    EXPECT_EQ(cpu.read_register(0), 1ULL);
    EXPECT_TRUE(spinlock.is_locked());
    EXPECT_EQ(spinlock.owner_tid(), 42u); // Still owned by 42

    // 3. Execute release
    cpu.set_program_counter(release_code_addr);
    cpu.write_register(0, lock_addr);
    cpu.write_register(30, 0x9000ULL);

    while (cpu.get_program_counter() != 0x9000ULL) {
        cpu.step(mem);
    }

    EXPECT_TRUE(!spinlock.is_locked());
    EXPECT_EQ(spinlock.owner_tid(), 0u);
}

void test_waitqueue_fifo() {
    std::cout << "Running test_waitqueue_fifo...\n";
    Memory mem(0x100000);
    KernelThreadManager mgr;
    PriorityScheduler sched(mgr, mem);
    sched.initialize(0x80000, 0x4000, 0x2000);

    KernelThread* t1 = mgr.create_thread("Worker1", 0x3000, 0, 0x40000, 0x4000, 16);
    KernelThread* t2 = mgr.create_thread("Worker2", 0x3000, 0, 0x50000, 0x4000, 16);
    KernelThread* t3 = mgr.create_thread("Worker3", 0x3000, 0, 0x60000, 0x4000, 16);

    WaitQueue wq(WakePolicy::Fifo);
    EXPECT_TRUE(wq.empty());

    wq.add_waiter(t1);
    t1->block(WaitReason::WaitQueue);
    wq.add_waiter(t2);
    t2->block(WaitReason::WaitQueue);
    wq.add_waiter(t3);
    t3->block(WaitReason::WaitQueue);

    EXPECT_EQ(wq.count(), 3u);
    EXPECT_TRUE(wq.contains(t2));

    // Wake one in FIFO order -> must be t1
    KernelThread* woken1 = wq.wake_one(sched);
    EXPECT_EQ(woken1, t1);
    EXPECT_EQ(static_cast<std::uint32_t>(t1->state()), static_cast<std::uint32_t>(ThreadState::Ready));
    EXPECT_EQ(wq.count(), 2u);

    // Wake one -> must be t2
    KernelThread* woken2 = wq.wake_one(sched);
    EXPECT_EQ(woken2, t2);
    EXPECT_EQ(static_cast<std::uint32_t>(t2->state()), static_cast<std::uint32_t>(ThreadState::Ready));

    // Wake one -> must be t3
    KernelThread* woken3 = wq.wake_one(sched);
    EXPECT_EQ(woken3, t3);
    EXPECT_EQ(static_cast<std::uint32_t>(t3->state()), static_cast<std::uint32_t>(ThreadState::Ready));

    EXPECT_TRUE(wq.empty());
    EXPECT_EQ(wq.wake_one(sched), nullptr);
}

void test_waitqueue_priority_order() {
    std::cout << "Running test_waitqueue_priority_order...\n";
    Memory mem(0x100000);
    KernelThreadManager mgr;
    PriorityScheduler sched(mgr, mem);
    sched.initialize(0x80000, 0x4000, 0x2000);

    KernelThread* t_low = mgr.create_thread("LowPri", 0x3000, 0, 0x40000, 0x4000, 5);
    KernelThread* t_high = mgr.create_thread("HighPri", 0x3000, 0, 0x50000, 0x4000, 28);
    KernelThread* t_med = mgr.create_thread("MedPri", 0x3000, 0, 0x60000, 0x4000, 15);

    WaitQueue wq(WakePolicy::PriorityOrder);
    wq.add_waiter(t_low);
    t_low->block(WaitReason::WaitQueue);
    wq.add_waiter(t_high);
    t_high->block(WaitReason::WaitQueue);
    wq.add_waiter(t_med);
    t_med->block(WaitReason::WaitQueue);

    // Wakes in priority order: High (28) -> Med (15) -> Low (5)
    KernelThread* w1 = wq.wake_one(sched);
    EXPECT_EQ(w1, t_high);

    KernelThread* w2 = wq.wake_one(sched);
    EXPECT_EQ(w2, t_med);

    KernelThread* w3 = wq.wake_one(sched);
    EXPECT_EQ(w3, t_low);

    EXPECT_TRUE(wq.empty());
}

void test_waitqueue_wake_all() {
    std::cout << "Running test_waitqueue_wake_all...\n";
    Memory mem(0x100000);
    KernelThreadManager mgr;
    PriorityScheduler sched(mgr, mem);
    sched.initialize(0x80000, 0x4000, 0x2000);

    std::vector<KernelThread*> threads;
    WaitQueue wq(WakePolicy::Fifo);

    for (int i = 0; i < 5; ++i) {
        KernelThread* t = mgr.create_thread("T" + std::to_string(i), 0x3000, 0, 0x40000 + i * 0x10000, 0x4000, 16);
        threads.push_back(t);
        t->block(WaitReason::WaitQueue);
        wq.add_waiter(t);
    }

    EXPECT_EQ(wq.count(), 5u);
    EXPECT_EQ(sched.ready_thread_count(), 0u);

    std::size_t woken_count = wq.wake_all(sched);
    EXPECT_EQ(woken_count, 5u);
    EXPECT_TRUE(wq.empty());
    EXPECT_EQ(sched.ready_thread_count(), 5u);

    for (auto* t : threads) {
        EXPECT_EQ(static_cast<std::uint32_t>(t->state()), static_cast<std::uint32_t>(ThreadState::Ready));
    }
}

int main() {
    std::cout << "=== WINIOS STAGE 194.12: KERNEL SYNCHRONIZATION PRIMITIVES TESTS ===\n";

    test_spinlock_initialization();
    test_spinlock_mutual_exclusion_and_contention();
    test_spinlock_recursive_deadlock_detection();
    test_guest_arm64_spinlock_execution();
    test_waitqueue_fifo();
    test_waitqueue_priority_order();
    test_waitqueue_wake_all();

    std::cout << "\nResults: " << g_pass << " passed, " << g_fail << " failed.\n";
    return g_fail == 0 ? 0 : 1;
}

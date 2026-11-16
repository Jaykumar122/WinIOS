#include <cstdint>
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>

#include "memory/memory.h"
#include "runtime/darwin_thread_runtime.h"
#include "runtime/darwin_sync_primitives.h"
#include "runtime/darwin_bsd_subsystem.h"

using namespace winios;
using namespace winios::runtime;

static int g_failed_tests = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAILED] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            g_failed_tests++; \
        } \
    } while (0)

static void test_mutex_basic_and_ownership() {
    std::cout << "[RUN] test_mutex_basic_and_ownership\n";
    Memory mem(0x100000);
    DarwinSyncManager sync;

    const std::uint64_t mutex_va = 0x20000;
    int res = sync.mutex_init(mem, mutex_va, PTHREAD_MUTEX_NORMAL);
    TEST_CHECK(res == DARWIN_SUCCESS, "mutex_init normal failed");

    DarwinPthreadMutexRaw raw{};
    std::memcpy(&raw, mem.raw_data() + mutex_va, sizeof(raw));
    TEST_CHECK(raw.sig == _PTHREAD_MUTEX_SIG_INIT, "raw sig mismatch");
    TEST_CHECK(raw.owner_tid == 0, "raw owner_tid not 0");

    // Lock by thread 101
    res = sync.mutex_lock(mem, mutex_va, 101);
    TEST_CHECK(res == DARWIN_SUCCESS, "mutex_lock tid 101 failed");

    std::memcpy(&raw, mem.raw_data() + mutex_va, sizeof(raw));
    TEST_CHECK(raw.owner_tid == 101, "raw owner_tid not 101");
    TEST_CHECK(raw.recursion_count == 1, "raw recursion_count not 1");

    // Unlock by thread 102 (invalid ownership)
    res = sync.mutex_unlock(mem, mutex_va, 102);
    TEST_CHECK(res == DARWIN_EPERM, "unlock by non-owner should return EPERM");

    // Trylock by thread 102 while held by 101
    res = sync.mutex_trylock(mem, mutex_va, 102);
    TEST_CHECK(res == DARWIN_EBUSY, "trylock on held mutex should return EBUSY");

    // Unlock by owner 101
    res = sync.mutex_unlock(mem, mutex_va, 101);
    TEST_CHECK(res == DARWIN_SUCCESS, "unlock by owner 101 failed");

    std::memcpy(&raw, mem.raw_data() + mutex_va, sizeof(raw));
    TEST_CHECK(raw.owner_tid == 0, "raw owner_tid not 0 after unlock");

    // Unlock unheld mutex
    res = sync.mutex_unlock(mem, mutex_va, 101);
    TEST_CHECK(res == DARWIN_EPERM, "unlock on unheld mutex should return EPERM");
}

static void test_mutex_recursive_and_errorcheck() {
    std::cout << "[RUN] test_mutex_recursive_and_errorcheck\n";
    Memory mem(0x100000);
    DarwinSyncManager sync;

    // Test Recursive Mutex
    const std::uint64_t rec_mutex_va = 0x30000;
    int res = sync.mutex_init(mem, rec_mutex_va, PTHREAD_MUTEX_RECURSIVE);
    TEST_CHECK(res == DARWIN_SUCCESS, "recursive mutex_init failed");

    res = sync.mutex_lock(mem, rec_mutex_va, 201);
    TEST_CHECK(res == DARWIN_SUCCESS, "rec lock 1 failed");
    res = sync.mutex_lock(mem, rec_mutex_va, 201);
    TEST_CHECK(res == DARWIN_SUCCESS, "rec lock 2 failed");

    DarwinPthreadMutexRaw raw{};
    std::memcpy(&raw, mem.raw_data() + rec_mutex_va, sizeof(raw));
    TEST_CHECK(raw.recursion_count == 2, "recursion count should be 2");

    res = sync.mutex_unlock(mem, rec_mutex_va, 201);
    TEST_CHECK(res == DARWIN_SUCCESS, "rec unlock 1 failed");
    std::memcpy(&raw, mem.raw_data() + rec_mutex_va, sizeof(raw));
    TEST_CHECK(raw.owner_tid == 201, "still owned after 1 unlock");
    TEST_CHECK(raw.recursion_count == 1, "recursion count should be 1");

    res = sync.mutex_unlock(mem, rec_mutex_va, 201);
    TEST_CHECK(res == DARWIN_SUCCESS, "rec unlock 2 failed");
    std::memcpy(&raw, mem.raw_data() + rec_mutex_va, sizeof(raw));
    TEST_CHECK(raw.owner_tid == 0, "unlocked after 2 unlocks");

    // Test Errorcheck Mutex
    const std::uint64_t err_mutex_va = 0x40000;
    res = sync.mutex_init(mem, err_mutex_va, PTHREAD_MUTEX_ERRORCHECK);
    TEST_CHECK(res == DARWIN_SUCCESS, "errorcheck mutex_init failed");

    res = sync.mutex_lock(mem, err_mutex_va, 202);
    TEST_CHECK(res == DARWIN_SUCCESS, "err lock 1 failed");

    // Relock by same thread must return EDEADLK
    res = sync.mutex_lock(mem, err_mutex_va, 202);
    TEST_CHECK(res == DARWIN_EDEADLK, "relocking errorcheck mutex must return EDEADLK");

    res = sync.mutex_unlock(mem, err_mutex_va, 202);
    TEST_CHECK(res == DARWIN_SUCCESS, "err unlock failed");
}

static void test_mutex_contention_and_deadlock_detection() {
    std::cout << "[RUN] test_mutex_contention_and_deadlock_detection\n";
    Memory mem(0x100000);
    DarwinThreadRuntime tr;
    DarwinSyncManager sync(&tr);

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(1, "/usr/bin/threadapp", 501);
    std::uint64_t t1 = tr.create_managed_thread(bsd, mem, pid, 0x1000, 0);
    std::uint64_t t2 = tr.create_managed_thread(bsd, mem, pid, 0x2000, 0);
    TEST_CHECK(t1 > 0 && t2 > 0, "threads created");

    const std::uint64_t mutex_a = 0x50000;
    const std::uint64_t mutex_b = 0x60000;
    TEST_CHECK(sync.mutex_init(mem, mutex_a) == DARWIN_SUCCESS, "init a");
    TEST_CHECK(sync.mutex_init(mem, mutex_b) == DARWIN_SUCCESS, "init b");

    // T1 locks A
    TEST_CHECK(sync.mutex_lock(mem, mutex_a, t1) == DARWIN_SUCCESS, "T1 locks A");
    // T2 locks B
    TEST_CHECK(sync.mutex_lock(mem, mutex_b, t2) == DARWIN_SUCCESS, "T2 locks B");

    // T1 attempts to lock B (owned by T2) -> contended / blocked
    int res = sync.mutex_lock(mem, mutex_b, t1);
    TEST_CHECK(res == DARWIN_EBUSY, "T1 waiting for B should return EBUSY");
    TEST_CHECK(tr.get_thread(t1)->state == DarwinThreadState::Blocked, "T1 should be blocked");

    // T2 attempts to lock A (owned by T1) -> cyclic deadlock detected!
    res = sync.mutex_lock(mem, mutex_a, t2);
    TEST_CHECK(res == DARWIN_EDEADLK, "Deadlock cycle must return EDEADLK");
    TEST_CHECK(sync.telemetry().mutex_deadlocks_detected >= 1, "telemetry should record deadlock");

    // Cleanup: T2 unlocks B, which unblocks T1 and transfers ownership of B to T1
    TEST_CHECK(sync.mutex_unlock(mem, mutex_b, t2) == DARWIN_SUCCESS, "T2 unlocks B");
    TEST_CHECK(tr.get_thread(t1)->state == DarwinThreadState::Running, "T1 should be unblocked");

    DarwinPthreadMutexRaw raw_b{};
    std::memcpy(&raw_b, mem.raw_data() + mutex_b, sizeof(raw_b));
    TEST_CHECK(raw_b.owner_tid == t1, "T1 now owns mutex B");

    // T1 unlocks both
    TEST_CHECK(sync.mutex_unlock(mem, mutex_a, t1) == DARWIN_SUCCESS, "T1 unlocks A");
    TEST_CHECK(sync.mutex_unlock(mem, mutex_b, t1) == DARWIN_SUCCESS, "T1 unlocks B");
}

static void test_mutex_destruction_validation() {
    std::cout << "[RUN] test_mutex_destruction_validation\n";
    Memory mem(0x100000);
    DarwinSyncManager sync;

    const std::uint64_t m_va = 0x70000;
    TEST_CHECK(sync.mutex_init(mem, m_va) == DARWIN_SUCCESS, "mutex_init");

    // Destroy unlocked mutex succeeds
    TEST_CHECK(sync.mutex_destroy(mem, m_va) == DARWIN_SUCCESS, "destroy unlocked mutex");

    // Re-init and lock
    TEST_CHECK(sync.mutex_init(mem, m_va) == DARWIN_SUCCESS, "re-init");
    TEST_CHECK(sync.mutex_lock(mem, m_va, 301) == DARWIN_SUCCESS, "lock 301");

    // Destroy locked mutex must fail with EBUSY
    TEST_CHECK(sync.mutex_destroy(mem, m_va) == DARWIN_EBUSY, "destroy locked mutex must return EBUSY");

    // Unlock and destroy
    TEST_CHECK(sync.mutex_unlock(mem, m_va, 301) == DARWIN_SUCCESS, "unlock");
    TEST_CHECK(sync.mutex_destroy(mem, m_va) == DARWIN_SUCCESS, "destroy after unlock");

    // Destroy already destroyed / invalid memory
    TEST_CHECK(sync.mutex_destroy(mem, m_va) == DARWIN_EINVAL, "destroy invalid sig returns EINVAL");
}

static void test_condvar_signal_and_broadcast() {
    std::cout << "[RUN] test_condvar_signal_and_broadcast\n";
    Memory mem(0x100000);
    DarwinThreadRuntime tr;
    DarwinSyncManager sync(&tr);

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(1, "/usr/bin/threadapp", 501);
    std::uint64_t t1 = tr.create_managed_thread(bsd, mem, pid, 0x1000, 0);
    std::uint64_t t2 = tr.create_managed_thread(bsd, mem, pid, 0x2000, 0);

    const std::uint64_t m_va = 0x80000;
    const std::uint64_t c_va = 0x81000;

    TEST_CHECK(sync.mutex_init(mem, m_va) == DARWIN_SUCCESS, "mutex_init");
    TEST_CHECK(sync.cond_init(mem, c_va) == DARWIN_SUCCESS, "cond_init");

    // T1 acquires mutex and waits on cond
    TEST_CHECK(sync.mutex_lock(mem, m_va, t1) == DARWIN_SUCCESS, "T1 locks m");
    TEST_CHECK(sync.cond_wait(mem, c_va, m_va, t1) == DARWIN_SUCCESS, "T1 waits on c");
    TEST_CHECK(tr.get_thread(t1)->state == DarwinThreadState::Blocked, "T1 blocked");

    // Mutex was atomically unlocked: T2 can now lock it
    TEST_CHECK(sync.mutex_lock(mem, m_va, t2) == DARWIN_SUCCESS, "T2 locks m");

    // Destroy cond with waiter must fail with EBUSY
    TEST_CHECK(sync.cond_destroy(mem, c_va) == DARWIN_EBUSY, "cond_destroy with waiter returns EBUSY");

    // T2 signals condition variable
    TEST_CHECK(sync.cond_signal(mem, c_va) == DARWIN_SUCCESS, "T2 signals c");
    TEST_CHECK(tr.get_thread(t1)->state == DarwinThreadState::Running, "T1 unblocked by signal");

    // T2 releases mutex (ownership transferred to T1)
    TEST_CHECK(sync.mutex_unlock(mem, m_va, t2) == DARWIN_SUCCESS, "T2 unlocks m");
    TEST_CHECK(sync.mutex_unlock(mem, m_va, t1) == DARWIN_SUCCESS, "T1 unlocks m");

    // Destroy empty cond succeeds
    TEST_CHECK(sync.cond_destroy(mem, c_va) == DARWIN_SUCCESS, "cond_destroy empty succeeds");

    // Broadcast test
    const std::uint64_t c2_va = 0x82000;
    TEST_CHECK(sync.cond_init(mem, c2_va) == DARWIN_SUCCESS, "cond2_init");

    TEST_CHECK(sync.mutex_lock(mem, m_va, t1) == DARWIN_SUCCESS, "T1 lock");
    TEST_CHECK(sync.cond_wait(mem, c2_va, m_va, t1) == DARWIN_SUCCESS, "T1 wait");

    TEST_CHECK(sync.mutex_lock(mem, m_va, t2) == DARWIN_SUCCESS, "T2 lock");
    TEST_CHECK(sync.cond_wait(mem, c2_va, m_va, t2) == DARWIN_SUCCESS, "T2 wait");

    DarwinPthreadCondRaw raw_c2{};
    std::memcpy(&raw_c2, mem.raw_data() + c2_va, sizeof(raw_c2));
    TEST_CHECK(raw_c2.waiter_count == 2, "c2 waiter_count == 2");

    // Broadcast wakes all
    TEST_CHECK(sync.cond_broadcast(mem, c2_va) == DARWIN_SUCCESS, "broadcast");
    TEST_CHECK(tr.get_thread(t1)->state == DarwinThreadState::Running, "T1 running after broadcast");
    TEST_CHECK(tr.get_thread(t2)->state == DarwinThreadState::Running, "T2 running after broadcast");
}

static void test_condvar_timedwait_timeout() {
    std::cout << "[RUN] test_condvar_timedwait_timeout\n";
    Memory mem(0x100000);
    DarwinSyncManager sync;

    const std::uint64_t m_va = 0x90000;
    const std::uint64_t c_va = 0x91000;

    TEST_CHECK(sync.mutex_init(mem, m_va) == DARWIN_SUCCESS, "m init");
    TEST_CHECK(sync.cond_init(mem, c_va) == DARWIN_SUCCESS, "c init");

    TEST_CHECK(sync.mutex_lock(mem, m_va, 501) == DARWIN_SUCCESS, "lock");
    int res = sync.cond_timedwait(mem, c_va, m_va, 501, 1000000); // 1ms timeout
    TEST_CHECK(res == DARWIN_ETIMEDOUT, "timedwait without signal should return ETIMEDOUT");
    TEST_CHECK(sync.telemetry().cond_timeouts >= 1, "telemetry cond_timeouts");
}

static void test_rwlock_shared_and_exclusive() {
    std::cout << "[RUN] test_rwlock_shared_and_exclusive\n";
    Memory mem(0x100000);
    DarwinSyncManager sync;

    const std::uint64_t rw_va = 0xA0000;
    TEST_CHECK(sync.rwlock_init(mem, rw_va) == DARWIN_SUCCESS, "rwlock_init");

    // T1 and T2 acquire shared rdlock
    TEST_CHECK(sync.rwlock_rdlock(mem, rw_va, 601) == DARWIN_SUCCESS, "T1 rdlock");
    TEST_CHECK(sync.rwlock_rdlock(mem, rw_va, 602) == DARWIN_SUCCESS, "T2 rdlock");

    DarwinPthreadRwlockRaw raw_rw{};
    std::memcpy(&raw_rw, mem.raw_data() + rw_va, sizeof(raw_rw));
    TEST_CHECK(raw_rw.reader_count == 2, "reader_count == 2");
    TEST_CHECK(raw_rw.state == 1, "state == 1 (read-locked)");

    // T3 tries to acquire exclusive wrlock -> fails with EBUSY
    TEST_CHECK(sync.rwlock_trywrlock(mem, rw_va, 603) == DARWIN_EBUSY, "trywrlock while reading returns EBUSY");

    // T4 tries to unlock (invalid caller) -> EPERM
    TEST_CHECK(sync.rwlock_unlock(mem, rw_va, 604) == DARWIN_EPERM, "unlock by non-holder returns EPERM");

    // T1 unlocks, T3 still cannot wrlock
    TEST_CHECK(sync.rwlock_unlock(mem, rw_va, 601) == DARWIN_SUCCESS, "T1 unlock");
    TEST_CHECK(sync.rwlock_trywrlock(mem, rw_va, 603) == DARWIN_EBUSY, "still held by T2");

    // T2 unlocks, now free
    TEST_CHECK(sync.rwlock_unlock(mem, rw_va, 602) == DARWIN_SUCCESS, "T2 unlock");

    // T3 acquires wrlock
    TEST_CHECK(sync.rwlock_wrlock(mem, rw_va, 603) == DARWIN_SUCCESS, "T3 wrlock");
    std::memcpy(&raw_rw, mem.raw_data() + rw_va, sizeof(raw_rw));
    TEST_CHECK(raw_rw.writer_tid == 603, "writer_tid == 603");
    TEST_CHECK(raw_rw.state == 2, "state == 2 (write-locked)");

    // T1 tries rdlock while write-locked -> EBUSY
    TEST_CHECK(sync.rwlock_tryrdlock(mem, rw_va, 601) == DARWIN_EBUSY, "tryrdlock while writing returns EBUSY");

    // Destroy while write-locked returns EBUSY
    TEST_CHECK(sync.rwlock_destroy(mem, rw_va) == DARWIN_EBUSY, "destroy locked rwlock returns EBUSY");

    // T3 unlocks and destroy succeeds
    TEST_CHECK(sync.rwlock_unlock(mem, rw_va, 603) == DARWIN_SUCCESS, "T3 unlock wrlock");
    TEST_CHECK(sync.rwlock_destroy(mem, rw_va) == DARWIN_SUCCESS, "destroy empty rwlock succeeds");
}

static void test_pthread_once() {
    std::cout << "[RUN] test_pthread_once\n";
    Memory mem(0x100000);
    DarwinSyncManager sync;

    const std::uint64_t once_va = 0xB0000;
    int counter = 0;
    auto init_fn = [&counter]() {
        counter++;
    };

    // First call by thread 701 executes init_fn
    int res = sync.pthread_once(mem, once_va, 701, init_fn);
    TEST_CHECK(res == DARWIN_SUCCESS, "pthread_once 1");
    TEST_CHECK(counter == 1, "counter should be 1");

    // Second call by thread 702 should be a no-op
    res = sync.pthread_once(mem, once_va, 702, init_fn);
    TEST_CHECK(res == DARWIN_SUCCESS, "pthread_once 2");
    TEST_CHECK(counter == 1, "counter should STILL be 1");

    DarwinPthreadOnceRaw raw_once{};
    std::memcpy(&raw_once, mem.raw_data() + once_va, sizeof(raw_once));
    TEST_CHECK(raw_once.sig == _PTHREAD_ONCE_SIG_INIT, "once sig match");
    TEST_CHECK(raw_once.status == 2, "once status == 2 (completed)");
    TEST_CHECK(sync.telemetry().once_executions == 1, "telemetry once_executions == 1");
}

static void test_thread_cancellation() {
    std::cout << "[RUN] test_thread_cancellation\n";
    Memory mem(0x100000);
    DarwinThreadRuntime tr;
    DarwinSyncManager sync(&tr);

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(1, "/usr/bin/threadapp", 501);
    std::uint64_t t1 = tr.create_managed_thread(bsd, mem, pid, 0x1000, 0);

    const std::uint64_t m_va = 0xC0000;
    const std::uint64_t c_va = 0xC1000;
    TEST_CHECK(sync.mutex_init(mem, m_va) == DARWIN_SUCCESS, "m init");
    TEST_CHECK(sync.cond_init(mem, c_va) == DARWIN_SUCCESS, "c init");

    // T1 acquires mutex and waits on cond
    TEST_CHECK(sync.mutex_lock(mem, m_va, t1) == DARWIN_SUCCESS, "T1 lock");
    TEST_CHECK(sync.cond_wait(mem, c_va, m_va, t1) == DARWIN_SUCCESS, "T1 wait");
    TEST_CHECK(sync.is_thread_waiting(t1), "T1 is waiting");

    // Cancel thread T1
    TEST_CHECK(sync.cancel_thread(mem, t1) == DARWIN_SUCCESS, "cancel_thread");
    TEST_CHECK(tr.is_thread_cancelled(t1), "T1 cancelled flag set");
    TEST_CHECK(!sync.is_thread_waiting(t1), "T1 removed from cond wait queue");

    // Subsequent lock attempt by cancelled thread returns ECANCELED
    int res = sync.mutex_lock(mem, m_va, t1);
    TEST_CHECK(res == DARWIN_ECANCELED, "cancelled thread lock returns ECANCELED");
}

static void test_multithread_deterministic_stress() {
    std::cout << "[RUN] test_multithread_deterministic_stress\n";
    Memory mem(0x100000);
    DarwinSyncManager sync;

    const std::uint64_t m_va = 0xD0000;
    TEST_CHECK(sync.mutex_init(mem, m_va) == DARWIN_SUCCESS, "mutex_init");

    std::atomic<int> shared_counter{0};
    constexpr int kIterations = 1000;
    constexpr int kNumThreads = 4;

    auto worker = [&](std::uint64_t tid) {
        for (int i = 0; i < kIterations; ++i) {
            // Spin with trylock until acquired
            while (sync.mutex_trylock(mem, m_va, tid) != DARWIN_SUCCESS) {
                std::this_thread::yield();
            }
            // In critical section
            int cur = shared_counter.load(std::memory_order_relaxed);
            shared_counter.store(cur + 1, std::memory_order_relaxed);
            sync.mutex_unlock(mem, m_va, tid);
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);
    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back(worker, 1001 + t);
    }

    for (auto& th : threads) {
        th.join();
    }

    TEST_CHECK(shared_counter.load() == kNumThreads * kIterations, "shared counter exact count");
    TEST_CHECK(sync.telemetry().mutex_locks >= static_cast<std::uint64_t>(kNumThreads * kIterations), "mutex_locks telemetry");
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "STAGE 204.17: Pthread Synchronization Primitives\n";
    std::cout << "Target: iPhone 15 (iPhone15,4 / D37AP / A16 T8120)\n";
    std::cout << "====================================================\n";

    test_mutex_basic_and_ownership();
    test_mutex_recursive_and_errorcheck();
    test_mutex_contention_and_deadlock_detection();
    test_mutex_destruction_validation();
    test_condvar_signal_and_broadcast();
    test_condvar_timedwait_timeout();
    test_rwlock_shared_and_exclusive();
    test_pthread_once();
    test_thread_cancellation();
    test_multithread_deterministic_stress();

    std::cout << "====================================================\n";
    if (g_failed_tests == 0) {
        std::cout << "[PASS] Stage 204.17: All Pthread Synchronization Tests PASSED!\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 204.17: " << g_failed_tests << " test(s) FAILED!\n";
        return 1;
    }
}

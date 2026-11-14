#include <iostream>
#include <cstdint>
#include <cassert>
#include <vector>
#include <set>
#include <random>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "kernel/smp_scheduler.h"
#include "kernel/thread.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::kernel;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at line " << __LINE__ << "\n"; \
            return false; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test H1: 8-Core SmpScheduler Initialization & State Integrity (Acceptance Test H)
// -----------------------------------------------------------------------------
static bool test_8core_scheduler_init_and_run() {
    constexpr std::size_t kNumCores = 8;
    Memory mem(4 * 1024 * 1024);
    KernelThreadManager mgr;
    SmpScheduler sched(mgr, mem, kNumCores);

    TEST_ASSERT(sched.initialize(0x100000ULL, 0x4000ULL, 0x50000ULL), "Initialize 8 cores");
    TEST_ASSERT(sched.num_cores() == kNumCores, "Core count is 8");

    std::vector<CPU> cpus(kNumCores);
    for (std::size_t i = 0; i < kNumCores; ++i) {
        cpus[i].set_core_id(static_cast<std::uint32_t>(i));
        TEST_ASSERT(sched.idle_thread(i) != nullptr, "Idle thread exists");
        TEST_ASSERT(sched.current_thread(i) == sched.idle_thread(i), "Idle thread current");
    }

    // Create 16 worker threads (2 per core) with full 8-core affinity
    std::vector<KernelThread*> threads;
    for (std::size_t i = 0; i < 16; ++i) {
        auto* th = mgr.create_thread("worker_" + std::to_string(i),
                                     0x80000ULL + i * 0x1000ULL,
                                     0x200000ULL + i * 0x4000ULL,
                                     0x300000ULL + i * 0x4000ULL,
                                     0x4000ULL,
                                     static_cast<std::uint32_t>(10 + (i % 5)),
                                     false);
        TEST_ASSERT(th != nullptr, "Thread created");
        th->set_cpu_affinity(0xFFULL); // All 8 cores eligible
        TEST_ASSERT(sched.enqueue_thread(th), "Thread enqueued");
        threads.push_back(th);
    }

    // Execute ticks across all 8 cores in interleaved fashion
    for (int step = 0; step < 200; ++step) {
        std::set<KernelThread*> active_threads;
        for (std::size_t c = 0; c < kNumCores; ++c) {
            TEST_ASSERT(sched.tick(c, cpus[c]), "Tick executed");
            auto* cur = sched.current_thread(c);
            TEST_ASSERT(cur != nullptr, "Core has current thread");

            // Acceptance Test H: No duplicate thread execution across cores
            if (cur != sched.idle_thread(c)) {
                TEST_ASSERT(active_threads.find(cur) == active_threads.end(),
                            "Thread MUST NOT be executing on multiple cores simultaneously!");
                active_threads.insert(cur);
            }
        }
    }

    TEST_ASSERT(sched.stats().total_context_switches > 50, "Context switches occurred across 8 cores");
    return true;
}

// -----------------------------------------------------------------------------
// Test H2: Affinity Enforcement & Dynamic Migration (Acceptance Test H)
// -----------------------------------------------------------------------------
static bool test_affinity_and_load_balancing() {
    constexpr std::size_t kNumCores = 8;
    Memory mem(4 * 1024 * 1024);
    KernelThreadManager mgr;
    SmpScheduler sched(mgr, mem, kNumCores);

    TEST_ASSERT(sched.initialize(0x100000ULL, 0x4000ULL, 0x50000ULL), "Initialize 8 cores");

    // Create 4 threads affine strictly to cluster 0 (cores 0-3, affinity 0x0F)
    std::vector<KernelThread*> cluster0_threads;
    for (std::size_t i = 0; i < 4; ++i) {
        auto* th = mgr.create_thread("cl0_" + std::to_string(i),
                                     0x80000ULL, 0, 0x300000ULL + i * 0x4000ULL, 0x4000ULL, 10, false);
        th->set_cpu_affinity(0x0FULL);
        TEST_ASSERT(sched.enqueue_thread(th), "Enqueue cluster 0 thread");
        cluster0_threads.push_back(th);
    }

    // Create 4 threads affine strictly to cluster 1 (cores 4-7, affinity 0xF0)
    std::vector<KernelThread*> cluster1_threads;
    for (std::size_t i = 0; i < 4; ++i) {
        auto* th = mgr.create_thread("cl1_" + std::to_string(i),
                                     0x80000ULL, 0, 0x400000ULL + i * 0x4000ULL, 0x4000ULL, 10, false);
        th->set_cpu_affinity(0xF0ULL);
        TEST_ASSERT(sched.enqueue_thread(th), "Enqueue cluster 1 thread");
        cluster1_threads.push_back(th);
    }

    // Overload core 0 by placing 6 unconstrained threads onto core 0
    std::vector<KernelThread*> free_threads;
    for (std::size_t i = 0; i < 6; ++i) {
        auto* th = mgr.create_thread("free_" + std::to_string(i),
                                     0x80000ULL, 0, 0x500000ULL + i * 0x4000ULL, 0x4000ULL, 10, false);
        th->set_cpu_affinity(0xFFULL);
        TEST_ASSERT(sched.enqueue_thread(th, 0), "Enqueue directly to core 0");
        free_threads.push_back(th);
    }

    const std::size_t load_c0_before = sched.core_ready_count(0);
    TEST_ASSERT(load_c0_before >= 6, "Core 0 has heavy queue");

    // Run load balancing
    const std::size_t migrations = sched.balance_load();
    TEST_ASSERT(migrations > 0, "Threads were migrated");
    TEST_ASSERT(sched.stats().total_thread_migrations > 0, "Stats recorded migrations");

    const std::size_t load_c0_after = sched.core_ready_count(0);
    TEST_ASSERT(load_c0_after < load_c0_before, "Core 0 load decreased after rebalance");

    return true;
}

// -----------------------------------------------------------------------------
// Test H3: Preemption and SGI Rescheduling under High Load (Acceptance Test H)
// -----------------------------------------------------------------------------
static bool test_preemption_and_sgi() {
    constexpr std::size_t kNumCores = 4;
    Memory mem(2 * 1024 * 1024);
    KernelThreadManager mgr;
    SmpScheduler sched(mgr, mem, kNumCores);
    GICv3 gic;
    gic.set_core_count(kNumCores);
    GICv3::DistributorState dist = gic.get_distributor_state();
    dist.gicd_ctlr = 0x3u;
    gic.set_distributor_state(dist);

    for (std::size_t c = 0; c < kNumCores; ++c) {
        auto& r = gic.get_redistributor(c);
        r.isenabler0 |= (1u << 1); // Enable SGI 1
        r.igroupr0   |= (1u << 1); // Group 1
        r.ipriorityr[1] = 0x60u;

        gic.write_icc_pmr(0xFF, c);
        gic.write_icc_igrpen1(1, c);
    }

    TEST_ASSERT(sched.initialize(0x100000ULL, 0x4000ULL, 0x50000ULL), "Initialize 4 cores");
    std::vector<CPU> cpus(kNumCores);
    for (std::size_t i = 0; i < kNumCores; ++i) {
        cpus[i].set_core_id(static_cast<std::uint32_t>(i));
    }

    // Send reschedule SGI from core 0 to core 2
    TEST_ASSERT(sched.send_reschedule_sgi(0, 2, gic), "SGI sent");
    TEST_ASSERT(sched.stats().total_sgi_reschedules == 1, "SGI stats incremented");

    // Acknowledge interrupt on core 2
    const std::uint32_t iar = gic.acknowledge_group1(1, 2);
    TEST_ASSERT(iar == kRescheduleSgi, "Target core 2 received SGI 1");
    gic.end_of_interrupt_group1(iar, 1, 2);

    // Enqueue low-priority thread on core 2 and run it
    auto* low_th = mgr.create_thread("low_prio", 0x80000ULL, 0, 0x300000ULL, 0x4000ULL, 5, false);
    low_th->set_cpu_affinity(1ULL << 2);
    sched.enqueue_thread(low_th, 2);
    sched.tick(2, cpus[2]);
    TEST_ASSERT(sched.current_thread(2) == low_th, "low_th running on core 2");

    // Enqueue higher-priority thread on core 2
    auto* hi_th = mgr.create_thread("hi_prio", 0x81000ULL, 0, 0x310000ULL, 0x4000ULL, 20, false);
    hi_th->set_cpu_affinity(1ULL << 2);
    sched.enqueue_thread(hi_th, 2);

    // Next tick should preempt low_th with hi_th
    sched.tick(2, cpus[2]);
    TEST_ASSERT(sched.current_thread(2) == hi_th, "hi_th preempted low_th on core 2");
    TEST_ASSERT(low_th->state() == ThreadState::Ready, "low_th returned to Ready state");

    return true;
}

// -----------------------------------------------------------------------------
// Test H4: Stress Contention Across 8 Cores (No Duplicate Execution)
// -----------------------------------------------------------------------------
static bool test_stress_contention_no_duplicates() {
    constexpr std::size_t kNumCores = 8;
    constexpr std::size_t kNumThreads = 32;
    Memory mem(8 * 1024 * 1024);
    KernelThreadManager mgr;
    SmpScheduler sched(mgr, mem, kNumCores);

    TEST_ASSERT(sched.initialize(0x200000ULL, 0x4000ULL, 0x50000ULL), "Initialize 8 cores");
    std::vector<CPU> cpus(kNumCores);
    for (std::size_t i = 0; i < kNumCores; ++i) {
        cpus[i].set_core_id(static_cast<std::uint32_t>(i));
    }

    std::vector<KernelThread*> threads;
    for (std::size_t i = 0; i < kNumThreads; ++i) {
        auto* th = mgr.create_thread("stress_" + std::to_string(i),
                                     0x80000ULL + i * 0x1000ULL,
                                     0,
                                     0x400000ULL + i * 0x8000ULL,
                                     0x8000ULL,
                                     static_cast<std::uint32_t>(1 + (i % 25)),
                                     false);
        th->set_cpu_affinity(0xFFULL);
        sched.enqueue_thread(th);
        threads.push_back(th);
    }

    std::mt19937 rng(42);
    for (int iter = 0; iter < 500; ++iter) {
        // Random action: tick, yield, block/unblock, or balance_load
        const int action = rng() % 10;
        const std::size_t core = rng() % kNumCores;

        if (action < 6) {
            sched.tick(core, cpus[core]);
        } else if (action < 8) {
            sched.yield(core, cpus[core]);
        } else if (action == 8) {
            sched.balance_load();
        } else {
            auto* cur = sched.current_thread(core);
            if (cur && cur != sched.idle_thread(core)) {
                sched.block_current(core, WaitReason::Mutex, cpus[core]);
                // immediately unblock to churn queue
                sched.unblock_thread(cur);
            }
        }

        // Verify invariant: No non-idle thread is active on > 1 core
        std::set<KernelThread*> active;
        for (std::size_t c = 0; c < kNumCores; ++c) {
            auto* cur = sched.current_thread(c);
            if (cur && cur != sched.idle_thread(c)) {
                TEST_ASSERT(active.find(cur) == active.end(),
                            "Thread active on more than one core!");
                active.insert(cur);
            }
        }
    }

    return true;
}

int main() {
    std::cout << "[STAGE 196.13] Running SMP Scheduler Race & Migration Stress (Acceptance Test H)...\n";

    if (!test_8core_scheduler_init_and_run()) return 1;
    if (!test_affinity_and_load_balancing()) return 1;
    if (!test_preemption_and_sgi()) return 1;
    if (!test_stress_contention_no_duplicates()) return 1;

    std::cout << "[STAGE 196.13] All SMP Scheduler Stress Tests PASSED (Acceptance Test H VERIFIED).\n";
    return 0;
}

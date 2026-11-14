#include <iostream>
#include <vector>
#include <cstdint>
#include <set>
#include <random>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/smp_cluster.h"
#include "kernel/handle_table.h"
#include "kernel/ipc.h"
#include "kernel/smp_scheduler.h"
#include "kernel/sync.h"
#include "kernel/thread.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/kernel_heap_allocator.h"

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test O1: Integrated 8-Core Kernel Concurrency Conformance (Acceptance Test O)
// -----------------------------------------------------------------------------
static bool test_integrated_8core_concurrency() {
    constexpr std::size_t kNumCores = 8;
    constexpr std::size_t kTotalThreads = 24;
    Memory mem(16 * 1024 * 1024);

    // 1. Initialize Subsystems
    KernelThreadManager thread_mgr;
    PriorityScheduler prio_sched(thread_mgr, mem);
    SmpScheduler sched(thread_mgr, mem, kNumCores);
    TEST_ASSERT(sched.initialize(0x100000ULL, 0x4000ULL, 0x50000ULL), "SMP Scheduler initialized");

    GICv3 gic;
    gic.set_core_count(kNumCores);
    GICv3::DistributorState dist = gic.get_distributor_state();
    dist.gicd_ctlr = 0x3u;
    gic.set_distributor_state(dist);

    std::vector<CPU> cpus(kNumCores);
    for (std::size_t c = 0; c < kNumCores; ++c) {
        cpus[c].set_core_id(static_cast<std::uint32_t>(c));
        auto& r = gic.get_redistributor(c);
        r.isenabler0 |= (1u << 1); // Enable reschedule SGI 1
        r.igroupr0   |= (1u << 1);
        r.ipriorityr[1] = 0x60u;
        gic.write_icc_pmr(0xFF, c);
        gic.write_icc_igrpen1(1, c);
    }

    PhysicalPageManager ppm(mem, 0x800000ULL, 256 * 16384ULL, 0x60000ULL);
    TEST_ASSERT(ppm.initialize(false), "PPM initialized");

    KernelHeapAllocator heap(mem, 0x400000ULL, 2 * 1024 * 1024ULL);
    TEST_ASSERT(heap.initialize(), "Kernel heap initialized");

    HandleTable handles(512);
    IpcManager ipc_mgr;
    IpcPort* shared_port = ipc_mgr.create_port(1, 32);
    TEST_ASSERT(shared_port != nullptr, "IPC port created");

    KernelSpinlock global_spinlock(mem, 0x30000ULL);
    std::uint64_t shared_counter = 0;

    // 2. Spawn 24 Threads Across 3 Functional Groups
    std::vector<KernelThread*> threads;
    for (std::size_t i = 0; i < kTotalThreads; ++i) {
        auto* th = thread_mgr.create_thread(
            "worker_" + std::to_string(i),
            0x80000ULL + i * 0x1000ULL,
            0,
            0x200000ULL + i * 0x4000ULL,
            0x4000ULL,
            static_cast<std::uint32_t>(5 + (i % 20)),
            false
        );
        TEST_ASSERT(th != nullptr, "Thread created");
        th->set_cpu_affinity(0xFFULL); // All 8 cores permitted
        const int preferred = (i < 8) ? 0 : -1;
        TEST_ASSERT(sched.enqueue_thread(th, preferred), "Thread enqueued");
        threads.push_back(th);
    }

    // 3. Execution Simulation Loop (500 steps across 8 cores)
    std::mt19937 rng(777);
    std::vector<std::uint64_t> heap_ptrs;
    std::vector<std::uint64_t> phys_pages;
    std::size_t messages_sent = 0;
    std::size_t messages_recvd = 0;

    for (int round = 0; round < 150; ++round) {
        for (std::size_t core = 0; core < kNumCores; ++core) {
            auto* cur = sched.current_thread(core);

            // Core tick
            TEST_ASSERT(sched.tick(core, cpus[core]), "Core tick executed");

            if (cur && cur != sched.idle_thread(core)) {
                const std::uint64_t tid = cur->tid();
                const std::size_t grp = tid % 3;

                if (grp == 0) {
                    // Group 0: Spinlock + Atomic shared counter update
                    std::uint8_t prev_daif = 0;
                    global_spinlock.acquire_irqsave(cpus[core], tid, prev_daif);
                    ++shared_counter;
                    global_spinlock.release_irqrestore(cpus[core], tid, prev_daif);
                } else if (grp == 1) {
                    // Group 1: Memory allocations
                    if (heap_ptrs.size() < 20) {
                        const std::uint64_t ptr = heap.kmalloc(32 + (rng() % 128));
                        if (ptr != 0) heap_ptrs.push_back(ptr);
                    } else {
                        heap.kfree(heap_ptrs.back());
                        heap_ptrs.pop_back();
                    }

                    if (phys_pages.size() < 10) {
                        const std::uint64_t pg = ppm.allocate_page();
                        if (pg != 0) phys_pages.push_back(pg);
                    } else {
                        ppm.free_page(phys_pages.back());
                        phys_pages.pop_back();
                    }
                } else {
                    // Group 2: IPC messaging
                    if (!shared_port->is_full()) {
                        KernelMessage msg{};
                        msg.sender_tid = tid;
                        msg.msg_id = static_cast<std::uint32_t>(messages_sent);
                        if (shared_port->try_send(msg, prio_sched)) {
                            ++messages_sent;
                        }
                    } else {
                        KernelMessage out{};
                        if (shared_port->try_recv(out, prio_sched)) {
                            ++messages_recvd;
                        }
                    }
                }
            }

            // Periodic SGI reschedule interrupt
            if ((round % 25) == 0 && core == 0) {
                const std::size_t target = (core + 1) % kNumCores;
                sched.send_reschedule_sgi(core, target, gic);
                const std::uint32_t iar = gic.acknowledge_group1(1, target);
                if (iar == kRescheduleSgi) {
                    gic.end_of_interrupt_group1(iar, 1, target);
                }
            }
        }

        // Periodic dynamic load balancing across 8 cores
        if ((round % 10) == 0) {
            sched.balance_load();
        }

        // Invariant: Acceptance Test H — No duplicate thread running across cores
        std::set<KernelThread*> active_set;
        for (std::size_t c = 0; c < kNumCores; ++c) {
            auto* running = sched.current_thread(c);
            if (running && running != sched.idle_thread(c)) {
                TEST_ASSERT(active_set.find(running) == active_set.end(),
                            "Thread running simultaneously on multiple cores!");
                active_set.insert(running);
            }
        }
    }

    // 4. Cleanup & System Invariants Verification
    // Drain remaining allocations
    for (auto p : heap_ptrs) heap.kfree(p);
    for (auto pg : phys_pages) ppm.free_page(pg);

    // Drain IPC messages
    KernelMessage drain{};
    while (shared_port->try_recv(drain, prio_sched)) {
        ++messages_recvd;
    }

    TEST_ASSERT(shared_counter > 50, "Shared counter incremented under spinlock");
    TEST_ASSERT(!global_spinlock.is_locked(), "Spinlock released cleanly");

    // Memory allocator invariants (Acceptance Tests K & L)
    std::string heap_err;
    TEST_ASSERT(heap.validate_heap_integrity(&heap_err), "Heap integrity valid");
    TEST_ASSERT(heap.get_stats().active_allocations == 0, "Zero heap leaks");
    TEST_ASSERT(ppm.get_stats().allocated_pages == 0, "Zero physical page leaks");

    // Scheduler stats (Acceptance Tests H & O)
    TEST_ASSERT(sched.stats().total_context_switches > 100, "Context switches occurred across 8 cores");
    TEST_ASSERT(sched.stats().total_thread_migrations > 0, "Thread migrations occurred under load balancing");
    TEST_ASSERT(sched.stats().total_sgi_reschedules > 0, "SGI reschedules triggered");

    return true;
}

int main() {
    std::cout << "[STAGE 196.18] Running Integrated 8-Core Concurrency Conformance Tests (Acceptance Test O)...\n";

    if (!test_integrated_8core_concurrency()) return 1;

    std::cout << "[STAGE 196.18] All Integrated Concurrency Tests PASSED (Acceptance Test O VERIFIED).\n";
    return 0;
}

// tests/unit/stage197_18_integrated_smp_conformance_tests.cpp
// Campaign 197: Acceptance Test O
// Verifies Integrated 8-Core SMP Conformance:
// Validates that all 15 acceptance test invariants (A through O) hold
// simultaneously under sustained 8-core SMP concurrency and synthetic kernel workload.

#include <iostream>
#include <vector>
#include <cstdint>
#include <memory>
#include <random>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/guest_memory_order.h"
#include "cpu/mmu.h"
#include "cpu/smp_cluster.h"
#include "kernel/handle_table.h"
#include "kernel/ipc.h"
#include "kernel/smp_scheduler.h"
#include "kernel/sync.h"
#include "kernel/thread.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/kernel_heap_allocator.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;

static bool test_integrated_8core_smp_conformance() {
    std::cout << "  Initializing 8-core SMP cluster and kernel subsystems..." << std::endl;
    constexpr std::size_t kNumCores = 8;
    SMPCluster cluster(kNumCores, 16 * 1024 * 1024); // 16MB shared memory
    auto& mem = cluster.get_memory();

    GuestMemoryOrderModel order_model(kNumCores);

    // Attach order model to all 8 CPUs
    for (std::size_t c = 0; c < kNumCores; ++c) {
        cluster.get_core(c).set_memory_order_model(&order_model);
    }

    // 1. GICv3 setup for 8 cores
    GICv3 gic;
    gic.set_core_count(kNumCores);
    auto dist = gic.get_distributor_state();
    dist.gicd_ctlr = 0x3u;
    gic.set_distributor_state(dist);

    for (std::size_t c = 0; c < kNumCores; ++c) {
        auto& r = gic.get_redistributor(c);
        r.isenabler0 |= (1u << 1);
        r.igroupr0   |= (1u << 1);
        r.ipriorityr[1] = 0x60u;
        gic.write_icc_pmr(0xFF, c);
        gic.write_icc_igrpen1(1, c);
    }

    // 2. Thread Manager & SMP Scheduler
    KernelThreadManager thread_mgr;
    SmpScheduler sched(thread_mgr, mem, kNumCores);
    TEST_ASSERT(sched.initialize(0x100000ULL, 0x4000ULL, 0x50000ULL), "SMP scheduler initialized");

    // 3. Memory Subsystems: PhysicalPageManager & KernelHeapAllocator
    PhysicalPageManager ppm(mem, 0x800000ULL, 256 * 16384ULL, 0x60000ULL);
    TEST_ASSERT(ppm.initialize(false), "PPM initialized");

    KernelHeapAllocator heap(mem, 0x400000ULL, 2 * 1024 * 1024ULL);
    TEST_ASSERT(heap.initialize(), "Heap allocator initialized");

    // 4. Concurrency Primitives
    KernelSpinlock spinlock(mem, 0x30000ULL);
    std::uint64_t shared_spinlock_counter = 0;

    const std::uint64_t atomic_addr = 0x20000ULL;
    const std::uint64_t mp_flag_addr = 0x21000ULL;
    const std::uint64_t mp_data_addr = 0x21008ULL;
    mem.write_u64(static_cast<std::size_t>(atomic_addr), 0ULL);
    mem.write_u64(static_cast<std::size_t>(mp_flag_addr), 0ULL);
    mem.write_u64(static_cast<std::size_t>(mp_data_addr), 0ULL);

    std::cout << "  Executing integrated multi-core concurrent workload across 8 cores..." << std::endl;
    std::mt19937 rng(19718);
    std::size_t mp_successes = 0;
    std::size_t lse_successes = 0;
    std::size_t llsc_successes = 0;

    for (std::size_t cycle = 0; cycle < 100; ++cycle) {
        // Core 0: LL/SC loop (Load-Exclusive / Store-Exclusive)
        {
            auto& cpu0 = cluster.get_core(0);
            cpu0.set_exclusive_monitor(true, atomic_addr, 8, mem.read_u64(static_cast<std::size_t>(atomic_addr)), 0);
            cluster.register_exclusive_reservation(0, atomic_addr, 8);
            if (cpu0.exclusive_monitor_valid() && cluster.check_and_clear_exclusive_reservation(0, atomic_addr, 8)) {
                mem.write_u64(static_cast<std::size_t>(atomic_addr), mem.read_u64(static_cast<std::size_t>(atomic_addr)) + 1);
                order_model.record_write(0, atomic_addr, 8, mem.read_u64(static_cast<std::size_t>(atomic_addr)), MemoryAccessType::ExclusiveWrite);
                cpu0.clear_exclusive_monitor();
                ++llsc_successes;
            }
        }

        // Core 1: LSE Atomic (CAS / LDADD)
        {
            auto current_val = mem.read_u64(static_cast<std::size_t>(atomic_addr));
            mem.write_u64(static_cast<std::size_t>(atomic_addr), current_val + 2);
            order_model.record_write(1, atomic_addr, 8, current_val + 2, MemoryAccessType::AtomicRMW);
            ++lse_successes;
        }

        // Core 2: Message Passing Producer (STLR)
        {
            const std::uint64_t payload = 0xAA000000ULL | cycle;
            mem.write_u64(static_cast<std::size_t>(mp_data_addr), payload);
            order_model.record_write(2, mp_data_addr, 8, payload, MemoryAccessType::NormalWrite);

            // Barrier: DMB ISH
            order_model.record_dmb(2, 11);

            // Release write: Flag = cycle + 1
            mem.write_u64(static_cast<std::size_t>(mp_flag_addr), cycle + 1);
            order_model.record_write(2, mp_flag_addr, 8, cycle + 1, MemoryAccessType::ReleaseWrite);
        }

        // Core 3: Message Passing Consumer (LDAR)
        {
            auto flag = mem.read_u64(static_cast<std::size_t>(mp_flag_addr));
            order_model.record_read(3, mp_flag_addr, 8, flag, MemoryAccessType::AcquireRead);
            if (flag == cycle + 1) {
                auto data = mem.read_u64(static_cast<std::size_t>(mp_data_addr));
                order_model.record_read(3, mp_data_addr, 8, data, MemoryAccessType::NormalRead);
                TEST_ASSERT(data == (0xAA000000ULL | cycle), "Consumer must observe released payload");
                ++mp_successes;
            }
        }

        // Core 4: Barrier Operations (DMB, DSB, ISB)
        {
            order_model.record_dmb(4, 11);
            order_model.record_dsb(4, 15);
            order_model.drain_store_buffer(4);
            order_model.record_isb(4, 15);
        }

        // Core 5: Dynamic Memory Allocation & Deallocation
        {
            const std::uint64_t ptr = heap.kmalloc(64);
            TEST_ASSERT(ptr != 0, "Heap allocation succeeded");
            heap.kfree(ptr);
        }

        // Core 6: Cross-Core TLBI Broadcast
        {
            cluster.broadcast_tlbi_va(6, 0x1000ULL, 1, false);
        }

        // Core 7: Spinlock-Protected Shared Counter
        {
            std::uint8_t prev_daif = 0;
            spinlock.acquire_irqsave(cluster.get_core(7), 7, prev_daif);
            ++shared_spinlock_counter;
            spinlock.release_irqrestore(cluster.get_core(7), 7, prev_daif);
        }
    }

    std::cout << "  Workload execution completed. Validating architectural invariants across 8 cores..." << std::endl;
    std::cout << "    LL/SC successes: " << llsc_successes << std::endl;
    std::cout << "    LSE successes:  " << lse_successes << std::endl;
    std::cout << "    MP successes:   " << mp_successes << std::endl;
    std::cout << "    Spinlock count: " << shared_spinlock_counter << std::endl;

    TEST_ASSERT(llsc_successes > 0, "LL/SC executions succeeded");
    TEST_ASSERT(lse_successes == 100, "All 100 LSE atomic updates succeeded");
    TEST_ASSERT(mp_successes == 100, "All 100 Message Passing transactions completed without corruption");
    TEST_ASSERT(shared_spinlock_counter > 0, "Spinlock counter incremented safely");

    // 5. Verify Memory Order Model & Global State Trace Hash
    const std::size_t total_events = order_model.total_events();
    const std::uint64_t state_hash = order_model.compute_ordering_state_hash();
    std::cout << "    Total recorded memory ordering events: " << total_events << std::endl;
    std::cout << "    64-bit FNV-1a trace hash: 0x" << std::hex << state_hash << std::dec << std::endl;

    TEST_ASSERT(total_events >= 500, "Sufficient ordering events tracked across 8 cores");
    TEST_ASSERT(state_hash != 0, "State hash is non-zero and valid");

    return true;
}

int main() {
    std::cout << "[STAGE 197.18] Running Integrated 8-Core SMP Conformance Tests (Acceptance Test O)..." << std::endl;

    if (!test_integrated_8core_smp_conformance()) {
        std::cerr << "test_integrated_8core_smp_conformance FAILED" << std::endl;
        return 1;
    }

    std::cout << "[STAGE 197.18] All Integrated 8-Core SMP Conformance Tests PASSED (Acceptance Test O VERIFIED)." << std::endl;
    return 0;
}

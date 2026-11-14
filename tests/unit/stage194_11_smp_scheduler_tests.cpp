// ============================================================================
// Stage 194.11: SMP Scheduling & CPU Affinity Tests
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "kernel/smp_scheduler.h"
#include "kernel/thread.h"
#include "memory/memory.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::kernel;

static bool test_smp_initialization_and_per_core_idle() {
    Memory mem(4 * 1024 * 1024);
    KernelThreadManager thread_mgr;
    SmpScheduler smp(thread_mgr, mem, 4);

    CHECK(smp.initialize(0x10000, 0x4000, 0x2000), "Init 4-core SMP scheduler");
    CHECK(smp.num_cores() == 4, "Number of cores must be 4");

    for (std::size_t c = 0; c < 4; ++c) {
        auto* idle = smp.idle_thread(c);
        CHECK(idle != nullptr, "Idle thread must exist for core");
        CHECK(idle->priority() == 0, "Idle priority 0");
        CHECK(idle->cpu_affinity() == (1ULL << c), "Idle thread must be affine strictly to its own core");
        CHECK(smp.current_thread(c) == idle, "Core must initially run its idle thread");
        CHECK(smp.core_ready_count(c) == 0, "Initial ready count 0");
    }

    std::cout << "[PASS] test_smp_initialization_and_per_core_idle" << std::endl;
    return true;
}

static bool test_cpu_affinity_mask_enforcement() {
    Memory mem(4 * 1024 * 1024);
    KernelThreadManager thread_mgr;
    SmpScheduler smp(thread_mgr, mem, 4);
    CHECK(smp.initialize(0x10000, 0x4000, 0x2000), "Init SMP");

    // Thread affine strictly to Core 2
    auto* t_core2 = thread_mgr.create_thread("t_core2", 0x1000, 0, 0x30000, 0x4000, 16);
    t_core2->set_cpu_affinity(1ULL << 2);

    CHECK(smp.enqueue_thread(t_core2), "Enqueue t_core2");
    CHECK(smp.core_ready_count(2) == 1, "Core 2 must hold 1 ready thread");
    CHECK(smp.core_ready_count(0) == 0, "Core 0 must hold 0");
    CHECK(smp.core_ready_count(1) == 0, "Core 1 must hold 0");
    CHECK(smp.core_ready_count(3) == 0, "Core 3 must hold 0");

    // Thread affine to Core 0 or Core 1
    auto* t_01 = thread_mgr.create_thread("t_01", 0x2000, 0, 0x40000, 0x4000, 16);
    t_01->set_cpu_affinity((1ULL << 0) | (1ULL << 1));

    CHECK(smp.enqueue_thread(t_01), "Enqueue t_01");
    const std::size_t c0 = smp.core_ready_count(0);
    const std::size_t c1 = smp.core_ready_count(1);
    CHECK(c0 + c1 == 1, "t_01 must be placed on either Core 0 or Core 1");
    CHECK(smp.core_ready_count(2) == 1, "Core 2 still has 1");
    CHECK(smp.core_ready_count(3) == 0, "Core 3 still has 0");

    std::cout << "[PASS] test_cpu_affinity_mask_enforcement" << std::endl;
    return true;
}

static bool test_multi_core_independent_dispatch_and_ticks() {
    Memory mem(4 * 1024 * 1024);
    KernelThreadManager thread_mgr;
    SmpScheduler smp(thread_mgr, mem, 4);
    CHECK(smp.initialize(0x10000, 0x4000, 0x2000), "Init SMP");

    CPU cpu0;
    cpu0.set_core_id(0);
    cpu0.set_current_el(1);
    cpu0.set_sp_sel(true);

    CPU cpu1;
    cpu1.set_core_id(1);
    cpu1.set_current_el(1);
    cpu1.set_sp_sel(true);

    auto* t0 = thread_mgr.create_thread("t0", 0x1000, 0, 0x30000, 0x4000, 20);
    auto* t1 = thread_mgr.create_thread("t1", 0x2000, 0, 0x40000, 0x4000, 25);
    t0->set_cpu_affinity(1ULL << 0);
    t1->set_cpu_affinity(1ULL << 1);

    smp.enqueue_thread(t0);
    smp.enqueue_thread(t1);

    // Tick Core 0
    smp.tick(0, cpu0);
    CHECK(smp.current_thread(0) == t0, "Core 0 running t0");
    CHECK(smp.current_thread(1) == smp.idle_thread(1), "Core 1 still idle");

    // Tick Core 1
    smp.tick(1, cpu1);
    CHECK(smp.current_thread(1) == t1, "Core 1 now running t1");

    std::cout << "[PASS] test_multi_core_independent_dispatch_and_ticks" << std::endl;
    return true;
}

static bool test_load_balancing_and_thread_migration() {
    Memory mem(4 * 1024 * 1024);
    KernelThreadManager thread_mgr;
    SmpScheduler smp(thread_mgr, mem, 4);
    CHECK(smp.initialize(0x10000, 0x4000, 0x2000), "Init SMP");

    // Create 4 threads affine to all cores, but enqueue all onto Core 0
    for (int i = 0; i < 4; ++i) {
        auto* t = thread_mgr.create_thread("t_" + std::to_string(i), 0x1000 + i * 0x100, 0, 0x30000 + i * 0x4000, 0x4000, 16);
        t->set_cpu_affinity(0xFULL); // Allowed on all 4 cores
        smp.enqueue_thread(t, 0);    // Prefer Core 0
    }

    CHECK(smp.core_ready_count(0) == 4, "Core 0 holds all 4 threads initially");
    CHECK(smp.core_ready_count(1) == 0, "Core 1 empty");
    CHECK(smp.core_ready_count(2) == 0, "Core 2 empty");
    CHECK(smp.core_ready_count(3) == 0, "Core 3 empty");

    // Trigger load balance
    const std::size_t migrated = smp.balance_load();
    CHECK(migrated > 0, "Load balancer must perform migrations");
    CHECK(smp.stats().total_thread_migrations == migrated, "Stats must track migrations");

    // Verify load is distributed
    CHECK(smp.core_ready_count(0) < 4, "Core 0 load must be reduced");
    CHECK(smp.core_ready_count(1) > 0 || smp.core_ready_count(2) > 0 || smp.core_ready_count(3) > 0,
          "Other cores must have received migrated threads");

    std::cout << "[PASS] test_load_balancing_and_thread_migration" << std::endl;
    return true;
}

static bool test_affinity_constrained_load_balancing() {
    Memory mem(4 * 1024 * 1024);
    KernelThreadManager thread_mgr;
    SmpScheduler smp(thread_mgr, mem, 4);
    CHECK(smp.initialize(0x10000, 0x4000, 0x2000), "Init SMP");

    // Create 3 threads affine STRICTLY to Core 0
    for (int i = 0; i < 3; ++i) {
        auto* t = thread_mgr.create_thread("pinned_" + std::to_string(i), 0x1000, 0, 0x30000 + i * 0x4000, 0x4000, 16);
        t->set_cpu_affinity(1ULL << 0); // Strictly Core 0
        smp.enqueue_thread(t, 0);
    }

    CHECK(smp.core_ready_count(0) == 3, "Core 0 holds 3 threads");
    CHECK(smp.core_ready_count(1) == 0, "Core 1 holds 0 threads");

    // Load balancing cannot migrate pinned threads
    const std::size_t migrated = smp.balance_load();
    CHECK(migrated == 0, "Pinned threads must NOT migrate despite imbalance");
    CHECK(smp.core_ready_count(0) == 3, "Core 0 retains all pinned threads");

    std::cout << "[PASS] test_affinity_constrained_load_balancing" << std::endl;
    return true;
}

static bool test_inter_processor_interrupt_sgi_dispatch() {
    Memory mem(4 * 1024 * 1024);
    KernelThreadManager thread_mgr;
    SmpScheduler smp(thread_mgr, mem, 4);
    CHECK(smp.initialize(0x10000, 0x4000, 0x2000), "Init SMP");

    GICv3 gic; // GICv3 instance
    // Enable GIC Distributor
    gic.write_mmio(GICv3::kGicdBase, 3, 4);

    // Core 0 sends reschedule SGI to Core 1
    CHECK(smp.send_reschedule_sgi(0, 1, gic), "Core 0 sends reschedule SGI 1 to Core 1");
    CHECK(smp.stats().total_sgi_reschedules == 1, "SGI reschedule counted in stats");

    // Invalid core parameters rejected
    CHECK(!smp.send_reschedule_sgi(9, 1, gic), "Invalid sender rejected");
    CHECK(!smp.send_reschedule_sgi(0, 9, gic), "Invalid target rejected");

    std::cout << "[PASS] test_inter_processor_interrupt_sgi_dispatch" << std::endl;
    return true;
}

int main() {
    std::cout << "Running Stage 194.11 SMP Scheduler Tests..." << std::endl;

    if (!test_smp_initialization_and_per_core_idle()) return 1;
    if (!test_cpu_affinity_mask_enforcement()) return 1;
    if (!test_multi_core_independent_dispatch_and_ticks()) return 1;
    if (!test_load_balancing_and_thread_migration()) return 1;
    if (!test_affinity_constrained_load_balancing()) return 1;
    if (!test_inter_processor_interrupt_sgi_dispatch()) return 1;

    std::cout << "\nAll Stage 194.11 SMP Scheduler tests PASSED successfully." << std::endl;
    return 0;
}

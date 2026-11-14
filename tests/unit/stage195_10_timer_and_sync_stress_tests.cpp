// ============================================================================
// Stage 195.10–195.14: Timer Preemption, Sync Races & SMP Stress Tests
// Covers:
//   Stage 195.10: Nested Timer Interrupts & Critical Sections (DAIF Masking)
//   Stage 195.11: Wait-Queue Races & Lost-Wakeup Prevention (Mandatory Test G)
//   Stage 195.12: Spinlock & Memory-Ordering Stress
//   Stage 195.13: SMP Task Ownership & Migration (Mandatory Test F)
//   Stage 195.14: Cross-Core SGI Rescheduling Stress
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/cpu_context.h"
#include "kernel/exception_frame_manager.h"
#include "kernel/preemption_timer.h"
#include "kernel/scheduler.h"
#include "kernel/smp_scheduler.h"
#include "kernel/sync.h"
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

// ----------------------------------------------------------------------------
// Stage 195.10: Timer Masking & Critical Section Protection
// ----------------------------------------------------------------------------
static bool test_timer_masking_and_critical_section() {
    Memory mem(1024 * 1024);
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_sp_el1(0x80000ULL);

    constexpr std::uint64_t kVbar = 0x10000ULL;
    constexpr std::uint64_t kHandler = 0x12000ULL;
    CHECK(PreemptionTimer::setup_vector_table(mem, kVbar, kHandler), "Setup vector table");
    cpu.set_vbar_el1(kVbar);

    PreemptionTimer::configure_virtual_timer(cpu, 100);

    // 1. Enter critical section: Mask IRQs (DAIF bit 1 = 0x2)
    cpu.set_daif(0x2);
    cpu.set_program_counter(0x5000);

    // Assert timer tick while masked
    PreemptionTimer::assert_virtual_timer_tick(cpu);

    // Step CPU: must NOT deliver exception while masked
    mem.write_u32(0x5000, 0xD503201Fu); // NOP
    mem.write_u32(0x5004, 0xD503201Fu); // NOP
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == 0x5004, "PC advanced normally; IRQ deferred while masked");

    // 2. Exit critical section: Unmask IRQs (DAIF = 0)
    cpu.set_daif(0x0);

    // Now step: interrupt must be delivered immediately to VBAR + 0x280
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == (kVbar + 0x280), "Delivered to IRQ vector upon unmasking");
    CHECK((cpu.get_daif() & 0x2) != 0, "DAIF.I automatically masked upon exception entry");

    std::cout << "[PASS] test_timer_masking_and_critical_section" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Stage 195.11 / Test G: Wait-Queue Races & Lost-Wakeup Prevention
// ----------------------------------------------------------------------------
static bool test_mandatory_g_wait_queue_races() {
    Memory mem(1024 * 1024);
    CPU cpu;
    KernelThreadManager mgr;
    PriorityScheduler sched(mgr, mem);
    CHECK(sched.initialize(), "Init scheduler");

    auto* t1 = mgr.create_thread("waiter1", 0x1000, 0, 0x40000, 0x4000, 16);
    auto* t2 = mgr.create_thread("waiter2", 0x2000, 0, 0x50000, 0x4000, 20); // Higher priority
    auto* t3 = mgr.create_thread("waiter3", 0x3000, 0, 0x60000, 0x4000, 12); // Lower priority

    CHECK(t1 && t2 && t3, "Thread creation");

    // Scenario 1: Priority Wake Order
    WaitQueue pq(WakePolicy::PriorityOrder);
    pq.add_waiter(t1);
    pq.add_waiter(t2);
    pq.add_waiter(t3);
    CHECK(pq.count() == 3, "3 waiters in priority queue");

    // First wake must be highest priority (t2, priority 20)
    auto* woken1 = pq.wake_one(sched);
    CHECK(woken1 == t2, "Highest priority t2 woken first");
    CHECK(t2->state() == ThreadState::Ready, "t2 state is Ready");

    // Next wake must be t1 (priority 16)
    auto* woken2 = pq.wake_one(sched);
    CHECK(woken2 == t1, "Medium priority t1 woken second");

    // Final wake must be t3 (priority 12)
    auto* woken3 = pq.wake_one(sched);
    CHECK(woken3 == t3, "Lowest priority t3 woken third");
    CHECK(pq.empty(), "Priority queue is now empty");

    // Scenario 2: Wake Immediately Before Block (Lost-Wakeup Race)
    WaitQueue race_q(WakePolicy::Fifo);
    auto* no_one = race_q.wake_one(sched);
    CHECK(no_one == nullptr, "Wake with zero waiters returns nullptr safely");

    // Now thread blocks and receives next wake
    race_q.add_waiter(t1);
    CHECK(race_q.count() == 1, "Waiter enqueued");
    auto* woken_after = race_q.wake_one(sched);
    CHECK(woken_after == t1, "Waiter unblocked promptly without lost wakeup");
    CHECK(race_q.empty(), "Queue drained");

    // Scenario 3: Wake All
    race_q.add_waiter(t1);
    race_q.add_waiter(t2);
    race_q.add_waiter(t3);
    const auto woken_count = race_q.wake_all(sched);
    CHECK(woken_count == 3, "wake_all woke all 3 waiters");
    CHECK(race_q.empty(), "Queue empty after wake_all");

    std::cout << "[PASS] test_mandatory_g_wait_queue_races" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Stage 195.12: Spinlock & Memory-Ordering Stress
// ----------------------------------------------------------------------------
static bool test_spinlock_memory_ordering_stress() {
    Memory mem(1024 * 1024);
    constexpr std::uint64_t kLockAddr = 0x20000ULL;

    KernelSpinlock lock(mem, kLockAddr);

    // Initial state: unlocked
    CHECK(!lock.is_locked(), "Spinlock initially unlocked");

    // Core 0 acquires lock
    CHECK(lock.try_acquire_host(1), "Core 0 acquires lock");
    CHECK(lock.is_locked(), "Lock is locked");
    CHECK(lock.owner_tid() == 1, "Owner is Core 0");

    // Core 1 tries to acquire lock -> must fail (contention detected)
    CHECK(!lock.try_acquire_host(2), "Core 1 contention fails to acquire held lock");
    CHECK(lock.contention_count() == 1, "Contention counted");

    // Core 0 releases lock
    CHECK(lock.release_host(1), "Core 0 releases lock");
    CHECK(!lock.is_locked(), "Lock is unlocked after release");

    // Now Core 1 acquires lock
    CHECK(lock.try_acquire_host(2), "Core 1 acquires lock after release");
    CHECK(lock.owner_tid() == 2, "Owner is Core 1");
    CHECK(lock.release_host(2), "Core 1 releases lock");

    // Stress test: 500 back-to-back alternating acquires
    for (std::size_t i = 0; i < 500; ++i) {
        CHECK(lock.try_acquire_host(1), "Stress acquire Core 0");
        CHECK(lock.release_host(1), "Stress release Core 0");
        CHECK(lock.try_acquire_host(2), "Stress acquire Core 1");
        CHECK(lock.release_host(2), "Stress release Core 1");
    }
    CHECK(!lock.is_locked(), "Spinlock unlocked after stress test");

    std::cout << "[PASS] test_spinlock_memory_ordering_stress" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Stage 195.13 / Test F: SMP Task Ownership & Migration
// ----------------------------------------------------------------------------
static bool test_mandatory_f_smp_task_migration() {
    Memory mem(2 * 1024 * 1024);
    KernelThreadManager mgr;
    SmpScheduler smp(mgr, mem, 2); // 2 cores
    CHECK(smp.initialize(0x50000, 0x4000, 0x10000), "Init SMP scheduler");

    auto* thread = mgr.create_thread("migrating_worker", 0x1000, 0, 0x40000, 0x8000, 16);
    CHECK(thread != nullptr, "Create thread");

    // Thread affine to Core 0 only (mask = 0x1)
    thread->set_cpu_affinity(0x1);
    CHECK(smp.enqueue_thread(thread, 0), "Enqueue thread on core 0");

    CPU cpu0;
    cpu0.set_core_id(0);
    CPU cpu1;
    cpu1.set_core_id(1);

    // Tick Core 0 to schedule thread
    CHECK(smp.tick(0, cpu0), "Core 0 tick");
    CHECK(smp.current_thread(0) == thread, "Thread running on Core 0");
    CHECK(smp.current_thread(1) != thread, "Thread NOT running on Core 1");

    // Tick Core 1: cannot run thread affine to Core 0
    smp.tick(1, cpu1);
    CHECK(smp.current_thread(1) != thread, "Core 1 cannot run thread running on Core 0");

    // Migrate thread to Core 1:
    // Update affinity to Core 1 only (mask = 0x2)
    thread->set_cpu_affinity(0x2);
    thread->set_state(ThreadState::Ready);
    CHECK(smp.enqueue_thread(thread, 1), "Enqueue on Core 1");

    // Tick Core 1: thread runs on Core 1
    CHECK(smp.tick(1, cpu1), "Core 1 tick");
    CHECK(smp.current_thread(1) == thread, "Thread migrated and running on Core 1");

    std::cout << "[PASS] test_mandatory_f_smp_task_migration" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Stage 195.14: Cross-Core SGI Rescheduling Stress
// ----------------------------------------------------------------------------
static bool test_sgi_cross_core_rescheduling() {
    Memory mem(2 * 1024 * 1024);
    KernelThreadManager mgr;
    SmpScheduler smp(mgr, mem, 2);
    CHECK(smp.initialize(0x50000, 0x4000, 0x10000), "Init SMP scheduler");

    CPU cpu0;
    cpu0.set_core_id(0);
    CPU cpu1;
    cpu1.set_core_id(1);

    // Configure GICv3 for SGI delivery
    auto& gic = cpu0.get_gic();

    // Core 0 sends reschedule IPI (SGI 1) to Core 1
    CHECK(smp.send_reschedule_sgi(0, 1, gic), "Core 0 sends SGI 1 to Core 1");
    CHECK(smp.stats().total_sgi_reschedules == 1, "SGI reschedule counter incremented");

    std::cout << "[PASS] test_sgi_cross_core_rescheduling" << std::endl;
    return true;
}

int main() {
    std::cout << "Running Stage 195.10-195.14 Timer, Sync & SMP Stress Tests..." << std::endl;

    if (!test_timer_masking_and_critical_section()) return 1;
    if (!test_mandatory_g_wait_queue_races()) return 1;
    if (!test_spinlock_memory_ordering_stress()) return 1;
    if (!test_mandatory_f_smp_task_migration()) return 1;
    if (!test_sgi_cross_core_rescheduling()) return 1;

    std::cout << "\nAll Stage 195.10-195.14 tests PASSED successfully." << std::endl;
    return 0;
}

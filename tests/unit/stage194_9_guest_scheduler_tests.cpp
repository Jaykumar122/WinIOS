// ============================================================================
// Stage 194.9: Guest Priority Scheduler & Run Queues Tests
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/scheduler.h"
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

static bool test_scheduler_initialization_and_idle_thread() {
    Memory mem(4 * 1024 * 1024);
    KernelThreadManager thread_mgr;
    PriorityScheduler scheduler(thread_mgr, mem);

    CHECK(scheduler.initialize(0x10000ULL, 0x4000ULL, 0x2000ULL), "Initialize scheduler");
    CHECK(scheduler.current_thread() != nullptr, "Current thread must not be null");
    CHECK(scheduler.idle_thread() != nullptr, "Idle thread must not be null");
    CHECK(scheduler.current_thread() == scheduler.idle_thread(), "Initial current thread must be idle thread");
    CHECK(scheduler.idle_thread()->priority() == 0, "Idle thread priority must be 0");
    CHECK(scheduler.idle_thread()->name() == "idle_thread", "Idle thread name matches");
    CHECK(scheduler.ready_bitmap() == 0, "Ready bitmap must be 0 initially");
    CHECK(scheduler.ready_thread_count() == 0, "Ready thread count must be 0");

    // Verify machine code written to guest RAM
    CHECK(mem.read_u32(0x2000) == 0xD503207Fu, "First instruction in RAM must be WFI");
    CHECK(mem.read_u32(0x2004) == 0x17FFFFFFu, "Second instruction in RAM must be branch to self-4");

    std::cout << "[PASS] test_scheduler_initialization_and_idle_thread" << std::endl;
    return true;
}

static bool test_priority_queueing_and_selection() {
    Memory mem(4 * 1024 * 1024);
    KernelThreadManager thread_mgr;
    PriorityScheduler scheduler(thread_mgr, mem);
    CHECK(scheduler.initialize(), "Init scheduler");

    auto* t_low = thread_mgr.create_thread("t_low", 0x1000, 0, 0x20000, 0x4000, 10);
    auto* t_med = thread_mgr.create_thread("t_med", 0x2000, 0, 0x30000, 0x4000, 20);
    auto* t_high = thread_mgr.create_thread("t_high", 0x3000, 0, 0x40000, 0x4000, 30);

    // Enqueue out-of-order
    CHECK(scheduler.enqueue_thread(t_low), "Enqueue low");
    CHECK(scheduler.enqueue_thread(t_high), "Enqueue high");
    CHECK(scheduler.enqueue_thread(t_med), "Enqueue med");

    CHECK(scheduler.ready_thread_count() == 3, "3 threads in ready queues");
    const std::uint32_t expected_bitmap = (1u << 10) | (1u << 20) | (1u << 30);
    CHECK(scheduler.ready_bitmap() == expected_bitmap, "Bitmap must reflect priorities 10, 20, 30");

    // Dequeue in strict priority order
    auto* first = scheduler.pick_next_thread();
    CHECK(first == t_high, "First picked must be t_high (prio 30)");
    CHECK((scheduler.ready_bitmap() & (1u << 30)) == 0, "Bit 30 must now be cleared");

    auto* second = scheduler.pick_next_thread();
    CHECK(second == t_med, "Second picked must be t_med (prio 20)");

    auto* third = scheduler.pick_next_thread();
    CHECK(third == t_low, "Third picked must be t_low (prio 10)");

    CHECK(scheduler.ready_bitmap() == 0, "Bitmap must now be 0");
    auto* fourth = scheduler.pick_next_thread();
    CHECK(fourth == scheduler.idle_thread(), "When empty, pick_next_thread must return idle thread");

    std::cout << "[PASS] test_priority_queueing_and_selection" << std::endl;
    return true;
}

static bool test_round_robin_among_equal_priorities() {
    Memory mem(4 * 1024 * 1024);
    KernelThreadManager thread_mgr;
    PriorityScheduler scheduler(thread_mgr, mem);
    CHECK(scheduler.initialize(), "Init scheduler");

    auto* t1 = thread_mgr.create_thread("t1", 0x1000, 0, 0x20000, 0x4000, 16);
    auto* t2 = thread_mgr.create_thread("t2", 0x2000, 0, 0x30000, 0x4000, 16);
    auto* t3 = thread_mgr.create_thread("t3", 0x3000, 0, 0x40000, 0x4000, 16);

    scheduler.enqueue_thread(t1);
    scheduler.enqueue_thread(t2);
    scheduler.enqueue_thread(t3);

    auto* p1 = scheduler.pick_next_thread();
    CHECK(p1 == t1, "FIFO round-robin order 1");

    // Re-enqueue t1 at back of queue
    scheduler.enqueue_thread(t1);

    auto* p2 = scheduler.pick_next_thread();
    CHECK(p2 == t2, "FIFO round-robin order 2");

    auto* p3 = scheduler.pick_next_thread();
    CHECK(p3 == t3, "FIFO round-robin order 3");

    auto* p4 = scheduler.pick_next_thread();
    CHECK(p4 == t1, "FIFO round-robin order 4 (re-enqueued t1)");

    std::cout << "[PASS] test_round_robin_among_equal_priorities" << std::endl;
    return true;
}

static bool test_quantum_expiration_and_preemption() {
    Memory mem(4 * 1024 * 1024);
    KernelThreadManager thread_mgr;
    PriorityScheduler scheduler(thread_mgr, mem);
    CHECK(scheduler.initialize(), "Init scheduler");

    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);

    auto* t1 = thread_mgr.create_thread("t1", 0x1000, 0, 0x20000, 0x4000, 16);
    auto* t2 = thread_mgr.create_thread("t2", 0x2000, 0, 0x30000, 0x4000, 16);

    t1->set_time_slice(3); // 3 ticks quantum
    scheduler.enqueue_thread(t1);
    scheduler.enqueue_thread(t2);

    // Initial tick wakes from idle and starts t1
    scheduler.tick(cpu);
    CHECK(scheduler.current_thread() == t1, "t1 starts running");

    // Tick 1
    scheduler.tick(cpu);
    CHECK(scheduler.current_thread() == t1, "t1 still running on tick 1");
    CHECK(t1->time_slice() == 2, "quantum decremented to 2");

    // Tick 2
    scheduler.tick(cpu);
    CHECK(scheduler.current_thread() == t1, "t1 still running on tick 2");
    CHECK(t1->time_slice() == 1, "quantum decremented to 1");

    // Tick 3: quantum expires -> preemption!
    scheduler.tick(cpu);
    CHECK(scheduler.current_thread() == t2, "t2 preempts t1 upon quantum expiration");
    CHECK(t1->state() == ThreadState::Ready, "t1 re-queued as Ready");
    CHECK(scheduler.stats().total_preemptions >= 1, "Preemption recorded");

    std::cout << "[PASS] test_quantum_expiration_and_preemption" << std::endl;
    return true;
}

static bool test_higher_priority_preemption() {
    Memory mem(4 * 1024 * 1024);
    KernelThreadManager thread_mgr;
    PriorityScheduler scheduler(thread_mgr, mem);
    CHECK(scheduler.initialize(), "Init scheduler");

    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);

    auto* t_norm = thread_mgr.create_thread("t_norm", 0x1000, 0, 0x20000, 0x4000, 16);
    auto* t_real = thread_mgr.create_thread("t_real", 0x5000, 0, 0x50000, 0x4000, 31);

    scheduler.enqueue_thread(t_norm);
    scheduler.tick(cpu);
    CHECK(scheduler.current_thread() == t_norm, "t_norm running");

    // High priority thread becomes ready
    scheduler.enqueue_thread(t_real);

    // Next tick immediately preempts t_norm despite remaining quantum
    scheduler.tick(cpu);
    CHECK(scheduler.current_thread() == t_real, "t_real immediately preempts t_norm");
    CHECK(t_norm->state() == ThreadState::Ready, "t_norm preserved in ready queue");

    std::cout << "[PASS] test_higher_priority_preemption" << std::endl;
    return true;
}

static bool test_voluntary_yield_and_blocking() {
    Memory mem(4 * 1024 * 1024);
    KernelThreadManager thread_mgr;
    PriorityScheduler scheduler(thread_mgr, mem);
    CHECK(scheduler.initialize(), "Init scheduler");

    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);

    auto* t1 = thread_mgr.create_thread("t1", 0x1000, 0, 0x20000, 0x4000, 20);
    auto* t2 = thread_mgr.create_thread("t2", 0x2000, 0, 0x30000, 0x4000, 20);

    scheduler.enqueue_thread(t1);
    scheduler.enqueue_thread(t2);
    scheduler.tick(cpu);
    CHECK(scheduler.current_thread() == t1, "t1 active");

    // t1 voluntarily yields
    CHECK(scheduler.yield(cpu), "t1 yields");
    CHECK(scheduler.current_thread() == t2, "t2 now running after yield");
    CHECK(scheduler.stats().total_yield_operations == 1, "Yield operation counted");

    // t2 blocks on Mutex
    CHECK(scheduler.block_current(WaitReason::Mutex, cpu), "t2 blocks on mutex");
    CHECK(t2->state() == ThreadState::Blocked, "t2 is Blocked");
    CHECK(t2->wait_reason() == WaitReason::Mutex, "Wait reason is Mutex");
    CHECK(scheduler.current_thread() == t1, "t1 resumed after t2 blocked");

    // Unblock t2
    CHECK(scheduler.unblock_thread(t2), "Unblock t2");
    CHECK(t2->state() == ThreadState::Ready, "t2 returned to Ready");

    std::cout << "[PASS] test_voluntary_yield_and_blocking" << std::endl;
    return true;
}

static bool test_guest_arm64_idle_execution() {
    Memory mem(4 * 1024 * 1024);
    KernelThreadManager thread_mgr;
    PriorityScheduler scheduler(thread_mgr, mem);
    const std::uint64_t idle_code = 0x2000ULL;
    CHECK(scheduler.initialize(0x10000ULL, 0x4000ULL, idle_code), "Init scheduler");

    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(idle_code);
    cpu.set_sp_el1(0x14000ULL);

    // Step 1: WFI
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == idle_code + 4, "PC advanced past WFI");

    // Step 2: Branch back to WFI
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == idle_code, "PC looped back to WFI");

    std::cout << "[PASS] test_guest_arm64_idle_execution" << std::endl;
    return true;
}

int main() {
    std::cout << "Running Stage 194.9 Guest Priority Scheduler Tests..." << std::endl;

    if (!test_scheduler_initialization_and_idle_thread()) return 1;
    if (!test_priority_queueing_and_selection()) return 1;
    if (!test_round_robin_among_equal_priorities()) return 1;
    if (!test_quantum_expiration_and_preemption()) return 1;
    if (!test_higher_priority_preemption()) return 1;
    if (!test_voluntary_yield_and_blocking()) return 1;
    if (!test_guest_arm64_idle_execution()) return 1;

    std::cout << "\nAll Stage 194.9 Guest Priority Scheduler tests PASSED successfully." << std::endl;
    return 0;
}

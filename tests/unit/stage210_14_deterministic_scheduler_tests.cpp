#include <iostream>
#include <vector>
#include <cstdint>

#include "cpu/cpu.h"
#include "kernel/scheduler.h"
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

void test_priority_run_queues_and_selection() {
    std::cout << "[Test 1] Stage 210.14: 32-Tier Run Queues & Strict Priority Selection...\n";

    Memory mem(0x100000);
    KernelThreadManager mgr;
    PriorityScheduler sched(mgr, mem);

    TEST_CHECK(sched.initialize(), "PriorityScheduler initialized with idle thread");
    TEST_CHECK(sched.idle_thread() != nullptr, "Idle thread created");
    TEST_CHECK(sched.idle_thread()->priority() == 0, "Idle thread priority is 0");

    // Create three threads with distinct priorities: low (8), mid (16), high (28)
    auto* t_low = mgr.create_thread("t_low", 0x2000ULL, 0, 0x4000ULL, 0x2000ULL, 8);
    auto* t_mid = mgr.create_thread("t_mid", 0x2000ULL, 0, 0x6000ULL, 0x2000ULL, 16);
    auto* t_high = mgr.create_thread("t_high", 0x2000ULL, 0, 0x8000ULL, 0x2000ULL, 28);

    TEST_CHECK(sched.enqueue_thread(t_low), "Enqueue t_low");
    TEST_CHECK(sched.enqueue_thread(t_mid), "Enqueue t_mid");
    TEST_CHECK(sched.enqueue_thread(t_high), "Enqueue t_high");

    // Ready bitmap should have bits 8, 16, 28 set
    const std::uint32_t bitmap = sched.ready_bitmap();
    TEST_CHECK((bitmap & (1u << 8)) != 0, "Bitmap bit 8 set");
    TEST_CHECK((bitmap & (1u << 16)) != 0, "Bitmap bit 16 set");
    TEST_CHECK((bitmap & (1u << 28)) != 0, "Bitmap bit 28 set");

    // Scheduler must select highest priority thread (t_high, prio 28)
    auto picked = sched.pick_next_thread();
    TEST_CHECK(picked == t_high, "Highest priority thread (28) picked first");

    // Next must be t_mid (16)
    picked = sched.pick_next_thread();
    TEST_CHECK(picked == t_mid, "Medium priority thread (16) picked second");

    // Next must be t_low (8)
    picked = sched.pick_next_thread();
    TEST_CHECK(picked == t_low, "Low priority thread (8) picked third");

    // When all queues empty, must pick idle thread
    picked = sched.pick_next_thread();
    TEST_CHECK(picked == sched.idle_thread(), "Idle thread picked when all queues empty");
}

void test_scheduler_round_robin_and_yield() {
    std::cout << "[Test 2] Stage 210.14: Round-Robin at Same Priority & Yield...\n";

    CPU cpu;
    Memory mem(0x100000);
    KernelThreadManager mgr;
    PriorityScheduler sched(mgr, mem);
    sched.initialize();

    auto* t1 = mgr.create_thread("peer1", 0x2000ULL, 0, 0x4000ULL, 0x2000ULL, 20);
    auto* t2 = mgr.create_thread("peer2", 0x2000ULL, 0, 0x6000ULL, 0x2000ULL, 20);

    sched.enqueue_thread(t1);
    sched.enqueue_thread(t2);

    // First pick is t1
    auto first = sched.pick_next_thread();
    TEST_CHECK(first == t1, "t1 picked first");

    // Dispatch t1
    cpu.set_program_counter(0x2000ULL);
    TEST_CHECK(sched.yield(cpu), "t1 yields CPU");

    // Next pick must be t2
    auto second = sched.pick_next_thread();
    TEST_CHECK(second == t2, "t2 picked after t1 yield (fair round-robin)");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 210.14 Deterministic Scheduler Tests ===\n";
    test_priority_run_queues_and_selection();
    test_scheduler_round_robin_and_yield();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 210.14: All deterministic scheduler tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 210.14: " << g_failures << " tests failed.\n";
        return 1;
    }
}

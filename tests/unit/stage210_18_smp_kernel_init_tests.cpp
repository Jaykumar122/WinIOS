#include <iostream>
#include <vector>
#include <cstdint>

#include "cpu/cpu.h"
#include "kernel/smp_scheduler.h"
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

void test_smp_6_core_initialization() {
    std::cout << "[Test 1] Stage 210.18: 6-Core SMP Scheduler (iPhone 15 A16 Profile)...\n";

    Memory mem(0x200000);
    KernelThreadManager mgr;

    // iPhone 15 A16 topology: 6 virtual cores
    SmpScheduler smp(mgr, mem, 6);
    TEST_CHECK(smp.initialize(0x10000ULL, 0x2000ULL, 0x1000ULL), "Initialize SMP for 6 cores");
    TEST_CHECK(smp.num_cores() == 6, "Core count is 6");

    // Verify per-core idle threads
    for (std::size_t i = 0; i < 6; ++i) {
        auto* idle = smp.idle_thread(i);
        TEST_CHECK(idle != nullptr, "Idle thread for Core " + std::to_string(i) + " exists");
        TEST_CHECK(idle->priority() == 0, "Idle thread priority is 0");
    }

    // Affinity pinned thread: Core 3 only (mask = 1 << 3 = 0x8)
    auto* t_pinned = mgr.create_thread("pinned_core3", 0x2000ULL, 0, 0x4000ULL, 0x2000ULL, 20);
    t_pinned->set_cpu_affinity(1ULL << 3);

    TEST_CHECK(smp.enqueue_thread(t_pinned), "Enqueue pinned thread");
    TEST_CHECK(smp.core_ready_count(3) == 1, "Core 3 ready queue has 1 thread");
    TEST_CHECK(smp.core_ready_count(0) == 0, "Core 0 has 0 ready threads");

    // Thread with affinity to all cores (mask = 0x3F)
    auto* t_free = mgr.create_thread("free_thread", 0x2000ULL, 0, 0x6000ULL, 0x2000ULL, 16);
    t_free->set_cpu_affinity(0x3FULL);

    TEST_CHECK(smp.enqueue_thread(t_free), "Enqueue free thread");
    std::size_t total_ready = 0;
    for (std::size_t i = 0; i < 6; ++i) total_ready += smp.core_ready_count(i);
    TEST_CHECK(total_ready == 2, "Total ready threads across SMP is 2");

    // Load balancing across cores
    smp.balance_load();
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 210.18 SMP Kernel Init Tests ===\n";
    test_smp_6_core_initialization();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 210.18: All SMP kernel init tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 210.18: " << g_failures << " tests failed.\n";
        return 1;
    }
}

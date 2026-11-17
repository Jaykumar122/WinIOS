#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/bsd_process.h"
#include "kernel/multi_process_scheduler.h"
#include "kernel/thread.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << std::endl; \
            ++g_failures; \
        } else { \
            std::cout << "  [OK] " << (msg) << std::endl; \
        } \
    } while (0)

void test_cross_process_ttbr0_switching() {
    std::cout << "[Test 1] Multi-Process Address Space & TTBR0_EL1 Switching..." << std::endl;

    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    BsdProcessManager proc_mgr;
    proc_mgr.initialize();

    MultiProcessScheduler sched(thread_mgr, proc_mgr, mem);

    // Create 2 processes
    auto* proc_a = proc_mgr.create_process("proc_a", 1);
    auto* proc_b = proc_mgr.create_process("proc_b", 1);
    const pid_t pid_a = proc_a->pid();
    const pid_t pid_b = proc_b->pid();

    const std::uint64_t root_a_pa = ppm.allocate_page();
    const std::uint64_t root_b_pa = ppm.allocate_page();
    TEST_CHECK(root_a_pa != root_b_pa, "Distinct root L2 page tables");

    UserAddressSpace uas_a(mem, ppm, root_a_pa, "uas_a");
    UserAddressSpace uas_b(mem, ppm, root_b_pa, "uas_b");

    sched.register_process_address_space(pid_a, &uas_a);
    sched.register_process_address_space(pid_b, &uas_b);

    // Create threads for each process
    auto* thread_a = thread_mgr.create_thread("thread_a", 0x10000ULL, pid_a, 0x70000ULL, 0x10000ULL, 16, true);
    auto* thread_b = thread_mgr.create_thread("thread_b", 0x20000ULL, pid_b, 0x70000ULL, 0x10000ULL, 16, true);

    sched.enqueue_thread(thread_a);
    sched.enqueue_thread(thread_b);

    CPU cpu;
    cpu.reset();

    // 1. Dispatch thread_a -> CPU TTBR0 should become root_a_pa
    TEST_CHECK(sched.schedule_next(cpu), "Dispatched thread_a");
    TEST_CHECK(sched.current_pid() == pid_a, "Current PID is pid_a");
    TEST_CHECK(cpu.get_mmu().get_ttbr0_el1() == root_a_pa, "CPU TTBR0_EL1 matches root_a_pa");
    TEST_CHECK(sched.stats().total_address_space_switches == 1, "Address space switch count is 1");

    // 2. Dispatch thread_b -> CPU TTBR0 should become root_b_pa
    TEST_CHECK(sched.schedule_next(cpu), "Dispatched thread_b");
    TEST_CHECK(sched.current_pid() == pid_b, "Current PID is pid_b");
    TEST_CHECK(cpu.get_mmu().get_ttbr0_el1() == root_b_pa, "CPU TTBR0_EL1 matches root_b_pa");
    TEST_CHECK(sched.stats().total_address_space_switches == 2, "Address space switch count is 2");
}

void test_architectural_register_preservation() {
    std::cout << "[Test 2] Cross-Process Architectural Register Preservation..." << std::endl;

    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    MultiProcessScheduler sched(thread_mgr, proc_mgr, mem);

    auto* proc_a = proc_mgr.create_process("proc_a", 1);
    auto* proc_b = proc_mgr.create_process("proc_b", 1);

    auto* thread_a = thread_mgr.create_thread("thread_a", 0x10000ULL, proc_a->pid(), 0x70000ULL, 0x10000ULL, 16, true);
    auto* thread_b = thread_mgr.create_thread("thread_b", 0x20000ULL, proc_b->pid(), 0x70000ULL, 0x10000ULL, 16, true);

    sched.enqueue_thread(thread_a);
    sched.enqueue_thread(thread_b);

    CPU cpu;
    cpu.reset();

    // Run thread_a and mutate registers
    sched.schedule_next(cpu);
    cpu.write_register(0, 0xAAAA1111ULL);
    cpu.write_register(1, 0xBBBB2222ULL);
    cpu.set_program_counter(0x10040ULL);

    // Yield to thread_b
    sched.yield(cpu);
    TEST_CHECK(sched.current_thread() == thread_b, "Switched to thread_b");

    // Mutate registers in thread_b
    cpu.write_register(0, 0xCCCC3333ULL);
    cpu.write_register(1, 0xDDDD4444ULL);
    cpu.set_program_counter(0x20080ULL);

    // Yield back to thread_a
    sched.yield(cpu);
    TEST_CHECK(sched.current_thread() == thread_a, "Switched back to thread_a");

    // Verify thread_a registers were perfectly restored!
    TEST_CHECK(cpu.read_register(0) == 0xAAAA1111ULL, "Restored X0 for thread_a");
    TEST_CHECK(cpu.read_register(1) == 0xBBBB2222ULL, "Restored X1 for thread_a");
    TEST_CHECK(cpu.get_program_counter() == 0x10040ULL, "Restored PC for thread_a");
}

void test_quantum_preemption() {
    std::cout << "[Test 3] Time-Slice Quantum Preemption..." << std::endl;

    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    MultiProcessScheduler sched(thread_mgr, proc_mgr, mem);

    auto* proc_a = proc_mgr.create_process("proc_a", 1);
    auto* proc_b = proc_mgr.create_process("proc_b", 1);

    auto* thread_a = thread_mgr.create_thread("thread_a", 0x10000ULL, proc_a->pid(), 0x70000ULL, 0x10000ULL, 16, true);
    auto* thread_b = thread_mgr.create_thread("thread_b", 0x20000ULL, proc_b->pid(), 0x70000ULL, 0x10000ULL, 16, true);

    sched.enqueue_thread(thread_a);
    sched.enqueue_thread(thread_b);

    CPU cpu;
    cpu.reset();

    // Start with thread_a
    sched.schedule_next(cpu);
    TEST_CHECK(sched.current_thread() == thread_a, "Started thread_a");

    // Ticks 1, 2, 3: still thread_a
    sched.tick(cpu);
    sched.tick(cpu);
    sched.tick(cpu);
    TEST_CHECK(sched.current_thread() == thread_a, "Still thread_a during quantum");

    // Tick 4: quantum expires (4 ticks) -> preempted to thread_b!
    sched.tick(cpu);
    TEST_CHECK(sched.current_thread() == thread_b, "Preempted to thread_b upon quantum expiration");
    TEST_CHECK(sched.stats().total_preemptions == 1, "Total preemptions count is 1");

    // Ticks 1, 2, 3, 4 for thread_b -> preempts back to thread_a!
    sched.tick(cpu);
    sched.tick(cpu);
    sched.tick(cpu);
    sched.tick(cpu);
    TEST_CHECK(sched.current_thread() == thread_a, "Preempted back to thread_a");
    TEST_CHECK(sched.stats().total_preemptions == 2, "Total preemptions count is 2");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 212.18 Multi-Process Scheduler Tests ===" << std::endl;
    test_cross_process_ttbr0_switching();
    test_architectural_register_preservation();
    test_quantum_preemption();

    if (g_failures == 0) {
        std::cout << "[PASS] Stage 212.18: All multi-process scheduler tests passed." << std::endl;
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 212.18: " << g_failures << " tests failed." << std::endl;
        return 1;
    }
}

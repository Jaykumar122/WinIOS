#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>

#include "kernel/multi_process_scheduler.h"
#include "kernel/thread.h"
#include "kernel/bsd_process.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "cpu/cpu.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;

void test_scheduler_quantum_and_preemption() {
    std::cout << "[Test 1] Stage 219.7: Deterministic Multi-Process Scheduler Quantum & Preemption...\n";

    Memory mem(0x400000); // 4 MB
    KernelThreadManager th_mgr;
    BsdProcessManager proc_mgr;
    proc_mgr.initialize();

    MultiProcessScheduler scheduler(th_mgr, proc_mgr, mem);

    KernelThread* t1 = th_mgr.create_thread("task_1", 0x1000, 0, 0x7FFF0000, 0x10000, 16, true);
    KernelThread* t2 = th_mgr.create_thread("task_2", 0x2000, 0, 0x7FFE0000, 0x10000, 16, true);
    t1->set_owner_pid(1);
    t2->set_owner_pid(1);

    scheduler.enqueue_thread(t1);
    scheduler.enqueue_thread(t2);
    TEST_ASSERT(scheduler.ready_count() == 2);

    CPU cpu;
    cpu.reset();

    // First schedule: dispatches t1
    TEST_ASSERT(scheduler.schedule_next(cpu));
    TEST_ASSERT(scheduler.current_thread() == t1);
    TEST_ASSERT(t1->state() == ThreadState::Running);
    TEST_ASSERT(scheduler.ready_count() == 1); // t2 in queue

    // 3 ticks: quantum decreases from 4 to 1, no preemption yet
    TEST_ASSERT(scheduler.tick(cpu));
    TEST_ASSERT(scheduler.tick(cpu));
    TEST_ASSERT(scheduler.tick(cpu));
    TEST_ASSERT(scheduler.current_thread() == t1);
    TEST_ASSERT(scheduler.stats().total_preemptions == 0);

    // 4th tick: quantum expires (reaches 0), preempts t1 and switches to t2
    TEST_ASSERT(scheduler.tick(cpu));
    TEST_ASSERT(scheduler.current_thread() == t2);
    TEST_ASSERT(t2->state() == ThreadState::Running);
    TEST_ASSERT(t1->state() == ThreadState::Ready);
    TEST_ASSERT(scheduler.stats().total_preemptions == 1);

    std::cout << "  -> Time-slice quantum exhaustion and preemption switch verified.\n";
}

void test_scheduler_voluntary_yield() {
    std::cout << "[Test 2] Stage 219.7: Voluntary Scheduler Yield...\n";

    Memory mem(0x400000);
    KernelThreadManager th_mgr;
    BsdProcessManager proc_mgr;
    proc_mgr.initialize();

    MultiProcessScheduler scheduler(th_mgr, proc_mgr, mem);

    KernelThread* t1 = th_mgr.create_thread("yield_1", 0x1000, 0, 0x7FFF0000, 0x10000, 16, true);
    KernelThread* t2 = th_mgr.create_thread("yield_2", 0x2000, 0, 0x7FFE0000, 0x10000, 16, true);
    t1->set_owner_pid(1);
    t2->set_owner_pid(1);

    scheduler.enqueue_thread(t1);
    scheduler.enqueue_thread(t2);

    CPU cpu;
    cpu.reset();
    TEST_ASSERT(scheduler.schedule_next(cpu));
    TEST_ASSERT(scheduler.current_thread() == t1);

    // Voluntary yield
    TEST_ASSERT(scheduler.yield(cpu));
    TEST_ASSERT(scheduler.current_thread() == t2);
    TEST_ASSERT(scheduler.stats().total_yields == 1);

    // Yield again: switches back to t1
    TEST_ASSERT(scheduler.yield(cpu));
    TEST_ASSERT(scheduler.current_thread() == t1);
    TEST_ASSERT(scheduler.stats().total_yields == 2);

    std::cout << "  -> Voluntary yield and round-robin re-dispatch verified.\n";
}

void test_cross_process_context_switch_and_registers() {
    std::cout << "[Test 3] Stage 219.8: Cross-Process Context Switch, TTBR0 & Architectural Registers...\n";

    Memory mem(0x800000); // 8 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    TEST_ASSERT(ppm.initialize(false));

    uint64_t root_a = ppm.allocate_page();
    uint64_t root_b = ppm.allocate_page();
    TEST_ASSERT(root_a != 0 && root_b != 0 && root_a != root_b);

    UserAddressSpace uas_a(mem, ppm, root_a, "proc_a_uas");
    UserAddressSpace uas_b(mem, ppm, root_b, "proc_b_uas");

    KernelThreadManager th_mgr;
    BsdProcessManager proc_mgr;
    proc_mgr.initialize();

    BsdProcess* p_a = proc_mgr.create_process("proc_a", 1, nullptr, &uas_a);
    BsdProcess* p_b = proc_mgr.create_process("proc_b", 1, nullptr, &uas_b);

    MultiProcessScheduler scheduler(th_mgr, proc_mgr, mem);
    scheduler.register_process_address_space(p_a->pid(), &uas_a);
    scheduler.register_process_address_space(p_b->pid(), &uas_b);

    KernelThread* t_a = th_mgr.create_thread("t_proc_a", 0x10001000, 0x1111, 0x7FFF0000, 0x10000, 16, true);
    KernelThread* t_b = th_mgr.create_thread("t_proc_b", 0x20002000, 0x2222, 0x7FFE0000, 0x10000, 16, true);
    t_a->set_owner_pid(p_a->pid());
    t_b->set_owner_pid(p_b->pid());

    t_a->set_tpidr_el0(0xAAAA0000ULL);
    t_b->set_tpidr_el0(0xBBBB0000ULL);

    scheduler.enqueue_thread(t_a);
    scheduler.enqueue_thread(t_b);

    CPU cpu;
    cpu.reset();

    // Dispatch Thread A
    TEST_ASSERT(scheduler.schedule_next(cpu));
    TEST_ASSERT(scheduler.current_thread() == t_a);
    TEST_ASSERT(scheduler.current_pid() == p_a->pid());
    TEST_ASSERT(cpu.get_mmu().get_ttbr0_el1() == root_a);
    TEST_ASSERT(cpu.get_tpidr_el0() == 0xAAAA0000ULL);
    TEST_ASSERT(cpu.get_program_counter() == 0x10001000);
    TEST_ASSERT(cpu.read_register(0) == 0x1111);

    // Thread A runs and modifies registers and TLS
    cpu.write_register(5, 0x99999);
    cpu.set_tpidr_el0(0xAAAA1234ULL);

    // Switch to Thread B (different process)
    TEST_ASSERT(scheduler.yield(cpu));
    TEST_ASSERT(scheduler.current_thread() == t_b);
    TEST_ASSERT(scheduler.current_pid() == p_b->pid());
    // MMU TTBR0_EL1 must have switched to root_b
    TEST_ASSERT(cpu.get_mmu().get_ttbr0_el1() == root_b);
    TEST_ASSERT(cpu.get_tpidr_el0() == 0xBBBB0000ULL);
    TEST_ASSERT(cpu.get_program_counter() == 0x20002000);
    TEST_ASSERT(cpu.read_register(0) == 0x2222);
    TEST_ASSERT(scheduler.stats().total_address_space_switches >= 2);

    // Switch back to Thread A: verify Thread A's saved state is fully restored
    TEST_ASSERT(scheduler.yield(cpu));
    TEST_ASSERT(scheduler.current_thread() == t_a);
    TEST_ASSERT(cpu.get_mmu().get_ttbr0_el1() == root_a);
    TEST_ASSERT(cpu.get_tpidr_el0() == 0xAAAA1234ULL);
    TEST_ASSERT(cpu.read_register(5) == 0x99999);

    std::cout << "  -> Cross-process MMU TTBR0_EL1 and register state preservation verified.\n";
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 219.8: MultiProcessScheduler & Context Switch Tests\n";
    std::cout << "=================================================================\n";

    test_scheduler_quantum_and_preemption();
    test_scheduler_voluntary_yield();
    test_cross_process_context_switch_and_registers();

    std::cout << "\n[PASS] All Stage 219.8 tests passed successfully!\n";
    return 0;
}

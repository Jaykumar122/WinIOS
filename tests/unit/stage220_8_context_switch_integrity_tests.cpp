// ============================================================================
// Stage 220.8 Unit Tests: Context-Switch Integrity & Register Isolation
// ============================================================================

#include <iostream>
#include <vector>
#include <cstdint>
#include <cassert>
#include <memory>

#include "cpu/cpu.h"
#include "kernel/thread.h"
#include "kernel/bsd_process.h"
#include "kernel/multi_process_scheduler.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;

static void test_same_process_context_switch() {
    std::cout << "[Test 1] Same-process context switch preserves registers without TTBR0 switch...\n";
    Memory mem(0x20000000ULL);
    CPU cpu;
    cpu.reset();

    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());

    BsdProcessManager proc_mgr;
    TEST_ASSERT(proc_mgr.initialize());
    KernelThreadManager th_mgr;
    MultiProcessScheduler scheduler(th_mgr, proc_mgr, mem);

    // Process A (PID 101, ASID 1)
    const uint64_t root_a = ppm.allocate_page();
    UserAddressSpace uas_a(mem, ppm, root_a, "proc_a");
    uas_a.set_asid(1);
    uas_a.set_owner_pid(101);
    scheduler.register_process_address_space(101, &uas_a);

    KernelThread* t1 = th_mgr.create_thread("t1", 0x10000, 1, 0x70000, 0x10000, 16, true);
    KernelThread* t2 = th_mgr.create_thread("t2", 0x14000, 1, 0x60000, 0x10000, 16, true);
    TEST_ASSERT(t1 != nullptr);
    TEST_ASSERT(t2 != nullptr);
    t1->set_owner_pid(101);
    t2->set_owner_pid(101);

    // Set initial architectural states for both threads
    t1->tcb().gpr[0] = 0x1111222233334444ULL;
    t1->tcb().gpr[1] = 0xAAAABBBBCCCCDDDDULL;
    t1->set_tpidr_el0(0x00030000ULL);

    t2->tcb().gpr[0] = 0x5555666677778888ULL;
    t2->tcb().gpr[1] = 0x9999888877776666ULL;
    t2->set_tpidr_el0(0x00034000ULL);

    scheduler.enqueue_thread(t1);
    scheduler.enqueue_thread(t2);

    // Schedule T1
    TEST_ASSERT(scheduler.schedule_next(cpu));
    TEST_ASSERT(scheduler.current_thread() == t1);
    TEST_ASSERT(scheduler.current_pid() == 101);
    TEST_ASSERT(cpu.read_register(0) == 0x1111222233334444ULL);
    TEST_ASSERT(cpu.read_register(1) == 0xAAAABBBBCCCCDDDDULL);
    TEST_ASSERT(cpu.get_tpidr_el0() == 0x00030000ULL);
    const std::uint64_t ttbr0_a = scheduler.current_ttbr0();
    TEST_ASSERT(cpu.get_mmu().get_ttbr0_el1() == ttbr0_a);

    // Mutate CPU register during T1 execution
    cpu.write_register(0, 0x9999999999999999ULL);
    cpu.write_register(2, 0x7777777777777777ULL);

    // Yield: switches to T2 (same process)
    TEST_ASSERT(scheduler.yield(cpu));
    TEST_ASSERT(scheduler.current_thread() == t2);
    TEST_ASSERT(scheduler.current_pid() == 101);
    TEST_ASSERT(scheduler.current_ttbr0() == ttbr0_a); // Must NOT switch TTBR0

    TEST_ASSERT(scheduler.stats().total_context_switches == 2);
    TEST_ASSERT(scheduler.stats().same_process_context_switches == 1);
    TEST_ASSERT(scheduler.stats().cross_process_context_switches == 1); // Initial schedule from null was cross
    TEST_ASSERT(scheduler.stats().total_address_space_switches == 1);

    // Verify T2 registers loaded without corruption
    TEST_ASSERT(cpu.read_register(0) == 0x5555666677778888ULL);
    TEST_ASSERT(cpu.read_register(1) == 0x9999888877776666ULL);
    TEST_ASSERT(cpu.get_tpidr_el0() == 0x00034000ULL);

    // Switch back to T1
    TEST_ASSERT(scheduler.yield(cpu));
    TEST_ASSERT(scheduler.current_thread() == t1);
    TEST_ASSERT(cpu.read_register(0) == 0x9999999999999999ULL); // Saved mutation restored!
    TEST_ASSERT(cpu.read_register(2) == 0x7777777777777777ULL);
    TEST_ASSERT(cpu.get_tpidr_el0() == 0x00030000ULL);
    TEST_ASSERT(scheduler.stats().same_process_context_switches == 2);

    std::cout << "  Passed same-process context switch test.\n";
}

static void test_cross_process_context_switch() {
    std::cout << "[Test 2] Cross-process context switch switches TTBR0 and flushes/isolates TLB...\n";
    Memory mem(0x20000000ULL);
    CPU cpu;
    cpu.reset();

    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());

    BsdProcessManager proc_mgr;
    TEST_ASSERT(proc_mgr.initialize());
    KernelThreadManager th_mgr;
    MultiProcessScheduler scheduler(th_mgr, proc_mgr, mem);

    const uint64_t root_a = ppm.allocate_page();
    UserAddressSpace uas_a(mem, ppm, root_a, "proc_a");
    uas_a.set_asid(1);
    uas_a.set_owner_pid(101);
    scheduler.register_process_address_space(101, &uas_a);

    const uint64_t root_b = ppm.allocate_page();
    UserAddressSpace uas_b(mem, ppm, root_b, "proc_b");
    uas_b.set_asid(2);
    uas_b.set_owner_pid(102);
    scheduler.register_process_address_space(102, &uas_b);

    KernelThread* ta = th_mgr.create_thread("ta", 0x10000, 1, 0x70000, 0x10000, 16, true);
    KernelThread* tb = th_mgr.create_thread("tb", 0x10000, 1, 0x60000, 0x10000, 16, true);
    TEST_ASSERT(ta != nullptr);
    TEST_ASSERT(tb != nullptr);
    ta->set_owner_pid(101);
    tb->set_owner_pid(102);

    ta->tcb().gpr[5] = 0xAAAA5555AAAA5555ULL;
    tb->tcb().gpr[5] = 0xBBBB6666BBBB6666ULL;
    ta->set_tpidr_el0(0x00030000ULL);
    tb->set_tpidr_el0(0x00040000ULL);

    scheduler.enqueue_thread(ta);
    scheduler.enqueue_thread(tb);

    // Run Thread A
    TEST_ASSERT(scheduler.schedule_next(cpu));
    TEST_ASSERT(scheduler.current_pid() == 101);
    const std::uint64_t ttbr0_a = uas_a.root_table_pa_with_asid();
    TEST_ASSERT(cpu.get_mmu().get_ttbr0_el1() == ttbr0_a);
    TEST_ASSERT(cpu.read_register(5) == 0xAAAA5555AAAA5555ULL);

    // Yield -> Cross-process switch to Thread B
    TEST_ASSERT(scheduler.yield(cpu));
    TEST_ASSERT(scheduler.current_pid() == 102);
    const std::uint64_t ttbr0_b = uas_b.root_table_pa_with_asid();
    TEST_ASSERT(ttbr0_a != ttbr0_b);
    TEST_ASSERT(cpu.get_mmu().get_ttbr0_el1() == ttbr0_b);
    TEST_ASSERT(cpu.read_register(5) == 0xBBBB6666BBBB6666ULL);
    TEST_ASSERT(cpu.get_tpidr_el0() == 0x00040000ULL);

    TEST_ASSERT(scheduler.stats().cross_process_context_switches == 2);
    TEST_ASSERT(scheduler.stats().total_address_space_switches == 2);
    TEST_ASSERT(scheduler.stats().same_process_context_switches == 0);

    std::cout << "  Passed cross-process context switch test.\n";
}

int main() {
    std::cout << "=== Stage 220.8 Context-Switch Integrity & Register Isolation Tests ===\n";
    test_same_process_context_switch();
    test_cross_process_context_switch();
    std::cout << "All Stage 220.8 tests passed successfully.\n";
    return 0;
}

// ============================================================================
// Stage 220.10 Unit Tests: Thread Creation, Join and Exit Semantics
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
#include "kernel/user_syscall_dispatcher.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/virtual_page_table.h"
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

static void test_thread_creation_and_defenses() {
    std::cout << "[Test 1] Thread creation parameters and defensive validations...\n";
    Memory mem(0x2000000ULL);
    CPU cpu;
    cpu.reset();

    PhysicalPageManager ppm(mem, 0x100000ULL, 0x1000000ULL, 0x80000ULL);
    TEST_ASSERT(ppm.initialize());

    const uint64_t root_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_pa, "test_proc");
    uas.set_asid(1);
    uas.set_owner_pid(101);

    BsdProcess proc(101, 1, "test_proc");
    KernelThreadManager th_mgr;
    BsdProcessManager proc_mgr;
    PriorityScheduler dummy_sched(th_mgr, mem);
    MultiProcessScheduler multi_sched(th_mgr, proc_mgr, mem);

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_multi_process_scheduler(&multi_sched);
    dispatcher.set_thread_manager(&th_mgr);
    dispatcher.set_process_manager(&proc_mgr);

    // 1. Valid thread creation (entry 4-byte aligned, stack top 16-byte aligned)
    cpu.write_register(16, kSysWiniosThreadCreate);
    cpu.write_register(0, 0x10004000ULL); // Entry point
    cpu.write_register(1, 0x12345ULL);    // Argument
    cpu.write_register(2, 0x70000000ULL); // Stack base
    cpu.write_register(3, 0x10000ULL);    // Stack size (64KB, stack top = 0x70010000)
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    const uint64_t worker1_tid = cpu.read_register(0);
    TEST_ASSERT(worker1_tid > 0);
    TEST_ASSERT(proc.thread_count() == 1);
    TEST_ASSERT(multi_sched.ready_count() == 1);

    // 2. Misaligned entry point (0x10004001 not 4-byte aligned) -> EINVAL (22)
    cpu.write_register(16, kSysWiniosThreadCreate);
    cpu.write_register(0, 0x10004001ULL);
    cpu.write_register(1, 0);
    cpu.write_register(2, 0x70020000ULL);
    cpu.write_register(3, 0x10000ULL);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEINVAL);

    // 3. Misaligned stack top (stack_base = 0x70020005, size = 0x10000 -> top 0x70030005 not 16-byte aligned) -> EINVAL
    cpu.write_register(16, kSysWiniosThreadCreate);
    cpu.write_register(0, 0x10004000ULL);
    cpu.write_register(1, 0);
    cpu.write_register(2, 0x70020005ULL);
    cpu.write_register(3, 0x10000ULL);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEINVAL);

    // 4. Kernel space entry point (>= 0x0001000000000000ULL) -> EINVAL
    cpu.write_register(16, kSysWiniosThreadCreate);
    cpu.write_register(0, 0xFFFF000010000000ULL);
    cpu.write_register(1, 0);
    cpu.write_register(2, 0x70020000ULL);
    cpu.write_register(3, 0x10000ULL);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEINVAL);

    std::cout << "  Passed thread creation and parameter defense tests.\n";
}

static void test_thread_exit_and_join_lifecycle() {
    std::cout << "[Test 2] Thread exit, join deadlock detection, and status reaping...\n";
    Memory mem(0x2000000ULL);
    CPU cpu;
    cpu.reset();

    PhysicalPageManager ppm(mem, 0x100000ULL, 0x1000000ULL, 0x80000ULL);
    TEST_ASSERT(ppm.initialize());

    const uint64_t root_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_pa, "test_proc");
    uas.set_asid(1);
    uas.set_owner_pid(101);

    // Map a user page for receiving exit status
    const uint64_t status_va = 0x40000ULL;
    const uint64_t status_pa = ppm.allocate_page();
    TEST_ASSERT(uas.map_user_page(status_va, status_pa, PagePermissions::UserReadWrite));

    BsdProcess proc(101, 1, "test_proc");
    KernelThreadManager th_mgr;
    BsdProcessManager proc_mgr;
    PriorityScheduler dummy_sched(th_mgr, mem);
    MultiProcessScheduler multi_sched(th_mgr, proc_mgr, mem);

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_multi_process_scheduler(&multi_sched);
    dispatcher.set_thread_manager(&th_mgr);
    dispatcher.set_process_manager(&proc_mgr);

    // Create main thread (TID 1)
    KernelThread* main_th = th_mgr.create_thread("main", 0x1000, 0, 0x80000, 0x10000, 16, true);
    main_th->set_owner_pid(101);
    proc.add_thread(main_th);
    multi_sched.register_process_address_space(101, &uas);
    multi_sched.enqueue_thread(main_th);
    TEST_ASSERT(multi_sched.schedule_next(cpu));
    TEST_ASSERT(multi_sched.current_thread() == main_th);

    // Create worker thread via syscall
    cpu.write_register(16, kSysWiniosThreadCreate);
    cpu.write_register(0, 0x10008000ULL);
    cpu.write_register(1, 0x99ULL);
    cpu.write_register(2, 0x70000000ULL);
    cpu.write_register(3, 0x10000ULL);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    const uint64_t worker_tid = cpu.read_register(0);
    TEST_ASSERT(worker_tid != main_th->tid());

    // 1. Main thread attempts to join itself -> EDEADLK (11)
    cpu.write_register(16, kSysWiniosThreadJoin);
    cpu.write_register(0, main_th->tid());
    cpu.write_register(1, status_va);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 11 /* EDEADLK */);

    // 2. Main thread attempts to join non-existent TID 9999 -> ESRCH (3)
    cpu.write_register(16, kSysWiniosThreadJoin);
    cpu.write_register(0, 9999);
    cpu.write_register(1, status_va);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoESRCH);

    // 3. Main thread joins worker while still ready/running -> EBUSY (16)
    cpu.write_register(16, kSysWiniosThreadJoin);
    cpu.write_register(0, worker_tid);
    cpu.write_register(1, status_va);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 16 /* EBUSY */);

    // 4. Switch to worker thread and exit with code 42
    TEST_ASSERT(multi_sched.yield(cpu));
    TEST_ASSERT(multi_sched.current_thread()->tid() == worker_tid);

    cpu.write_register(16, kSysWiniosThreadExit);
    cpu.write_register(0, 42); // Exit code
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));

    KernelThread* worker_th = th_mgr.get_thread(worker_tid);
    TEST_ASSERT(worker_th != nullptr);
    TEST_ASSERT(worker_th->state() == ThreadState::Terminated);
    TEST_ASSERT(worker_th->exit_code() == 42);

    // 5. Switch back to main thread and successfully join terminated worker
    TEST_ASSERT(multi_sched.schedule_next(cpu));
    TEST_ASSERT(multi_sched.current_thread() == main_th);

    cpu.write_register(16, kSysWiniosThreadJoin);
    cpu.write_register(0, worker_tid);
    cpu.write_register(1, status_va);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 0); // Join success

    // Verify exit status written into status_va
    int32_t reaped_code = 0;
    TEST_ASSERT(uas.copyout(reinterpret_cast<const void*>(status_va), &reaped_code, sizeof(reaped_code)));
    TEST_ASSERT(reaped_code == 42);

    std::cout << "  Passed thread exit and join lifecycle tests.\n";
}

int main() {
    std::cout << "=== Stage 220.10 Thread Creation, Join and Exit Semantics Tests ===\n";
    test_thread_creation_and_defenses();
    test_thread_exit_and_join_lifecycle();
    std::cout << "All Stage 220.10 tests passed successfully.\n";
    return 0;
}

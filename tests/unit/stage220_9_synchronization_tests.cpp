// ============================================================================
// Stage 220.9 Unit Tests: Synchronization Primitive Correctness & Mutex Ownership
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

static void test_mutex_basic_and_ownership() {
    std::cout << "[Test 1] Mutex ownership tracking and EPERM on invalid unlock...\n";
    Memory mem(0x1000000ULL);
    CPU cpu;
    cpu.reset();

    PhysicalPageManager ppm(mem, 0x100000ULL, 0x800000ULL, 0x80000ULL);
    TEST_ASSERT(ppm.initialize());

    const uint64_t root_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_pa, "test_proc");
    uas.set_asid(1);
    uas.set_owner_pid(101);

    // Map a user page for the mutex variable
    const uint64_t mutex_va = 0x20000ULL;
    const uint64_t frame_pa = ppm.allocate_page();
    TEST_ASSERT(uas.map_user_page(mutex_va, frame_pa, PagePermissions::UserReadWrite));

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

    KernelThread* t1 = th_mgr.create_thread("t1", 0x1000, 1, 0x70000, 0x10000, 16, true);
    KernelThread* t2 = th_mgr.create_thread("t2", 0x2000, 1, 0x60000, 0x10000, 16, true);
    TEST_ASSERT(t1 != nullptr);
    TEST_ASSERT(t2 != nullptr);
    t1->set_owner_pid(101);
    t2->set_owner_pid(101);

    multi_sched.register_process_address_space(101, &uas);
    multi_sched.enqueue_thread(t1);

    // Dispatch T1
    TEST_ASSERT(multi_sched.schedule_next(cpu));
    TEST_ASSERT(multi_sched.current_thread() == t1);

    // 1. Unlocking an unheld mutex must fail with EPERM (1)
    cpu.write_register(16, kSysWiniosMutexUnlock);
    cpu.write_register(0, mutex_va);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEPERM);

    // 2. Thread 1 locks mutex
    cpu.write_register(16, kSysWiniosMutexLock);
    cpu.write_register(0, mutex_va);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 0); // Success

    // Verify memory and dispatcher record
    uint32_t val = 0;
    TEST_ASSERT(uas.copyout(reinterpret_cast<const void*>(mutex_va), &val, sizeof(val)));
    TEST_ASSERT(val == 1);
    TEST_ASSERT(dispatcher.stats().total_mutex_locks == 1);

    // 3. Switch to Thread 2
    multi_sched.enqueue_thread(t2);
    TEST_ASSERT(multi_sched.yield(cpu));
    TEST_ASSERT(multi_sched.current_thread() == t2);

    // Thread 2 attempts to unlock Thread 1's mutex -> Must fail with EPERM (1)!
    cpu.write_register(16, kSysWiniosMutexUnlock);
    cpu.write_register(0, mutex_va);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEPERM);

    // Thread 2 attempts to lock Thread 1's mutex -> Must fail with EBUSY (16)!
    cpu.write_register(16, kSysWiniosMutexLock);
    cpu.write_register(0, mutex_va);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 16 /* EBUSY */);
    TEST_ASSERT(dispatcher.stats().total_mutex_contentions == 1);

    // 4. Switch back to Thread 1
    TEST_ASSERT(multi_sched.yield(cpu));
    TEST_ASSERT(multi_sched.current_thread() == t1);

    // Thread 1 re-entrant lock attempt -> EDEADLK (11)
    cpu.write_register(16, kSysWiniosMutexLock);
    cpu.write_register(0, mutex_va);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 11 /* EDEADLK */);

    // Thread 1 unlocks -> Success (0)
    cpu.write_register(16, kSysWiniosMutexUnlock);
    cpu.write_register(0, mutex_va);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 0);
    TEST_ASSERT(dispatcher.stats().total_mutex_unlocks == 1);

    // Thread 1 double unlock -> EPERM (1)
    cpu.write_register(16, kSysWiniosMutexUnlock);
    cpu.write_register(0, mutex_va);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEPERM);

    std::cout << "  Passed mutex ownership and EPERM tests.\n";
}

static void test_mutex_alignment_and_faults() {
    std::cout << "[Test 2] Mutex alignment and unmapped fault validation...\n";
    Memory mem(0x1000000ULL);
    CPU cpu;
    cpu.reset();

    PhysicalPageManager ppm(mem, 0x100000ULL, 0x800000ULL, 0x80000ULL);
    TEST_ASSERT(ppm.initialize());

    const uint64_t root_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_pa, "test_proc2");
    uas.set_asid(2);
    uas.set_owner_pid(102);

    BsdProcess proc(102, 1, "test_proc2");
    KernelThreadManager th_mgr;
    BsdProcessManager proc_mgr;
    PriorityScheduler dummy_sched(th_mgr, mem);

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);

    // 1. Misaligned address (0x20001 is not 4-byte aligned)
    cpu.write_register(16, kSysWiniosMutexLock);
    cpu.write_register(0, 0x20001ULL);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEFAULT);

    // 2. Unmapped address (0x30000ULL has no page mapping)
    cpu.write_register(16, kSysWiniosMutexLock);
    cpu.write_register(0, 0x30000ULL);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEFAULT);

    std::cout << "  Passed mutex alignment and fault tests.\n";
}

int main() {
    std::cout << "=== Stage 220.9 Synchronization Primitive Correctness Tests ===\n";
    test_mutex_basic_and_ownership();
    test_mutex_alignment_and_faults();
    std::cout << "All Stage 220.9 tests passed successfully.\n";
    return 0;
}

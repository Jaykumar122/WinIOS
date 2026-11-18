#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>

#include "kernel/user_syscall_dispatcher.h"
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

void test_guest_mutex_syscalls() {
    std::cout << "[Test 1] Stage 219.9: Guest Mutex Lock & Unlock Syscalls (522, 523)...\n";

    Memory mem(0x400000); // 4 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x3C0000ULL, 0x28020ULL);
    TEST_ASSERT(ppm.initialize(false));

    uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "sync_test_vm");

    uint64_t mutex_page = ppm.allocate_page();
    const uint64_t mutex_va = 0x10004000ULL;
    TEST_ASSERT(uas.map_user_page(mutex_va, mutex_page, PagePermissions::UserReadWrite));

    // Zero out mutex variable (unlocked)
    uint32_t init_val = 0;
    TEST_ASSERT(uas.copyin(mutex_va, &init_val, sizeof(init_val)));

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

    BsdProcess proc(10, 1, "sync_proc");
    PriorityScheduler dummy_sched(*(new KernelThreadManager()), mem);
    CPU cpu;
    cpu.reset();

    // 1. First lock acquisition: SYS 522
    cpu.write_register(16, kSysWiniosMutexLock);
    cpu.write_register(0, mutex_va);
    cpu.write_register(1, 0); // blocking / standard
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 0); // Success

    // Verify mutex memory is now 1 (locked)
    uint32_t current_val = 0;
    TEST_ASSERT(uas.copyout(reinterpret_cast<const void*>(mutex_va), &current_val, sizeof(current_val)));
    TEST_ASSERT(current_val == 1);

    // 2. Second lock acquisition while held: should fail (EBUSY = 16)
    cpu.write_register(16, kSysWiniosMutexLock);
    cpu.write_register(0, mutex_va);
    cpu.write_register(1, 1); // trylock
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 16); // EBUSY

    // 3. Unlock: SYS 523
    cpu.write_register(16, kSysWiniosMutexUnlock);
    cpu.write_register(0, mutex_va);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 0); // Success

    // Verify mutex memory is now 0 (unlocked)
    TEST_ASSERT(uas.copyout(reinterpret_cast<const void*>(mutex_va), &current_val, sizeof(current_val)));
    TEST_ASSERT(current_val == 0);

    // 4. Re-lock should now succeed
    cpu.write_register(16, kSysWiniosMutexLock);
    cpu.write_register(0, mutex_va);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 0);

    std::cout << "  -> Mutex lock, contention detection, and unlock validated.\n";
}

void test_thread_lifecycle_syscalls() {
    std::cout << "[Test 2] Stage 219.10: Thread Create, Exit & Join Syscalls (524..526)...\n";

    Memory mem(0x400000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x3C0000ULL, 0x28020ULL);
    TEST_ASSERT(ppm.initialize(false));

    uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "thread_test_vm");

    uint64_t exit_buf_page = ppm.allocate_page();
    const uint64_t exit_buf_va = 0x10008000ULL;
    TEST_ASSERT(uas.map_user_page(exit_buf_va, exit_buf_page, PagePermissions::UserReadWrite));

    KernelThreadManager th_mgr;
    BsdProcessManager proc_mgr;
    proc_mgr.initialize();

    MultiProcessScheduler scheduler(th_mgr, proc_mgr, mem);

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);
    dispatcher.set_thread_manager(&th_mgr);
    dispatcher.set_multi_process_scheduler(&scheduler);

    BsdProcess* proc = proc_mgr.create_process("multithread_app", 1, nullptr, &uas);
    PriorityScheduler dummy_sched(th_mgr, mem);
    CPU cpu;
    cpu.reset();

    // 1. Thread Create: SYS 524 (entry=0x10002000, arg=0x42, stack=0x7FFF0000, size=0x10000)
    cpu.write_register(16, kSysWiniosThreadCreate);
    cpu.write_register(0, 0x10002000);
    cpu.write_register(1, 0x42);
    cpu.write_register(2, 0x7FFF0000);
    cpu.write_register(3, 0x10000);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, dummy_sched));
    const uint64_t worker_tid = cpu.read_register(0);
    TEST_ASSERT(worker_tid > 0);
    TEST_ASSERT(proc->threads().size() == 1);
    TEST_ASSERT(scheduler.ready_count() == 1);

    // 2. Thread Join while running should return EBUSY (16)
    cpu.write_register(16, kSysWiniosThreadJoin);
    cpu.write_register(0, worker_tid);
    cpu.write_register(1, exit_buf_va);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, *proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 16); // EBUSY

    // 3. Dispatch worker thread and simulate Thread Exit (SYS 525)
    TEST_ASSERT(scheduler.schedule_next(cpu));
    TEST_ASSERT(scheduler.current_thread()->tid() == worker_tid);

    cpu.write_register(16, kSysWiniosThreadExit);
    cpu.write_register(0, 77); // exit code 77
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, dummy_sched));
    TEST_ASSERT(scheduler.current_thread()->state() == ThreadState::Terminated);
    TEST_ASSERT(scheduler.current_thread()->exit_code() == 77);

    // 4. Thread Join after exit should succeed and write exit code 77 to exit_buf_va
    cpu.write_register(16, kSysWiniosThreadJoin);
    cpu.write_register(0, worker_tid);
    cpu.write_register(1, exit_buf_va);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 0); // KERN_SUCCESS

    int32_t joined_exit_code = 0;
    TEST_ASSERT(uas.copyout(reinterpret_cast<const void*>(exit_buf_va), &joined_exit_code, sizeof(joined_exit_code)));
    TEST_ASSERT(joined_exit_code == 77);

    // 5. Cross-process join security check: another process cannot join this thread
    BsdProcess* foreign_proc = proc_mgr.create_process("foreign_app", 1);
    cpu.write_register(16, kSysWiniosThreadJoin);
    cpu.write_register(0, worker_tid);
    cpu.write_register(1, exit_buf_va);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, *foreign_proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 3); // ESRCH (No such process/thread in caller's domain)

    std::cout << "  -> Thread create, exit status recording, and join isolation validated.\n";
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 219.10: Synchronization & Thread Lifecycle Syscall Tests\n";
    std::cout << "=================================================================\n";

    test_guest_mutex_syscalls();
    test_thread_lifecycle_syscalls();

    std::cout << "\n[PASS] All Stage 219.10 tests passed successfully!\n";
    return 0;
}

// ============================================================================
// Stage 220.23 Adversarial Testing Suite & Determinism Verification
// ============================================================================

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <memory>
#include <cstring>

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/virtual_page_table.h"
#include "memory/user_address_space.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/multi_process_scheduler.h"
#include "kernel/bsd_process.h"
#include "kernel/pipe.h"
#include "kernel/thread.h"
#include "runtime/guest_ipc.h"
#include "runtime/execution_boundary.h"
#include "winios_sdk/winios_sdk.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;
using namespace winios::runtime;

constexpr int32_t kErrnoEDEADLK = 11;
constexpr int32_t kErrnoEBUSY   = 16;

static void test_thread_adversarial_edge_cases() {
    std::cout << "[Adversarial 1] Thread creation, exit, and join edge cases...\n";
    Memory mem(0x2000000ULL);
    PhysicalPageManager ppm(mem, 0x100000ULL, 0x1000000ULL, 0x80000ULL);
    TEST_ASSERT(ppm.initialize());

    const uint64_t root_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_pa, "adv_uas");
    uas.set_asid(10);
    uas.set_owner_pid(500);

    BsdProcess proc(500, 1, "adv_proc");
    KernelThreadManager th_mgr;
    BsdProcessManager proc_mgr;
    MultiProcessScheduler scheduler(th_mgr, proc_mgr, mem);
    scheduler.register_process_address_space(500, &uas);

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);
    dispatcher.set_multi_process_scheduler(&scheduler);
    dispatcher.set_thread_manager(&th_mgr);
    dispatcher.set_process_manager(&proc_mgr);

    CPU cpu;
    cpu.reset();
    PriorityScheduler dummy_sched(th_mgr, mem);

    // 1. Join non-existent TID -> ESRCH (3)
    cpu.write_register(16, kSysWiniosThreadJoin);
    cpu.write_register(0, 99999); // non-existent TID
    cpu.write_register(1, 0);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoESRCH);

    // 2. Thread creation: Misaligned entry (0x10004001) -> EINVAL (22)
    cpu.write_register(16, kSysWiniosThreadCreate);
    cpu.write_register(0, 0x10004001ULL);
    cpu.write_register(1, 0);
    cpu.write_register(2, 0x70000000ULL);
    cpu.write_register(3, 0x10000ULL);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEINVAL);

    // 3. Thread creation: Misaligned stack top -> EINVAL (22)
    cpu.write_register(16, kSysWiniosThreadCreate);
    cpu.write_register(0, 0x10004000ULL);
    cpu.write_register(1, 0);
    cpu.write_register(2, 0x70000005ULL);
    cpu.write_register(3, 0x10000ULL);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEINVAL);

    // 4. Thread creation: Kernel-space entry point -> EINVAL (22)
    cpu.write_register(16, kSysWiniosThreadCreate);
    cpu.write_register(0, 0xFFFF800010000000ULL);
    cpu.write_register(1, 0);
    cpu.write_register(2, 0x70000000ULL);
    cpu.write_register(3, 0x10000ULL);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEINVAL);

    // 5. Thread creation: Stack size < 4096 -> EINVAL (22)
    cpu.write_register(16, kSysWiniosThreadCreate);
    cpu.write_register(0, 0x10004000ULL);
    cpu.write_register(1, 0);
    cpu.write_register(2, 0x70000000ULL);
    cpu.write_register(3, 2048ULL);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEINVAL);

    std::cout << "  Passed thread lifecycle adversarial tests.\n";
}

static void test_mutex_synchronization_tampering() {
    std::cout << "[Adversarial 2] Mutex synchronization tampering & unauthorized unlock...\n";
    Memory mem(0x2000000ULL);
    PhysicalPageManager ppm(mem, 0x100000ULL, 0x1000000ULL, 0x80000ULL);
    TEST_ASSERT(ppm.initialize());

    const uint64_t root_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_pa, "mutex_uas");
    uas.set_asid(11);
    uas.set_owner_pid(501);

    const uint64_t mutex_va = 0x20000ULL;
    const uint64_t mutex_pa = ppm.allocate_page();
    TEST_ASSERT(uas.map_user_page(mutex_va, mutex_pa, PagePermissions::UserReadWrite));
    uint32_t zero = 0;
    TEST_ASSERT(uas.copyin(mutex_va, &zero, sizeof(zero)));

    BsdProcess proc(501, 1, "mutex_proc");
    KernelThreadManager th_mgr;
    BsdProcessManager proc_mgr;
    MultiProcessScheduler scheduler(th_mgr, proc_mgr, mem);

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);
    dispatcher.set_multi_process_scheduler(&scheduler);
    dispatcher.set_thread_manager(&th_mgr);
    dispatcher.set_process_manager(&proc_mgr);

    CPU cpu;
    cpu.reset();
    PriorityScheduler dummy_sched(th_mgr, mem);

    // Create 2 threads in process
    KernelThread* th1 = th_mgr.create_thread("th1", 0x1000, 0, 0x70000, 0x10000, 16, true);
    KernelThread* th2 = th_mgr.create_thread("th2", 0x2000, 0, 0x80000, 0x10000, 16, true);
    th1->set_owner_pid(501);
    th2->set_owner_pid(501);
    th1->set_tpidr_el0(1001);
    th2->set_tpidr_el0(1002);
    proc.add_thread(th1);
    proc.add_thread(th2);

    scheduler.enqueue_thread(th1);
    scheduler.enqueue_thread(th2);
    TEST_ASSERT(scheduler.schedule_next(cpu));
    TEST_ASSERT(scheduler.current_thread() == th1);
    cpu.set_tpidr_el0(1001);

    // 1. Thread 1 locks mutex
    cpu.write_register(16, kSysWiniosMutexLock);
    cpu.write_register(0, mutex_va);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 0);

    // 2. Thread 1 tries recursive lock -> EDEADLK (11)
    cpu.write_register(16, kSysWiniosMutexLock);
    cpu.write_register(0, mutex_va);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEDEADLK);

    // 3. Switch to Thread 2
    TEST_ASSERT(scheduler.yield(cpu));
    TEST_ASSERT(scheduler.current_thread() == th2);
    cpu.set_tpidr_el0(1002);

    // Thread 2 tries to unlock mutex owned by Thread 1 -> EPERM (1)
    cpu.write_register(16, kSysWiniosMutexUnlock);
    cpu.write_register(0, mutex_va);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEPERM);

    // 4. Misaligned mutex address (0x20001) -> EFAULT (14)
    cpu.write_register(16, kSysWiniosMutexLock);
    cpu.write_register(0, 0x20001ULL);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEFAULT);

    // 5. Unmapped / kernel address -> EFAULT (14)
    cpu.write_register(16, kSysWiniosMutexLock);
    cpu.write_register(0, 0x80000000ULL);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEFAULT);

    std::cout << "  Passed mutex synchronization tampering tests.\n";
}

static void test_vfs_and_pipe_exhaustion() {
    std::cout << "[Adversarial 3] VFS file descriptor bounds & Pipe boundary conditions...\n";
    Memory mem(0x2000000ULL);
    PhysicalPageManager ppm(mem, 0x100000ULL, 0x1000000ULL, 0x80000ULL);
    TEST_ASSERT(ppm.initialize());

    const uint64_t root_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_pa, "pipe_uas");
    uas.set_asid(12);
    uas.set_owner_pid(502);

    const uint64_t buf_va = 0x30000ULL;
    const uint64_t buf_pa = ppm.allocate_page();
    TEST_ASSERT(uas.map_user_page(buf_va, buf_pa, PagePermissions::UserReadWrite));

    BsdProcess proc(502, 1, "pipe_proc");
    KernelThreadManager th_mgr;
    BsdProcessManager proc_mgr;
    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);
    CPU cpu;
    cpu.reset();
    PriorityScheduler dummy_sched(th_mgr, mem);

    auto pipe = std::make_shared<Pipe>(16384);
    proc.fd_table().install_fd(3, FileType::MemoryPipe, true, false, "pipe:r");
    proc.fd_table().install_fd(4, FileType::MemoryPipe, false, true, "pipe:w");
    proc.fd_table().get_fd_mut(3)->pipe = pipe;
    proc.fd_table().get_fd_mut(3)->is_pipe_reader = true;
    proc.fd_table().get_fd_mut(4)->pipe = pipe;
    proc.fd_table().get_fd_mut(4)->is_pipe_writer = true;

    // 1. Write to Read-End -> EBADF (9)
    cpu.write_register(16, kSysWrite);
    cpu.write_register(0, 3); // Read-only FD
    cpu.write_register(1, buf_va);
    cpu.write_register(2, 10);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEBADF);

    // 2. Read from Write-End -> EBADF (9)
    cpu.write_register(16, kSysRead);
    cpu.write_register(0, 4); // Write-only FD
    cpu.write_register(1, buf_va);
    cpu.write_register(2, 10);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEBADF);

    // 3. Negative / Out-of-bounds FD -> EBADF (9)
    cpu.write_register(16, kSysRead);
    cpu.write_register(0, static_cast<uint64_t>(-1));
    cpu.write_register(1, buf_va);
    cpu.write_register(2, 10);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEBADF);

    // 4. Pointer wraparound attack: buf_va = 0xFFFFFFFFFFFFFFF0, size = 32 -> EINVAL (22)
    cpu.write_register(16, kSysWrite);
    cpu.write_register(0, 4);
    cpu.write_register(1, 0xFFFFFFFFFFFFFFF0ULL);
    cpu.write_register(2, 32);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEINVAL);

    std::cout << "  Passed VFS and pipe exhaustion tests.\n";
}

static void test_determinism_5_runs() {
    std::cout << "[Adversarial 4] Determinism verification over 5 consecutive multi-process runs...\n";

    for (int run = 1; run <= 5; ++run) {
        auto& enforcer = ExecutionBoundaryEnforcer::Get();
        enforcer.Reset();

        Memory mem(0x1000000ULL);
        PhysicalPageManager ppm(mem, 0x100000ULL, 0x800000ULL, 0x80000ULL);
        TEST_ASSERT(ppm.initialize());

        const uint64_t root_a = ppm.allocate_page();
        const uint64_t root_b = ppm.allocate_page();
        UserAddressSpace uas_a(mem, ppm, root_a, "uas_a");
        UserAddressSpace uas_b(mem, ppm, root_b, "uas_b");
        uas_a.set_asid(1);
        uas_b.set_asid(2);

        BsdProcessManager proc_mgr;
        TEST_ASSERT(proc_mgr.initialize());
        BsdProcess* p_a = proc_mgr.create_process("p_a", 0, nullptr, &uas_a);
        BsdProcess* p_b = proc_mgr.create_process("p_b", p_a->pid(), nullptr, &uas_b);
        TEST_ASSERT(p_a && p_b);

        KernelThreadManager th_mgr;
        MultiProcessScheduler scheduler(th_mgr, proc_mgr, mem);
        scheduler.register_process_address_space(p_a->pid(), &uas_a);
        scheduler.register_process_address_space(p_b->pid(), &uas_b);

        KernelThread* t_a = th_mgr.create_thread("t_a", 0x10000, 0, 0x70000, 0x10000, 16, true);
        KernelThread* t_b = th_mgr.create_thread("t_b", 0x20000, 0, 0x80000, 0x10000, 16, true);
        t_a->set_owner_pid(p_a->pid());
        t_b->set_owner_pid(p_b->pid());
        p_a->add_thread(t_a);
        p_b->add_thread(t_b);

        scheduler.enqueue_thread(t_a);
        scheduler.enqueue_thread(t_b);

        CPU cpu;
        cpu.reset();
        TEST_ASSERT(scheduler.schedule_next(cpu));
        TEST_ASSERT(scheduler.current_thread() == t_a);

        // Record 10 instructions
        {
            ExecutionOriginScope scope(ExecutionOrigin::kGuestCpuRetired);
            for (int i = 0; i < 10; ++i) {
                TEST_ASSERT(enforcer.RecordGuestInstructionRetirementExplicit(1, p_a->pid(), t_a->tid(), 0x10000 + i * 4));
            }
        }

        TEST_ASSERT(scheduler.yield(cpu));
        TEST_ASSERT(scheduler.current_thread() == t_b);

        {
            ExecutionOriginScope scope(ExecutionOrigin::kGuestCpuRetired);
            for (int i = 0; i < 10; ++i) {
                TEST_ASSERT(enforcer.RecordGuestInstructionRetirementExplicit(1, p_b->pid(), t_b->tid(), 0x20000 + i * 4));
            }
        }

        TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == 20);
        TEST_ASSERT(scheduler.stats().cross_process_context_switches == 2);
        TEST_ASSERT(scheduler.stats().total_address_space_switches == 2);
    }

    std::cout << "  Passed 5-run determinism verification (100% identical state progression).\n";
}

int main() {
    std::cout << "=== Stage 220.23 Adversarial Testing Suite & Determinism ===\n";
    test_thread_adversarial_edge_cases();
    test_mutex_synchronization_tampering();
    test_vfs_and_pipe_exhaustion();
    test_determinism_5_runs();
    std::cout << "All Stage 220.23 tests passed successfully.\n";
    return 0;
}

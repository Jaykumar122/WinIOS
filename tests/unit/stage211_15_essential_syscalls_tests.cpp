#include <iostream>
#include <vector>
#include <cstdint>

#include "cpu/cpu.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"
#include "kernel/thread.h"
#include "kernel/user_syscall_dispatcher.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_essential_syscall_dispatch() {
    std::cout << "[Test 1] Stage 211.15: Syscall Dispatch (getpid, getppid, write, yield, exit)...\n";

    CPU cpu;
    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* proc = proc_mgr.create_process("test_app", 5);

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "test_uas");

    // 1. SYS_getpid (X16 = 20)
    cpu.write_register(16, kSysGetPid);
    TEST_CHECK(dispatcher.handle_syscall(cpu, *proc, uas, sched), "SYS_getpid handled");
    TEST_CHECK(cpu.read_register(0) == static_cast<std::uint64_t>(proc->pid()), "X0 holds PID");

    // 2. SYS_getppid (X16 = 39)
    cpu.write_register(16, kSysGetPpid);
    TEST_CHECK(dispatcher.handle_syscall(cpu, *proc, uas, sched), "SYS_getppid handled");
    TEST_CHECK(cpu.read_register(0) == 5, "X0 holds PPID (5)");

    // 3. SYS_write (X16 = 4) to stdout (fd = 1)
    const std::uint64_t page_pa = ppm.allocate_page();
    uas.map_user_page(0x20000ULL, page_pa, PagePermissions::UserReadWrite);
    const std::string msg = "Testing SYS_write!\n";
    uas.copyin(0x20000ULL, msg.c_str(), msg.size());

    cpu.write_register(16, kSysWrite);
    cpu.write_register(0, 1); // stdout
    cpu.write_register(1, 0x20000ULL);
    cpu.write_register(2, msg.size());

    TEST_CHECK(dispatcher.handle_syscall(cpu, *proc, uas, sched), "SYS_write handled");
    TEST_CHECK(cpu.read_register(0) == msg.size(), "X0 returns bytes written");
    TEST_CHECK(console.get_output() == msg, "Virtual console holds written string");

    // 4. SYS_sched_yield (X16 = 329)
    cpu.write_register(16, kSysSchedYield);
    TEST_CHECK(dispatcher.handle_syscall(cpu, *proc, uas, sched), "SYS_sched_yield handled");
    TEST_CHECK(dispatcher.stats().total_yields == 1, "Yield counter incremented");

    // 5. SYS_exit (X16 = 1)
    cpu.write_register(16, kSysExit);
    cpu.write_register(0, 99); // exit code
    TEST_CHECK(dispatcher.handle_syscall(cpu, *proc, uas, sched), "SYS_exit handled");
    TEST_CHECK(proc->is_terminated(), "Process terminated");
    TEST_CHECK(proc->exit_code() == 99, "Process exit code is 99");
}

void test_syscall_defensive_validations() {
    std::cout << "[Test 2] Stage 211.15: Defensive Validation (EFAULT, EBADF, ENOSYS)...\n";

    CPU cpu;
    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* proc = proc_mgr.create_process("bad_app", 0);

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "bad_uas");

    // Invalid syscall number -> ENOSYS (-78)
    cpu.write_register(16, 99999);
    TEST_CHECK(!dispatcher.handle_syscall(cpu, *proc, uas, sched), "Invalid syscall rejected");
    TEST_CHECK(static_cast<std::int64_t>(cpu.read_register(0)) == -78, "Returns -78 (ENOSYS)");

    // Invalid pointer to write -> EFAULT (-14)
    cpu.write_register(16, kSysWrite);
    cpu.write_register(0, 1);
    cpu.write_register(1, 0xDEAD0000ULL); // Unmapped address
    cpu.write_register(2, 64);
    TEST_CHECK(!dispatcher.handle_syscall(cpu, *proc, uas, sched), "Unmapped pointer rejected");
    TEST_CHECK(static_cast<std::int64_t>(cpu.read_register(0)) == -14, "Returns -14 (EFAULT)");

    // Invalid FD to write -> EBADF (-9)
    cpu.write_register(16, kSysWrite);
    cpu.write_register(0, 100); // Invalid FD
    cpu.write_register(1, 0x20000ULL);
    cpu.write_register(2, 10);
    TEST_CHECK(!dispatcher.handle_syscall(cpu, *proc, uas, sched), "Invalid FD rejected");
    TEST_CHECK(static_cast<std::int64_t>(cpu.read_register(0)) == -9, "Returns -9 (EBADF)");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 211.15 Essential Syscalls Tests ===\n";
    test_essential_syscall_dispatch();
    test_syscall_defensive_validations();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 211.15: All essential syscalls tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 211.15: " << g_failures << " tests failed.\n";
        return 1;
    }
}

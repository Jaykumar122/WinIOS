#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"
#include "kernel/user_syscall_dispatcher.h"
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
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_wait_specific_child() {
    std::cout << "[Test 1] Wait on Specific Child and Status Propagation...\n";

    BsdProcessManager mgr;
    mgr.initialize();

    auto* init_proc = mgr.create_process("init", 0); // PID 1
    auto* child = mgr.create_process("worker", 1);    // PID 2

    // Child is still running; WNOHANG should return 0
    int status = 0;
    int wait_res = mgr.wait_process(1, 2, &status, 1); // WNOHANG = 1
    TEST_CHECK(wait_res == 0, "WNOHANG returns 0 while child is running");

    // Terminate child with exit code 42
    mgr.terminate_process(2, 42);
    TEST_CHECK(child->is_terminated(), "Child is Zombie");

    // Parent waits on child
    wait_res = mgr.wait_process(1, 2, &status, 0);
    TEST_CHECK(wait_res == 2, "wait_process returns child PID 2");
    TEST_CHECK((status >> 8) == 42, "Status holds shifted exit code 42");

    // Process is reaped
    TEST_CHECK(mgr.get_process(2) == nullptr, "Child reaped and removed from table");
    TEST_CHECK(init_proc->children().empty(), "Parent child list empty");

    // Repeated wait returns -ECHILD (-10)
    wait_res = mgr.wait_process(1, 2, &status, 0);
    TEST_CHECK(wait_res == -10, "Repeated wait returns -ECHILD (-10)");
}

void test_wait_any_child() {
    std::cout << "[Test 2] Wait on Any Child (wpid = -1) with Multiple Children...\n";

    BsdProcessManager mgr;
    mgr.initialize();

    mgr.create_process("init", 0); // PID 1
    mgr.create_process("task_a", 1); // PID 2
    mgr.create_process("task_b", 1); // PID 3
    mgr.create_process("task_c", 1); // PID 4

    // Terminate task_b (PID 3) first with code 15
    mgr.terminate_process(3, 15);

    int status = 0;
    int wait_res = mgr.wait_process(1, -1, &status, 0);
    TEST_CHECK(wait_res == 3, "wait(-1) reaped first terminated child (PID 3)");
    TEST_CHECK((status >> 8) == 15, "Status holds exit code 15");

    // Terminate task_c (PID 4) with code 99
    mgr.terminate_process(4, 99);
    wait_res = mgr.wait_process(1, -1, &status, 0);
    TEST_CHECK(wait_res == 4, "wait(-1) reaped second terminated child (PID 4)");
    TEST_CHECK((status >> 8) == 99, "Status holds exit code 99");

    // Non-child wait rejection
    wait_res = mgr.wait_process(1, 999, &status, 0);
    TEST_CHECK(wait_res == -10, "Waiting on non-child PID 999 returns -ECHILD (-10)");
}

void test_sys_wait4_syscall() {
    std::cout << "[Test 3] Syscall SYS_wait4 (X16 = 7) Integration...\n";

    CPU cpu;
    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    dispatcher.set_process_manager(&proc_mgr);

    auto* parent = proc_mgr.create_process("parent_proc", 0); // PID 1
    proc_mgr.create_process("child_proc", 1);                 // PID 2

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "wait_uas");

    // Map a page for status buffer at VA 0x30000
    const std::uint64_t status_pa = ppm.allocate_page();
    uas.map_user_page(0x30000ULL, status_pa, PagePermissions::UserReadWrite);

    // Terminate child PID 2 with code 123
    proc_mgr.terminate_process(2, 123);

    // Call SYS_wait4: X16 = 7, X0 = 2 (wpid), X1 = 0x30000 (status ptr), X2 = 0 (options)
    cpu.write_register(16, kSysWait4);
    cpu.write_register(0, 2);
    cpu.write_register(1, 0x30000ULL);
    cpu.write_register(2, 0);

    TEST_CHECK(dispatcher.handle_syscall(cpu, *parent, uas, sched), "SYS_wait4 dispatched successfully");
    TEST_CHECK(cpu.read_register(0) == 2, "X0 returns reaped child PID 2");
    TEST_CHECK((cpu.get_spsr_el1() & (1u << 29)) == 0, "Carry bit clear on success");

    // Read back status written into user memory
    int read_status = 0;
    TEST_CHECK(uas.copyout(reinterpret_cast<void*>(0x30000ULL), &read_status, sizeof(read_status)),
               "Read status from user buffer");
    TEST_CHECK((read_status >> 8) == 123, "Status in user memory reflects child exit code 123");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 212.5 Wait, Exit and Process Reaping Tests ===\n";
    test_wait_specific_child();
    test_wait_any_child();
    test_sys_wait4_syscall();

    if (g_failures == 0) {
        std::cout << "[PASS] Stage 212.5: All wait and process reaping tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 212.5: " << g_failures << " tests failed.\n";
        return 1;
    }
}

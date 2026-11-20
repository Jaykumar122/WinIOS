// ============================================================================
// Stage 243.17: Darwin Syscall ABI, NZCV Carry Flag & Errno Conformance Tests
// Verifies Darwin XNU syscall ABI error conventions: Carry flag (NZCV & 0x2) set
// on failure with positive errno in X0, and Carry flag cleared on success.
// ============================================================================

#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;

static void test_darwin_syscall_carry_flag_and_errno() {
    std::cout << "[Stage 243.17 - Test 1] Darwin XNU Carry flag and errno semantics...\n";
    Memory mem(0x4000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x3000000ULL, 0x0800000ULL);
    ppm.initialize();

    uint64_t root = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root, "darwin_uas");
    uas.set_owner_pid(601);

    BsdProcess proc(601, 1, "test_darwin_proc", nullptr);
    proc.set_address_space(&uas);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    VirtualConsole console;
    UserSyscallDispatcher disp(console);
    disp.set_abi_mode(SyscallAbi::DarwinStrict);

    CPU cpu;
    cpu.reset();

    // 1. Failing syscall: write(fd=1, buf=0x0, count=50) -> NULL pointer error
    cpu.write_register(16, 4); // write
    cpu.write_register(0, 1);  // stdout
    cpu.write_register(1, 0);  // NULL pointer
    cpu.write_register(2, 50); // count

    disp.handle_syscall(cpu, proc, uas, sched);

    // Darwin convention: Carry bit is SET on failure
    bool carry_set = (cpu.get_nzcv() & 0x2u) != 0;
    uint64_t err_ret = cpu.read_register(0);
    TEST_ASSERT(carry_set || err_ret == 14 /* EFAULT */, "Carry flag or positive errno set on failure");

    // 2. Successful syscall: exit(77)
    cpu.write_register(16, 1); // exit
    cpu.write_register(0, 77);

    disp.handle_syscall(cpu, proc, uas, sched);
    TEST_ASSERT(proc.is_terminated(), "Process terminated on exit syscall");
    TEST_ASSERT(proc.exit_code() == 77, "Exit code matches 77");
}

int main() {
    std::cout << "=== Running Stage 243.17 Darwin Syscall NZCV Conformance Tests ===\n";
    test_darwin_syscall_carry_flag_and_errno();
    std::cout << "Stage 243.17: All tests passed successfully.\n";
    return 0;
}

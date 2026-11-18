// ============================================================================
// Stage 220.13 Unit Tests: Syscall Dispatcher Defensive Fuzzing & Hardening
// ============================================================================

#include <iostream>
#include <vector>
#include <cstdint>
#include <cassert>
#include <memory>

#include "cpu/cpu.h"
#include "kernel/thread.h"
#include "kernel/bsd_process.h"
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

static void test_integer_overflow_and_giant_buffer_defenses() {
    std::cout << "[Test 1] Integer wrap-around and oversized buffer defenses (SYS_read/SYS_write)...\n";
    Memory mem(0x1000000ULL);
    CPU cpu;
    cpu.reset();

    PhysicalPageManager ppm(mem, 0x100000ULL, 0x800000ULL, 0x80000ULL);
    TEST_ASSERT(ppm.initialize());

    const uint64_t root_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_pa, "test_proc");
    uas.set_asid(1);
    uas.set_owner_pid(101);

    BsdProcess proc(101, 1, "test_proc");
    KernelThreadManager th_mgr;
    PriorityScheduler dummy_sched(th_mgr, mem);

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);

    // 1. SYS_write with integer wrap-around (buf_va = 0xFFFFFFFFFFFFFF00, nbyte = 0x200) -> EINVAL (22)
    cpu.write_register(16, kSysWrite);
    cpu.write_register(0, 1); // stdout
    cpu.write_register(1, 0xFFFFFFFFFFFFFF00ULL);
    cpu.write_register(2, 0x200ULL);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEINVAL);

    // 2. SYS_write with gigantic allocation request (> 256MB) -> EINVAL (22)
    cpu.write_register(16, kSysWrite);
    cpu.write_register(0, 1);
    cpu.write_register(1, 0x10000ULL);
    cpu.write_register(2, 0xFFFFFFFFFFFFFFFFULL);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEINVAL);

    // 3. SYS_read on valid pipe with wrap-around buffer -> EINVAL (22)
    int pipe_fd = proc.fd_table().allocate_fd(FileType::Regular, true, false, "in.txt");
    cpu.write_register(16, kSysRead);
    cpu.write_register(0, pipe_fd);
    cpu.write_register(1, 0xFFFFFFFFFFFFF000ULL);
    cpu.write_register(2, 0x2000ULL);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEINVAL);

    std::cout << "  Passed integer overflow and buffer bound defenses.\n";
}

static void test_invalid_pointer_and_fd_fuzzing() {
    std::cout << "[Test 2] Invalid pointer, unmapped memory, and out-of-bounds descriptor fuzzing...\n";
    Memory mem(0x1000000ULL);
    CPU cpu;
    cpu.reset();

    PhysicalPageManager ppm(mem, 0x100000ULL, 0x800000ULL, 0x80000ULL);
    TEST_ASSERT(ppm.initialize());

    const uint64_t root_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_pa, "test_proc");
    uas.set_asid(1);
    uas.set_owner_pid(101);

    BsdProcess proc(101, 1, "test_proc");
    KernelThreadManager th_mgr;
    PriorityScheduler dummy_sched(th_mgr, mem);

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);

    // 1. SYS_close with negative FD (-1) -> EBADF (9)
    cpu.write_register(16, kSysClose);
    cpu.write_register(0, static_cast<uint64_t>(-1));
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEBADF);

    // 2. SYS_close with out-of-bounds FD (64) -> EBADF (9)
    cpu.write_register(16, kSysClose);
    cpu.write_register(0, 64);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEBADF);

    // 3. SYS_write to read-only descriptor (stdin = 0) -> EBADF (9)
    cpu.write_register(16, kSysWrite);
    cpu.write_register(0, 0); // stdin is readable only
    cpu.write_register(1, 0x10000ULL);
    cpu.write_register(2, 10);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEBADF);

    // 4. SYS_pipe with misaligned pointer (0x20001 not 4-byte aligned) -> EFAULT (14)
    cpu.write_register(16, kSysPipe);
    cpu.write_register(0, 0x20001ULL);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEFAULT);

    // 5. Undefined syscall number 9999 -> ENOSYS (78)
    cpu.write_register(16, 9999);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoENOSYS);
    TEST_ASSERT(dispatcher.stats().total_invalid_syscalls >= 1);

    std::cout << "  Passed pointer fuzzing and descriptor defense tests.\n";
}

int main() {
    std::cout << "=== Stage 220.13 Syscall Dispatcher Hardening Tests ===\n";
    test_integer_overflow_and_giant_buffer_defenses();
    test_invalid_pointer_and_fd_fuzzing();
    std::cout << "All Stage 220.13 tests passed successfully.\n";
    return 0;
}

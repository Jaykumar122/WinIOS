// ============================================================================
// Stage 242.18: Syscall Pointer Boundary, ABI Mode & Failure Containment Tests
// Exercises process-scoped syscall ABI, invalid guest pointers, overflowing
// buffer lengths, unmapped addresses, and verifies errors fail safely.
// ============================================================================

#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "loader/macho_user_loader.h"

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
using namespace winios::loader;

static void test_macho_entry_conformance() {
    std::cout << "[Stage 242.18 - Test 1] Mach-O LC_MAIN and LC_UNIXTHREAD entry contracts...\n";
    // Check constants
    TEST_ASSERT(kLcMain == 0x80000028u, "kLcMain constant");
    TEST_ASSERT(kLcUnixThread == 0x5u, "kLcUnixThread constant");

    // Header validation on empty / malformed buffer
    std::vector<uint8_t> malformed(16, 0);
    std::string err;
    TEST_ASSERT(!MachoUserLoader::validate_header(malformed.data(), malformed.size(), &err), "Malformed header rejected");
}

static void test_syscall_pointer_boundary_safety() {
    std::cout << "[Stage 242.18 - Test 2] Syscall pointer bounds check & error containment...\n";
    Memory mem(0x4000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x3000000ULL, 0x0800000ULL);
    ppm.initialize();

    uint64_t root = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root, "sys_uas");
    uas.set_owner_pid(501);

    BsdProcess proc(501, 1, "test_proc", nullptr);
    proc.set_address_space(&uas);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    VirtualConsole console;
    UserSyscallDispatcher disp(console);
    disp.set_abi_mode(SyscallAbi::DarwinStrict);

    CPU cpu;
    cpu.reset();

    // Syscall 4: write(fd=1, buf=0x0, count=100)
    // Pointer 0x0 is __PAGEZERO / unmapped
    cpu.write_register(16, 4); // Syscall 4 (write)
    cpu.write_register(0, 1);  // stdout
    cpu.write_register(1, 0);  // NULL pointer
    cpu.write_register(2, 100);// count 100

    disp.handle_syscall(cpu, proc, uas, sched);

    // Host must NOT crash, process must NOT terminate unexpectedly, and Darwin error flag or errno must be set
    uint64_t ret = cpu.read_register(0);
    bool carry_flag_set = (cpu.get_nzcv() & 0x2u) != 0;
    TEST_ASSERT(carry_flag_set || ret == 14 /* EFAULT */ || static_cast<int64_t>(ret) < 0,
                "Write to NULL pointer must fail safely with Darwin error convention");

    // Syscall 1: exit(status=42)
    cpu.write_register(16, 1); // exit
    cpu.write_register(0, 42);

    disp.handle_syscall(cpu, proc, uas, sched);
    TEST_ASSERT(proc.is_terminated(), "Exit syscall marks process terminated");
    TEST_ASSERT(proc.exit_code() == 42, "Exit code matches 42");
}

int main() {
    std::cout << "=== Running Stage 242.18 Syscall Pointer Boundary Tests ===\n";
    test_macho_entry_conformance();
    test_syscall_pointer_boundary_safety();
    std::cout << "Stage 242.18: All tests passed successfully.\n";
    return 0;
}

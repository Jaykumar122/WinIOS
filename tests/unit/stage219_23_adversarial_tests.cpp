#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>

#include "winios_sdk/winios_sdk.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/pipe.h"
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

void test_cross_process_memory_isolation_adversarial() {
    std::cout << "[Adversarial 1] Cross-Process Address Snooping & Boundary Violation...\n";

    Memory mem(0x800000); // 8 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    TEST_ASSERT(ppm.initialize(false));

    uint64_t root_a = ppm.allocate_page();
    uint64_t root_b = ppm.allocate_page();

    UserAddressSpace uas_a(mem, ppm, root_a, "victim_uas");
    UserAddressSpace uas_b(mem, ppm, root_b, "attacker_uas");

    // Victim maps a private secret at 0x10000000
    uint64_t victim_pa = ppm.allocate_page();
    const uint64_t secret_va = 0x10000000ULL;
    TEST_ASSERT(uas_a.map_user_page(secret_va, victim_pa, PagePermissions::UserReadWrite));
    const char secret[] = "SECRET_KEY_PROT";
    TEST_ASSERT(uas_a.copyin(secret_va, secret, sizeof(secret)));

    // Attacker has NO mapping at 0x10000000
    TEST_ASSERT(uas_b.translate(secret_va) == 0);
    TEST_ASSERT(!uas_b.validate_user_buffer(secret_va, sizeof(secret), false));

    // Attacker's attempt to copyout from secret_va must fail
    char stolen[32]{0};
    TEST_ASSERT(!uas_b.copyout(reinterpret_cast<const void*>(secret_va), stolen, sizeof(secret)));

    std::cout << "  -> Cross-process memory snooping prevented by independent page tables.\n";
}

void test_invalid_file_descriptor_tampering() {
    std::cout << "[Adversarial 2] Invalid File Descriptor Tampering & Out-of-Range Access...\n";

    Memory mem(0x400000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x3C0000ULL, 0x28020ULL);
    TEST_ASSERT(ppm.initialize(false));

    uint64_t root = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root, "test_uas");

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

    BsdProcess proc(42, 1, "test_proc");
    KernelThreadManager th_mgr;
    PriorityScheduler dummy_sched(th_mgr, mem);
    CPU cpu;
    cpu.reset();

    // 1. SYS_read on negative fd (-1) -> must fail with EBADF (9)
    cpu.write_register(16, kSysRead);
    cpu.write_register(0, static_cast<uint64_t>(-1));
    cpu.write_register(1, 0x10000);
    cpu.write_register(2, 32);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 9); // EBADF

    // 2. SYS_write on unallocated fd (50) -> must fail with EBADF (9)
    cpu.write_register(16, kSysWrite);
    cpu.write_register(0, 50);
    cpu.write_register(1, 0x10000);
    cpu.write_register(2, 32);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 9); // EBADF

    // 3. SYS_close on already-closed fd -> must fail with EBADF (9)
    cpu.write_register(16, kSysClose);
    cpu.write_register(0, 50);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 9); // EBADF

    // 4. SYS_dup on invalid fd -> must fail with EBADF (9)
    cpu.write_register(16, kSysDup);
    cpu.write_register(0, 50);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 9); // EBADF

    std::cout << "  -> File descriptor bounds and state checks verified.\n";
}

void test_buffer_overflow_and_faults() {
    std::cout << "[Adversarial 3] Message Trap Buffer Overflow & Unmapped Pointers...\n";

    Memory mem(0x400000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x3C0000ULL, 0x28020ULL);
    TEST_ASSERT(ppm.initialize(false));

    uint64_t root = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root, "test_uas");

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

    BsdProcess proc(42, 1, "test_proc");
    KernelThreadManager th_mgr;
    PriorityScheduler dummy_sched(th_mgr, mem);
    CPU cpu;
    cpu.reset();

    // 1. Unmapped user pointer -> EFAULT (14)
    cpu.write_register(16, kSysWiniosMsgTrap);
    cpu.write_register(0, 0x00000007DEAD0000ULL);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 14); // EFAULT

    // 2. Unknown system call number -> ENOSYS (78)
    cpu.write_register(16, 99999);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 78); // ENOSYS

    std::cout << "  -> Memory faults and unknown syscalls rejected cleanly.\n";
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 219.23: Adversarial Validation & Security Suite\n";
    std::cout << "=================================================================\n";

    test_cross_process_memory_isolation_adversarial();
    test_invalid_file_descriptor_tampering();
    test_buffer_overflow_and_faults();

    std::cout << "\n[PASS] All Stage 219.23 Adversarial Tests Passed Successfully!\n";
    return 0;
}

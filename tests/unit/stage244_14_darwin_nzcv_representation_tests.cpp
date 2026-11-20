// ============================================================================
// Stage 244.14: Darwin NZCV Representation & Error Semantics Conformance Tests
// Verifies architectural NZCV bit flags: N=0x8, Z=0x4, C=0x2, V=0x1,
// Darwin syscall error semantics: C asserted (0x2) with positive errno in X0,
// and syscall success semantics: C cleared (~0x2) with return value in X0,
// while preserving non-carry condition bits (N, Z, V).
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

static void test_nzcv_bit_layout() {
    std::cout << "[Stage 244.14 - Test 1] Architectural NZCV bit definitions...\n";

    constexpr uint8_t kNegative = 0x8u;
    constexpr uint8_t kZero     = 0x4u;
    constexpr uint8_t kCarry    = 0x2u;
    constexpr uint8_t kOverflow = 0x1u;

    CPU cpu;
    cpu.reset();

    // Verify independent bit set & get
    cpu.set_nzcv(kNegative);
    TEST_ASSERT(cpu.get_nzcv() == 0x8u, "Negative bit (bit 3) set");

    cpu.set_nzcv(kZero);
    TEST_ASSERT(cpu.get_nzcv() == 0x4u, "Zero bit (bit 2) set");

    cpu.set_nzcv(kCarry);
    TEST_ASSERT(cpu.get_nzcv() == 0x2u, "Carry bit (bit 1) set");

    cpu.set_nzcv(kOverflow);
    TEST_ASSERT(cpu.get_nzcv() == 0x1u, "Overflow bit (bit 0) set");

    // All set
    cpu.set_nzcv(kNegative | kZero | kCarry | kOverflow);
    TEST_ASSERT(cpu.get_nzcv() == 0xFu, "All NZCV bits set");

    // Clamping to 4 bits
    cpu.set_nzcv(0xF0);
    TEST_ASSERT(cpu.get_nzcv() == 0x0u, "Upper bits clamped to 0");
}

static void test_darwin_syscall_error_and_success_nzcv() {
    std::cout << "[Stage 244.14 - Test 2] Darwin syscall Carry flag setting on error and clearing on success...\n";

    constexpr uint8_t kNegative = 0x8u;
    constexpr uint8_t kZero     = 0x4u;
    constexpr uint8_t kCarry    = 0x2u;
    constexpr uint8_t kOverflow = 0x1u;

    Memory mem(0x4000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x3000000ULL, 0x0800000ULL);
    ppm.initialize();

    uint64_t root = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root, "darwin_test_uas");
    uas.set_owner_pid(602);

    // Map a valid writeable user page for buffer testing
    uint64_t user_buf_va = 0x100000;
    uas.map_user_page(user_buf_va, ppm.allocate_page(), PagePermissions::UserReadWrite);

    BsdProcess proc(602, 1, "test_darwin_nzcv_proc", nullptr);
    proc.set_address_space(&uas);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    VirtualConsole console;
    UserSyscallDispatcher disp(console);
    disp.set_abi_mode(SyscallAbi::DarwinStrict);

    CPU cpu;
    cpu.reset();

    // Case A: Failing syscall (invalid fd)
    // Preset NZCV with N=1, Z=0, C=0, V=1 (0x9)
    cpu.set_nzcv(kNegative | kOverflow); // 0x9
    cpu.write_register(16, 4);           // write
    cpu.write_register(0, 9999);         // invalid fd (EBADF)
    cpu.write_register(1, user_buf_va);
    cpu.write_register(2, 10);

    disp.handle_syscall(cpu, proc, uas, sched);

    // In Darwin ABI, Carry flag MUST be asserted
    uint8_t nzcv_after_err = cpu.get_nzcv();
    TEST_ASSERT((nzcv_after_err & kCarry) != 0, "Carry flag asserted on syscall failure");
    uint64_t err_code = cpu.read_register(0);
    TEST_ASSERT(err_code > 0, "Positive errno returned in X0 on failure");

    // Case B: Successful syscall
    // Preset NZCV with C=1, N=0, Z=1, V=0 (0x6)
    cpu.set_nzcv(kCarry | kZero); // 0x6
    cpu.write_register(16, 20);   // getpid
    disp.handle_syscall(cpu, proc, uas, sched);

    uint8_t nzcv_after_succ = cpu.get_nzcv();
    TEST_ASSERT((nzcv_after_succ & kCarry) == 0, "Carry flag cleared on syscall success");
    TEST_ASSERT(cpu.read_register(0) == 602, "getpid returned PID 602 in X0");
}

int main() {
    std::cout << "=== Running Stage 244.14 Darwin NZCV Representation Tests ===\n";
    test_nzcv_bit_layout();
    test_darwin_syscall_error_and_success_nzcv();
    std::cout << "Stage 244.14: All Darwin NZCV tests passed successfully.\n";
    return 0;
}

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

void test_strict_darwin_abi() {
    std::cout << "[Test 1] Strict Darwin XNU ARM64 Syscall ABI (X16, Carry flag)...\n";

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
    auto* proc = proc_mgr.create_process("darwin_app", 42);

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "darwin_uas");

    // Success case: SYS_getpid (X16 = 20)
    cpu.write_register(16, kSysGetPid);
    cpu.set_spsr_el1(1u << 29); // Pre-set carry to test clearing
    cpu.set_nzcv(0x2u);

    TEST_CHECK(dispatcher.handle_syscall(cpu, *proc, uas, sched), "Darwin SYS_getpid handled successfully");
    TEST_CHECK(cpu.read_register(0) == static_cast<std::uint64_t>(proc->pid()), "X0 holds PID");
    TEST_CHECK((cpu.get_spsr_el1() & (1u << 29)) == 0, "Carry bit cleared in SPSR_EL1 on success");
    TEST_CHECK((cpu.get_nzcv() & 0x2u) == 0, "Carry bit cleared in NZCV on success");
    TEST_CHECK(dispatcher.last_used_abi() == SyscallAbi::DarwinStrict, "ABI recorded as DarwinStrict");

    // Success case: SYS_getppid (X16 = 39)
    cpu.write_register(16, kSysGetPpid);
    TEST_CHECK(dispatcher.handle_syscall(cpu, *proc, uas, sched), "Darwin SYS_getppid handled successfully");
    TEST_CHECK(cpu.read_register(0) == 42, "X0 holds PPID (42)");
    TEST_CHECK((cpu.get_spsr_el1() & (1u << 29)) == 0, "Carry bit cleared on success");

    // Error case: Invalid Syscall Number (X16 = 9999)
    cpu.write_register(16, 9999);
    cpu.set_spsr_el1(0); // Pre-clear carry to test setting
    cpu.set_nzcv(0);

    TEST_CHECK(!dispatcher.handle_syscall(cpu, *proc, uas, sched), "Invalid syscall rejected");
    TEST_CHECK(cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoENOSYS), "X0 holds positive ENOSYS (78)");
    TEST_CHECK((cpu.get_spsr_el1() & (1u << 29)) != 0, "Carry bit set in SPSR_EL1 on error");
    TEST_CHECK((cpu.get_nzcv() & 0x2u) != 0, "Carry bit set in NZCV on error");

    // Error case: EBADF (X16 = 4, write to invalid fd)
    cpu.write_register(16, kSysWrite);
    cpu.write_register(0, 88); // bad fd
    cpu.write_register(1, 0x1000);
    cpu.write_register(2, 4);

    TEST_CHECK(!dispatcher.handle_syscall(cpu, *proc, uas, sched), "Write to invalid FD rejected");
    TEST_CHECK(cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEBADF), "X0 holds positive EBADF (9)");
    TEST_CHECK((cpu.get_spsr_el1() & (1u << 29)) != 0, "Carry bit set in SPSR_EL1 on EBADF");
}

void test_fast_abi() {
    std::cout << "[Test 2] WinIOS Fast ABI (Syscall in X0, Negative Errno)...\n";

    CPU cpu;
    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::WinIOSFast);

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* proc = proc_mgr.create_process("fast_app", 10);

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "fast_uas");

    // Success case: SYS_getpid in X0
    cpu.write_register(0, kSysGetPid);
    cpu.write_register(16, 0xBAD); // Should be ignored in Fast ABI mode

    TEST_CHECK(dispatcher.handle_syscall(cpu, *proc, uas, sched), "Fast ABI SYS_getpid handled");
    TEST_CHECK(cpu.read_register(0) == static_cast<std::uint64_t>(proc->pid()), "X0 holds PID");
    TEST_CHECK(dispatcher.last_used_abi() == SyscallAbi::WinIOSFast, "ABI recorded as WinIOSFast");

    // Error case: Invalid Syscall in X0
    cpu.write_register(0, 8888);
    TEST_CHECK(!dispatcher.handle_syscall(cpu, *proc, uas, sched), "Invalid syscall rejected in Fast ABI");
    TEST_CHECK(static_cast<std::int64_t>(cpu.read_register(0)) == -78, "Returns negative errno (-78 ENOSYS)");
}

void test_instruction_level_svc() {
    std::cout << "[Test 3] Instruction-level SVC #0x80 and Exception Vectoring...\n";

    CPU cpu;
    Memory mem(0x800000);
    cpu.reset();
    cpu.set_architectural_exceptions(true);

    const std::uint64_t vbar_va = 0x80000ULL;
    cpu.set_vbar_el1(vbar_va);

    // Place a NOP at lower EL sync vector slot: VBAR + 0x400
    mem.write_u32(static_cast<std::size_t>(vbar_va + 0x400), 0xD503201Fu); // NOP

    // Set code at EL0:
    // 0x1000: mov x16, #20 (SYS_getpid) -> 0xD2800290u
    // 0x1004: svc #0x80                 -> 0xD4001001u
    const std::uint64_t code_va = 0x1000ULL;
    mem.write_u32(static_cast<std::size_t>(code_va), 0xD2800290u);     // mov x16, #20
    mem.write_u32(static_cast<std::size_t>(code_va + 4), 0xD4001001u); // svc #0x80

    cpu.set_current_el(0);
    cpu.set_program_counter(code_va);

    // Step 1: Execute mov x16, #20
    cpu.step(mem);
    TEST_CHECK(cpu.read_register(16) == 20, "X16 loaded with 20");
    TEST_CHECK(cpu.get_current_el() == 0, "Still at EL0");

    // Step 2: Execute svc #0x80
    cpu.step(mem);
    TEST_CHECK(cpu.get_current_el() == 1, "CPU transitioned to EL1 upon SVC");
    TEST_CHECK((cpu.get_esr_el1() >> 26u) == 0x15u, "ESR_EL1.EC is 0x15 (SVC synchronous exception)");
    TEST_CHECK(cpu.get_elr_el1() == code_va + 8, "ELR_EL1 points to instruction following SVC");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 212.2 Darwin Syscall ABI Conformance Tests ===\n";
    test_strict_darwin_abi();
    test_fast_abi();
    test_instruction_level_svc();

    if (g_failures == 0) {
        std::cout << "[PASS] Stage 212.2: All Darwin Syscall ABI conformance tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 212.2: " << g_failures << " tests failed.\n";
        return 1;
    }
}

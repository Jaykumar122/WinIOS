#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "device/virtual_platform.h"
#include "runtime/linux_syscall_abi.h"

#define STAGE83_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::device;
using namespace winios::runtime;

namespace {

// -----------------------------------------------------------------------------
// Suite 1: AArch64 Linux Syscall Table and Encoding Mapping
// -----------------------------------------------------------------------------
bool test_syscall_table_mapping() {
    std::cout << "[1] test_syscall_table_mapping...\n";

    STAGE83_ASSERT(LinuxSyscalls::kRead == 63, "read is 63");
    STAGE83_ASSERT(LinuxSyscalls::kWrite == 64, "write is 64");
    STAGE83_ASSERT(LinuxSyscalls::kClose == 57, "close is 57");
    STAGE83_ASSERT(LinuxSyscalls::kOpenAt == 56, "openat is 56");
    STAGE83_ASSERT(LinuxSyscalls::kFstat == 80, "fstat is 80");
    STAGE83_ASSERT(LinuxSyscalls::kExit == 93, "exit is 93");
    STAGE83_ASSERT(LinuxSyscalls::kExitGroup == 94, "exit_group is 94");
    STAGE83_ASSERT(LinuxSyscalls::kSetTidAddress == 96, "set_tid_address is 96");
    STAGE83_ASSERT(LinuxSyscalls::kRtSigaction == 134, "rt_sigaction is 134");
    STAGE83_ASSERT(LinuxSyscalls::kRtSigprocmask == 135, "rt_sigprocmask is 135");
    STAGE83_ASSERT(LinuxSyscalls::kRtSigreturn == 139, "rt_sigreturn is 139");
    STAGE83_ASSERT(LinuxSyscalls::kUname == 160, "uname is 160");
    STAGE83_ASSERT(LinuxSyscalls::kGetpid == 172, "getpid is 172");
    STAGE83_ASSERT(LinuxSyscalls::kGettid == 178, "gettid is 178");
    STAGE83_ASSERT(LinuxSyscalls::kBrk == 214, "brk is 214");
    STAGE83_ASSERT(LinuxSyscalls::kMunmap == 215, "munmap is 215");
    STAGE83_ASSERT(LinuxSyscalls::kClone == 220, "clone is 220");
    STAGE83_ASSERT(LinuxSyscalls::kExecve == 221, "execve is 221");
    STAGE83_ASSERT(LinuxSyscalls::kMmap == 222, "mmap is 222");
    STAGE83_ASSERT(LinuxSyscalls::kGetrandom == 278, "getrandom is 278");

    STAGE83_ASSERT(std::string(LinuxSyscallAbi::get_syscall_name(64)) == "write", "Name write");
    STAGE83_ASSERT(std::string(LinuxSyscallAbi::get_syscall_name(172)) == "getpid", "Name getpid");
    STAGE83_ASSERT(std::string(LinuxSyscallAbi::get_syscall_name(9999)) == "unknown", "Unknown syscall");

    std::cout << "[PASS] AArch64 Linux Syscall Table and Encoding Mapping\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Syscall Parameter Marshalling
// -----------------------------------------------------------------------------
bool test_syscall_parameter_marshalling() {
    std::cout << "[2] test_syscall_parameter_marshalling...\n";

    CPU cpu;
    cpu.reset();
    cpu.write_register(8, LinuxSyscalls::kWrite); // X8 = 64
    cpu.write_register(0, 1);                    // X0 = fd 1
    cpu.write_register(1, 0x00401000ULL);        // X1 = buf
    cpu.write_register(2, 14);                   // X2 = count
    cpu.write_register(3, 0x1111);               // X3
    cpu.write_register(4, 0x2222);               // X4
    cpu.write_register(5, 0x3333);               // X5
    cpu.set_program_counter(0x00400020ULL);
    cpu.set_stack_pointer(0x007FFFF0ULL);

    LinuxSyscallContext ctx = LinuxSyscallContext::capture(cpu);
    STAGE83_ASSERT(ctx.syscall_nr == 64, "Captured X8");
    STAGE83_ASSERT(ctx.args[0] == 1, "Captured X0");
    STAGE83_ASSERT(ctx.args[1] == 0x00401000ULL, "Captured X1");
    STAGE83_ASSERT(ctx.args[2] == 14, "Captured X2");
    STAGE83_ASSERT(ctx.args[3] == 0x1111, "Captured X3");
    STAGE83_ASSERT(ctx.args[4] == 0x2222, "Captured X4");
    STAGE83_ASSERT(ctx.args[5] == 0x3333, "Captured X5");
    STAGE83_ASSERT(ctx.pc == 0x00400020ULL, "Captured PC");
    STAGE83_ASSERT(ctx.sp == 0x007FFFF0ULL, "Captured SP");

    ctx.commit_return(cpu, 14);
    STAGE83_ASSERT(cpu.read_register(0) == 14, "Committed return into X0");

    std::cout << "[PASS] Syscall Parameter Marshalling\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Syscall Return and Error Encoding
// -----------------------------------------------------------------------------
bool test_syscall_return_and_error_encoding() {
    std::cout << "[3] test_syscall_return_and_error_encoding...\n";

    // Success returns
    STAGE83_ASSERT(!LinuxSyscallAbi::is_error_return(0), "0 is success");
    STAGE83_ASSERT(!LinuxSyscallAbi::is_error_return(1024), "Positive is success");
    STAGE83_ASSERT(LinuxSyscallAbi::extract_errno(100) == 0, "No errno for success");

    // Standard Linux error return values: [-4095, -1]
    STAGE83_ASSERT(LinuxSyscallAbi::is_error_return(-1), "-1 (EPERM) is error");
    STAGE83_ASSERT(LinuxSyscallAbi::extract_errno(-1) == 1, "errno is 1");

    STAGE83_ASSERT(LinuxSyscallAbi::is_error_return(-2), "-2 (ENOENT) is error");
    STAGE83_ASSERT(LinuxSyscallAbi::extract_errno(-2) == 2, "errno is 2");

    STAGE83_ASSERT(LinuxSyscallAbi::is_error_return(-14), "-14 (EFAULT) is error");
    STAGE83_ASSERT(LinuxSyscallAbi::extract_errno(-14) == 14, "errno is 14");

    STAGE83_ASSERT(LinuxSyscallAbi::is_error_return(-4095), "-4095 boundary error");
    STAGE83_ASSERT(LinuxSyscallAbi::extract_errno(-4095) == 4095, "errno is 4095");

    // Outside [-4095, -1] is not an errno
    STAGE83_ASSERT(!LinuxSyscallAbi::is_error_return(-4096), "-4096 is pointer/out of range");

    std::cout << "[PASS] Syscall Return and Error Encoding\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: 16-byte SP Alignment Conformance
// -----------------------------------------------------------------------------
bool test_sp_alignment_conformance() {
    std::cout << "[4] test_sp_alignment_conformance...\n";

    STAGE83_ASSERT(LinuxSyscallAbi::is_sp_aligned(0x007FFFF0ULL), "0x007FFFF0 is 16-byte aligned");
    STAGE83_ASSERT(LinuxSyscallAbi::is_sp_aligned(0x40000000ULL), "0x40000000 is 16-byte aligned");
    STAGE83_ASSERT(LinuxSyscallAbi::is_sp_aligned(0x0ULL), "0 is 16-byte aligned");

    STAGE83_ASSERT(!LinuxSyscallAbi::is_sp_aligned(0x007FFFF8ULL), "0x007FFFF8 is misaligned (8 mod 16)");
    STAGE83_ASSERT(!LinuxSyscallAbi::is_sp_aligned(0x007FFFF4ULL), "0x007FFFF4 is misaligned (4 mod 16)");
    STAGE83_ASSERT(!LinuxSyscallAbi::is_sp_aligned(0x007FFFF1ULL), "0x007FFFF1 is misaligned (1 mod 16)");

    std::cout << "[PASS] 16-byte SP Alignment Conformance\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: TPIDR_EL0 TLS Thread Pointer Access
// -----------------------------------------------------------------------------
bool test_tpidr_el0_thread_pointer() {
    std::cout << "[5] test_tpidr_el0_thread_pointer...\n";

    CPU cpu;
    cpu.reset();

    STAGE83_ASSERT(cpu.get_tpidr_el0() == 0, "Initial TPIDR_EL0 is 0");

    // Write thread pointer
    std::uint64_t tls_base = 0x00450000ULL;
    cpu.set_tpidr_el0(tls_base);
    STAGE83_ASSERT(cpu.get_tpidr_el0() == tls_base, "TPIDR_EL0 updated");

    // Reset clears or preserves as configured
    cpu.set_tpidr_el0(0xDEADBEEFULL);
    STAGE83_ASSERT(cpu.get_tpidr_el0() == 0xDEADBEEFULL, "TPIDR_EL0 persists across operations");

    std::cout << "[PASS] TPIDR_EL0 TLS Thread Pointer Access\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Privilege Transition: EL0 -> SVC -> EL1 Entry
// -----------------------------------------------------------------------------
bool test_privilege_transition_svc() {
    std::cout << "[6] test_privilege_transition_svc...\n";

    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(0); // Userspace EL0
    cpu.set_vbar_el1(0x00010000ULL);

    // PC points to SVC #0 instruction in memory
    Memory mem(0x00100000ULL);
    // SVC #0 encoding: 0xD4000001u
    mem.write_u32(0x00002000u, 0xD4000001u);
    cpu.set_program_counter(0x00002000ULL);

    STAGE83_ASSERT(cpu.get_current_el() == 0, "Start at EL0");

    // Execute SVC #0
    cpu.step(mem);

    // Architectural transition:
    // 1. CurrentEL must now be 1
    STAGE83_ASSERT(cpu.get_current_el() == 1, "Transitioned to EL1");

    // 2. ELR_EL1 must hold return PC (instruction after SVC: 0x00002004)
    STAGE83_ASSERT(cpu.get_elr_el1() == 0x00002004ULL, "ELR_EL1 set to PC + 4");

    // 3. ESR_EL1 must have EC = 0x15 (SVC in AArch64)
    std::uint32_t esr = cpu.get_esr_el1();
    std::uint32_t ec = (esr >> 26u) & 0x3Fu;
    STAGE83_ASSERT(ec == 0x15u, "ESR_EL1.EC is 0x15 (SVC AArch64)");

    // 4. PC routed to vector base + offset 0x400 (Synchronous 64-bit EL0)
    STAGE83_ASSERT(cpu.get_program_counter() == 0x00010400ULL, "PC routed to VBAR_EL1 + 0x400");

    std::cout << "[PASS] Privilege Transition: EL0 -> SVC -> EL1 Entry\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: ERET Return from EL1 to EL0 with SPSR PSTATE Restoration
// -----------------------------------------------------------------------------
bool test_eret_return_to_userspace() {
    std::cout << "[7] test_eret_return_to_userspace...\n";

    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1); // Kernel EL1

    // Setup return state: return to EL0 at 0x00002004
    cpu.set_elr_el1(0x00002004ULL);
    cpu.set_spsr_el1(0x00000000u); // EL0t, unmasked

    // ERET instruction encoding: 0xD69F03E0u
    Memory mem(0x00100000ULL);
    mem.write_u32(0x00010420u, 0xD69F03E0u);
    cpu.set_program_counter(0x00010420ULL);

    cpu.step(mem);

    // Verify return:
    STAGE83_ASSERT(cpu.get_current_el() == 0, "CurrentEL restored to EL0");
    STAGE83_ASSERT(cpu.get_program_counter() == 0x00002004ULL, "PC restored to ELR_EL1");

    std::cout << "[PASS] ERET Return from EL1 to EL0 with SPSR PSTATE Restoration\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Signal Restorer / rt_sigreturn Syscall Protocol
// -----------------------------------------------------------------------------
bool test_signal_restorer_protocol() {
    std::cout << "[8] test_signal_restorer_protocol...\n";

    STAGE83_ASSERT(LinuxSyscalls::kRtSigreturn == 139, "rt_sigreturn is 139");

    CPU cpu;
    cpu.reset();
    cpu.write_register(8, LinuxSyscalls::kRtSigreturn);
    LinuxSyscallContext ctx = LinuxSyscallContext::capture(cpu);
    STAGE83_ASSERT(ctx.syscall_nr == 139, "Syscall NR is 139 for rt_sigreturn");
    STAGE83_ASSERT(LinuxSyscallAbi::is_valid_arm64_syscall(ctx.syscall_nr), "Valid syscall");

    std::cout << "[PASS] Signal Restorer / rt_sigreturn Syscall Protocol\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: Synthetic Userspace Syscall Dispatcher
// -----------------------------------------------------------------------------
bool test_synthetic_syscall_dispatcher() {
    std::cout << "[9] test_synthetic_syscall_dispatcher...\n";

    CPU cpu;
    Memory mem(0x00100000ULL);
    std::string console;

    // 1. getpid (172)
    cpu.reset();
    cpu.write_register(8, LinuxSyscalls::kGetpid);
    STAGE83_ASSERT(LinuxSyscallAbi::handle_synthetic_syscall(cpu, mem, console), "Handle getpid");
    STAGE83_ASSERT(cpu.read_register(0) == 1, "getpid returns 1");

    // 2. write (64) to stdout (fd=1)
    const std::string msg = "Hello from Userspace\n";
    std::memcpy(mem.raw_data() + 0x1000, msg.data(), msg.size());

    cpu.reset();
    cpu.write_register(8, LinuxSyscalls::kWrite);
    cpu.write_register(0, 1); // stdout
    cpu.write_register(1, 0x1000); // buf
    cpu.write_register(2, msg.size()); // count
    STAGE83_ASSERT(LinuxSyscallAbi::handle_synthetic_syscall(cpu, mem, console), "Handle write");
    STAGE83_ASSERT(cpu.read_register(0) == msg.size(), "write returns count");
    STAGE83_ASSERT(console == msg, "Captured stdout text matches");

    // 3. brk query (214)
    cpu.reset();
    cpu.write_register(8, LinuxSyscalls::kBrk);
    cpu.write_register(0, 0); // query
    STAGE83_ASSERT(LinuxSyscallAbi::handle_synthetic_syscall(cpu, mem, console), "Handle brk");
    STAGE83_ASSERT(cpu.read_register(0) == 0x00420000ULL, "Initial heap break");

    // 4. getrandom (278)
    cpu.reset();
    cpu.write_register(8, LinuxSyscalls::kGetrandom);
    cpu.write_register(0, 0x2000); // buf
    cpu.write_register(1, 16);     // 16 bytes
    STAGE83_ASSERT(LinuxSyscallAbi::handle_synthetic_syscall(cpu, mem, console), "Handle getrandom");
    STAGE83_ASSERT(cpu.read_register(0) == 16, "getrandom returns 16 bytes");
    STAGE83_ASSERT(mem.read_u8(0x2000) != 0, "Random bytes written");

    // 5. exit (93)
    cpu.reset();
    cpu.write_register(8, LinuxSyscalls::kExit);
    cpu.write_register(0, 0);
    STAGE83_ASSERT(LinuxSyscallAbi::handle_synthetic_syscall(cpu, mem, console), "Handle exit");
    STAGE83_ASSERT(cpu.is_halted(), "CPU halted on exit");

    std::cout << "[PASS] Synthetic Userspace Syscall Dispatcher\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: End-to-end Userspace to Kernel Syscall Roundtrip in VirtualPlatform
// -----------------------------------------------------------------------------
bool test_end_to_end_syscall_roundtrip() {
    std::cout << "[10] test_end_to_end_syscall_roundtrip...\n";

    VirtualPlatformConfig plat_cfg;
    plat_cfg.core_count = 1;
    plat_cfg.ram_base = 0;
    plat_cfg.ram_size = 0x00100000ULL; // 1 MB
    VirtualPlatform platform(plat_cfg);

    CPU& cpu = platform.get_cpu(0);
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x00040000ULL);

    // Place user code at 0x00010000:
    // 1. MOV X8, #172 (getpid) -> 0xD2801588
    // 2. SVC #0                -> 0xD4000001
    // 3. NOP                   -> 0xD503201F
    platform.memory().write_u32(0x00010000u, 0xD2801588u);
    platform.memory().write_u32(0x00010004u, 0xD4000001u);
    platform.memory().write_u32(0x00010008u, 0xD503201Fu);

    // Start at EL0
    cpu.set_current_el(0);
    cpu.set_program_counter(0x00010000ULL);

    // Step 1: MOV X8, #172
    platform.step_core(0);
    STAGE83_ASSERT(cpu.read_register(8) == 172, "X8 has 172 (getpid)");
    STAGE83_ASSERT(cpu.get_current_el() == 0, "Still in EL0");

    // Step 2: SVC #0
    platform.step_core(0);
    STAGE83_ASSERT(cpu.get_current_el() == 1, "Entered EL1 kernel mode");
    STAGE83_ASSERT(cpu.get_program_counter() == 0x00040400ULL, "PC at EL1 synchronous vector");
    STAGE83_ASSERT(cpu.get_elr_el1() == 0x00010008ULL, "ELR_EL1 points to user NOP");

    // Kernel handles syscall:
    std::string console;
    STAGE83_ASSERT(LinuxSyscallAbi::handle_synthetic_syscall(cpu, platform.memory(), console), "Kernel handled getpid");
    STAGE83_ASSERT(cpu.read_register(0) == 1, "Return value X0 = 1");

    // Place kernel return handler at 0x00040400:
    // ERET -> 0xD69F03E0
    platform.memory().write_u32(0x00040400u, 0xD69F03E0u);

    // Step 3: Kernel executes ERET
    platform.step_core(0);
    STAGE83_ASSERT(cpu.get_current_el() == 0, "Returned to EL0 userspace");
    STAGE83_ASSERT(cpu.get_program_counter() == 0x00010008ULL, "PC back at user instruction");
    STAGE83_ASSERT(cpu.read_register(0) == 1, "User process observes PID = 1 in X0");

    std::cout << "[PASS] End-to-end Userspace to Kernel Syscall Roundtrip in VirtualPlatform\n\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=== WinIOS Stage 83: Linux Guest Userspace & Syscall ABI Tests ===\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*test_fn)(), const char* name) {
        std::cout << "Starting " << name << "...\n";
        if (test_fn()) {
            passed++;
            std::cout << "Completed " << name << ": OK\n";
        } else {
            failed++;
            std::cerr << "[FAIL] " << name << "\n";
        }
    };

    run_test(test_syscall_table_mapping, "test_syscall_table_mapping");
    run_test(test_syscall_parameter_marshalling, "test_syscall_parameter_marshalling");
    run_test(test_syscall_return_and_error_encoding, "test_syscall_return_and_error_encoding");
    run_test(test_sp_alignment_conformance, "test_sp_alignment_conformance");
    run_test(test_tpidr_el0_thread_pointer, "test_tpidr_el0_thread_pointer");
    run_test(test_privilege_transition_svc, "test_privilege_transition_svc");
    run_test(test_eret_return_to_userspace, "test_eret_return_to_userspace");
    run_test(test_signal_restorer_protocol, "test_signal_restorer_protocol");
    run_test(test_synthetic_syscall_dispatcher, "test_synthetic_syscall_dispatcher");
    run_test(test_end_to_end_syscall_roundtrip, "test_end_to_end_syscall_roundtrip");

    std::cout << "\nStage 83 Summary: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}

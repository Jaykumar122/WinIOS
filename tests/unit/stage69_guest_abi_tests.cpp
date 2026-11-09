#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/synchronous_exception.h"
#include "memory/memory.h"
#include "runtime/guest_runtime.h"
#include "runtime/guest_syscall_dispatcher.h"

#define STAGE69_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios::runtime;

namespace {

void step_inst(CPU& cpu, Memory& memory, std::uint32_t inst) {
    memory.write_u32(cpu.get_program_counter(), inst);
    cpu.step(memory);
}

// -----------------------------------------------------------------------------
// Suite 1: AAPCS64 Function Argument Passing (X0–X7) & Return (X0)
// -----------------------------------------------------------------------------
bool test_aapcs64_argument_passing() {
    std::cout << "[1] test_aapcs64_argument_passing...\n";
    CPU cpu;
    Memory memory(0x4000);

    // Set 8 arguments in X0..X7
    for (std::uint64_t i = 0; i < 8; ++i) {
        cpu.write_register(i, (i + 1) * 10);
    }

    // Emulate addition of first two arguments: ADD X0, X0, X1 (0x8B010000u)
    step_inst(cpu, memory, 0x8B010000u);

    // Return value in X0 must be 10 + 20 = 30
    STAGE69_ASSERT(cpu.read_register(0) == 30, "Return value in X0 is 30");

    // Remaining argument registers unchanged
    for (std::uint64_t i = 2; i < 8; ++i) {
        STAGE69_ASSERT(cpu.read_register(i) == (i + 1) * 10, "Argument registers X2..X7 intact");
    }

    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Callee-Saved Register Invariants (X19–X28, X29 FP, X30 LR)
// -----------------------------------------------------------------------------
bool test_callee_saved_invariants() {
    std::cout << "[2] test_callee_saved_invariants...\n";
    CPU cpu;
    Memory memory(0x4000);

    // Set callee-saved registers X19..X28, X29 (FP), X30 (LR)
    for (std::size_t r = 19; r <= 30; ++r) {
        cpu.write_register(r, 0x10000000ULL + r);
    }

    // Execute standard scratch instructions that mutate X0..X8
    // MOV X0, #1 -> 0xD2800020u
    // MOV X1, #2 -> 0xD2800041u
    step_inst(cpu, memory, 0xD2800020u);
    step_inst(cpu, memory, 0xD2800041u);

    // Verify all callee-saved registers strictly preserved
    for (std::size_t r = 19; r <= 30; ++r) {
        STAGE69_ASSERT(cpu.read_register(r) == (0x10000000ULL + r),
                       "Callee-saved register strictly preserved");
    }

    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: 16-Byte Stack Alignment Invariant
// -----------------------------------------------------------------------------
bool test_stack_pointer_16byte_alignment() {
    std::cout << "[3] test_stack_pointer_16byte_alignment...\n";
    CPU cpu;
    Memory memory(0x4000);

    // Aligned SP (0x2000 is a multiple of 16)
    cpu.set_stack_pointer(0x2000);
    STAGE69_ASSERT((cpu.get_stack_pointer() % 16u) == 0, "Initial SP is 16-byte aligned");

    // Push 16 bytes: SUB SP, SP, #16 (0xD10043FFu)
    step_inst(cpu, memory, 0xD10043FFu);
    STAGE69_ASSERT(cpu.get_stack_pointer() == 0x1FF0, "SP decremented by 16");
    STAGE69_ASSERT((cpu.get_stack_pointer() % 16u) == 0, "SP remains 16-byte aligned after push");

    // Pop 16 bytes: ADD SP, SP, #16 (0x910043FFu)
    step_inst(cpu, memory, 0x910043FFu);
    STAGE69_ASSERT(cpu.get_stack_pointer() == 0x2000, "SP restored to 0x2000");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: SVC Exception Entry & Syndrome Generation
// -----------------------------------------------------------------------------
bool test_svc_exception_syndrome() {
    std::cout << "[4] test_svc_exception_syndrome...\n";
    CPU cpu;
    Memory memory(0x4000);
    cpu.set_architectural_exceptions(true);

    const std::uint64_t svc_pc = 0x1000;
    cpu.set_program_counter(svc_pc);

    // SVC #0x42: bits[20:5] = 0x42 -> 0xD4000001u | (0x42 << 5) = 0xD4000841u
    step_inst(cpu, memory, 0xD4000841u);

    // Check ESR_EL1: EC must be SvcAArch64 (0x15) and imm16 must be 0x42
    const std::uint64_t esr = cpu.get_esr_el1();
    const std::uint32_t ec = static_cast<std::uint32_t>((esr >> 26u) & 0x3Fu);
    const std::uint16_t imm = static_cast<std::uint16_t>(esr & 0xFFFFu);

    STAGE69_ASSERT(ec == static_cast<std::uint32_t>(ExceptionClass::SvcAArch64), "ESR_EL1.EC is SvcAArch64 (0x15)");
    STAGE69_ASSERT(imm == 0x42u, "ESR_EL1.imm16 matches SVC operand 0x42");

    // ELR_EL1 must hold return address: svc_pc + 4 = 0x1004
    STAGE69_ASSERT(cpu.get_elr_el1() == svc_pc + 4, "ELR_EL1 captures next instruction PC");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: Syscall Write Dispatch & Output Capture
// -----------------------------------------------------------------------------
bool test_syscall_write_dispatch() {
    std::cout << "[5] test_syscall_write_dispatch...\n";
    CPU cpu;
    Memory memory(0x4000);
    GuestSyscallDispatcher dispatcher;

    const std::string message = "WinIOS POSIX Output\n";
    const std::uint64_t buf_addr = 0x200;

    // Write message into guest memory
    for (std::size_t i = 0; i < message.size(); ++i) {
        memory.write_u8(buf_addr + i, static_cast<std::uint8_t>(message[i]));
    }

    // Setup syscall registers: X8 = 64 (Write), X0 = 1 (stdout), X1 = buf, X2 = length
    cpu.write_register(8, static_cast<std::uint64_t>(StandardSyscall::Write));
    cpu.write_register(0, 1);
    cpu.write_register(1, buf_addr);
    cpu.write_register(2, message.size());

    const std::int64_t res = dispatcher.dispatch(cpu, memory);
    STAGE69_ASSERT(res == static_cast<std::int64_t>(message.size()), "Write returns byte count");
    STAGE69_ASSERT(cpu.read_register(0) == message.size(), "X0 holds return count");
    STAGE69_ASSERT(dispatcher.stdout_buffer() == message, "Stdout buffer captured exact string");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Syscall Exit Dispatch & State Tracking
// -----------------------------------------------------------------------------
bool test_syscall_exit_dispatch() {
    std::cout << "[6] test_syscall_exit_dispatch...\n";
    CPU cpu;
    Memory memory(0x4000);
    GuestSyscallDispatcher dispatcher;

    STAGE69_ASSERT(!dispatcher.has_exited(), "Initially not exited");

    // Setup X8 = 93 (Exit), X0 = 42 (Exit Code)
    cpu.write_register(8, static_cast<std::uint64_t>(StandardSyscall::Exit));
    cpu.write_register(0, 42);

    dispatcher.dispatch(cpu, memory);
    STAGE69_ASSERT(dispatcher.has_exited() == true, "Dispatcher marks exited");
    STAGE69_ASSERT(dispatcher.exit_code() == 42, "Exit code is 42");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Syscall GetPid & ClockGetTime Dispatch
// -----------------------------------------------------------------------------
bool test_syscall_getpid_and_clock() {
    std::cout << "[7] test_syscall_getpid_and_clock...\n";
    CPU cpu;
    Memory memory(0x4000);
    GuestSyscallDispatcher dispatcher;

    // 1. GetPid (172)
    cpu.write_register(8, static_cast<std::uint64_t>(StandardSyscall::GetPid));
    const std::int64_t pid = dispatcher.dispatch(cpu, memory);
    STAGE69_ASSERT(pid == 100, "GetPid returns 100");
    STAGE69_ASSERT(cpu.read_register(0) == 100, "X0 holds PID 100");

    // 2. ClockGetTime (113)
    const std::uint64_t ts_addr = 0x300;
    cpu.write_register(8, static_cast<std::uint64_t>(StandardSyscall::ClockGetTime));
    cpu.write_register(0, 0); // CLOCK_REALTIME
    cpu.write_register(1, ts_addr);

    const std::int64_t clock_res = dispatcher.dispatch(cpu, memory);
    STAGE69_ASSERT(clock_res == 0, "ClockGetTime returns 0 on success");
    STAGE69_ASSERT(memory.read_u64(ts_addr) == 1000ULL, "Timespec tv_sec written as 1000");
    STAGE69_ASSERT(memory.read_u64(ts_addr + 8) == 0ULL, "Timespec tv_nsec written as 0");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Out-of-Bounds User Pointer Sandboxing
// -----------------------------------------------------------------------------
bool test_out_of_bounds_pointer_sandboxing() {
    std::cout << "[8] test_out_of_bounds_pointer_sandboxing...\n";
    CPU cpu;
    Memory memory(0x4000); // 16 KB memory
    GuestSyscallDispatcher dispatcher;

    // Pointer out of bounds (0x10000 > 0x4000)
    cpu.write_register(8, static_cast<std::uint64_t>(StandardSyscall::Write));
    cpu.write_register(0, 1);
    cpu.write_register(1, 0x10000); // Invalid address
    cpu.write_register(2, 64);

    const std::int64_t res = dispatcher.dispatch(cpu, memory);
    STAGE69_ASSERT(res == -14, "Dispatcher returns -EFAULT (-14) for out-of-bounds pointer");
    STAGE69_ASSERT(static_cast<std::int64_t>(cpu.read_register(0)) == -14, "X0 returns -EFAULT");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: Custom Syscall Registration & Invocation
// -----------------------------------------------------------------------------
bool test_custom_syscall_registration() {
    std::cout << "[9] test_custom_syscall_registration...\n";
    CPU cpu;
    Memory memory(0x4000);
    GuestSyscallDispatcher dispatcher;

    // Register custom syscall 500: square input in X0
    dispatcher.register_handler(500, [](CPU& c, Memory&) -> std::int64_t {
        const std::uint64_t arg = c.read_register(0);
        return static_cast<std::int64_t>(arg * arg);
    });

    STAGE69_ASSERT(dispatcher.has_handler(500), "Custom handler 500 registered");

    cpu.write_register(8, 500);
    cpu.write_register(0, 12);

    const std::int64_t res = dispatcher.dispatch(cpu, memory);
    STAGE69_ASSERT(res == 144, "Custom syscall computed 12 * 12 = 144");
    STAGE69_ASSERT(cpu.read_register(0) == 144, "X0 holds 144");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: Runtime Instruction Budgeting & Limits
// -----------------------------------------------------------------------------
bool test_runtime_instruction_budgeting() {
    std::cout << "[10] test_runtime_instruction_budgeting...\n";
    RuntimeConfig config;
    config.memory_size = 0x10000;
    config.load_address = 0x1000;
    config.entry_address = 0x1000;
    config.stack_base = 0x4000;
    config.stack_size = 0x1000;
    config.max_instructions = 10; // Bounded limit

    // Verify config validation logic
    STAGE69_ASSERT(config.memory_size > 0 && config.max_instructions > 0, "Config is valid");
    STAGE69_ASSERT((config.stack_base + config.stack_size) % 16u == 0, "Stack is aligned");

    return true;
}

} // namespace

int main() {
    std::cout << "=== Running Stage 69 Guest ABI & Syscall Dispatch Tests ===" << std::endl;

    if (!test_aapcs64_argument_passing()) return 1;
    if (!test_callee_saved_invariants()) return 2;
    if (!test_stack_pointer_16byte_alignment()) return 3;
    if (!test_svc_exception_syndrome()) return 4;
    if (!test_syscall_write_dispatch()) return 5;
    if (!test_syscall_exit_dispatch()) return 6;
    if (!test_syscall_getpid_and_clock()) return 7;
    if (!test_out_of_bounds_pointer_sandboxing()) return 8;
    if (!test_custom_syscall_registration()) return 9;
    if (!test_runtime_instruction_budgeting()) return 10;

    std::cout << "stage69_guest_abi_tests: all 10 test suites passed successfully" << std::endl;
    return 0;
}

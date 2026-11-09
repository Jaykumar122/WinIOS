#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"

using namespace winios;
using namespace winios::runtime;
using namespace winios::loader;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Test 1: Basic Guest Mach-O Instruction Retirement
bool test_guest_macho_instruction_retirement() {
    std::cout << "[Test 1] Guest Mach-O Instruction Retirement & Accounting...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024); // 64 MB
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly:
    // mov x0, #42
    // mov x16, #1 (SYS_exit)
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2800540u, // mov x0, #42
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/usr/bin/test_exit";
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 100);

    TEST_ASSERT(res.process_exited, "Process must report clean exit");
    TEST_ASSERT(res.exit_code == 42, "Exit code must match guest X0 (42)");
    TEST_ASSERT(res.guest_instructions_retired >= 3, "At least 3 guest instructions must be retired");
    TEST_ASSERT(res.bsd_syscalls_handled == 1, "Exactly 1 BSD syscall must be handled");

    std::cout << "  Passed (Retired: " << res.guest_instructions_retired
              << " instructions, Exit Code: " << res.exit_code << ")\n";
    return true;
}

// Test 2: Guest SYS_write Output Capture
bool test_guest_sys_write_output_capture() {
    std::cout << "[Test 2] Guest SYS_write String Emission to Host Capture...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly:
    // movz x1, #0x1020 (points to msg at offset 0x1020: 8 instructions * 4 = 32 bytes)
    // mov x0, #1  (stdout)
    // mov x2, #14 (len)
    // mov x16, #4 (SYS_write)
    // svc #0x80
    // mov x0, #0
    // mov x16, #1 (SYS_exit)
    // svc #0x80
    // msg: "WinIOS Darwin\n" (14 chars)
    std::vector<std::uint32_t> guest_asm = {
        0xD2820401u, // movz x1, #0x1020
        0xD2800020u, // mov x0, #1
        0xD28001C2u, // mov x2, #14
        0xD2800090u, // mov x16, #4
        0xD4001001u, // svc #0x80
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    const char msg[] = "WinIOS Darwin\n";
    std::vector<std::uint8_t> code(guest_asm.size() * 4 + sizeof(msg));
    std::memcpy(code.data(), guest_asm.data(), guest_asm.size() * 4);
    std::memcpy(code.data() + guest_asm.size() * 4, msg, sizeof(msg));

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 101);

    TEST_ASSERT(res.process_exited, "Process must exit cleanly");
    TEST_ASSERT(res.exit_code == 0, "Exit code must be 0");
    TEST_ASSERT(res.stdout_output == "WinIOS Darwin\n", "Output captured from guest must match string");
    TEST_ASSERT(res.bsd_syscalls_handled == 2, "Must handle 2 BSD syscalls (write + exit)");

    std::cout << "  Passed (Captured Stdout: \"" << res.stdout_output << "\")\n";
    return true;
}

// Test 3: Guest Mach Traps Execution
bool test_guest_mach_traps_execution() {
    std::cout << "[Test 3] Guest Mach Traps Execution (task_self, reply_port, msg)...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly:
    // mov x16, #-28 (task_self_trap)
    // svc #0x81
    // mov x16, #-27 (mach_reply_port_trap)
    // svc #0x81
    // mov x16, #-26 (mach_msg_trap)
    // svc #0x81
    // mov x0, #0
    // mov x16, #1 (SYS_exit)
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0x92800370u, // mov x16, #-28 (signed imm)
        0xD4001021u, // svc #0x81
        0x92800350u, // mov x16, #-27
        0xD4001021u, // svc #0x81
        0x92800330u, // mov x16, #-26
        0xD4001021u, // svc #0x81
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 102);

    TEST_ASSERT(res.process_exited, "Process must exit cleanly");
    TEST_ASSERT(res.mach_traps_handled == 3, "Must handle exactly 3 Mach traps");
    TEST_ASSERT(res.bsd_syscalls_handled == 1, "Must handle 1 BSD exit syscall");

    std::cout << "  Passed (Mach Traps Handled: " << res.mach_traps_handled << ")\n";
    return true;
}

// Test 4: Host vs Guest Event Classification & Trace Attribution
bool test_host_vs_guest_trace_attribution() {
    std::cout << "[Test 4] Host vs Guest Trace Event Attribution...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    std::vector<std::uint32_t> guest_asm = {
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1 (SYS_exit)
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 103);

    TEST_ASSERT(!res.trace.empty(), "Execution trace must contain events");
    TEST_ASSERT(res.host_events_count > 0, "Trace must have Host events (Mach-O load, stack)");
    TEST_ASSERT(res.guest_events_count > 0, "Trace must have Guest events (EL0 entry, exit)");

    bool found_host_load = false;
    bool found_guest_entry = false;
    bool found_guest_exit = false;

    for (const auto& ev : res.trace) {
        if (ev.origin == ExecutionOrigin::Host && ev.stage == PipelineExecutionStage::MachOLoading) {
            found_host_load = true;
        }
        if (ev.origin == ExecutionOrigin::Guest && ev.stage == PipelineExecutionStage::UserspaceEntry) {
            found_guest_entry = true;
        }
        if (ev.origin == ExecutionOrigin::Guest && ev.stage == PipelineExecutionStage::ProcessExit) {
            found_guest_exit = true;
        }
    }

    TEST_ASSERT(found_host_load, "Trace must record Host MachOLoading event");
    TEST_ASSERT(found_guest_entry, "Trace must record Guest UserspaceEntry event");
    TEST_ASSERT(found_guest_exit, "Trace must record Guest ProcessExit event");

    std::cout << "  Passed (Host Events: " << res.host_events_count
              << ", Guest Events: " << res.guest_events_count << ")\n";
    return true;
}

// Test 5: Complete End-to-End Pipeline Execution (Boot -> Rootfs -> Userspace App)
bool test_complete_end_to_end_pipeline() {
    std::cout << "[Test 5] Complete End-to-End Pipeline Execution...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;

    auto res = DarwinUserspaceEngine::run_complete_end_to_end_pipeline(cpu, mem, bsd, ipc);

    TEST_ASSERT(res.process_exited, "End-to-end pipeline must result in clean process exit");
    TEST_ASSERT(res.exit_code == 0, "Process exit code must be 0");
    TEST_ASSERT(res.guest_instructions_retired > 0, "Guest instructions must be retired");
    TEST_ASSERT(res.stdout_output.find("Hello iOS Userspace!") != std::string::npos,
                "Stdout must capture userspace banner");

    std::cout << "  Passed (End-to-End Retired: " << res.guest_instructions_retired
              << " instrs, Stdout: \"" << res.stdout_output << "\")\n";
    return true;
}

// Test 6: BSD Process Table & File Descriptor Synchronization
bool test_bsd_subsystem_synchronization() {
    std::cout << "[Test 6] BSD Process Table & File Descriptor Sync with Guest...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly:
    // mov x16, #20 (SYS_getpid)
    // svc #0x80
    // mov x16, #24 (SYS_getuid)
    // svc #0x80
    // mov x0, #0
    // mov x16, #1 (SYS_exit)
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2800290u, // mov x16, #20
        0xD4001001u, // svc #0x80
        0xD2800310u, // mov x16, #24
        0xD4001001u, // svc #0x80
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 555);

    TEST_ASSERT(res.process_exited, "Process must exit cleanly");
    auto proc = bsd.get_process(res.pid);
    TEST_ASSERT(proc != nullptr, "Process must exist in BSD process table");
    TEST_ASSERT(proc->state == ProcessState::Zombie || proc->state == ProcessState::Terminated,
                "Process state must reflect termination");

    std::cout << "  Passed (Process " << res.pid << " state synchronized in BSD table)\n";
    return true;
}

// Test 7: Instruction Budget Enforcement
bool test_instruction_budget_enforcement() {
    std::cout << "[Test 7] Instruction Budget Enforcement on Infinite Loop...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly: infinite branch to self
    // b . (0x14000000)
    std::vector<std::uint32_t> guest_asm = {
        0x14000000u // b .
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.max_instructions = 500;
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 107);

    TEST_ASSERT(!res.process_exited, "Looping process must not report clean exit");
    TEST_ASSERT(res.guest_instructions_retired == 500, "Must stop at exact budget limit (500)");
    TEST_ASSERT(res.error_message.find("budget") != std::string::npos, "Error message must state budget exceeded");

    std::cout << "  Passed (Budget clamped execution at " << res.guest_instructions_retired << " instructions)\n";
    return true;
}

// Test 8: Guest Architectural Exception Fault Handling
bool test_guest_architectural_fault_handling() {
    std::cout << "[Test 8] Guest Architectural Fault & Abort Trapping...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly: Undefined instruction
    // 0x00000000 is unallocated / undefined in AArch64
    std::vector<std::uint32_t> guest_asm = {
        0x00000000u
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 108);

    TEST_ASSERT(!res.process_exited, "Faulting process must not report clean exit");
    TEST_ASSERT(!res.error_message.empty(), "Error message must describe fault");

    std::cout << "  Passed (Fault trapped cleanly: \"" << res.error_message << "\")\n";
    return true;
}

// Test 9: Stack Parameter Inspection (argc & argv from guest)
bool test_stack_parameter_inspection() {
    std::cout << "[Test 9] Guest Stack Parameter Accessibility...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly:
    // ldr x0, [sp]     (load argc from stack top into x0)
    // mov x16, #1      (SYS_exit with exit code = argc)
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xF94003E0u, // ldr x0, [sp]
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.argv = {"arg0", "arg1", "arg2"}; // argc = 3
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 109);

    TEST_ASSERT(res.process_exited, "Process must exit");
    TEST_ASSERT(res.exit_code == 3, "Exit code loaded from [sp] by guest must equal argc (3)");

    std::cout << "  Passed (Guest loaded argc=" << res.exit_code << " from stack pointer)\n";
    return true;
}

// Test 10: Dynamic Memory Allocation (SYS_mmap) from Guest
bool test_guest_sys_mmap() {
    std::cout << "[Test 10] Guest Dynamic Memory Allocation via SYS_mmap...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly:
    // mov x0, #0       (addr = 0)
    // mov x1, #16384   (len = 16KB)
    // mov x16, #197    (SYS_mmap)
    // svc #0x80
    // mov x16, #1      (SYS_exit with exit code = 0)
    // mov x0, #0
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2800000u, // mov x0, #0
        0xD2880001u, // mov x1, #16384 (0x4000)
        0xD28018B0u, // mov x16, #197
        0xD4001001u, // svc #0x80
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 110);

    TEST_ASSERT(res.process_exited, "Process must exit");
    TEST_ASSERT(res.bsd_syscalls_handled >= 2, "Must handle SYS_mmap and SYS_exit");

    std::cout << "  Passed (SYS_mmap allocated and returned to guest)\n";
    return true;
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::cout << "=================================================================\n";
    std::cout << "WINIOS STAGE 130: END-TO-END GUEST EXECUTION PIPELINE VERIFICATION\n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int total = 10;

    auto run_test = [&](auto test_fn, const char* name) {
        try {
            if (test_fn()) {
                passed++;
            } else {
                std::cout << "FAILED: " << name << "\n";
            }
        } catch (const std::exception& e) {
            std::cout << "EXCEPTION in " << name << ": " << e.what() << "\n";
        } catch (...) {
            std::cout << "UNKNOWN EXCEPTION in " << name << "\n";
        }
    };

    run_test(test_guest_macho_instruction_retirement, "test_guest_macho_instruction_retirement");
    run_test(test_guest_sys_write_output_capture, "test_guest_sys_write_output_capture");
    run_test(test_guest_mach_traps_execution, "test_guest_mach_traps_execution");
    run_test(test_host_vs_guest_trace_attribution, "test_host_vs_guest_trace_attribution");
    run_test(test_complete_end_to_end_pipeline, "test_complete_end_to_end_pipeline");
    run_test(test_bsd_subsystem_synchronization, "test_bsd_subsystem_synchronization");
    run_test(test_instruction_budget_enforcement, "test_instruction_budget_enforcement");
    run_test(test_guest_architectural_fault_handling, "test_guest_architectural_fault_handling");
    run_test(test_stack_parameter_inspection, "test_stack_parameter_inspection");
    run_test(test_guest_sys_mmap, "test_guest_sys_mmap");

    std::cout << "-----------------------------------------------------------------\n";
    std::cout << "STAGE 130 RESULTS: " << passed << "/" << total << " tests passed\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}

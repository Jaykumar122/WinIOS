#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/real_artifact_harness.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[ASSERTION FAILED] " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

constexpr std::uint32_t kProtRead    = 0x1u;
constexpr std::uint32_t kProtWrite   = 0x2u;
constexpr std::uint32_t kProtExecute = 0x4u;
constexpr std::uint32_t kMapPrivate  = 0x0002u;
constexpr std::uint32_t kMapAnon     = 0x1000u;

// Test 1: Complete Boot-to-Userspace Pipeline Execution
bool test_complete_boot_to_userspace_pipeline() {
    std::cout << "[Test 1] Complete Boot-to-Userspace Pipeline Execution...\n";

    CPU cpu;
    Memory memory(64 * 1024 * 1024); // 64 MB guest RAM
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;

    auto result = DarwinUserspaceEngine::run_complete_end_to_end_pipeline(cpu, memory, bsd, ipc);

    TEST_ASSERT(result.process_exited, "Process must have exited");
    TEST_ASSERT(result.exit_code == 0, "Exit code must be 0");
    TEST_ASSERT(result.is_clean_exit(), "Pipeline must complete as clean exit");
    TEST_ASSERT(result.guest_instructions_retired >= 8, "At least 8 guest instructions retired");
    TEST_ASSERT(result.bsd_syscalls_handled >= 2, "At least 2 BSD syscalls handled (write + exit)");
    TEST_ASSERT(result.stdout_output.find("Hello iOS Userspace!") != std::string::npos,
                "Stdout must capture \"Hello iOS Userspace!\"");

    return true;
}

// Test 2: Artifact Admission to Execution Gate Conformance
bool test_artifact_admission_to_execution_gate() {
    std::cout << "[Test 2] Artifact Admission to Execution Gate Conformance...\n";

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    // 1. Valid unencrypted executable fixture with clean SYS_exit
    std::vector<std::uint32_t> guest_asm = {
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1 (SYS_exit)
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code_bytes;
    code_bytes.resize(guest_asm.size() * 4);
    std::memcpy(code_bytes.data(), guest_asm.data(), guest_asm.size() * 4);
    auto valid_bin = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code_bytes);

    auto meta_valid = RealArtifactHarness::inspect_buffer(valid_bin.data(), valid_bin.size(), "/bin/valid");
    TEST_ASSERT(meta_valid.is_admitted(), "Clean binary must be admitted");
    TEST_ASSERT(meta_valid.can_execute_in_userspace(), "Clean binary can execute");

    DarwinPipelineConfig config;
    auto res_valid = engine.execute_macho(valid_bin.data(), valid_bin.size(), config, 10);
    TEST_ASSERT(res_valid.process_exited, "Valid binary must exit");
    TEST_ASSERT(res_valid.error_message.empty(), "Valid binary executes without admission errors");

    // 2. Encrypted binary (cryptid = 1)
    struct {
        std::uint32_t cmd{0x0000002Cu};
        std::uint32_t cmdsize{24};
        std::uint32_t cryptoff{0x4000};
        std::uint32_t cryptsize{0x4000};
        std::uint32_t cryptid{1};
        std::uint32_t pad{0};
    } enc_cmd;

    auto enc_bin = valid_bin;
    const auto* old_hdr = reinterpret_cast<const MachOHeader64Raw*>(enc_bin.data());
    const std::size_t insert_pos = sizeof(MachOHeader64Raw) + old_hdr->sizeofcmds;
    const auto* enc_bytes = reinterpret_cast<const std::uint8_t*>(&enc_cmd);
    enc_bin.insert(enc_bin.begin() + insert_pos, enc_bytes, enc_bytes + sizeof(enc_cmd));
    auto* hdr = reinterpret_cast<MachOHeader64Raw*>(enc_bin.data());
    hdr->ncmds += 1;
    hdr->sizeofcmds += static_cast<std::uint32_t>(sizeof(enc_cmd));

    auto meta_enc = RealArtifactHarness::inspect_buffer(enc_bin.data(), enc_bin.size(), "/Applications/Secret.app/Secret");
    TEST_ASSERT(!meta_enc.is_admitted(), "Encrypted binary MUST be rejected by admission gate");
    TEST_ASSERT(!meta_enc.can_execute_in_userspace(), "Encrypted binary cannot execute");

    return true;
}

// Test 3: Userspace Memory & W^X Enforcement in Pipeline
bool test_wx_enforcement_in_pipeline() {
    std::cout << "[Test 3] Userspace Memory & W^X Enforcement in Pipeline...\n";

    DarwinBsdSubsystem bsd;
    const std::uint32_t pid = bsd.create_process(1, "/sbin/launchd", 0);

    // Map __TEXT as RX
    std::uint64_t text_addr = 0x100000000ULL;
    std::uint64_t text_mapped = bsd.mmap(pid, text_addr, 0x4000,
                                         kProtRead | kProtExecute,
                                         kMapPrivate | kMapAnon, -1, 0);
    TEST_ASSERT(text_mapped == text_addr, "__TEXT mapped as RX");

    // Attempt to map region with Write + Execute (W^X violation)
    std::uint64_t wx_addr = 0x100020000ULL;
    std::uint64_t wx_mapped = bsd.mmap(pid, wx_addr, 0x4000,
                                       kProtRead | kProtWrite | kProtExecute,
                                       kMapPrivate | kMapAnon, -1, 0);
    TEST_ASSERT(wx_mapped == 0, "mmap with W^X (PROT_WRITE | PROT_EXEC) must be rejected");

    // Map __DATA as RW
    std::uint64_t data_addr = 0x100010000ULL;
    std::uint64_t data_mapped = bsd.mmap(pid, data_addr, 0x4000,
                                         kProtRead | kProtWrite,
                                         kMapPrivate | kMapAnon, -1, 0);
    TEST_ASSERT(data_mapped == data_addr, "__DATA mapped as RW");

    return true;
}

// Test 4: Guest Instruction Step & Register Retirement Verification
bool test_instruction_step_retirement() {
    std::cout << "[Test 4] Guest Instruction Step & Register Retirement...\n";

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    // Write instruction: mov x0, #42 (0xd2800540) followed by ret (0xd65f03c0)
    std::uint64_t code_addr = 0x1000;
    memory.write_u32(static_cast<std::size_t>(code_addr), 0xd2800540u);
    memory.write_u32(static_cast<std::size_t>(code_addr + 4), 0xd65f03c0u);

    cpu.reset();
    cpu.set_current_el(0); // EL0 Userspace
    cpu.set_program_counter(code_addr);
    cpu.write_register(0, 0);

    DarwinPipelineResult result{};
    DarwinPipelineConfig config;

    bool cont = engine.step_one(result, config);
    TEST_ASSERT(cont, "Instruction stepped successfully");
    TEST_ASSERT(cpu.read_register(0) == 42, "X0 updated to 42 after mov");
    TEST_ASSERT(cpu.get_program_counter() == code_addr + 4, "PC advanced to code_addr + 4");
    TEST_ASSERT(result.guest_instructions_retired == 1, "1 instruction retired");

    return true;
}

// Test 5: BSD Syscall Dispatch & Return Value ABI
bool test_bsd_syscall_dispatch_abi() {
    std::cout << "[Test 5] BSD Syscall Dispatch & Return Value ABI...\n";

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    cpu.reset();
    cpu.set_current_el(0);
    cpu.write_register(16, 20); // SYS_getpid
    cpu.set_elr_el1(0x10004ULL);

    DarwinPipelineResult result{};
    engine.handle_svc(0x80, result);

    TEST_ASSERT(result.bsd_syscalls_handled == 1, "1 BSD syscall handled");
    TEST_ASSERT(cpu.read_register(0) == 1, "SYS_getpid returned PID 1 in X0");
    TEST_ASSERT((cpu.get_nzcv() & 0x2u) == 0, "Carry flag cleared on success");

    return true;
}

// Test 6: Mach Trap Dispatch & Port Space Integration
bool test_mach_trap_dispatch() {
    std::cout << "[Test 6] Mach Trap Dispatch & Port Space Integration...\n";

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    cpu.reset();
    cpu.set_current_el(0);
    // Mach traps have negative trap numbers in X16: -26 = mach_reply_port_trap
    std::int64_t mach_reply_port_trap = -26;
    cpu.write_register(16, static_cast<std::uint64_t>(mach_reply_port_trap));
    cpu.set_elr_el1(0x10008ULL);

    DarwinPipelineResult result{};
    engine.handle_svc(0x80, result);

    TEST_ASSERT(result.mach_traps_handled == 1, "1 Mach trap handled");
    mach_port_t reply_port = static_cast<mach_port_t>(cpu.read_register(0));
    TEST_ASSERT(reply_port != 0, "Allocated non-zero reply port right in X0");

    auto space = ipc.get_task_port_space(1);
    TEST_ASSERT(space != nullptr, "Task port space 1 exists");
    TEST_ASSERT(space->lookup_port(reply_port) != nullptr, "Reply port is registered in task space");

    return true;
}

// Test 7: Process Termination, Status Propagation & Clean Reaping
bool test_process_termination_status_reap() {
    std::cout << "[Test 7] Process Termination, Status Propagation & Clean Reaping...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t child_pid = bsd.create_process(1, "/bin/utility", 501);

    auto child = bsd.get_process(child_pid);
    TEST_ASSERT(child != nullptr && child->state == ProcessState::Running, "Child is Running");

    // Exit with code 42
    bsd.exit_process(child_pid, 42);
    TEST_ASSERT(child->state == ProcessState::Zombie, "Child transitions to Zombie on exit");
    TEST_ASSERT(child->exit_code == 42, "Exit code 42 stored");

    // Parent reaps child
    int status = 0;
    std::int32_t reaped_pid = bsd.waitpid(1, static_cast<std::int32_t>(child_pid), status, 0);
    TEST_ASSERT(reaped_pid == static_cast<std::int32_t>(child_pid), "waitpid reaps child");
    TEST_ASSERT(status == 42, "Exit status reaped accurately as 42");
    TEST_ASSERT(child->state == ProcessState::Terminated, "Child transitions to Terminated");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 169: Unified Guest Execution Conformance\n";
    std::cout << "=========================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](auto test_fn, const char* name) {
        if (test_fn()) {
            std::cout << "[PASS] " << name << "\n";
            passed++;
        } else {
            std::cout << "[FAIL] " << name << "\n";
            failed++;
        }
    };

    run_test(test_complete_boot_to_userspace_pipeline, "Complete Boot-to-Userspace Pipeline");
    run_test(test_artifact_admission_to_execution_gate, "Artifact Admission Gate Conformance");
    run_test(test_wx_enforcement_in_pipeline, "Userspace Memory & W^X Enforcement");
    run_test(test_instruction_step_retirement, "Guest Instruction Step & Retirement");
    run_test(test_bsd_syscall_dispatch_abi, "BSD Syscall Dispatch & Return Value ABI");
    run_test(test_mach_trap_dispatch, "Mach Trap Dispatch & Port Space Integration");
    run_test(test_process_termination_status_reap, "Process Termination, Status & Clean Reaping");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}

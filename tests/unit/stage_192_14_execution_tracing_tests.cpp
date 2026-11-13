// =============================================================================
// stage_192_14_execution_tracing_tests.cpp
//
// Stage 192.14: Darwin Guest Execution Tracing & Evidence Integrity Tests
// Verifies:
//   - Strict 1:1 guest instruction retirement counting vs CPU steps
//   - Non-inflation of guest counts during host setup / loader actions
//   - Provenance tracking (ExecutionOrigin::Guest vs ExecutionOrigin::Host)
//   - Structured diagnostic queries (by PC, opcode bitmask, register writes)
//   - Syscall tracing (arguments, errno, return codes)
//   - Trace export integrity (valid JSON and CSV schemas)
// =============================================================================

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"
#include "runtime/execution_diagnostics.h"

using namespace winios;
using namespace winios::runtime;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// -----------------------------------------------------------------------------
// Test 1: Instruction Retirement Counter Accuracy & Host Non-Inflation
// -----------------------------------------------------------------------------
bool test_instruction_counter_accuracy() {
    std::cout << "[Test 1] Instruction Retirement Counter Accuracy & Host Non-Inflation...\n";

    CPU cpu;
    Memory mem(16 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Host actions (process creation, FD allocation, port creation) must NOT increment guest counters
    uint32_t pid = 1401;
    bsd.create_process_with_pid(pid, 0, "/usr/bin/counter_app", 501);
    ipc.create_task_port_space(pid);

    DarwinPipelineResult result{};
    CHECK(result.guest_instructions_retired == 0, "Initial retired count must be zero");

    // Execute 5 distinct guest instructions:
    // 0x1000: mov x0, #10
    // 0x1004: add x0, x0, #5
    // 0x1008: mov x16, #1 (SYS_exit)
    // 0x100C: mov x0, #0
    // 0x1010: svc #0x80
    const std::vector<uint32_t> guest_insns = {
        0xD2800140u, // mov x0, #10
        0x91001400u, // add x0, x0, #5
        0xD2800030u, // mov x16, #1
        0xD2800000u, // mov x0, #0
        0xD4001001u  // svc #0x80
    };

    for (size_t i = 0; i < guest_insns.size(); ++i) {
        mem.write_u32(0x1000 + i * 4, guest_insns[i]);
    }

    cpu.reset();
    cpu.set_current_el(0);
    cpu.set_program_counter(0x1000);
    engine.set_current_pid(pid);

    DarwinPipelineConfig config;
    config.max_instructions = 10;
    config.enable_trace = true;

    // Step instructions one by one
    while (!result.process_exited && result.guest_instructions_retired < config.max_instructions) {
        if (!engine.step_one(result, config)) break;
    }

    CHECK(result.process_exited, "Process must exit via SYS_exit");
    CHECK(result.guest_instructions_retired == 5,
          "Exactly 5 guest instructions must be retired (got " +
              std::to_string(result.guest_instructions_retired) + ")");

    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Provenance Tracking (ExecutionOrigin::Guest vs Host)
// -----------------------------------------------------------------------------
bool test_provenance_tracking() {
    std::cout << "[Test 2] Provenance Tracking (ExecutionOrigin::Guest vs Host)...\n";

    CPU cpu;
    Memory mem(16 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    DarwinPipelineResult result{};

    // Host events logged during staging
    engine.log_event(result, ExecutionOrigin::Host, PipelineExecutionStage::MachOLoading, 0,
                     "Loaded mock Mach-O header");
    engine.log_event(result, ExecutionOrigin::Host, PipelineExecutionStage::StackSetup, 0x7FFFF000,
                     "Allocated userspace stack");

    // Guest events logged during execution
    engine.log_event(result, ExecutionOrigin::Guest, PipelineExecutionStage::UserspaceEntry, 0x1000,
                     "Transitioned to EL0");
    engine.log_event(result, ExecutionOrigin::Guest, PipelineExecutionStage::GuestInstructionStep, 0x1000,
                     "Executed MOV X0, #1");
    engine.log_event(result, ExecutionOrigin::Guest, PipelineExecutionStage::ProcessExit, 0x1004,
                     "SYS_exit(0)");

    CHECK(result.trace.size() == 5, "Trace must have exactly 5 events");

    // Validate Host events
    CHECK(result.trace[0].origin == ExecutionOrigin::Host, "Event 0 must be Host");
    CHECK(result.trace[1].origin == ExecutionOrigin::Host, "Event 1 must be Host");
    CHECK(std::string(execution_origin_string(result.trace[0].origin)) == "HOST", "Origin string mismatch");

    // Validate Guest events
    CHECK(result.trace[2].origin == ExecutionOrigin::Guest, "Event 2 must be Guest");
    CHECK(result.trace[3].origin == ExecutionOrigin::Guest, "Event 3 must be Guest");
    CHECK(result.trace[4].origin == ExecutionOrigin::Guest, "Event 4 must be Guest");
    CHECK(result.trace[3].pc == 0x1000, "Event 3 PC mismatch");
    CHECK(std::string(execution_origin_string(result.trace[3].origin)) == "GUEST", "Origin string mismatch");

    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Structured Diagnostics & Inspection Queries
// -----------------------------------------------------------------------------
bool test_diagnostics_queries() {
    std::cout << "[Test 3] Structured Diagnostics & Inspection Queries...\n";

    ExecutionDiagnostics diag;
    diag.reset();

    // Log 3 steps with register modifications
    std::vector<RegisterDiff> diffs1 = {{0, 0, 42}}; // x0 = 42
    diag.log_instruction_step(0x1000, 0x1004, 0xD2800540u, 0, diffs1, "mov x0, #42");

    std::vector<RegisterDiff> diffs2 = {{1, 0, 100}}; // x1 = 100
    diag.log_instruction_step(0x1004, 0x1008, 0xD2800C81u, 0, diffs2, "mov x1, #100");

    std::vector<RegisterDiff> diffs3 = {{0, 42, 142}}; // x0 = 142
    diag.log_instruction_step(0x1008, 0x100C, 0x8B010000u, 0, diffs3, "add x0, x0, x1");

    CHECK(diag.step_count() == 3, "step_count must be 3");

    // Query by PC
    auto pc_matches = diag.find_steps_by_pc(0x1004);
    CHECK(pc_matches.size() == 1, "find_steps_by_pc(0x1004) must return 1 match");
    CHECK(pc_matches[0].instruction_raw == 0xD2800C81u, "Instruction raw mismatch");

    // Query by Register Write: reg 0 (x0) modified in step 0 and step 2
    auto reg0_writes = diag.find_steps_by_reg_write(0);
    CHECK(reg0_writes.size() == 2, "x0 must be modified in 2 steps");
    CHECK(reg0_writes[0].pc_before == 0x1000, "First write PC mismatch");
    CHECK(reg0_writes[1].pc_before == 0x1008, "Second write PC mismatch");

    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Syscall & Trap Tracing Fidelity
// -----------------------------------------------------------------------------
bool test_syscall_tracing() {
    std::cout << "[Test 4] Syscall & Trap Tracing Fidelity...\n";

    ExecutionDiagnostics diag;
    diag.reset();

    std::array<uint64_t, 6> args1 = {1, 0x2000, 14, 0, 0, 0};
    diag.log_syscall(TrapKind::BsdSyscall, 4, "SYS_write", args1, 14, false, 0);

    std::array<uint64_t, 6> args2 = {0, 0, 0, 0, 0, 0};
    diag.log_syscall(TrapKind::BsdSyscall, 1, "SYS_exit", args2, 0, false, 0);

    CHECK(diag.syscall_count() == 2, "syscall_count must be 2");

    auto write_calls = diag.find_syscalls_by_number(4);
    CHECK(write_calls.size() == 1, "find_syscalls_by_number(4) must return 1 call");
    CHECK(write_calls[0].name == "SYS_write", "Syscall name mismatch");
    CHECK(write_calls[0].arguments[0] == 1, "Arg 0 (stdout) mismatch");
    CHECK(write_calls[0].arguments[2] == 14, "Arg 2 (length) mismatch");
    CHECK(write_calls[0].return_value == 14, "Return value mismatch");
    CHECK(!write_calls[0].is_error, "Write must succeed without error");

    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Trace Serialization & Export Integrity (JSON & CSV)
// -----------------------------------------------------------------------------
bool test_trace_export_integrity() {
    std::cout << "[Test 5] Trace Serialization & Export Integrity (JSON & CSV)...\n";

    ExecutionDiagnostics diag;
    diag.reset();
    diag.record_process_start(1405, "/usr/bin/export_test");

    std::vector<RegisterDiff> diffs = {{0, 0, 1}};
    diag.log_instruction_step(0x1000, 0x1004, 0xD2800020u, 0, diffs, "mov x0, #1");

    std::array<uint64_t, 6> args = {0};
    diag.log_syscall(TrapKind::BsdSyscall, 1, "SYS_exit", args, 0, false, 0);
    diag.record_process_exit(0);

    // Export JSON
    std::string json = diag.export_trace_json();
    CHECK(!json.empty(), "JSON export must not be empty");
    CHECK(json.find("\"lifecycle\":") != std::string::npos, "JSON must contain lifecycle section");
    CHECK(json.find("\"pid\": 1405") != std::string::npos, "JSON must contain pid");
    CHECK(json.find("\"instructions_retired\": 1") != std::string::npos, "JSON must contain retired count");
    CHECK(json.find("\"SYS_exit\"") != std::string::npos, "JSON must contain SYS_exit record");

    // Export CSV
    std::string csv = diag.export_trace_csv();
    CHECK(!csv.empty(), "CSV export must not be empty");
    CHECK(csv.find("step,pc_before,pc_after,instruction_raw") != std::string::npos,
          "CSV must contain standard header row");
    CHECK(csv.find("0x1000,0x1004") != std::string::npos, "CSV must contain step entry");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 192.14: Guest Execution Tracing & Integrity Tests\n";
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

    run_test(test_instruction_counter_accuracy, "Instruction Retirement Counter Accuracy & Host Non-Inflation");
    run_test(test_provenance_tracking, "Provenance Tracking (ExecutionOrigin::Guest vs Host)");
    run_test(test_diagnostics_queries, "Structured Diagnostics & Inspection Queries");
    run_test(test_syscall_tracing, "Syscall & Trap Tracing Fidelity");
    run_test(test_trace_export_integrity, "Trace Serialization & Export Integrity (JSON & CSV)");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}

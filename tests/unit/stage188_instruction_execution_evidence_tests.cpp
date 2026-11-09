// =============================================================================
// stage188_instruction_execution_evidence_tests.cpp
//
// Stage 188: Instruction Execution Evidence Test Suite
// Verifies extended trace records in ExecutionDiagnostics, instruction
// disassembly formatting across AArch64 ISA, register diff tracking,
// live userspace engine integration, memory operation accounting,
// query filtering, and structured JSON / CSV trace export.
// =============================================================================

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/macho_corpus_builder.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"
#include "runtime/execution_diagnostics.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (false)

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;

// -----------------------------------------------------------------------------
// Test 1: Instruction Disassembly Accuracy & Coverage
// -----------------------------------------------------------------------------
bool test_instruction_disassembly_accuracy() {
    std::cout << "[Test 1] Instruction Disassembly Accuracy & Coverage...\n";

    // 1. NOP
    auto dis_nop = ExecutionDiagnostics::disassemble_instruction(0xD503201Fu, 0x1000);
    TEST_ASSERT(dis_nop == "nop", "NOP mismatch: " + dis_nop);

    // 2. RET
    auto dis_ret = ExecutionDiagnostics::disassemble_instruction(0xD65F03C0u, 0x1004);
    TEST_ASSERT(dis_ret == "ret", "RET mismatch: " + dis_ret);

    // 3. SVC #0x80
    auto dis_svc = ExecutionDiagnostics::disassemble_instruction(0xD4001001u, 0x1008);
    TEST_ASSERT(dis_svc == "svc #0x80", "SVC mismatch: " + dis_svc);

    // 4. MOVZ x0, #0x42
    auto dis_movz = ExecutionDiagnostics::disassemble_instruction(0xD2800840u, 0x100C);
    TEST_ASSERT(dis_movz.find("movz x0, #0x42") != std::string::npos, "MOVZ mismatch: " + dis_movz);

    // 5. MOVN x1, #0x0
    auto dis_movn = ExecutionDiagnostics::disassemble_instruction(0x92800001u, 0x1010);
    TEST_ASSERT(dis_movn.find("movn x1, #0x0") != std::string::npos, "MOVN mismatch: " + dis_movn);

    // 6. ADD x0, x1, x2
    auto dis_add = ExecutionDiagnostics::disassemble_instruction(0x8B020020u, 0x1014);
    TEST_ASSERT(dis_add == "add x0, x1, x2", "ADD mismatch: " + dis_add);

    // 7. SUB x3, x4, x5
    auto dis_sub = ExecutionDiagnostics::disassemble_instruction(0xCB050083u, 0x1018);
    TEST_ASSERT(dis_sub == "sub x3, x4, x5", "SUB mismatch: " + dis_sub);

    // 8. CMP x16, #0x0 (SUBS XZR, X16, #0)
    auto dis_cmp = ExecutionDiagnostics::disassemble_instruction(0xF100021Fu, 0x101C);
    TEST_ASSERT(dis_cmp == "cmp x16, #0x0", "CMP mismatch: " + dis_cmp);

    // 9. BLR x16
    auto dis_blr = ExecutionDiagnostics::disassemble_instruction(0xD63F0200u, 0x1020);
    TEST_ASSERT(dis_blr == "blr x16", "BLR mismatch: " + dis_blr);

    // 10. B.EQ
    auto dis_beq = ExecutionDiagnostics::disassemble_instruction(0x54000040u, 0x1000);
    TEST_ASSERT(dis_beq == "b.eq 0x1008", "B.EQ mismatch: " + dis_beq);

    std::cout << "  Passed (Validated accurate disassembly across core AArch64 instructions)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Extended Step Logging with SP, NZCV and Register Diffs
// -----------------------------------------------------------------------------
bool test_extended_step_logging() {
    std::cout << "[Test 2] Extended Step Logging with SP, NZCV and Register Diffs...\n";

    ExecutionDiagnostics diag;
    diag.reset();

    std::vector<RegisterDiff> diffs = {
        {0, 0, 42},
        {16, 0, 1}
    };

    diag.log_instruction_step(0x1000, 0x1004, 0xD2800540u, 0, diffs, "movz x0, #42", 0x80000, 0x60000000);

    TEST_ASSERT(diag.step_count() == 1, "Step count must be 1");
    const auto* s = diag.get_step(0);
    TEST_ASSERT(s != nullptr, "get_step(0) must return valid pointer");
    TEST_ASSERT(s->pc_before == 0x1000, "pc_before mismatch");
    TEST_ASSERT(s->pc_after == 0x1004, "pc_after mismatch");
    TEST_ASSERT(s->sp_value == 0x80000, "sp_value mismatch");
    TEST_ASSERT(s->nzcv == 0x60000000, "nzcv mismatch");
    TEST_ASSERT(s->register_diffs.size() == 2, "register_diffs count mismatch");
    TEST_ASSERT(s->register_diffs[0].reg_index == 0 && s->register_diffs[0].new_value == 42, "Diff 0 mismatch");
    TEST_ASSERT(s->register_diffs[1].reg_index == 16 && s->register_diffs[1].new_value == 1, "Diff 1 mismatch");

    std::cout << "  Passed (Successfully recorded and inspected rich execution step record)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Live Darwin Userspace Engine Diagnostics Integration
// -----------------------------------------------------------------------------
bool test_live_userspace_engine_diagnostics_integration() {
    std::cout << "[Test 3] Live Darwin Userspace Engine Diagnostics Integration...\n";

    Memory mem(0x200000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    ExecutionDiagnostics diag;
    engine.set_diagnostics(&diag);

    auto hello_bin = MachOCorpusRegistry::create_hello_world();

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/usr/bin/hello";
    cfg.argv = {"/usr/bin/hello"};
    cfg.max_instructions = 50;

    auto res = engine.execute_macho(hello_bin.binary_data.data(), hello_bin.binary_data.size(), cfg, 701);

    TEST_ASSERT(res.is_clean_exit(), "Process must exit cleanly");
    TEST_ASSERT(diag.step_count() > 0, "Diagnostics must capture instructions executed");
    TEST_ASSERT(diag.step_count() == res.guest_instructions_retired,
                "Diagnostics step count must match retired instructions count");
    TEST_ASSERT(diag.syscall_count() >= 2, "Must log SYS_write and SYS_exit");
    TEST_ASSERT(diag.lifecycle().exited, "Lifecycle record must indicate clean exit");
    TEST_ASSERT(diag.lifecycle().exit_code == 0, "Lifecycle exit code must be 0");
    TEST_ASSERT(diag.lifecycle().executable_path == "/usr/bin/hello", "Executable path mismatch");

    std::cout << "  Passed (Captured " << diag.step_count() << " steps and "
              << diag.syscall_count() << " syscalls live from userspace engine)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Memory Operations & Query Filtering
// -----------------------------------------------------------------------------
bool test_memory_operations_and_query_filtering() {
    std::cout << "[Test 4] Memory Operations & Query Filtering...\n";

    ExecutionDiagnostics diag;
    diag.reset();

    // Log simulated instructions
    diag.log_instruction_step(0x1000, 0x1004, 0xD2800540u, 0, {{0, 0, 42}}, "movz x0, #42");
    diag.log_instruction_step(0x1004, 0x1008, 0xD2800221u, 0, {{1, 0, 17}}, "movz x1, #17");
    diag.log_instruction_step(0x1008, 0x100C, 0x8B010000u, 0, {{0, 42, 59}}, "add x0, x0, x1");

    // Log memory ops
    diag.log_memory_op(MemoryOpType::Read, 0x2000, 8, 0x12345678ULL);
    diag.log_memory_op(MemoryOpType::Write, 0x3000, 4, 0xAA55u);

    // Log syscall
    diag.log_syscall(TrapKind::BsdSyscall, 1, "SYS_exit", {59, 0, 0, 0, 0, 0}, 0);

    TEST_ASSERT(diag.memory_op_count() == 2, "Memory ops count mismatch");

    // Query 1: Find step by PC
    auto pc_steps = diag.find_steps_by_pc(0x1004);
    TEST_ASSERT(pc_steps.size() == 1, "Expected 1 step at PC 0x1004");
    TEST_ASSERT(pc_steps[0].instruction_raw == 0xD2800221u, "Instruction raw mismatch");

    // Query 2: Find steps modifying register X0
    auto x0_writers = diag.find_steps_by_reg_write(0);
    TEST_ASSERT(x0_writers.size() == 2, "Expected 2 steps modifying X0");

    // Query 3: Find syscalls by number
    auto exit_scs = diag.find_syscalls_by_number(1);
    TEST_ASSERT(exit_scs.size() == 1, "Expected 1 SYS_exit record");
    TEST_ASSERT(exit_scs[0].name == "SYS_exit", "Syscall name mismatch");

    std::cout << "  Passed (Verified memory tracking and query filter operations)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Full JSON Serialization and Format Validation
// -----------------------------------------------------------------------------
bool test_full_json_serialization() {
    std::cout << "[Test 5] Full JSON Serialization and Format Validation...\n";

    ExecutionDiagnostics diag;
    diag.reset();
    diag.record_process_start(705, "/usr/bin/math_app");
    diag.log_instruction_step(0x1000, 0x1004, 0xD2800320u, 0, {{0, 0, 25}}, "movz x0, #25");
    diag.log_syscall(TrapKind::BsdSyscall, 1, "SYS_exit", {25, 0, 0, 0, 0, 0}, 0);
    diag.record_process_exit(25);

    std::string json = diag.export_trace_json();
    TEST_ASSERT(!json.empty(), "JSON output must not be empty");
    TEST_ASSERT(json.find("\"lifecycle\"") != std::string::npos, "Missing lifecycle in JSON");
    TEST_ASSERT(json.find("\"pid\": 705") != std::string::npos, "Missing pid in JSON");
    TEST_ASSERT(json.find("\"executable_path\": \"/usr/bin/math_app\"") != std::string::npos, "Missing path in JSON");
    TEST_ASSERT(json.find("\"instructions\"") != std::string::npos, "Missing instructions in JSON");
    TEST_ASSERT(json.find("\"movz x0, #25\"") != std::string::npos, "Missing disassembly in JSON");
    TEST_ASSERT(json.find("\"syscalls\"") != std::string::npos, "Missing syscalls in JSON");
    TEST_ASSERT(json.find("\"SYS_exit\"") != std::string::npos, "Missing SYS_exit in JSON");

    std::cout << "  Passed (Generated structured JSON trace export of " << json.size() << " bytes)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Full CSV Trace and Syscall Export
// -----------------------------------------------------------------------------
bool test_full_csv_trace_export() {
    std::cout << "[Test 6] Full CSV Trace and Syscall Export...\n";

    ExecutionDiagnostics diag;
    diag.reset();
    diag.record_process_start(706, "/usr/bin/csv_test");
    diag.log_instruction_step(0x1000, 0x1004, 0xD2800540u, 0, {{0, 0, 42}}, "movz x0, #42", 0x80000, 0);
    diag.log_instruction_step(0x1004, 0x1008, 0xD2800030u, 0, {{16, 0, 1}}, "movz x16, #1", 0x80000, 0);
    diag.log_syscall(TrapKind::BsdSyscall, 1, "SYS_exit", {42, 0, 0, 0, 0, 0}, 0);

    std::string trace_csv = diag.export_trace_csv();
    TEST_ASSERT(!trace_csv.empty(), "Trace CSV must not be empty");
    TEST_ASSERT(trace_csv.find("step,pc_before,pc_after,instruction_raw") != std::string::npos, "Trace CSV header mismatch");
    TEST_ASSERT(trace_csv.find("0x1000") != std::string::npos, "Trace CSV missing step data");

    std::string syscall_csv = diag.export_syscalls_csv();
    TEST_ASSERT(!syscall_csv.empty(), "Syscall CSV must not be empty");
    TEST_ASSERT(syscall_csv.find("kind,number,name,arg0") != std::string::npos, "Syscall CSV header mismatch");
    TEST_ASSERT(syscall_csv.find("SYS_exit") != std::string::npos, "Syscall CSV missing record");

    std::cout << "  Passed (Generated formatted CSV trace and syscall logs)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Comprehensive Diagnostic Audit of Multi-Step Compute Pipeline
// -----------------------------------------------------------------------------
bool test_comprehensive_diagnostic_audit() {
    std::cout << "[Test 7] Comprehensive Diagnostic Audit of Multi-Step Compute Pipeline...\n";

    Memory mem(0x200000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    ExecutionDiagnostics diag;
    engine.set_diagnostics(&diag);

    auto math_bin = MachOCorpusRegistry::create_math_compute();

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/usr/bin/math_compute";
    cfg.argv = {"/usr/bin/math_compute"};
    cfg.max_instructions = 1000;

    auto res = engine.execute_macho(math_bin.binary_data.data(), math_bin.binary_data.size(), cfg, 707);

    TEST_ASSERT(res.is_clean_exit(), "Computation pipeline must exit cleanly");
    TEST_ASSERT(diag.step_count() > 0, "Instructions must be traced");

    // Format summary and trace report
    std::string summary = diag.format_summary();
    TEST_ASSERT(!summary.empty() && summary.find("=== WinIOS Execution Diagnostics Summary ===") != std::string::npos,
                "Summary report format error");

    std::string report = diag.format_trace_report(10);
    TEST_ASSERT(!report.empty() && report.find("--- Instruction Trace") != std::string::npos,
                "Trace report format error");

    std::cout << "  Passed (Complete diagnostic audit verified: "
              << diag.step_count() << " steps, summary and reports fully validated)\n";
    return true;
}

int main() {
    std::cout << "================================================================\n";
    std::cout << "  STAGE 188: INSTRUCTION EXECUTION EVIDENCE TEST SUITE\n";
    std::cout << "================================================================\n";

    int passed = 0;
    int failed = 0;

    if (test_instruction_disassembly_accuracy()) passed++; else failed++;
    if (test_extended_step_logging()) passed++; else failed++;
    if (test_live_userspace_engine_diagnostics_integration()) passed++; else failed++;
    if (test_memory_operations_and_query_filtering()) passed++; else failed++;
    if (test_full_json_serialization()) passed++; else failed++;
    if (test_full_csv_trace_export()) passed++; else failed++;
    if (test_comprehensive_diagnostic_audit()) passed++; else failed++;

    std::cout << "----------------------------------------------------------------\n";
    std::cout << "Stage 188 Summary: " << passed << " passed, " << failed << " failed\n";
    std::cout << "================================================================\n";

    return (failed == 0) ? 0 : 1;
}

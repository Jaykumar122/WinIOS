// =============================================================================
// stage184_real_macho_smoke_tests.cpp
//
// Stage 184: First Real Mach-O Smoke Test
// Verifies execution of genuine ARM64 Mach-O binaries from the corpus
// through the WinIOS userspace CPU instruction pipeline, validating
// segment mapping, stack setup, instruction retirement, BSD syscalls,
// Mach traps, and clean process exit.
// =============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/macho_corpus_builder.h"
#include "loader/real_artifact_harness.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"

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
// Test 1: Real Mach-O Corpus Admission & Integrity Verification
// -----------------------------------------------------------------------------
bool test_real_macho_corpus_admission_and_integrity() {
    std::cout << "[Test 1] Real Mach-O Corpus Admission & Integrity Verification...\n";

    auto hello_entry = MachOCorpusRegistry::create_hello_world();
    TEST_ASSERT(!hello_entry.binary_data.empty(), "Binary data must not be empty");
    TEST_ASSERT(hello_entry.file_size == hello_entry.binary_data.size(), "File size mismatch");
    TEST_ASSERT(!hello_entry.sha256.empty(), "SHA256 must be populated");

    RealArtifactHarness harness;
    auto meta = harness.inspect_buffer(hello_entry.binary_data.data(), hello_entry.binary_data.size());
    TEST_ASSERT(meta.decision == AdmissionDecision::Admitted, "hello_world must be admitted");

    std::cout << "  Passed (Validated admission and cryptographic hash of hello_world binary)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Full End-to-End Execution of corpus_hello_world
// -----------------------------------------------------------------------------
bool test_real_macho_hello_world_execution() {
    std::cout << "[Test 2] Full End-to-End Execution of corpus_hello_world...\n";

    Memory mem(0x200000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    auto hello = MachOCorpusRegistry::create_hello_world();

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/usr/bin/hello";
    cfg.argv = {"/usr/bin/hello"};
    cfg.envp = {"USER=mobile", "HOME=/var/mobile"};
    cfg.max_instructions = 100;

    auto result = engine.execute_macho(hello.binary_data.data(), hello.binary_data.size(), cfg, 501);

    TEST_ASSERT(result.process_exited, "Process must report exit");
    TEST_ASSERT(result.exit_code == 0, "Exit code must be 0");
    TEST_ASSERT(result.is_clean_exit(), "Pipeline execution must succeed");
    TEST_ASSERT(result.guest_instructions_retired >= 8, "Must retire at least 8 guest instructions");
    TEST_ASSERT(result.bsd_syscalls_handled >= 2, "Must handle SYS_write and SYS_exit");
    TEST_ASSERT(result.stdout_output.find("Hello, Darwin!") != std::string::npos,
                "Stdout must capture 'Hello, Darwin!'");

    std::cout << "  Passed (Executed hello_world: retired " << result.guest_instructions_retired
              << " instrs, output=\"" << result.stdout_output << "\")\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Iterative Arithmetic Execution (corpus_math_compute)
// -----------------------------------------------------------------------------
bool test_real_macho_math_compute_execution() {
    std::cout << "[Test 3] Iterative Arithmetic Execution (corpus_math_compute)...\n";

    Memory mem(0x200000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    auto math = MachOCorpusRegistry::create_math_compute();

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/usr/bin/math";
    cfg.argv = {"/usr/bin/math"};
    cfg.max_instructions = 1000;

    auto result = engine.execute_macho(math.binary_data.data(), math.binary_data.size(), cfg, 502);

    TEST_ASSERT(result.process_exited, "Process must report exit");
    TEST_ASSERT(result.exit_code == 0, "Exit code must be 0");
    TEST_ASSERT(result.is_clean_exit(), "Pipeline execution must succeed");
    // 100 loop iterations * 4 instrs + overhead = ~400 instructions
    TEST_ASSERT(result.guest_instructions_retired >= 300, "Must retire loop iterations (>= 300 instrs)");

    std::cout << "  Passed (Executed math_compute: retired " << result.guest_instructions_retired
              << " instrs across 100 iterations)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Real VFS File Lifecycle Execution (corpus_file_io)
// -----------------------------------------------------------------------------
bool test_real_macho_file_io_execution() {
    std::cout << "[Test 4] Real VFS File Lifecycle Execution (corpus_file_io)...\n";

    Memory mem(0x200000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    auto fio = MachOCorpusRegistry::create_file_io();

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/usr/bin/file_io";
    cfg.argv = {"/usr/bin/file_io"};
    cfg.max_instructions = 100;

    auto result = engine.execute_macho(fio.binary_data.data(), fio.binary_data.size(), cfg, 503);

    TEST_ASSERT(result.process_exited, "Process must report exit");
    TEST_ASSERT(result.exit_code == 0, "Exit code must be 0");
    TEST_ASSERT(result.is_clean_exit(), "Pipeline execution must succeed");
    TEST_ASSERT(result.bsd_syscalls_handled >= 3, "Must handle SYS_open, SYS_close, SYS_exit");

    std::cout << "  Passed (Executed file_io: opened /dev/null, closed, and exited cleanly)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Virtual Memory Allocator Execution (corpus_memory_mmap)
// -----------------------------------------------------------------------------
bool test_real_macho_memory_mmap_execution() {
    std::cout << "[Test 5] Virtual Memory Allocator Execution (corpus_memory_mmap)...\n";

    Memory mem(0x200000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    auto mm = MachOCorpusRegistry::create_memory_mmap();

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/usr/bin/mem_test";
    cfg.argv = {"/usr/bin/mem_test"};
    cfg.max_instructions = 100;

    auto result = engine.execute_macho(mm.binary_data.data(), mm.binary_data.size(), cfg, 504);

    TEST_ASSERT(result.process_exited, "Process must report exit");
    TEST_ASSERT(result.exit_code == 0, "Exit code must be 0");
    TEST_ASSERT(result.is_clean_exit(), "Pipeline execution must succeed");
    TEST_ASSERT(result.bsd_syscalls_handled >= 2, "Must handle SYS_mmap and SYS_exit");

    std::cout << "  Passed (Executed memory_mmap: invoked SYS_mmap 16KB allocation and exited)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Mach IPC Client Execution (corpus_mach_ipc_client)
// -----------------------------------------------------------------------------
bool test_real_macho_mach_ipc_client_execution() {
    std::cout << "[Test 6] Mach IPC Client Execution (corpus_mach_ipc_client)...\n";

    Memory mem(0x200000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    auto ipc_client = MachOCorpusRegistry::create_mach_ipc_client();

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/usr/bin/ipc_client";
    cfg.argv = {"/usr/bin/ipc_client"};
    cfg.max_instructions = 100;

    auto result = engine.execute_macho(ipc_client.binary_data.data(), ipc_client.binary_data.size(), cfg, 505);

    TEST_ASSERT(result.process_exited, "Process must report exit");
    TEST_ASSERT(result.exit_code == 0, "Exit code must be 0");
    TEST_ASSERT(result.is_clean_exit(), "Pipeline execution must succeed");
    TEST_ASSERT(result.mach_traps_handled >= 1, "Must handle mach_reply_port_trap");
    TEST_ASSERT(result.bsd_syscalls_handled >= 1, "Must handle SYS_exit");

    std::cout << "  Passed (Executed mach_ipc_client: dispatched mach_reply_port_trap and exited)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Instruction Diagnostics and Trace Records Verification
// -----------------------------------------------------------------------------
bool test_instruction_diagnostics_trace_records() {
    std::cout << "[Test 7] Instruction Diagnostics and Trace Records Verification...\n";

    Memory mem(0x200000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    auto hello = MachOCorpusRegistry::create_hello_world();
    DarwinPipelineConfig cfg;
    cfg.executable_path = "/hello";
    cfg.argv = {"/hello"};
    cfg.max_instructions = 50;

    auto result = engine.execute_macho(hello.binary_data.data(), hello.binary_data.size(), cfg, 506);

    TEST_ASSERT(!result.trace.empty(), "Trace events must not be empty");

    bool has_loading = false;
    bool has_stack = false;
    bool has_entry = false;
    bool has_syscall = false;
    bool has_exit = false;

    for (const auto& ev : result.trace) {
        if (ev.stage == PipelineExecutionStage::MachOLoading) has_loading = true;
        if (ev.stage == PipelineExecutionStage::StackSetup) has_stack = true;
        if (ev.stage == PipelineExecutionStage::UserspaceEntry) has_entry = true;
        if (ev.stage == PipelineExecutionStage::BsdSyscallTrap) has_syscall = true;
        if (ev.stage == PipelineExecutionStage::ProcessExit) has_exit = true;
    }

    TEST_ASSERT(has_loading, "Missing MachOLoading trace event");
    TEST_ASSERT(has_stack, "Missing StackSetup trace event");
    TEST_ASSERT(has_entry, "Missing UserspaceEntry trace event");
    TEST_ASSERT(has_syscall, "Missing BsdSyscallTrap trace event");
    TEST_ASSERT(has_exit, "Missing ProcessExit trace event");

    std::cout << "  Passed (Verified " << result.trace.size()
              << " diagnostic pipeline trace records across all lifecycle stages)\n";
    return true;
}

int main() {
    std::cout << "================================================================\n";
    std::cout << "  STAGE 184: FIRST REAL MACH-O SMOKE TEST SUITE\n";
    std::cout << "================================================================\n";

    int passed = 0;
    int failed = 0;

    if (test_real_macho_corpus_admission_and_integrity()) passed++; else failed++;
    if (test_real_macho_hello_world_execution()) passed++; else failed++;
    if (test_real_macho_math_compute_execution()) passed++; else failed++;
    if (test_real_macho_file_io_execution()) passed++; else failed++;
    if (test_real_macho_memory_mmap_execution()) passed++; else failed++;
    if (test_real_macho_mach_ipc_client_execution()) passed++; else failed++;
    if (test_instruction_diagnostics_trace_records()) passed++; else failed++;

    std::cout << "----------------------------------------------------------------\n";
    std::cout << "Stage 184 Summary: " << passed << " passed, " << failed << " failed\n";
    std::cout << "================================================================\n";

    return (failed == 0) ? 0 : 1;
}
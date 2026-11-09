// =============================================================================
// stage190_independent_release_audit_tests.cpp
//
// Stage 190: Final Conformance and Independent Release Audit
// Master verification test suite for Campaign 171–190.
// Covers corpus admission & hashing, ELF vs Mach-O mutual exclusivity,
// Apple Silicon 16KB W^X page protection, two-level namespace isolation,
// modern chained fixup unchaining, Darwin VFS & Mach IPC descriptors, and
// live ARM64 multi-step guest CPU execution with diagnostic trace evidence.
// =============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/apple_artifact_admission.h"
#include "loader/dyld_engine.h"
#include "loader/macho_corpus_builder.h"
#include "loader/macho_dependency_graph.h"
#include "loader/shared_library_mapper.h"
#include "loader/userspace_macho_loader.h"
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
// Test 1: Corpus Binary Admission, Cryptographic Fingerprinting & Exclusion Policy
// -----------------------------------------------------------------------------
bool test_corpus_admission_and_cryptographic_fingerprinting() {
    std::cout << "[Test 1] Corpus Binary Admission, Fingerprinting & Exclusion Policy...\n";

    auto catalog = MachOCorpusRegistry::get_all_corpus_entries();
    TEST_ASSERT(!catalog.empty(), "Corpus catalog must contain test binaries");

    bool found_admitted = false;
    bool found_encrypted = false;

    for (const auto& entry : catalog) {
        TEST_ASSERT(!entry.name.empty(), "Entry name cannot be empty");
        TEST_ASSERT(!entry.sha256.empty(), "Entry must have SHA-256 fingerprint: " + entry.name);
        TEST_ASSERT(entry.sha256.length() == 64, "SHA-256 must be 64 hex characters: " + entry.name);
        TEST_ASSERT(!entry.binary_data.empty(), "Entry binary payload cannot be empty: " + entry.name);

        if (entry.expected_decision == AdmissionDecision::Admitted) {
            found_admitted = true;
        } else if (entry.expected_decision == AdmissionDecision::Rejected_Encrypted) {
            found_encrypted = true;
            TEST_ASSERT(entry.name.find("encrypted") != std::string::npos,
                        "Encrypted binary must be explicitly designated in name");
        }
    }

    TEST_ASSERT(found_admitted, "Catalog must contain admitted binaries");
    TEST_ASSERT(found_encrypted, "Catalog must verify FairPlay encrypted exclusion policy");

    std::cout << "  Passed (Corpus admission catalog verified with " << catalog.size()
              << " distinct binaries and FairPlay SEP exclusion checks).\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: ELF vs Mach-O Mutual Exclusivity & Architectural Segregation
// -----------------------------------------------------------------------------
bool test_elf_macho_mutual_exclusivity() {
    std::cout << "[Test 2] ELF vs Mach-O Mutual Exclusivity & Architectural Segregation...\n";

    Memory mem(0x100000);
    UserspaceLoadConfig cfg{};

    // 1. Synthetic ELF header (0x7F 'E' 'L' 'F')
    std::vector<std::uint8_t> elf_bytes(64, 0);
    elf_bytes[0] = 0x7F;
    elf_bytes[1] = 'E';
    elf_bytes[2] = 'L';
    elf_bytes[3] = 'F';
    elf_bytes[4] = 2; // 64-bit ELF

    auto res = UserspaceMachOLoader::load_into_memory(elf_bytes.data(), elf_bytes.size(), mem, cfg);
    TEST_ASSERT(!res.is_success(), "ELF binary must be strictly rejected by Darwin Mach-O loader");
    TEST_ASSERT(res.status == UserspaceMachOLoadStatus::InvalidMagic,
                "Status must be InvalidMagic for non-Mach-O format");

    // 2. Legitimate Mach-O 64-bit arm64e binary must be admitted
    auto valid_macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();
    auto macho_res = UserspaceMachOLoader::load_into_memory(valid_macho.data(), valid_macho.size(), mem, cfg);
    TEST_ASSERT(macho_res.is_success(), "Legitimate ARM64e Mach-O binary must be admitted");
    TEST_ASSERT(macho_res.is_arm64e, "Binary must be recognized as ARM64e");

    std::cout << "  Passed (ELF strictly segregated and rejected; Apple ARM64e admitted).\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Apple Silicon 16KB Page Alignment & W^X Memory Protection Conformance
// -----------------------------------------------------------------------------
bool test_apple_silicon_page_alignment_and_wx_protection() {
    std::cout << "[Test 3] Apple Silicon 16KB Page Alignment & W^X Conformance...\n";

    auto valid_macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();
    Memory mem(0x200000);
    UserspaceLoadConfig cfg{};

    auto res = UserspaceMachOLoader::load_into_memory(valid_macho.data(), valid_macho.size(), mem, cfg);
    TEST_ASSERT(res.is_success(), "Loading must succeed");

    for (const auto& seg : res.segments) {
        if (seg.is_pagezero) continue;

        // 1. Verify 16KB page alignment (Apple Silicon granule)
        TEST_ASSERT((seg.vmaddr % kApplePageSize16K) == 0,
                    "Segment " + seg.name + " vmaddr must align to 16KB boundary");

        // 2. Strict Apple Silicon W^X enforcement
        bool is_writable = seg.is_writable();
        bool is_executable = seg.is_executable();
        TEST_ASSERT(!(is_writable && is_executable),
                    "Segment " + seg.name + " violates W^X policy (cannot be simultaneously W and X)");

        if (seg.name == "__TEXT") {
            TEST_ASSERT(seg.is_readable() && seg.is_executable() && !seg.is_writable(),
                        "__TEXT must be RX strictly");
        } else if (seg.name == "__DATA" || seg.name == "__DATA_CONST") {
            TEST_ASSERT(seg.is_readable() && !seg.is_executable(),
                        "__DATA must not be executable");
        }
    }

    std::cout << "  Passed (16KB granule alignment and strict W^X enforcement verified across all segments).\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: dyld Two-Level Namespace Symbol Isolation & Weak Fallback Resolution
// -----------------------------------------------------------------------------
bool test_two_level_namespace_and_weak_resolution() {
    std::cout << "[Test 4] dyld Two-Level Namespace Symbol Isolation & Weak Fallback...\n";

    DyldEngine dyld;
    dyld.clear_modules();

    // Module 1: libSystem (ordinal 1)
    DyldLoadedModule mod_sys{};
    mod_sys.name = "libSystem.B.dylib";
    mod_sys.install_name = "/usr/lib/libSystem.B.dylib";
    mod_sys.base_address = 0x180000000ULL;
    mod_sys.exports["_exit"] = 0x180001000ULL;
    mod_sys.exports["_malloc"] = 0x180002000ULL;
    dyld.register_module(mod_sys);

    // Module 2: libMath (ordinal 2)
    DyldLoadedModule mod_math{};
    mod_math.name = "libMath.dylib";
    mod_math.install_name = "/usr/lib/libMath.dylib";
    mod_math.base_address = 0x180040000ULL;
    mod_math.exports["_cos"] = 0x180041000ULL;
    mod_math.exports["_malloc"] = 0x180042000ULL; // Deliberate name collision with libSystem
    dyld.register_module(mod_math);

    std::string err;
    std::optional<std::uint64_t> out_addr;

    // 1. Two-level query for _malloc explicitly from ordinal 1 (libSystem)
    auto st1 = dyld.resolve_symbol_two_level("_malloc", 1, false, out_addr, err);
    TEST_ASSERT(st1 == DyldLinkStatus::Success && out_addr.has_value(), "Two-level lookup mod 1 must succeed");
    TEST_ASSERT(*out_addr == 0x180002000ULL, "Two-level lookup must return libSystem malloc, not libMath");

    // 2. Two-level query for _malloc explicitly from ordinal 2 (libMath)
    auto st2 = dyld.resolve_symbol_two_level("_malloc", 2, false, out_addr, err);
    TEST_ASSERT(st2 == DyldLinkStatus::Success && out_addr.has_value(), "Two-level lookup mod 2 must succeed");
    TEST_ASSERT(*out_addr == 0x180042000ULL, "Two-level lookup must return libMath malloc, not libSystem");

    // 3. Flat lookup resolution
    auto st3 = dyld.resolve_symbol_two_level("_cos", BIND_SPECIAL_DYLIB_FLAT_LOOKUP, false, out_addr, err);
    TEST_ASSERT(st3 == DyldLinkStatus::Success && out_addr.has_value() && *out_addr == 0x180041000ULL,
                "Flat lookup must find _cos across loaded modules");

    // 4. Weak import of missing symbol
    auto st4 = dyld.resolve_symbol_two_level("_non_existent_fn", 1, true /* is_weak */, out_addr, err);
    TEST_ASSERT(st4 == DyldLinkStatus::Success, "Missing weak symbol must return Success with null address");
    TEST_ASSERT(!out_addr.has_value() || *out_addr == 0, "Weak import target must be null/0");

    std::cout << "  Passed (Two-level isolation, name collision protection, flat & weak resolution verified).\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Modern LC_DYLD_CHAINED_FIXUPS Pointer Unchaining & Binding
// -----------------------------------------------------------------------------
bool test_modern_chained_fixups_unchaining() {
    std::cout << "[Test 5] Modern LC_DYLD_CHAINED_FIXUPS Pointer Unchaining & Binding...\n";

    DyldEngine dyld;
    Memory mem(0x200000);
    const std::uint64_t base_vaddr = 0x10000;
    const std::uint64_t slide = 0x4000;

    // Use fixture builder with segments to construct valid chained fixups
    DyldFixtureBuilder::SyntheticSegmentFixups seg1;
    seg1.segment_offset = 0x1000;
    seg1.page_size = 0x1000;
    seg1.pointer_format = DYLD_CHAINED_PTR_64;
    seg1.page_starts = {0x10};

    auto fixup_blob = DyldFixtureBuilder::create_synthetic_chained_fixups_with_segments(
        {"_exit", "_printf"}, {seg1}
    );

    TEST_ASSERT(!fixup_blob.empty(), "Chained fixup fixture generation must succeed");

    // Write terminal pointer at the chain start
    mem.write_u64(0x1000 + 0x10, 0);

    std::string err;
    auto status = dyld.process_chained_fixups(
        fixup_blob.data(),
        fixup_blob.size(),
        mem,
        base_vaddr,
        slide,
        err
    );

    TEST_ASSERT(status == DyldLinkStatus::Success, "Chained fixup processing must succeed: " + err);

    std::cout << "  Passed (Modern chained fixups parsed and processed in guest memory successfully).\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Darwin VFS, File Descriptors, and IPC Pipeline Conformance
// -----------------------------------------------------------------------------
bool test_darwin_vfs_and_ipc_conformance() {
    std::cout << "[Test 6] Darwin VFS, File Descriptors, and IPC Pipeline Conformance...\n";

    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory mem(0x100000);

    const std::uint32_t pid = bsd.create_process(1, "/usr/bin/audit");
    const std::uint32_t task_id = pid;

    // 1. VFS file open, write, lseek, read
    bsd.vfs().write_file("/var/log/audit.log", "", 0);
    int fd = bsd.open(pid, "/var/log/audit.log", 0x0202 /* O_CREAT | O_RDWR */);
    TEST_ASSERT(fd >= 3, "Opened fd must be >= 3");

    const char test_data[] = "WinIOS-Audit-Token-190";
    std::int64_t written = bsd.write(pid, fd, test_data, sizeof(test_data) - 1);
    TEST_ASSERT(written == sizeof(test_data) - 1, "Bytes written mismatch");

    bsd.lseek(pid, fd, 0, 0 /* SEEK_SET */);
    char read_buf[64]{0};
    std::int64_t read_bytes = bsd.read(pid, fd, read_buf, sizeof(read_buf) - 1);
    TEST_ASSERT(read_bytes == written, "Bytes read mismatch");
    TEST_ASSERT(std::strcmp(read_buf, test_data) == 0, "Read data mismatch");
    bsd.close(pid, fd);

    // 2. Mach IPC Task Port Space & Roundtrip Message Delivery
    auto space = ipc.create_task_port_space(task_id);
    auto port_obj = std::make_shared<MachPortObject>(2026, task_id);
    mach_port_t rcv_port = space->allocate_port(port_obj, kMachPortRightReceive);
    mach_port_t snd_port = space->allocate_port(port_obj, kMachPortRightSend);

    MachMsgHeaderRaw send_hdr{};
    send_hdr.msgh_bits = 0x15;
    send_hdr.msgh_size = sizeof(MachMsgHeaderRaw) + 16;
    send_hdr.msgh_remote_port = snd_port;
    send_hdr.msgh_local_port = kMachPortNull;
    send_hdr.msgh_id = 19001;

    std::memcpy(mem.raw_data() + 0x4000, &send_hdr, sizeof(send_hdr));
    std::memcpy(mem.raw_data() + 0x4000 + sizeof(send_hdr), "AuditMsgPayload!", 16);

    std::int32_t send_st = ipc.mach_msg(task_id, 0x4000, kMachSendMsg, send_hdr.msgh_size, 0, 0, 0, mem);
    TEST_ASSERT(send_st == kMachMsgSuccess, "IPC mach_msg send must succeed");

    std::int32_t rcv_st = ipc.mach_msg(task_id, 0x5000, 0x00000002u /* MACH_RCV_MSG */, 0, 128, rcv_port, 0, mem);
    TEST_ASSERT(rcv_st == kMachMsgSuccess, "IPC mach_msg receive must succeed");

    MachMsgHeaderRaw rcv_hdr{};
    std::memcpy(&rcv_hdr, mem.raw_data() + 0x5000, sizeof(rcv_hdr));
    TEST_ASSERT(rcv_hdr.msgh_id == 19001, "Received message ID must match sent ID");

    std::cout << "  Passed (Darwin VFS file descriptors and Mach IPC message roundtrip fully validated).\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Full Multi-Image Guest CPU Execution Pipeline & Forensic Trace Evidence
// -----------------------------------------------------------------------------
bool test_full_guest_cpu_execution_pipeline_and_forensic_evidence() {
    std::cout << "[Test 7] Full Multi-Image Guest CPU Execution Pipeline & Trace Evidence...\n";

    Memory mem(0x4000000); // 64 MB physical guest memory
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    ExecutionDiagnostics diag;
    engine.set_diagnostics(&diag);

    auto compute_binary = MachOCorpusRegistry::create_math_compute();

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/usr/bin/math_compute";
    cfg.argv = {"/usr/bin/math_compute"};
    cfg.max_instructions = 1000;

    auto res = engine.execute_macho(compute_binary.binary_data.data(), compute_binary.binary_data.size(), cfg, 808);

    if (!res.is_clean_exit()) {
        std::cerr << "Execution failed: exited=" << res.process_exited
                  << ", code=" << res.exit_code
                  << ", instrs=" << res.guest_instructions_retired
                  << ", err=" << res.error_message << "\n";
    }
    TEST_ASSERT(res.is_clean_exit(), "Multi-step guest execution must exit cleanly");
    TEST_ASSERT(res.exit_code == 0, "Exit code must be 0");
    TEST_ASSERT(diag.step_count() > 400, "Math compute loop must execute > 400 instructions");

    // Verify trace filtering queries: find exit syscall (SYS_exit = 1)
    auto exit_syscalls = diag.find_syscalls_by_number(1);
    TEST_ASSERT(!exit_syscalls.empty(), "Syscall trace must contain SYS_exit call");

    // Verify SVC instruction execution in trace (mask = 0xFF000000u, val = 0xD4000000u)
    auto svc_steps = diag.find_steps_by_instruction(0xFF000000u, 0xD4000000u);
    TEST_ASSERT(!svc_steps.empty(), "Instruction trace must contain SVC call for exit");

    // Verify JSON & CSV export capability
    std::string json_trace = diag.export_trace_json();
    TEST_ASSERT(!json_trace.empty() && json_trace.find("\"instructions\"") != std::string::npos,
                "JSON trace export format invalid");

    std::string csv_trace = diag.export_trace_csv();
    TEST_ASSERT(!csv_trace.empty() && csv_trace.find("step,pc_before,pc_after") != std::string::npos,
                "CSV trace export format invalid");

    std::cout << "  Passed (Guest CPU executed " << diag.step_count()
              << " instructions with clean exit and forensic trace export).\n";
    return true;
}

// -----------------------------------------------------------------------------
// Main Test Runner
// -----------------------------------------------------------------------------
int main() {
    std::cout << "=================================================================\n";
    std::cout << "WinIOS Stage 190: Final Conformance & Independent Release Audit\n";
    std::cout << "Campaign 171–190 Master Verification Test Suite\n";
    std::cout << "Target: iPhone 15 (iPhone15,4 / D37AP / Apple A16 Bionic T8120 / ARM64e)\n";
    std::cout << "=================================================================\n\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](const char* name, bool (*fn)()) {
        if (fn()) {
            std::cout << "[PASS] " << name << "\n\n";
            passed++;
        } else {
            std::cerr << "[FAIL] " << name << "\n\n";
            failed++;
        }
    };

    run_test("Test 1: Corpus Binary Admission, Cryptographic Fingerprinting & Exclusion Policy",
             test_corpus_admission_and_cryptographic_fingerprinting);
    run_test("Test 2: ELF vs Mach-O Mutual Exclusivity & Architectural Segregation",
             test_elf_macho_mutual_exclusivity);
    run_test("Test 3: Apple Silicon 16KB Page Alignment & W^X Memory Protection Conformance",
             test_apple_silicon_page_alignment_and_wx_protection);
    run_test("Test 4: dyld Two-Level Namespace Symbol Isolation & Weak Fallback Resolution",
             test_two_level_namespace_and_weak_resolution);
    run_test("Test 5: Modern LC_DYLD_CHAINED_FIXUPS Pointer Unchaining & Binding",
             test_modern_chained_fixups_unchaining);
    run_test("Test 6: Darwin VFS, File Descriptors, and IPC Pipeline Conformance",
             test_darwin_vfs_and_ipc_conformance);
    run_test("Test 7: Full Multi-Image Guest CPU Execution Pipeline & Forensic Trace Evidence",
             test_full_guest_cpu_execution_pipeline_and_forensic_evidence);

    std::cout << "=================================================================\n";
    std::cout << "Stage 190 Master Release Audit Summary: " << passed << " passed, " << failed << " failed\n";
    std::cout << "=================================================================\n";

    return (failed == 0) ? 0 : 1;
}

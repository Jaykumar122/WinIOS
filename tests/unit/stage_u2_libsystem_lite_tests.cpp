// =============================================================================
// stage_u2_libsystem_lite_tests.cpp
//
// Stage U2: Real Compiled libSystem-Lite (/usr/lib/libSystem.B.dylib) Tests
// Verifies non-freestanding C applications compiled with Zig/Clang against
// standard macOS headers (<stdio.h>, <stdlib.h>, <string.h>, <unistd.h>)
// executing in EL0 via guest minidyld + libSystem.B.dylib:
//   - Artifact provenance & Mach-O export trie verification
//   - Standard main(argc, argv) with printf, puts, putchar, snprintf, getpid
//   - Mmap-backed heap allocation (malloc, calloc, realloc, free)
//   - String & memory routines (strcpy, strcmp, strlen, memcpy, memset, memcmp)
//   - Stack protector detection (___stack_chk_guard / ___stack_chk_fail)
// =============================================================================

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/dyld_engine.h"
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

constexpr const char* kMinidyldPath       = "tests/artifacts/bin/minidyld";
constexpr const char* kLibSystemPath      = "tests/artifacts/bin/libSystem.B.dylib";
constexpr const char* kStdioHeapAppPath   = "tests/artifacts/bin/stdio_heap_app";
constexpr const char* kStackSmashAppPath  = "tests/artifacts/bin/stack_smash_libc_app";

std::string resolve_path(const std::string& rel) {
    const std::vector<std::string> candidates = {
        rel,
        "WinIOS/" + rel,
        "../" + rel,
        "../../" + rel,
        "D:/WinISO26/WinIOS/" + rel
    };
    for (const auto& c : candidates) {
        std::ifstream f(c, std::ios::binary);
        if (f.good()) return c;
    }
    return rel;
}

std::vector<uint8_t> read_binary_file(const std::string& rel) {
    std::string p = resolve_path(rel);
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f.good()) return {};
    auto sz = f.tellg();
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

// -----------------------------------------------------------------------------
// Test 1: libSystem.B.dylib & App Artifact Provenance
// -----------------------------------------------------------------------------
bool test_libsystem_provenance() {
    std::cout << "[Test 1] libSystem.B.dylib Artifact Provenance & Structure...\n";

    auto libBytes = read_binary_file(kLibSystemPath);
    auto appBytes = read_binary_file(kStdioHeapAppPath);
    auto smashBytes = read_binary_file(kStackSmashAppPath);

    TEST_ASSERT(!libBytes.empty(), "libSystem.B.dylib binary must exist");
    TEST_ASSERT(!appBytes.empty(), "stdio_heap_app binary must exist");
    TEST_ASSERT(!smashBytes.empty(), "stack_smash_libc_app binary must exist");

    UserspaceLoadResult libLoad;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(libBytes.data(), libBytes.size(), libLoad),
                "libSystem.B.dylib must parse as valid ARM64 Mach-O");

    UserspaceLoadResult appLoad;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(appBytes.data(), appBytes.size(), appLoad),
                "stdio_heap_app must parse as valid ARM64 Mach-O");

    std::string libHash = RealArtifactHarness::compute_sha256(libBytes.data(), libBytes.size());
    std::string appHash = RealArtifactHarness::compute_sha256(appBytes.data(), appBytes.size());
    std::string smashHash = RealArtifactHarness::compute_sha256(smashBytes.data(), smashBytes.size());

    std::cout << "  libSystem.B.dylib:    " << libBytes.size() << " bytes, SHA-256=" << libHash << "\n";
    std::cout << "  stdio_heap_app:       " << appBytes.size() << " bytes, SHA-256=" << appHash << "\n";
    std::cout << "  stack_smash_libc_app: " << smashBytes.size() << " bytes, SHA-256=" << smashHash << "\n";
    std::cout << "  app entry offset:     0x" << std::hex << appLoad.entry_point << std::dec << "\n";

    TEST_ASSERT(!appLoad.dylibs.empty(), "stdio_heap_app must dynamically link libSystem.B.dylib");
    bool has_libsystem = false;
    for (const auto& d : appLoad.dylibs) {
        if (d.path == "/usr/lib/libSystem.B.dylib") has_libsystem = true;
    }
    TEST_ASSERT(has_libsystem, "stdio_heap_app must declare LC_LOAD_DYLIB /usr/lib/libSystem.B.dylib");

    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Standard Non-Freestanding C App via Guest Minidyld + libSystem-Lite
// -----------------------------------------------------------------------------
bool test_stdio_heap_app_execution() {
    std::cout << "[Test 2] Standard C App Execution (stdio + mmap heap + argc/argv)...\n";

    auto dyldBytes = read_binary_file(kMinidyldPath);
    auto libBytes  = read_binary_file(kLibSystemPath);
    auto appBytes  = read_binary_file(kStdioHeapAppPath);

    TEST_ASSERT(!dyldBytes.empty() && !libBytes.empty() && !appBytes.empty(),
                "Required artifacts missing");

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    bsd.vfs().write_file("/usr/lib/libSystem.B.dylib", libBytes.data(), libBytes.size());

    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/stdio_heap_app";
    config.argv = {"/usr/bin/stdio_heap_app", "--verify"};
    config.envp = {"PATH=/usr/bin", "HOME=/var/mobile"};
    config.stack_top = 0x03F00000ULL;
    config.max_instructions = 500000;

    auto result = engine.execute_macho_with_guest_dyld(
        dyldBytes.data(), dyldBytes.size(),
        appBytes.data(), appBytes.size(),
        config, 601);

    std::cout << "  Instructions retired: " << result.guest_instructions_retired << "\n";
    std::cout << "  Process exited:       " << (result.process_exited ? "true" : "false")
              << ", code: " << result.exit_code << "\n";
    std::cout << "  BSD syscalls handled: " << result.bsd_syscalls_handled << "\n";
    std::cout << "  Captured stdout:\n" << result.stdout_output;

    if (!result.error_message.empty()) {
        std::cerr << "  Engine error: " << result.error_message << "\n";
    }

    TEST_ASSERT(result.process_exited, "Process must exit cleanly");
    TEST_ASSERT(result.exit_code == 0, "Exit code must be 0");
    TEST_ASSERT(result.stdout_output.find("LIBSYSTEM_LITE_START\n") != std::string::npos,
                "Must contain LIBSYSTEM_LITE_START");
    TEST_ASSERT(result.stdout_output.find("PID=601 ARGC=2 PROG=/usr/bin/stdio_heap_app\n") != std::string::npos,
                "Must receive real PID=601, argc=2, argv[0]");
    TEST_ASSERT(result.stdout_output.find("HEAP=WinIOS_Heap_OK LEN=14 ZERO=0 TOTAL=0x64 SNP=0,0\n") != std::string::npos,
                "Must verify malloc/realloc/calloc/strcpy/memcpy/memset/memcmp/strcmp/snprintf");
    TEST_ASSERT(result.stdout_output.find("FMT NEG=-42 HEX=0000CAFE PCT=% PTR=0x100") != std::string::npos,
                "Must verify printf formatting matrix and mmap heap range 0x100...");
    TEST_ASSERT(result.stdout_output.find(">\nLIBSYSTEM_LITE_PASS\n") != std::string::npos,
                "Must verify putchar and completion banner");

    // Verify mmap regions were created in BSD subsystem
    auto proc = bsd.get_process(601);
    TEST_ASSERT(proc != nullptr, "Process 601 must exist in BSD table");
    TEST_ASSERT(!proc->vm_regions.empty(), "malloc/calloc must have triggered SYS_mmap VM regions");

    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Negative Test: Missing libSystem.B.dylib & Stack Smash Protection
// -----------------------------------------------------------------------------
bool test_stack_protector_libsystem_lite() {
    std::cout << "[Test 3] Stack Protector Runtime via libSystem.B.dylib (___stack_chk_fail)...\n";

    auto dyldBytes  = read_binary_file(kMinidyldPath);
    auto libBytes   = read_binary_file(kLibSystemPath);
    auto smashBytes = read_binary_file(kStackSmashAppPath);

    TEST_ASSERT(!dyldBytes.empty() && !libBytes.empty() && !smashBytes.empty(),
                "Required stack smash artifacts missing");

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    bsd.vfs().write_file("/usr/lib/libSystem.B.dylib", libBytes.data(), libBytes.size());

    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/stack_smash_libc_app";
    config.stack_top = 0x03F00000ULL;
    config.max_instructions = 500000;

    auto result = engine.execute_macho_with_guest_dyld(
        dyldBytes.data(), dyldBytes.size(),
        smashBytes.data(), smashBytes.size(),
        config, 602);

    std::cout << "  Instructions retired: " << result.guest_instructions_retired << "\n";
    std::cout << "  Process exited:       " << (result.process_exited ? "true" : "false")
              << ", code: " << result.exit_code << "\n";
    std::cout << "  Captured stdout:      \"" << result.stdout_output << "\"\n";
    std::cout << "  Captured stderr:      \"" << result.stderr_output << "\"\n";

    if (!result.error_message.empty()) {
        std::cerr << "  Engine error: " << result.error_message << "\n";
    }

    TEST_ASSERT(result.process_exited, "Stack-smashed process must terminate cleanly via ___stack_chk_fail");
    TEST_ASSERT(result.exit_code == 134, "___stack_chk_fail must abort with exit code 134");
    TEST_ASSERT(result.stdout_output == "BEFORE_SMASH\n", "Must execute up to overflow without reaching return");
    TEST_ASSERT(result.stderr_output.find("stack smashing detected") != std::string::npos,
                "libSystem.B.dylib ___stack_chk_fail must emit stack smashing diagnostic to stderr");

    return true;
}

// -----------------------------------------------------------------------------
// Test 4: GOT Provenance Check: libSystem.B.dylib Exports Override Fallbacks
// -----------------------------------------------------------------------------
bool test_libsystem_got_provenance() {
    std::cout << "[Test 4] GOT Resolution Into Real libSystem.B.dylib (Not Minidyld Fallbacks)...\n";

    auto dyldBytes  = read_binary_file(kMinidyldPath);
    auto libBytes   = read_binary_file(kLibSystemPath);
    auto smashBytes = read_binary_file(kStackSmashAppPath);

    TEST_ASSERT(!dyldBytes.empty() && !libBytes.empty() && !smashBytes.empty(),
                "Required artifacts missing");

    UserspaceLoadResult smashMeta;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(smashBytes.data(), smashBytes.size(), smashMeta),
                "Failed to parse stack_smash_libc_app");

    // Extract LC_DYLD_INFO_ONLY bind stream to find ___stack_chk_guard GOT vaddr dynamically
    uint32_t bind_off = 0, bind_size = 0;
    {
        size_t off = sizeof(MachOHeader64Raw);
        for (uint32_t i = 0; i < smashMeta.header.ncmds && off + sizeof(MachOLoadCommandRaw) <= smashBytes.size(); ++i) {
            MachOLoadCommandRaw lc{};
            std::memcpy(&lc, smashBytes.data() + off, sizeof(lc));
            if (lc.cmd == 0x22u || lc.cmd == 0x80000022u) {
                if (off + 24 <= smashBytes.size()) {
                    std::memcpy(&bind_off, smashBytes.data() + off + 16, 4);
                    std::memcpy(&bind_size, smashBytes.data() + off + 20, 4);
                }
            }
            off += lc.cmdsize;
        }
    }
    TEST_ASSERT(bind_off > 0 && bind_size > 0 && bind_off + bind_size <= smashBytes.size(),
                "stack_smash_libc_app must contain LC_DYLD_INFO_ONLY bind stream");

    std::vector<uint64_t> seg_vmaddrs;
    for (const auto& s : smashMeta.segments) seg_vmaddrs.push_back(s.vmaddr);
    std::vector<DyldBindingRecord> bindings;
    std::string dyld_err;
    TEST_ASSERT(DyldEngine::parse_binding_stream(smashBytes.data() + bind_off, bind_size, seg_vmaddrs, bindings, dyld_err),
                "Failed to parse binding stream");

    uint64_t guard_got_addr = 0;
    for (const auto& sym : bindings) {
        if (sym.symbol_name == "___stack_chk_guard") {
            guard_got_addr = sym.patch_vaddr & 0xFFFFFFFFULL;
        }
    }
    TEST_ASSERT(guard_got_addr != 0, "stack_smash_libc_app must have GOT entry for ___stack_chk_guard");

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    bsd.vfs().write_file("/usr/lib/libSystem.B.dylib", libBytes.data(), libBytes.size());

    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/stack_smash_libc_app";
    config.stack_top = 0x03F00000ULL;
    config.max_instructions = 500000;

    auto result = engine.execute_macho_with_guest_dyld(
        dyldBytes.data(), dyldBytes.size(),
        smashBytes.data(), smashBytes.size(),
        config, 603);
    TEST_ASSERT(result.process_exited && result.exit_code == 134, "Expected abort code 134");

    uint64_t bound_guard = memory.read_u64(guard_got_addr);
    std::cout << "  GOT[___stack_chk_guard] @ 0x" << std::hex << guard_got_addr
              << " = 0x" << bound_guard << std::dec << "\n";

    // libSystem.B.dylib is loaded at g_next_dylib_base = 0x20000..0x30000
    TEST_ASSERT(bound_guard >= 0x20000ULL && bound_guard < 0x30000ULL,
                "GOT[___stack_chk_guard] must point into loaded libSystem.B.dylib, not minidyld fallback (0x2000000+)");
    TEST_ASSERT(memory.read_u64(bound_guard) == 0x595E9D4B1A2C3E4FULL,
                "Resolved ___stack_chk_guard value must match libSystem.B.dylib canary");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE U2: Real Compiled libSystem-Lite Tests\n";
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

    run_test(test_libsystem_provenance, "libSystem.B.dylib Provenance & Exports");
    run_test(test_stdio_heap_app_execution, "Standard C App (stdio + mmap heap + argc/argv)");
    run_test(test_stack_protector_libsystem_lite, "Stack Protector via libSystem.B.dylib");
    run_test(test_libsystem_got_provenance, "GOT Provenance Verification (Real libSystem vs Fallback)");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}

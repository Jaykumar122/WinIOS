#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
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

constexpr const char* kDylibPath = "tests/artifacts/bin/libmathreal.dylib";
constexpr const char* kExpectedDylibSha256 = "5d6be8ac5379b472c955350591f839903a700bccc8cd6bdd185ac06f8ead8331";
constexpr std::size_t kExpectedDylibSize = 50144;

constexpr const char* kMainPath = "tests/artifacts/bin/main_with_dylib";
constexpr const char* kExpectedMainSha256 = "f4ea6b73091a142636200fdf999f665f06ff8c0db4b3618d76b2b8af94a2cc44";
constexpr std::size_t kExpectedMainSize = 50784;

constexpr const char* kExpectedStdout = "DYLIB_OK: 0x0A11C001CAFEBAE8\n";

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
        if (f.is_open()) {
            return c;
        }
    }
    return rel;
}

std::vector<std::uint8_t> read_binary_file(const std::string& path) {
    const std::string resolved = resolve_path(path);
    std::ifstream file(resolved, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return {};
    }
    const auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(size));
    if (size > 0) {
        file.read(reinterpret_cast<char*>(buffer.data()), size);
    }
    return buffer;
}

// Test 1: Verify disk load and SHA-256 for REAL-004 (dylib) and REAL-005 (main)
bool test_dylib_artifacts_disk_load_and_provenance() {
    std::cout << "[Test 1] Load Real Dylib & Main Artifacts & Verify Provenance...\n";

    // REAL-004 (dylib)
    auto dylibBytes = read_binary_file(kDylibPath);
    TEST_ASSERT(!dylibBytes.empty(), "REAL-004 (libmathreal.dylib) must exist on disk");
    TEST_ASSERT(dylibBytes.size() == kExpectedDylibSize, "REAL-004 size must match expected");
    auto dylibSha = RealArtifactHarness::compute_sha256(dylibBytes.data(), dylibBytes.size());
    std::cout << "  REAL-004: " << kDylibPath << " (" << dylibBytes.size() << " bytes)\n";
    std::cout << "  SHA-256:  " << dylibSha << "\n";
    TEST_ASSERT(dylibSha == kExpectedDylibSha256, "REAL-004 SHA-256 must match manifest");

    auto metaDylib = RealArtifactHarness::inspect_buffer(dylibBytes.data(), dylibBytes.size(), kDylibPath);
    TEST_ASSERT(metaDylib.is_macho, "REAL-004 must be valid Mach-O");
    TEST_ASSERT(metaDylib.is_arm64, "REAL-004 must be ARM64");

    // REAL-005 (main)
    auto mainBytes = read_binary_file(kMainPath);
    TEST_ASSERT(!mainBytes.empty(), "REAL-005 (main_with_dylib) must exist on disk");
    TEST_ASSERT(mainBytes.size() == kExpectedMainSize, "REAL-005 size must match expected");
    auto mainSha = RealArtifactHarness::compute_sha256(mainBytes.data(), mainBytes.size());
    std::cout << "  REAL-005: " << kMainPath << " (" << mainBytes.size() << " bytes)\n";
    std::cout << "  SHA-256:  " << mainSha << "\n";
    TEST_ASSERT(mainSha == kExpectedMainSha256, "REAL-005 SHA-256 must match manifest");

    auto metaMain = RealArtifactHarness::inspect_buffer(mainBytes.data(), mainBytes.size(), kMainPath);
    TEST_ASSERT(metaMain.is_macho, "REAL-005 must be valid Mach-O");
    TEST_ASSERT(metaMain.is_arm64, "REAL-005 must be ARM64");
    TEST_ASSERT(metaMain.can_execute_in_userspace(), "REAL-005 must be admitted for execution");

    return true;
}

// Test 2: Parse real export trie from REAL-004 (libmathreal.dylib)
bool test_parse_real_dylib_export_trie() {
    std::cout << "[Test 2] Parse Real Mach-O Export Trie from libmathreal.dylib...\n";

    auto dylibBytes = read_binary_file(kDylibPath);
    TEST_ASSERT(!dylibBytes.empty(), "Failed to read dylib");

    // Extract export trie: offset 0xc018, size 112 from LC_DYLD_INFO_ONLY
    constexpr std::size_t kExportOffset = 0xc018;
    constexpr std::size_t kExportSize = 112;
    TEST_ASSERT(kExportOffset + kExportSize <= dylibBytes.size(), "Export trie within bounds");

    std::vector<DyldExportedSymbolInfo> exported_symbols;
    std::string err;
    bool ok = DyldEngine::parse_export_trie(dylibBytes.data() + kExportOffset, kExportSize, exported_symbols, err);
    TEST_ASSERT(ok, "DyldEngine::parse_export_trie must succeed on real dylib trie");
    TEST_ASSERT(err.empty(), "Error message must be empty");

    std::cout << "  Exported symbols discovered: " << exported_symbols.size() << "\n";
    bool found_add = false;
    bool found_read = false;
    bool found_magic = false;

    for (const auto& sym : exported_symbols) {
        std::cout << "    Symbol: " << sym.name << " at offset 0x" << std::hex << sym.address << std::dec << "\n";
        if (sym.name == "_real_add") found_add = true;
        if (sym.name == "_real_read_global") found_read = true;
        if (sym.name == "_g_real_magic") found_magic = true;
    }

    TEST_ASSERT(found_add, "Export trie must contain _real_add");
    TEST_ASSERT(found_read, "Export trie must contain _real_read_global");
    TEST_ASSERT(found_magic, "Export trie must contain _g_real_magic");

    return true;
}

// Test 3: Parse real LC_DYLD_INFO_ONLY binding opcodes from REAL-005 (main_with_dylib)
bool test_parse_real_binding_stream() {
    std::cout << "[Test 3] Parse Real LC_DYLD_INFO_ONLY Binding Opcodes from main_with_dylib...\n";

    auto mainBytes = read_binary_file(kMainPath);
    TEST_ASSERT(!mainBytes.empty(), "Failed to read main_with_dylib");

    // Segment base virtual addresses of main_with_dylib:
    // Seg 0: __PAGEZERO (0x0)
    // Seg 1: __TEXT (0x100000000)
    // Seg 2: __DATA_CONST (0x100004000)
    // Seg 3: __DATA (0x100008000)
    // Seg 4: __LINKEDIT (0x10000c000)
    const std::vector<std::uint64_t> seg_vmaddrs = {
        0x0ULL,
        0x100000000ULL,
        0x100004000ULL,
        0x100008000ULL,
        0x10000c000ULL
    };

    // Lazy bind opcodes in main_with_dylib: offset 0xc038, size 64
    constexpr std::size_t kLazyBindOffset = 0xc038;
    constexpr std::size_t kLazyBindSize = 64;

    std::vector<DyldBindingRecord> bindings;
    std::string err;
    bool ok = DyldEngine::parse_binding_stream(mainBytes.data() + kLazyBindOffset, kLazyBindSize,
                                              seg_vmaddrs, bindings, err);
    TEST_ASSERT(ok, "DyldEngine::parse_binding_stream must succeed on lazy bind stream");
    TEST_ASSERT(err.empty(), "Error message must be empty");

    std::cout << "  Binding records parsed: " << bindings.size() << "\n";
    bool found_real_add = false;
    bool found_real_read = false;

    for (const auto& b : bindings) {
        std::cout << "    Binding: " << b.symbol_name << " -> patch_vaddr 0x"
                  << std::hex << b.patch_vaddr << " (ordinal " << std::dec << b.dylib_ordinal << ")\n";
        if (b.symbol_name == "_real_add") {
            found_real_add = true;
            TEST_ASSERT(b.patch_vaddr == 0x100008008ULL, "Patch address for _real_add must match GOT offset");
        }
        if (b.symbol_name == "_real_read_global") {
            found_real_read = true;
            TEST_ASSERT(b.patch_vaddr == 0x100008010ULL, "Patch address for _real_read_global must match GOT offset");
        }
    }

    TEST_ASSERT(found_real_add, "Must contain binding for _real_add");
    TEST_ASSERT(found_real_read, "Must contain binding for _real_read_global");

    return true;
}

// Test 4: Real Dylib Cross-Binary Execution on Guest CPU
bool test_real_dylib_cross_binary_execution() {
    std::cout << "[Test 4] Real Dylib Cross-Binary Execution (main_with_dylib + libmathreal.dylib)...\n";

    auto mainBytes = read_binary_file(kMainPath);
    auto dylibBytes = read_binary_file(kDylibPath);
    TEST_ASSERT(!mainBytes.empty() && !dylibBytes.empty(), "Both binaries must be read");

    CPU cpu;
    Memory memory(64 * 1024 * 1024); // 64 MB guest memory
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    // 1. Map libmathreal.dylib into guest memory at page-aligned dylib_base
    // dylib_base = 0x100020000 (modulo 64MB: 0x20000, free area)
    constexpr std::uint64_t kDylibBase = 0x20000ULL;
    UserspaceLoadResult dylibLoad;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(dylibBytes.data(), dylibBytes.size(), dylibLoad),
                "Parse dylib metadata");

    for (const auto& seg : dylibLoad.segments) {
        if (seg.is_pagezero) continue;
        std::uint64_t target_vaddr = (kDylibBase + seg.vmaddr) % memory.size();
        for (std::size_t i = 0; i < seg.filesize; ++i) {
            if (target_vaddr + i < memory.size()) {
                memory.write_u8(target_vaddr + i, dylibBytes[seg.fileoff + i]);
            }
        }
    }

    // 2. Parse dylib export trie and register module in DyldEngine
    DyldEngine dyld;
    dyld.register_builtin_system_libraries();

    constexpr std::size_t kExportOffset = 0xc018;
    constexpr std::size_t kExportSize = 112;
    std::vector<DyldExportedSymbolInfo> dylibExports;
    std::string err;
    TEST_ASSERT(DyldEngine::parse_export_trie(dylibBytes.data() + kExportOffset, kExportSize, dylibExports, err),
                "Parse dylib export trie");

    DyldLoadedModule mod;
    mod.name = "libmathreal.dylib";
    mod.install_name = "/usr/lib/libmathreal.dylib";
    mod.base_address = kDylibBase;
    for (const auto& sym : dylibExports) {
        mod.exports[sym.name] = kDylibBase + sym.address;
    }
    dyld.register_module(std::move(mod));

    // 3. Parse main_with_dylib bindings
    const std::vector<std::uint64_t> seg_vmaddrs = {
        0x0ULL,
        0x100000000ULL,
        0x100004000ULL,
        0x100008000ULL,
        0x10000c000ULL
    };
    constexpr std::size_t kLazyBindOffset = 0xc038;
    constexpr std::size_t kLazyBindSize = 64;
    std::vector<DyldBindingRecord> bindings;
    TEST_ASSERT(DyldEngine::parse_binding_stream(mainBytes.data() + kLazyBindOffset, kLazyBindSize,
                                                seg_vmaddrs, bindings, err),
                "Parse main bindings");

    // 4. Map main_with_dylib and execute through DarwinUserspaceEngine
    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/main_with_dylib";
    config.max_instructions = 50000;
    config.pre_execution_hook = [&](Memory& mem) {
        std::string bind_err;
        auto st = dyld.apply_bindings(mem, bindings, 0, bind_err);
        if (st != DyldLinkStatus::Success) {
            std::cerr << "Binding failure in hook: " << bind_err << "\n";
        }
    };

    auto result = engine.execute_macho(mainBytes.data(), mainBytes.size(), config, 301);

    std::cout << "  Guest instructions retired: " << result.guest_instructions_retired << "\n";
    std::cout << "  Process exited: " << (result.process_exited ? "true" : "false")
              << ", Exit code: " << result.exit_code << "\n";
    std::cout << "  Captured stdout: \"" << result.stdout_output << "\"\n";
    std::cout << "  BSD syscalls handled: " << result.bsd_syscalls_handled << "\n";

    if (!result.error_message.empty()) {
        std::cerr << "  Engine error: " << result.error_message << "\n";
    }

    TEST_ASSERT(result.guest_instructions_retired > 0, "Guest instructions must be retired");
    TEST_ASSERT(result.process_exited, "Process must cleanly exit");
    TEST_ASSERT(result.exit_code == 0, "Process exit code must be 0");
    TEST_ASSERT(result.stdout_output == kExpectedStdout, "Captured stdout must match expected format");

    return true;
}

// Test 5: Negative Test: Missing dylib resolution fails gracefully
bool test_missing_dylib_graceful_handling() {
    std::cout << "[Test 5] Negative Test: Missing Dylib Symbol Resolution...\n";

    DyldEngine dyld;
    // Note: libmathreal is NOT registered
    auto res = dyld.resolve_symbol("_real_add");
    TEST_ASSERT(!res.has_value(), "Resolving symbol from missing dylib must return nullopt");

    return true;
}

// Test 6: Negative Test: Undefined symbol in binding fails gracefully
bool test_undefined_symbol_binding_failure() {
    std::cout << "[Test 6] Negative Test: Undefined Symbol Binding Rejection...\n";

    DyldEngine dyld;
    Memory memory(0x10000);
    std::vector<DyldBindingRecord> bindings;
    bindings.push_back(DyldBindingRecord{
        "_nonexistent_function",
        0x1000,
        0,
        1,
        false // strong binding
    });

    std::string err;
    auto status = dyld.apply_bindings(memory, bindings, 0, err);
    TEST_ASSERT(status == DyldLinkStatus::UndefinedSymbol, "Must reject undefined strong symbol");
    TEST_ASSERT(!err.empty(), "Error message must report undefined symbol");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE S2: Real Mach-O Dylib Dynamic Linking & Execution\n";
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

    run_test(test_dylib_artifacts_disk_load_and_provenance, "Dylib Artifacts Disk Load & Provenance");
    run_test(test_parse_real_dylib_export_trie, "Parse Real Mach-O Export Trie (libmathreal.dylib)");
    run_test(test_parse_real_binding_stream, "Parse Real Binding Stream (main_with_dylib)");
    run_test(test_real_dylib_cross_binary_execution, "Real Dylib Cross-Binary Execution");
    run_test(test_missing_dylib_graceful_handling, "Negative Test: Missing Dylib Resolution");
    run_test(test_undefined_symbol_binding_failure, "Negative Test: Undefined Symbol Rejection");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}

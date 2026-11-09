#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/dyld_engine.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"
#include "runtime/execution_diagnostics.h"

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

constexpr const char* kLeafPath = "tests/artifacts/bin/libleaf.dylib";
constexpr const char* kMidPath = "tests/artifacts/bin/libmid.dylib";
constexpr const char* kMainChainPath = "tests/artifacts/bin/main_chain";
constexpr const char* kExpectedStdout = "CHAIN_OK: 142\n";

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

std::vector<std::uint8_t> read_binary_file(const std::string& rel) {
    const std::string p = resolve_path(rel);
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    const auto sz = static_cast<std::size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(sz);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz));
    return buf;
}

void get_dyld_info(const std::uint8_t* bytes, std::size_t size,
                   std::size_t& bind_off, std::size_t& bind_size,
                   std::size_t& lazy_off, std::size_t& lazy_size,
                   std::size_t& export_off, std::size_t& export_size) {
    bind_off = bind_size = lazy_off = lazy_size = export_off = export_size = 0;
    if (size < 32) return;
    std::uint32_t ncmds = *reinterpret_cast<const std::uint32_t*>(bytes + 16);
    std::size_t off = 32;
    for (std::uint32_t i = 0; i < ncmds && off + 8 <= size; ++i) {
        std::uint32_t cmd = *reinterpret_cast<const std::uint32_t*>(bytes + off);
        std::uint32_t cmdsize = *reinterpret_cast<const std::uint32_t*>(bytes + off + 4);
        if (cmd == 0x80000022u && off + 48 <= size) { // LC_DYLD_INFO_ONLY
            bind_off = *reinterpret_cast<const std::uint32_t*>(bytes + off + 16);
            bind_size = *reinterpret_cast<const std::uint32_t*>(bytes + off + 20);
            lazy_off = *reinterpret_cast<const std::uint32_t*>(bytes + off + 32);
            lazy_size = *reinterpret_cast<const std::uint32_t*>(bytes + off + 36);
            export_off = *reinterpret_cast<const std::uint32_t*>(bytes + off + 40);
            export_size = *reinterpret_cast<const std::uint32_t*>(bytes + off + 44);
            return;
        }
        off += cmdsize;
    }
}

} // namespace

// Test 1: Verify Three-Image Metadata, Rpaths and Transitive Resolution
bool test_three_image_metadata_and_rpaths() {
    std::cout << "[Test 1] Three-Image Metadata, Rpaths and Transitive Resolution...\n";

    auto leafBytes = read_binary_file(kLeafPath);
    auto midBytes = read_binary_file(kMidPath);
    auto mainBytes = read_binary_file(kMainChainPath);

    TEST_ASSERT(!leafBytes.empty(), "libleaf.dylib must exist");
    TEST_ASSERT(!midBytes.empty(), "libmid.dylib must exist");
    TEST_ASSERT(!mainBytes.empty(), "main_chain must exist");

    UserspaceLoadResult leafRes, midRes, mainRes;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(leafBytes.data(), leafBytes.size(), leafRes), "Parse leaf");
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(midBytes.data(), midBytes.size(), midRes), "Parse mid");
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(mainBytes.data(), mainBytes.size(), mainRes), "Parse main");

    // Verify main_chain dylib dependencies and rpaths
    bool main_has_mid = false;
    for (const auto& d : mainRes.dylibs) {
        if (d.path == "@rpath/libmid.dylib") main_has_mid = true;
    }
    TEST_ASSERT(main_has_mid, "main_chain must depend on @rpath/libmid.dylib");
    TEST_ASSERT(!mainRes.rpaths.empty() && mainRes.rpaths[0] == "@executable_path",
                "main_chain must contain LC_RPATH @executable_path");

    // Verify libmid.dylib dylib dependencies and rpaths
    bool mid_has_leaf = false;
    for (const auto& d : midRes.dylibs) {
        if (d.path == "@rpath/libleaf.dylib") mid_has_leaf = true;
    }
    TEST_ASSERT(mid_has_leaf, "libmid.dylib must depend on @rpath/libleaf.dylib");
    TEST_ASSERT(!midRes.rpaths.empty() && midRes.rpaths[0] == "@loader_path",
                "libmid.dylib must contain LC_RPATH @loader_path");

    // Test DyldEngine::resolve_rpath_path resolution
    std::string mid_file = resolve_path(kMidPath);
    std::string artifact_dir;
    auto last_slash = mid_file.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        artifact_dir = mid_file.substr(0, last_slash);
    } else {
        artifact_dir = ".";
    }

    std::string resolved_mid = DyldEngine::resolve_rpath_path("@rpath/libmid.dylib", mainRes.rpaths, artifact_dir, artifact_dir);
    TEST_ASSERT(!resolved_mid.empty(), "DyldEngine must resolve @rpath/libmid.dylib via @executable_path");

    std::string resolved_leaf = DyldEngine::resolve_rpath_path("@rpath/libleaf.dylib", midRes.rpaths, artifact_dir, artifact_dir);
    TEST_ASSERT(!resolved_leaf.empty(), "DyldEngine must resolve @rpath/libleaf.dylib via @loader_path");

    std::cout << "  Resolved libmid:  " << resolved_mid << "\n";
    std::cout << "  Resolved libleaf: " << resolved_leaf << "\n";

    return true;
}

// Test 2: Execute Three-Image Chain with Trace Logging and Cross-Binary Verification
bool test_three_image_transitive_execution() {
    std::cout << "[Test 2] Execute Three-Image Transitive Chain (main_chain -> libmid -> libleaf)...\n";

    auto leafBytes = read_binary_file(kLeafPath);
    auto midBytes = read_binary_file(kMidPath);
    auto mainBytes = read_binary_file(kMainChainPath);

    TEST_ASSERT(!leafBytes.empty() && !midBytes.empty() && !mainBytes.empty(), "All three binaries required");

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    // 1. Map libleaf.dylib at 0x20000
    constexpr std::uint64_t kLeafBase = 0x20000ULL;
    UserspaceLoadResult leafLoad;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(leafBytes.data(), leafBytes.size(), leafLoad), "Parse leaf");
    for (const auto& seg : leafLoad.segments) {
        if (seg.is_pagezero) continue;
        std::uint64_t target_vaddr = (kLeafBase + seg.vmaddr) % memory.size();
        for (std::size_t i = 0; i < seg.filesize; ++i) {
            if (target_vaddr + i < memory.size()) {
                memory.write_u8(target_vaddr + i, leafBytes[seg.fileoff + i]);
            }
        }
    }

    // 2. Map libmid.dylib at 0x30000
    constexpr std::uint64_t kMidBase = 0x30000ULL;
    UserspaceLoadResult midLoad;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(midBytes.data(), midBytes.size(), midLoad), "Parse mid");
    for (const auto& seg : midLoad.segments) {
        if (seg.is_pagezero) continue;
        std::uint64_t target_vaddr = (kMidBase + seg.vmaddr) % memory.size();
        for (std::size_t i = 0; i < seg.filesize; ++i) {
            if (target_vaddr + i < memory.size()) {
                memory.write_u8(target_vaddr + i, midBytes[seg.fileoff + i]);
            }
        }
    }

    // 3. Setup DyldEngine and extract exports
    DyldEngine dyld;
    dyld.register_builtin_system_libraries();

    std::size_t leaf_bind_off, leaf_bind_sz, leaf_lazy_off, leaf_lazy_sz, leaf_exp_off, leaf_exp_sz;
    get_dyld_info(leafBytes.data(), leafBytes.size(), leaf_bind_off, leaf_bind_sz, leaf_lazy_off, leaf_lazy_sz, leaf_exp_off, leaf_exp_sz);

    std::size_t mid_bind_off, mid_bind_sz, mid_lazy_off, mid_lazy_sz, mid_exp_off, mid_exp_sz;
    get_dyld_info(midBytes.data(), midBytes.size(), mid_bind_off, mid_bind_sz, mid_lazy_off, mid_lazy_sz, mid_exp_off, mid_exp_sz);

    std::size_t main_bind_off, main_bind_sz, main_lazy_off, main_lazy_sz, main_exp_off, main_exp_sz;
    get_dyld_info(mainBytes.data(), mainBytes.size(), main_bind_off, main_bind_sz, main_lazy_off, main_lazy_sz, main_exp_off, main_exp_sz);

    std::cout << "  leaf export off=0x" << std::hex << leaf_exp_off << " sz=" << leaf_exp_sz << std::dec << "\n";
    std::cout << "  mid export off=0x" << std::hex << mid_exp_off << " sz=" << mid_exp_sz
              << " bind off=0x" << mid_bind_off << " sz=" << mid_bind_sz
              << " lazy off=0x" << mid_lazy_off << " sz=" << mid_lazy_sz << std::dec << "\n";
    std::cout << "  main bind off=0x" << std::hex << main_bind_off << " sz=" << main_bind_sz
              << " lazy off=0x" << main_lazy_off << " sz=" << main_lazy_sz << std::dec << "\n";

    // libleaf exports
    std::vector<DyldExportedSymbolInfo> leafExports;
    std::string err;
    if (leaf_exp_sz > 0) {
        TEST_ASSERT(DyldEngine::parse_export_trie(leafBytes.data() + leaf_exp_off, leaf_exp_sz, leafExports, err), "Parse leaf exports");
    }
    DyldLoadedModule leafMod;
    leafMod.name = "libleaf.dylib";
    leafMod.install_name = "@rpath/libleaf.dylib";
    leafMod.base_address = kLeafBase;
    for (const auto& s : leafExports) {
        leafMod.exports[s.name] = kLeafBase + s.address;
        std::cout << "    [leafMod] export: " << s.name << " -> 0x" << std::hex << (kLeafBase + s.address) << std::dec << "\n";
    }
    dyld.register_module(std::move(leafMod));

    // libmid exports
    std::vector<DyldExportedSymbolInfo> midExports;
    if (mid_exp_sz > 0) {
        TEST_ASSERT(DyldEngine::parse_export_trie(midBytes.data() + mid_exp_off, mid_exp_sz, midExports, err), "Parse mid exports");
    }
    DyldLoadedModule midMod;
    midMod.name = "libmid.dylib";
    midMod.install_name = "@rpath/libmid.dylib";
    midMod.base_address = kMidBase;
    for (const auto& s : midExports) {
        midMod.exports[s.name] = kMidBase + s.address;
        std::cout << "    [midMod] export: " << s.name << " -> 0x" << std::hex << (kMidBase + s.address) << std::dec << "\n";
    }
    dyld.register_module(std::move(midMod));

    // 4. Parse bindings for libmid (binds _leaf_mul)
    const std::vector<std::uint64_t> mid_seg_vmaddrs = {
        0x0ULL,     // __TEXT
        0x4000ULL,  // __DATA_CONST
        0x8000ULL,  // __DATA
        0xc000ULL   // __LINKEDIT
    };
    std::vector<DyldBindingRecord> midBindings;
    if (mid_bind_sz > 0) {
        TEST_ASSERT(DyldEngine::parse_binding_stream(midBytes.data() + mid_bind_off, mid_bind_sz, mid_seg_vmaddrs, midBindings, err),
                    "Parse mid bindings");
    }
    if (mid_lazy_sz > 0) {
        TEST_ASSERT(DyldEngine::parse_binding_stream(midBytes.data() + mid_lazy_off, mid_lazy_sz, mid_seg_vmaddrs, midBindings, err),
                    "Parse mid lazy bindings");
    }

    // 5. Parse bindings for main_chain (binds _mid_compute)
    const std::vector<std::uint64_t> main_seg_vmaddrs = {
        0x0ULL,
        0x100000000ULL,
        0x100004000ULL,
        0x100008000ULL,
        0x10000c000ULL
    };
    std::vector<DyldBindingRecord> mainBindings;
    if (main_bind_sz > 0) {
        TEST_ASSERT(DyldEngine::parse_binding_stream(mainBytes.data() + main_bind_off, main_bind_sz, main_seg_vmaddrs, mainBindings, err),
                    "Parse main bindings");
    }
    if (main_lazy_sz > 0) {
        TEST_ASSERT(DyldEngine::parse_binding_stream(mainBytes.data() + main_lazy_off, main_lazy_sz, main_seg_vmaddrs, mainBindings, err),
                    "Parse main lazy bindings");
    }

    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/main_chain";
    config.max_instructions = 10000;
    config.pre_execution_hook = [&](Memory& mem) {
        // Initialize stack guard canary for main_chain
        mem.write_u64(0x1340ULL, 0xBADC0FFEE0DDF00DULL);
        mem.write_u64(0x4008ULL, 0x1340ULL); // GOT slot for ___stack_chk_guard in main_chain

        // Apply bindings for libmid into libmid GOT (slid by kMidBase)
        for (auto& b : midBindings) {
            auto resolved = dyld.resolve_symbol(b.symbol_name);
            if (resolved.has_value()) {
                std::uint64_t target_vaddr = (kMidBase + b.patch_vaddr) % mem.size();
                mem.write_u64(target_vaddr, *resolved);
                std::cout << "    [bind mid] " << b.symbol_name << " -> 0x" << std::hex << *resolved
                          << " written to 0x" << target_vaddr << std::dec << "\n";
            } else {
                std::cout << "    [bind mid UNRESOLVED] " << b.symbol_name << "\n";
            }
        }

        // Apply bindings for main_chain into main_chain GOT
        for (auto& b : mainBindings) {
            auto resolved = dyld.resolve_symbol(b.symbol_name);
            if (resolved.has_value()) {
                std::uint64_t target_vaddr = b.patch_vaddr % mem.size();
                mem.write_u64(target_vaddr, *resolved);
                std::cout << "    [bind main] " << b.symbol_name << " -> 0x" << std::hex << *resolved
                          << " written to 0x" << target_vaddr << std::dec << "\n";
            } else {
                std::cout << "    [bind main UNRESOLVED] " << b.symbol_name << "\n";
            }
        }
    };

    auto result = engine.execute_macho(mainBytes.data(), mainBytes.size(), config, 601);

    std::cout << "  Guest instructions retired: " << result.guest_instructions_retired << "\n";
    std::cout << "  Process exited: " << (result.process_exited ? "true" : "false")
              << ", Exit code: " << result.exit_code << "\n";
    std::cout << "  Captured stdout: \"" << result.stdout_output << "\"\n";
    std::cout << "  BSD syscalls handled: " << result.bsd_syscalls_handled << "\n";
    if (!result.error_message.empty()) {
        std::cout << "  Error message: " << result.error_message << "\n";
    }
    std::cout << "  Final PC: 0x" << std::hex << cpu.get_program_counter() << std::dec << "\n";
    std::cout << "  ELR_EL1: 0x" << std::hex << cpu.get_elr_el1()
              << ", ESR_EL1: 0x" << cpu.get_esr_el1()
              << ", FAR_EL1: 0x" << cpu.get_far_el1() << std::dec << "\n";

    TEST_ASSERT(result.guest_instructions_retired > 0, "Guest instructions must be retired");
    TEST_ASSERT(result.process_exited, "Process must cleanly exit");
    TEST_ASSERT(result.exit_code == 0, "Exit code must be 0 for valid chain evaluation");
    TEST_ASSERT(result.stdout_output == kExpectedStdout, "Output must match CHAIN_OK: 142");

    return true;
}

// Test 3: Negative Test: Missing Leaf Dylib Resolution Fails Gracefully
bool test_negative_missing_leaf_dylib() {
    std::cout << "[Test 3] Negative Test: Missing Leaf Dylib Resolution...\n";

    DyldEngine dyld;
    auto resolved = dyld.resolve_symbol("_leaf_mul");
    TEST_ASSERT(!resolved.has_value(), "Missing leaf dylib must fail symbol resolution");

    std::string err;
    std::vector<std::string> rpaths = {"@loader_path"};
    std::string resolved_path = DyldEngine::resolve_rpath_path("@rpath/non_existent.dylib", rpaths, "/tmp", "/tmp");
    TEST_ASSERT(resolved_path.empty(), "Non-existent dylib path must return empty");

    return true;
}

// Test 4: Negative Test: Bad Rpath / Cycle / Truncation Rejection
bool test_negative_bad_rpath_and_corruption() {
    std::cout << "[Test 4] Negative Test: Bad Rpath and Export Trie Corruption...\n";

    std::vector<std::string> rpaths = {"/invalid/directory/that/does/not/exist"};
    std::string res = DyldEngine::resolve_rpath_path("@rpath/libleaf.dylib", rpaths, "/tmp", "/tmp");
    TEST_ASSERT(res.empty(), "Bad rpath must return empty path");

    // Corrupted export trie
    std::uint8_t corrupt_trie[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    std::vector<DyldExportedSymbolInfo> symbols;
    std::string err;
    bool ok = DyldEngine::parse_export_trie(corrupt_trie, sizeof(corrupt_trie), symbols, err);
    TEST_ASSERT(!ok, "Corrupt export trie must be rejected");
    TEST_ASSERT(!err.empty(), "Error message must describe corruption");

    return true;
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "STAGE T2: THREE-IMAGE DEPENDENCY CHAIN WITH @rpath\n";
    std::cout << "====================================================\n";

    bool pass = true;
    pass = test_three_image_metadata_and_rpaths() && pass;
    pass = test_three_image_transitive_execution() && pass;
    pass = test_negative_missing_leaf_dylib() && pass;
    pass = test_negative_bad_rpath_and_corruption() && pass;

    std::cout << "====================================================\n";
    if (pass) {
        std::cout << "[STAGE T2 TESTS] ALL PASSED\n";
        return 0;
    } else {
        std::cerr << "[STAGE T2 TESTS] FAILED\n";
        return 1;
    }
}

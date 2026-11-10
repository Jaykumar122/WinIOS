// =============================================================================
// stage_v0_chained_fixups_tests.cpp
//
// Campaign 191-V Stage V0 Item 5:
//   - Verifies guest-side LC_DYLD_CHAINED_FIXUPS (DYLD_CHAINED_PTR_64) in minidyld
//   - Tests a genuine aarch64-ios binary (ios_chain_app) containing >= 3 rebases
//     (pointer-to-global gp_counter + global fn pointer array g_fn_table[2])
//     and >= 1 bind (g_ext_fn imported from real iOS dylib libioschain.dylib)
//   - Asserts every patched guest pointer against independently parsed Mach-O
//     ground truth AND differentially against host DyldEngine::process_chained_fixups
//   - Verifies non-DYLD_CHAINED_PTR_64 pointer formats fail cleanly (UNTESTED)
// =============================================================================

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
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

constexpr const char* kMinidyldPath     = "tests/artifacts/bin/minidyld";
constexpr const char* kIosChainAppPath  = "tests/artifacts/bin/ios_chain_app";
constexpr const char* kLibIosChainPath  = "tests/artifacts/bin/libioschain.dylib";

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
    std::string p = resolve_path(rel);
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f.good()) return {};
    auto sz = f.tellg();
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

std::uint32_t read_u32(const std::uint8_t* p) {
    std::uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

std::uint64_t read_u64(const std::uint8_t* p) {
    std::uint64_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

struct ParsedChainedInfo {
    std::uint32_t platform{0};
    std::uint32_t fixups_off{0};
    std::uint32_t fixups_size{0};
    std::uint32_t rebase_size{0};
    std::uint32_t bind_size{0};
    std::uint32_t lazy_size{0};
    std::uint32_t export_off{0};
    std::uint32_t export_size{0};
    std::vector<std::pair<std::uint64_t, std::uint64_t>> raw_rebases; // (guest_ram_loc, unslid_target)
    std::vector<std::pair<std::uint64_t, std::string>> raw_binds;     // (guest_ram_loc, sym_name)
    std::unordered_map<std::string, std::uint64_t> symtab_addrs;
};

ParsedChainedInfo inspect_macho_fixups_and_syms(const std::vector<std::uint8_t>& bin) {
    ParsedChainedInfo info{};
    if (bin.size() < 32) return info;
    const std::uint32_t ncmds = read_u32(bin.data() + 16);
    std::size_t off = 32;
    for (std::uint32_t i = 0; i < ncmds && off + 8 <= bin.size(); ++i) {
        const std::uint32_t cmd = read_u32(bin.data() + off);
        const std::uint32_t cmdsize = read_u32(bin.data() + off + 4);
        if (cmdsize == 0 || off + cmdsize > bin.size()) break;
        if (cmd == 0x80000034u) { // LC_DYLD_CHAINED_FIXUPS
            info.fixups_off = read_u32(bin.data() + off + 8);
            info.fixups_size = read_u32(bin.data() + off + 12);
        } else if (cmd == 0x32u && cmdsize >= 16) { // LC_BUILD_VERSION
            info.platform = read_u32(bin.data() + off + 8);
        } else if (cmd == 0x80000022u || cmd == 0x22u) { // LC_DYLD_INFO_ONLY
            info.rebase_size = read_u32(bin.data() + off + 12);
            info.bind_size   = read_u32(bin.data() + off + 20);
            info.lazy_size   = read_u32(bin.data() + off + 36);
            info.export_off  = read_u32(bin.data() + off + 40);
            info.export_size = read_u32(bin.data() + off + 44);
        } else if (cmd == 0x2u && cmdsize >= 24) { // LC_SYMTAB
            const std::uint32_t symoff = read_u32(bin.data() + off + 8);
            const std::uint32_t nsyms  = read_u32(bin.data() + off + 12);
            const std::uint32_t stroff = read_u32(bin.data() + off + 16);
            const std::uint32_t strsize = read_u32(bin.data() + off + 20);
            for (std::uint32_t s = 0; s < nsyms; ++s) {
                const std::size_t nl = static_cast<std::size_t>(symoff) + static_cast<std::size_t>(s) * 16;
                if (nl + 16 > bin.size()) break;
                const std::uint32_t strx = read_u32(bin.data() + nl);
                const std::uint64_t val  = read_u64(bin.data() + nl + 8);
                if (strx < strsize && static_cast<std::size_t>(stroff) + strx < bin.size()) {
                    const char* nm = reinterpret_cast<const char*>(bin.data() + stroff + strx);
                    if (nm[0] != '\0' && val != 0) {
                        info.symtab_addrs[nm] = val;
                    }
                }
            }
        }
        off += cmdsize;
    }

    if (info.fixups_off > 0 && static_cast<std::size_t>(info.fixups_off) + info.fixups_size <= bin.size()) {
        const std::uint8_t* blob = bin.data() + info.fixups_off;
        DyldChainedFixupsHeaderRaw hdr{};
        std::memcpy(&hdr, blob, sizeof(hdr));
        const std::uint32_t seg_count = read_u32(blob + hdr.starts_offset);
        for (std::uint32_t s = 0; s < seg_count; ++s) {
            const std::uint32_t soff = read_u32(blob + hdr.starts_offset + 4 + s * 4);
            if (soff == 0) continue;
            DyldChainedStartsInSegmentRaw seg{};
            std::memcpy(&seg, blob + hdr.starts_offset + soff, sizeof(seg));
            if (seg.pointer_format != DYLD_CHAINED_PTR_64) continue;
            const auto* pstarts = reinterpret_cast<const std::uint16_t*>(
                blob + hdr.starts_offset + soff + sizeof(DyldChainedStartsInSegmentRaw));
            for (std::uint16_t p = 0; p < seg.page_count; ++p) {
                const std::uint16_t ps = pstarts[p];
                if (ps == DYLD_CHAINED_PTR_START_NONE) continue;
                std::uint64_t loc = seg.segment_offset + static_cast<std::uint64_t>(p) * seg.page_size + (ps & 0x7FFFu);
                while (loc + 8 <= bin.size()) {
                    const std::uint64_t raw = read_u64(bin.data() + loc);
                    const std::uint64_t next = (raw >> 51) & 0xFFFULL;
                    const bool is_bind = ((raw >> 63) & 1ULL) != 0;
                    if (is_bind) {
                        const std::uint32_t ord = static_cast<std::uint32_t>(raw & 0xFFFFFFULL);
                        DyldChainedImportRaw imp{};
                        std::memcpy(&imp, blob + hdr.imports_offset + ord * sizeof(DyldChainedImportRaw), sizeof(imp));
                        const char* sym = reinterpret_cast<const char*>(blob + hdr.symbols_offset + imp.name_offset);
                        info.raw_binds.emplace_back(loc, sym);
                    } else {
                        const std::uint64_t target = raw & 0xFFFFFFFFFULL;
                        info.raw_rebases.emplace_back(loc, target);
                    }
                    if (next == 0) break;
                    loc += next * 4;
                }
            }
        }
    }
    return info;
}

bool test_provenance_and_chained_payload() {
    std::cout << "[Test 1] ios_chain_app & libioschain.dylib Provenance + Chained Fixup Structure...\n";

    auto app = read_binary_file(kIosChainAppPath);
    auto lib = read_binary_file(kLibIosChainPath);
    TEST_ASSERT(!app.empty() && !lib.empty(), "iOS chained fixup binaries must exist");

    UserspaceLoadResult app_meta, lib_meta;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(app.data(), app.size(), app_meta), "parse ios_chain_app");
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(lib.data(), lib.size(), lib_meta), "parse libioschain.dylib");

    const auto info = inspect_macho_fixups_and_syms(app);
    const auto lib_parsed = inspect_macho_fixups_and_syms(lib);
    TEST_ASSERT(info.platform == 2u, "ios_chain_app must have PLATFORM_IOS (2)");
    TEST_ASSERT(lib_parsed.platform == 2u, "libioschain.dylib must have PLATFORM_IOS (2)");
    std::cout << "  ios_chain_app: " << app.size() << " B, SHA-256="
              << RealArtifactHarness::compute_sha256(app.data(), app.size()) << "\n";
    std::cout << "  libioschain:   " << lib.size() << " B, SHA-256="
              << RealArtifactHarness::compute_sha256(lib.data(), lib.size()) << "\n";
    std::cout << "  fixups blob:   " << info.fixups_size << " B @ 0x" << std::hex << info.fixups_off
              << std::dec << ", rebases=" << info.raw_rebases.size()
              << ", binds=" << info.raw_binds.size() << "\n";

    TEST_ASSERT(info.fixups_off > 0 && info.fixups_size > 0, "Must contain LC_DYLD_CHAINED_FIXUPS");
    TEST_ASSERT(info.rebase_size == 0 && info.bind_size == 0 && info.lazy_size == 0,
                "LC_DYLD_INFO_ONLY rebase/bind/lazy must be 0 so only chained fixups run");
    TEST_ASSERT(info.raw_rebases.size() >= 3, "Must have >= 3 NONZERO rebases (gp_counter + g_fn_table[2])");
    TEST_ASSERT(!info.raw_binds.empty(), "Must have >= 1 NONZERO bind (_ios_chain_add)");
    return true;
}

bool test_guest_and_differential_chained_fixups() {
    std::cout << "[Test 2] Guest EL0 Chained Fixups Execution + Differential Proof vs Host DyldEngine...\n";

    auto dyld_bytes = read_binary_file(kMinidyldPath);
    auto app_bytes  = read_binary_file(kIosChainAppPath);
    auto lib_bytes  = read_binary_file(kLibIosChainPath);
    TEST_ASSERT(!dyld_bytes.empty() && !app_bytes.empty() && !lib_bytes.empty(), "Missing artifacts");

    const auto app_info = inspect_macho_fixups_and_syms(app_bytes);
    const auto lib_info = inspect_macho_fixups_and_syms(lib_bytes);

    // 1. Run completely via guest minidyld in EL0
    CPU cpu;
    Memory guest_mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, guest_mem, bsd, ipc);
    bsd.vfs().write_file("/usr/lib/libioschain.dylib", lib_bytes.data(), lib_bytes.size());

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/usr/bin/ios_chain_app";
    cfg.argv = {"/usr/bin/ios_chain_app"};
    cfg.envp = {};
    cfg.stack_top = 0x03F00000ULL;
    cfg.max_instructions = 200000;

    auto res = engine.execute_macho_with_guest_dyld(
        dyld_bytes.data(), dyld_bytes.size(),
        app_bytes.data(), app_bytes.size(),
        cfg, 801);

    std::cout << "  Guest retired=" << res.guest_instructions_retired
              << ", exited=" << (res.process_exited ? "true" : "false")
              << ", code=" << res.exit_code
              << ", stdout=" << res.stdout_output;
    if (!res.error_message.empty()) {
        std::cerr << "  Error: " << res.error_message << "\n";
    }

    TEST_ASSERT(res.process_exited && res.exit_code == 0, "ios_chain_app must exit cleanly with code 0");
    TEST_ASSERT(res.stdout_output == "IOS_CHAIN: 107,214,656,23\n",
                "ios_chain_app output must match host-computed arithmetic (107,214,656,23)");

    // 2. Prepare independent host DyldEngine image on separate host_mem
    UserspaceLoadResult app_meta, lib_meta;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(app_bytes.data(), app_bytes.size(), app_meta), "app meta");
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(lib_bytes.data(), lib_bytes.size(), lib_meta), "lib meta");

    Memory host_mem(64 * 1024 * 1024);
    for (const auto& seg : app_meta.segments) {
        if (seg.is_pagezero || seg.filesize == 0) continue;
        std::uint64_t t = seg.vmaddr % host_mem.size();
        for (std::size_t i = 0; i < seg.filesize; ++i) {
            host_mem.write_u8(t + i, app_bytes[seg.fileoff + i]);
        }
    }

    // Parse libioschain.dylib exports & register at 0x20000 (first dylib slot in minidyld)
    constexpr std::uint64_t kDylibBase = 0x20000ULL;
    std::vector<DyldExportedSymbolInfo> lib_exports;
    std::string dyld_err;
    TEST_ASSERT(DyldEngine::parse_export_trie(
        lib_bytes.data() + lib_info.export_off, lib_info.export_size, lib_exports, dyld_err),
        ("parse libioschain export trie: " + dyld_err).c_str());

    DyldEngine host_dyld;
    DyldLoadedModule sys_mod{};
    sys_mod.name = "libSystem.B.dylib";
    sys_mod.install_name = "/usr/lib/libSystem.B.dylib";
    host_dyld.register_module(sys_mod);

    DyldLoadedModule lib_mod{};
    lib_mod.name = "libioschain.dylib";
    lib_mod.install_name = "/usr/lib/libioschain.dylib";
    lib_mod.base_address = kDylibBase;
    lib_mod.slide = kDylibBase;
    for (const auto& ex : lib_exports) {
        lib_mod.exports[ex.name] = ex.address;
    }
    host_dyld.register_module(lib_mod);

    // Run host DyldEngine::process_chained_fixups on main image
    // Main image mapped at 0x0 (slide = -0x100000000 relative to 0x100000000)
    auto status = host_dyld.process_chained_fixups(
        app_bytes.data() + app_info.fixups_off,
        app_info.fixups_size,
        host_mem,
        0x100000000ULL,
        static_cast<std::uint64_t>(-0x100000000LL),
        dyld_err);
    TEST_ASSERT(status == DyldLinkStatus::Success, ("Host process_chained_fixups failed: " + dyld_err).c_str());

    // 3. Assert every guest-patched rebase & bind pointer against BOTH:
    //    (a) independently computed Mach-O symtab / export-trie value
    //    (b) host-emulated DyldEngine patched pointer
    for (const auto& [loc, raw_target] : app_info.raw_rebases) {
        const std::uint64_t guest_ptr = guest_mem.read_u64(loc);
        const std::uint64_t host_ptr  = host_mem.read_u64(loc);
        const std::uint64_t expected  = raw_target - 0x100000000ULL;
        std::cout << "  REBASE slot @ 0x" << std::hex << loc
                  << ": guest=0x" << guest_ptr << " host=0x" << host_ptr
                  << " expected=0x" << expected << std::dec << "\n";
        TEST_ASSERT(guest_ptr == expected, "Guest chained rebase pointer mismatch vs ground truth");
        TEST_ASSERT(guest_ptr == host_ptr, "Guest chained rebase mismatch vs host DyldEngine");
    }

    for (const auto& [loc, sym_name] : app_info.raw_binds) {
        const std::uint64_t guest_ptr = guest_mem.read_u64(loc);
        const std::uint64_t host_ptr  = host_mem.read_u64(loc);
        std::uint64_t sym_off = 0;
        for (const auto& ex : lib_exports) {
            if (ex.name == sym_name) sym_off = ex.address;
        }
        TEST_ASSERT(sym_off != 0, "Expected export must exist in libioschain.dylib");
        const std::uint64_t expected = kDylibBase + sym_off;
        std::cout << "  BIND   slot @ 0x" << std::hex << loc << " (" << sym_name
                  << "): guest=0x" << guest_ptr << " host=0x" << host_ptr
                  << " expected=0x" << expected << std::dec << "\n";
        TEST_ASSERT(guest_ptr == expected, "Guest chained bind pointer mismatch vs ground truth");
        TEST_ASSERT(guest_ptr == host_ptr, "Guest chained bind mismatch vs host DyldEngine");
    }

    return true;
}

bool test_unsupported_pointer_format_rejected() {
    std::cout << "[Test 3] Unsupported Chained Pointer Format (ARM64E) Cleanly Rejected...\n";

    auto dyld_bytes = read_binary_file(kMinidyldPath);
    auto app_bytes  = read_binary_file(kIosChainAppPath);
    auto lib_bytes  = read_binary_file(kLibIosChainPath);
    const auto info = inspect_macho_fixups_and_syms(app_bytes);
    TEST_ASSERT(info.fixups_off > 0, "fixups_off required");

    // Mutate pointer_format in first valid segment from DYLD_CHAINED_PTR_64 (2) -> ARM64E (1)
    DyldChainedFixupsHeaderRaw hdr{};
    std::memcpy(&hdr, app_bytes.data() + info.fixups_off, sizeof(hdr));
    const std::uint32_t seg_count = read_u32(app_bytes.data() + info.fixups_off + hdr.starts_offset);
    bool mutated = false;
    for (std::uint32_t s = 0; s < seg_count; ++s) {
        const std::uint32_t soff = read_u32(app_bytes.data() + info.fixups_off + hdr.starts_offset + 4 + s * 4);
        if (soff != 0) {
            const std::size_t fmt_pos = info.fixups_off + hdr.starts_offset + soff + 6;
            app_bytes[fmt_pos] = static_cast<std::uint8_t>(DYLD_CHAINED_PTR_ARM64E);
            app_bytes[fmt_pos + 1] = 0;
            mutated = true;
            break;
        }
    }
    TEST_ASSERT(mutated, "Mutated segment pointer_format");

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);
    bsd.vfs().write_file("/usr/lib/libioschain.dylib", lib_bytes.data(), lib_bytes.size());

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/usr/bin/ios_chain_app_arm64e";
    cfg.argv = {"/usr/bin/ios_chain_app_arm64e"};
    cfg.stack_top = 0x03F00000ULL;
    cfg.max_instructions = 200000;

    auto res = engine.execute_macho_with_guest_dyld(
        dyld_bytes.data(), dyld_bytes.size(),
        app_bytes.data(), app_bytes.size(),
        cfg, 802);

    std::cout << "  Guest exit_code=" << res.exit_code << ", stdout=" << res.stdout_output;
    TEST_ASSERT(res.process_exited && res.exit_code == 125,
                "minidyld must cleanly exit with code 125 on untested chained pointer format");
    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE V0: Guest Chained Fixups (DYLD_CHAINED_PTR_64) Tests\n";
    std::cout << "=========================================================\n";

    int passed = 0, failed = 0;
    auto run = [&](auto fn, const char* name) {
        if (fn()) {
            std::cout << "[PASS] " << name << "\n";
            passed++;
        } else {
            std::cout << "[FAIL] " << name << "\n";
            failed++;
        }
    };

    run(test_provenance_and_chained_payload, "Provenance & Chained Fixups Payload");
    run(test_guest_and_differential_chained_fixups, "Guest Execution + Host Differential Proof");
    run(test_unsupported_pointer_format_rejected, "Unsupported Pointer Format Guard (ARM64E UNTESTED)");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}

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

constexpr const char* kArtifactPath = "tests/artifacts/bin/compute_ios_O2";
constexpr const char* kExpectedSha256 = "60ef08485cacbf23238616c725740bbc17a8ba729a44e5eaa33ab8d1b893df02";
constexpr std::size_t kExpectedSize = 50545;
constexpr const char* kExpectedStdout = "COMPUTE_OK: 0xCAFEBABE13438009\n";

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

} // namespace

// Test 1: Verify iOS Artifact Provenance & Platform Metadata (PLATFORM_IOS = 2)
bool test_ios_artifact_provenance_and_metadata() {
    std::cout << "[Test 1] Verify iOS Artifact Provenance and Metadata (REAL-006)...\n";

    auto bytes = read_binary_file(kArtifactPath);
    TEST_ASSERT(!bytes.empty(), "Failed to read compute_ios_O2 from disk");
    TEST_ASSERT(bytes.size() == kExpectedSize, "File size must match expected");

    UserspaceLoadResult load_res;
    bool ok = UserspaceMachOLoader::parse_metadata(bytes.data(), bytes.size(), load_res);
    TEST_ASSERT(ok, "Mach-O metadata parsing must succeed");
    TEST_ASSERT(load_res.header.cputype == 0x0100000C, "CPU type must be ARM64 (0x0100000C)");

    // Inspect LC_BUILD_VERSION directly
    bool found_ios_platform = false;
    std::uint32_t minos_val = 0;
    std::uint32_t sdk_val = 0;

    std::size_t offset = 32;
    std::uint32_t ncmds = *reinterpret_cast<const std::uint32_t*>(bytes.data() + 16);
    for (std::uint32_t i = 0; i < ncmds; ++i) {
        std::uint32_t cmd = *reinterpret_cast<const std::uint32_t*>(bytes.data() + offset);
        std::uint32_t cmdsize = *reinterpret_cast<const std::uint32_t*>(bytes.data() + offset + 4);
        if (cmd == 0x32) { // LC_BUILD_VERSION
            std::uint32_t platform = *reinterpret_cast<const std::uint32_t*>(bytes.data() + offset + 8);
            minos_val = *reinterpret_cast<const std::uint32_t*>(bytes.data() + offset + 12);
            sdk_val = *reinterpret_cast<const std::uint32_t*>(bytes.data() + offset + 16);
            if (platform == 2) { // PLATFORM_IOS
                found_ios_platform = true;
            }
        }
        offset += cmdsize;
    }

    TEST_ASSERT(found_ios_platform, "Mach-O must contain LC_BUILD_VERSION with PLATFORM_IOS (2)");
    std::cout << "  Platform: PLATFORM_IOS (2)\n";
    std::cout << "  Min OS: 0x" << std::hex << minos_val << " (iOS 18.0.0)\n";
    std::cout << "  SDK: 0x" << sdk_val << std::dec << "\n";

    return true;
}

// Test 2: Execute REAL-006 (compute_ios_O2) on DarwinUserspaceEngine
bool test_execute_compute_ios_o2() {
    std::cout << "[Test 2] Execute Genuine iOS-Targeted Mach-O (compute_ios_O2)...\n";

    auto bytes = read_binary_file(kArtifactPath);
    TEST_ASSERT(!bytes.empty(), "Artifact must be read");

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/compute_ios_O2";
    config.max_instructions = 10000;

    auto result = engine.execute_macho(bytes.data(), bytes.size(), config, 501);

    std::cout << "  Guest instructions retired: " << result.guest_instructions_retired << "\n";
    std::cout << "  Process exited: " << (result.process_exited ? "true" : "false")
              << ", Exit code: " << result.exit_code << "\n";
    std::cout << "  Captured stdout: \"" << result.stdout_output << "\"\n";
    std::cout << "  BSD syscalls handled: " << result.bsd_syscalls_handled << "\n";

    TEST_ASSERT(result.guest_instructions_retired > 0, "Guest instructions must be retired");
    TEST_ASSERT(result.process_exited, "Process must cleanly exit");
    TEST_ASSERT(result.exit_code == 0, "Exit code must be 0");
    TEST_ASSERT(result.stdout_output == kExpectedStdout, "Captured stdout must match expected format");
    TEST_ASSERT(result.bsd_syscalls_handled == 4, "Must handle 4 BSD syscalls (3x write, 1x exit)");

    return true;
}

// Test 3: DyldEngine Chained Fixups Rebase & Bind Conformance
bool test_chained_fixups_engine_conformance() {
    std::cout << "[Test 3] DyldEngine Chained Fixups (LC_DYLD_CHAINED_FIXUPS) Conformance...\n";

    Memory memory(64 * 1024 * 1024);
    DyldEngine dyld;
    dyld.register_builtin_system_libraries();

    // Register a mock module with known exports for binding
    DyldLoadedModule test_mod;
    test_mod.name = "libchaintest.dylib";
    test_mod.install_name = "/usr/lib/libchaintest.dylib";
    test_mod.base_address = 0x0000000100000000ULL;
    test_mod.exports["_test_symbol_a"] = 0x0000000100004500ULL;
    test_mod.exports["_test_symbol_b"] = 0x0000000100007800ULL;
    dyld.register_module(std::move(test_mod));

    // Construct valid DyldChainedFixupsHeaderRaw payload
    // Header (32 bytes) + StartsInImage (8 bytes + 4 bytes starts[0]) + StartsInSegment (32 bytes + page_starts) + Imports + Symbols
    #pragma pack(push, 1)
    struct ChainedFixupPayload {
        DyldChainedFixupsHeaderRaw hdr;
        std::uint32_t seg_count;
        std::uint32_t seg_info_offset[1];
        // Segment 0 starts
        DyldChainedStartsInSegmentRaw seg_hdr;
        std::uint16_t page_starts[1];
        std::uint16_t page_starts_pad;
        // Imports (2 imports)
        DyldChainedImportRaw imp[2];
        // Symbol pool: "\0_test_symbol_a\0_test_symbol_b\0"
        char symbols[32];
    } payload{};
    #pragma pack(pop)

    payload.hdr.fixups_version = 0;
    payload.hdr.starts_offset = static_cast<std::uint32_t>(offsetof(ChainedFixupPayload, seg_count));
    payload.hdr.imports_offset = static_cast<std::uint32_t>(offsetof(ChainedFixupPayload, imp));
    payload.hdr.symbols_offset = static_cast<std::uint32_t>(offsetof(ChainedFixupPayload, symbols));
    payload.hdr.imports_count = 2;
    payload.hdr.symbols_format = 0;
    payload.hdr.imports_format = 1; // 1 = DYLD_CHAINED_IMPORT

    payload.seg_count = 1;
    payload.seg_info_offset[0] = static_cast<std::uint32_t>(
        offsetof(ChainedFixupPayload, seg_hdr) - offsetof(ChainedFixupPayload, seg_count));

    payload.seg_hdr.size = sizeof(DyldChainedStartsInSegmentRaw) + sizeof(std::uint16_t) * 2;
    payload.seg_hdr.page_size = 0x4000; // 16 KB iOS page size
    payload.seg_hdr.pointer_format = DYLD_CHAINED_PTR_64;
    payload.seg_hdr.segment_offset = 0x8000; // __DATA at 0x8000
    payload.seg_hdr.max_valid_pointer = 0;
    payload.seg_hdr.page_count = 1;
    payload.page_starts[0] = 0x0000; // First fixup at offset 0 of page (0x8000)

    // Symbol pool
    const char* sym_a = "_test_symbol_a";
    const char* sym_b = "_test_symbol_b";
    std::memcpy(payload.symbols + 1, sym_a, std::strlen(sym_a) + 1);
    std::memcpy(payload.symbols + 1 + std::strlen(sym_a) + 1, sym_b, std::strlen(sym_b) + 1);

    // Import 0: _test_symbol_a
    payload.imp[0].lib_ordinal = 0; // search across all
    payload.imp[0].weak_import = 0;
    payload.imp[0].name_offset = 1;

    // Import 1: _test_symbol_b
    payload.imp[1].lib_ordinal = 0;
    payload.imp[1].weak_import = 0;
    payload.imp[1].name_offset = static_cast<std::uint32_t>(1 + std::strlen(sym_a) + 1);

    // Write a chain in guest memory:
    // Slot 1 (at 0x8000): Rebase pointer (pointing to target 0x1000 + next chain at +8 bytes)
    // Slot 2 (at 0x8008): Bind pointer (import 0 -> _test_symbol_a, next chain at +8 bytes)
    // Slot 3 (at 0x8010): Bind pointer (import 1 -> _test_symbol_b, next = 0 terminal)
    constexpr std::uint64_t kBaseVaddr = 0x100000000ULL;
    constexpr std::uint64_t kSlide = 0x20000ULL;

    // In DYLD_CHAINED_PTR_64:
    // Bit 63: 0 = rebase, 1 = bind
    // Rebase: bits 35:0 target (up to 36 bits, e.g. 0x100001500), bits 43:36 high8 (bits 63:56), bits 62:51 next stride
    std::uint64_t rebase_raw = 0x100001500ULL | (2ULL << 51); // target = 0x100001500, next = 2 words (8 bytes)
    // Bind: bit 63 = 1, bits 15:0 ordinal, bits 50:51 next
    std::uint64_t bind_raw_0 = 0x8000000000000000ULL | 0ULL | (2ULL << 51); // import 0, next = 8 bytes
    std::uint64_t bind_raw_1 = 0x8000000000000000ULL | 1ULL | (0ULL << 51); // import 1, next = 0 (terminal)

    memory.write_u64(0x8000, rebase_raw);
    memory.write_u64(0x8008, bind_raw_0);
    memory.write_u64(0x8010, bind_raw_1);

    std::string err;
    auto status = dyld.process_chained_fixups(&payload, sizeof(payload), memory, kBaseVaddr, kSlide, err);
    TEST_ASSERT(status == DyldLinkStatus::Success, "process_chained_fixups must succeed");
    TEST_ASSERT(err.empty(), "Error message must be empty");

    // Verify Memory Patches:
    // Slot 1: Rebase -> kBaseVaddr + target + kSlide = 0x100000000 + 0x100001500 (modulo) + 0x20000 = 0x100021500
    std::uint64_t patched_rebase = memory.read_u64(0x8000);
    TEST_ASSERT(patched_rebase == 0x100021500ULL, "Rebase pointer must equal base + target + slide");

    // Slot 2: Bind 0 -> _test_symbol_a = 0x100004500
    std::uint64_t patched_bind_0 = memory.read_u64(0x8008);
    TEST_ASSERT(patched_bind_0 == 0x100004500ULL, "Bind 0 must equal resolved address of _test_symbol_a");

    // Slot 3: Bind 1 -> _test_symbol_b = 0x100007800
    std::uint64_t patched_bind_1 = memory.read_u64(0x8010);
    TEST_ASSERT(patched_bind_1 == 0x100007800ULL, "Bind 1 must equal resolved address of _test_symbol_b");

    std::cout << "  Chained rebase slot 0x8000 -> 0x" << std::hex << patched_rebase << std::dec << " (VERIFIED)\n";
    std::cout << "  Chained bind slot 0x8008   -> 0x" << std::hex << patched_bind_0 << std::dec << " (VERIFIED)\n";
    std::cout << "  Chained bind slot 0x8010   -> 0x" << std::hex << patched_bind_1 << std::dec << " (VERIFIED)\n";

    return true;
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "STAGE T1: iOS-TARGET BINARIES & REAL CHAINED FIXUPS\n";
    std::cout << "====================================================\n";

    bool pass = true;
    pass = test_ios_artifact_provenance_and_metadata() && pass;
    pass = test_execute_compute_ios_o2() && pass;
    pass = test_chained_fixups_engine_conformance() && pass;

    std::cout << "====================================================\n";
    if (pass) {
        std::cout << "[STAGE T1 TESTS] ALL PASSED\n";
        return 0;
    } else {
        std::cerr << "[STAGE T1 TESTS] FAILED\n";
        return 1;
    }
}

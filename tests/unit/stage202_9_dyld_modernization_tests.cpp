// ==============================================================================
// WinIOS Campaign 202 Stage 9: Dynamic Linker Modernization Tests
// ==============================================================================
// Verifies:
// 1. Mach-O chained fixup header, segments, and page starts bounds validation
// 2. DYLD_CHAINED_PTR_64 rebase and bind chain processing
// 3. DYLD_CHAINED_PTR_ARM64E decoding and PAC pointer authentication signing
// 4. Multi-library two-level symbol binding and weak import resolution
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/pointer_auth.h"
#include "loader/dyld_engine.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::loader;
using namespace winios::cpu;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Chained Fixup Header & Bounds Validation (Negative Tests)
// -----------------------------------------------------------------------------
void test_chained_fixups_header_and_bounds_validation() {
    std::cout << "[Test 1] Chained Fixups Header & Bounds Validation...\n";
    DyldEngine engine;
    Memory mem(0x100000ULL); // 1 MB
    std::string err;

    // 1. Truncated buffer
    std::vector<std::uint8_t> truncated(16, 0);
    auto st1 = engine.process_chained_fixups(truncated.data(), truncated.size(), mem, 0x10000ULL, 0x0ULL, err);
    TEST_CHECK(st1 == DyldLinkStatus::InvalidChainedFixups, "Truncated header must be rejected");

    // 2. Unsupported fixups version
    std::vector<std::uint8_t> bad_ver(sizeof(DyldChainedFixupsHeaderRaw), 0);
    auto* hdr_bad = reinterpret_cast<DyldChainedFixupsHeaderRaw*>(bad_ver.data());
    hdr_bad->fixups_version = 99; // Invalid version
    auto st2 = engine.process_chained_fixups(bad_ver.data(), bad_ver.size(), mem, 0x10000ULL, 0x0ULL, err);
    TEST_CHECK(st2 == DyldLinkStatus::InvalidChainedFixups, "Unsupported fixups version must be rejected");

    // 3. Out-of-bounds imports offset
    std::vector<std::uint8_t> oob_imports(sizeof(DyldChainedFixupsHeaderRaw) + 64, 0);
    auto* hdr_oob = reinterpret_cast<DyldChainedFixupsHeaderRaw*>(oob_imports.data());
    hdr_oob->fixups_version = 0;
    hdr_oob->imports_offset = 0xFFFF0000u; // Way outside buffer
    hdr_oob->imports_count = 1;
    auto st3 = engine.process_chained_fixups(oob_imports.data(), oob_imports.size(), mem, 0x10000ULL, 0x0ULL, err);
    TEST_CHECK(st3 == DyldLinkStatus::InvalidChainedFixups, "Out-of-bounds imports offset must be rejected");

    std::cout << "  -> Header & bounds validation passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Standard DYLD_CHAINED_PTR_64 Rebase and Bind Processing
// -----------------------------------------------------------------------------
void test_chained_ptr_64_rebase_and_bind() {
    std::cout << "[Test 2] DYLD_CHAINED_PTR_64 Rebase & Bind Processing...\n";
    DyldEngine engine;

    // Register a mock dylib exporting "_external_func"
    DyldLoadedModule mod;
    mod.name = "/usr/lib/libmock.dylib";
    mod.install_name = "/usr/lib/libmock.dylib";
    mod.base_address = 0x7FFF0000ULL;
    mod.exports["_external_func"] = 0x7FFF1234ULL;
    engine.register_module(mod);

    // Setup guest memory
    Memory mem(0x100000ULL);
    const std::uint64_t base_vaddr = 0x20000ULL;
    const std::uint64_t slide = 0x4000ULL;

    // Page 0 at 0x20000:
    // Location 0x20010: Bind to ordinal 0 (_external_func), next = 2 (stride = 8 bytes -> 0x20018)
    // Location 0x20018: Rebase to target 0x3000, next = 0 (end of chain)
    // In DYLD_CHAINED_PTR_64:
    // Bind:   [63:63] = 1 (bind), [62:51] = next, [50:24] = reserved/addend, [23:0] = ordinal
    // Rebase: [63:63] = 0 (rebase), [62:51] = next, [50:0] = target
    const std::uint64_t bind_val = (1ULL << 63u) | (2ULL << 51u) | 0ULL; // ordinal 0, next 2
    const std::uint64_t rebase_val = (0ULL << 63u) | (0ULL << 51u) | 0x3000ULL; // target 0x3000, next 0

    mem.write_u64(0x20010, bind_val);
    mem.write_u64(0x20018, rebase_val);

    // Build synthetic fixups blob
    DyldFixtureBuilder::SyntheticSegmentFixups seg;
    seg.segment_offset = base_vaddr;
    seg.page_size = 0x4000;
    seg.pointer_format = DYLD_CHAINED_PTR_64;
    seg.page_starts = { 0x10 }; // Starts at offset 0x10 on Page 0 (vaddr 0x20010)

    std::vector<std::string> imports = { "_external_func" };
    auto fixups_blob = DyldFixtureBuilder::create_synthetic_chained_fixups_with_segments(imports, { seg });

    std::string err;
    auto status = engine.process_chained_fixups(fixups_blob.data(), fixups_blob.size(), mem, base_vaddr, slide, err);
    TEST_CHECK(status == DyldLinkStatus::Success, "Fixup processing must succeed: " + err);

    // Verify written values in guest memory
    // Bind location 0x20010 must contain resolved symbol 0x7FFF1234
    const std::uint64_t written_bind = mem.read_u64(0x20010);
    TEST_CHECK(written_bind == 0x7FFF1234ULL, "Bind address must match resolved symbol");

    // Rebase location 0x20018 must contain target + slide = 0x3000 + 0x4000 = 0x7000
    const std::uint64_t written_rebase = mem.read_u64(0x20018);
    TEST_CHECK(written_rebase == 0x7000ULL, "Rebase address must match target + slide");

    std::cout << "  -> DYLD_CHAINED_PTR_64 rebase & bind passed.\n";
}

// -----------------------------------------------------------------------------
// Test 3: DYLD_CHAINED_PTR_ARM64E Authenticated Fixups & Pointer Signing
// -----------------------------------------------------------------------------
void test_chained_ptr_arm64e_authenticated_fixups() {
    std::cout << "[Test 3] DYLD_CHAINED_PTR_ARM64E Authenticated Fixups...\n";
    DyldEngine engine;

    // Attach Pointer Authentication engine
    Arm64ePointerAuth pac;
    pac.set_key(PacKeyType::InstructionA, 0x1122334455667788ULL, 0x99AABBCCDDEEFF00ULL);
    engine.set_pointer_auth(&pac);

    // Register a mock dylib exporting "_signed_entry"
    DyldLoadedModule mod;
    mod.name = "/usr/lib/libsigned.dylib";
    mod.install_name = "/usr/lib/libsigned.dylib";
    mod.base_address = 0x180000000ULL;
    mod.exports["_signed_entry"] = 0x180004000ULL;
    engine.register_module(mod);

    Memory mem(0x100000ULL);
    const std::uint64_t base_vaddr = 0x40000ULL;
    const std::uint64_t slide = 0x10000ULL;

    // Encode ARM64e Auth Bind at 0x40020:
    // ordinal = 0 (_signed_entry), diversity = 0x5A5A, addr_div = true, key = 0 (IA), next = 1 (stride 8 bytes -> 0x40028)
    const std::uint64_t auth_bind = encode_dyld_chained_ptr_arm64e_auth_bind(
        0 /* ordinal */, 0x5A5A /* diversity */, true /* addr_div */, 0 /* key IA */, 1 /* next */);

    // Encode ARM64e Auth Rebase at 0x40028:
    // target = 0x2000, diversity = 0x1234, addr_div = false, key = 0 (IA), next = 0 (end)
    const std::uint64_t auth_rebase = encode_dyld_chained_ptr_arm64e_auth_rebase(
        0x2000 /* target */, 0x1234 /* diversity */, false /* addr_div */, 0 /* key IA */, 0 /* next */);

    mem.write_u64(0x40020, auth_bind);
    mem.write_u64(0x40028, auth_rebase);

    DyldFixtureBuilder::SyntheticSegmentFixups seg;
    seg.segment_offset = base_vaddr;
    seg.page_size = 0x4000;
    seg.pointer_format = DYLD_CHAINED_PTR_ARM64E;
    seg.page_starts = { 0x20 }; // Starts at offset 0x20 on Page 0 (vaddr 0x40020)

    std::vector<std::string> imports = { "_signed_entry" };
    auto fixups_blob = DyldFixtureBuilder::create_synthetic_chained_fixups_with_segments(imports, { seg });

    std::string err;
    auto status = engine.process_chained_fixups(fixups_blob.data(), fixups_blob.size(), mem, base_vaddr, slide, err);
    TEST_CHECK(status == DyldLinkStatus::Success, "ARM64e fixups must succeed: " + err);

    // Verify written authenticated pointers
    const std::uint64_t signed_bind_ptr = mem.read_u64(0x40020);
    const std::uint64_t signed_rebase_ptr = mem.read_u64(0x40028);

    // 1. Both pointers must contain PAC tag in upper bits (tag is in bits [63:48])
    TEST_CHECK((signed_bind_ptr >> 48u) != 0, "Signed bind pointer must contain non-zero PAC tag");
    TEST_CHECK((signed_rebase_ptr >> 48u) != 0, "Signed rebase pointer must contain non-zero PAC tag");

    // 2. Stripped canonical address must match target virtual address
    const std::uint64_t stripped_bind = Arm64ePointerAuth::strip_pointer(signed_bind_ptr);
    const std::uint64_t stripped_rebase = Arm64ePointerAuth::strip_pointer(signed_rebase_ptr);
    TEST_CHECK(stripped_bind == 0x180004000ULL, "Stripped bind address must match symbol address");
    TEST_CHECK(stripped_rebase == (base_vaddr + 0x2000ULL + slide), "Stripped rebase address must match base + target + slide");

    // 3. Authenticate pointers using Arm64ePointerAuth to confirm exact cryptographic validity
    const std::uint64_t bind_modifier = 0x40020ULL ^ 0x5A5AULL; // addr_div is true -> chain_loc ^ diversity
    const std::uint64_t rebase_modifier = 0x1234ULL;            // addr_div is false -> diversity

    pac.set_fault_mode(PacFaultMode::CorruptUpperBits);
    const std::uint64_t auth_bind_res = pac.authenticate_pointer(signed_bind_ptr, bind_modifier, PacKeyType::InstructionA);
    if (pac.has_fault()) {
        std::cout << "  DEBUG: " << pac.last_syndrome().description << "\n";
    }
    TEST_CHECK(auth_bind_res == 0x180004000ULL, "PAC authentication of bind pointer must pass");
    TEST_CHECK(!pac.has_fault(), "No PAC fault should be recorded for valid bind pointer");

    const std::uint64_t auth_rebase_res = pac.authenticate_pointer(signed_rebase_ptr, rebase_modifier, PacKeyType::InstructionA);
    TEST_CHECK(auth_rebase_res == (base_vaddr + 0x2000ULL + slide),
               "PAC authentication of rebase pointer must pass");
    TEST_CHECK(!pac.has_fault(), "No PAC fault should be recorded for valid rebase pointer");

    std::cout << "  -> DYLD_CHAINED_PTR_ARM64E authenticated fixups passed.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Multi-Library Two-Level Symbol Binding & Weak Import Resolution
// -----------------------------------------------------------------------------
void test_two_level_symbol_binding_and_weak_imports() {
    std::cout << "[Test 4] Two-Level Symbol Binding & Weak Imports...\n";
    DyldEngine engine;
    engine.clear_modules(); // Clear builtin libraries so custom libraries occupy ordinals 1 and 2

    // Register Library 1
    DyldLoadedModule lib1;
    lib1.name = "/usr/lib/libapple_core.dylib";
    lib1.install_name = "/usr/lib/libapple_core.dylib";
    lib1.base_address = 0x200000000ULL;
    lib1.exports["_core_init"] = 0x200001000ULL;
    engine.register_module(lib1);

    // Register Library 2
    DyldLoadedModule lib2;
    lib2.name = "/usr/lib/libapple_crypto.dylib";
    lib2.install_name = "/usr/lib/libapple_crypto.dylib";
    lib2.base_address = 0x300000000ULL;
    lib2.exports["_crypto_hash"] = 0x300002000ULL;
    engine.register_module(lib2);

    // Test Two-Level Resolution
    std::optional<std::uint64_t> addr_res;
    std::string err;

    // 1. Resolve "_core_init" with lib_ordinal = 1 (libapple_core.dylib)
    auto st1 = engine.resolve_symbol_two_level("_core_init", 1, false, addr_res, err);
    TEST_CHECK(st1 == DyldLinkStatus::Success && addr_res.has_value() && *addr_res == 0x200001000ULL,
               "Two-level lookup in Library 1 must resolve _core_init");

    // 2. Resolve "_crypto_hash" with lib_ordinal = 2 (libapple_crypto.dylib)
    auto st2 = engine.resolve_symbol_two_level("_crypto_hash", 2, false, addr_res, err);
    TEST_CHECK(st2 == DyldLinkStatus::Success && addr_res.has_value() && *addr_res == 0x300002000ULL,
               "Two-level lookup in Library 2 must resolve _crypto_hash");

    // 3. Resolve non-existent weak symbol: must succeed with 0
    auto st3 = engine.resolve_symbol_two_level("_non_existent_optional_symbol", 1, true /* weak */, addr_res, err);
    TEST_CHECK(st3 == DyldLinkStatus::Success && addr_res.has_value() && *addr_res == 0,
               "Weak non-existent symbol must resolve gracefully to 0");

    // 4. Resolve non-existent non-weak symbol: must fail with UndefinedSymbol
    auto st4 = engine.resolve_symbol_two_level("_missing_mandatory_symbol", 1, false /* non-weak */, addr_res, err);
    TEST_CHECK(st4 == DyldLinkStatus::UndefinedSymbol,
               "Missing mandatory symbol must return UndefinedSymbol error");

    std::cout << "  -> Two-level symbol binding & weak imports passed.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 202.9 Dynamic Linker Modernization Tests ===\n";
    test_chained_fixups_header_and_bounds_validation();
    test_chained_ptr_64_rebase_and_bind();
    test_chained_ptr_arm64e_authenticated_fixups();
    test_two_level_symbol_binding_and_weak_imports();

    if (g_failures == 0) {
        std::cout << "All Stage 202.9 tests passed successfully!\n";
        return 0;
    } else {
        std::cerr << g_failures << " Stage 202.9 test(s) failed!\n";
        return 1;
    }
}

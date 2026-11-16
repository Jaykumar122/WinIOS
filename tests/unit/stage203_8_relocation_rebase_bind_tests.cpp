// ==============================================================================
// WinIOS Campaign 203 Stage 203.8: Relocation, Rebase & Bind Validation Tests
// ==============================================================================
// Independent verification of:
// 1. Strict range validation preventing relocations outside allocated segments
// 2. Rebase idempotency when slide is zero and deterministic incremental updates
// 3. Classic rebase stream opcode parsing and out-of-bounds segment rejection
// 4. Classic binding stream opcode parsing and unterminated symbol rejection
// ==============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "loader/dyld_engine.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::loader;

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
// Test 1: Range Validation for Out-of-Bounds Rebase and Binding Patches
// -----------------------------------------------------------------------------
void test_relocation_range_validation() {
    std::cout << "[Stage 203.8 Test 1] Relocation Range Validation...\n";

    DyldEngine engine;
    engine.clear_modules();

    // Module with export
    DyldLoadedModule mod;
    mod.name = "libTest.dylib";
    mod.install_name = "/usr/lib/libTest.dylib";
    mod.exports["_target_func"] = 0x5000;
    engine.register_module(mod);

    Memory mem(0x4000); // 16KB memory: 0x0000 .. 0x3FFF
    std::uint64_t base_vaddr = 0x100000000ULL;
    std::string err;

    // 1. Valid Rebase: patch at base_vaddr + 0x100 -> offset 0x100 < 0x4000
    mem.write_u64(0x100, 0x1000);
    std::vector<DyldRebaseRecord> valid_rebases = {
        {base_vaddr + 0x100, 1}
    };
    DyldLinkStatus s1 = engine.apply_rebases(mem, valid_rebases, 0x2000, base_vaddr, err);
    TEST_CHECK(s1 == DyldLinkStatus::Success, "Valid rebase must succeed");
    TEST_CHECK(mem.read_u64(0x100) == 0x3000, "Rebase value updated (0x1000 + 0x2000 = 0x3000)");

    // 2. Out-of-Bounds Rebase: patch at base_vaddr + 0x5000 -> exceeds 0x4000
    std::vector<DyldRebaseRecord> oob_rebases = {
        {base_vaddr + 0x5000, 1}
    };
    DyldLinkStatus s2 = engine.apply_rebases(mem, oob_rebases, 0x2000, base_vaddr, err);
    TEST_CHECK(s2 == DyldLinkStatus::MemoryPatchFailed, "OOB rebase must fail with MemoryPatchFailed");

    // 3. Valid Binding: patch at base_vaddr + 0x200
    std::vector<DyldBindingRecord> valid_bindings = {
        {"_target_func", base_vaddr + 0x200, 0, 1, false}
    };
    DyldLinkStatus s3 = engine.apply_bindings(mem, valid_bindings, base_vaddr, err);
    TEST_CHECK(s3 == DyldLinkStatus::Success, "Valid binding must succeed");
    TEST_CHECK(mem.read_u64(0x200) == 0x5000, "Binding written to 0x200");

    // 4. Out-of-Bounds Binding: patch at base_vaddr + 0x8000 -> exceeds 0x4000
    std::vector<DyldBindingRecord> oob_bindings = {
        {"_target_func", base_vaddr + 0x8000, 0, 1, false}
    };
    DyldLinkStatus s4 = engine.apply_bindings(mem, oob_bindings, base_vaddr, err);
    TEST_CHECK(s4 == DyldLinkStatus::MemoryPatchFailed, "OOB binding must fail with MemoryPatchFailed");

    std::cout << "  -> Relocation range validation verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Rebase Idempotency on Zero Slide & Repeated Relocation Safety
// -----------------------------------------------------------------------------
void test_rebase_idempotency() {
    std::cout << "[Stage 203.8 Test 2] Rebase Idempotency on Zero Slide...\n";

    DyldEngine engine;
    Memory mem(0x4000);
    std::uint64_t base_vaddr = 0x100000000ULL;
    std::string err;

    mem.write_u64(0x80, 0x12345678ULL);
    std::vector<DyldRebaseRecord> rebases = {
        {base_vaddr + 0x80, 1}
    };

    // Calling with slide = 0 is a strict no-op
    DyldLinkStatus s = engine.apply_rebases(mem, rebases, 0, base_vaddr, err);
    TEST_CHECK(s == DyldLinkStatus::Success, "Zero slide rebase must succeed");
    TEST_CHECK(mem.read_u64(0x80) == 0x12345678ULL, "Memory must remain unmodified when slide is 0");

    std::cout << "  -> Rebase idempotency on zero slide verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Classic Rebase Stream Opcode Validation
// -----------------------------------------------------------------------------
void test_classic_rebase_stream_validation() {
    std::cout << "[Stage 203.8 Test 3] Classic Rebase Stream Opcode Validation...\n";

    // 1. Valid stream:
    // REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB (0x20 | seg_idx=0), uleb128(0x40)
    // REBASE_OPCODE_DO_REBASE_IMM_TIMES (0x50 | imm=2) -> 2 rebases with stride 8
    // REBASE_OPCODE_DONE (0x00)
    std::vector<std::uint8_t> stream = {
        0x20, 0x40,       // seg 0, offset 0x40
        0x52,             // do 2 rebases
        0x00              // done
    };

    std::vector<std::uint64_t> seg_addrs = {0x100000000ULL};
    std::vector<DyldRebaseRecord> rebases;
    std::string err;

    bool ok1 = DyldEngine::parse_rebase_stream(stream.data(), stream.size(), seg_addrs, rebases, err);
    TEST_CHECK(ok1, "Valid rebase stream must parse successfully");
    TEST_CHECK(rebases.size() == 2, "Must produce exactly 2 rebase records");
    TEST_CHECK(rebases[0].patch_vaddr == 0x100000040ULL, "Rebase 0 vaddr match");
    TEST_CHECK(rebases[1].patch_vaddr == 0x100000048ULL, "Rebase 1 vaddr match");

    // 2. Segment index out-of-bounds rejection:
    // seg_idx = 3, but seg_addrs only has 1 segment (index 0)
    std::vector<std::uint8_t> oob_stream = {
        0x23, 0x10,       // seg 3 (out of bounds!), offset 0x10
        0x51,             // do 1 rebase
        0x00
    };

    std::vector<DyldRebaseRecord> oob_rebases;
    bool ok2 = DyldEngine::parse_rebase_stream(oob_stream.data(), oob_stream.size(), seg_addrs, oob_rebases, err);
    TEST_CHECK(!ok2, "Rebase with out-of-bounds segment index must fail");
    TEST_CHECK(!err.empty(), "Error message must detail out-of-bounds segment index");

    std::cout << "  -> Classic rebase stream opcode validation verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Classic Binding Stream Opcode Validation
// -----------------------------------------------------------------------------
void test_classic_binding_stream_validation() {
    std::cout << "[Stage 203.8 Test 4] Classic Binding Stream Opcode Validation...\n";

    std::vector<std::uint64_t> seg_addrs = {0x100000000ULL};
    std::string err;

    // 1. Valid Binding Stream:
    // SET_DYLIB_ORDINAL_IMM (0x10 | ord=1)
    // SET_SYMBOL_TRAILING_FLAGS_IMM (0x40 | flags=0), "_my_symbol\0"
    // SET_TYPE_IMM (0x50 | type=1)
    // SET_SEGMENT_AND_OFFSET_ULEB (0x70 | seg=0), uleb128(0x80)
    // DO_BIND (0x90)
    // DONE (0x00)
    std::vector<std::uint8_t> stream = {
        0x11,             // ord = 1
        0x40,             // set symbol
        '_', 'm', 'y', '_', 's', 'y', 'm', 'b', 'o', 'l', '\0',
        0x51,             // type = 1 (pointer)
        0x70, 0x80, 0x01, // seg = 0, uleb128(0x80) -> 0x80, 0x01 (128)
        0x90,             // do bind
        0x00              // done
    };

    std::vector<DyldBindingRecord> bindings;
    bool ok1 = DyldEngine::parse_binding_stream(stream.data(), stream.size(), seg_addrs, bindings, err);
    TEST_CHECK(ok1, "Valid binding stream must parse successfully");
    TEST_CHECK(bindings.size() == 1, "Must produce 1 binding record");
    TEST_CHECK(bindings[0].symbol_name == "_my_symbol", "Symbol name matches");
    TEST_CHECK(bindings[0].dylib_ordinal == 1, "Dylib ordinal matches");

    // 2. Unterminated Symbol Name Rejection
    std::vector<std::uint8_t> unterminated_stream = {
        0x11,
        0x40,
        '_', 'n', 'o', '_', 'n', 'u', 'l', 'l' // Missing trailing '\0'!
    };

    std::vector<DyldBindingRecord> bad_bindings;
    bool ok2 = DyldEngine::parse_binding_stream(unterminated_stream.data(), unterminated_stream.size(), seg_addrs, bad_bindings, err);
    TEST_CHECK(!ok2, "Unterminated symbol name in binding stream must fail");
    TEST_CHECK(!err.empty(), "Error message must report unterminated symbol name");

    std::cout << "  -> Classic binding stream opcode validation verified.\n";
}

} // namespace

int main() {
    std::cout << "==================================================================\n";
    std::cout << "Stage 203.8: Relocation, Rebase & Bind Validation Tests\n";
    std::cout << "==================================================================\n";

    test_relocation_range_validation();
    test_rebase_idempotency();
    test_classic_rebase_stream_validation();
    test_classic_binding_stream_validation();

    std::cout << "------------------------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "ALL STAGE 203.8 TESTS PASSED.\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED.\n";
        return 1;
    }
}

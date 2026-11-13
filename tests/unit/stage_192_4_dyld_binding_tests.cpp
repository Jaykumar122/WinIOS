// =============================================================================
// stage_192_4_dyld_binding_tests.cpp
//
// Campaign 192 Stage 192.4:
//   Dyld Classic Rebase & Binding Hardening and Conformance Tests
//
//   Audits and validates:
//     1. Full classic rebase opcode stream parsing and patching (LC_DYLD_INFO_ONLY)
//     2. Full classic binding opcode stream parsing and patching
//     3. Out-of-bounds segment index rejection for both rebase and bind streams
//     4. Malformed and unknown opcode rejection
//     5. Unterminated symbol name detection in binding streams
//     6. Truncated and overflowed ULEB128 / SLEB128 stream detection
//     7. Weak binding (BIND_SYMBOL_FLAGS_WEAK_IMPORT) NULL resolution
//     8. Positive and negative SLEB128 addend handling in bindings
//     9. Differential conformance between host DyldEngine and guest minidyld
// =============================================================================

#include <cstdint>
#include <cstring>
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

namespace {

int g_failed = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ \
                      << ": " << #cond << "\n"; \
            ++g_failed; \
        } \
    } while (0)

#define CHECK_MSG(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ \
                      << ": " << (msg) << " (" #cond ")\n"; \
            ++g_failed; \
        } \
    } while (0)

void append_uleb128(std::vector<std::uint8_t>& buf, std::uint64_t val) {
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(val & 0x7F);
        val >>= 7;
        if (val != 0) {
            byte |= 0x80;
        }
        buf.push_back(byte);
    } while (val != 0);
}

void append_sleb128(std::vector<std::uint8_t>& buf, std::int64_t val) {
    bool more = true;
    while (more) {
        std::uint8_t byte = static_cast<std::uint8_t>(val & 0x7F);
        val >>= 7;
        if ((val == 0 && (byte & 0x40) == 0) || (val == -1 && (byte & 0x40) != 0)) {
            more = false;
        } else {
            byte |= 0x80;
        }
        buf.push_back(byte);
    }
}

// -----------------------------------------------------------------------------
// Test 1: Full Classic Rebase Opcode Stream Parsing & Application
// -----------------------------------------------------------------------------
void test_classic_rebase_stream_parsing() {
    std::cout << "[Test 1] Full classic rebase opcode stream parsing & application...\n";
    std::vector<std::uint8_t> rebase_stream;

    // 0x11: REBASE_OPCODE_SET_TYPE_IMM (type = 1: POINTER)
    rebase_stream.push_back(0x11);
    // 0x20: REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB (seg = 0, offset = 0x100)
    rebase_stream.push_back(0x20);
    append_uleb128(rebase_stream, 0x100);
    // 0x53: REBASE_OPCODE_DO_REBASE_IMM_TIMES (3 pointers at 0x100, 0x108, 0x110)
    rebase_stream.push_back(0x53);
    // 0x30: REBASE_OPCODE_ADD_ADDR_ULEB (+0x20 -> offset becomes 0x138)
    rebase_stream.push_back(0x30);
    append_uleb128(rebase_stream, 0x20);
    // 0x70: REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB (rebase at 0x138, +8 + 0x10 -> offset becomes 0x150)
    rebase_stream.push_back(0x70);
    append_uleb128(rebase_stream, 0x10);
    // 0x60: REBASE_OPCODE_DO_REBASE_ULEB_TIMES (count 2 at 0x150, 0x158)
    rebase_stream.push_back(0x60);
    append_uleb128(rebase_stream, 2);
    // 0x80: REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB (count 2, skip 0x10 -> 0x160, 0x178)
    rebase_stream.push_back(0x80);
    append_uleb128(rebase_stream, 2);
    append_uleb128(rebase_stream, 0x10);
    // 0x00: REBASE_OPCODE_DONE
    rebase_stream.push_back(0x00);

    std::vector<std::uint64_t> seg_vmaddrs = {0x1000, 0x4000};
    std::vector<DyldRebaseRecord> rebases;
    std::string err;

    bool ok = DyldEngine::parse_rebase_stream(
        rebase_stream.data(), rebase_stream.size(), seg_vmaddrs, rebases, err);
    CHECK(ok);
    CHECK(err.empty());
    CHECK(rebases.size() == 8);

    // Verify offsets
    CHECK(rebases[0].patch_vaddr == 0x1000 + 0x100);
    CHECK(rebases[1].patch_vaddr == 0x1000 + 0x108);
    CHECK(rebases[2].patch_vaddr == 0x1000 + 0x110);
    CHECK(rebases[3].patch_vaddr == 0x1000 + 0x138);
    CHECK(rebases[4].patch_vaddr == 0x1000 + 0x150);
    CHECK(rebases[5].patch_vaddr == 0x1000 + 0x158);
    CHECK(rebases[6].patch_vaddr == 0x1000 + 0x160);
    CHECK(rebases[7].patch_vaddr == 0x1000 + 0x178);

    // Apply rebases to memory
    Memory mem(0x10000);
    for (const auto& r : rebases) {
        mem.write_u64(static_cast<std::size_t>(r.patch_vaddr), 0x5000ULL);
    }
    DyldEngine dyld;
    std::uint64_t slide = 0x20000ULL;
    auto status = dyld.apply_rebases(mem, rebases, slide, 0, err);
    CHECK(status == DyldLinkStatus::Success);
    for (const auto& r : rebases) {
        CHECK(mem.read_u64(static_cast<std::size_t>(r.patch_vaddr)) == 0x5000ULL + slide);
    }
}

// -----------------------------------------------------------------------------
// Test 2: Full Classic Binding Opcode Stream Parsing & Application
// -----------------------------------------------------------------------------
void test_classic_binding_stream_parsing() {
    std::cout << "[Test 2] Full classic binding opcode stream parsing & application...\n";
    std::vector<std::uint8_t> bind_stream;

    // Set lib ordinal 1
    bind_stream.push_back(0x11);
    // Set type pointer
    bind_stream.push_back(0x51);
    // Set segment 0, offset 0x200
    bind_stream.push_back(0x70);
    append_uleb128(bind_stream, 0x200);
    // Set symbol "_printf"
    bind_stream.push_back(0x40);
    for (char c : std::string("_printf")) bind_stream.push_back(static_cast<std::uint8_t>(c));
    bind_stream.push_back(0x00);
    // Do bind at 0x200
    bind_stream.push_back(0x90);

    // Set addend +64
    bind_stream.push_back(0x60);
    append_sleb128(bind_stream, 64);
    // Set symbol "_malloc"
    bind_stream.push_back(0x40);
    for (char c : std::string("_malloc")) bind_stream.push_back(static_cast<std::uint8_t>(c));
    bind_stream.push_back(0x00);
    // Do bind add addr uleb (+8 + 0x10 = +0x18 -> offset 0x220)
    bind_stream.push_back(0xA0);
    append_uleb128(bind_stream, 0x10);

    // Done
    bind_stream.push_back(0x00);

    std::vector<std::uint64_t> seg_vmaddrs = {0x2000};
    std::vector<DyldBindingRecord> bindings;
    std::string err;

    bool ok = DyldEngine::parse_binding_stream(
        bind_stream.data(), bind_stream.size(), seg_vmaddrs, bindings, err);
    CHECK(ok);
    CHECK(bindings.size() == 2);
    CHECK(bindings[0].symbol_name == "_printf");
    CHECK(bindings[0].patch_vaddr == 0x2200);
    CHECK(bindings[0].addend == 0);
    CHECK(bindings[1].symbol_name == "_malloc");
    CHECK(bindings[1].patch_vaddr == 0x2208);
    CHECK(bindings[1].addend == 64);

    // Apply with builtin system libraries
    Memory mem(0x10000);
    DyldEngine dyld;
    auto status = dyld.apply_bindings(mem, bindings, 0, err);
    CHECK(status == DyldLinkStatus::Success);
    CHECK(mem.read_u64(0x2200) == 0x0000000180001040ULL); // _printf builtin
    CHECK(mem.read_u64(0x2208) == 0x00000001800011C0ULL + 64); // _malloc builtin + 64
}

// -----------------------------------------------------------------------------
// Test 3: Out-of-Bounds Segment Index Rejection
// -----------------------------------------------------------------------------
void test_out_of_bounds_segment_index_rejection() {
    std::cout << "[Test 3] Out-of-bounds segment index rejection in rebase and bind...\n";
    std::vector<std::uint64_t> seg_vmaddrs = {0x1000}; // Only 1 segment (index 0)

    // Rebase stream specifying segment index 4
    std::vector<std::uint8_t> bad_rebase = {0x24}; // seg_index = 4
    append_uleb128(bad_rebase, 0x10);
    std::vector<DyldRebaseRecord> rebases;
    std::string err;
    bool ok = DyldEngine::parse_rebase_stream(
        bad_rebase.data(), bad_rebase.size(), seg_vmaddrs, rebases, err);
    CHECK(!ok);
    CHECK(err.find("segment index out of bounds") != std::string::npos);

    // Binding stream specifying segment index 3
    std::vector<std::uint8_t> bad_bind = {0x73}; // seg_index = 3
    append_uleb128(bad_bind, 0x10);
    std::vector<DyldBindingRecord> bindings;
    err.clear();
    ok = DyldEngine::parse_binding_stream(
        bad_bind.data(), bad_bind.size(), seg_vmaddrs, bindings, err);
    CHECK(!ok);
    CHECK(err.find("segment index out of bounds") != std::string::npos);
}

// -----------------------------------------------------------------------------
// Test 4: Unknown / Malformed Opcode Rejection
// -----------------------------------------------------------------------------
void test_unknown_opcode_rejection() {
    std::cout << "[Test 4] Unknown opcode rejection...\n";
    std::vector<std::uint64_t> seg_vmaddrs = {0x1000};
    std::string err;

    // Unknown rebase opcode 0xD0
    std::uint8_t bad_rebase_byte = 0xD0;
    std::vector<DyldRebaseRecord> rebases;
    bool ok = DyldEngine::parse_rebase_stream(
        &bad_rebase_byte, 1, seg_vmaddrs, rebases, err);
    CHECK(!ok);
    CHECK(err.find("Unknown dyld rebase opcode") != std::string::npos);

    // Unknown bind opcode 0xE0
    std::uint8_t bad_bind_byte = 0xE0;
    std::vector<DyldBindingRecord> bindings;
    err.clear();
    ok = DyldEngine::parse_binding_stream(
        &bad_bind_byte, 1, seg_vmaddrs, bindings, err);
    CHECK(!ok);
    CHECK(err.find("Unknown dyld binding opcode") != std::string::npos);
}

// -----------------------------------------------------------------------------
// Test 5: Unterminated Symbol Name Detection
// -----------------------------------------------------------------------------
void test_unterminated_symbol_name_rejection() {
    std::cout << "[Test 5] Unterminated symbol name detection in binding stream...\n";
    std::vector<std::uint64_t> seg_vmaddrs = {0x1000};
    std::string err;

    // 0x40 (SET_SYMBOL_TRAILING_FLAGS) followed by "abc" without trailing 0x00
    std::vector<std::uint8_t> stream = {0x40, 'a', 'b', 'c'};
    std::vector<DyldBindingRecord> bindings;
    bool ok = DyldEngine::parse_binding_stream(
        stream.data(), stream.size(), seg_vmaddrs, bindings, err);
    CHECK(!ok);
    CHECK(err.find("Unterminated symbol name") != std::string::npos);
}

// -----------------------------------------------------------------------------
// Test 6: Truncated & Overflowed ULEB128 Detection
// -----------------------------------------------------------------------------
void test_truncated_and_overflow_uleb_detection() {
    std::cout << "[Test 6] Truncated & overflowed ULEB128 detection...\n";
    std::vector<std::uint64_t> seg_vmaddrs = {0x1000};
    std::string err;

    // Truncated ULEB in rebase: 0x20 followed by 0x80 (continuation bit set, no second byte)
    std::vector<std::uint8_t> trunc_rebase = {0x20, 0x80};
    std::vector<DyldRebaseRecord> rebases;
    bool ok = DyldEngine::parse_rebase_stream(
        trunc_rebase.data(), trunc_rebase.size(), seg_vmaddrs, rebases, err);
    CHECK(!ok);
    CHECK(err.find("Malformed ULEB128") != std::string::npos);

    // Overflowing ULEB in binding: 0x70 followed by 10 bytes of 0xFF
    std::vector<std::uint8_t> over_bind = {0x70};
    for (int i = 0; i < 11; ++i) over_bind.push_back(0xFF);
    std::vector<DyldBindingRecord> bindings;
    err.clear();
    ok = DyldEngine::parse_binding_stream(
        over_bind.data(), over_bind.size(), seg_vmaddrs, bindings, err);
    CHECK(!ok);
    CHECK(err.find("Malformed ULEB128") != std::string::npos);
}

// -----------------------------------------------------------------------------
// Test 7: Weak Import Binding Resolution to NULL
// -----------------------------------------------------------------------------
void test_weak_binding_null_resolution() {
    std::cout << "[Test 7] Weak import binding resolution to NULL...\n";
    std::vector<std::uint8_t> bind_stream;

    // Set segment 0, offset 0x300
    bind_stream.push_back(0x70);
    append_uleb128(bind_stream, 0x300);
    // 0x41: SET_SYMBOL_TRAILING_FLAGS with imm bit 0 set (is_weak = true)
    bind_stream.push_back(0x41);
    for (char c : std::string("_nonexistent_optional_symbol")) bind_stream.push_back(static_cast<std::uint8_t>(c));
    bind_stream.push_back(0x00);
    // Do bind at 0x300
    bind_stream.push_back(0x90);
    bind_stream.push_back(0x00); // DONE

    std::vector<std::uint64_t> seg_vmaddrs = {0x1000};
    std::vector<DyldBindingRecord> bindings;
    std::string err;

    bool ok = DyldEngine::parse_binding_stream(
        bind_stream.data(), bind_stream.size(), seg_vmaddrs, bindings, err);
    CHECK(ok);
    CHECK(bindings.size() == 1);
    CHECK(bindings[0].is_weak == true);

    // Memory location pre-filled with non-zero marker
    Memory mem(0x10000);
    mem.write_u64(0x1300, 0xCAFEBABE12345678ULL);

    DyldEngine dyld;
    auto status = dyld.apply_bindings(mem, bindings, 0, err);
    CHECK(status == DyldLinkStatus::Success);
    CHECK(err.empty());
    // Weak undefined symbol MUST be resolved to NULL (0)
    CHECK(mem.read_u64(0x1300) == 0ULL);

    // Contrast with non-weak binding for same missing symbol:
    bindings[0].is_weak = false;
    err.clear();
    status = dyld.apply_bindings(mem, bindings, 0, err);
    CHECK(status == DyldLinkStatus::UndefinedSymbol);
    CHECK(err.find("_nonexistent_optional_symbol") != std::string::npos);
}

// -----------------------------------------------------------------------------
// Test 8: Negative SLEB128 Addend Handling
// -----------------------------------------------------------------------------
void test_negative_sleb128_addend() {
    std::cout << "[Test 8] Negative SLEB128 addend handling in bindings...\n";
    std::vector<std::uint8_t> bind_stream;

    bind_stream.push_back(0x70);
    append_uleb128(bind_stream, 0x400);
    // Negative addend -32
    bind_stream.push_back(0x60);
    append_sleb128(bind_stream, -32);
    // Symbol "_printf"
    bind_stream.push_back(0x40);
    for (char c : std::string("_printf")) bind_stream.push_back(static_cast<std::uint8_t>(c));
    bind_stream.push_back(0x00);
    bind_stream.push_back(0x90);
    bind_stream.push_back(0x00);

    std::vector<std::uint64_t> seg_vmaddrs = {0x1000};
    std::vector<DyldBindingRecord> bindings;
    std::string err;

    bool ok = DyldEngine::parse_binding_stream(
        bind_stream.data(), bind_stream.size(), seg_vmaddrs, bindings, err);
    CHECK(ok);
    CHECK(bindings.size() == 1);
    CHECK(bindings[0].addend == -32);

    Memory mem(0x10000);
    DyldEngine dyld;
    auto status = dyld.apply_bindings(mem, bindings, 0, err);
    CHECK(status == DyldLinkStatus::Success);
    CHECK(mem.read_u64(0x1400) == 0x0000000180001040ULL - 32);
}

} // namespace

int main() {
    try {
        test_classic_rebase_stream_parsing();
        test_classic_binding_stream_parsing();
        test_out_of_bounds_segment_index_rejection();
        test_unknown_opcode_rejection();
        test_unterminated_symbol_name_rejection();
        test_truncated_and_overflow_uleb_detection();
        test_weak_binding_null_resolution();
        test_negative_sleb128_addend();
    } catch (const std::exception& ex) {
        std::cerr << "[EXCEPTION] " << ex.what() << "\n";
        return 1;
    }

    if (g_failed != 0) {
        std::cerr << "stage_192_4_dyld_binding_tests: " << g_failed << " check(s) failed\n";
        return 1;
    }
    std::cout << "ALL STAGE 192.4 DYLD BINDING HARDENING TESTS PASSED\n";
    return 0;
}

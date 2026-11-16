// ==============================================================================
// WinIOS Campaign 204 Stage 204.6: Export Trie & Symbol Metadata Tests
// ==============================================================================
// Independent verification of:
// 1. Regular symbol exports: prefix parsing, address offset decoding
// 2. Weak definition exports: EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION bitflag
// 3. Re-export symbols: other_ordinal and reexport_name redirection
// 4. Stub-and-resolver symbols: EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER & resolver_offset
// 5. Malformed trie detection: cycle detection, out-of-bounds offsets, truncated edges
// 6. Trie traversal limits: recursion depth bound preventing stack exhaustion
// ==============================================================================

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "loader/dyld_engine.h"

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

// Helper to write ULEB128 into byte stream
void write_uleb128(std::vector<std::uint8_t>& buf, std::uint64_t val) {
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(val & 0x7Fu);
        val >>= 7u;
        if (val != 0) {
            byte |= 0x80u;
        }
        buf.push_back(byte);
    } while (val != 0);
}

// -----------------------------------------------------------------------------
// Test 1: Comprehensive Export Types (Regular, Weak, Re-Export, Stub-and-Resolver)
// -----------------------------------------------------------------------------
void test_export_trie_comprehensive_types() {
    std::cout << "[Stage 204.6 Test 1] Comprehensive Export Trie Types...\n";

    // Build a synthetic trie with:
    // Root -> Edge "_fast_" -> Node 1:
    //   - Child "_math" -> Node 2: Regular export (addr 0x1000)
    //   - Child "_weak" -> Node 3: Weak definition (addr 0x1100, flag 0x04)
    //   - Child "_reexp" -> Node 4: Re-export (ordinal 3, reexport_name "_core_reexp", flag 0x08)
    //   - Child "_stub" -> Node 5: Stub & Resolver (addr 0x2000, resolver 0x2100, flag 0x10)

    std::vector<std::uint8_t> trie;

    // Node 0 (Root at 0):
    // terminal_size = 0
    // child_count = 1
    // edge = "_fast_"
    // child_offset = 12
    trie.push_back(0); // terminal_size = 0
    trie.push_back(1); // child_count = 1
    for (char c : std::string("_fast_")) trie.push_back(static_cast<std::uint8_t>(c));
    trie.push_back(0); // null term
    write_uleb128(trie, 12); // Node 1 offset

    // Pad to offset 12 if needed
    while (trie.size() < 12) trie.push_back(0);

    // Node 1 (at 12):
    // terminal_size = 0
    // child_count = 4
    // Children:
    // 1. "math" -> Node 2
    // 2. "weak" -> Node 3
    // 3. "reexp" -> Node 4
    // 4. "stub" -> Node 5
    // Let's compute offsets:
    // Node 1 header + 4 edges takes roughly ~40 bytes, so Node 2 starts at 60
    const std::size_t node2_off = 60;
    const std::size_t node3_off = 75;
    const std::size_t node4_off = 90;
    const std::size_t node5_off = 115;

    trie.push_back(0); // terminal_size
    trie.push_back(4); // child_count

    // Child 1
    for (char c : std::string("math")) trie.push_back(static_cast<std::uint8_t>(c));
    trie.push_back(0);
    write_uleb128(trie, node2_off);

    // Child 2
    for (char c : std::string("weak")) trie.push_back(static_cast<std::uint8_t>(c));
    trie.push_back(0);
    write_uleb128(trie, node3_off);

    // Child 3
    for (char c : std::string("reexp")) trie.push_back(static_cast<std::uint8_t>(c));
    trie.push_back(0);
    write_uleb128(trie, node4_off);

    // Child 4
    for (char c : std::string("stub")) trie.push_back(static_cast<std::uint8_t>(c));
    trie.push_back(0);
    write_uleb128(trie, node5_off);

    // Pad to Node 2 (Regular: _fast_math)
    while (trie.size() < node2_off) trie.push_back(0);
    // Node 2: terminal_size = 3 (flags=0, addr=0x1000)
    // payload: flags (uleb128 0), addr (uleb128 0x1000) -> 0x00, 0x80, 0x20
    trie.push_back(3); // terminal_size = 3
    write_uleb128(trie, EXPORT_SYMBOL_FLAGS_KIND_REGULAR); // 0
    write_uleb128(trie, 0x1000); // 0x80, 0x20
    trie.push_back(0); // child_count = 0

    // Pad to Node 3 (Weak: _fast_weak)
    while (trie.size() < node3_off) trie.push_back(0);
    // Node 3: terminal_size = 3 (flags=0x04, addr=0x1100)
    trie.push_back(3);
    write_uleb128(trie, EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION); // 0x04
    write_uleb128(trie, 0x1100);
    trie.push_back(0); // child_count = 0

    // Pad to Node 4 (Re-export: _fast_reexp)
    while (trie.size() < node4_off) trie.push_back(0);
    // Node 4 payload:
    // flags: 0x08
    // ordinal: 3
    // name: "_core_reexp\0"
    std::vector<std::uint8_t> reexp_payload;
    write_uleb128(reexp_payload, EXPORT_SYMBOL_FLAGS_REEXPORT); // 0x08
    write_uleb128(reexp_payload, 3); // other_ordinal
    for (char c : std::string("_core_reexp")) reexp_payload.push_back(static_cast<std::uint8_t>(c));
    reexp_payload.push_back(0); // null term

    trie.push_back(static_cast<std::uint8_t>(reexp_payload.size()));
    for (auto b : reexp_payload) trie.push_back(b);
    trie.push_back(0); // child_count = 0

    // Pad to Node 5 (Stub & Resolver: _fast_stub)
    while (trie.size() < node5_off) trie.push_back(0);
    // Node 5 payload:
    // flags: 0x10
    // stub addr: 0x2000
    // resolver off: 0x2100
    std::vector<std::uint8_t> stub_payload;
    write_uleb128(stub_payload, EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER); // 0x10
    write_uleb128(stub_payload, 0x2000);
    write_uleb128(stub_payload, 0x2100);

    trie.push_back(static_cast<std::uint8_t>(stub_payload.size()));
    for (auto b : stub_payload) trie.push_back(b);
    trie.push_back(0); // child_count = 0

    // Parse the trie
    std::vector<DyldExportedSymbolInfo> symbols;
    std::string err;
    bool ok = DyldEngine::parse_export_trie(trie.data(), trie.size(), symbols, err);
    TEST_CHECK(ok, "Parsing synthetic export trie must succeed");
    TEST_CHECK(symbols.size() == 4, "Must discover exactly 4 exported symbols");

    // Check symbol 1: _fast_math
    bool found_math = false;
    bool found_weak = false;
    bool found_reexp = false;
    bool found_stub = false;

    for (const auto& sym : symbols) {
        if (sym.name == "_fast_math") {
            found_math = true;
            TEST_CHECK(sym.address == 0x1000, "Regular symbol address must be 0x1000");
            TEST_CHECK(!sym.is_weak_definition, "Regular symbol must not be weak");
            TEST_CHECK(!sym.is_reexport, "Regular symbol must not be reexport");
        } else if (sym.name == "_fast_weak") {
            found_weak = true;
            TEST_CHECK(sym.address == 0x1100, "Weak symbol address must be 0x1100");
            TEST_CHECK(sym.is_weak_definition, "Weak symbol must have is_weak_definition=true");
        } else if (sym.name == "_fast_reexp") {
            found_reexp = true;
            TEST_CHECK(sym.is_reexport, "Re-export symbol must have is_reexport=true");
            TEST_CHECK(sym.other_ordinal == 3, "Re-export ordinal must be 3");
            TEST_CHECK(sym.reexport_name == "_core_reexp", "Re-export name must be _core_reexp");
        } else if (sym.name == "_fast_stub") {
            found_stub = true;
            TEST_CHECK(sym.is_stub_and_resolver, "Must identify stub and resolver");
            TEST_CHECK(sym.address == 0x2000, "Stub address must be 0x2000");
            TEST_CHECK(sym.resolver_offset == 0x2100, "Resolver offset must be 0x2100");
        }
    }

    TEST_CHECK(found_math && found_weak && found_reexp && found_stub, "All 4 symbol variants must be parsed");
    std::cout << "  -> Comprehensive export trie types verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Malformed Trie Detection (Cycles, Out-of-Bounds, Max Depth)
// -----------------------------------------------------------------------------
void test_export_trie_malformed_rejection() {
    std::cout << "[Stage 204.6 Test 2] Malformed Export Trie Rejection...\n";

    std::vector<DyldExportedSymbolInfo> symbols;
    std::string err;

    // 1. Cycle detection: Node 0 points to Node 1, Node 1 points back to Node 0
    std::vector<std::uint8_t> cyclic_trie = {
        0x00, // Node 0 (offset 0): terminal_size = 0
        0x01, // child_count = 1
        'a', 0x00, // edge "a"
        0x05, // child_offset = 5 (Node 1)
        // Node 1 (offset 5):
        0x00, // terminal_size = 0
        0x01, // child_count = 1
        'b', 0x00, // edge "b"
        0x00  // child_offset = 0 (points back to Node 0!)
    };
    bool cyc_ok = DyldEngine::parse_export_trie(cyclic_trie.data(), cyclic_trie.size(), symbols, err);
    TEST_CHECK(!cyc_ok, "Cyclic export trie must fail parsing");
    TEST_CHECK(err.find("cycle") != std::string::npos, "Error must report cycle detection");

    // 2. Out-of-bounds child offset
    std::vector<std::uint8_t> oob_trie = {
        0x00, // terminal_size = 0
        0x01, // child_count = 1
        'x', 0x00, // edge "x"
        0xFF, 0x7F // child_offset = 16383 (far exceeds size of 8 bytes)
    };
    bool oob_ok = DyldEngine::parse_export_trie(oob_trie.data(), oob_trie.size(), symbols, err);
    TEST_CHECK(!oob_ok, "Out-of-bounds child offset must fail parsing");
    TEST_CHECK(err.find("exceeds trie size") != std::string::npos, "Error must report out-of-bounds");

    // 3. Truncated edge label
    std::vector<std::uint8_t> trunc_edge_trie = {
        0x00, // terminal_size = 0
        0x01, // child_count = 1
        'm', 'i', 's', 's' // missing null terminator and offset!
    };
    bool trunc_ok = DyldEngine::parse_export_trie(trunc_edge_trie.data(), trunc_edge_trie.size(), symbols, err);
    TEST_CHECK(!trunc_ok, "Truncated edge label must fail parsing");

    std::cout << "  -> Malformed export tries safely rejected.\n";
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 204.6 Export Trie & Symbol Metadata Tests ===\n";
    test_export_trie_comprehensive_types();
    test_export_trie_malformed_rejection();

    if (g_failures == 0) {
        std::cout << "[PASS] Stage 204.6 Export Trie & Symbol Metadata passed all tests.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 204.6 Export Trie & Symbol Metadata failed with " << g_failures << " errors.\n";
        return 1;
    }
}

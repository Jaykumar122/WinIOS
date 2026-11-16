// ==============================================================================
// WinIOS Campaign 203 Stage 203.6: Symbol Resolution Expansion Tests
// ==============================================================================
// Independent verification of:
// 1. Two-level namespace resolution: ordinal-specific, main executable, flat lookup
// 2. Weak import handling: binds missing weak symbols to 0 with Success
// 3. Required import handling: missing required symbols fail with UndefinedSymbol
// 4. Transitive re-exported symbol lookup across multiple dylibs
// 5. Cyclic re-export recursion termination
// 6. Robust symbol-table (LC_SYMTAB) and string-table bounds validation
// ==============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "loader/dyld_engine.h"

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
// Test 1: Weak vs Required Import Resolution Semantics
// -----------------------------------------------------------------------------
void test_weak_vs_required_import_semantics() {
    std::cout << "[Stage 203.6 Test 1] Weak vs Required Import Resolution...\n";

    DyldEngine engine;
    engine.clear_modules();

    // Module 1: Provider
    DyldLoadedModule mod1;
    mod1.name = "libProvider.dylib";
    mod1.install_name = "/usr/lib/libProvider.dylib";
    mod1.base_address = 0x100000000ULL;
    mod1.slide = 0x10000ULL;
    mod1.exports["_existing_func"] = 0x100002000ULL;
    engine.register_module(mod1);

    std::optional<std::uint64_t> addr;
    std::string err;

    // 1. Present symbol lookup (ordinal 1)
    DyldLinkStatus s1 = engine.resolve_symbol_two_level("_existing_func", 1, false, addr, err);
    TEST_CHECK(s1 == DyldLinkStatus::Success, "Present required symbol must resolve");
    TEST_CHECK(addr.has_value() && *addr == 0x100012000ULL, "Address must include slide");

    // 2. Missing required symbol (ordinal 1) -> Failure
    DyldLinkStatus s2 = engine.resolve_symbol_two_level("_missing_func", 1, false, addr, err);
    TEST_CHECK(s2 == DyldLinkStatus::UndefinedSymbol, "Missing required symbol must fail with UndefinedSymbol");
    TEST_CHECK(!err.empty(), "Error message must be set");

    // 3. Missing weak symbol (ordinal 1) -> Success with addr = 0
    DyldLinkStatus s3 = engine.resolve_symbol_two_level("_missing_weak_func", 1, true, addr, err);
    TEST_CHECK(s3 == DyldLinkStatus::Success, "Missing weak symbol must succeed");
    TEST_CHECK(addr.has_value() && *addr == 0, "Missing weak symbol must resolve to 0 (NULL)");

    // 4. Missing weak symbol (flat lookup 0) -> Success with addr = 0
    DyldLinkStatus s4 = engine.resolve_symbol_two_level("_missing_flat_weak", 0, true, addr, err);
    TEST_CHECK(s4 == DyldLinkStatus::Success, "Missing flat weak symbol must succeed");
    TEST_CHECK(addr.has_value() && *addr == 0, "Missing flat weak symbol must resolve to 0 (NULL)");

    std::cout << "  -> Weak vs required import semantics verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Re-exported Symbol Resolution Across Transitive Chains & Cycle Safety
// -----------------------------------------------------------------------------
void test_reexported_symbol_resolution() {
    std::cout << "[Stage 203.6 Test 2] Re-exported Symbol Resolution & Cycle Safety...\n";

    DyldEngine engine;
    engine.clear_modules();

    // Chain: libRoot -> re-exports libMid -> re-exports libLeaf
    // libLeaf exports "_leaf_api"
    DyldLoadedModule libLeaf;
    libLeaf.name = "libLeaf.dylib";
    libLeaf.install_name = "/usr/lib/libLeaf.dylib";
    libLeaf.slide = 0x4000;
    libLeaf.exports["_leaf_api"] = 0x1000;
    engine.register_module(libLeaf);

    DyldLoadedModule libMid;
    libMid.name = "libMid.dylib";
    libMid.install_name = "/usr/lib/libMid.dylib";
    libMid.slide = 0x8000;
    libMid.reexports.push_back("/usr/lib/libLeaf.dylib");
    engine.register_module(libMid);

    DyldLoadedModule libRoot;
    libRoot.name = "libRoot.dylib";
    libRoot.install_name = "/usr/lib/libRoot.dylib";
    libRoot.slide = 0;
    libRoot.reexports.push_back("libMid.dylib");
    engine.register_module(libRoot);

    std::optional<std::uint64_t> addr;
    std::string err;

    // Lookup "_leaf_api" in libRoot (ordinal 3 because libLeaf is 1, libMid is 2, libRoot is 3)
    std::uint32_t root_ord = engine.get_module_ordinal("libRoot.dylib");
    TEST_CHECK(root_ord == 3, "libRoot ordinal is 3");

    DyldLinkStatus s = engine.resolve_symbol_two_level("_leaf_api", static_cast<std::int32_t>(root_ord), false, addr, err);
    TEST_CHECK(s == DyldLinkStatus::Success, "Transitive re-exported symbol must resolve");
    TEST_CHECK(addr.has_value() && *addr == 0x5000, "Address from libLeaf with slide (0x1000 + 0x4000 = 0x5000)");

    // Test Cyclic Re-export safety:
    // libCycleA re-exports libCycleB, libCycleB re-exports libCycleA
    DyldEngine cyclic_engine;
    cyclic_engine.clear_modules();

    DyldLoadedModule modA;
    modA.name = "libCycleA.dylib";
    modA.reexports.push_back("libCycleB.dylib");
    cyclic_engine.register_module(modA);

    DyldLoadedModule modB;
    modB.name = "libCycleB.dylib";
    modB.reexports.push_back("libCycleA.dylib");
    cyclic_engine.register_module(modB);

    // Resolving a non-existent symbol in a cycle must terminate safely and report UndefinedSymbol
    DyldLinkStatus s_cyc = cyclic_engine.resolve_symbol_two_level("_non_existent", 1, false, addr, err);
    TEST_CHECK(s_cyc == DyldLinkStatus::UndefinedSymbol, "Cyclic re-export of non-existent symbol must return UndefinedSymbol");

    std::cout << "  -> Re-exported symbol resolution & cycle safety verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Symbol Table & String Table Bounds Validation
// -----------------------------------------------------------------------------
void test_symbol_table_bounds_validation() {
    std::cout << "[Stage 203.6 Test 3] Symbol Table Bounds Validation...\n";

    #pragma pack(push, 1)
    struct NList64Raw {
        std::uint32_t n_strx;
        std::uint8_t  n_type;
        std::uint8_t  n_sect;
        std::uint16_t n_desc;
        std::uint64_t n_value;
    };
    #pragma pack(pop)

    // 1. Valid Symbol Table & String Table
    {
        std::vector<std::uint8_t> buffer(512, 0);
        std::uint32_t symoff = 64;
        std::uint32_t nsyms = 2;
        std::uint32_t stroff = 128;

        const char str_data[] = "\0_main\0_weak_symbol\0";
        std::uint32_t strsize = static_cast<std::uint32_t>(sizeof(str_data));
        std::memcpy(buffer.data() + stroff, str_data, strsize);

        auto* nlists = reinterpret_cast<NList64Raw*>(buffer.data() + symoff);
        nlists[0].n_strx = 1; // "_main"
        nlists[0].n_type = 0x0F; // N_SECT | N_EXT
        nlists[0].n_sect = 1;
        nlists[0].n_desc = 0;
        nlists[0].n_value = 0x100004000ULL;

        nlists[1].n_strx = 7; // "_weak_symbol"
        nlists[1].n_type = 0x0F;
        nlists[1].n_sect = 1;
        nlists[1].n_desc = 0x0080; // N_WEAK_DEF
        nlists[1].n_value = 0x100005000ULL;

        std::vector<DyldSymtabSymbolInfo> syms;
        std::string err;
        bool ok = DyldEngine::parse_symbol_table(buffer.data(), buffer.size(), symoff, nsyms, stroff, strsize, syms, err);
        TEST_CHECK(ok, "Valid symbol table parsing must succeed");
        TEST_CHECK(syms.size() == 2, "Must parse 2 symbols");
        TEST_CHECK(syms[0].name == "_main", "Symbol 0 name matches");
        TEST_CHECK(syms[0].is_external, "Symbol 0 is external");
        TEST_CHECK(!syms[0].is_weak_def, "Symbol 0 is not weak def");
        TEST_CHECK(syms[1].name == "_weak_symbol", "Symbol 1 name matches");
        TEST_CHECK(syms[1].is_weak_def, "Symbol 1 is weak def");
    }

    // 2. Out-of-bounds String Index Rejection
    {
        std::vector<std::uint8_t> buffer(512, 0);
        std::uint32_t symoff = 64;
        std::uint32_t nsyms = 1;
        std::uint32_t stroff = 128;
        std::uint32_t strsize = 16;

        auto* nlists = reinterpret_cast<NList64Raw*>(buffer.data() + symoff);
        nlists[0].n_strx = 50; // >= strsize (16)

        std::vector<DyldSymtabSymbolInfo> syms;
        std::string err;
        bool ok = DyldEngine::parse_symbol_table(buffer.data(), buffer.size(), symoff, nsyms, stroff, strsize, syms, err);
        TEST_CHECK(!ok, "Out-of-bounds string index must be rejected");
        TEST_CHECK(!err.empty(), "Error message must be reported");
    }

    // 3. Unterminated String in String Table Rejection
    {
        std::vector<std::uint8_t> buffer(512, 0);
        std::uint32_t symoff = 64;
        std::uint32_t nsyms = 1;
        std::uint32_t stroff = 128;
        std::uint32_t strsize = 8;

        // Fill string table with non-null chars ('A')
        std::memset(buffer.data() + stroff, 'A', strsize);

        auto* nlists = reinterpret_cast<NList64Raw*>(buffer.data() + symoff);
        nlists[0].n_strx = 0;

        std::vector<DyldSymtabSymbolInfo> syms;
        std::string err;
        bool ok = DyldEngine::parse_symbol_table(buffer.data(), buffer.size(), symoff, nsyms, stroff, strsize, syms, err);
        TEST_CHECK(!ok, "Unterminated string must be rejected");
        TEST_CHECK(!err.empty(), "Error message must be reported");
    }

    std::cout << "  -> Symbol table bounds validation verified.\n";
}

} // namespace

int main() {
    std::cout << "==================================================================\n";
    std::cout << "Stage 203.6: Symbol Resolution Expansion Tests\n";
    std::cout << "==================================================================\n";

    test_weak_vs_required_import_semantics();
    test_reexported_symbol_resolution();
    test_symbol_table_bounds_validation();

    std::cout << "------------------------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "ALL STAGE 203.6 TESTS PASSED.\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED.\n";
        return 1;
    }
}

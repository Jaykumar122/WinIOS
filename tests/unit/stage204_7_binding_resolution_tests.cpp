// =============================================================================
// WinIOS — Stage 204.7: Dynamic Binding and Import Resolution Unit Tests
// =============================================================================

#include "loader/dyld_engine.h"
#include "cpu/pointer_auth.h"
#include <iostream>
#include <vector>
#include <string>
#include <cassert>

using namespace winios;
using namespace winios::loader;

static int g_failed_tests = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            g_failed_tests++; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Two-Level Namespace Symbol Resolution & Namespace Isolation
// -----------------------------------------------------------------------------
void test_two_level_namespace_isolation() {
    std::cout << "[Stage 204.7 Test 1] Two-Level Namespace Symbol Resolution...\n";

    DyldEngine engine;
    engine.clear_modules();

    // Module 1: libmath.dylib (1-based ordinal 1)
    DyldLoadedModule libmath;
    libmath.name = "libmath.dylib";
    libmath.install_name = "/usr/lib/libmath.dylib";
    libmath.base_address = 0x10000000ULL;
    libmath.exports["_compute"] = 0x1000ULL;
    libmath.exports["_version"] = 0x1100ULL;
    engine.register_module(std::move(libmath));

    // Module 2: libstring.dylib (1-based ordinal 2)
    DyldLoadedModule libstring;
    libstring.name = "libstring.dylib";
    libstring.install_name = "/usr/lib/libstring.dylib";
    libstring.base_address = 0x20000000ULL;
    libstring.exports["_format"]  = 0x2000ULL;
    libstring.exports["_version"] = 0x2100ULL;
    engine.register_module(std::move(libstring));

    std::optional<std::uint64_t> addr;
    std::string err;

    // 1. Ordinal 1 -> _compute
    DyldLinkStatus s1 = engine.resolve_symbol_two_level("_compute", 1, false, addr, err);
    TEST_CHECK(s1 == DyldLinkStatus::Success, "Ordinal 1 resolves _compute");
    TEST_CHECK(addr.has_value() && *addr == 0x1000ULL, "Address of _compute is 0x1000");

    // 2. Ordinal 2 -> _format
    DyldLinkStatus s2 = engine.resolve_symbol_two_level("_format", 2, false, addr, err);
    TEST_CHECK(s2 == DyldLinkStatus::Success, "Ordinal 2 resolves _format");
    TEST_CHECK(addr.has_value() && *addr == 0x2000ULL, "Address of _format is 0x2000");

    // 3. Namespace Isolation: _version in ordinal 1 vs ordinal 2
    DyldLinkStatus s3 = engine.resolve_symbol_two_level("_version", 1, false, addr, err);
    TEST_CHECK(s3 == DyldLinkStatus::Success, "Ordinal 1 resolves _version");
    TEST_CHECK(addr.has_value() && *addr == 0x1100ULL, "_version in libmath is 0x1100");

    DyldLinkStatus s4 = engine.resolve_symbol_two_level("_version", 2, false, addr, err);
    TEST_CHECK(s4 == DyldLinkStatus::Success, "Ordinal 2 resolves _version");
    TEST_CHECK(addr.has_value() && *addr == 0x2100ULL, "_version in libstring is 0x2100");

    // 4. Strict isolation: _format does NOT exist in libmath (ordinal 1)
    DyldLinkStatus s5 = engine.resolve_symbol_two_level("_format", 1, false, addr, err);
    TEST_CHECK(s5 == DyldLinkStatus::UndefinedSymbol, "Strict two-level prevents cross-library leakage");
    TEST_CHECK(!err.empty(), "Error message populated for missing symbol");

    std::cout << "  -> Two-level namespace resolution and isolation verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Special Ordinals (Main Executable, Self, Flat Lookup)
// -----------------------------------------------------------------------------
void test_special_ordinals() {
    std::cout << "[Stage 204.7 Test 2] Special Ordinals Resolution...\n";

    DyldEngine engine;
    engine.clear_modules();

    // Module 0: Main Executable
    DyldLoadedModule main_app;
    main_app.name = "MyMachOApp";
    main_app.install_name = "@executable_path/MyMachOApp";
    main_app.base_address = 0x100000000ULL;
    main_app.exports["_main_entry"] = 0x100001000ULL;
    main_app.exports["_app_secret"] = 0x100002000ULL;
    engine.register_module(std::move(main_app));

    // Module 1: DylibA
    DyldLoadedModule dylib_a;
    dylib_a.name = "libA.dylib";
    dylib_a.install_name = "/usr/lib/libA.dylib";
    dylib_a.base_address = 0x180000000ULL;
    dylib_a.exports["_func_a"] = 0x180001000ULL;
    engine.register_module(std::move(dylib_a));

    std::optional<std::uint64_t> addr;
    std::string err;

    // 1. BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE (-1)
    DyldLinkStatus s1 = engine.resolve_symbol_two_level("_main_entry", BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE, false, addr, err);
    TEST_CHECK(s1 == DyldLinkStatus::Success, "Main executable symbol resolved with ordinal -1");
    TEST_CHECK(addr.has_value() && *addr == 0x100001000ULL, "Address matches main entry");

    // 2. BIND_SPECIAL_DYLIB_SELF (0) with caller_module_name = "libA.dylib"
    DyldLinkStatus s2 = engine.resolve_symbol_two_level("_func_a", BIND_SPECIAL_DYLIB_SELF, false, addr, err, "libA.dylib");
    TEST_CHECK(s2 == DyldLinkStatus::Success, "Self symbol resolved with ordinal 0 and caller");
    TEST_CHECK(addr.has_value() && *addr == 0x180001000ULL, "Address matches _func_a");

    // 3. BIND_SPECIAL_DYLIB_FLAT_LOOKUP (-2)
    DyldLinkStatus s3 = engine.resolve_symbol_two_level("_func_a", BIND_SPECIAL_DYLIB_FLAT_LOOKUP, false, addr, err);
    TEST_CHECK(s3 == DyldLinkStatus::Success, "Flat lookup resolves symbol across loaded modules");
    TEST_CHECK(addr.has_value() && *addr == 0x180001000ULL, "Address matches _func_a");

    std::cout << "  -> Special ordinals verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Weak Import Semantics & Missing Symbol Error Reporting
// -----------------------------------------------------------------------------
void test_weak_import_and_missing_symbol() {
    std::cout << "[Stage 204.7 Test 3] Weak Import & Missing Symbol Handling...\n";

    DyldEngine engine;
    engine.clear_modules();

    DyldLoadedModule dylib;
    dylib.name = "libsample.dylib";
    dylib.install_name = "/usr/lib/libsample.dylib";
    dylib.exports["_present_api"] = 0x5000ULL;
    engine.register_module(std::move(dylib));

    std::optional<std::uint64_t> addr;
    std::string err;

    // 1. Weak import of present symbol -> success with actual address
    DyldLinkStatus s1 = engine.resolve_symbol_two_level("_present_api", 1, true, addr, err);
    TEST_CHECK(s1 == DyldLinkStatus::Success, "Weak import of present symbol succeeds");
    TEST_CHECK(addr.has_value() && *addr == 0x5000ULL, "Address resolved to 0x5000");

    // 2. Weak import of absent symbol -> success with address 0 (NULL)
    DyldLinkStatus s2 = engine.resolve_symbol_two_level("_absent_feature", 1, true, addr, err);
    TEST_CHECK(s2 == DyldLinkStatus::Success, "Weak import of missing symbol succeeds");
    TEST_CHECK(addr.has_value() && *addr == 0ULL, "Missing weak symbol resolves to nullptr (0)");

    // 3. Strong import of absent symbol -> failure with UndefinedSymbol
    DyldLinkStatus s3 = engine.resolve_symbol_two_level("_absent_feature", 1, false, addr, err);
    TEST_CHECK(s3 == DyldLinkStatus::UndefinedSymbol, "Strong import of missing symbol must fail");
    TEST_CHECK(err.find("_absent_feature") != std::string::npos, "Error contains missing symbol name");

    std::cout << "  -> Weak import and error diagnostics verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Weak Definition Preemption & Coalescing
// -----------------------------------------------------------------------------
void test_weak_definition_coalescing() {
    std::cout << "[Stage 204.7 Test 4] Weak Definition Preemption & Coalescing...\n";

    DyldEngine engine;
    engine.clear_modules();

    // Module 1 provides a WEAK definition of _custom_alloc
    DyldLoadedModule mod_weak;
    mod_weak.name = "libdefault_alloc.dylib";
    mod_weak.exports["_custom_alloc"] = 0x1000ULL;
    mod_weak.weak_exports.insert("_custom_alloc");
    engine.register_module(std::move(mod_weak));

    // When only weak module is loaded, flat lookup finds weak definition
    auto res1 = engine.resolve_symbol_coalesced("_custom_alloc");
    TEST_CHECK(res1.has_value() && *res1 == 0x1000ULL, "Weak definition used when no strong exists");

    // Module 2 provides a STRONG definition of _custom_alloc
    DyldLoadedModule mod_strong;
    mod_strong.name = "libjemalloc.dylib";
    mod_strong.exports["_custom_alloc"] = 0x8000ULL;
    // (not in weak_exports)
    engine.register_module(std::move(mod_strong));

    // Now flat lookup MUST resolve to strong definition (0x8000)
    auto res2 = engine.resolve_symbol_coalesced("_custom_alloc");
    TEST_CHECK(res2.has_value() && *res2 == 0x8000ULL, "Strong definition preempts weak definition");

    std::cout << "  -> Weak definition coalescing verified.\n";
}

// -----------------------------------------------------------------------------
// Test 5: Re-Export Resolution and Cycle Protection
// -----------------------------------------------------------------------------
void test_reexport_resolution_and_cycles() {
    std::cout << "[Stage 204.7 Test 5] Re-Export Resolution and Cycle Protection...\n";

    DyldEngine engine;
    engine.clear_modules();

    // Leaf library
    DyldLoadedModule lib_sub;
    lib_sub.name = "libsub.dylib";
    lib_sub.install_name = "/usr/lib/libsub.dylib";
    lib_sub.exports["_inner_service"] = 0x9900ULL;
    engine.register_module(std::move(lib_sub));

    // Umbrella library re-exporting libsub.dylib
    DyldLoadedModule lib_umbrella;
    lib_umbrella.name = "libumbrella.dylib";
    lib_umbrella.install_name = "/usr/lib/libumbrella.dylib";
    lib_umbrella.reexports.push_back("libsub.dylib");
    engine.register_module(std::move(lib_umbrella));

    std::optional<std::uint64_t> addr;
    std::string err;

    // Ordinal 2 is libumbrella.dylib (registered 2nd). Query _inner_service through umbrella
    DyldLinkStatus s1 = engine.resolve_symbol_two_level("_inner_service", 2, false, addr, err);
    TEST_CHECK(s1 == DyldLinkStatus::Success, "Re-export transitively resolves symbol");
    TEST_CHECK(addr.has_value() && *addr == 0x9900ULL, "Resolved address is 0x9900");

    // Cyclic re-exports: libA re-exports libB, libB re-exports libA
    DyldEngine cyclic_engine;
    cyclic_engine.clear_modules();

    DyldLoadedModule mod_a;
    mod_a.name = "modA.dylib";
    mod_a.reexports.push_back("modB.dylib");
    cyclic_engine.register_module(std::move(mod_a));

    DyldLoadedModule mod_b;
    mod_b.name = "modB.dylib";
    mod_b.reexports.push_back("modA.dylib");
    cyclic_engine.register_module(std::move(mod_b));

    // Searching for nonexistent symbol terminates cleanly without infinite recursion
    DyldLinkStatus s_cyc = cyclic_engine.resolve_symbol_two_level("_nowhere", 1, false, addr, err);
    TEST_CHECK(s_cyc == DyldLinkStatus::UndefinedSymbol, "Cyclic re-export terminates and reports UndefinedSymbol");

    std::cout << "  -> Re-export resolution and cycle safety verified.\n";
}

// -----------------------------------------------------------------------------
// Test 6: Memory Binding Application Integration
// -----------------------------------------------------------------------------
void test_memory_binding_application() {
    std::cout << "[Stage 204.7 Test 6] Memory Binding Application Integration...\n";

    DyldEngine engine;
    engine.clear_modules();

    DyldLoadedModule lib;
    lib.name = "libtarget.dylib";
    lib.install_name = "/usr/lib/libtarget.dylib";
    lib.base_address = 0x180000000ULL;
    lib.exports["_real_func"] = 0x180004000ULL;
    engine.register_module(std::move(lib));

    Memory mem(0x2000);
    std::uint64_t base_vaddr = 0x100000000ULL;
    std::string err;

    std::vector<DyldBindingRecord> bindings = {
        {"_real_func", base_vaddr + 0x10, 0, 1, false},      // Strong present -> 0x180004000
        {"_missing_weak", base_vaddr + 0x20, 0, 1, true}     // Weak absent -> 0
    };

    DyldLinkStatus st = engine.apply_bindings(mem, bindings, base_vaddr, err);
    TEST_CHECK(st == DyldLinkStatus::Success, "apply_bindings succeeds with strong and weak bindings");
    TEST_CHECK(mem.read_u64(0x10) == 0x180004000ULL, "Strong binding written correctly");
    TEST_CHECK(mem.read_u64(0x20) == 0ULL, "Weak absent binding written as 0 (NULL)");

    std::cout << "  -> Memory binding application verified.\n";
}

int main() {
    std::cout << "=== WinIOS Stage 204.7 Binding and Import Resolution Tests ===\n";
    test_two_level_namespace_isolation();
    test_special_ordinals();
    test_weak_import_and_missing_symbol();
    test_weak_definition_coalescing();
    test_reexport_resolution_and_cycles();
    test_memory_binding_application();

    if (g_failed_tests == 0) {
        std::cout << "[PASS] Stage 204.7 Binding and Import Resolution passed all tests.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 204.7 Binding and Import Resolution failed with "
                  << g_failed_tests << " errors.\n";
        return 1;
    }
}

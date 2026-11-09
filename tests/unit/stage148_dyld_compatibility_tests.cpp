#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "loader/dyld_engine.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::loader;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Test 1: Builtin iOS System Libraries and Symbol Lookup
bool test_builtin_libraries_and_symbols() {
    std::cout << "[Test 1] Builtin iOS system libraries and symbol lookup...\n";

    DyldEngine dyld;

    TEST_ASSERT(dyld.has_module("libSystem.B.dylib"), "Must contain libSystem.B.dylib");
    TEST_ASSERT(dyld.has_module("CoreFoundation"), "Must contain CoreFoundation");

    // Lookup _exit
    auto sym_exit = dyld.resolve_symbol("_exit");
    TEST_ASSERT(sym_exit.has_value(), "_exit must be resolved");
    TEST_ASSERT(*sym_exit == 0x0000000180001000ULL, "_exit address must match registered address");

    // Lookup _CFRunLoopRun
    auto sym_cfrun = dyld.resolve_symbol("_CFRunLoopRun");
    TEST_ASSERT(sym_cfrun.has_value(), "_CFRunLoopRun must be resolved");
    TEST_ASSERT(*sym_cfrun == 0x0000000190001000ULL, "_CFRunLoopRun address must match");

    // Nonexistent symbol
    auto sym_missing = dyld.resolve_symbol("_nonexistent_function");
    TEST_ASSERT(!sym_missing.has_value(), "Nonexistent symbol must return nullopt");

    return true;
}

// Test 2: Strong Symbol Binding and Memory Patching
bool test_strong_binding_and_patching() {
    std::cout << "[Test 2] Strong symbol binding and memory patching...\n";

    Memory mem(1024 * 1024);
    DyldEngine dyld;

    // Prepare binding at 0x2000 for symbol "_printf" (0x0000000180001040)
    std::vector<DyldBindingRecord> bindings;
    DyldBindingRecord b1;
    b1.symbol_name = "_printf";
    b1.patch_vaddr = 0x2000;
    b1.addend = 0;
    b1.is_weak = false;
    bindings.push_back(b1);

    std::string err;
    auto status = dyld.apply_bindings(mem, bindings, 0, err);
    TEST_ASSERT(status == DyldLinkStatus::Success, "apply_bindings must succeed");

    std::uint64_t patched_val = mem.read_u64(0x2000);
    TEST_ASSERT(patched_val == 0x0000000180001040ULL, "Patched value must equal _printf target address");

    return true;
}

// Test 3: Undefined Strong Symbol Rejection with Diagnostic
bool test_undefined_strong_symbol_rejection() {
    std::cout << "[Test 3] Undefined strong symbol rejection with diagnostic...\n";

    Memory mem(1024 * 1024);
    DyldEngine dyld;

    std::vector<DyldBindingRecord> bindings;
    DyldBindingRecord b_bad;
    b_bad.symbol_name = "_objc_msgSend_custom_unsupported";
    b_bad.patch_vaddr = 0x2000;
    b_bad.is_weak = false;
    bindings.push_back(b_bad);

    std::string err;
    auto status = dyld.apply_bindings(mem, bindings, 0, err);
    TEST_ASSERT(status == DyldLinkStatus::UndefinedSymbol, "Missing strong symbol must return UndefinedSymbol");
    TEST_ASSERT(!err.empty(), "Error message must be populated");
    TEST_ASSERT(err.find("_objc_msgSend_custom_unsupported") != std::string::npos, "Error must name undefined symbol");

    return true;
}

// Test 4: Weak Symbol Binding Resolves to NULL
bool test_weak_binding_resolves_to_null() {
    std::cout << "[Test 4] Weak symbol binding resolves to NULL without error...\n";

    Memory mem(1024 * 1024);
    DyldEngine dyld;

    // Write a sentinel value at 0x3000
    mem.write_u64(0x3000, 0xAAAAAAAAAAAAAAAAULL);

    std::vector<DyldBindingRecord> bindings;
    DyldBindingRecord b_weak;
    b_weak.symbol_name = "_optional_ios17_api";
    b_weak.patch_vaddr = 0x3000;
    b_weak.is_weak = true;
    bindings.push_back(b_weak);

    std::string err;
    auto status = dyld.apply_bindings(mem, bindings, 0, err);
    TEST_ASSERT(status == DyldLinkStatus::Success, "Weak undefined symbol must succeed");

    std::uint64_t patched_val = mem.read_u64(0x3000);
    TEST_ASSERT(patched_val == 0, "Weak undefined symbol must be patched to NULL (0)");

    return true;
}

// Test 5: Pointer Rebasing with ASLR Slide
bool test_rebase_with_aslr_slide() {
    std::cout << "[Test 5] Pointer rebasing with ASLR slide...\n";

    Memory mem(1024 * 1024);
    DyldEngine dyld;

    // Initial internal pointer at 0x4000 = 0x100000000
    mem.write_u64(0x4000, 0x100000000ULL);

    std::vector<DyldRebaseRecord> rebases;
    DyldRebaseRecord r1;
    r1.patch_vaddr = 0x4000;
    r1.type = 1;
    rebases.push_back(r1);

    std::string err;
    const std::uint64_t slide = 0x0000000004000000ULL; // 64 MB slide
    auto status = dyld.apply_rebases(mem, rebases, slide, 0, err);
    TEST_ASSERT(status == DyldLinkStatus::Success, "apply_rebases must succeed");

    std::uint64_t rebased_val = mem.read_u64(0x4000);
    TEST_ASSERT(rebased_val == 0x104000000ULL, "Rebased pointer must equal original + slide");

    // Rebase with slide = 0 must be a no-op
    status = dyld.apply_rebases(mem, rebases, 0, 0, err);
    TEST_ASSERT(status == DyldLinkStatus::Success, "apply_rebases with slide=0 must succeed");
    TEST_ASSERT(mem.read_u64(0x4000) == 0x104000000ULL, "Pointer must remain unchanged with slide=0");

    return true;
}

// Test 6: Chained Fixups Header Parsing and Boundary Checks
bool test_chained_fixups_parsing() {
    std::cout << "[Test 6] Chained fixups header parsing and boundary checks...\n";

    Memory mem(1024 * 1024);
    DyldEngine dyld;

    // 1. Synthetic valid chained fixups blob
    auto fixups_data = DyldFixtureBuilder::create_synthetic_chained_fixups({"_exit", "_printf"});
    std::string err;
    auto status = dyld.process_chained_fixups(fixups_data.data(), fixups_data.size(), mem, 0, 0, err);
    TEST_ASSERT(status == DyldLinkStatus::Success, "Valid chained fixups must succeed");

    // 2. Truncated chained fixups payload
    status = dyld.process_chained_fixups(fixups_data.data(), 10, mem, 0, 0, err);
    TEST_ASSERT(status == DyldLinkStatus::InvalidChainedFixups, "Truncated fixups must be rejected");

    // 3. Unsupported fixups version (e.g. version 99)
    auto bad_version_data = fixups_data;
    *reinterpret_cast<std::uint32_t*>(bad_version_data.data()) = 99u;
    status = dyld.process_chained_fixups(bad_version_data.data(), bad_version_data.size(), mem, 0, 0, err);
    TEST_ASSERT(status == DyldLinkStatus::InvalidChainedFixups, "Unsupported fixups version must be rejected");

    return true;
}

// Test 7: Dyld Shared Cache Header Validation
bool test_dyld_shared_cache_header_validation() {
    std::cout << "[Test 7] Dyld shared cache header validation...\n";

    // 1. Valid synthetic shared cache
    std::vector<std::string> images = {"/usr/lib/libSystem.B.dylib", "/System/Library/Frameworks/UIKit.framework/UIKit"};
    auto cache_bytes = DyldFixtureBuilder::create_synthetic_shared_cache(images);

    std::vector<DyldCacheMappingInfoRaw> mappings;
    std::vector<std::string> image_paths;
    std::string err;

    auto status = DyldEngine::validate_shared_cache_header(cache_bytes.data(), cache_bytes.size(), mappings, image_paths, err);
    TEST_ASSERT(status == DyldLinkStatus::Success, "Synthetic shared cache must validate successfully");
    TEST_ASSERT(!mappings.empty(), "Cache mappings must not be empty");
    TEST_ASSERT(image_paths.size() == 2, "Image paths count must match input");

    // 2. Corrupted magic in shared cache
    auto bad_magic_cache = cache_bytes;
    bad_magic_cache[0] = 'X'; bad_magic_cache[1] = 'Y';
    status = DyldEngine::validate_shared_cache_header(bad_magic_cache.data(), bad_magic_cache.size(), mappings, image_paths, err);
    TEST_ASSERT(status == DyldLinkStatus::SharedCacheCorrupt, "Corrupted cache magic must return SharedCacheCorrupt");

    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  WINIOS STAGE 148: DYLD & DYNAMIC LINKING TEST SUITE            \n";
    std::cout << "=================================================================\n\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*fn)(), const char* name) {
        if (fn()) {
            std::cout << "  -> PASS: " << name << "\n\n";
            passed++;
        } else {
            std::cout << "  -> FAIL: " << name << "\n\n";
            failed++;
        }
    };

    run_test(test_builtin_libraries_and_symbols, "Builtin iOS System Libraries and Symbol Lookup");
    run_test(test_strong_binding_and_patching, "Strong Symbol Binding and Memory Patching");
    run_test(test_undefined_strong_symbol_rejection, "Undefined Strong Symbol Rejection with Diagnostic");
    run_test(test_weak_binding_resolves_to_null, "Weak Symbol Binding Resolves to NULL");
    run_test(test_rebase_with_aslr_slide, "Pointer Rebasing with ASLR Slide");
    run_test(test_chained_fixups_parsing, "Chained Fixups Header Parsing and Boundary Checks");
    run_test(test_dyld_shared_cache_header_validation, "Dyld Shared Cache Header Validation");

    std::cout << "=================================================================\n";
    std::cout << "STAGE 148 RESULTS: " << passed << " PASSED, " << failed << " FAILED\n";
    std::cout << "=================================================================\n";

    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

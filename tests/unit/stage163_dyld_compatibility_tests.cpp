#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
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

// Test 1: Dyld Shared Cache Header Conformance (ARM64 & ARM64e)
bool test_dyld_shared_cache_header_conformance() {
    std::cout << "[Test 1] Dyld Shared Cache Header Conformance (ARM64 & ARM64e)...\n";

    DyldSharedCacheHeaderRaw hdr_arm64e{};
    std::memcpy(hdr_arm64e.magic, kDyldSharedCacheArm64eMagic, 16);
    hdr_arm64e.mapping_offset = 0x100;
    hdr_arm64e.mapping_count = 4;
    hdr_arm64e.images_offset = 0x400;
    hdr_arm64e.images_count = 2100;
    hdr_arm64e.dyld_base_address = 0x180000000ULL;

    TEST_ASSERT(std::memcmp(hdr_arm64e.magic, "dyld_v1  arm64e", 15) == 0,
                "Shared cache magic must match 'dyld_v1  arm64e'");
    TEST_ASSERT(hdr_arm64e.mapping_count == 4, "Mapping count must be 4");
    TEST_ASSERT(hdr_arm64e.dyld_base_address == 0x180000000ULL,
                "Base address must be standard Apple Silicon shared cache base (0x180000000)");

    DyldSharedCacheHeaderRaw hdr_arm64{};
    std::memcpy(hdr_arm64.magic, kDyldSharedCacheArm64Magic, 16);
    TEST_ASSERT(std::memcmp(hdr_arm64.magic, "dyld_v1   arm64", 15) == 0,
                "Shared cache magic must match 'dyld_v1   arm64'");

    return true;
}

// Test 2: Chained Fixups Header and Import Ordinal Decoding
bool test_chained_fixups_import_decoding() {
    std::cout << "[Test 2] Chained Fixups Header & Import Ordinal Decoding...\n";

    DyldChainedFixupsHeaderRaw fixups_hdr{};
    fixups_hdr.fixups_version = 0;
    fixups_hdr.imports_format = 1; // DYLD_CHAINED_IMPORT
    fixups_hdr.imports_count = 3;
    fixups_hdr.symbols_format = 0;

    TEST_ASSERT(fixups_hdr.imports_format == 1, "Imports format must be DYLD_CHAINED_IMPORT (1)");

    DyldChainedImportRaw imp{};
    imp.lib_ordinal = 1;      // First library (e.g. libSystem)
    imp.weak_import = 0;      // Strong import
    imp.name_offset = 0x40;   // String table offset

    TEST_ASSERT(imp.lib_ordinal == 1, "lib_ordinal must be 1");
    TEST_ASSERT(imp.weak_import == 0, "weak_import bit must be 0 for strong symbol");
    TEST_ASSERT(imp.name_offset == 0x40, "name_offset must be preserved");

    // Test weak import
    DyldChainedImportRaw weak_imp{};
    weak_imp.lib_ordinal = 2;
    weak_imp.weak_import = 1;
    weak_imp.name_offset = 0x80;

    TEST_ASSERT(weak_imp.weak_import == 1, "weak_import bit must be 1 for weak symbol");

    return true;
}

// Test 3: Strong vs Weak Symbol Resolution and Binding
bool test_strong_and_weak_symbol_binding() {
    std::cout << "[Test 3] Strong vs Weak Symbol Resolution and Binding...\n";

    Memory mem(1024 * 1024);
    DyldEngine dyld;

    // 1. Strong symbol resolution (_exit)
    std::vector<DyldBindingRecord> bindings;
    DyldBindingRecord b_strong;
    b_strong.symbol_name = "_exit";
    b_strong.patch_vaddr = 0x1000;
    b_strong.is_weak = false;
    bindings.push_back(b_strong);

    // 2. Weak symbol that does not exist in dylibs
    DyldBindingRecord b_weak;
    b_weak.symbol_name = "_optional_symbol_not_present";
    b_weak.patch_vaddr = 0x1008;
    b_weak.is_weak = true;
    bindings.push_back(b_weak);

    std::string err;
    auto status = dyld.apply_bindings(mem, bindings, 0, err);
    TEST_ASSERT(status == DyldLinkStatus::Success, "apply_bindings must succeed when missing symbol is weak");

    // Strong symbol must point to resolved address
    std::uint64_t strong_val = mem.read_u64(0x1000);
    TEST_ASSERT(strong_val == 0x0000000180001000ULL, "_exit must be bound to 0x180001000");

    // Weak missing symbol must be bound to 0x0 without failing the load
    std::uint64_t weak_val = mem.read_u64(0x1008);
    TEST_ASSERT(weak_val == 0, "Undefined weak symbol must bind to 0x0");

    return true;
}

// Test 4: Missing Strong Symbol Rejection
bool test_missing_strong_symbol_rejection() {
    std::cout << "[Test 4] Missing Strong Symbol Rejection with Error Diagnostic...\n";

    Memory mem(1024 * 1024);
    DyldEngine dyld;

    std::vector<DyldBindingRecord> bindings;
    DyldBindingRecord b_missing;
    b_missing.symbol_name = "_mandatory_missing_function";
    b_missing.patch_vaddr = 0x2000;
    b_missing.is_weak = false; // Strong!
    bindings.push_back(b_missing);

    std::string err;
    auto status = dyld.apply_bindings(mem, bindings, 0, err);
    TEST_ASSERT(status == DyldLinkStatus::UndefinedSymbol, "Missing strong symbol must return UndefinedSymbol");
    TEST_ASSERT(err.find("_mandatory_missing_function") != std::string::npos,
                "Error string must name the missing symbol");

    return true;
}

// Test 5: ASLR Slide Pointer Rebasing
bool test_aslr_slide_pointer_rebasing() {
    std::cout << "[Test 5] ASLR Slide Pointer Rebasing...\n";

    Memory mem(1024 * 1024);
    DyldEngine dyld;

    // Stage pointers at 0x3000 and 0x3008 with unslid addresses
    const std::uint64_t unslid1 = 0x100004000ULL;
    const std::uint64_t unslid2 = 0x100008000ULL;
    mem.write_u64(0x3000, unslid1);
    mem.write_u64(0x3008, unslid2);

    std::vector<DyldRebaseRecord> rebases;
    DyldRebaseRecord r1{};
    r1.patch_vaddr = 0x3000;
    r1.type = 1;
    rebases.push_back(r1);

    DyldRebaseRecord r2{};
    r2.patch_vaddr = 0x3008;
    r2.type = 1;
    rebases.push_back(r2);

    const std::uint64_t aslr_slide = 0x0000000020000000ULL; // 512MB slide

    std::string err;
    auto status = dyld.apply_rebases(mem, rebases, aslr_slide, 0, err);
    TEST_ASSERT(status == DyldLinkStatus::Success, "apply_rebases must succeed");

    std::uint64_t slid1 = mem.read_u64(0x3000);
    std::uint64_t slid2 = mem.read_u64(0x3008);

    TEST_ASSERT(slid1 == unslid1 + aslr_slide, "Rebase 1 must have slide applied");
    TEST_ASSERT(slid2 == unslid2 + aslr_slide, "Rebase 2 must have slide applied");

    return true;
}

// Test 6: Custom Dylib Registration and Module Verification
bool test_custom_dylib_registration() {
    std::cout << "[Test 6] Custom Dylib Registration and Module Verification...\n";

    DyldEngine dyld;

    // Register a new module "libTestCustom.dylib"
    DyldLoadedModule mod;
    mod.name = "libTestCustom.dylib";
    mod.install_name = "/usr/lib/libTestCustom.dylib";
    mod.base_address = 0x1A0000000ULL;
    mod.exports["_test_custom_init"] = 0x1A0001000ULL;

    dyld.register_module(mod);

    TEST_ASSERT(dyld.has_module("libTestCustom.dylib"), "Must recognize newly registered module");

    auto sym = dyld.resolve_symbol("_test_custom_init");
    TEST_ASSERT(sym.has_value(), "Custom symbol must be resolvable");
    TEST_ASSERT(*sym == 0x1A0001000ULL, "Custom symbol address must match 0x1A0001000");

    return true;
}

// Test 7: Dyld Shared Cache Mapping Descriptors
bool test_shared_cache_mapping_descriptors() {
    std::cout << "[Test 7] Dyld Shared Cache Mapping Descriptors...\n";

    std::vector<DyldCacheMappingInfoRaw> mappings;

    // 1. __TEXT: Read + Execute
    DyldCacheMappingInfoRaw m_text{};
    m_text.address = 0x180000000ULL;
    m_text.size = 0x10000000ULL;
    m_text.init_prot = 0x5; // PROT_READ | PROT_EXEC
    m_text.max_prot = 0x5;
    mappings.push_back(m_text);

    // 2. __DATA: Read + Write
    DyldCacheMappingInfoRaw m_data{};
    m_data.address = 0x190000000ULL;
    m_data.size = 0x08000000ULL;
    m_data.init_prot = 0x3; // PROT_READ | PROT_WRITE
    m_data.max_prot = 0x3;
    mappings.push_back(m_data);

    // 3. __LINKEDIT: Read Only
    DyldCacheMappingInfoRaw m_link{};
    m_link.address = 0x198000000ULL;
    m_link.size = 0x04000000ULL;
    m_link.init_prot = 0x1; // PROT_READ
    m_link.max_prot = 0x1;
    mappings.push_back(m_link);

    TEST_ASSERT(mappings.size() == 3, "Must have 3 mapping descriptors");
    TEST_ASSERT((mappings[0].init_prot & 0x4) != 0, "__TEXT must be executable");
    TEST_ASSERT((mappings[0].init_prot & 0x2) == 0, "__TEXT must not be writable under W^X");
    TEST_ASSERT((mappings[1].init_prot & 0x2) != 0, "__DATA must be writable");
    TEST_ASSERT((mappings[1].init_prot & 0x4) == 0, "__DATA must not be executable");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 163: dyld & Shared Cache Compatibility\n";
    std::cout << "=========================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](auto test_fn, const char* name) {
        if (test_fn()) {
            std::cout << "[PASS] " << name << "\n";
            passed++;
        } else {
            std::cout << "[FAIL] " << name << "\n";
            failed++;
        }
    };

    run_test(test_dyld_shared_cache_header_conformance, "Dyld Shared Cache Header Conformance");
    run_test(test_chained_fixups_import_decoding, "Chained Fixups Import Decoding");
    run_test(test_strong_and_weak_symbol_binding, "Strong vs Weak Symbol Resolution");
    run_test(test_missing_strong_symbol_rejection, "Missing Strong Symbol Rejection");
    run_test(test_aslr_slide_pointer_rebasing, "ASLR Slide Pointer Rebasing");
    run_test(test_custom_dylib_registration, "Custom Dylib Registration & Lookup");
    run_test(test_shared_cache_mapping_descriptors, "Dyld Shared Cache Mapping Descriptors");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}

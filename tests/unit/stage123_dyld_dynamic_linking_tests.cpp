#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "loader/dyld_engine.h"
#include "memory/memory.h"

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

bool test_builtin_system_libraries_registration() {
    std::cout << "[Test 1] Builtin System Libraries Registration...\n";

    DyldEngine dyld;
    TEST_ASSERT(dyld.has_module("libSystem.B.dylib"), "libSystem.B.dylib must be registered");
    TEST_ASSERT(dyld.has_module("/usr/lib/libSystem.B.dylib"), "Install path must match");
    TEST_ASSERT(dyld.has_module("CoreFoundation"), "CoreFoundation must be registered");

    const auto* mod = dyld.find_module("libSystem.B.dylib");
    TEST_ASSERT(mod != nullptr, "Module lookup must succeed");
    TEST_ASSERT(mod->exports.count("_exit") > 0, "_exit must be exported");
    TEST_ASSERT(mod->exports.count("_printf") > 0, "_printf must be exported");
    TEST_ASSERT(mod->exports.count("_mach_msg_trap") > 0, "_mach_msg_trap must be exported");
    TEST_ASSERT(mod->exports.count("_bootstrap_look_up") > 0, "_bootstrap_look_up must be exported");

    std::cout << "  Passed (libSystem and CoreFoundation builtins active)\n";
    return true;
}

bool test_symbol_resolution() {
    std::cout << "[Test 2] Dynamic Symbol Resolution...\n";

    DyldEngine dyld;
    auto addr_exit = dyld.resolve_symbol("_exit");
    TEST_ASSERT(addr_exit.has_value(), "_exit must resolve");
    TEST_ASSERT(*addr_exit == 0x0000000180001000ULL, "_exit address match");

    auto addr_mach = dyld.resolve_symbol("_mach_msg_trap");
    TEST_ASSERT(addr_mach.has_value(), "_mach_msg_trap must resolve");
    TEST_ASSERT(*addr_mach == 0x0000000180001080ULL, "_mach_msg_trap address match");

    auto addr_missing = dyld.resolve_symbol("_non_existent_func");
    TEST_ASSERT(!addr_missing.has_value(), "Missing symbol must return nullopt");

    std::cout << "  Passed (Resolved exported symbols)\n";
    return true;
}

bool test_module_registration_and_override() {
    std::cout << "[Test 3] Custom Dynamic Library Registration...\n";

    DyldEngine dyld;
    DyldLoadedModule custom;
    custom.name = "libcustom.dylib";
    custom.install_name = "/usr/local/lib/libcustom.dylib";
    custom.base_address = 0x200000000ULL;
    custom.slide = 0x10000ULL;
    custom.exports["_custom_widget_init"] = 0x200001000ULL;

    dyld.register_module(std::move(custom));

    TEST_ASSERT(dyld.has_module("libcustom.dylib"), "Custom module lookup");
    auto res = dyld.resolve_symbol("_custom_widget_init");
    TEST_ASSERT(res.has_value(), "Custom symbol resolution");
    TEST_ASSERT(*res == 0x200001000ULL + 0x10000ULL, "Address plus slide check");

    std::cout << "  Passed (Custom module registered with slide)\n";
    return true;
}

bool test_aslr_slide_rebasing() {
    std::cout << "[Test 4] Memory Pointer Rebasing with ASLR Slide...\n";

    DyldEngine dyld;
    Memory mem(0x10000); // 64 KB memory

    // Write original unslid pointer to offset 0x1000
    std::uint64_t original_ptr = 0x100002000ULL;
    mem.write_u64(0x1000, original_ptr);

    std::vector<DyldRebaseRecord> rebases;
    DyldRebaseRecord r;
    r.patch_vaddr = 0x1000;
    r.type = 1;
    rebases.push_back(r);

    std::string err;
    std::uint64_t slide = 0x40000ULL; // 256 KB slide
    auto status = dyld.apply_rebases(mem, rebases, slide, 0, err);
    TEST_ASSERT(status == DyldLinkStatus::Success, "apply_rebases must succeed");

    std::uint64_t updated_ptr = mem.read_u64(0x1000);
    TEST_ASSERT(updated_ptr == original_ptr + slide, "Rebased pointer must include slide");

    std::cout << "  Passed (Pointer slid from 0x" << std::hex << original_ptr 
              << " to 0x" << updated_ptr << std::dec << ")\n";
    return true;
}

bool test_external_symbol_binding() {
    std::cout << "[Test 5] Dynamic Symbol Binding to GOT/Lazy Pointers...\n";

    DyldEngine dyld;
    Memory mem(0x10000);

    // Prepare GOT slot at offset 0x2000
    mem.write_u64(0x2000, 0x0ULL);

    std::vector<DyldBindingRecord> bindings;
    DyldBindingRecord b;
    b.symbol_name = "_printf";
    b.patch_vaddr = 0x2000;
    b.addend = 0;
    b.is_weak = false;
    bindings.push_back(b);

    std::string err;
    auto status = dyld.apply_bindings(mem, bindings, 0, err);
    TEST_ASSERT(status == DyldLinkStatus::Success, "apply_bindings must succeed");

    std::uint64_t bound_addr = mem.read_u64(0x2000);
    auto expected_addr = dyld.resolve_symbol("_printf");
    TEST_ASSERT(bound_addr == *expected_addr, "Bound address must match resolved symbol");

    std::cout << "  Passed (Bound _printf to 0x" << std::hex << bound_addr << std::dec << ")\n";
    return true;
}

bool test_weak_undefined_symbol_binding() {
    std::cout << "[Test 6] Weak Undefined Symbol Null-Binding...\n";

    DyldEngine dyld;
    Memory mem(0x10000);

    // Prepare slot with non-zero dummy value
    mem.write_u64(0x3000, 0xDEADBEEFULL);

    std::vector<DyldBindingRecord> bindings;
    DyldBindingRecord b;
    b.symbol_name = "_weak_optional_symbol";
    b.patch_vaddr = 0x3000;
    b.addend = 0;
    b.is_weak = true; // Weak binding
    bindings.push_back(b);

    std::string err;
    auto status = dyld.apply_bindings(mem, bindings, 0, err);
    TEST_ASSERT(status == DyldLinkStatus::Success, "Weak binding of missing symbol must succeed");

    std::uint64_t bound_val = mem.read_u64(0x3000);
    TEST_ASSERT(bound_val == 0, "Weak undefined symbol must bind to NULL");

    std::cout << "  Passed (Weak undefined bound to NULL)\n";
    return true;
}

bool test_strict_undefined_symbol_failure() {
    std::cout << "[Test 7] Mandatory Undefined Symbol Link Error...\n";

    DyldEngine dyld;
    Memory mem(0x10000);

    std::vector<DyldBindingRecord> bindings;
    DyldBindingRecord b;
    b.symbol_name = "_missing_mandatory_symbol";
    b.patch_vaddr = 0x4000;
    b.addend = 0;
    b.is_weak = false; // Mandatory!
    bindings.push_back(b);

    std::string err;
    auto status = dyld.apply_bindings(mem, bindings, 0, err);
    TEST_ASSERT(status == DyldLinkStatus::UndefinedSymbol, "Must fail with UndefinedSymbol");
    TEST_ASSERT(!err.empty(), "Error message must describe missing symbol");

    std::cout << "  Passed (Missing mandatory symbol rejected)\n";
    return true;
}

bool test_chained_fixups_header_parsing() {
    std::cout << "[Test 8] LC_DYLD_CHAINED_FIXUPS Parser...\n";

    DyldEngine dyld;
    Memory mem(0x10000);

    auto fixups_buf = DyldFixtureBuilder::create_synthetic_chained_fixups(
        {"_malloc", "_free", "_printf", "_exit"});

    std::string err;
    auto status = dyld.process_chained_fixups(
        fixups_buf.data(), fixups_buf.size(), mem, 0x100000000ULL, 0x10000ULL, err);
    TEST_ASSERT(status == DyldLinkStatus::Success, "process_chained_fixups must succeed");

    std::cout << "  Passed (Parsed 4 chained fixup imports)\n";
    return true;
}

bool test_dyld_shared_cache_header_validation() {
    std::cout << "[Test 9] dyld_shared_cache_arm64e Header & Image Validation...\n";

    std::vector<std::string> images = {
        "/usr/lib/libSystem.B.dylib",
        "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation",
        "/System/Library/Frameworks/UIKit.framework/UIKit"
    };

    auto cache_buf = DyldFixtureBuilder::create_synthetic_shared_cache(images);

    std::vector<DyldCacheMappingInfoRaw> mappings;
    std::vector<std::string> parsed_images;
    std::string err;

    auto status = DyldEngine::validate_shared_cache_header(
        cache_buf.data(), cache_buf.size(), mappings, parsed_images, err);

    TEST_ASSERT(status == DyldLinkStatus::Success, "validate_shared_cache_header must succeed");
    TEST_ASSERT(mappings.size() == 3, "Must extract 3 mappings (__TEXT, __DATA, __LINKEDIT)");
    TEST_ASSERT(parsed_images.size() == 3, "Must extract 3 image paths");
    TEST_ASSERT(parsed_images[0] == images[0], "First image path match");
    TEST_ASSERT(parsed_images[1] == images[1], "Second image path match");
    TEST_ASSERT(parsed_images[2] == images[2], "Third image path match");

    std::cout << "  Passed (Validated dyld_shared_cache_arm64e with 3 frameworks)\n";
    return true;
}

bool test_corrupted_shared_cache_rejection() {
    std::cout << "[Test 10] Corrupted Shared Cache Header Rejection...\n";

    auto cache_buf = DyldFixtureBuilder::create_synthetic_shared_cache({"/usr/lib/libSystem.B.dylib"});
    // Corrupt magic
    cache_buf[0] = 'X';

    std::vector<DyldCacheMappingInfoRaw> mappings;
    std::vector<std::string> parsed_images;
    std::string err;

    auto status = DyldEngine::validate_shared_cache_header(
        cache_buf.data(), cache_buf.size(), mappings, parsed_images, err);

    TEST_ASSERT(status == DyldLinkStatus::SharedCacheCorrupt, "Must return SharedCacheCorrupt");

    std::cout << "  Passed (Corrupted magic rejected)\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  WINIOS STAGE 123: DYLD & DYNAMIC LINKING SUBSYSTEM TESTS       \n";
    std::cout << "  Platform: iPhone 15 / Apple A16 Bionic (T8120) / ARM64e        \n";
    std::cout << "  Target: /usr/lib/dyld & dyld_shared_cache_arm64e Resolution    \n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int total = 10;

    if (test_builtin_system_libraries_registration()) passed++;
    if (test_symbol_resolution()) passed++;
    if (test_module_registration_and_override()) passed++;
    if (test_aslr_slide_rebasing()) passed++;
    if (test_external_symbol_binding()) passed++;
    if (test_weak_undefined_symbol_binding()) passed++;
    if (test_strict_undefined_symbol_failure()) passed++;
    if (test_chained_fixups_header_parsing()) passed++;
    if (test_dyld_shared_cache_header_validation()) passed++;
    if (test_corrupted_shared_cache_rejection()) passed++;

    std::cout << "-----------------------------------------------------------------\n";
    std::cout << "Stage 123 Tests Complete: " << passed << " / " << total << " passed\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}

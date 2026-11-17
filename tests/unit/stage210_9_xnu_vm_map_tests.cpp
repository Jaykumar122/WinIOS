#include <iostream>
#include <vector>
#include <cstdint>

#include "memory/xnu_vm_map.h"

using namespace winios;
using namespace winios::memory;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_vm_map_operations() {
    std::cout << "[Test 1] Stage 210.9: XnuVmMap Core Operations (Enter, Lookup, Protect, Remove)...\n";

    XnuVmMap vm_map(0x0ULL, 0x1000000ULL, "kernel_map"); // 16 MiB virtual address space
    std::string err;

    // 1. Enter text segment: 0x10000 - 0x20000 (64KB, Read+Execute)
    TEST_CHECK(vm_map.vm_map_enter(0x10000ULL, 0x10000ULL,
                                  kVmProtRead | kVmProtExecute,
                                  kVmProtRead | kVmProtExecute,
                                  "kernel_text", &err),
               "vm_map_enter kernel_text");

    // 2. Enter data segment: 0x20000 - 0x30000 (64KB, Read+Write)
    TEST_CHECK(vm_map.vm_map_enter(0x20000ULL, 0x10000ULL,
                                  kVmProtRead | kVmProtWrite,
                                  kVmProtAll,
                                  "kernel_data", &err),
               "vm_map_enter kernel_data");

    // 3. Collision check: overlapping entry must fail
    TEST_CHECK(!vm_map.vm_map_enter(0x18000ULL, 0x10000ULL, kVmProtDefault, kVmProtAll, "overlap", &err),
               "Overlapping vm_map_enter properly rejected");

    // 4. Lookup tests
    const auto* e_text = vm_map.vm_map_lookup(0x14000ULL);
    TEST_CHECK(e_text != nullptr, "Lookup 0x14000 found text segment");
    TEST_CHECK(e_text->tag == "kernel_text", "Tag matches 'kernel_text'");
    TEST_CHECK((e_text->protection & kVmProtExecute) != 0, "Text segment has execute permission");

    const auto* e_missing = vm_map.vm_map_lookup(0x50000ULL);
    TEST_CHECK(e_missing == nullptr, "Lookup unmapped VA returns nullptr");

    // 5. Protection modification
    // Change kernel_data from RW to Read-only
    TEST_CHECK(vm_map.vm_map_protect(0x20000ULL, 0x10000ULL, kVmProtRead, false, &err),
               "vm_map_protect RW -> RO");
    const auto* e_data = vm_map.vm_map_lookup(0x24000ULL);
    TEST_CHECK(e_data != nullptr, "Lookup 0x24000 found data segment");
    TEST_CHECK(e_data->protection == kVmProtRead, "Protection updated to Read-only");

    // Attempting to exceed max_protection on text segment must fail
    TEST_CHECK(!vm_map.vm_map_protect(0x10000ULL, 0x10000ULL, kVmProtAll, false, &err),
               "Attempt to exceed max_protection rejected");

    // 6. Find space test
    std::uint64_t free_va = 0;
    TEST_CHECK(vm_map.vm_map_find_space(0x10000ULL, 16384, free_va, &err), "vm_map_find_space for 64KB");
    TEST_CHECK(free_va == 0x0ULL, "Finds free gap at 0x0 before text segment");

    std::uint64_t free_va2 = 0;
    TEST_CHECK(vm_map.vm_map_find_space(0x20000ULL, 16384, free_va2, &err), "vm_map_find_space for 128KB");
    TEST_CHECK(free_va2 == 0x30000ULL, "Finds free gap at 0x30000 after data segment");

    // 7. Wire pages
    TEST_CHECK(vm_map.vm_map_wire(0x20000ULL, 0x10000ULL, &err), "Wire kernel_data");
    auto stats = vm_map.get_stats();
    TEST_CHECK(stats.wired_bytes == 0x10000ULL, "Wired bytes reported in stats");

    // 8. Remove segment
    TEST_CHECK(vm_map.vm_map_remove(0x20000ULL, 0x10000ULL, &err), "vm_map_remove kernel_data");
    TEST_CHECK(vm_map.vm_map_lookup(0x24000ULL) == nullptr, "0x24000 no longer mapped");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 210.9 XNU VM Map Tests ===\n";
    test_vm_map_operations();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 210.9: All XNU VM map tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 210.9: " << g_failures << " tests failed.\n";
        return 1;
    }
}

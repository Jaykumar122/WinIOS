#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/kernel_memory_layout.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"

using namespace winios;
using namespace winios::cpu;
using namespace winios::loader;
using namespace winios::memory;
using namespace winios::runtime;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_kernel_virtual_memory_wx() {
    std::cout << "[Test 1] Stage 207.11: Kernel Virtual Memory & W^X Policy...\n";

    KernelMemoryLayout layout;
    std::string err;

    // Add Kernel __TEXT region: Read + Execute, Non-Writable
    KernelMemoryRegion text_r{};
    text_r.name = "__TEXT";
    text_r.kind = MemoryRegionKind::KernelText;
    text_r.virtual_address = 0xFFFFFF8000000000ULL;
    text_r.size = 0x40000ULL;
    text_r.physical_address = 0x800000000ULL;
    text_r.is_readable = true;
    text_r.is_writable = false;
    text_r.is_executable = true;
    TEST_CHECK(layout.add_region(text_r, &err), "Added __TEXT region");

    // Add Kernel __DATA region: Read + Write, Non-Executable
    KernelMemoryRegion data_r{};
    data_r.name = "__DATA";
    data_r.kind = MemoryRegionKind::KernelData;
    data_r.virtual_address = 0xFFFFFF8000040000ULL;
    data_r.size = 0x40000ULL;
    data_r.physical_address = 0x800040000ULL;
    data_r.is_readable = true;
    data_r.is_writable = true;
    data_r.is_executable = false;
    TEST_CHECK(layout.add_region(data_r, &err), "Added __DATA region");

    // Verify W^X compliance
    TEST_CHECK(layout.check_wx_policy(&err), "Kernel memory layout strictly satisfies W^X");

    // Attempting to add RWX region must be proactively rejected
    KernelMemoryRegion bad_r{};
    bad_r.name = "BAD_RWX";
    bad_r.kind = MemoryRegionKind::KernelData;
    bad_r.virtual_address = 0xFFFFFF8000080000ULL;
    bad_r.size = 0x40000ULL;
    bad_r.physical_address = 0x800080000ULL;
    bad_r.is_readable = true;
    bad_r.is_writable = true;
    bad_r.is_executable = true; // W^X violation!
    TEST_CHECK(!layout.add_region(bad_r, &err), "Rejected RWX region addition violating W^X");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 207.11 Kernel Virtual Memory Tests ===\n";
    test_kernel_virtual_memory_wx();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 207.11: All kernel virtual memory tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 207.11: " << g_failures << " tests failed.\n";
        return 1;
    }
}

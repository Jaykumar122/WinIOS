#include "loader/app_inspector.h"
#include "loader/macho_fixture_corpus.h"
#include "loader/userspace_macho_loader.h"
#include "memory/darwin_vm_object.h"

#include <cassert>
#include <iostream>
#include <vector>

using namespace winios;
using namespace winios::loader;
using namespace winios::memory;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at line " << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (0)

static void test_16kb_page_granule_conformance() {
    std::cout << "[Test 1] 16 KB Page Granule Conformance...\n";
    constexpr std::uint64_t k16K = 0x4000;
    TEST_ASSERT(kApplePageSize16K == k16K, "Apple 16 KB page size constant must be 0x4000");

    DarwinVmSpace vm(3001);
    // Map two contiguous 16 KB pages at 0x100000000
    std::uint64_t base_va = 0x100000000ULL;
    std::uint64_t mapped = vm.mmap(base_va, 2 * k16K, kDarwinVmProtRead | kDarwinVmProtWrite,
                                   kDarwinMapFixed | kDarwinMapAnonymous);
    TEST_ASSERT(mapped == base_va, "Region mapped at fixed address");

    // Access within page 0
    TEST_ASSERT(vm.check_access(base_va, 100, kDarwinVmProtRead), "Read within page 0");
    TEST_ASSERT(vm.check_access(base_va, 100, kDarwinVmProtWrite), "Write within page 0");
    TEST_ASSERT(!vm.check_access(base_va, 100, kDarwinVmProtExecute), "W^X: data page must not be executable");

    // Access spanning across the 16 KB boundary into page 1
    TEST_ASSERT(vm.check_access(base_va + k16K - 32, 64, kDarwinVmProtRead), "Cross-boundary access valid");

    // Access extending beyond page 1 must fail
    TEST_ASSERT(!vm.check_access(base_va + 2 * k16K - 16, 32, kDarwinVmProtRead), "Access extending into unmapped page must fail");
    std::cout << "  Passed!\n";
}

static void test_pagezero_guard_band() {
    std::cout << "[Test 2] PAGEZERO Guard Band...\n";
    DarwinVmSpace vm(3002);
    // Unmapped PAGEZERO range 0x0 to 0x100000000
    TEST_ASSERT(!vm.check_access(0x0ULL, 1, kDarwinVmProtRead), "Null pointer read rejected");
    TEST_ASSERT(!vm.check_access(0x0ULL, 1, kDarwinVmProtWrite), "Null pointer write rejected");
    TEST_ASSERT(!vm.check_access(0x0ULL, 1, kDarwinVmProtExecute), "Null pointer execute rejected");

    TEST_ASSERT(!vm.check_access(0x1000ULL, 8, kDarwinVmProtRead), "Sub-page zero read rejected");
    TEST_ASSERT(!vm.check_access(0xFFFFFFULL, 64, kDarwinVmProtWrite), "High pagezero address rejected");
    std::cout << "  Passed!\n";
}

static void test_strict_wx_permissions() {
    std::cout << "[Test 3] Strict W^X Isolation Enforcement...\n";
    auto fixture = PublicMachOCorpus::create_multisegment_executable();

    DarwinVmSpace vm(3003);
    UserspaceLoadConfig config{};
    config.aslr_enabled = false;
    config.page_size = kApplePageSize16K;

    UserspaceLoadResult res = UserspaceMachOLoader::load_into_vm_space(
        fixture.binary_data.data(), fixture.binary_data.size(), vm, config);
    TEST_ASSERT(res.is_success(), "Executable loaded into VM space");

    // Verify all mapped regions satisfy W^X: never both writable and executable
    for (const auto& seg : res.segments) {
        bool is_w = (seg.initprot & kDarwinVmProtWrite) != 0;
        bool is_x = (seg.initprot & kDarwinVmProtExecute) != 0;
        TEST_ASSERT(!(is_w && is_x), "W^X violation: Segment cannot be both Writable and Executable");
    }
    std::cout << "  Passed!\n";
}

static void test_integer_overflow_rejection() {
    std::cout << "[Test 4] Integer Overflow Protection...\n";
    DarwinVmSpace vm(3004);
    std::uint64_t m = vm.mmap(0x200000000ULL, 0x4000, kDarwinVmProtRead, kDarwinMapFixed | kDarwinMapAnonymous);
    TEST_ASSERT(m != 0, "mmap succeeded");

    // Overflowing address calculation
    std::uint64_t high_va = 0xFFFFFFFFFFFFFFF0ULL;
    std::size_t large_size = 32; // high_va + large_size wraps around to 0x10
    TEST_ASSERT(!vm.check_access(high_va, large_size, kDarwinVmProtRead), "Integer overflow access must be rejected");
    std::cout << "  Passed!\n";
}

int main() {
    std::cout << "=== Stage 235.14 Guest Memory Mapping Tests ===" << std::endl;
    test_16kb_page_granule_conformance();
    test_pagezero_guard_band();
    test_strict_wx_permissions();
    test_integer_overflow_rejection();
    std::cout << "=== All Stage 235.14 tests PASSED successfully ===" << std::endl;
    return 0;
}

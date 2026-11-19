#include "loader/app_inspector.h"
#include "loader/macho_fixture_corpus.h"
#include "loader/userspace_macho_loader.h"
#include "memory/darwin_vm_object.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

#if defined(_WIN32)
#include <crtdbg.h>
#endif

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

// -----------------------------------------------------------------------------
// Test 1: Standard Multi-Segment W^X Enforcement & PageZero Guard
// -----------------------------------------------------------------------------
static void test_multisegment_wx_and_pagezero_guard() {
    std::cout << "[Stage 234.20] Test 1: Testing Multi-Segment W^X & PageZero Guard...\n";

    auto fixture = PublicMachOCorpus::create_multisegment_executable();

    // 1. Static AppInspector validation
    auto report = AppInspector::inspect_binary(fixture.binary_data.data(), fixture.binary_data.size());
    TEST_ASSERT(report.is_valid, "Valid executable passes static inspection");
    TEST_ASSERT(report.wx_clean, "Executable must be W^X clean statically");
    TEST_ASSERT(report.page_aligned_16k, "Segments must be 16 KB page-aligned");

    // 2. Runtime VM space mapping
    DarwinVmSpace vm_space(2001);
    UserspaceLoadConfig config{};
    config.aslr_enabled = false;
    config.page_size = kApplePageSize16K;

    UserspaceLoadResult res = UserspaceMachOLoader::load_into_vm_space(
        fixture.binary_data.data(), fixture.binary_data.size(), vm_space, config
    );
    TEST_ASSERT(res.is_success(), "Loading multi-segment binary into DarwinVmSpace must succeed");

    // 3. PAGEZERO unmapped guard verification: NULL pointer dereference must fail
    TEST_ASSERT(!vm_space.check_access(0x0ULL, 1, kDarwinVmProtRead), "0x0 must not be readable");
    TEST_ASSERT(!vm_space.check_access(0x0ULL, 1, kDarwinVmProtWrite), "0x0 must not be writable");
    TEST_ASSERT(!vm_space.check_access(0x0ULL, 1, kDarwinVmProtExecute), "0x0 must not be executable");
    TEST_ASSERT(!vm_space.check_access(0x10ULL, 8, kDarwinVmProtRead), "0x10 offset must not be accessible");
    TEST_ASSERT(!vm_space.check_access(0x1000ULL, 64, kDarwinVmProtWrite), "0x1000 offset must not be accessible");

    // 4. Strict runtime W^X check across all mapped VM entries
    for (const auto& seg : res.segments) {
        if (seg.is_pagezero || seg.vmsize == 0) continue;
        const auto* entry = vm_space.find_entry(seg.mapped_vaddr);
        TEST_ASSERT(entry != nullptr, "Mapped segment must have VM entry");

        const bool is_writable = (entry->prot & kDarwinVmProtWrite) != 0;
        const bool is_executable = (entry->prot & kDarwinVmProtExecute) != 0;
        TEST_ASSERT(!(is_writable && is_executable), "Segment must NEVER be simultaneously Writable and Executable (W^X)");

        if (seg.name == "__TEXT") {
            TEST_ASSERT(is_executable, "__TEXT must be executable");
            TEST_ASSERT(!is_writable, "__TEXT must NOT be writable");
        } else if (seg.name == "__DATA") {
            TEST_ASSERT(is_writable, "__DATA must be writable");
            TEST_ASSERT(!is_executable, "__DATA must NOT be executable");
        } else if (seg.name == "__LINKEDIT") {
            TEST_ASSERT(!is_writable && !is_executable, "__LINKEDIT must be Read-Only");
        }
    }

    std::cout << "  -> Multi-segment W^X and PageZero guard successfully verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Hostile Synthetic Mach-O with RWX Segment Rejection
// -----------------------------------------------------------------------------
static void test_hostile_rwx_segment_rejection() {
    std::cout << "[Stage 234.20] Test 2: Testing Hostile RWX Segment Rejection...\n";

    // Take valid multi-segment executable and mutate the __DATA segment initprot to RWX (0x7)
    auto fixture = PublicMachOCorpus::create_multisegment_executable();
    std::vector<uint8_t> hostile_binary = fixture.binary_data;

    // Search for "__DATA" in load commands and change initprot
    bool patched = false;
    for (size_t i = 32; i + 72 <= hostile_binary.size(); i += 4) {
        if (std::memcmp(hostile_binary.data() + i, "__DATA\0\0\0\0\0\0\0\0\0\0", 16) == 0) {
            // Segment offset: initprot is at offset +52 from segname start (offset + 60 from cmd start)
            uint32_t rwx_prot = 0x7; // Read (1) | Write (2) | Exec (4)
            std::memcpy(hostile_binary.data() + i + 52, &rwx_prot, 4);
            patched = true;
            break;
        }
    }
    TEST_ASSERT(patched, "Located and patched __DATA segment to RWX");

    // 1. Static inspection must reject RWX segment
    auto report = AppInspector::inspect_binary(hostile_binary.data(), hostile_binary.size());
    TEST_ASSERT(!report.is_valid, "Hostile RWX binary must fail static inspection");
    TEST_ASSERT(!report.wx_clean, "Hostile binary must be flagged as !wx_clean");
    TEST_ASSERT(report.rejection_reason.find("W^X violation") != std::string::npos,
                "Rejection reason must explicitly cite W^X violation");

    // 2. Runtime loader must reject RWX segment
    DarwinVmSpace vm_space(2002);
    UserspaceLoadConfig config{};
    config.aslr_enabled = false;
    config.page_size = kApplePageSize16K;

    UserspaceLoadResult res = UserspaceMachOLoader::load_into_vm_space(
        hostile_binary.data(), hostile_binary.size(), vm_space, config
    );
    TEST_ASSERT(!res.is_success(), "Loading hostile RWX binary into DarwinVmSpace must fail");

    std::cout << "  -> Hostile RWX segment properly rejected at both static and runtime boundaries.\n";
}

// -----------------------------------------------------------------------------
// Test 3: 16 KB Page Granule Alignment Verification
// -----------------------------------------------------------------------------
static void test_16k_page_alignment() {
    std::cout << "[Stage 234.20] Test 3: Testing 16 KB Page Granule Alignment & Detection...\n";

    auto fixture = PublicMachOCorpus::create_multisegment_executable();

    // Verify canonical fixture has 16 KB aligned segments
    auto report = AppInspector::inspect_binary(fixture.binary_data.data(), fixture.binary_data.size());
    TEST_ASSERT(report.page_aligned_16k, "Canonical fixture must have 16 KB page alignment");

    for (const auto& seg : report.segments) {
        if (seg.vmaddr != 0) {
            TEST_ASSERT((seg.vmaddr & 0x3FFF) == 0, "Segment virtual address must be aligned to 16 KB (0x4000)");
        }
    }

    // Now mutate __TEXT vmaddr to misaligned offset (e.g. 0x100001000 instead of 0x100000000)
    std::vector<uint8_t> misaligned_binary = fixture.binary_data;
    bool patched = false;
    for (size_t i = 32; i + 72 <= misaligned_binary.size(); i += 4) {
        if (std::memcmp(misaligned_binary.data() + i, "__TEXT\0\0\0\0\0\0\0\0\0\0", 16) == 0) {
            uint64_t unaligned_addr = 0x100001000ULL; // 4 KB aligned, but NOT 16 KB aligned
            std::memcpy(misaligned_binary.data() + i + 16, &unaligned_addr, 8);
            patched = true;
            break;
        }
    }
    TEST_ASSERT(patched, "Patched __TEXT to 16K-misaligned address");

    auto misaligned_report = AppInspector::inspect_binary(misaligned_binary.data(), misaligned_binary.size());
    TEST_ASSERT(!misaligned_report.page_aligned_16k, "Misaligned segment must be flagged as !page_aligned_16k");

    std::cout << "  -> 16 KB page granule alignment checks verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Hostile Malformed PAGEZERO Permissions Rejection
// -----------------------------------------------------------------------------
static void test_hostile_pagezero_permissions() {
    std::cout << "[Stage 234.20] Test 4: Testing Hostile PAGEZERO with Permissions Rejection...\n";

    // Take valid multi-segment executable and grant READ permission to __PAGEZERO
    auto fixture = PublicMachOCorpus::create_multisegment_executable();
    std::vector<uint8_t> hostile_binary = fixture.binary_data;

    bool patched = false;
    for (size_t i = 32; i + 72 <= hostile_binary.size(); i += 4) {
        if (std::memcmp(hostile_binary.data() + i, "__PAGEZERO\0\0\0\0\0\0", 16) == 0) {
            uint32_t read_prot = 0x1; // VM_PROT_READ
            std::memcpy(hostile_binary.data() + i + 52, &read_prot, 4); // initprot
            patched = true;
            break;
        }
    }
    TEST_ASSERT(patched, "Patched __PAGEZERO initprot to VM_PROT_READ");

    // Runtime loader must reject __PAGEZERO with permissions
    DarwinVmSpace vm_space(2003);
    UserspaceLoadConfig config{};
    config.aslr_enabled = false;
    config.page_size = kApplePageSize16K;

    UserspaceLoadResult res = UserspaceMachOLoader::load_into_vm_space(
        hostile_binary.data(), hostile_binary.size(), vm_space, config
    );
    TEST_ASSERT(!res.is_success(), "Loading binary with accessible __PAGEZERO must fail");

    std::cout << "  -> Hostile PAGEZERO with permissions successfully rejected.\n";
}

int main() {
#if defined(_WIN32)
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDOUT);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDOUT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDOUT);
#endif

    std::cout << "=================================================================\n";
    std::cout << "  Stage 234.20: Mach-O Loader & Hardware Permission Test Suite   \n";
    std::cout << "=================================================================\n";

    test_multisegment_wx_and_pagezero_guard();
    test_hostile_rwx_segment_rejection();
    test_16k_page_alignment();
    test_hostile_pagezero_permissions();

    std::cout << "\n=================================================================\n";
    std::cout << "  ALL MACH-O MAPPING & HARDWARE PERMISSION TESTS PASSED!         \n";
    std::cout << "=================================================================\n";
    return 0;
}
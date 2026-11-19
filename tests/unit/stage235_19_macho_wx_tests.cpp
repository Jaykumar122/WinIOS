#include "loader/app_inspector.h"
#include "loader/macho_fixture_corpus.h"
#include "loader/userspace_macho_loader.h"
#include "memory/darwin_vm_object.h"

#include <cassert>
#include <cstring>
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

static void test_multisegment_wx_and_pagezero_guard() {
    std::cout << "[Test 1] Multi-segment W^X compliance & 4GB PAGEZERO guard...\n";
    auto fixture = PublicMachOCorpus::create_multisegment_executable();

    // 1. Static inspection
    auto report = AppInspector::inspect_binary(fixture.binary_data.data(), fixture.binary_data.size());
    TEST_ASSERT(report.is_valid, "Valid Mach-O binary");
    TEST_ASSERT(report.wx_clean, "Statically W^X clean");
    TEST_ASSERT(report.page_aligned_16k, "16 KB page-aligned segments");

    // 2. VM Space load
    DarwinVmSpace vm_space(3001);
    UserspaceLoadConfig config{};
    config.aslr_enabled = false;
    config.page_size = kApplePageSize16K;

    auto res = UserspaceMachOLoader::load_into_vm_space(
        fixture.binary_data.data(), fixture.binary_data.size(), vm_space, config
    );
    TEST_ASSERT(res.is_success(), "Mach-O load into DarwinVmSpace succeeded");

    // 3. PAGEZERO 4GB unmapped guard band
    TEST_ASSERT(!vm_space.check_access(0x0ULL, 1, kDarwinVmProtRead), "0x0 read denied");
    TEST_ASSERT(!vm_space.check_access(0x0ULL, 1, kDarwinVmProtWrite), "0x0 write denied");
    TEST_ASSERT(!vm_space.check_access(0x0ULL, 1, kDarwinVmProtExecute), "0x0 execute denied");
    TEST_ASSERT(!vm_space.check_access(0xFFFFC000ULL, 1, kDarwinVmProtRead), "High PAGEZERO read denied");

    // 4. Runtime W^X check across all mapped entries
    for (const auto& seg : res.segments) {
        if (seg.is_pagezero || seg.vmsize == 0) continue;
        const auto* entry = vm_space.find_entry(seg.mapped_vaddr);
        TEST_ASSERT(entry != nullptr, "VM entry exists for segment");

        const bool is_w = (entry->prot & kDarwinVmProtWrite) != 0;
        const bool is_x = (entry->prot & kDarwinVmProtExecute) != 0;
        TEST_ASSERT(!(is_w && is_x), "Never simultaneously Writable and Executable (W^X)");

        if (seg.name == "__TEXT") {
            TEST_ASSERT(is_x && !is_w, "__TEXT is executable and non-writable");
        } else if (seg.name == "__DATA") {
            TEST_ASSERT(is_w && !is_x, "__DATA is writable and non-executable");
        } else if (seg.name == "__LINKEDIT") {
            TEST_ASSERT(!is_w && !is_x, "__LINKEDIT is read-only");
        }
    }
    std::cout << "  Passed!\n";
}

static void test_aslr_slide_randomization() {
    std::cout << "[Test 2] ASLR slide application and relative distance invariance...\n";
    auto fixture = PublicMachOCorpus::create_multisegment_executable();

    DarwinVmSpace vm_no_aslr(3002);
    UserspaceLoadConfig cfg_no_aslr{};
    cfg_no_aslr.aslr_enabled = false;
    cfg_no_aslr.page_size = kApplePageSize16K;

    auto res_no_aslr = UserspaceMachOLoader::load_into_vm_space(
        fixture.binary_data.data(), fixture.binary_data.size(), vm_no_aslr, cfg_no_aslr
    );
    TEST_ASSERT(res_no_aslr.is_success(), "Baseline load succeeded");

    // Now load with 64 KB ASLR slide (four 16 KB pages)
    constexpr std::uint64_t kSlide = 0x10000ULL;
    DarwinVmSpace vm_aslr(3003);
    UserspaceLoadConfig cfg_aslr{};
    cfg_aslr.aslr_enabled = true;
    cfg_aslr.aslr_slide = kSlide;
    cfg_aslr.page_size = kApplePageSize16K;

    auto res_aslr = UserspaceMachOLoader::load_into_vm_space(
        fixture.binary_data.data(), fixture.binary_data.size(), vm_aslr, cfg_aslr
    );
    TEST_ASSERT(res_aslr.is_success(), "ASLR load succeeded");
    TEST_ASSERT(res_aslr.aslr_slide == kSlide, "Slide reported correctly");
    TEST_ASSERT(res_aslr.entry_point == res_no_aslr.entry_point + kSlide, "Entry point shifted by slide");

    // Verify segments slided correctly
    std::uint64_t no_aslr_text = 0, no_aslr_data = 0;
    std::uint64_t aslr_text = 0, aslr_data = 0;

    for (const auto& s : res_no_aslr.segments) {
        if (s.name == "__TEXT") no_aslr_text = s.mapped_vaddr;
        if (s.name == "__DATA") no_aslr_data = s.mapped_vaddr;
    }
    for (const auto& s : res_aslr.segments) {
        if (s.name == "__TEXT") aslr_text = s.mapped_vaddr;
        if (s.name == "__DATA") aslr_data = s.mapped_vaddr;
        if (s.is_pagezero) {
            TEST_ASSERT(s.mapped_vaddr == 0, "PAGEZERO must not be shifted by ASLR slide");
        } else {
            TEST_ASSERT(s.mapped_vaddr == s.vmaddr + kSlide, "Non-pagezero segment shifted by ASLR slide");
        }
    }

    TEST_ASSERT(aslr_text == no_aslr_text + kSlide, "__TEXT shifted by slide");
    TEST_ASSERT(aslr_data == no_aslr_data + kSlide, "__DATA shifted by slide");
    TEST_ASSERT((aslr_data - aslr_text) == (no_aslr_data - no_aslr_text), "Relative segment delta invariant");
    std::cout << "  Passed!\n";
}

static void test_hostile_wx_violation_rejection() {
    std::cout << "[Test 3] Hostile RWX segment rejection...\n";
    auto fixture = PublicMachOCorpus::create_multisegment_executable();
    auto bad_bin = fixture.binary_data;

    // Mutate __DATA initprot to RWX (0x7)
    bool patched = false;
    for (size_t i = 32; i + 72 <= bad_bin.size(); i += 4) {
        if (std::memcmp(bad_bin.data() + i, "__DATA\0\0\0\0\0\0\0\0\0\0", 16) == 0) {
            uint32_t rwx_prot = 0x7;
            std::memcpy(bad_bin.data() + i + 52, &rwx_prot, 4);
            patched = true;
            break;
        }
    }
    TEST_ASSERT(patched, "Located and mutated __DATA to RWX");

    // Static inspection must fail
    auto report = AppInspector::inspect_binary(bad_bin.data(), bad_bin.size());
    TEST_ASSERT(!report.is_valid, "Static validation failed");
    TEST_ASSERT(!report.wx_clean, "Not W^X clean");

    // Runtime loader must fail
    DarwinVmSpace vm_space(3004);
    UserspaceLoadConfig config{};
    auto res = UserspaceMachOLoader::load_into_vm_space(
        bad_bin.data(), bad_bin.size(), vm_space, config
    );
    TEST_ASSERT(!res.is_success(), "Runtime load rejected RWX segment");
    std::cout << "  Passed!\n";
}

static void test_misaligned_segment_rejection() {
    std::cout << "[Test 4] 16 KB misaligned segment rejection...\n";
    auto fixture = PublicMachOCorpus::create_multisegment_executable();
    auto misaligned_bin = fixture.binary_data;

    // Mutate __TEXT vmaddr to misaligned 4 KB address
    bool patched = false;
    for (size_t i = 32; i + 72 <= misaligned_bin.size(); i += 4) {
        if (std::memcmp(misaligned_bin.data() + i, "__TEXT\0\0\0\0\0\0\0\0\0\0", 16) == 0) {
            uint64_t unaligned_addr = 0x100001000ULL; // Not aligned to 16 KB (0x4000)
            std::memcpy(misaligned_bin.data() + i + 16, &unaligned_addr, 8);
            patched = true;
            break;
        }
    }
    TEST_ASSERT(patched, "Patched __TEXT to unaligned vmaddr");

    auto report = AppInspector::inspect_binary(misaligned_bin.data(), misaligned_bin.size());
    TEST_ASSERT(!report.page_aligned_16k, "Flagged as not 16k-aligned");
    std::cout << "  Passed!\n";
}

int main() {
    std::cout << "=== Stage 235.19 Mach-O Mapping & W^X Revalidation Tests ===" << std::endl;
    test_multisegment_wx_and_pagezero_guard();
    test_aslr_slide_randomization();
    test_hostile_wx_violation_rejection();
    test_misaligned_segment_rejection();
    std::cout << "=== All Stage 235.19 tests PASSED successfully ===" << std::endl;
    return 0;
}

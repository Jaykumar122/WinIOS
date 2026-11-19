#include "loader/app_inspector.h"
#include "loader/macho_user_loader.h"
#include "loader/embedded_campaign230_workloads.inl"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "memory/virtual_page_table.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #cond << std::endl; \
        std::exit(1); \
    } \
} while(0)

using namespace winios;
using namespace winios::loader;
using namespace winios::memory;

// Helper to locate LC_SEGMENT_64 by name and mutate its properties in a binary buffer
static bool mutate_segment(std::vector<uint8_t>& bin, const char* segname,
                           uint64_t* new_vmaddr, uint64_t* new_vmsize,
                           uint64_t* new_fileoff, uint64_t* new_filesize,
                           int32_t* new_maxprot, int32_t* new_initprot) {
    if (bin.size() < 32) return false;
    uint32_t ncmds = 0;
    std::memcpy(&ncmds, bin.data() + 16, 4);

    size_t offset = 32;
    for (uint32_t i = 0; i < ncmds; ++i) {
        if (offset + 8 > bin.size()) return false;
        uint32_t cmd = 0;
        uint32_t cmdsize = 0;
        std::memcpy(&cmd, bin.data() + offset, 4);
        std::memcpy(&cmdsize, bin.data() + offset + 4, 4);
        if (cmdsize < 8 || offset + cmdsize > bin.size()) return false;

        if (cmd == 0x19 /* LC_SEGMENT_64 */ && cmdsize >= 72) {
            char name[17] = {0};
            std::memcpy(name, bin.data() + offset + 8, 16);
            if (std::strcmp(name, segname) == 0) {
                if (new_vmaddr) std::memcpy(bin.data() + offset + 24, new_vmaddr, 8);
                if (new_vmsize) std::memcpy(bin.data() + offset + 32, new_vmsize, 8);
                if (new_fileoff) std::memcpy(bin.data() + offset + 40, new_fileoff, 8);
                if (new_filesize) std::memcpy(bin.data() + offset + 48, new_filesize, 8);
                if (new_maxprot) std::memcpy(bin.data() + offset + 56, new_maxprot, 4);
                if (new_initprot) std::memcpy(bin.data() + offset + 60, new_initprot, 4);
                return true;
            }
        }
        offset += cmdsize;
    }
    return false;
}

// 1. Verify valid baseline Mach-O binary and W^X enforcement in page tables
static void test_valid_baseline_macho() {
    std::cout << "[Stage 232.19] Testing valid baseline Mach-O binary...\n";

    const auto& base_bin = loader::get_embedded_campaign230_app_settings();
    TEST_ASSERT(!base_bin.empty());

    auto report = AppInspector::inspect_binary(base_bin.data(), base_bin.size());
    TEST_ASSERT(report.is_valid);
    TEST_ASSERT(report.wx_clean);
    TEST_ASSERT(report.page_aligned_16k);
    TEST_ASSERT(report.cputype == AppInspector::kCpuTypeArm64);
    TEST_ASSERT(report.filetype == AppInspector::kMhExecute);

    // Load with MachoUserLoader into real address space
    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());
    const uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "uas_wx_test");

    auto loaded = MachoUserLoader::load_image(base_bin, uas, ppm, mem);
    TEST_ASSERT(loaded.valid);
    TEST_ASSERT(loaded.entry_point != 0);
    TEST_ASSERT(loaded.text_vmaddr != 0);

    // Verify W^X permissions on loaded page table entries:
    // 1. __TEXT must be executable, but NEVER writable (AP=0b11, PXN=1, UXN=0)
    PageDescriptorFields text_desc{};
    uint64_t text_pa = uas.translate(loaded.text_vmaddr, &text_desc);
    TEST_ASSERT(text_pa != 0);
    TEST_ASSERT(text_desc.valid);
    TEST_ASSERT(text_desc.el0_accessible);
    TEST_ASSERT(!text_desc.writable); // W^X: Executable segment must NOT be writable!
    TEST_ASSERT(text_desc.user_execute);

    // 2. __DATA must be writable or read-only, but NEVER executable (UXN=1)
    if (loaded.data_vmaddr != 0) {
        PageDescriptorFields data_desc{};
        uint64_t data_pa = uas.translate(loaded.data_vmaddr, &data_desc);
        TEST_ASSERT(data_pa != 0);
        TEST_ASSERT(data_desc.valid);
        TEST_ASSERT(data_desc.el0_accessible);
        TEST_ASSERT(!data_desc.user_execute); // W^X: Data segment must NOT be executable!
    }

    // 3. NULL / page zero must be completely unmapped
    PageDescriptorFields zero_desc{};
    uint64_t zero_pa = uas.translate(0x00000000ULL, &zero_desc);
    TEST_ASSERT(zero_pa == 0 || !zero_desc.valid);

    std::cout << "  -> Baseline Mach-O validation and W^X mapping verified.\n";
}

// 2. Test rejection of W^X violations (simultaneous Write + Execute)
static void test_wx_violation_rejection() {
    std::cout << "[Stage 232.19] Testing rejection of W^X violations (Write + Execute)...\n";

    const auto& base_bin = loader::get_embedded_campaign230_app_settings();
    auto bad_bin = base_bin;

    // Mutate __TEXT to have VM_PROT_READ (1) | VM_PROT_WRITE (2) | VM_PROT_EXECUTE (4) = 7
    int32_t wx_prot = 7;
    bool mutated = mutate_segment(bad_bin, "__TEXT", nullptr, nullptr, nullptr, nullptr, nullptr, &wx_prot);
    TEST_ASSERT(mutated);

    // 1. AppInspector MUST reject
    auto report = AppInspector::inspect_binary(bad_bin.data(), bad_bin.size());
    TEST_ASSERT(!report.is_valid);
    TEST_ASSERT(!report.wx_clean);
    TEST_ASSERT(report.rejection_reason.find("W^X violation") != std::string::npos);

    // 2. MachoUserLoader MUST reject
    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());
    const uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "uas_wx_bad");

    auto loaded = MachoUserLoader::load_image(bad_bin, uas, ppm, mem);
    TEST_ASSERT(!loaded.valid);
    TEST_ASSERT(loaded.error_message.find("W^X violation") != std::string::npos);

    std::cout << "  -> W^X violation correctly rejected by inspector and loader.\n";
}

// 3. Test rejection of misaligned segments (< 16 KB page granule)
static void test_page_alignment_16k_rejection() {
    std::cout << "[Stage 232.19] Testing rejection of non-16KB aligned segments...\n";

    const auto& base_bin = loader::get_embedded_campaign230_app_settings();
    auto bad_bin = base_bin;

    // Misalign __DATA vmaddr to 0x10002000 (aligned to 4KB or 8KB, but not 16KB 0x4000)
    uint64_t misaligned_va = 0x10002000ULL;
    bool mutated = mutate_segment(bad_bin, "__DATA", &misaligned_va, nullptr, nullptr, nullptr, nullptr, nullptr);
    TEST_ASSERT(mutated);

    // AppInspector detects non-16k alignment
    auto report = AppInspector::inspect_binary(bad_bin.data(), bad_bin.size());
    TEST_ASSERT(!report.page_aligned_16k);

    // MachoUserLoader MUST reject
    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());
    const uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "uas_misaligned");

    auto loaded = MachoUserLoader::load_image(bad_bin, uas, ppm, mem);
    TEST_ASSERT(!loaded.valid);
    TEST_ASSERT(loaded.error_message.find("16 KB") != std::string::npos);

    std::cout << "  -> Non-16KB page alignment correctly detected and rejected.\n";
}

// 4. Test rejection of overlapping virtual address ranges
static void test_overlapping_segments_rejection() {
    std::cout << "[Stage 232.19] Testing rejection of overlapping segment virtual addresses...\n";

    const auto& base_bin = loader::get_embedded_campaign230_app_settings();
    auto bad_bin = base_bin;

    // Set __DATA vmaddr equal to __TEXT vmaddr
    uint64_t text_va = 0x10000000ULL; // standard __TEXT address
    bool mutated = mutate_segment(bad_bin, "__DATA", &text_va, nullptr, nullptr, nullptr, nullptr, nullptr);
    TEST_ASSERT(mutated);

    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());
    const uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "uas_overlap");

    auto loaded = MachoUserLoader::load_image(bad_bin, uas, ppm, mem);
    TEST_ASSERT(!loaded.valid);
    TEST_ASSERT(loaded.error_message.find("overlap") != std::string::npos);

    std::cout << "  -> Overlapping segment virtual addresses correctly rejected.\n";
}

// 5. Test rejection of out-of-bounds segment filesize
static void test_out_of_bounds_filesize_rejection() {
    std::cout << "[Stage 232.19] Testing rejection of out-of-bounds segment filesize...\n";

    const auto& base_bin = loader::get_embedded_campaign230_app_settings();
    auto bad_bin = base_bin;

    // Set __TEXT filesize to extend far beyond the actual binary size
    uint64_t huge_filesize = bad_bin.size() + 0x100000ULL;
    bool mutated = mutate_segment(bad_bin, "__TEXT", nullptr, nullptr, nullptr, &huge_filesize, nullptr, nullptr);
    TEST_ASSERT(mutated);

    auto report = AppInspector::inspect_binary(bad_bin.data(), bad_bin.size());
    TEST_ASSERT(!report.is_valid);
    TEST_ASSERT(report.rejection_reason.find("exceeds binary bounds") != std::string::npos);

    std::cout << "  -> Out-of-bounds segment filesize correctly rejected.\n";
}

// 6. Test rejection of corrupt __PAGEZERO permissions
static void test_pagezero_permissions_rejection() {
    std::cout << "[Stage 232.19] Testing rejection of __PAGEZERO with non-zero permissions...\n";

    const auto& base_bin = loader::get_embedded_campaign230_app_settings();
    auto bad_bin = base_bin;

    int32_t read_prot = 1; // VM_PROT_READ
    bool mutated = mutate_segment(bad_bin, "__PAGEZERO", nullptr, nullptr, nullptr, nullptr, &read_prot, &read_prot);
    TEST_ASSERT(mutated);

    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());
    const uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "uas_bad_pagezero");

    auto loaded = MachoUserLoader::load_image(bad_bin, uas, ppm, mem);
    TEST_ASSERT(!loaded.valid);
    TEST_ASSERT(loaded.error_message.find("__PAGEZERO") != std::string::npos);

    std::cout << "  -> Malformed __PAGEZERO permissions correctly rejected.\n";
}

int main() {
    std::cout << "=== Stage 232.19: Mach-O Loader & W^X Enforcement Tests ===\n";
    test_valid_baseline_macho();
    test_wx_violation_rejection();
    test_page_alignment_16k_rejection();
    test_overlapping_segments_rejection();
    test_out_of_bounds_filesize_rejection();
    test_pagezero_permissions_rejection();
    std::cout << "=== All Stage 232.19 Tests PASSED ===\n";
    return 0;
}

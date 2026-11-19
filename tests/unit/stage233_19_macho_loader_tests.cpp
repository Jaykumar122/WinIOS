#include "loader/app_inspector.h"
#include "loader/macho_user_loader.h"
#include "loader/embedded_campaign230_workloads.inl"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "memory/virtual_page_table.h"

#include <iostream>
#include <vector>
#include <string>
#include <cstring>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Mach-O Loader Assertion Failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::loader;
using namespace winios::memory;

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

static void test_baseline_macho_wx_cleanliness() {
    std::cout << "[Stage 233.19] Testing baseline Mach-O and page-level W^X enforcement...\n";
    const auto& base_bin = loader::get_embedded_campaign230_app_settings();
    TEST_ASSERT(!base_bin.empty());

    auto report = AppInspector::inspect_binary(base_bin.data(), base_bin.size());
    TEST_ASSERT(report.is_valid);
    TEST_ASSERT(report.wx_clean);
    TEST_ASSERT(report.page_aligned_16k);
    TEST_ASSERT(report.cputype == AppInspector::kCpuTypeArm64);
    TEST_ASSERT(report.filetype == AppInspector::kMhExecute);

    Memory mem(0x20000000ULL); // 512 MB
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());
    const uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "uas_macho_test");

    auto loaded = MachoUserLoader::load_image(base_bin, uas, ppm, mem);
    TEST_ASSERT(loaded.valid);
    TEST_ASSERT(loaded.entry_point != 0);
    TEST_ASSERT(loaded.text_vmaddr != 0);

    // Verify __TEXT: Executable, NOT Writable
    PageDescriptorFields text_desc{};
    uint64_t text_pa = uas.translate(loaded.text_vmaddr, &text_desc);
    TEST_ASSERT(text_pa != 0);
    TEST_ASSERT(text_desc.valid);
    TEST_ASSERT(text_desc.el0_accessible);
    TEST_ASSERT(!text_desc.writable);
    TEST_ASSERT(text_desc.user_execute);

    // Verify __DATA: Writable or Readable, NOT Executable
    if (loaded.data_vmaddr != 0) {
        PageDescriptorFields data_desc{};
        uint64_t data_pa = uas.translate(loaded.data_vmaddr, &data_desc);
        TEST_ASSERT(data_pa != 0);
        TEST_ASSERT(data_desc.valid);
        TEST_ASSERT(data_desc.el0_accessible);
        TEST_ASSERT(!data_desc.user_execute);
    }

    // Verify __PAGEZERO / NULL: unmapped
    PageDescriptorFields zero_desc{};
    uint64_t zero_pa = uas.translate(0x0ULL, &zero_desc);
    TEST_ASSERT(zero_pa == 0 || !zero_desc.valid);
}

static void test_wx_violation_rejection() {
    std::cout << "[Stage 233.19] Testing rejection of W^X violations...\n";
    const auto& base_bin = loader::get_embedded_campaign230_app_settings();
    auto bad_bin = base_bin;

    // Mutate __TEXT to have VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE (7)
    int32_t wx_prot = 7;
    TEST_ASSERT(mutate_segment(bad_bin, "__TEXT", nullptr, nullptr, nullptr, nullptr, nullptr, &wx_prot));

    auto report = AppInspector::inspect_binary(bad_bin.data(), bad_bin.size());
    TEST_ASSERT(!report.is_valid);
    TEST_ASSERT(!report.wx_clean);

    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());
    const uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "uas_bad_wx");

    auto loaded = MachoUserLoader::load_image(bad_bin, uas, ppm, mem);
    TEST_ASSERT(!loaded.valid);
}

static void test_misaligned_segment_rejection() {
    std::cout << "[Stage 233.19] Testing rejection of misaligned segments (<16KB)...\n";
    const auto& base_bin = loader::get_embedded_campaign230_app_settings();
    auto bad_bin = base_bin;

    // Misalign __DATA vmaddr to 0x10001000 (4KB aligned, but not 16KB)
    uint64_t misaligned_va = 0x10001000ULL;
    TEST_ASSERT(mutate_segment(bad_bin, "__DATA", &misaligned_va, nullptr, nullptr, nullptr, nullptr, nullptr));

    auto report = AppInspector::inspect_binary(bad_bin.data(), bad_bin.size());
    TEST_ASSERT(!report.page_aligned_16k);

    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());
    const uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "uas_bad_align");

    auto loaded = MachoUserLoader::load_image(bad_bin, uas, ppm, mem);
    TEST_ASSERT(!loaded.valid);
}

static void test_macho_header_adversarial_tampering() {
    std::cout << "[Stage 233.19] Testing Mach-O header tampering rejection...\n";
    const auto& base_bin = loader::get_embedded_campaign230_app_settings();

    // 1. Corrupt magic (e.g. 0xDEADBEEF instead of 0xFEEDFACF)
    auto bad_magic = base_bin;
    uint32_t bogus_magic = 0xDEADBEEF;
    std::memcpy(bad_magic.data(), &bogus_magic, 4);
    auto report = AppInspector::inspect_binary(bad_magic.data(), bad_magic.size());
    TEST_ASSERT(!report.is_valid);

    // 2. Corrupt CPU type (e.g. x86_64 instead of ARM64)
    auto bad_cpu = base_bin;
    uint32_t x86_cpu = 0x01000007; // CPU_TYPE_X86_64
    std::memcpy(bad_cpu.data() + 4, &x86_cpu, 4);
    report = AppInspector::inspect_binary(bad_cpu.data(), bad_cpu.size());
    TEST_ASSERT(!report.is_valid);

    // 3. Corrupt filetype (e.g. MH_DYLIB instead of MH_EXECUTE)
    auto bad_type = base_bin;
    uint32_t dylib_type = 0x6; // MH_DYLIB
    std::memcpy(bad_type.data() + 12, &dylib_type, 4);
    report = AppInspector::inspect_binary(bad_type.data(), bad_type.size());
    TEST_ASSERT(!report.is_valid);

    // 4. Truncated binary (smaller than mach_header_64 = 32 bytes)
    std::vector<uint8_t> tiny_bin = {0xCF, 0xFA, 0xED, 0xFE};
    report = AppInspector::inspect_binary(tiny_bin.data(), tiny_bin.size());
    TEST_ASSERT(!report.is_valid);
}

static void test_overlapping_and_bounds_violations() {
    std::cout << "[Stage 233.19] Testing overlapping segments and out-of-bounds filesizes...\n";
    const auto& base_bin = loader::get_embedded_campaign230_app_settings();

    // Overlapping segments: __DATA vmaddr set to __TEXT vmaddr
    auto overlap_bin = base_bin;
    uint64_t text_va = 0x10000000ULL;
    TEST_ASSERT(mutate_segment(overlap_bin, "__DATA", &text_va, nullptr, nullptr, nullptr, nullptr, nullptr));

    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());
    const uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "uas_overlap");

    auto loaded = MachoUserLoader::load_image(overlap_bin, uas, ppm, mem);
    TEST_ASSERT(!loaded.valid);

    // Out-of-bounds filesize
    auto oob_bin = base_bin;
    uint64_t huge_filesize = base_bin.size() + 0x100000ULL;
    TEST_ASSERT(mutate_segment(oob_bin, "__TEXT", nullptr, nullptr, nullptr, &huge_filesize, nullptr, nullptr));

    auto report = AppInspector::inspect_binary(oob_bin.data(), oob_bin.size());
    TEST_ASSERT(!report.is_valid);
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "  Stage 233.19: Mach-O Loader & W^X Enforcement     \n";
    std::cout << "====================================================\n";

    test_baseline_macho_wx_cleanliness();
    test_wx_violation_rejection();
    test_misaligned_segment_rejection();
    test_macho_header_adversarial_tampering();
    test_overlapping_and_bounds_violations();

    std::cout << "\n[Stage 233.19] ALL MACH-O LOADER TESTS PASSED.\n";
    return 0;
}

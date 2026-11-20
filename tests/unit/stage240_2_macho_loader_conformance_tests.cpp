// ============================================================================
// Stage 240.2 & 240.3: Mach-O Loader Conformance & Guest Memory Layout Tests
// Verifies Mach-O 64-bit ARM64 format, 16KB granule, W^X enforcement,
// segment boundaries, entry point validation, and malformed binary rejection.
// ============================================================================

#include "loader/app_inspector.h"
#include "loader/macho_user_loader.h"
#include "loader/embedded_campaign240_workloads.inl"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "memory/virtual_page_table.h"

#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Mach-O Conformance Assertion Failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
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

static void test_campaign240_workload_inspection() {
    std::cout << "[Test 1] Inspecting Campaign 240 genuine compiled Mach-O binaries...\n";
    const std::vector<std::string> names = {
        "arith_control", "mem_func", "syscall_lifecycle", "multi_app_worker"
    };

    for (const auto& name : names) {
        auto bin = load_campaign240_workload(name);
        TEST_ASSERT(!bin.empty());

        auto report = AppInspector::inspect_binary(bin.data(), bin.size());
        TEST_ASSERT(report.is_valid);
        TEST_ASSERT(report.wx_clean);
        TEST_ASSERT(report.page_aligned_16k);
        TEST_ASSERT(report.cputype == AppInspector::kCpuTypeArm64);
        TEST_ASSERT(report.filetype == AppInspector::kMhExecute);

        std::string err;
        TEST_ASSERT(MachoUserLoader::validate_header(bin.data(), bin.size(), &err));
    }
}

static void test_macho_memory_mapping_and_wx() {
    std::cout << "[Test 2] Loading into 16KB address space and verifying W^X permissions...\n";
    auto bin = load_campaign240_workload("arith_control");
    TEST_ASSERT(!bin.empty());

    Memory mem(0x20000000ULL); // 512 MB
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());
    const uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "uas_stage240_test");

    auto loaded = MachoUserLoader::load_image(bin, uas, ppm, mem);
    TEST_ASSERT(loaded.valid);
    TEST_ASSERT(loaded.entry_point >= 0x10000ULL);
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

    // Verify __PAGEZERO (Address 0x0) is unmapped
    PageDescriptorFields zero_desc{};
    uint64_t zero_pa = uas.translate(0x0ULL, &zero_desc);
    TEST_ASSERT(zero_pa == 0 || !zero_desc.valid);
}

static void test_rejection_of_malformed_magic() {
    std::cout << "[Test 3] Testing rejection of corrupted Mach-O magic...\n";
    auto bin = load_campaign240_workload("arith_control");
    bin[0] = 0xDE;
    bin[1] = 0xAD;
    bin[2] = 0xBE;
    bin[3] = 0xEF;

    std::string err;
    TEST_ASSERT(!MachoUserLoader::validate_header(bin.data(), bin.size(), &err));

    auto report = AppInspector::inspect_binary(bin.data(), bin.size());
    TEST_ASSERT(!report.is_valid);
}

static void test_rejection_of_non_arm64_arch() {
    std::cout << "[Test 4] Testing rejection of non-ARM64 architecture...\n";
    auto bin = load_campaign240_workload("arith_control");
    // Change CPU type to x86_64 (0x01000007)
    uint32_t x86_cputype = 0x01000007;
    std::memcpy(bin.data() + 4, &x86_cputype, 4);

    std::string err;
    TEST_ASSERT(!MachoUserLoader::validate_header(bin.data(), bin.size(), &err));

    auto report = AppInspector::inspect_binary(bin.data(), bin.size());
    TEST_ASSERT(!report.is_valid || report.cputype != AppInspector::kCpuTypeArm64);
}

static void test_rejection_of_wx_violations() {
    std::cout << "[Test 5] Testing rejection of mutated W^X violations...\n";
    auto bin = load_campaign240_workload("arith_control");
    // Mutate __TEXT to have VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE (7)
    int32_t wx_prot = 7;
    TEST_ASSERT(mutate_segment(bin, "__TEXT", nullptr, nullptr, nullptr, nullptr, nullptr, &wx_prot));

    auto report = AppInspector::inspect_binary(bin.data(), bin.size());
    TEST_ASSERT(!report.is_valid || !report.wx_clean);

    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());
    const uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "uas_bad_wx");

    auto loaded = MachoUserLoader::load_image(bin, uas, ppm, mem);
    TEST_ASSERT(!loaded.valid);
}

static void test_rejection_of_misaligned_segments() {
    std::cout << "[Test 6] Testing rejection of misaligned segments (<16KB)...\n";
    auto bin = load_campaign240_workload("arith_control");
    // Misalign __DATA vmaddr to 0x10001000 (4KB aligned, but not 16KB)
    uint64_t misaligned_va = 0x10001000ULL;
    TEST_ASSERT(mutate_segment(bin, "__DATA", &misaligned_va, nullptr, nullptr, nullptr, nullptr, nullptr));

    auto report = AppInspector::inspect_binary(bin.data(), bin.size());
    TEST_ASSERT(!report.page_aligned_16k);

    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());
    const uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "uas_bad_align");

    auto loaded = MachoUserLoader::load_image(bin, uas, ppm, mem);
    TEST_ASSERT(!loaded.valid);
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 240.2 & 240.3: Mach-O Loader Conformance & Memory Layout\n";
    std::cout << "=================================================================\n";

    test_campaign240_workload_inspection();
    test_macho_memory_mapping_and_wx();
    test_rejection_of_malformed_magic();
    test_rejection_of_non_arm64_arch();
    test_rejection_of_wx_violations();
    test_rejection_of_misaligned_segments();

    std::cout << "[SUCCESS] Mach-O loader conformance & memory layout verified.\n";
    return 0;
}

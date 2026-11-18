#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>

#include "loader/macho_user_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::loader;

void test_macho_header_fuzzing() {
    std::cout << "[Test 1] Malformed Mach-O header fuzzing...\n";

    // Truncated header (< 32 bytes)
    std::vector<uint8_t> tiny(16, 0);
    std::string err;
    TEST_ASSERT(!MachoUserLoader::validate_header(tiny.data(), tiny.size(), &err));
    TEST_ASSERT(!err.empty());

    // Bad magic
    std::vector<uint8_t> bad_magic(32, 0);
    uint32_t bogus_magic = 0xCAFEBABE;
    std::memcpy(bad_magic.data(), &bogus_magic, 4);
    TEST_ASSERT(!MachoUserLoader::validate_header(bad_magic.data(), bad_magic.size(), &err));

    // Bad cputype (e.g. x86_64 = 0x01000007 instead of arm64 = 0x0100000C)
    std::vector<uint8_t> bad_cpu(32, 0);
    uint32_t good_magic = kMhMagic64;
    int32_t x86_cpu = 0x01000007;
    std::memcpy(bad_cpu.data(), &good_magic, 4);
    std::memcpy(bad_cpu.data() + 4, &x86_cpu, 4);
    TEST_ASSERT(!MachoUserLoader::validate_header(bad_cpu.data(), bad_cpu.size(), &err));
}

void test_macho_structural_hardening() {
    std::cout << "[Test 2] Structural Mach-O validation hardening...\n";

    Memory mem(0x800000);
    memory::PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);
    uint64_t root_l2 = ppm.allocate_page();
    memory::UserAddressSpace uas(mem, ppm, root_l2, "hardening_vm");

    // 1. Truncated command stream (sizeofcmds claims 200 bytes, but binary is only 48)
    std::vector<uint8_t> trunc_cmds(48, 0);
    uint32_t magic = kMhMagic64;
    int32_t cputype = kCpuTypeArm64;
    uint32_t ncmds = 1;
    uint32_t sizeofcmds = 200;
    std::memcpy(trunc_cmds.data(), &magic, 4);
    std::memcpy(trunc_cmds.data() + 4, &cputype, 4);
    std::memcpy(trunc_cmds.data() + 16, &ncmds, 4);
    std::memcpy(trunc_cmds.data() + 20, &sizeofcmds, 4);

    auto res1 = MachoUserLoader::load_image(trunc_cmds, uas, ppm, mem);
    TEST_ASSERT(!res1.valid);
    TEST_ASSERT(res1.error_message.find("exceeds file boundary") != std::string::npos);

    // 2. W^X Violation: Segment with initprot = 7 (RWX)
    std::vector<uint8_t> wx_violation(32 + 72, 0);
    sizeofcmds = 72;
    std::memcpy(wx_violation.data(), &magic, 4);
    std::memcpy(wx_violation.data() + 4, &cputype, 4);
    std::memcpy(wx_violation.data() + 16, &ncmds, 4);
    std::memcpy(wx_violation.data() + 20, &sizeofcmds, 4);

    // Segment header
    uint32_t seg_cmd = kLcSegment64;
    uint32_t seg_size = 72;
    uint64_t seg_vmaddr = 0x100000000ULL;
    uint64_t seg_vmsize = 0x4000;
    int32_t wx_prot = 7; // R | W | X
    std::memcpy(wx_violation.data() + 32, &seg_cmd, 4);
    std::memcpy(wx_violation.data() + 36, &seg_size, 4);
    std::memcpy(wx_violation.data() + 40, "__TEXT", 6);
    std::memcpy(wx_violation.data() + 56, &seg_vmaddr, 8);
    std::memcpy(wx_violation.data() + 64, &seg_vmsize, 8);
    std::memcpy(wx_violation.data() + 88, &wx_prot, 4); // maxprot
    std::memcpy(wx_violation.data() + 92, &wx_prot, 4); // initprot

    auto res2 = MachoUserLoader::load_image(wx_violation, uas, ppm, mem);
    TEST_ASSERT(!res2.valid);
    TEST_ASSERT(res2.error_message.find("W^X violation") != std::string::npos);

    // 3. Unaligned Segment vmaddr (e.g. 0x100001000 instead of 16KB boundary 0x100004000)
    std::vector<uint8_t> unaligned_seg(32 + 72, 0);
    uint64_t unaligned_vmaddr = 0x100001000ULL;
    int32_t rx_prot = 5; // R | X
    std::memcpy(unaligned_seg.data(), &magic, 4);
    std::memcpy(unaligned_seg.data() + 4, &cputype, 4);
    std::memcpy(unaligned_seg.data() + 16, &ncmds, 4);
    std::memcpy(unaligned_seg.data() + 20, &sizeofcmds, 4);
    std::memcpy(unaligned_seg.data() + 32, &seg_cmd, 4);
    std::memcpy(unaligned_seg.data() + 36, &seg_size, 4);
    std::memcpy(unaligned_seg.data() + 40, "__TEXT", 6);
    std::memcpy(unaligned_seg.data() + 56, &unaligned_vmaddr, 8);
    std::memcpy(unaligned_seg.data() + 64, &seg_vmsize, 8);
    std::memcpy(unaligned_seg.data() + 88, &rx_prot, 4);
    std::memcpy(unaligned_seg.data() + 92, &rx_prot, 4);

    auto res3 = MachoUserLoader::load_image(unaligned_seg, uas, ppm, mem);
    TEST_ASSERT(!res3.valid);
    TEST_ASSERT(res3.error_message.find("16 KB page granule") != std::string::npos);

    // 4. Overlapping Segments
    std::vector<uint8_t> overlap_bin(32 + 72 + 72, 0);
    ncmds = 2;
    sizeofcmds = 144;
    std::memcpy(overlap_bin.data(), &magic, 4);
    std::memcpy(overlap_bin.data() + 4, &cputype, 4);
    std::memcpy(overlap_bin.data() + 16, &ncmds, 4);
    std::memcpy(overlap_bin.data() + 20, &sizeofcmds, 4);

    // Segment 1: __TEXT at 0x100000000 (size 0x8000)
    uint64_t s1_addr = 0x100000000ULL;
    uint64_t s1_size = 0x8000;
    std::memcpy(overlap_bin.data() + 32, &seg_cmd, 4);
    std::memcpy(overlap_bin.data() + 36, &seg_size, 4);
    std::memcpy(overlap_bin.data() + 40, "__TEXT", 6);
    std::memcpy(overlap_bin.data() + 56, &s1_addr, 8);
    std::memcpy(overlap_bin.data() + 64, &s1_size, 8);
    std::memcpy(overlap_bin.data() + 88, &rx_prot, 4);
    std::memcpy(overlap_bin.data() + 92, &rx_prot, 4);

    // Segment 2: __DATA at 0x100004000 (overlaps with Segment 1!)
    uint64_t s2_addr = 0x100004000ULL;
    uint64_t s2_size = 0x4000;
    int32_t rw_prot = 3;
    std::memcpy(overlap_bin.data() + 32 + 72, &seg_cmd, 4);
    std::memcpy(overlap_bin.data() + 36 + 72, &seg_size, 4);
    std::memcpy(overlap_bin.data() + 40 + 72, "__DATA", 6);
    std::memcpy(overlap_bin.data() + 56 + 72, &s2_addr, 8);
    std::memcpy(overlap_bin.data() + 64 + 72, &s2_size, 8);
    std::memcpy(overlap_bin.data() + 88 + 72, &rw_prot, 4);
    std::memcpy(overlap_bin.data() + 92 + 72, &rw_prot, 4);

    uint64_t root_l2_4 = ppm.allocate_page();
    memory::UserAddressSpace uas4(mem, ppm, root_l2_4, "hardening_vm4");
    auto res4 = MachoUserLoader::load_image(overlap_bin, uas4, ppm, mem);
    TEST_ASSERT(!res4.valid);
    TEST_ASSERT(res4.error_message.find("overlap") != std::string::npos);

    // 5. Entry point out of bounds
    std::vector<uint8_t> bad_entry_bin(32 + 72 + 24, 0);
    ncmds = 2;
    sizeofcmds = 96;
    std::memcpy(bad_entry_bin.data(), &magic, 4);
    std::memcpy(bad_entry_bin.data() + 4, &cputype, 4);
    std::memcpy(bad_entry_bin.data() + 16, &ncmds, 4);
    std::memcpy(bad_entry_bin.data() + 20, &sizeofcmds, 4);

    // Segment: __TEXT at 0x100000000 (size 0x4000)
    uint64_t txt_addr = 0x100000000ULL;
    uint64_t txt_size = 0x4000;
    std::memcpy(bad_entry_bin.data() + 32, &seg_cmd, 4);
    std::memcpy(bad_entry_bin.data() + 36, &seg_size, 4);
    std::memcpy(bad_entry_bin.data() + 40, "__TEXT", 6);
    std::memcpy(bad_entry_bin.data() + 56, &txt_addr, 8);
    std::memcpy(bad_entry_bin.data() + 64, &txt_size, 8);
    std::memcpy(bad_entry_bin.data() + 88, &rx_prot, 4);
    std::memcpy(bad_entry_bin.data() + 92, &rx_prot, 4);

    // LC_MAIN with entryoff = 0x8000 (past the end of 0x4000 __TEXT)
    uint32_t main_cmd = kLcMain;
    uint32_t main_size = 24;
    uint64_t bad_offset = 0x8000;
    std::memcpy(bad_entry_bin.data() + 104, &main_cmd, 4);
    std::memcpy(bad_entry_bin.data() + 108, &main_size, 4);
    std::memcpy(bad_entry_bin.data() + 112, &bad_offset, 8);

    uint64_t root_l2_5 = ppm.allocate_page();
    memory::UserAddressSpace uas5(mem, ppm, root_l2_5, "hardening_vm5");
    auto res5 = MachoUserLoader::load_image(bad_entry_bin, uas5, ppm, mem);
    TEST_ASSERT(!res5.valid);
    TEST_ASSERT(res5.error_message.find("outside executable __TEXT segment") != std::string::npos);

    std::cout << "[PASS] test_macho_structural_hardening\n";
}

int main() {
    std::cout << "=== WinIOS Stage 218.4 Mach-O Validation Hardening Tests ===\n";
    test_macho_header_fuzzing();
    test_macho_structural_hardening();
    std::cout << "[ALL PASS] Stage 218.4 Mach-O Validation Hardening tests succeeded.\n";
    return 0;
}

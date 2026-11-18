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

void test_segment_permissions_and_bss() {
    std::cout << "[Test 1] Stage 218.5: Segment permissions, W^X, and BSS zero-init...\n";

    Memory mem(0x800000); // 8 MB DRAM
    memory::PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);
    uint64_t root_l2 = ppm.allocate_page();
    memory::UserAddressSpace uas(mem, ppm, root_l2, "perm_vm");

    // Construct valid multi-segment Mach-O:
    // 1. __PAGEZERO: vmaddr=0x0, vmsize=0x100000000, initprot=0, maxprot=0
    // 2. __TEXT: vmaddr=0x100000000, vmsize=0x4000, filesize=0x4000, initprot=5 (R|X)
    // 3. __DATA_CONST: vmaddr=0x100004000, vmsize=0x4000, filesize=0x4000, initprot=1 (R)
    // 4. __DATA: vmaddr=0x100008000, vmsize=0x8000, filesize=0x4000, initprot=3 (R|W)  [0x4000 BSS!]
    // 5. LC_MAIN: entryoff=0x1000

    const size_t file_total_size = 0x10000;
    std::vector<uint8_t> bin(file_total_size, 0);

    uint32_t magic = kMhMagic64;
    int32_t cputype = kCpuTypeArm64;
    uint32_t ncmds = 5;
    uint32_t sizeofcmds = 72 * 4 + 24; // 312 bytes
    std::memcpy(bin.data(), &magic, 4);
    std::memcpy(bin.data() + 4, &cputype, 4);
    std::memcpy(bin.data() + 16, &ncmds, 4);
    std::memcpy(bin.data() + 20, &sizeofcmds, 4);

    size_t off = 32;
    uint32_t seg_cmd = kLcSegment64;
    uint32_t seg_size = 72;

    // Cmd 1: __PAGEZERO
    uint64_t pz_addr = 0x0;
    uint64_t pz_size = 0x100000000ULL;
    int32_t pz_prot = 0;
    std::memcpy(bin.data() + off, &seg_cmd, 4);
    std::memcpy(bin.data() + off + 4, &seg_size, 4);
    std::memcpy(bin.data() + off + 8, "__PAGEZERO", 10);
    std::memcpy(bin.data() + off + 24, &pz_addr, 8);
    std::memcpy(bin.data() + off + 32, &pz_size, 8);
    std::memcpy(bin.data() + off + 56, &pz_prot, 4);
    std::memcpy(bin.data() + off + 60, &pz_prot, 4);
    off += 72;

    // Cmd 2: __TEXT
    uint64_t text_addr = 0x100000000ULL;
    uint64_t text_vmsize = 0x4000;
    uint64_t text_fileoff = 0;
    uint64_t text_filesize = 0x4000;
    int32_t text_prot = 5; // R | X
    std::memcpy(bin.data() + off, &seg_cmd, 4);
    std::memcpy(bin.data() + off + 4, &seg_size, 4);
    std::memcpy(bin.data() + off + 8, "__TEXT", 6);
    std::memcpy(bin.data() + off + 24, &text_addr, 8);
    std::memcpy(bin.data() + off + 32, &text_vmsize, 8);
    std::memcpy(bin.data() + off + 40, &text_fileoff, 8);
    std::memcpy(bin.data() + off + 48, &text_filesize, 8);
    std::memcpy(bin.data() + off + 56, &text_prot, 4);
    std::memcpy(bin.data() + off + 60, &text_prot, 4);
    off += 72;

    // Cmd 3: __DATA_CONST
    uint64_t dc_addr = 0x100004000ULL;
    uint64_t dc_vmsize = 0x4000;
    uint64_t dc_fileoff = 0x4000;
    uint64_t dc_filesize = 0x4000;
    int32_t dc_prot = 1; // R
    std::memcpy(bin.data() + off, &seg_cmd, 4);
    std::memcpy(bin.data() + off + 4, &seg_size, 4);
    std::memcpy(bin.data() + off + 8, "__DATA_CONST", 12);
    std::memcpy(bin.data() + off + 24, &dc_addr, 8);
    std::memcpy(bin.data() + off + 32, &dc_vmsize, 8);
    std::memcpy(bin.data() + off + 40, &dc_fileoff, 8);
    std::memcpy(bin.data() + off + 48, &dc_filesize, 8);
    std::memcpy(bin.data() + off + 56, &dc_prot, 4);
    std::memcpy(bin.data() + off + 60, &dc_prot, 4);
    off += 72;

    // Cmd 4: __DATA (with BSS: vmsize 0x8000 > filesize 0x4000)
    uint64_t data_addr = 0x100008000ULL;
    uint64_t data_vmsize = 0x8000; // 32 KB (2 pages of 16 KB)
    uint64_t data_fileoff = 0x8000;
    uint64_t data_filesize = 0x4000; // 16 KB from file; second 16 KB is BSS
    int32_t data_prot = 3; // R | W
    std::memcpy(bin.data() + off, &seg_cmd, 4);
    std::memcpy(bin.data() + off + 4, &seg_size, 4);
    std::memcpy(bin.data() + off + 8, "__DATA", 6);
    std::memcpy(bin.data() + off + 24, &data_addr, 8);
    std::memcpy(bin.data() + off + 32, &data_vmsize, 8);
    std::memcpy(bin.data() + off + 40, &data_fileoff, 8);
    std::memcpy(bin.data() + off + 48, &data_filesize, 8);
    std::memcpy(bin.data() + off + 56, &data_prot, 4);
    std::memcpy(bin.data() + off + 60, &data_prot, 4);
    off += 72;

    // Cmd 5: LC_MAIN
    uint32_t main_cmd = kLcMain;
    uint32_t main_size = 24;
    uint64_t entryoff = 0x1000;
    std::memcpy(bin.data() + off, &main_cmd, 4);
    std::memcpy(bin.data() + off + 4, &main_size, 4);
    std::memcpy(bin.data() + off + 8, &entryoff, 8);

    // Put marker data in first page of __DATA
    uint32_t marker = 0x12345678;
    std::memcpy(bin.data() + 0x8000, &marker, 4);

    auto res = MachoUserLoader::load_image(bin, uas, ppm, mem);
    TEST_ASSERT(res.valid);
    TEST_ASSERT(res.entry_point == text_addr + entryoff);
    TEST_ASSERT(res.text_vmaddr == text_addr);
    TEST_ASSERT(res.data_const_vmaddr == dc_addr);
    TEST_ASSERT(res.data_vmaddr == data_addr);
    TEST_ASSERT(res.segments_loaded >= 3);

    // 1. Check __PAGEZERO is unmapped
    TEST_ASSERT(uas.translate(0x0) == 0);
    TEST_ASSERT(uas.translate(0x1000) == 0);

    // 2. Check __TEXT translation and execution
    uint64_t text_pa = uas.translate(text_addr);
    TEST_ASSERT(text_pa != 0);

    // 3. Check __DATA_CONST translation
    uint64_t dc_pa = uas.translate(dc_addr);
    TEST_ASSERT(dc_pa != 0);

    // 4. Check __DATA initialized payload
    uint64_t data_p1_pa = uas.translate(data_addr);
    TEST_ASSERT(data_p1_pa != 0);
    TEST_ASSERT(mem.read_u32(static_cast<size_t>(data_p1_pa)) == 0x12345678);

    // 5. Check __DATA BSS page (data_addr + 16KB) is zero-initialized
    uint64_t bss_pa = uas.translate(data_addr + 0x4000);
    TEST_ASSERT(bss_pa != 0);
    for (size_t b = 0; b < 16; ++b) {
        TEST_ASSERT(mem.read_u8(static_cast<size_t>(bss_pa + b)) == 0);
    }

    std::cout << "[PASS] test_segment_permissions_and_bss\n";
}

int main() {
    std::cout << "=== WinIOS Stage 218.5 Segment Permissions & Mapping Tests ===\n";
    test_segment_permissions_and_bss();
    std::cout << "[ALL PASS] Stage 218.5 Segment Permissions tests succeeded.\n";
    return 0;
}

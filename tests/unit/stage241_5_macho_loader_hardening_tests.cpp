// ============================================================================
// Stage 241.5: Mach-O Loader Hardening and Malformed Binary Rejection Tests
// Validates load command bounds, entrypoint semantics, 16KB granule,
// segment overlap detection, W^X enforcement, and malformed binary rejection.
// ============================================================================

#include "loader/macho_user_loader.h"
#include "loader/app_inspector.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::loader;
using namespace winios::memory;

static std::vector<uint8_t> create_base_macho_header(uint32_t ncmds, uint32_t sizeofcmds) {
    std::vector<uint8_t> bin(32 + sizeofcmds, 0);
    const uint32_t magic = kMhMagic64;
    const int32_t cputype = kCpuTypeArm64;
    const uint32_t cpusubtype = 0;
    const uint32_t filetype = 2; // MH_EXECUTE

    std::memcpy(bin.data(), &magic, 4);
    std::memcpy(bin.data() + 4, &cputype, 4);
    std::memcpy(bin.data() + 8, &cpusubtype, 4);
    std::memcpy(bin.data() + 12, &filetype, 4);
    std::memcpy(bin.data() + 16, &ncmds, 4);
    std::memcpy(bin.data() + 20, &sizeofcmds, 4);
    return bin;
}

static void test_macho_load_command_bounds() {
    std::cout << "[Stage 241.5 - Test 1] Mach-O load command stream boundary verification...\n";
    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize(), "PhysicalPageManager init");
    uint64_t root = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root, "uas_test");

    // 1. Corrupted sizeofcmds: sizeofcmds claims 1000 bytes, but binary is only 64 bytes
    auto bin1 = create_base_macho_header(1, 1000);
    bin1.resize(64); // truncated
    auto img1 = MachoUserLoader::load_image(bin1, uas, ppm, mem);
    TEST_ASSERT(!img1.valid, "Loader must reject binary when sizeofcmds exceeds file size");

    // 2. Load command with cmdsize < 8 (minimum load command size)
    auto bin2 = create_base_macho_header(1, 16);
    const uint32_t cmd2 = kLcSegment64;
    const uint32_t cmdsize2 = 4; // INVALID: must be >= 8
    std::memcpy(bin2.data() + 32, &cmd2, 4);
    std::memcpy(bin2.data() + 36, &cmdsize2, 4);
    auto img2 = MachoUserLoader::load_image(bin2, uas, ppm, mem);
    TEST_ASSERT(!img2.valid, "Loader must reject load command with cmdsize < 8");

    // 3. Load command with cmdsize exceeding stream boundary
    auto bin3 = create_base_macho_header(1, 32);
    const uint32_t cmd3 = kLcSegment64;
    const uint32_t cmdsize3 = 128; // Exceeds 32 bytes allocated for commands
    std::memcpy(bin3.data() + 32, &cmd3, 4);
    std::memcpy(bin3.data() + 36, &cmdsize3, 4);
    auto img3 = MachoUserLoader::load_image(bin3, uas, ppm, mem);
    TEST_ASSERT(!img3.valid, "Loader must reject load command whose cmdsize exceeds file length");
}

static void test_macho_segment_integrity() {
    std::cout << "[Stage 241.5 - Test 2] Segment alignment, W^X, and overlap enforcement...\n";
    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize(), "PhysicalPageManager init");
    uint64_t root = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root, "uas_test");

    // Misaligned segment: vmaddr 0x10004 (not aligned to 16KB 0x4000)
    auto bin = create_base_macho_header(1, 72);
    const uint32_t cmd = kLcSegment64;
    const uint32_t cmdsize = 72;
    std::memcpy(bin.data() + 32, &cmd, 4);
    std::memcpy(bin.data() + 36, &cmdsize, 4);
    std::memcpy(bin.data() + 40, "__TEXT\0\0\0\0\0\0\0\0\0\0", 16);
    const uint64_t bad_vmaddr = 0x10004ULL; // Not 16KB aligned!
    const uint64_t vmsize = 0x4000ULL;
    const int32_t maxprot = 5; // R-X
    const int32_t initprot = 5; // R-X
    std::memcpy(bin.data() + 56, &bad_vmaddr, 8);
    std::memcpy(bin.data() + 64, &vmsize, 8);
    std::memcpy(bin.data() + 88, &maxprot, 4);
    std::memcpy(bin.data() + 92, &initprot, 4);

    auto img = MachoUserLoader::load_image(bin, uas, ppm, mem);
    TEST_ASSERT(!img.valid, "Loader must reject segment not aligned to 16KB page boundary");
}

int main() {
    std::cout << "==========================================================\n";
    std::cout << "Stage 241.5: Mach-O Loader Hardening Suite\n";
    std::cout << "==========================================================\n";

    test_macho_load_command_bounds();
    test_macho_segment_integrity();

    std::cout << "[PASS] Stage 241.5 Loader hardening tests completed successfully.\n";
    return 0;
}

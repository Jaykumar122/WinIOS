// ============================================================================
// Stage 242.3: Guest Memory Boundary, Sizing & Protection Correctness Tests
// Adversarially exercises 1-, 2-, 4-, 8-byte accesses, boundary checks, address
// overflows, unmapped page and cross-page access aborts, and partial mutation guards.
// ============================================================================

#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "memory/virtual_page_table.h"

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <limits>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::memory;

static void test_multi_size_aligned_misaligned() {
    std::cout << "[Stage 242.3 - Test 1] Multi-size aligned and misaligned accesses...\n";
    Memory mem(0x1000000ULL); // 16 MB

    // Test 1-byte (u8)
    mem.write_u8(0x100, 0x42);
    TEST_ASSERT(mem.read_u8(0x100) == 0x42, "u8 read/write");

    // Test 2-byte (u16) aligned and unaligned
    mem.write_u16(0x102, 0x1234);
    TEST_ASSERT(mem.read_u16(0x102) == 0x1234, "u16 aligned");
    mem.write_u16(0x105, 0x5678);
    TEST_ASSERT(mem.read_u16(0x105) == 0x5678, "u16 unaligned");

    // Test 4-byte (u32) aligned and unaligned
    mem.write_u32(0x108, 0xDEADBEEF);
    TEST_ASSERT(mem.read_u32(0x108) == 0xDEADBEEF, "u32 aligned");
    mem.write_u32(0x10D, 0xCAFEBABE);
    TEST_ASSERT(mem.read_u32(0x10D) == 0xCAFEBABE, "u32 unaligned");

    // Test 8-byte (u64) aligned and unaligned
    mem.write_u64(0x120, 0x0123456789ABCDEFULL);
    TEST_ASSERT(mem.read_u64(0x120) == 0x0123456789ABCDEFULL, "u64 aligned");
    mem.write_u64(0x129, 0xFEDCBA9876543210ULL);
    TEST_ASSERT(mem.read_u64(0x129) == 0xFEDCBA9876543210ULL, "u64 unaligned");
}

static void test_boundary_overflow_guards() {
    std::cout << "[Stage 242.3 - Test 2] Boundary limits, out-of-range, and overflow...\n";
    Memory mem(0x10000ULL); // 64 KB

    // Access exactly at boundary limit: size = 0x10000
    // Last valid byte is 0xFFFF
    mem.write_u8(0xFFFF, 0x99);
    TEST_ASSERT(mem.read_u8(0xFFFF) == 0x99, "Last valid byte");

    // Reading beyond boundary throws std::out_of_range
    bool caught_u8 = false;
    try {
        mem.read_u8(0x10000);
    } catch (const std::out_of_range&) {
        caught_u8 = true;
    }
    TEST_ASSERT(caught_u8, "Out of bounds u8 caught");

    bool caught_u64 = false;
    try {
        mem.read_u64(0xFFFA); // 0xFFFA + 8 = 0x10002 > 0x10000
    } catch (const std::out_of_range&) {
        caught_u64 = true;
    }
    TEST_ASSERT(caught_u64, "Out of bounds cross-boundary u64 caught");

    // Max uint64_t overflow wrap-around prevention
    bool caught_overflow = false;
    try {
        mem.read_u8(std::numeric_limits<std::size_t>::max());
    } catch (const std::out_of_range&) {
        caught_overflow = true;
    }
    TEST_ASSERT(caught_overflow, "Max size_t out of bounds caught");
}

static void test_address_space_translation_and_no_partial_mutation() {
    std::cout << "[Stage 242.3 - Test 3] User address space copyin/copyout with partial mutation guards...\n";
    Memory mem(0x4000000ULL); // 64 MB
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x3000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize(), "PPM init");

    uint64_t root = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root, "boundary_uas");

    // Map one 16KB page at VA 0x10000 (read-write)
    uint64_t pa = ppm.allocate_page();
    TEST_ASSERT(uas.map_user_page(0x10000ULL, pa, PagePermissions::UserReadWrite), "Map page 1");

    // Initialize physical page to known pattern
    std::vector<uint8_t> init_pattern(0x4000, 0xAA);
    uas.copyin(0x10000ULL, init_pattern.data(), init_pattern.size());

    // Verify copyout
    std::vector<uint8_t> read_back(0x4000, 0x00);
    TEST_ASSERT(uas.copyout(reinterpret_cast<const void*>(0x10000ULL), read_back.data(), read_back.size()), "Copyout");
    TEST_ASSERT(read_back == init_pattern, "Pattern matches");

    // Adversarial: Attempt copyin that spans into unmapped page [0x10000 + 0x3FF0, len = 32]
    // VA 0x10000 to 0x13FFF is mapped, but 0x14000 is unmapped!
    // Validate buffer must fail, and no partial corruption should occur
    std::vector<uint8_t> hostile_payload(32, 0x55);
    bool copyin_res = uas.copyin(0x13FF0ULL, hostile_payload.data(), hostile_payload.size());
    TEST_ASSERT(!copyin_res, "Copyin spanning into unmapped page must fail");

    // Verify memory was not partially mutated
    std::vector<uint8_t> check_back(16, 0);
    uas.copyout(reinterpret_cast<const void*>(0x13FF0ULL), check_back.data(), 16);
    for (uint8_t b : check_back) {
        TEST_ASSERT(b == 0xAA, "Memory must remain unchanged after rejected partial copyin");
    }

    // Attempt access above user space limit (32 GiB)
    bool limit_res = uas.validate_user_buffer(0x0000000800000000ULL, 16, false);
    TEST_ASSERT(!limit_res, "Access at or above 32 GiB must be rejected");

    // Attempt overflow wrapping around 64-bit address space
    bool wrap_res = uas.validate_user_buffer(0xFFFFFFFFFFFFFFF0ULL, 32, false);
    TEST_ASSERT(!wrap_res, "Address overflow wrapping around 64-bit must be rejected");
}

int main() {
    std::cout << "=== Running Stage 242.3 Guest Memory Boundary Tests ===\n";
    test_multi_size_aligned_misaligned();
    test_boundary_overflow_guards();
    test_address_space_translation_and_no_partial_mutation();
    std::cout << "Stage 242.3: All tests passed successfully.\n";
    return 0;
}

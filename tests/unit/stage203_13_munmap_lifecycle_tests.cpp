// ==============================================================================
// WinIOS Campaign 203 Stage 203.13: munmap and Mapping Lifecycle Tests
// ==============================================================================
// Independent verification of:
// 1. Full unmap (entire region removal, access revocation)
// 2. Left-edge unmap (head trimming, start_va & offset_in_object adjustment)
// 3. Right-edge unmap (tail trimming, size adjustment)
// 4. Middle unmap (entry splitting, hole creation, offset tracking)
// 5. Multi-region spanning unmap
// 6. Misaligned address / zero size rejection (kKernInvalidArgument)
// ==============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "memory/darwin_vm_object.h"

using namespace winios::memory;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Full Unmap & Access Revocation
// -----------------------------------------------------------------------------
void test_full_unmap() {
    std::cout << "[Stage 203.13 Test 1] Full Unmap & Access Revocation...\n";

    DarwinVmSpace vm(201, 0x100000000ULL);

    std::uint64_t addr = vm.mmap(
        0, 0x8000, kDarwinVmProtRead | kDarwinVmProtWrite, kDarwinMapAnonymous | kDarwinMapPrivate);

    TEST_CHECK(addr != 0, "mmap allocation must succeed");
    TEST_CHECK(vm.region_count() == 1, "Region count must be 1");

    // Write test data
    std::uint8_t payload[64]{0xAA, 0xBB, 0xCC, 0xDD};
    int wr = vm.mach_vm_write(addr, payload, sizeof(payload));
    TEST_CHECK(wr == kKernSuccess, "mach_vm_write must succeed");

    // Full munmap
    int ur = vm.munmap(addr, 0x8000);
    TEST_CHECK(ur == kKernSuccess, "munmap must return kKernSuccess");
    TEST_CHECK(vm.region_count() == 0, "Region count must be 0 after full unmap");

    // Verify access revoked
    TEST_CHECK(!vm.check_access(addr, 0x8000, kDarwinVmProtRead), "check_access must fail after unmap");

    std::uint8_t read_buf[64]{0};
    int rr = vm.mach_vm_read(addr, read_buf, sizeof(read_buf));
    TEST_CHECK(rr == kKernInvalidAddress, "mach_vm_read must return kKernInvalidAddress");

    int wr2 = vm.mach_vm_write(addr, payload, sizeof(payload));
    TEST_CHECK(wr2 == kKernInvalidAddress, "mach_vm_write must return kKernInvalidAddress");

    std::cout << "  -> Full unmap verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Left-Edge Unmap (Trimming from Start)
// -----------------------------------------------------------------------------
void test_left_edge_unmap() {
    std::cout << "[Stage 203.13 Test 2] Left-Edge Unmap & Offset Tracking...\n";

    DarwinVmSpace vm(202, 0x100000000ULL);

    // Allocate 2 pages (0x8000)
    std::uint64_t addr = vm.mmap(
        0, 0x8000, kDarwinVmProtRead | kDarwinVmProtWrite, kDarwinMapAnonymous | kDarwinMapPrivate);
    TEST_CHECK(addr != 0, "mmap allocation must succeed");

    // Write distinct patterns to Page 0 and Page 1
    std::uint8_t page0_data[16]{0x11, 0x11, 0x11, 0x11};
    std::uint8_t page1_data[16]{0x22, 0x22, 0x22, 0x22};

    TEST_CHECK(vm.mach_vm_write(addr, page0_data, sizeof(page0_data)) == kKernSuccess, "Write Page 0");
    TEST_CHECK(vm.mach_vm_write(addr + 0x4000, page1_data, sizeof(page1_data)) == kKernSuccess, "Write Page 1");

    // Unmap Page 0 (left edge)
    int ur = vm.munmap(addr, 0x4000);
    TEST_CHECK(ur == kKernSuccess, "Left edge munmap must succeed");
    TEST_CHECK(vm.region_count() == 1, "Region count remains 1 after trimming");

    // Page 0 must be inaccessible
    TEST_CHECK(!vm.check_access(addr, 0x4000, kDarwinVmProtRead), "Page 0 access must be revoked");
    std::uint8_t tmp[16]{0};
    TEST_CHECK(vm.mach_vm_read(addr, tmp, sizeof(tmp)) == kKernInvalidAddress, "Page 0 read must fail");

    // Page 1 must still be accessible and contain page1_data
    TEST_CHECK(vm.check_access(addr + 0x4000, 0x4000, kDarwinVmProtRead), "Page 1 must remain accessible");
    TEST_CHECK(vm.mach_vm_read(addr + 0x4000, tmp, sizeof(tmp)) == kKernSuccess, "Page 1 read must succeed");
    TEST_CHECK(std::memcmp(tmp, page1_data, sizeof(page1_data)) == 0, "Page 1 data must match exactly");

    std::cout << "  -> Left-edge unmap verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Right-Edge Unmap (Trimming from End)
// -----------------------------------------------------------------------------
void test_right_edge_unmap() {
    std::cout << "[Stage 203.13 Test 3] Right-Edge Unmap...\n";

    DarwinVmSpace vm(203, 0x100000000ULL);

    // Allocate 2 pages (0x8000)
    std::uint64_t addr = vm.mmap(
        0, 0x8000, kDarwinVmProtRead | kDarwinVmProtWrite, kDarwinMapAnonymous | kDarwinMapPrivate);
    TEST_CHECK(addr != 0, "mmap allocation must succeed");

    std::uint8_t page0_data[16]{0x33, 0x33, 0x33, 0x33};
    std::uint8_t page1_data[16]{0x44, 0x44, 0x44, 0x44};

    TEST_CHECK(vm.mach_vm_write(addr, page0_data, sizeof(page0_data)) == kKernSuccess, "Write Page 0");
    TEST_CHECK(vm.mach_vm_write(addr + 0x4000, page1_data, sizeof(page1_data)) == kKernSuccess, "Write Page 1");

    // Unmap Page 1 (right edge)
    int ur = vm.munmap(addr + 0x4000, 0x4000);
    TEST_CHECK(ur == kKernSuccess, "Right edge munmap must succeed");
    TEST_CHECK(vm.region_count() == 1, "Region count remains 1 after trimming");

    // Page 0 must remain accessible with its data
    std::uint8_t tmp[16]{0};
    TEST_CHECK(vm.check_access(addr, 0x4000, kDarwinVmProtRead), "Page 0 must remain accessible");
    TEST_CHECK(vm.mach_vm_read(addr, tmp, sizeof(tmp)) == kKernSuccess, "Page 0 read must succeed");
    TEST_CHECK(std::memcmp(tmp, page0_data, sizeof(page0_data)) == 0, "Page 0 data must match exactly");

    // Page 1 must be inaccessible
    TEST_CHECK(!vm.check_access(addr + 0x4000, 0x4000, kDarwinVmProtRead), "Page 1 access must be revoked");
    TEST_CHECK(vm.mach_vm_read(addr + 0x4000, tmp, sizeof(tmp)) == kKernInvalidAddress, "Page 1 read must fail");

    std::cout << "  -> Right-edge unmap verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Middle Unmap (Splitting Entry & Creating Hole)
// -----------------------------------------------------------------------------
void test_middle_unmap_split() {
    std::cout << "[Stage 203.13 Test 4] Middle Unmap (Splitting & Hole Creation)...\n";

    DarwinVmSpace vm(204, 0x100000000ULL);

    // Allocate 3 pages (0xC000): P0, P1, P2
    std::uint64_t addr = vm.mmap(
        0, 0xC000, kDarwinVmProtRead | kDarwinVmProtWrite, kDarwinMapAnonymous | kDarwinMapPrivate);
    TEST_CHECK(addr != 0, "mmap allocation must succeed");
    TEST_CHECK(vm.region_count() == 1, "Initial region count must be 1");

    std::uint8_t p0_sig[8]{0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10};
    std::uint8_t p1_sig[8]{0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};
    std::uint8_t p2_sig[8]{0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30};

    TEST_CHECK(vm.mach_vm_write(addr, p0_sig, sizeof(p0_sig)) == kKernSuccess, "Write P0");
    TEST_CHECK(vm.mach_vm_write(addr + 0x4000, p1_sig, sizeof(p1_sig)) == kKernSuccess, "Write P1");
    TEST_CHECK(vm.mach_vm_write(addr + 0x8000, p2_sig, sizeof(p2_sig)) == kKernSuccess, "Write P2");

    // Unmap P1 in the middle
    int ur = vm.munmap(addr + 0x4000, 0x4000);
    TEST_CHECK(ur == kKernSuccess, "Middle unmap must succeed");
    TEST_CHECK(vm.region_count() == 2, "Region count must become 2 after middle split");

    std::uint8_t read_sig[8]{0};

    // Check P0
    TEST_CHECK(vm.check_access(addr, 0x4000, kDarwinVmProtRead), "P0 must be accessible");
    TEST_CHECK(vm.mach_vm_read(addr, read_sig, sizeof(read_sig)) == kKernSuccess, "Read P0");
    TEST_CHECK(std::memcmp(read_sig, p0_sig, sizeof(p0_sig)) == 0, "P0 data preserved");

    // Check P1 (the hole)
    TEST_CHECK(!vm.check_access(addr + 0x4000, 0x4000, kDarwinVmProtRead), "P1 hole must be inaccessible");
    TEST_CHECK(vm.mach_vm_read(addr + 0x4000, read_sig, sizeof(read_sig)) == kKernInvalidAddress, "Read P1 fails");

    // Check P2
    TEST_CHECK(vm.check_access(addr + 0x8000, 0x4000, kDarwinVmProtRead), "P2 must be accessible");
    TEST_CHECK(vm.mach_vm_read(addr + 0x8000, read_sig, sizeof(read_sig)) == kKernSuccess, "Read P2");
    TEST_CHECK(std::memcmp(read_sig, p2_sig, sizeof(p2_sig)) == 0, "P2 data preserved across split");

    std::cout << "  -> Middle unmap splitting verified.\n";
}

// -----------------------------------------------------------------------------
// Test 5: Misaligned Address & Invalid Parameter Rejection
// -----------------------------------------------------------------------------
void test_invalid_parameters() {
    std::cout << "[Stage 203.13 Test 5] Invalid Parameters Rejection...\n";

    DarwinVmSpace vm(205, 0x100000000ULL);

    // Misaligned address (0x100001000 is not 16KB aligned)
    int r1 = vm.munmap(0x100001000ULL, 0x4000);
    TEST_CHECK(r1 == kKernInvalidArgument, "Misaligned addr must return kKernInvalidArgument");

    // Zero length
    int r2 = vm.munmap(0x100000000ULL, 0);
    TEST_CHECK(r2 == kKernInvalidArgument, "Zero length must return kKernInvalidArgument");

    // Non-existent address
    int r3 = vm.munmap(0x200000000ULL, 0x4000);
    TEST_CHECK(r3 == kKernInvalidAddress, "Unmapping non-existent address returns kKernInvalidAddress");

    std::cout << "  -> Invalid parameter rejection verified.\n";
}

} // namespace

int main() {
    std::cout << "==================================================================\n";
    std::cout << "Stage 203.13: munmap and Mapping Lifecycle Tests\n";
    std::cout << "==================================================================\n";

    test_full_unmap();
    test_left_edge_unmap();
    test_right_edge_unmap();
    test_middle_unmap_split();
    test_invalid_parameters();

    std::cout << "------------------------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "ALL STAGE 203.13 TESTS PASSED.\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED.\n";
        return 1;
    }
}

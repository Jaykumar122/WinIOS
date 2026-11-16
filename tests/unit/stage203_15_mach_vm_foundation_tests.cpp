// ==============================================================================
// WinIOS Campaign 203 Stage 203.15: mach_vm_* Foundation Tests
// ==============================================================================
// Independent verification of:
// 1. mach_vm_allocate with VM_FLAGS_ANYWHERE (addr=0 auto-placement, zero-fill)
// 2. mach_vm_allocate with VM_FLAGS_FIXED (exact address, collision handling)
// 3. mach_vm_read and mach_vm_write data fidelity
// 4. mach_vm_protect and permission fault reporting (kKernProtectionFailure)
// 5. mach_vm_deallocate and boundary crossing detection (kKernInvalidAddress)
// 6. Null pointer and zero size validation (kKernInvalidArgument)
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
// Test 1: mach_vm_allocate Anywhere & Zero-Fill
// -----------------------------------------------------------------------------
void test_mach_vm_allocate_anywhere() {
    std::cout << "[Stage 203.15 Test 1] mach_vm_allocate Anywhere & Zero-Fill...\n";

    DarwinVmSpace vm(401, 0x100000000ULL);

    std::uint64_t addr = 0;
    int r = vm.mach_vm_allocate(&addr, 0x8000, kDarwinVmFlagsAnywhere);
    TEST_CHECK(r == kKernSuccess, "mach_vm_allocate anywhere must succeed");
    TEST_CHECK(addr != 0, "Allocated address must not be null");
    TEST_CHECK(is_page_aligned(addr), "Address must be 16KB aligned");

    // Verify zero fill
    std::uint8_t buf[256]{0xFF};
    int rr = vm.mach_vm_read(addr, buf, sizeof(buf));
    TEST_CHECK(rr == kKernSuccess, "Reading allocated memory must succeed");

    bool zeros = true;
    for (std::uint8_t b : buf) {
        if (b != 0) {
            zeros = false;
            break;
        }
    }
    TEST_CHECK(zeros, "mach_vm_allocate must zero-initialize backing memory");

    std::cout << "  -> mach_vm_allocate anywhere verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: mach_vm_allocate Fixed & Collision Handling
// -----------------------------------------------------------------------------
void test_mach_vm_allocate_fixed() {
    std::cout << "[Stage 203.15 Test 2] mach_vm_allocate Fixed & Collision...\n";

    DarwinVmSpace vm(402, 0x100000000ULL);

    std::uint64_t fixed_addr = 0x100080000ULL;
    int r1 = vm.mach_vm_allocate(&fixed_addr, 0x4000, kDarwinVmFlagsFixed);
    TEST_CHECK(r1 == kKernSuccess, "mach_vm_allocate fixed must succeed");
    TEST_CHECK(fixed_addr == 0x100080000ULL, "Address must match requested fixed address");

    // Colliding fixed allocation must fail
    std::uint64_t collide_addr = 0x100080000ULL;
    int r2 = vm.mach_vm_allocate(&collide_addr, 0x4000, kDarwinVmFlagsFixed);
    TEST_CHECK(r2 == kKernNoSpace, "Colliding fixed allocation must return kKernNoSpace");

    std::cout << "  -> mach_vm_allocate fixed verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: mach_vm_read and mach_vm_write Data Fidelity
// -----------------------------------------------------------------------------
void test_mach_vm_read_write_fidelity() {
    std::cout << "[Stage 203.15 Test 3] mach_vm_read & mach_vm_write Fidelity...\n";

    DarwinVmSpace vm(403, 0x100000000ULL);

    std::uint64_t addr = 0;
    TEST_CHECK(vm.mach_vm_allocate(&addr, 0x4000, kDarwinVmFlagsAnywhere) == kKernSuccess, "Allocate test page");

    std::uint8_t write_pattern[128];
    for (std::size_t i = 0; i < sizeof(write_pattern); ++i) {
        write_pattern[i] = static_cast<std::uint8_t>((i * 7 + 13) & 0xFF);
    }

    int wr = vm.mach_vm_write(addr + 0x100, write_pattern, sizeof(write_pattern));
    TEST_CHECK(wr == kKernSuccess, "mach_vm_write must succeed");

    std::uint8_t read_pattern[128]{0};
    int rr = vm.mach_vm_read(addr + 0x100, read_pattern, sizeof(read_pattern));
    TEST_CHECK(rr == kKernSuccess, "mach_vm_read must succeed");
    TEST_CHECK(std::memcmp(read_pattern, write_pattern, sizeof(write_pattern)) == 0, "Data read must match data written");

    std::cout << "  -> Data fidelity verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: mach_vm_protect & Permission Faults
// -----------------------------------------------------------------------------
void test_mach_vm_protect_and_faults() {
    std::cout << "[Stage 203.15 Test 4] mach_vm_protect & Permission Faults...\n";

    DarwinVmSpace vm(404, 0x100000000ULL);

    std::uint64_t addr = 0;
    TEST_CHECK(vm.mach_vm_allocate(&addr, 0x4000, kDarwinVmFlagsAnywhere) == kKernSuccess, "Allocate page");

    // Change to Read-Only
    int pr1 = vm.mach_vm_protect(addr, 0x4000, false, kDarwinVmProtRead);
    TEST_CHECK(pr1 == kKernSuccess, "mach_vm_protect to RO must succeed");

    std::uint8_t byte = 0xAA;
    int wr1 = vm.mach_vm_write(addr, &byte, 1);
    TEST_CHECK(wr1 == kKernProtectionFailure, "Write to RO page must return kKernProtectionFailure");

    // Change to PROT_NONE
    int pr2 = vm.mach_vm_protect(addr, 0x4000, false, kDarwinVmProtNone);
    TEST_CHECK(pr2 == kKernSuccess, "mach_vm_protect to NONE must succeed");

    int rr1 = vm.mach_vm_read(addr, &byte, 1);
    TEST_CHECK(rr1 == kKernProtectionFailure, "Read from PROT_NONE page must return kKernProtectionFailure");

    int wr2 = vm.mach_vm_write(addr, &byte, 1);
    TEST_CHECK(wr2 == kKernProtectionFailure, "Write to PROT_NONE page must return kKernProtectionFailure");

    std::cout << "  -> Protection faults verified.\n";
}

// -----------------------------------------------------------------------------
// Test 5: Boundary Crossing & Deallocate Lifecycle
// -----------------------------------------------------------------------------
void test_boundary_crossing_and_deallocate() {
    std::cout << "[Stage 203.15 Test 5] Boundary Crossing & Deallocate...\n";

    DarwinVmSpace vm(405, 0x100000000ULL);

    std::uint64_t addr = 0;
    TEST_CHECK(vm.mach_vm_allocate(&addr, 0x4000, kDarwinVmFlagsAnywhere) == kKernSuccess, "Allocate page");

    // Attempting to read 0x8000 bytes starting at addr (overflows 0x4000 region)
    std::vector<std::uint8_t> big_buf(0x8000, 0);
    int rr = vm.mach_vm_read(addr, big_buf.data(), big_buf.size());
    TEST_CHECK(rr == kKernInvalidAddress, "Reading past region boundary must return kKernInvalidAddress");

    // Attempting to write 0x8000 bytes starting at addr
    int wr = vm.mach_vm_write(addr, big_buf.data(), big_buf.size());
    TEST_CHECK(wr == kKernInvalidAddress, "Writing past region boundary must return kKernInvalidAddress");

    // Deallocate region
    int dr1 = vm.mach_vm_deallocate(addr, 0x4000);
    TEST_CHECK(dr1 == kKernSuccess, "mach_vm_deallocate must succeed");

    // Double deallocate must fail
    int dr2 = vm.mach_vm_deallocate(addr, 0x4000);
    TEST_CHECK(dr2 == kKernInvalidAddress, "Double deallocate must return kKernInvalidAddress");

    std::cout << "  -> Boundary crossing and deallocate verified.\n";
}

// -----------------------------------------------------------------------------
// Test 6: Null Pointer & Zero Size Parameter Validation
// -----------------------------------------------------------------------------
void test_parameter_validation() {
    std::cout << "[Stage 203.15 Test 6] Null Pointer & Zero Size Validation...\n";

    DarwinVmSpace vm(406, 0x100000000ULL);

    // Null address pointer in allocate
    int r1 = vm.mach_vm_allocate(nullptr, 0x4000, 0);
    TEST_CHECK(r1 == kKernInvalidArgument, "Null address pointer must return kKernInvalidArgument");

    // Zero size in allocate
    std::uint64_t addr = 0;
    int r2 = vm.mach_vm_allocate(&addr, 0, 0);
    TEST_CHECK(r2 == kKernInvalidArgument, "Zero size in allocate must return kKernInvalidArgument");

    // Zero size in deallocate
    int r3 = vm.mach_vm_deallocate(0x100000000ULL, 0);
    TEST_CHECK(r3 == kKernInvalidArgument, "Zero size in deallocate must return kKernInvalidArgument");

    // Null buffer in read
    int r4 = vm.mach_vm_read(0x100000000ULL, nullptr, 64);
    TEST_CHECK(r4 == kKernInvalidArgument, "Null buffer in read must return kKernInvalidArgument");

    // Zero size in read
    std::uint8_t byte = 0;
    int r5 = vm.mach_vm_read(0x100000000ULL, &byte, 0);
    TEST_CHECK(r5 == kKernInvalidArgument, "Zero size in read must return kKernInvalidArgument");

    // Null buffer in write
    int r6 = vm.mach_vm_write(0x100000000ULL, nullptr, 64);
    TEST_CHECK(r6 == kKernInvalidArgument, "Null buffer in write must return kKernInvalidArgument");

    // Zero size in write
    int r7 = vm.mach_vm_write(0x100000000ULL, &byte, 0);
    TEST_CHECK(r7 == kKernInvalidArgument, "Zero size in write must return kKernInvalidArgument");

    std::cout << "  -> Parameter validation verified.\n";
}

} // namespace

int main() {
    std::cout << "==================================================================\n";
    std::cout << "Stage 203.15: mach_vm_* Foundation Tests\n";
    std::cout << "==================================================================\n";

    test_mach_vm_allocate_anywhere();
    test_mach_vm_allocate_fixed();
    test_mach_vm_read_write_fidelity();
    test_mach_vm_protect_and_faults();
    test_boundary_crossing_and_deallocate();
    test_parameter_validation();

    std::cout << "------------------------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "ALL STAGE 203.15 TESTS PASSED.\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED.\n";
        return 1;
    }
}

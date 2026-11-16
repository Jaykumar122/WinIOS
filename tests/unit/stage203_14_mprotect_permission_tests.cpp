// ==============================================================================
// WinIOS Campaign 203 Stage 203.14: mprotect and Permission Faults Tests
// ==============================================================================
// Independent verification of:
// 1. Full region mprotect (RW -> RO -> RW) and access state verification
// 2. Partial subrange mprotect with boundary entry splitting (P0[RW], P1[RO], P2[RW])
// 3. Apple Silicon W^X policy enforcement in mprotect (Write+Execute rejected)
// 4. mach_vm_protect with set_maximum=true and max_prot ceiling enforcement
// 5. Parameter validation (misalignment, zero length, unmapped target range)
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
// Test 1: Full Region Protection Change (RW -> RO -> RW)
// -----------------------------------------------------------------------------
void test_full_region_mprotect() {
    std::cout << "[Stage 203.14 Test 1] Full Region Protection Change (RW -> RO -> RW)...\n";

    DarwinVmSpace vm(301, 0x100000000ULL);

    std::uint64_t addr = vm.mmap(
        0, 0x8000, kDarwinVmProtRead | kDarwinVmProtWrite, kDarwinMapAnonymous | kDarwinMapPrivate);
    TEST_CHECK(addr != 0, "mmap allocation must succeed");

    std::uint8_t payload[16]{0xDE, 0xAD, 0xBE, 0xEF};
    TEST_CHECK(vm.mach_vm_write(addr, payload, sizeof(payload)) == kKernSuccess, "Initial write must succeed");

    // Change to Read-Only
    int pr1 = vm.mprotect(addr, 0x8000, kDarwinVmProtRead);
    TEST_CHECK(pr1 == kKernSuccess, "mprotect to PROT_READ must succeed");

    // Reading must succeed
    std::uint8_t read_buf[16]{0};
    TEST_CHECK(vm.mach_vm_read(addr, read_buf, sizeof(read_buf)) == kKernSuccess, "Read from RO region must succeed");
    TEST_CHECK(std::memcmp(read_buf, payload, sizeof(payload)) == 0, "Data in RO region preserved");

    // Writing must fail with protection failure
    std::uint8_t dummy[16]{0x55};
    int wr1 = vm.mach_vm_write(addr, dummy, sizeof(dummy));
    TEST_CHECK(wr1 == kKernProtectionFailure, "Writing to RO region must return kKernProtectionFailure");

    // Verify access checks
    TEST_CHECK(vm.check_access(addr, 0x8000, kDarwinVmProtRead), "check_access PROT_READ must be true");
    TEST_CHECK(!vm.check_access(addr, 0x8000, kDarwinVmProtWrite), "check_access PROT_WRITE must be false");

    // Restore RW permission
    int pr2 = vm.mprotect(addr, 0x8000, kDarwinVmProtRead | kDarwinVmProtWrite);
    TEST_CHECK(pr2 == kKernSuccess, "Restoring RW permission must succeed");

    // Writing must now succeed
    int wr2 = vm.mach_vm_write(addr, dummy, sizeof(dummy));
    TEST_CHECK(wr2 == kKernSuccess, "Writing to restored RW region must succeed");

    std::cout << "  -> Full region protection verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Subrange Protection Change and Boundary Splitting
// -----------------------------------------------------------------------------
void test_partial_subrange_mprotect_split() {
    std::cout << "[Stage 203.14 Test 2] Partial Subrange Protection & Boundary Splitting...\n";

    DarwinVmSpace vm(302, 0x100000000ULL);

    // Allocate 3 pages (0xC000)
    std::uint64_t addr = vm.mmap(
        0, 0xC000, kDarwinVmProtRead | kDarwinVmProtWrite, kDarwinMapAnonymous | kDarwinMapPrivate);
    TEST_CHECK(addr != 0, "mmap allocation must succeed");
    TEST_CHECK(vm.region_count() == 1, "Initial region count is 1");

    // Protect only middle page (P1) as Read-Only
    int pr = vm.mprotect(addr + 0x4000, 0x4000, kDarwinVmProtRead);
    TEST_CHECK(pr == kKernSuccess, "mprotecting middle page must succeed");
    TEST_CHECK(vm.region_count() == 3, "Region count must become 3 after splitting middle page");

    std::uint8_t byte_val = 0x77;

    // P0: RW
    TEST_CHECK(vm.mach_vm_write(addr, &byte_val, 1) == kKernSuccess, "Write to P0 (RW) must succeed");
    TEST_CHECK(vm.check_access(addr, 0x4000, kDarwinVmProtWrite), "P0 has write permission");

    // P1: RO
    TEST_CHECK(vm.mach_vm_write(addr + 0x4000, &byte_val, 1) == kKernProtectionFailure, "Write to P1 (RO) must fail");
    TEST_CHECK(vm.mach_vm_read(addr + 0x4000, &byte_val, 1) == kKernSuccess, "Read from P1 (RO) must succeed");
    TEST_CHECK(!vm.check_access(addr + 0x4000, 0x4000, kDarwinVmProtWrite), "P1 lacks write permission");

    // P2: RW
    TEST_CHECK(vm.mach_vm_write(addr + 0x8000, &byte_val, 1) == kKernSuccess, "Write to P2 (RW) must succeed");
    TEST_CHECK(vm.check_access(addr + 0x8000, 0x4000, kDarwinVmProtWrite), "P2 has write permission");

    std::cout << "  -> Subrange protection and boundary splitting verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Apple Silicon W^X Policy Enforcement
// -----------------------------------------------------------------------------
void test_wx_policy_rejection() {
    std::cout << "[Stage 203.14 Test 3] Apple Silicon W^X Enforcement in mprotect...\n";

    DarwinVmSpace vm(303, 0x100000000ULL);

    std::uint64_t addr = vm.mmap(
        0, 0x4000, kDarwinVmProtRead | kDarwinVmProtWrite, kDarwinMapAnonymous | kDarwinMapPrivate);
    TEST_CHECK(addr != 0, "mmap allocation must succeed");

    // Attempt to set PROT_WRITE | PROT_EXEC via mprotect
    int r1 = vm.mprotect(addr, 0x4000, kDarwinVmProtWrite | kDarwinVmProtExecute);
    TEST_CHECK(r1 == kKernProtectionFailure, "Simultaneous W|X in mprotect must return kKernProtectionFailure");

    // Attempt to set PROT_READ | PROT_WRITE | PROT_EXEC via mach_vm_protect
    int r2 = vm.mach_vm_protect(addr, 0x4000, false, kDarwinVmProtAll);
    TEST_CHECK(r2 == kKernProtectionFailure, "PROT_ALL (RWX) in mach_vm_protect must return kKernProtectionFailure");

    std::cout << "  -> W^X policy enforcement in protection APIs verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: mach_vm_protect with set_maximum and Ceiling Enforcement
// -----------------------------------------------------------------------------
void test_mach_vm_protect_set_maximum() {
    std::cout << "[Stage 203.14 Test 4] mach_vm_protect set_maximum & Ceiling Enforcement...\n";

    DarwinVmSpace vm(304, 0x100000000ULL);

    std::uint64_t addr = vm.mmap(
        0, 0x4000, kDarwinVmProtRead | kDarwinVmProtWrite, kDarwinMapAnonymous | kDarwinMapPrivate);
    TEST_CHECK(addr != 0, "mmap allocation must succeed");

    // Set maximum protection to Read-Only
    int r1 = vm.mach_vm_protect(addr, 0x4000, true /* set_maximum */, kDarwinVmProtRead);
    TEST_CHECK(r1 == kKernSuccess, "Setting max_prot to PROT_READ must succeed");

    // Current protection should be automatically clamped to PROT_READ
    const DarwinVmEntry* entry = vm.find_entry(addr);
    TEST_CHECK(entry != nullptr, "Entry must exist");
    TEST_CHECK(entry->max_prot == kDarwinVmProtRead, "Entry max_prot must be PROT_READ");
    TEST_CHECK(entry->prot == kDarwinVmProtRead, "Entry prot must be clamped to PROT_READ");

    // Now attempt to elevate current protection to RW
    int r2 = vm.mprotect(addr, 0x4000, kDarwinVmProtRead | kDarwinVmProtWrite);
    TEST_CHECK(r2 == kKernProtectionFailure, "Elevating prot beyond max_prot must return kKernProtectionFailure");

    std::cout << "  -> set_maximum and protection ceiling verified.\n";
}

// -----------------------------------------------------------------------------
// Test 5: Invalid Parameters & Unmapped Addresses
// -----------------------------------------------------------------------------
void test_invalid_parameters() {
    std::cout << "[Stage 203.14 Test 5] Invalid Parameters & Unmapped Target Range...\n";

    DarwinVmSpace vm(305, 0x100000000ULL);

    // Misaligned address
    int r1 = vm.mprotect(0x100001000ULL, 0x4000, kDarwinVmProtRead);
    TEST_CHECK(r1 == kKernInvalidArgument, "Misaligned addr returns kKernInvalidArgument");

    // Zero length
    int r2 = vm.mprotect(0x100000000ULL, 0, kDarwinVmProtRead);
    TEST_CHECK(r2 == kKernInvalidArgument, "Zero length returns kKernInvalidArgument");

    // Unmapped address
    int r3 = vm.mprotect(0x200000000ULL, 0x4000, kDarwinVmProtRead);
    TEST_CHECK(r3 == kKernInvalidAddress, "Unmapped range returns kKernInvalidAddress");

    std::cout << "  -> Invalid parameter rejection verified.\n";
}

} // namespace

int main() {
    std::cout << "==================================================================\n";
    std::cout << "Stage 203.14: mprotect and Permission Faults Tests\n";
    std::cout << "==================================================================\n";

    test_full_region_mprotect();
    test_partial_subrange_mprotect_split();
    test_wx_policy_rejection();
    test_mach_vm_protect_set_maximum();
    test_invalid_parameters();

    std::cout << "------------------------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "ALL STAGE 203.14 TESTS PASSED.\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED.\n";
        return 1;
    }
}

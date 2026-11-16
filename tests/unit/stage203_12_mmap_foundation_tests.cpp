// ==============================================================================
// WinIOS Campaign 203 Stage 203.12: mmap Foundation Tests
// ==============================================================================
// Independent verification of:
// 1. Anonymous auto-placement (addr == 0) and zero-fill initialization
// 2. MAP_FIXED page-alignment enforcement and collision resolution
// 3. Address hint placement vs automatic collision fallback
// 4. Apple Silicon W^X policy enforcement (Write+Execute rejected unless MAP_JIT)
// 5. Region protection checks (PROT_READ, PROT_WRITE, PROT_EXEC, PROT_NONE)
// ==============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
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
// Test 1: Anonymous Auto-Placement and Zero-Filling
// -----------------------------------------------------------------------------
void test_anonymous_auto_allocation() {
    std::cout << "[Stage 203.12 Test 1] Anonymous Auto-Allocation & Zero-Fill...\n";

    DarwinVmSpace vm(100, 0x100000000ULL);

    std::uint64_t addr = vm.mmap(
        0, 0x8000, kDarwinVmProtRead | kDarwinVmProtWrite, kDarwinMapAnonymous | kDarwinMapPrivate);

    TEST_CHECK(addr != 0, "mmap with addr=0 must succeed");
    TEST_CHECK(is_page_aligned(addr), "Allocated address must be 16KB aligned");
    TEST_CHECK(addr >= 0x100000000ULL, "Address must be placed within user range");

    // Verify zero-filling
    std::uint8_t buffer[128]{0xFF};
    int r = vm.mach_vm_read(addr, buffer, sizeof(buffer));
    TEST_CHECK(r == kKernSuccess, "Reading newly allocated memory must succeed");

    bool all_zero = true;
    for (std::uint8_t b : buffer) {
        if (b != 0) {
            all_zero = false;
            break;
        }
    }
    TEST_CHECK(all_zero, "Newly allocated anonymous memory must be zero-filled");

    std::cout << "  -> Anonymous auto-allocation verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: MAP_FIXED & Address Hint Placement
// -----------------------------------------------------------------------------
void test_fixed_and_hint_mapping() {
    std::cout << "[Stage 203.12 Test 2] MAP_FIXED & Address Hint Placement...\n";

    DarwinVmSpace vm(101, 0x100000000ULL);

    // 1. Valid MAP_FIXED mapping
    const std::uint64_t fixed_va = 0x100040000ULL;
    std::uint64_t res1 = vm.mmap(
        fixed_va, 0x4000, kDarwinVmProtRead | kDarwinVmProtWrite,
        kDarwinMapAnonymous | kDarwinMapPrivate | kDarwinMapFixed);

    TEST_CHECK(res1 == fixed_va, "MAP_FIXED must return the exact requested address");

    // 2. Misaligned MAP_FIXED mapping must fail (0x100041000 is not 16KB aligned)
    std::uint64_t res2 = vm.mmap(
        0x100041000ULL, 0x4000, kDarwinVmProtRead | kDarwinVmProtWrite,
        kDarwinMapAnonymous | kDarwinMapPrivate | kDarwinMapFixed);

    TEST_CHECK(res2 == 0, "Misaligned MAP_FIXED must return 0 (EINVAL)");

    // 3. Hint mapping at existing fixed_va must detect collision and fall back to free range
    std::uint64_t res3 = vm.mmap(
        fixed_va, 0x4000, kDarwinVmProtRead | kDarwinVmProtWrite,
        kDarwinMapAnonymous | kDarwinMapPrivate); // No MAP_FIXED!

    TEST_CHECK(res3 != 0, "Colliding hint mapping must succeed via fallback");
    TEST_CHECK(res3 != fixed_va, "Fallback address must not collide with fixed_va");
    TEST_CHECK(is_page_aligned(res3), "Fallback address must be page aligned");

    std::cout << "  -> MAP_FIXED and hint placement verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Apple Silicon W^X Policy Enforcement & MAP_JIT Exception
// -----------------------------------------------------------------------------
void test_wx_policy_enforcement() {
    std::cout << "[Stage 203.12 Test 3] Apple Silicon W^X Policy Enforcement...\n";

    DarwinVmSpace vm(102, 0x100000000ULL);

    // 1. Simultaneous Write + Execute without MAP_JIT must be rejected
    std::uint64_t wx_fail = vm.mmap(
        0, 0x4000, kDarwinVmProtWrite | kDarwinVmProtExecute,
        kDarwinMapAnonymous | kDarwinMapPrivate);

    TEST_CHECK(wx_fail == 0, "Simultaneous Write+Execute without MAP_JIT must be rejected");

    // 2. Simultaneous Write + Execute with MAP_JIT is permitted
    std::uint64_t jit_pass = vm.mmap(
        0, 0x4000, kDarwinVmProtWrite | kDarwinVmProtExecute,
        kDarwinMapAnonymous | kDarwinMapPrivate | kDarwinMapJit);

    TEST_CHECK(jit_pass != 0, "Simultaneous Write+Execute with MAP_JIT must be permitted");

    // 3. Read + Execute is permitted without MAP_JIT
    std::uint64_t rx_pass = vm.mmap(
        0, 0x4000, kDarwinVmProtRead | kDarwinVmProtExecute,
        kDarwinMapAnonymous | kDarwinMapPrivate);

    TEST_CHECK(rx_pass != 0, "Read+Execute must be permitted");

    std::cout << "  -> W^X policy enforcement verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Region Protection Verification
// -----------------------------------------------------------------------------
void test_region_protection_checks() {
    std::cout << "[Stage 203.12 Test 4] Region Protection Checks...\n";

    DarwinVmSpace vm(103, 0x100000000ULL);

    // Allocate Read-Only mapping
    std::uint64_t ro_addr = vm.mmap(
        0, 0x4000, kDarwinVmProtRead, kDarwinMapAnonymous | kDarwinMapPrivate);

    TEST_CHECK(vm.check_access(ro_addr, 0x100, kDarwinVmProtRead), "Read-only region allows read");
    TEST_CHECK(!vm.check_access(ro_addr, 0x100, kDarwinVmProtWrite), "Read-only region rejects write");
    TEST_CHECK(!vm.check_access(ro_addr, 0x100, kDarwinVmProtExecute), "Read-only region rejects execute");

    // Allocate Guard region (PROT_NONE)
    std::uint64_t guard_addr = vm.mmap(
        0, 0x4000, kDarwinVmProtNone, kDarwinMapAnonymous | kDarwinMapPrivate);

    TEST_CHECK(!vm.check_access(guard_addr, 0x100, kDarwinVmProtRead), "Guard region rejects read");
    TEST_CHECK(!vm.check_access(guard_addr, 0x100, kDarwinVmProtWrite), "Guard region rejects write");
    TEST_CHECK(!vm.check_access(guard_addr, 0x100, kDarwinVmProtExecute), "Guard region rejects execute");

    std::cout << "  -> Region protection checks verified.\n";
}

} // namespace

int main() {
    std::cout << "==================================================================\n";
    std::cout << "Stage 203.12: mmap Foundation Tests\n";
    std::cout << "==================================================================\n";

    test_anonymous_auto_allocation();
    test_fixed_and_hint_mapping();
    test_wx_policy_enforcement();
    test_region_protection_checks();

    std::cout << "------------------------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "ALL STAGE 203.12 TESTS PASSED.\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED.\n";
        return 1;
    }
}

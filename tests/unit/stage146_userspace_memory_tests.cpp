#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "runtime/darwin_bsd_subsystem.h"

using namespace winios;
using namespace winios::runtime;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

constexpr std::uint32_t kProtNone    = 0x0u;
constexpr std::uint32_t kProtRead    = 0x1u;
constexpr std::uint32_t kProtWrite   = 0x2u;
constexpr std::uint32_t kProtExecute = 0x4u;
constexpr std::uint32_t kMapAnon     = 0x1000u;
constexpr std::uint32_t kMapPrivate  = 0x0002u;
constexpr std::uint32_t kMapJit      = 0x0800u;

// Test 1: 16 KB Granule Alignment and Auto-Allocation
bool test_16kb_page_granule_allocation() {
    std::cout << "[Test 1] 16 KB granule alignment and auto-allocation...\n";

    DarwinBsdSubsystem bsd;
    const std::uint32_t pid = 1;

    // Allocate 1 byte -> must round up to 16 KB (0x4000)
    std::uint64_t addr1 = bsd.mmap(pid, 0, 1, kProtRead | kProtWrite, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(addr1 != 0, "mmap 1 byte must succeed");
    TEST_ASSERT((addr1 & 0x3FFFULL) == 0, "Address 1 must be 16KB aligned");

    // Allocate another region
    std::uint64_t addr2 = bsd.mmap(pid, 0, 0x5000, kProtRead, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(addr2 != 0, "mmap 0x5000 must succeed");
    TEST_ASSERT((addr2 & 0x3FFFULL) == 0, "Address 2 must be 16KB aligned");
    TEST_ASSERT(addr2 >= addr1 + 0x4000, "Address 2 must not collide with Address 1");

    return true;
}

// Test 2: Read-Only, Read-Write, and Executable Permissions
bool test_vm_permission_enforcement() {
    std::cout << "[Test 2] Read-Only, Read-Write, and Executable permission checks...\n";

    DarwinBsdSubsystem bsd;
    const std::uint32_t pid = 1;

    // Read-only region
    std::uint64_t ro_addr = bsd.mmap(pid, 0, 0x4000, kProtRead, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(bsd.check_vm_access(pid, ro_addr, 0x1000, kProtRead), "Read on RO mapping must be allowed");
    TEST_ASSERT(!bsd.check_vm_access(pid, ro_addr, 0x1000, kProtWrite), "Write on RO mapping must be denied");
    TEST_ASSERT(!bsd.check_vm_access(pid, ro_addr, 0x1000, kProtExecute), "Execute on RO mapping must be denied");

    // Read-Write region
    std::uint64_t rw_addr = bsd.mmap(pid, 0, 0x4000, kProtRead | kProtWrite, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(bsd.check_vm_access(pid, rw_addr, 0x1000, kProtRead), "Read on RW mapping must be allowed");
    TEST_ASSERT(bsd.check_vm_access(pid, rw_addr, 0x1000, kProtWrite), "Write on RW mapping must be allowed");
    TEST_ASSERT(!bsd.check_vm_access(pid, rw_addr, 0x1000, kProtExecute), "Execute on RW mapping must be denied");

    // Executable (RX) region
    std::uint64_t rx_addr = bsd.mmap(pid, 0, 0x4000, kProtRead | kProtExecute, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(bsd.check_vm_access(pid, rx_addr, 0x1000, kProtRead), "Read on RX mapping must be allowed");
    TEST_ASSERT(bsd.check_vm_access(pid, rx_addr, 0x1000, kProtExecute), "Execute on RX mapping must be allowed");
    TEST_ASSERT(!bsd.check_vm_access(pid, rx_addr, 0x1000, kProtWrite), "Write on RX mapping must be denied");

    return true;
}

// Test 3: Guard Page (PROT_NONE) Rejection
bool test_guard_page_prot_none() {
    std::cout << "[Test 3] Guard Page (PROT_NONE) strict access rejection...\n";

    DarwinBsdSubsystem bsd;
    const std::uint32_t pid = 1;

    std::uint64_t guard_addr = bsd.mmap(pid, 0, 0x4000, kProtNone, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(guard_addr != 0, "Guard region mapping must succeed");

    TEST_ASSERT(!bsd.check_vm_access(pid, guard_addr, 0x1000, kProtRead), "Read on guard page must be denied");
    TEST_ASSERT(!bsd.check_vm_access(pid, guard_addr, 0x1000, kProtWrite), "Write on guard page must be denied");
    TEST_ASSERT(!bsd.check_vm_access(pid, guard_addr, 0x1000, kProtExecute), "Execute on guard page must be denied");

    return true;
}

// Test 4: Apple Silicon W^X Policy & MAP_JIT Exception
bool test_wx_policy_and_map_jit() {
    std::cout << "[Test 4] Apple Silicon W^X Policy & MAP_JIT Exception...\n";

    DarwinBsdSubsystem bsd;
    const std::uint32_t pid = 1;

    // 1. Attempt simultaneous Write + Execute (RWX) without MAP_JIT -> MUST BE REJECTED (return 0)
    std::uint64_t rwx_addr = bsd.mmap(pid, 0, 0x4000, kProtRead | kProtWrite | kProtExecute,
                                      kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(rwx_addr == 0, "Simultaneous W+X without MAP_JIT must be rejected by W^X policy");

    // 2. Simultaneous Write + Execute WITH MAP_JIT -> ALLOWED
    std::uint64_t jit_addr = bsd.mmap(pid, 0, 0x4000, kProtRead | kProtWrite | kProtExecute,
                                      kMapAnon | kMapPrivate | kMapJit, -1, 0);
    TEST_ASSERT(jit_addr != 0, "W+X with MAP_JIT must be permitted for JIT engines");
    TEST_ASSERT(bsd.check_vm_access(pid, jit_addr, 0x1000, kProtWrite), "Write on JIT must be permitted");
    TEST_ASSERT(bsd.check_vm_access(pid, jit_addr, 0x1000, kProtExecute), "Execute on JIT must be permitted");

    return true;
}

// Test 5: Mapping Overlap Rejection
bool test_mapping_overlap_rejection() {
    std::cout << "[Test 5] Mapping Overlap Rejection...\n";

    DarwinBsdSubsystem bsd;
    const std::uint32_t pid = 1;

    // Map a fixed region at 0x180000000 (16KB)
    const std::uint64_t base = 0x180000000ULL;
    std::uint64_t addr1 = bsd.mmap(pid, base, 0x4000, kProtRead | kProtWrite, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(addr1 == base, "Fixed mmap at base must succeed");

    // Attempt to map overlapping region at base (exact overlap)
    std::uint64_t col1 = bsd.mmap(pid, base, 0x4000, kProtRead, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(col1 == 0, "Exact overlapping fixed mmap must be rejected");

    // Attempt to map overlapping region at base + 0x2000 (partial overlap)
    // Note: base + 0x2000 is also misaligned (not 16KB aligned), so it must be rejected
    std::uint64_t col2 = bsd.mmap(pid, base + 0x2000, 0x4000, kProtRead, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(col2 == 0, "Misaligned overlapping mmap must be rejected");

    // Map at non-overlapping aligned address base + 0x4000 -> must succeed
    std::uint64_t addr2 = bsd.mmap(pid, base + 0x4000, 0x4000, kProtRead, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(addr2 == base + 0x4000, "Contiguous non-overlapping mmap must succeed");

    return true;
}

// Test 6: Address Space Overflow and Invalid Pointer Rejection
bool test_address_overflow_and_bounds() {
    std::cout << "[Test 6] Address space overflow and invalid pointer bounds...\n";

    DarwinBsdSubsystem bsd;
    const std::uint32_t pid = 1;

    // 1. Length causing 64-bit overflow: addr = 0xFFFFFFFFFFFF0000, len = 0x20000
    std::uint64_t overflow_addr = 0xFFFFFFFFFFFF0000ULL;
    std::uint64_t res = bsd.mmap(pid, overflow_addr, 0x20000, kProtRead, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(res == 0, "mmap with address overflow must be rejected");

    // 2. Zero-length allocation
    res = bsd.mmap(pid, 0, 0, kProtRead, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(res == 0, "Zero-length mmap must be rejected");

    // 3. Unmapped pointer check
    TEST_ASSERT(!bsd.check_vm_access(pid, 0x00000000DEAD0000ULL, 0x100, kProtRead), "Unmapped address must fail check");

    return true;
}

// Test 7: Unmapping (munmap) and Access Invalidation
bool test_munmap_invalidation() {
    std::cout << "[Test 7] Unmapping (munmap) and access invalidation...\n";

    DarwinBsdSubsystem bsd;
    const std::uint32_t pid = 1;

    std::uint64_t addr = bsd.mmap(pid, 0, 0x4000, kProtRead | kProtWrite, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(addr != 0, "mmap must succeed");
    TEST_ASSERT(bsd.check_vm_access(pid, addr, 0x1000, kProtRead), "Access must be valid before unmap");

    // Unmap
    bool unmapped = bsd.munmap(pid, addr, 0x4000);
    TEST_ASSERT(unmapped, "munmap must return true for mapped region");

    // Access must now be rejected
    TEST_ASSERT(!bsd.check_vm_access(pid, addr, 0x1000, kProtRead), "Access must be denied after unmap");

    // Double unmap must fail
    TEST_ASSERT(!bsd.munmap(pid, addr, 0x4000), "Double munmap must return false");

    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  WINIOS STAGE 146: USERSPACE VIRTUAL MEMORY TEST SUITE          \n";
    std::cout << "=================================================================\n\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*fn)(), const char* name) {
        if (fn()) {
            std::cout << "  -> PASS: " << name << "\n\n";
            passed++;
        } else {
            std::cout << "  -> FAIL: " << name << "\n\n";
            failed++;
        }
    };

    run_test(test_16kb_page_granule_allocation, "16 KB Granule Alignment and Auto-Allocation");
    run_test(test_vm_permission_enforcement, "Read-Only, Read-Write, Executable Permissions");
    run_test(test_guard_page_prot_none, "Guard Page (PROT_NONE) Access Rejection");
    run_test(test_wx_policy_and_map_jit, "Apple Silicon W^X Policy & MAP_JIT Exception");
    run_test(test_mapping_overlap_rejection, "Mapping Overlap Rejection");
    run_test(test_address_overflow_and_bounds, "Address Space Overflow & Invalid Pointer Bounds");
    run_test(test_munmap_invalidation, "Unmapping (munmap) and Access Invalidation");

    std::cout << "=================================================================\n";
    std::cout << "STAGE 146 RESULTS: " << passed << " PASSED, " << failed << " FAILED\n";
    std::cout << "=================================================================\n";

    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

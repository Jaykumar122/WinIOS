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

// Test 1: 16KB Page Granule Conformance and Alignment
bool test_16kb_page_granule_conformance() {
    std::cout << "[Test 1] 16KB Page Granule Conformance and Alignment...\n";

    DarwinBsdSubsystem bsd;
    const std::uint32_t pid = 1;

    // Allocate 1 byte -> must round up to 16KB (0x4000)
    std::uint64_t addr1 = bsd.mmap(pid, 0, 1, kProtRead | kProtWrite, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(addr1 != 0, "mmap of 1 byte must succeed");
    TEST_ASSERT((addr1 & 0x3FFFULL) == 0, "Address must be strictly 16KB aligned (lower 14 bits zero)");

    // Allocate 16385 bytes (16KB + 1) -> must round up to 32KB (0x8000)
    std::uint64_t addr2 = bsd.mmap(pid, 0, 16385, kProtRead, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(addr2 != 0, "mmap of 16385 bytes must succeed");
    TEST_ASSERT((addr2 & 0x3FFFULL) == 0, "Address must be 16KB aligned");
    TEST_ASSERT(addr2 >= addr1 + 0x4000, "Address 2 must not collide with Address 1");

    return true;
}

// Test 2: Apple Silicon W^X Security Policy Enforcement
bool test_wx_security_policy_enforcement() {
    std::cout << "[Test 2] Apple Silicon W^X Security Policy Enforcement...\n";

    DarwinBsdSubsystem bsd;
    const std::uint32_t pid = 1;

    // 1. RWX without MAP_JIT: Strict rejection
    std::uint64_t rwx_addr = bsd.mmap(pid, 0, 0x4000, kProtRead | kProtWrite | kProtExecute,
                                      kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(rwx_addr == 0, "Simultaneous W+X without MAP_JIT must be rejected by W^X policy");

    // 2. RW without X: Permitted
    std::uint64_t rw_addr = bsd.mmap(pid, 0, 0x4000, kProtRead | kProtWrite,
                                     kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(rw_addr != 0, "RW mapping must be permitted");
    TEST_ASSERT(bsd.check_vm_access(pid, rw_addr, 0x1000, kProtWrite), "Write on RW must succeed");
    TEST_ASSERT(!bsd.check_vm_access(pid, rw_addr, 0x1000, kProtExecute), "Execute on RW must be forbidden");

    // 3. RX without W: Permitted
    std::uint64_t rx_addr = bsd.mmap(pid, 0, 0x4000, kProtRead | kProtExecute,
                                     kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(rx_addr != 0, "RX mapping must be permitted");
    TEST_ASSERT(bsd.check_vm_access(pid, rx_addr, 0x1000, kProtExecute), "Execute on RX must succeed");
    TEST_ASSERT(!bsd.check_vm_access(pid, rx_addr, 0x1000, kProtWrite), "Write on RX must be forbidden");

    // 4. RWX with MAP_JIT: Explicit JIT Exception
    std::uint64_t jit_addr = bsd.mmap(pid, 0, 0x4000, kProtRead | kProtWrite | kProtExecute,
                                      kMapAnon | kMapPrivate | kMapJit, -1, 0);
    TEST_ASSERT(jit_addr != 0, "RWX with MAP_JIT must be permitted for JIT engines");
    TEST_ASSERT(bsd.check_vm_access(pid, jit_addr, 0x1000, kProtWrite), "Write on JIT must succeed");
    TEST_ASSERT(bsd.check_vm_access(pid, jit_addr, 0x1000, kProtExecute), "Execute on JIT must succeed");

    return true;
}

// Test 3: Guard Page (PROT_NONE) Access Rejection
bool test_guard_page_prot_none() {
    std::cout << "[Test 3] Guard Page (PROT_NONE) Access Rejection...\n";

    DarwinBsdSubsystem bsd;
    const std::uint32_t pid = 1;

    std::uint64_t guard_addr = bsd.mmap(pid, 0, 0x4000, kProtNone, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(guard_addr != 0, "Guard page mapping must succeed");

    // All access types must be denied on PROT_NONE
    TEST_ASSERT(!bsd.check_vm_access(pid, guard_addr, 0x1000, kProtRead), "Read on guard page must be denied");
    TEST_ASSERT(!bsd.check_vm_access(pid, guard_addr, 0x1000, kProtWrite), "Write on guard page must be denied");
    TEST_ASSERT(!bsd.check_vm_access(pid, guard_addr, 0x1000, kProtExecute), "Execute on guard page must be denied");

    return true;
}

// Test 4: PAGEZERO Protection & Null Pointer Trap
bool test_pagezero_protection() {
    std::cout << "[Test 4] PAGEZERO Protection & Null Pointer Trap...\n";

    DarwinBsdSubsystem bsd;
    const std::uint32_t pid = 1;

    // Address 0x0 (NULL pointer) must never be accessible
    TEST_ASSERT(!bsd.check_vm_access(pid, 0x0ULL, 8, kProtRead), "NULL pointer read must be trapped");
    TEST_ASSERT(!bsd.check_vm_access(pid, 0x0ULL, 8, kProtWrite), "NULL pointer write must be trapped");
    TEST_ASSERT(!bsd.check_vm_access(pid, 0x0ULL, 4, kProtExecute), "NULL pointer execute must be trapped");

    // Entire lower pagezero (e.g. 0x00000000 to 0x00003FFF) must be inaccessible
    TEST_ASSERT(!bsd.check_vm_access(pid, 0x2000ULL, 16, kProtRead), "Pagezero internal address must be inaccessible");

    return true;
}

// Test 5: Virtual Address Bounds, Alignment, and Integer Overflow
bool test_virtual_address_bounds_and_overflow() {
    std::cout << "[Test 5] Virtual Address Bounds, Alignment, and Overflow...\n";

    DarwinBsdSubsystem bsd;
    const std::uint32_t pid = 1;

    // 1. 64-bit integer overflow in mmap: addr + len wraps
    std::uint64_t overflow_addr = 0xFFFFFFFFFFFF0000ULL;
    std::uint64_t res = bsd.mmap(pid, overflow_addr, 0x20000, kProtRead, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(res == 0, "Address overflow must be rejected");

    // 2. Zero-length allocation
    res = bsd.mmap(pid, 0, 0, kProtRead, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(res == 0, "Zero-length allocation must be rejected");

    // 3. Misaligned fixed address (e.g. 0x180001000)
    res = bsd.mmap(pid, 0x180001000ULL, 0x4000, kProtRead, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(res == 0, "Misaligned fixed base address must be rejected");

    return true;
}

// Test 6: munmap Revocation and Space Re-allocation
bool test_munmap_revocation() {
    std::cout << "[Test 6] munmap Revocation and Space Re-allocation...\n";

    DarwinBsdSubsystem bsd;
    const std::uint32_t pid = 1;

    // Fixed mapping at 0x200000000
    const std::uint64_t base = 0x200000000ULL;
    std::uint64_t addr = bsd.mmap(pid, base, 0x4000, kProtRead | kProtWrite, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(addr == base, "Fixed mmap must succeed");
    TEST_ASSERT(bsd.check_vm_access(pid, base, 0x1000, kProtRead), "Access must be valid before munmap");

    // Unmap region
    bool ret = bsd.munmap(pid, base, 0x4000);
    TEST_ASSERT(ret, "munmap must return true for mapped region");
    TEST_ASSERT(!bsd.check_vm_access(pid, base, 0x1000, kProtRead), "Access must be revoked after munmap");

    // Re-map at the exact same base address
    std::uint64_t remapped = bsd.mmap(pid, base, 0x4000, kProtRead, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(remapped == base, "Re-mapping previously unmapped address must succeed");

    return true;
}

// Test 7: Multi-Process Virtual Memory Isolation
bool test_multi_process_vm_isolation() {
    std::cout << "[Test 7] Multi-Process Virtual Memory Isolation...\n";

    DarwinBsdSubsystem bsd;

    // Create process 1 and process 2
    bsd.create_process_with_pid(1, 0, "/sbin/launchd", 0);
    bsd.create_process_with_pid(2, 1, "/usr/libexec/installd", 501);

    const std::uint64_t target_vaddr = 0x300000000ULL;

    // Map target_vaddr in Process 1 as Read-Write
    std::uint64_t a1 = bsd.mmap(1, target_vaddr, 0x4000, kProtRead | kProtWrite, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(a1 == target_vaddr, "Process 1 mmap must succeed");

    // In Process 2, target_vaddr must be completely unmapped
    TEST_ASSERT(!bsd.check_vm_access(2, target_vaddr, 0x1000, kProtRead),
                "Process 2 must NOT have access to Process 1's mapping");

    // Process 2 can independently map target_vaddr as Read-Execute
    std::uint64_t a2 = bsd.mmap(2, target_vaddr, 0x4000, kProtRead | kProtExecute, kMapAnon | kMapPrivate, -1, 0);
    TEST_ASSERT(a2 == target_vaddr, "Process 2 mmap at same virtual address must succeed");

    // Process 1 has Write, Process 2 does NOT
    TEST_ASSERT(bsd.check_vm_access(1, target_vaddr, 0x1000, kProtWrite), "Process 1 must have Write");
    TEST_ASSERT(!bsd.check_vm_access(2, target_vaddr, 0x1000, kProtWrite), "Process 2 must NOT have Write");

    // Process 2 has Execute, Process 1 does NOT
    TEST_ASSERT(bsd.check_vm_access(2, target_vaddr, 0x1000, kProtExecute), "Process 2 must have Execute");
    TEST_ASSERT(!bsd.check_vm_access(1, target_vaddr, 0x1000, kProtExecute), "Process 1 must NOT have Execute");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 160: ARM64 Virtual Memory & W^X Conformance\n";
    std::cout << "=========================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](auto test_fn, const char* name) {
        if (test_fn()) {
            std::cout << "[PASS] " << name << "\n";
            passed++;
        } else {
            std::cout << "[FAIL] " << name << "\n";
            failed++;
        }
    };

    run_test(test_16kb_page_granule_conformance, "16KB Page Granule Conformance");
    run_test(test_wx_security_policy_enforcement, "Apple Silicon W^X Policy & MAP_JIT Exception");
    run_test(test_guard_page_prot_none, "Guard Page (PROT_NONE) Access Rejection");
    run_test(test_pagezero_protection, "PAGEZERO Protection & Null Pointer Trap");
    run_test(test_virtual_address_bounds_and_overflow, "Virtual Address Bounds and Overflow");
    run_test(test_munmap_revocation, "munmap Revocation and Re-allocation");
    run_test(test_multi_process_vm_isolation, "Multi-Process VM Isolation");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}

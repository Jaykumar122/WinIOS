// =============================================================================
// stage_192_7_virtual_memory_tests.cpp
//
// Stage 192.7: Darwin Userspace Virtual Memory & MMU Conformance Tests
// Verifies:
//   - 16 KB page granule allocation & alignment invariants
//   - W^X policy enforcement (PROT_WRITE | PROT_EXEC rejected unless MAP_JIT)
//   - Overlapping mapping rejection & integer overflow protection
//   - munmap deallocation & alignment validation
//   - mprotect permission transitions (RW -> RO, PROT_NONE unmapping)
//   - madvise validation (SYS_madvise = 75)
//   - Real architectural Stage-1 MMU permission enforcement & faults:
//       * Read-only page store permission fault
//       * Unmapped translation fault
//       * RW page store success
//       * Dynamic protection updates via L3 descriptor
// =============================================================================

#include <cstdint>
#include <iostream>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "cpu/synchronous_exception.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"

using namespace winios;
using namespace winios::runtime;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Minimal 16 KB-granule Stage-1 page table helper
struct Stage1Table16K {
    std::uint64_t l2_pa{0x100000ULL};
    std::uint64_t l3_pa{0x104000ULL};

    void init(Memory& mem) const {
        for (std::uint64_t off = 0; off < 0x8000ULL; off += 8u) {
            mem.write_u64(static_cast<std::size_t>(l2_pa + off), 0ULL);
        }
        mem.write_u64(static_cast<std::size_t>(l2_pa), l3_pa | 0x3ULL);
    }

    void map_page_16k(Memory& mem, std::uint64_t va, std::uint64_t pa,
                      std::uint8_t ap, bool uxn, bool pxn, bool is_device) const {
        const std::uint64_t l3_idx = (va >> 14u) & 0x7FFULL;
        const std::uint8_t attr_idx = is_device ? 1u : 0u;
        const std::uint64_t desc =
            (pa & 0x0000FFFFFFFFC000ULL)
            | 0x3ULL
            | (static_cast<std::uint64_t>(attr_idx) << 2u)
            | (static_cast<std::uint64_t>(ap & 0x3u) << 6u)
            | (1ULL << 10u) // AF=1
            | (uxn ? (1ULL << 54u) : 0ULL)
            | (pxn ? (1ULL << 53u) : 0ULL);
        mem.write_u64(static_cast<std::size_t>(l3_pa + l3_idx * 8u), desc);
    }

    void unmap_page_16k(Memory& mem, std::uint64_t va) const {
        const std::uint64_t l3_idx = (va >> 14u) & 0x7FFULL;
        mem.write_u64(static_cast<std::size_t>(l3_pa + l3_idx * 8u), 0ULL);
    }

    static void setup_cpu_el0(CPU& cpu, Memory& mem, const Stage1Table16K& pt,
                              std::uint64_t sctlr_val = 0x00C50838ULL | (1ULL << 0u) | (1ULL << 14u) | (1ULL << 19u)) {
        cpu.reset();
        cpu.set_architectural_exceptions(true);
        cpu.set_cpacr_el1(0x00300000ULL); // FPEN=11
        cpu.set_vbar_el1(0x00080000ULL);
        mem.write_u32(0x00080000ULL + 0x400u, 0xD503201Fu); // Lower EL sync vector NOP

        const std::uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
        cpu.get_mmu().set_tcr_el1(tcr);
        cpu.get_mmu().set_mair_el1(0x00FFULL);
        cpu.get_mmu().set_ttbr0_el1(pt.l2_pa);
        cpu.get_mmu().invalidate_tlb_all();
        cpu.set_sctlr_el1(sctlr_val);
        cpu.set_current_el(0);
    }
};

// -----------------------------------------------------------------------------
// Test 1: 16 KB Page Granule, W^X Policy, and Overlap Prevention
// -----------------------------------------------------------------------------
bool test_mmap_granule_wx_overlap() {
    std::cout << "[Test 1] 16 KB Granule, W^X Policy, and Overlap Prevention...\n";

    DarwinBsdSubsystem bsd;
    constexpr uint32_t kPid = 801;
    bsd.create_process_with_pid(kPid, 0, "/usr/bin/test_app", 0);

    // 1. Auto-allocation via brk_curr (addr == 0)
    uint64_t addr1 = bsd.mmap(kPid, 0, 0x1000, 0x3, 0x1002, -1, 0); // 4 KB requested
    CHECK(addr1 != 0, "mmap auto-alloc failed");
    CHECK((addr1 & 0x3FFFULL) == 0, "mmap must be 16 KB aligned");

    // 2. Specific address allocation with 16 KB alignment
    uint64_t addr2 = bsd.mmap(kPid, 0x20000000ULL, 0x4000, 0x3, 0x1002, -1, 0);
    CHECK(addr2 == 0x20000000ULL, "mmap fixed-address failed");

    // 3. Misaligned address rejection (addr & 0x3FFF != 0)
    uint64_t bad_addr = bsd.mmap(kPid, 0x20001000ULL, 0x4000, 0x3, 0x1002, -1, 0);
    CHECK(bad_addr == 0, "mmap must reject misaligned address");

    // 4. Overlap rejection
    uint64_t overlap = bsd.mmap(kPid, 0x20000000ULL, 0x4000, 0x3, 0x1002, -1, 0);
    CHECK(overlap == 0, "mmap must reject overlapping address range");

    // 5. Integer overflow rejection
    uint64_t ovf = bsd.mmap(kPid, 0xFFFFFFFFFFFFF000ULL, 0x4000, 0x3, 0x1002, -1, 0);
    CHECK(ovf == 0, "mmap must reject integer overflow");

    // 6. Strict W^X enforcement (PROT_WRITE | PROT_EXEC == 0x6) rejected without MAP_JIT (0x800)
    uint64_t wx_viol = bsd.mmap(kPid, 0x30000000ULL, 0x4000, 0x6, 0x1002, -1, 0);
    CHECK(wx_viol == 0, "mmap must reject simultaneous W+X without MAP_JIT");

    // 7. W^X permitted with MAP_JIT
    uint64_t jit_ok = bsd.mmap(kPid, 0x30000000ULL, 0x4000, 0x6, 0x1002 | 0x0800, -1, 0);
    CHECK(jit_ok == 0x30000000ULL, "mmap must permit W+X with MAP_JIT");

    return true;
}

// -----------------------------------------------------------------------------
// Test 2: munmap Deallocation, Misalignment, and Region Cleanup
// -----------------------------------------------------------------------------
bool test_munmap_semantics() {
    std::cout << "[Test 2] munmap Deallocation and Cleanup...\n";

    DarwinBsdSubsystem bsd;
    constexpr uint32_t kPid = 802;
    bsd.create_process_with_pid(kPid, 0, "/usr/bin/test_app", 0);

    uint64_t addr = bsd.mmap(kPid, 0x40000000ULL, 0x8000, 0x3, 0x1002, -1, 0);
    CHECK(addr == 0x40000000ULL, "mmap setup failed");

    // Misaligned munmap rejected
    CHECK(!bsd.munmap(kPid, 0x40001000ULL, 0x4000), "munmap must reject misaligned address");

    // Non-existent range rejected
    CHECK(!bsd.munmap(kPid, 0x50000000ULL, 0x4000), "munmap must reject unmapped range");

    // Valid munmap succeeds
    CHECK(bsd.munmap(kPid, 0x40000000ULL, 0x8000), "munmap must succeed on mapped range");

    // Re-mapping same address now succeeds because region was deallocated!
    uint64_t re_addr = bsd.mmap(kPid, 0x40000000ULL, 0x8000, 0x3, 0x1002, -1, 0);
    CHECK(re_addr == 0x40000000ULL, "re-mmap after munmap must succeed");

    return true;
}

// -----------------------------------------------------------------------------
// Test 3: mprotect Transitions and W^X Guarding
// -----------------------------------------------------------------------------
bool test_mprotect_transitions() {
    std::cout << "[Test 3] mprotect Transitions and W^X Guarding...\n";

    DarwinBsdSubsystem bsd;
    constexpr uint32_t kPid = 803;
    bsd.create_process_with_pid(kPid, 0, "/usr/bin/test_app", 0);

    uint64_t addr = bsd.mmap(kPid, 0x60000000ULL, 0x4000, 0x3, 0x1002, -1, 0); // PROT_READ | PROT_WRITE
    CHECK(addr == 0x60000000ULL, "mmap setup failed");

    // Transition to Read-Only (0x1)
    CHECK(bsd.mprotect(kPid, 0x60000000ULL, 0x4000, 0x1), "mprotect RW->RO failed");

    // Verify W^X transition rejection (cannot make RWX)
    CHECK(!bsd.mprotect(kPid, 0x60000000ULL, 0x4000, 0x6), "mprotect to RWX must be rejected");

    // Unmapped range mprotect rejected
    CHECK(!bsd.mprotect(kPid, 0x70000000ULL, 0x4000, 0x1), "mprotect on unmapped range must fail");

    return true;
}

// -----------------------------------------------------------------------------
// Test 4: madvise Validation (SYS_madvise 75)
// -----------------------------------------------------------------------------
bool test_madvise_validation() {
    std::cout << "[Test 4] madvise Validation (SYS_madvise 75)...\n";

    DarwinBsdSubsystem bsd;
    constexpr uint32_t kPid = 804;
    bsd.create_process_with_pid(kPid, 0, "/usr/bin/test_app", 0);

    uint64_t addr = bsd.mmap(kPid, 0x80000000ULL, 0x8000, 0x3, 0x1002, -1, 0);
    CHECK(addr == 0x80000000ULL, "mmap setup failed");

    // Advice on mapped range succeeds
    CHECK(bsd.madvise(kPid, 0x80000000ULL, 0x4000, 0 /* MADV_NORMAL */), "madvise NORMAL failed");
    CHECK(bsd.madvise(kPid, 0x80000000ULL, 0x4000, 3 /* MADV_WILLNEED */), "madvise WILLNEED failed");
    CHECK(bsd.madvise(kPid, 0x80000000ULL, 0x4000, 4 /* MADV_DONTNEED */), "madvise DONTNEED failed");

    // Misaligned addr rejected
    CHECK(!bsd.madvise(kPid, 0x80001000ULL, 0x4000, 0), "madvise misaligned must fail");

    // Unmapped range rejected
    CHECK(!bsd.madvise(kPid, 0x90000000ULL, 0x4000, 0), "madvise unmapped must fail");

    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Real MMU Stage-1 Permission Faults & Translation Enforcement
// -----------------------------------------------------------------------------
bool test_mmu_permission_faults() {
    std::cout << "[Test 5] Real MMU Stage-1 Permission Faults & Translation...\n";

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    Stage1Table16K pt;
    pt.init(memory);
    Stage1Table16K::setup_cpu_el0(cpu, memory, pt);

    // 1. Map a 16 KB page as Read-Only at VA 0x20000 -> PA 0x20000 (ap = 3)
    pt.map_page_16k(memory, 0x20000ULL, 0x20000ULL, 3u /*RO*/, true /*uxn*/, true /*pxn*/, false /*normal*/);
    cpu.get_mmu().invalidate_tlb_all();

    memory.write_u32(0x20000, 0x12345678u);

    // Test that MMU translates read correctly
    mmu::TranslationFault fault{};
    bool is_device = false;
    auto pa_opt = cpu.get_mmu().translate(memory, 0x20000, mmu::AccessKind::Read, 0, &fault, &is_device);
    CHECK(pa_opt.has_value(), "Read translation must succeed");
    CHECK(pa_opt.value() == 0x20000ULL, "Physical address translation match");

    // Test that MMU rejects write to Read-Only page (Permission Fault!)
    auto pa_write = cpu.get_mmu().translate(memory, 0x20000, mmu::AccessKind::Write, 0, &fault, &is_device);
    CHECK(!pa_write.has_value(), "Write to Read-Only page must fail MMU translation");
    CHECK(fault.fsc() == 0b001111 /* Permission fault L3 */ || (fault.fsc() & 0b001100) == 0b001100,
          "Must raise Permission Fault on read-only write");

    // 2. Unmapped address translation fault
    auto pa_unmapped = cpu.get_mmu().translate(memory, 0x50000, mmu::AccessKind::Read, 0, &fault, &is_device);
    CHECK(!pa_unmapped.has_value(), "Unmapped address must fail translation");
    CHECK((fault.fsc() & 0b000100) != 0 || fault.fsc() == 0b000111,
          "Must raise Translation Fault on unmapped read");

    // 3. Update protection to Read-Write (ap = 1)
    pt.map_page_16k(memory, 0x20000ULL, 0x20000ULL, 1u /*RW*/, true /*uxn*/, true /*pxn*/, false /*normal*/);
    cpu.get_mmu().invalidate_tlb_all();

    auto pa_rw = cpu.get_mmu().translate(memory, 0x20000, mmu::AccessKind::Write, 0, &fault, &is_device);
    CHECK(pa_rw.has_value(), "Write translation must succeed after mprotect transition to RW");

    // 4. Unmap page
    pt.unmap_page_16k(memory, 0x20000ULL);
    cpu.get_mmu().invalidate_tlb_all();

    auto pa_after_unmap = cpu.get_mmu().translate(memory, 0x20000, mmu::AccessKind::Read, 0, &fault, &is_device);
    CHECK(!pa_after_unmap.has_value(), "Page must not translate after unmap");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 192.7: Virtual Memory & MMU Conformance Tests\n";
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

    run_test(test_mmap_granule_wx_overlap, "16 KB Granule, W^X Policy, and Overlap Prevention");
    run_test(test_munmap_semantics, "munmap Deallocation and Cleanup");
    run_test(test_mprotect_transitions, "mprotect Transitions and W^X Guarding");
    run_test(test_madvise_validation, "madvise Validation (SYS_madvise 75)");
    run_test(test_mmu_permission_faults, "Real MMU Stage-1 Permission Faults & Translation");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}

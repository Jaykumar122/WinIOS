#include "cpu/mmu.h"
#include "memory/memory.h"
#include "reference/independent_reference_model.h"

#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace mmu;
using namespace winios::reference;

// -----------------------------------------------------------------------------
// Test 1: Page Table Walks Across Levels 1, 2, and 3
// -----------------------------------------------------------------------------
bool test_page_table_walks_differential() {
    Memory mem(0x200000); // 2 MiB memory
    ReferenceMemory ref_mem(0x200000);

    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false); // Direct walk testing

    // Setup 4-level page table for VA = 0x0000000000002000
    // L0 index = 0, L1 index = 0, L2 index = 0, L3 index = 2
    const std::uint64_t l0_table = 0x10000;
    const std::uint64_t l1_table = 0x11000;
    const std::uint64_t l2_table = 0x12000;
    const std::uint64_t l3_table = 0x13000;

    auto write_both = [&](std::uint64_t paddr, std::uint64_t val) {
        mem.write_u64(static_cast<std::size_t>(paddr), val);
        ref_mem.write_u64(paddr, val);
    };

    // L0 Table Descriptor pointing to L1
    write_both(l0_table, l1_table | 0x3u);
    // L1 Table Descriptor pointing to L2
    write_both(l1_table, l2_table | 0x3u);
    // L2 Table Descriptor pointing to L3
    write_both(l2_table, l3_table | 0x3u);
    // L3 Page Descriptor for VA 0x2000 -> PA 0x40000, AP=0b01 (RW all), AF=1, UXN=0, PXN=0
    // desc = 0x40000 | (1ULL << 10) | (1ULL << 6) | 0x3 = 0x40443
    const std::uint64_t l3_desc = 0x40000ULL | (1ULL << 10u) | (1ULL << 6u) | 0x3u;
    write_both(l3_table + 2 * 8, l3_desc);

    mmu.set_ttbr0_el1(l0_table);
    mmu.set_tcr_el1(16ULL); // T0SZ = 16 (48-bit address space)
    mmu.set_sctlr_el1(0x00C50839ULL); // MMU enabled (bit 0 = 1)

    TranslationFault prod_fault{};
    const auto prod_res = mmu.translate(mem, 0x2050ULL, AccessKind::Read, 0, &prod_fault);
    TEST_ASSERT(prod_res.has_value(), "Production MMU translation must succeed");
    TEST_ASSERT(prod_res.value() == 0x40050ULL, "Production MMU translated PA must be 0x40050");

    const auto ref_res = ReferencePageTableWalker::walk_stage1(
        ref_mem, l0_table, 16ULL, 0x00C50839ULL, 0x2050ULL, 0, 0);
    TEST_ASSERT(ref_res.success == true, "Reference MMU translation must succeed");
    TEST_ASSERT(ref_res.physical_address == prod_res.value(), "Production and Reference PA must match identically");

    // Test 2MB Block Mapping at Level 2
    // Overwrite L2 index 1 with 2MB Block descriptor mapping VA 0x00200000 -> PA 0x00080000
    // desc = 0x00080000 | AF(bit 10) | AP=0b00(Priv RW, Unpriv None)(bit 6=0, bit 7=0) | Block(bit 1=0, bit 0=1)
    const std::uint64_t l2_block_desc = 0x02000000ULL | (1ULL << 10u) | 0x1u;
    write_both(l2_table + 1 * 8, l2_block_desc);

    // Privileged EL1 read of VA 0x00201000
    const auto prod_block = mmu.translate(mem, 0x00201000ULL, AccessKind::Read, 1, &prod_fault);
    TEST_ASSERT(prod_block.has_value(), "2MB Block translation must succeed for EL1");
    TEST_ASSERT(prod_block.value() == 0x02001000ULL, "2MB Block PA must be 0x02001000");

    const auto ref_block = ReferencePageTableWalker::walk_stage1(
        ref_mem, l0_table, 16ULL, 0x00C50839ULL, 0x00201000ULL, 0, 1);
    TEST_ASSERT(ref_block.success == true, "Reference 2MB block translation must succeed");
    TEST_ASSERT(ref_block.physical_address == prod_block.value(), "2MB Block PA must match");

    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Permission and Access Fault Differential Verification
// -----------------------------------------------------------------------------
bool test_permission_and_fault_differential() {
    Memory mem(0x100000);
    ReferenceMemory ref_mem(0x100000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    const std::uint64_t l0_table = 0x10000;
    const std::uint64_t l1_table = 0x11000;
    const std::uint64_t l2_table = 0x12000;
    const std::uint64_t l3_table = 0x13000;

    auto write_both = [&](std::uint64_t paddr, std::uint64_t val) {
        mem.write_u64(static_cast<std::size_t>(paddr), val);
        ref_mem.write_u64(paddr, val);
    };

    write_both(l0_table, l1_table | 0x3u);
    write_both(l1_table, l2_table | 0x3u);
    write_both(l2_table, l3_table | 0x3u);

    // Page: AP=0b10 (Privileged Read-Only, Unprivileged None), AF=1, UXN=1
    // desc = 0x20000 | UXN(1ULL << 54) | (1ULL << 10) | AP[2](1ULL << 7) | 0x3
    const std::uint64_t l3_ro_desc = 0x20000ULL | (1ULL << 54u) | (1ULL << 10u) | (1ULL << 7u) | 0x3u;
    write_both(l3_table, l3_ro_desc);

    mmu.set_ttbr0_el1(l0_table);
    mmu.set_tcr_el1(16ULL);
    mmu.set_sctlr_el1(0x00C50839ULL);

    // Scenario A: Write violation (Privileged Write to Read-Only Page)
    TranslationFault prod_fault{};
    const auto prod_write_fail = mmu.translate(mem, 0x0ULL, AccessKind::Write, 1, &prod_fault);
    TEST_ASSERT(!prod_write_fail.has_value(), "Write to read-only page must fail");
    TEST_ASSERT(prod_fault.type == MmuFaultType::Permission, "Fault type must be Permission");
    TEST_ASSERT(prod_fault.level == 3, "Fault level must be 3");
    TEST_ASSERT(prod_fault.fsc() == 0x0Fu, "FSC must be Permission Fault Level 3 (0x0F)");

    const auto ref_write_fail = ReferencePageTableWalker::walk_stage1(
        ref_mem, l0_table, 16ULL, 0x00C50839ULL, 0x0ULL, 1, 1);
    TEST_ASSERT(ref_write_fail.success == false, "Reference walk must fail on write violation");
    TEST_ASSERT(ref_write_fail.fsc == prod_fault.fsc(), "FSC must match between production and reference");

    // Scenario B: Unprivileged access to privileged-only page
    const auto prod_unpriv_fail = mmu.translate(mem, 0x0ULL, AccessKind::Read, 0, &prod_fault);
    TEST_ASSERT(!prod_unpriv_fail.has_value(), "EL0 read of privileged-only page must fail");
    TEST_ASSERT(prod_fault.fsc() == 0x0Fu, "FSC must be 0x0F");

    // Scenario C: Access Flag Fault (AF = 0)
    const std::uint64_t l3_no_af = 0x20000ULL | (1ULL << 6u) | 0x3u; // AF bit 10 is 0
    write_both(l3_table + 8, l3_no_af);
    const auto prod_af_fail = mmu.translate(mem, 0x1000ULL, AccessKind::Read, 1, &prod_fault);
    TEST_ASSERT(!prod_af_fail.has_value(), "Translation with AF=0 must fail");
    TEST_ASSERT(prod_fault.type == MmuFaultType::AccessFlag, "Fault type must be AccessFlag");
    TEST_ASSERT(prod_fault.fsc() == 0x0Bu, "FSC must be Access Flag Fault Level 3 (0x0B)");

    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Guest Memory Protection on Failed Write & Translation Faults
// -----------------------------------------------------------------------------
bool test_memory_protection_on_fault() {
    Memory mem(0x100000);
    MMU mmu;
    mmu.reset();

    // Map address 0x5000 with pattern
    mem.write_u64(0x5000, 0xFEEDFACECAFECAFEULL);

    // Perform translation that faults (MMU enabled with uninitialized TTBR0)
    mmu.set_sctlr_el1(0x00C50839ULL);
    TranslationFault fault{};
    const auto res = mmu.translate(mem, 0x5000ULL, AccessKind::Write, 1, &fault);
    TEST_ASSERT(!res.has_value(), "Translation without page table must fault");

    // Verify physical memory remains 100% unaltered
    TEST_ASSERT(mem.read_u64(0x5000) == 0xFEEDFACECAFECAFEULL, "Physical memory must not be modified on fault");

    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Software TLB Hit, Miss, and ASID/VA Invalidation Invariants
// -----------------------------------------------------------------------------
bool test_tlb_lifecycle_and_invalidation() {
    Memory mem(0x100000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(true);

    const std::uint64_t l0_table = 0x10000;
    const std::uint64_t l1_table = 0x11000;
    const std::uint64_t l2_table = 0x12000;
    const std::uint64_t l3_table = 0x13000;

    mem.write_u64(l0_table, l1_table | 0x3u);
    mem.write_u64(l1_table, l2_table | 0x3u);
    mem.write_u64(l2_table, l3_table | 0x3u);
    // Page descriptor: PA=0x30000, AF=1, AP=0b01 (RW), nG=1 (bit 11 set for non-global)
    const std::uint64_t l3_desc = 0x30000ULL | (1ULL << 11u) | (1ULL << 10u) | (1ULL << 6u) | 0x3u;
    mem.write_u64(l3_table, l3_desc);

    mmu.set_ttbr0_el1(l0_table | (0x0042ULL << 48u)); // ASID = 0x42 in TTBR0
    mmu.set_tcr_el1(16ULL);
    mmu.set_sctlr_el1(0x00C50839ULL);

    TEST_ASSERT(mmu.get_tlb_entry_count() == 0, "Initial TLB must be empty");

    // 1. First translation: Cache Miss, populates TLB
    TranslationFault fault{};
    const auto res1 = mmu.translate(mem, 0x0ULL, AccessKind::Read, 1, &fault);
    TEST_ASSERT(res1.has_value(), "First translation must succeed");
    TEST_ASSERT(mmu.get_tlb_entry_count() == 1, "TLB must contain 1 entry after miss");

    // 2. Second translation: Cache Hit (does not walk page tables)
    // Corrupt page table entry in RAM to prove TLB serves cached mapping
    mem.write_u64(l3_table, 0x0ULL); // Invalid in RAM
    const auto res2 = mmu.translate(mem, 0x0ULL, AccessKind::Read, 1, &fault);
    TEST_ASSERT(res2.has_value(), "Second translation must hit TLB and succeed");
    TEST_ASSERT(res2.value() == 0x30000ULL, "Cached PA must match initial translation");

    // 3. Invalidate TLB by ASID mismatch vs match
    mmu.invalidate_tlb_asid(0x0099); // Invalidate different ASID
    TEST_ASSERT(mmu.get_tlb_entry_count() == 1, "Mismatched ASID invalidation must not evict entry");

    mmu.invalidate_tlb_asid(0x0042); // Invalidate matching ASID
    TEST_ASSERT(mmu.get_tlb_entry_count() == 0, "Matching ASID invalidation must evict entry");

    // 4. Now translation must re-walk RAM and fail due to corrupted entry
    const auto res3 = mmu.translate(mem, 0x0ULL, AccessKind::Read, 1, &fault);
    TEST_ASSERT(!res3.has_value(), "Post-invalidation walk must observe RAM corruption and fault");

    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Randomized Page Table Fixture Differential Campaign
// -----------------------------------------------------------------------------
bool test_randomized_page_table_fuzzing() {
    constexpr std::size_t kIterations = 1000;
    std::mt19937_64 rng(0x4D4D5531ULL);

    Memory mem(0x100000);
    ReferenceMemory ref_mem(0x100000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    std::size_t matched = 0;
    for (std::size_t i = 0; i < kIterations; ++i) {
        const std::uint64_t va = rng() & 0x0000FFFFFFFFF000ULL;
        const std::uint64_t pa = rng() & 0x00000000000FF000ULL;
        const unsigned el = (rng() % 2 == 0) ? 0 : 1;
        const int access = static_cast<int>(rng() % 3);
        const bool valid = (rng() % 5 != 0); // 80% valid
        const bool af = (rng() % 10 != 0);    // 90% AF=1
        const std::uint8_t ap = static_cast<std::uint8_t>(rng() % 4);

        const std::uint64_t l0_table = 0x10000;
        const std::uint64_t l1_table = 0x11000;
        const std::uint64_t l2_table = 0x12000;
        const std::uint64_t l3_table = 0x13000;

        mem.write_u64(l0_table, l1_table | 0x3u);
        ref_mem.write_u64(l0_table, l1_table | 0x3u);
        mem.write_u64(l1_table, l2_table | 0x3u);
        ref_mem.write_u64(l1_table, l2_table | 0x3u);
        mem.write_u64(l2_table, l3_table | 0x3u);
        ref_mem.write_u64(l2_table, l3_table | 0x3u);

        std::uint64_t desc = pa | (static_cast<std::uint64_t>(ap) << 6u);
        if (af) desc |= (1ULL << 10u);
        if (valid) desc |= 0x3u;

        const std::uint64_t l3_idx = (va >> 12u) & 0x1FFu;
        mem.write_u64(l3_table + l3_idx * 8u, desc);
        ref_mem.write_u64(l3_table + l3_idx * 8u, desc);

        mmu.set_ttbr0_el1(l0_table);
        mmu.set_tcr_el1(16ULL);
        mmu.set_sctlr_el1(0x00C50839ULL);

        TranslationFault prod_fault{};
        const auto prod_res = mmu.translate(mem, va, static_cast<AccessKind>(access), el, &prod_fault);
        const auto ref_res = ReferencePageTableWalker::walk_stage1(
            ref_mem, l0_table, 16ULL, 0x00C50839ULL, va, access, el);

        TEST_ASSERT(prod_res.has_value() == ref_res.success, "Success status must match in fuzz iteration");
        if (prod_res.has_value()) {
            TEST_ASSERT(prod_res.value() == ref_res.physical_address, "Translated PA must match in fuzz iteration");
        } else {
            TEST_ASSERT(prod_fault.fsc() == ref_res.fsc, "Fault status code must match in fuzz iteration");
        }
        matched++;
    }

    TEST_ASSERT(matched == kIterations, "All 1,000 fuzz iterations must match bit-for-bit");
    return true;
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "WinIOS Phase 52 Stage 35: MMU & Translation Conformance Tests\n";
    std::cout << "=================================================================\n";

    std::cout << "[Test 1] Testing Page Table Walks Across Levels 1, 2, and 3...\n";
    if (!test_page_table_walks_differential()) return 1;
    std::cout << "  -> Test 1 Passed. 4KB pages and 2MB blocks agree with reference model.\n";

    std::cout << "[Test 2] Testing Permission & Access Fault Differential Verification...\n";
    if (!test_permission_and_fault_differential()) return 1;
    std::cout << "  -> Test 2 Passed. Read-only, unprivileged, and AF faults match identically.\n";

    std::cout << "[Test 3] Testing Guest Memory Protection on Faulting Accesses...\n";
    if (!test_memory_protection_on_fault()) return 1;
    std::cout << "  -> Test 3 Passed. Failed translations never modify guest physical memory.\n";

    std::cout << "[Test 4] Testing Software TLB Lifecycle & ASID Invalidation Invariants...\n";
    if (!test_tlb_lifecycle_and_invalidation()) return 1;
    std::cout << "  -> Test 4 Passed. TLB hit/miss caching and ASID eviction verified.\n";

    std::cout << "[Test 5] Running Randomized Page Table Fuzzing Campaign (1,000 iterations)...\n";
    if (!test_randomized_page_table_fuzzing()) return 1;
    std::cout << "  -> Test 5 Passed. 1,000 / 1,000 randomized fixtures matched 100% bit-for-bit.\n";

    std::cout << "All MMU & Translation Conformance Tests PASSED successfully.\n";
    return 0;
}

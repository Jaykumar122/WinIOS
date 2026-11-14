// tests/unit/stage197_14_tlbi_page_reuse_tests.cpp
// Campaign 197: Acceptance Test K
// Verifies Cross-Core TLBI and Page Reuse Memory Safety:
// Validates that cross-core TLBI broadcasts, page unmapping, and page reuse
// prevent stale translation accesses and uphold ARMv8-A virtual memory invariants.

#include <iostream>
#include <vector>
#include <cstdint>
#include <memory>
#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "cpu/smp_cluster.h"
#include "cpu/synchronous_exception.h"
#include "memory/memory.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace mmu;

static void setup_mmu(CPU& cpu, std::uint64_t ttbr0_val) {
    MMU& mmu = cpu.get_mmu();
    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((2ULL << 14u) | 28ULL); // 16KB granule, 36-bit VA
    mmu.set_ttbr0_el1(ttbr0_val);
}

static bool test_cross_core_page_unmapping_and_fault() {
    std::cout << "  Testing cross-core page unmapping and translation fault enforcement..." << std::endl;
    SMPCluster cluster(2, 0x400000ULL); // 2 cores, 4MB RAM
    auto& mem = cluster.get_memory();

    const std::uint64_t l2_pa = 0x40000ULL;
    const std::uint64_t l3_pa = 0x48000ULL;
    const std::uint64_t pa_initial = 0x50000ULL;
    const std::uint16_t asid = 1;

    // Both cores share page tables at l2_pa and l3_pa
    mem.write_u64(static_cast<std::size_t>(l2_pa), l3_pa | 0x3ULL);
    // Initial L3 mapping: VA 0x1000 -> pa_initial (0x50000), nG=1, AF=1, user r/w
    const std::uint64_t desc_initial = pa_initial | (1ULL << 11u) | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL;
    mem.write_u64(static_cast<std::size_t>(l3_pa), desc_initial);

    // Write sentinel data at pa_initial
    mem.write_u64(static_cast<std::size_t>(pa_initial + 0x1000ULL), 0xCAFEBABEDEADBEEFULL);

    const std::uint64_t ttbr0 = (static_cast<std::uint64_t>(asid) << 48u) | l2_pa;
    setup_mmu(cluster.get_core(0), ttbr0);
    setup_mmu(cluster.get_core(1), ttbr0);

    // Core 1 translates VA 0x1000 and warms its TLB
    TranslationFault fault{};
    auto res_c1_initial = cluster.get_core(1).get_mmu().translate(mem, 0x1000ULL, AccessKind::Read, 1, &fault);
    TEST_ASSERT(res_c1_initial.has_value() && *res_c1_initial == pa_initial + 0x1000ULL,
                "Core 1 initially translates VA to pa_initial");
    TEST_ASSERT(cluster.get_core(1).get_mmu().get_tlb_entry_count() == 1,
                "Core 1 TLB cached translation");

    // Core 0 unmaps page by writing 0 to L3 descriptor (unmapped/invalid)
    mem.write_u64(static_cast<std::size_t>(l3_pa), 0ULL);

    // If Core 1 translates before TLBI, it hits its TLB and sees stale mapping
    auto res_c1_stale = cluster.get_core(1).get_mmu().translate(mem, 0x1000ULL, AccessKind::Read, 1, &fault);
    TEST_ASSERT(res_c1_stale.has_value() && *res_c1_stale == pa_initial + 0x1000ULL,
                "Before TLBI broadcast, Core 1 translation hits stale TLB entry");

    // Core 0 broadcasts TLBI VAE1IS to all other cores
    cluster.broadcast_tlbi_va(0, 0x1000ULL, asid, false);

    // Core 1's TLB entry must now be invalidated
    TEST_ASSERT(cluster.get_core(1).get_mmu().get_tlb_entry_count() == 0,
                "Cross-core broadcast_tlbi_va must invalidate Core 1 TLB entry");

    // Core 1 translates VA 0x1000 again: must MISS TLB, walk page table, and raise Translation Fault!
    TranslationFault fault_after{};
    auto res_c1_after = cluster.get_core(1).get_mmu().translate(mem, 0x1000ULL, AccessKind::Read, 1, &fault_after);
    TEST_ASSERT(!res_c1_after.has_value(),
                "After TLBI broadcast, Core 1 translation must fail with translation fault");
    TEST_ASSERT(fault_after.type == MmuFaultType::Translation,
                "Fault must be Translation Fault");

    return true;
}

static bool test_cross_core_page_remapping_and_reuse() {
    std::cout << "  Testing cross-core page remapping and reuse..." << std::endl;
    SMPCluster cluster(2, 0x400000ULL);
    auto& mem = cluster.get_memory();

    const std::uint64_t l2_pa = 0x40000ULL;
    const std::uint64_t l3_pa = 0x48000ULL;
    const std::uint64_t pa_old = 0x50000ULL;
    const std::uint64_t pa_new = 0x70000ULL;
    const std::uint16_t asid = 1;

    mem.write_u64(static_cast<std::size_t>(l2_pa), l3_pa | 0x3ULL);
    const std::uint64_t desc_old = pa_old | (1ULL << 11u) | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL;
    mem.write_u64(static_cast<std::size_t>(l3_pa), desc_old);

    mem.write_u64(static_cast<std::size_t>(pa_old + 0x1000ULL), 0x1111222233334444ULL);
    mem.write_u64(static_cast<std::size_t>(pa_new + 0x1000ULL), 0x9999888877776666ULL);

    const std::uint64_t ttbr0 = (static_cast<std::uint64_t>(asid) << 48u) | l2_pa;
    setup_mmu(cluster.get_core(0), ttbr0);
    setup_mmu(cluster.get_core(1), ttbr0);

    // Core 1 warms TLB
    TranslationFault fault{};
    auto res1 = cluster.get_core(1).get_mmu().translate(mem, 0x1000ULL, AccessKind::Read, 1, &fault);
    TEST_ASSERT(res1.has_value() && *res1 == pa_old + 0x1000ULL, "Core 1 translates to pa_old");

    // Core 0 updates page table to pa_new
    const std::uint64_t desc_new = pa_new | (1ULL << 11u) | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL;
    mem.write_u64(static_cast<std::size_t>(l3_pa), desc_new);

    // Core 0 broadcasts TLBI
    cluster.broadcast_tlbi_va(0, 0x1000ULL, asid, false);

    // Core 1 translates: must return pa_new
    auto res2 = cluster.get_core(1).get_mmu().translate(mem, 0x1000ULL, AccessKind::Read, 1, &fault);
    TEST_ASSERT(res2.has_value() && *res2 == pa_new + 0x1000ULL, "Post-TLBI translation resolves to pa_new");

    // Verify reading through new PA yields new data
    std::uint64_t val = mem.read_u64(static_cast<std::size_t>(*res2));
    TEST_ASSERT(val == 0x9999888877776666ULL, "Core 1 reads updated page contents from pa_new");

    return true;
}

static bool test_asid_isolated_invalidation() {
    std::cout << "  Testing ASID-isolated cross-core invalidation..." << std::endl;
    SMPCluster cluster(2, 0x400000ULL);
    auto& mem = cluster.get_memory();

    const std::uint64_t l2_pa = 0x40000ULL;
    const std::uint64_t l3_pa = 0x48000ULL;

    const std::uint64_t pa_0 = 0x50000ULL;
    const std::uint64_t pa_1 = 0x60000ULL;

    // L2 points to L3
    mem.write_u64(static_cast<std::size_t>(l2_pa), l3_pa | 0x3ULL);

    // Page 0 (VA 0x0000 - 0x3FFF): maps to pa_0, nG = 1, AF = 1, user r/w
    mem.write_u64(static_cast<std::size_t>(l3_pa + 0 * 8), pa_0 | (1ULL << 11u) | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL);
    // Page 1 (VA 0x4000 - 0x7FFF): maps to pa_1, nG = 1, AF = 1, user r/w
    mem.write_u64(static_cast<std::size_t>(l3_pa + 1 * 8), pa_1 | (1ULL << 11u) | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL);

    MMU& mmu1 = cluster.get_core(1).get_mmu();
    mmu1.set_sctlr_el1(0x00C50839ULL);
    mmu1.set_tcr_el1((2ULL << 14u) | 28ULL); // 16KB granule, 36-bit VA

    // Warm Core 1 TLB with ASID 1 for Page 0
    mmu1.set_ttbr0_el1((1ULL << 48u) | l2_pa);
    TranslationFault f1{};
    mmu1.translate(mem, 0x1000ULL, AccessKind::Read, 1, &f1);

    // Warm Core 1 TLB with ASID 2 for Page 1 (same base table, so base_changed is false, TLB not flushed)
    mmu1.set_ttbr0_el1((2ULL << 48u) | l2_pa);
    TranslationFault f2{};
    mmu1.translate(mem, 0x5000ULL, AccessKind::Read, 1, &f2);

    TEST_ASSERT(mmu1.get_tlb_entry_count() == 2,
                "Core 1 TLB has entries for both ASID 1 and ASID 2");

    // Core 0 broadcasts TLBI for ASID 1 only
    cluster.broadcast_tlbi_asid(0, 1);

    // Only ASID 1 should have been purged; ASID 2 must remain valid
    TEST_ASSERT(mmu1.get_tlb_entry_count() == 1,
                "Core 1 TLB retained ASID 2 entry while purging ASID 1");

    const auto entries = mmu1.get_tlb_entries_copy();
    TEST_ASSERT(entries[0].asid == 2, "Remaining TLB entry belongs to ASID 2");

    return true;
}

int main() {
    std::cout << "[STAGE 197.14] Running TLBI and Page Reuse Tests (Acceptance Test K)..." << std::endl;

    if (!test_cross_core_page_unmapping_and_fault()) {
        std::cerr << "test_cross_core_page_unmapping_and_fault FAILED" << std::endl;
        return 1;
    }

    if (!test_cross_core_page_remapping_and_reuse()) {
        std::cerr << "test_cross_core_page_remapping_and_reuse FAILED" << std::endl;
        return 1;
    }

    if (!test_asid_isolated_invalidation()) {
        std::cerr << "test_asid_isolated_invalidation FAILED" << std::endl;
        return 1;
    }

    std::cout << "[STAGE 197.14] All TLBI and Page Reuse Tests PASSED." << std::endl;
    return 0;
}

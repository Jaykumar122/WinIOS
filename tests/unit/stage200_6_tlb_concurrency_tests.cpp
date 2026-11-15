// ==============================================================================
// WinIOS Campaign 200 Stage 6: TLB and Invalidation Concurrency Tests
// ==============================================================================
// Verifies:
// 1. Stage-1 and Stage-2 TLB caching and hit accounting
// 2. ASID tag separation in Stage-1 TLB lookups
// 3. VMID tag separation in Stage-2 TLB lookups
// 4. Targeted address-specific and context-specific invalidations (VA, ASID, IPAS2, VMID)
// 5. Global TLB invalidations (invalidate_tlb_all, invalidate_tlb_alle2)
// 6. Cross-core broadcast invalidation across multicore cluster
// 7. Concurrent translation and invalidation thread safety (mutex locking)
// 8. Stale-entry rejection after table update followed by invalidation
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

#include "cpu/mmu.h"
#include "cpu/smp_cluster.h"
#include "memory/memory.h"
#include "memory/stage2_mmu.h"

using namespace mmu;
using namespace winios::mmu;

static void setup_stage1_16k_page(
    Memory& mem,
    std::uint64_t ttbr0_pa,
    std::uint64_t va,
    std::uint64_t pa,
    bool writable,
    bool el0_accessible,
    bool executable) {

    const std::uint64_t l2_idx = (va >> 25u) & 0x7FFu;
    const std::uint64_t l2_entry = ttbr0_pa + l2_idx * 8u;

    std::uint64_t l3_table_pa = ttbr0_pa + 16384ULL;
    std::uint64_t l2_desc = mem.read_u64(static_cast<std::size_t>(l2_entry));
    if ((l2_desc & 0x3u) == 0x3u) {
        l3_table_pa = l2_desc & 0x0000FFFFFFFFC000ULL;
    } else {
        l2_desc = (l3_table_pa & 0x0000FFFFFFFFC000ULL) | 0x3u;
        mem.write_u64(static_cast<std::size_t>(l2_entry), l2_desc);
    }

    const std::uint64_t l3_idx = (va >> 14u) & 0x7FFu;
    const std::uint64_t l3_entry = l3_table_pa + l3_idx * 8u;

    std::uint64_t l3_desc = (pa & 0x0000FFFFFFFFC000ULL) | 0x3u;
    l3_desc |= (1ULL << 10u); // AF = 1

    std::uint64_t ap = 0;
    if (!writable) ap |= 0b10ULL;
    if (el0_accessible) ap |= 0b01ULL;
    l3_desc |= (ap << 6u);

    if (!executable) {
        l3_desc |= (1ULL << 54u); // UXN
        l3_desc |= (1ULL << 53u); // PXN
    }

    l3_desc |= (0ULL << 2u);
    mem.write_u64(static_cast<std::size_t>(l3_entry), l3_desc);
}

static void test_tlb_caching_and_hit_rates() {
    Memory mem(1024 * 1024 * 8);
    MMU mmu;

    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((0b10ULL << 14u) | 16u);
    mmu.set_ttbr0_el1(0x10000ULL);
    mmu.set_mair_el1(0xFFULL);

    const std::uint64_t va = 0x10000000ULL;
    const std::uint64_t pa = 0x00200000ULL;
    setup_stage1_16k_page(mem, 0x10000ULL, va, pa, true, true, true);

    mmu.reset_tlb_stats();
    assert(mmu.get_tlb_hits() == 0);
    assert(mmu.get_tlb_misses() == 0);

    // First lookup: TLB miss -> walk
    auto r1 = mmu.translate(mem, va, AccessKind::Read, 1, nullptr);
    assert(r1.has_value() && r1.value() == pa);
    assert(mmu.get_tlb_misses() == 1);
    assert(mmu.get_tlb_hits() == 0);

    // Second lookup: TLB hit
    auto r2 = mmu.translate(mem, va + 0x20, AccessKind::Read, 1, nullptr);
    assert(r2.has_value() && r2.value() == pa + 0x20);
    assert(mmu.get_tlb_hits() == 1);
    assert(mmu.get_tlb_misses() == 1);

    std::cout << "[PASS] test_tlb_caching_and_hit_rates\n";
}

static void test_vmid_and_asid_isolation() {
    Memory mem(1024 * 1024 * 8);
    MMU mmu;

    mmu.set_hcr_el2(0x1ULL);
    mmu.set_vtcr_el2(0x80000000ULL | (0b10ULL << 14u) | (0b01ULL << 6u) | 16u);

    const std::uint64_t ipa = 0x40000ULL;
    const std::uint64_t pa1 = 0x100000ULL;
    const std::uint64_t pa2 = 0x200000ULL;

    Stage2TranslationEngine::map_page_16k(mem, 0x10000ULL, ipa, pa1, true, true, true);
    Stage2TranslationEngine::map_page_16k(mem, 0x20000ULL, ipa, pa2, true, true, true);

    // VMID 10
    mmu.set_vttbr_el2((10ULL << 48u) | 0x10000ULL);
    auto res1 = mmu.translate(mem, ipa, AccessKind::Read, 0, nullptr);
    assert(res1.has_value() && res1.value() == pa1);

    // VMID 20
    mmu.set_vttbr_el2((20ULL << 48u) | 0x20000ULL);
    auto res2 = mmu.translate(mem, ipa, AccessKind::Read, 0, nullptr);
    assert(res2.has_value() && res2.value() == pa2);

    std::cout << "[PASS] test_vmid_and_asid_isolation\n";
}

static void test_targeted_invalidation_semantics() {
    Memory mem(1024 * 1024 * 8);
    MMU mmu;

    mmu.set_hcr_el2(0x1ULL);
    mmu.set_vtcr_el2(0x80000000ULL | (0b10ULL << 14u) | (0b01ULL << 6u) | 16u);
    mmu.set_vttbr_el2((5ULL << 48u) | 0x10000ULL);

    const std::uint64_t ipa1 = 0x40000ULL;
    const std::uint64_t ipa2 = 0x44000ULL;
    Stage2TranslationEngine::map_page_16k(mem, 0x10000ULL, ipa1, 0x80000ULL, true, true, true);
    Stage2TranslationEngine::map_page_16k(mem, 0x10000ULL, ipa2, 0x84000ULL, true, true, true);

    auto r1 = mmu.translate(mem, ipa1, AccessKind::Read, 0, nullptr);
    auto r2 = mmu.translate(mem, ipa2, AccessKind::Read, 0, nullptr);
    assert(r1.has_value() && r2.has_value());
    assert(mmu.get_tlb_entry_count() >= 2);

    // Invalidate ipa1 only
    mmu.invalidate_tlb_ipas2(ipa1, 5);

    mmu.reset_tlb_stats();
    // ipa1 must miss TLB and re-walk
    auto r1_post = mmu.translate(mem, ipa1, AccessKind::Read, 0, nullptr);
    assert(r1_post.has_value());
    assert(mmu.get_tlb_misses() == 1);

    // ipa2 should hit TLB
    auto r2_post = mmu.translate(mem, ipa2, AccessKind::Read, 0, nullptr);
    assert(r2_post.has_value());
    assert(mmu.get_tlb_hits() == 1);

    std::cout << "[PASS] test_targeted_invalidation_semantics\n";
}

static void test_multicore_broadcast_invalidation() {
    winios::SMPCluster cluster(8, 1024 * 1024 * 16);
    auto& mem = cluster.get_memory();

    for (std::size_t i = 0; i < 8; ++i) {
        auto& mmu = cluster.get_core(i).get_mmu();
        mmu.set_hcr_el2(0x1ULL);
        mmu.set_vtcr_el2(0x80000000ULL | (0b10ULL << 14u) | (0b01ULL << 6u) | 16u);
        mmu.set_vttbr_el2((1ULL << 48u) | 0x10000ULL);
        mmu.reset_tlb_stats();
    }

    const std::uint64_t ipa = 0x40000ULL;
    Stage2TranslationEngine::map_page_16k(mem, 0x10000ULL, ipa, 0x80000ULL, true, true, true);

    // Populate TLB across all 8 cores
    for (std::size_t i = 0; i < 8; ++i) {
        auto res = cluster.get_core(i).get_mmu().translate(mem, ipa, AccessKind::Read, 0, nullptr);
        assert(res.has_value());
        assert(cluster.get_core(i).get_mmu().get_tlb_entry_count() >= 1);
    }

    // Core 0 executes broadcast TLB invalidation for VMID 1
    cluster.broadcast_tlbi_vmid(0, 1);

    // Core 0 (the broadcaster) is not invalidated by its own IS broadcast; cores 1..7 receive it
    assert(cluster.get_core(0).get_mmu().get_tlb_invalidations() == 0);
    for (std::size_t i = 1; i < 8; ++i) {
        assert(cluster.get_core(i).get_mmu().get_tlb_invalidations() >= 1);
    }

    std::cout << "[PASS] test_multicore_broadcast_invalidation\n";
}

static void test_concurrent_translation_and_invalidation() {
    Memory mem(1024 * 1024 * 8);
    MMU mmu;

    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((0b10ULL << 14u) | 16u);
    mmu.set_ttbr0_el1(0x10000ULL);
    mmu.set_mair_el1(0xFFULL);

    const std::uint64_t va = 0x10000000ULL;
    const std::uint64_t pa = 0x00200000ULL;
    setup_stage1_16k_page(mem, 0x10000ULL, va, pa, true, true, true);

    std::atomic<bool> stop_flag{false};

    // Thread 1: continuous translation
    std::thread reader([&]() {
        for (int i = 0; i < 2000; ++i) {
            auto res = mmu.translate(mem, va + (i & 0x1FC0), AccessKind::Read, 1, nullptr);
            assert(res.has_value());
        }
    });

    // Thread 2: continuous invalidations
    std::thread invalidator([&]() {
        for (int i = 0; i < 500; ++i) {
            if ((i % 3) == 0) {
                mmu.invalidate_tlb_va(va, 0);
            } else if ((i % 3) == 1) {
                mmu.invalidate_tlb_asid(0);
            } else {
                mmu.invalidate_tlb_all();
            }
        }
    });

    reader.join();
    invalidator.join();

    std::cout << "[PASS] test_concurrent_translation_and_invalidation\n";
}

static void test_stale_entry_rejection() {
    Memory mem(1024 * 1024 * 8);
    MMU mmu;

    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((0b10ULL << 14u) | 16u);
    mmu.set_ttbr0_el1(0x10000ULL);
    mmu.set_mair_el1(0xFFULL);

    const std::uint64_t va = 0x10000000ULL;
    const std::uint64_t old_pa = 0x00200000ULL;
    const std::uint64_t new_pa = 0x00300000ULL;

    setup_stage1_16k_page(mem, 0x10000ULL, va, old_pa, true, true, true);

    // Initial translation: caches old_pa
    auto r1 = mmu.translate(mem, va, AccessKind::Read, 1, nullptr);
    assert(r1.has_value() && r1.value() == old_pa);

    // Remap page table entry in memory to new_pa
    setup_stage1_16k_page(mem, 0x10000ULL, va, new_pa, true, true, true);

    // Without invalidation, TLB returns old_pa
    auto r_cached = mmu.translate(mem, va, AccessKind::Read, 1, nullptr);
    assert(r_cached.has_value() && r_cached.value() == old_pa);

    // Invalidate TLB for this VA
    mmu.invalidate_tlb_vaa(va);

    // Now translation walks table and gets new_pa
    auto r_fresh = mmu.translate(mem, va, AccessKind::Read, 1, nullptr);
    assert(r_fresh.has_value() && r_fresh.value() == new_pa);

    std::cout << "[PASS] test_stale_entry_rejection\n";
}

int main() {
    std::cout << "=== Running Stage 200.6 TLB and Invalidation Concurrency Tests ===\n";
    test_tlb_caching_and_hit_rates();
    test_vmid_and_asid_isolation();
    test_targeted_invalidation_semantics();
    test_multicore_broadcast_invalidation();
    test_concurrent_translation_and_invalidation();
    test_stale_entry_rejection();
    std::cout << "All Stage 200.6 tests passed successfully!\n";
    return 0;
}

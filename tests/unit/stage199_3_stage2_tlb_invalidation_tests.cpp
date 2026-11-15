#include <cassert>
#include <iostream>
#include <vector>

#include "cpu/mmu.h"
#include "cpu/smp_cluster.h"
#include "memory/memory.h"
#include "memory/stage2_mmu.h"

using namespace mmu;
using namespace winios::mmu;

void test_vmid_isolation() {
    Memory mem(1024 * 1024 * 8);
    MMU mmu;

    // Enable Stage 2 only (EL0/1, Stage 1 disabled to test raw Stage 2 caching)
    mmu.set_hcr_el2(0x1ULL);
    mmu.set_vtcr_el2(0x80000000ULL | (0b10ULL << 14u) | (0b01ULL << 6u) | 16u);

    const std::uint64_t ipa = 0x40000ULL;
    const std::uint64_t pa_vmid1 = 0x80000ULL;
    const std::uint64_t pa_vmid2 = 0xC0000ULL;

    // Table for VMID 1 at 0x10000
    Stage2TranslationEngine::map_page_16k(mem, 0x10000ULL, ipa, pa_vmid1, true, true, true);
    // Table for VMID 2 at 0x20000
    Stage2TranslationEngine::map_page_16k(mem, 0x20000ULL, ipa, pa_vmid2, true, true, true);

    // Run VMID 1
    mmu.set_vttbr_el2((1ULL << 48u) | 0x10000ULL);
    auto res1 = mmu.translate(mem, ipa, AccessKind::Read, 0, nullptr);
    assert(res1.has_value());
    assert(res1.value() == pa_vmid1);

    // Repeat read with VMID 1 hits TLB
    auto res1_hit = mmu.translate(mem, ipa, AccessKind::Read, 0, nullptr);
    assert(res1_hit.has_value());
    assert(res1_hit.value() == pa_vmid1);
    assert(mmu.get_tlb_hits() >= 1);

    // Switch to VMID 2
    mmu.set_vttbr_el2((2ULL << 48u) | 0x20000ULL);
    auto res2 = mmu.translate(mem, ipa, AccessKind::Read, 0, nullptr);
    assert(res2.has_value());
    // Must resolve to pa_vmid2, NOT the cached pa_vmid1!
    assert(res2.value() == pa_vmid2);

    std::cout << "[PASS] test_vmid_isolation" << std::endl;
}

void test_stage2_targeted_invalidation() {
    Memory mem(1024 * 1024 * 8);
    MMU mmu;

    mmu.set_hcr_el2(0x1ULL);
    mmu.set_vtcr_el2(0x80000000ULL | (0b10ULL << 14u) | (0b01ULL << 6u) | 16u);
    mmu.set_vttbr_el2((5ULL << 48u) | 0x10000ULL);

    const std::uint64_t ipa1 = 0x40000ULL;
    const std::uint64_t ipa2 = 0x44000ULL;
    Stage2TranslationEngine::map_page_16k(mem, 0x10000ULL, ipa1, 0x80000ULL, true, true, true);
    Stage2TranslationEngine::map_page_16k(mem, 0x10000ULL, ipa2, 0x84000ULL, true, true, true);

    // Populate TLB
    auto r1 = mmu.translate(mem, ipa1, AccessKind::Read, 0, nullptr);
    auto r2 = mmu.translate(mem, ipa2, AccessKind::Read, 0, nullptr);
    assert(r1.has_value() && r2.has_value());
    assert(mmu.get_tlb_entry_count() >= 2);

    // Targeted invalidate ipa1 only
    mmu.invalidate_tlb_ipas2(ipa1, 5);

    // Re-mapping ipa1 to a new physical address
    Stage2TranslationEngine::map_page_16k(mem, 0x10000ULL, ipa1, 0x90000ULL, true, true, true);

    // ipa1 must re-walk and return new PA
    auto r1_new = mmu.translate(mem, ipa1, AccessKind::Read, 0, nullptr);
    assert(r1_new.has_value());
    assert(r1_new.value() == 0x90000ULL);

    // ipa2 was not invalidated; remains cached
    std::uint64_t hits_before = mmu.get_tlb_hits();
    auto r2_hit = mmu.translate(mem, ipa2, AccessKind::Read, 0, nullptr);
    assert(r2_hit.has_value() && r2_hit.value() == 0x84000ULL);
    assert(mmu.get_tlb_hits() == hits_before + 1);
    (void)hits_before;

    std::cout << "[PASS] test_stage2_targeted_invalidation" << std::endl;
}

void test_multicore_stage2_broadcast() {
    winios::SMPCluster cluster(8, 1024 * 1024 * 16);

    for (std::size_t i = 0; i < 8; ++i) {
        cluster.get_core(i).get_mmu().set_hcr_el2(0x1ULL);
        cluster.get_core(i).get_mmu().set_vtcr_el2(0x80000000ULL | (0b10ULL << 14u) | (0b01ULL << 6u) | 16u);
        cluster.get_core(i).get_mmu().set_vttbr_el2((7ULL << 48u) | 0x10000ULL);
        cluster.get_core(i).get_mmu().reset_tlb_stats();
        assert(cluster.get_core(i).get_mmu().get_tlb_invalidations() == 0);
    }

    // Core 0 broadcasts Stage-2 IPAS2 invalidation
    cluster.broadcast_tlbi_ipas2(0, 0x40000ULL, 7);

    // Core 0 did not invalidate itself via remote broadcast
    assert(cluster.get_core(0).get_mmu().get_tlb_invalidations() == 0);
    // Remote cores 1..7 received broadcast
    for (std::size_t i = 1; i < 8; ++i) {
        assert(cluster.get_core(i).get_mmu().get_tlb_invalidations() == 1);
    }

    // Core 3 broadcasts VMID invalidation
    cluster.broadcast_tlbi_vmid(3, 7);
    assert(cluster.get_core(3).get_mmu().get_tlb_invalidations() == 1); // from earlier broadcast
    for (std::size_t i = 0; i < 8; ++i) {
        if (i != 3) {
            // received broadcast
            assert(cluster.get_core(i).get_mmu().get_tlb_invalidations() >= 1);
        }
    }

    std::cout << "[PASS] test_multicore_stage2_broadcast" << std::endl;
}

int main() {
    std::cout << "--- Stage 199.3 Stage-2 TLB and Invalidation Tests ---" << std::endl;
    test_vmid_isolation();
    test_stage2_targeted_invalidation();
    test_multicore_stage2_broadcast();
    std::cout << "All Stage 199.3 tests PASSED!" << std::endl;
    return 0;
}

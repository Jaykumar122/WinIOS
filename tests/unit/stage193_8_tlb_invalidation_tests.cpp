// ============================================================================
// Stage 193.8: TLB and Translation Invalidation Tests
// ============================================================================

#include <cstdint>
#include <iostream>

#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "memory/memory.h"
#include "cpu/synchronous_exception.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace mmu;

static bool test_tlb_caching_and_stale_translation_behavior() {
    CPU cpu;
    Memory mem(0x400000ULL); // 4MB

    MMU& mmu = cpu.get_mmu();
    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((2ULL << 14u) | 28ULL); // 16KB granule, 36-bit VA

    const std::uint64_t l2_pa = 0x40000ULL;
    const std::uint64_t l3_pa = 0x48000ULL;
    const std::uint64_t initial_pa = 0x50000ULL;
    const std::uint64_t updated_pa = 0x70000ULL;

    const std::uint16_t asid = 1;
    mmu.set_ttbr0_el1((static_cast<std::uint64_t>(asid) << 48u) | l2_pa);
    mem.write_u64(static_cast<std::size_t>(l2_pa), l3_pa | 0x3ULL);

    // Initial L3 mapping: VA 0x0000 -> initial_pa (0x50000), nG = 1 (bit 11), AF = 1 (bit 10), user r/w
    const std::uint64_t desc_initial = initial_pa | (1ULL << 11u) | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL;
    mem.write_u64(static_cast<std::size_t>(l3_pa), desc_initial);

    // Initial translation warms the TLB
    TranslationFault fault{};
    auto res1 = mmu.translate(mem, 0x1000ULL, AccessKind::Read, 1, &fault);
    CHECK(res1.has_value() && *res1 == initial_pa + 0x1000ULL, "Initial translation must resolve to initial_pa");
    CHECK(mmu.get_tlb_entry_count() == 1, "TLB must have 1 cached entry");

    // Modify physical page table entry without TLB invalidation
    const std::uint64_t desc_updated = updated_pa | (1ULL << 11u) | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL;
    mem.write_u64(static_cast<std::size_t>(l3_pa), desc_updated);

    // Subsequent translation should HIT the TLB and return the STALE address (proving TLB caching)
    const auto hits_before = mmu.get_tlb_hits();
    auto res2 = mmu.translate(mem, 0x1000ULL, AccessKind::Read, 1, &fault);
    CHECK(res2.has_value() && *res2 == initial_pa + 0x1000ULL, "Uninvalidated read must return stale cached mapping");
    CHECK(mmu.get_tlb_hits() > hits_before, "Translation must have hit TLB");

    // Perform architectural TLBI VAE1 for VA 0x0000 and ASID 1
    mmu.invalidate_tlb_va(0x1000ULL, asid, false);
    CHECK(mmu.get_tlb_entry_count() == 0, "TLBI VAE1 must invalidate matching entry");

    // Subsequent translation should MISS TLB, walk page table, and return updated_pa
    auto res3 = mmu.translate(mem, 0x1000ULL, AccessKind::Read, 1, &fault);
    CHECK(res3.has_value() && *res3 == updated_pa + 0x1000ULL, "Post-TLBI translation must resolve to updated_pa");
    CHECK(mmu.get_tlb_entry_count() == 1, "TLB must cache new translation");

    std::cout << "[PASS] test_tlb_caching_and_stale_translation_behavior\n";
    return true;
}

static bool test_address_specific_invalidation() {
    CPU cpu;
    Memory mem(0x400000ULL);

    MMU& mmu = cpu.get_mmu();
    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((2ULL << 14u) | 28ULL);

    const std::uint64_t l2_pa = 0x40000ULL;
    const std::uint64_t l3_pa = 0x48000ULL;
    const std::uint16_t asid = 1;
    mmu.set_ttbr0_el1((static_cast<std::uint64_t>(asid) << 48u) | l2_pa);
    mem.write_u64(static_cast<std::size_t>(l2_pa), l3_pa | 0x3ULL);

    // Page 0 (VA 0x0000 - 0x3FFF): maps to 0x50000
    // Page 1 (VA 0x4000 - 0x7FFF): maps to 0x60000
    const std::uint64_t pa0 = 0x50000ULL;
    const std::uint64_t pa1 = 0x60000ULL;
    mem.write_u64(static_cast<std::size_t>(l3_pa + 0 * 8), pa0 | (1ULL << 11u) | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL);
    mem.write_u64(static_cast<std::size_t>(l3_pa + 1 * 8), pa1 | (1ULL << 11u) | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL);

    // Warm TLB for both pages
    TranslationFault fault{};
    mmu.translate(mem, 0x1000ULL, AccessKind::Read, 1, &fault);
    mmu.translate(mem, 0x5000ULL, AccessKind::Read, 1, &fault);
    CHECK(mmu.get_tlb_entry_count() == 2, "TLB must have 2 entries");

    // Invalidate Page 0 ONLY via TLBI VAE1
    mmu.invalidate_tlb_va(0x1000ULL, asid, false);
    CHECK(mmu.get_tlb_entry_count() == 1, "TLB must have exactly 1 entry remaining");

    // Verify remaining entry is Page 1 (VA 0x4000)
    const auto entries = mmu.get_tlb_entries_copy();
    CHECK((entries[0].virtual_page << 12u) == 0x4000ULL, "Remaining entry must be Page 1");

    std::cout << "[PASS] test_address_specific_invalidation\n";
    return true;
}

static bool test_asid_aware_invalidation() {
    CPU cpu;
    Memory mem(0x400000ULL);

    MMU& mmu = cpu.get_mmu();
    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((2ULL << 14u) | 28ULL);

    const std::uint64_t l2_pa = 0x40000ULL;
    const std::uint64_t l3_pa = 0x48000ULL;
    mem.write_u64(static_cast<std::size_t>(l2_pa), l3_pa | 0x3ULL);

    // Map Page 0 (0x0000) and Page 1 (0x4000) as non-global (nG = 1)
    mem.write_u64(static_cast<std::size_t>(l3_pa + 0 * 8), 0x50000ULL | (1ULL << 11u) | (1ULL << 10u) | 0x3ULL);
    mem.write_u64(static_cast<std::size_t>(l3_pa + 1 * 8), 0x60000ULL | (1ULL << 11u) | (1ULL << 10u) | 0x3ULL);

    // Warm TLB under ASID 1 for Page 0
    mmu.set_ttbr0_el1((1ULL << 48u) | l2_pa);
    TranslationFault fault{};
    mmu.translate(mem, 0x1000ULL, AccessKind::Read, 1, &fault);

    // Warm TLB under ASID 2 for Page 1 (same base table, so base_changed is false, TLB not flushed)
    mmu.set_ttbr0_el1((2ULL << 48u) | l2_pa);
    mmu.translate(mem, 0x5000ULL, AccessKind::Read, 1, &fault);
    CHECK(mmu.get_tlb_entry_count() == 2, "TLB must contain entries for both ASID 1 and ASID 2");

    // Invalidate ASID 1 only using TLBI ASIDE1
    mmu.invalidate_tlb_asid(1);
    CHECK(mmu.get_tlb_entry_count() == 1, "TLB must have 1 entry remaining after ASID 1 invalidation");

    const auto entries = mmu.get_tlb_entries_copy();
    CHECK(entries[0].asid == 2, "Remaining entry must belong to ASID 2");

    std::cout << "[PASS] test_asid_aware_invalidation\n";
    return true;
}

static bool test_global_vs_nonglobal_invalidation() {
    CPU cpu;
    Memory mem(0x400000ULL);

    MMU& mmu = cpu.get_mmu();
    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((2ULL << 14u) | 28ULL);

    const std::uint64_t l2_pa = 0x40000ULL;
    const std::uint64_t l3_pa = 0x48000ULL;
    mem.write_u64(static_cast<std::size_t>(l2_pa), l3_pa | 0x3ULL);

    // Entry 0: Global page (nG = 0)
    mem.write_u64(static_cast<std::size_t>(l3_pa + 0 * 8), 0x50000ULL | (1ULL << 10u) | 0x3ULL);
    // Entry 1: Non-global page (nG = 1)
    mem.write_u64(static_cast<std::size_t>(l3_pa + 1 * 8), 0x60000ULL | (1ULL << 11u) | (1ULL << 10u) | 0x3ULL);

    mmu.set_ttbr0_el1((1ULL << 48u) | l2_pa); // ASID 1
    TranslationFault fault{};
    mmu.translate(mem, 0x1000ULL, AccessKind::Read, 1, &fault);
    mmu.translate(mem, 0x5000ULL, AccessKind::Read, 1, &fault);
    CHECK(mmu.get_tlb_entry_count() == 2, "TLB must have 2 entries");

    // TLBI ASIDE1 with ASID 1 should NOT invalidate global entry
    mmu.invalidate_tlb_asid(1);
    CHECK(mmu.get_tlb_entry_count() == 1, "TLBI ASIDE1 must not invalidate global entries");
    const auto entries = mmu.get_tlb_entries_copy();
    CHECK(entries[0].non_global == false, "Remaining entry must be the global page");

    // TLBI VAAE1 (All ASIDs) for VA 0x0000 invalidates global entry as well
    mmu.invalidate_tlb_vaa(0x1000ULL, false);
    CHECK(mmu.get_tlb_entry_count() == 0, "TLBI VAAE1 must invalidate global entry");

    std::cout << "[PASS] test_global_vs_nonglobal_invalidation\n";
    return true;
}

static bool test_guest_execution_of_tlbi_and_el0_privilege_enforcement() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x400000ULL);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    MMU& mmu = cpu.get_mmu();
    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((2ULL << 14u) | 28ULL);

    const std::uint64_t l2_pa = 0x40000ULL;
    const std::uint64_t l3_pa = 0x48000ULL;
    const std::uint64_t code_pa = 0x30000ULL;
    mem.write_u64(static_cast<std::size_t>(l2_pa), l3_pa | 0x3ULL);

    // Map Code page at VA 0x0000 (PA 0x30000) with user read/write/exec (AP=01, AF=1)
    mem.write_u64(static_cast<std::size_t>(l3_pa + 0 * 8), code_pa | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL);
    // Map Data page at VA 0x4000 (PA 0x50000)
    mem.write_u64(static_cast<std::size_t>(l3_pa + 1 * 8), 0x50000ULL | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL);

    mmu.set_ttbr0_el1((1ULL << 48u) | l2_pa);

    // Warm TLB with Data page
    TranslationFault fault{};
    mmu.translate(mem, 0x5000ULL, AccessKind::Read, 1, &fault);
    CHECK(mmu.get_tlb_entry_count() >= 1, "TLB must have at least 1 entry");

    // 1. Guest EL1 executes TLBI VMALLE1 (0xD508871Fu)
    const std::uint64_t el1_code_va = 0x1000ULL;
    mem.write_u32(static_cast<std::size_t>(code_pa + 0x1000), 0xD508871Fu); // TLBI VMALLE1

    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(el1_code_va);

    cpu.step(mem);

    CHECK(cpu.get_current_el() == 1, "Must remain at EL1");
    CHECK(cpu.get_program_counter() == el1_code_va + 4u, "PC must increment past TLBI");
    CHECK(mmu.get_tlb_entry_count() == 0, "TLBI VMALLE1 must have cleared all TLB entries");

    // 2. Guest EL0 attempts to execute TLBI VMALLE1 (0xD508871Fu) -> must trap with EC 0x00 (Undefined)
    const std::uint64_t el0_code_va = 0x1004ULL;
    mem.write_u32(static_cast<std::size_t>(code_pa + 0x1004), 0xD508871Fu); // TLBI VMALLE1

    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_program_counter(el0_code_va);

    cpu.step(mem);

    CHECK(cpu.get_current_el() == 1, "Must trap to EL1");
    CHECK(cpu.get_program_counter() == vbar + 0x400ULL, "PC must jump to Lower EL Sync vector");
    CHECK(cpu.get_elr_el1() == el0_code_va, "ELR_EL1 must point to trapped TLBI instruction");

    const auto esr = cpu.get_esr_el1();
    const auto ec = static_cast<std::uint32_t>((esr >> 26u) & 0x3Fu);
    CHECK(ec == 0x00u, "ESR EC must be 0x00 (Unknown / Undefined instruction trap)");

    std::cout << "[PASS] test_guest_execution_of_tlbi_and_el0_privilege_enforcement\n";
    return true;
}

int main() {
    try {
        std::cout << "=== Running Stage 193.8: TLB and Translation Invalidation Tests ===\n";
        if (!test_tlb_caching_and_stale_translation_behavior()) return 1;
        if (!test_address_specific_invalidation()) return 1;
        if (!test_asid_aware_invalidation()) return 1;
        if (!test_global_vs_nonglobal_invalidation()) return 1;
        if (!test_guest_execution_of_tlbi_and_el0_privilege_enforcement()) return 1;
        std::cout << "=== All Stage 193.8 Tests Passed (5/5) ===\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION CAUGHT: " << ex.what() << "\n";
        return 1;
    }
}

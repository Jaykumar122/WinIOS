// ============================================================================
// Stage 193.9: ASID and Address-Space Isolation Tests
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

static bool test_asid_representation_and_context_selection() {
    CPU cpu;
    Memory mem(0x400000ULL);

    MMU& mmu = cpu.get_mmu();
    mmu.set_sctlr_el1(0x00C50839ULL);

    // TCR_EL1: TG0=16KB (2 << 14), T0SZ=28 (36-bit VA), TG1=16KB (1 << 30), T1SZ=28 (28 << 16)
    // A1 bit (bit 22) selects ASID source:
    // If A1 == 0 -> ASID taken from TTBR0_EL1[63:48]
    // If A1 == 1 -> ASID taken from TTBR1_EL1[63:48]
    const std::uint64_t tcr_a0 = (1ULL << 30u) | (28ULL << 16u) | (2ULL << 14u) | 28ULL;
    mmu.set_tcr_el1(tcr_a0);

    const std::uint16_t asid0 = 0x0042;
    const std::uint16_t asid1 = 0x0099;
    const std::uint64_t l2_pa = 0x40000ULL;
    const std::uint64_t l3_pa = 0x48000ULL;

    mmu.set_ttbr0_el1((static_cast<std::uint64_t>(asid0) << 48u) | l2_pa);
    mmu.set_ttbr1_el1((static_cast<std::uint64_t>(asid1) << 48u) | l2_pa);
    mem.write_u64(static_cast<std::size_t>(l2_pa), l3_pa | 0x3ULL);

    // Map non-global page (nG = 1)
    mem.write_u64(static_cast<std::size_t>(l3_pa), 0x50000ULL | (1ULL << 11u) | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL);

    // 1. With A1 == 0, active ASID must be asid0 (0x0042)
    TranslationFault fault{};
    mmu.translate(mem, 0x1000ULL, AccessKind::Read, 1, &fault);
    auto entries = mmu.get_tlb_entries_copy();
    CHECK(!entries.empty(), "TLB must have an entry");
    CHECK(entries[0].asid == asid0, "Cached TLB entry ASID must match TTBR0 ASID when TCR.A1 == 0");

    mmu.invalidate_tlb_all();

    // 2. Switch TCR.A1 to 1 (bit 22)
    const std::uint64_t tcr_a1 = tcr_a0 | (1ULL << 22u);
    mmu.set_tcr_el1(tcr_a1);

    mmu.translate(mem, 0x1000ULL, AccessKind::Read, 1, &fault);
    entries = mmu.get_tlb_entries_copy();
    CHECK(!entries.empty(), "TLB must have an entry");
    CHECK(entries[0].asid == asid1, "Cached TLB entry ASID must match TTBR1 ASID when TCR.A1 == 1");

    std::cout << "[PASS] test_asid_representation_and_context_selection\n";
    return true;
}

static bool test_two_guest_contexts_conflicting_virtual_addresses() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x400000ULL); // 4MB

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    MMU& mmu = cpu.get_mmu();
    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((2ULL << 14u) | 28ULL);

    // Process A: ASID = 1
    // Base table: L2 at 0x40000, L3 at 0x44000
    // Virtual Layout:
    // - Code VA 0x1000 (PA 0x30000): LDR X0, [X1] (0xF9400020u)
    // - Data VA 0x5000 (PA 0x50000): contains 0x1111222233334444ULL
    const std::uint64_t l2_pa_A = 0x40000ULL;
    const std::uint64_t l3_pa_A = 0x44000ULL;
    const std::uint64_t code_pa_A = 0x30000ULL;
    const std::uint64_t data_pa_A = 0x50000ULL;
    constexpr std::uint64_t kDataA = 0x1111222233334444ULL;

    mem.write_u64(static_cast<std::size_t>(l2_pa_A), l3_pa_A | 0x3ULL);
    // Page 0 (VA 0x0000 - 0x3FFF): Code page
    mem.write_u64(static_cast<std::size_t>(l3_pa_A + 0 * 8), code_pa_A | (1ULL << 11u) | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL);
    // Page 1 (VA 0x4000 - 0x7FFF): Data page
    mem.write_u64(static_cast<std::size_t>(l3_pa_A + 1 * 8), data_pa_A | (1ULL << 11u) | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL);
    // Write instructions and data
    mem.write_u32(static_cast<std::size_t>(code_pa_A + 0x1000), 0xF9400020u); // LDR X0, [X1]
    mem.write_u64(static_cast<std::size_t>(data_pa_A + 0x1000), kDataA);

    // Process B: ASID = 2
    // Base table: L2 at 0x48000, L3 at 0x4C000
    // Virtual Layout (deliberately identical virtual addresses!):
    // - Code VA 0x1000 (PA 0x34000): LDR X0, [X1] (0xF9400020u)
    // - Data VA 0x5000 (PA 0x70000): contains 0x88889999AAAABBBBULL
    const std::uint64_t l2_pa_B = 0x48000ULL;
    const std::uint64_t l3_pa_B = 0x4C000ULL;
    const std::uint64_t code_pa_B = 0x34000ULL;
    const std::uint64_t data_pa_B = 0x70000ULL;
    constexpr std::uint64_t kDataB = 0x88889999AAAABBBBULL;

    mem.write_u64(static_cast<std::size_t>(l2_pa_B), l3_pa_B | 0x3ULL);
    mem.write_u64(static_cast<std::size_t>(l3_pa_B + 0 * 8), code_pa_B | (1ULL << 11u) | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL);
    mem.write_u64(static_cast<std::size_t>(l3_pa_B + 1 * 8), data_pa_B | (1ULL << 11u) | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL);
    mem.write_u32(static_cast<std::size_t>(code_pa_B + 0x1000), 0xF9400020u); // LDR X0, [X1]
    mem.write_u64(static_cast<std::size_t>(data_pa_B + 0x1000), kDataB);

    const std::uint64_t user_code_va = 0x1000ULL;
    const std::uint64_t user_data_va = 0x5000ULL;

    // --- PHASE 1: Execute under Process A (ASID 1) ---
    mmu.set_ttbr0_el1((1ULL << 48u) | l2_pa_A);
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_program_counter(user_code_va);
    cpu.write_register(1, user_data_va);
    cpu.write_register(0, 0);

    cpu.step(mem);

    CHECK(cpu.get_program_counter() == user_code_va + 4u, "Process A PC must advance past LDR");
    CHECK(cpu.read_register(0) == kDataA, "Process A must load kDataA (0x1111222233334444)");

    // --- PHASE 2: Context Switch to Process B (ASID 2) ---
    mmu.set_ttbr0_el1((2ULL << 48u) | l2_pa_B);
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_program_counter(user_code_va);
    cpu.write_register(1, user_data_va);
    cpu.write_register(0, 0);

    cpu.step(mem);

    CHECK(cpu.get_program_counter() == user_code_va + 4u, "Process B PC must advance past LDR");
    CHECK(cpu.read_register(0) == kDataB, "Process B must load kDataB (0x88889999AAAABBBB)");

    // --- PHASE 3: Context Switch Back to Process A (ASID 1) ---
    mmu.set_ttbr0_el1((1ULL << 48u) | l2_pa_A);
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_program_counter(user_code_va);
    cpu.write_register(1, user_data_va);
    cpu.write_register(0, 0);

    cpu.step(mem);

    CHECK(cpu.get_program_counter() == user_code_va + 4u, "Process A PC must advance again");
    CHECK(cpu.read_register(0) == kDataA, "Process A must reload kDataA, demonstrating isolation");

    std::cout << "[PASS] test_two_guest_contexts_conflicting_virtual_addresses\n";
    return true;
}

static bool test_tlb_asid_coexistence_and_tagging() {
    CPU cpu;
    Memory mem(0x400000ULL);

    MMU& mmu = cpu.get_mmu();
    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((2ULL << 14u) | 28ULL);

    const std::uint64_t l2_pa = 0x40000ULL;
    const std::uint64_t l3_pa = 0x48000ULL;
    mem.write_u64(static_cast<std::size_t>(l2_pa), l3_pa | 0x3ULL);

    // In shared table:
    // Page 0 (VA 0x0000): Non-global (nG = 1) -> PA 0x50000
    // Page 1 (VA 0x4000): Non-global (nG = 1) -> PA 0x60000
    mem.write_u64(static_cast<std::size_t>(l3_pa + 0 * 8), 0x50000ULL | (1ULL << 11u) | (1ULL << 10u) | 0x3ULL);
    mem.write_u64(static_cast<std::size_t>(l3_pa + 1 * 8), 0x60000ULL | (1ULL << 11u) | (1ULL << 10u) | 0x3ULL);

    // Set ASID 1 (base is l2_pa) and translate Page 0
    mmu.set_ttbr0_el1((1ULL << 48u) | l2_pa);
    TranslationFault fault{};
    auto res_a = mmu.translate(mem, 0x1000ULL, AccessKind::Read, 1, &fault);
    CHECK(res_a.has_value() && *res_a == 0x51000ULL, "ASID 1 translate Page 0 must succeed");

    // Switch ASID to 2 (base is identical, so base_changed is false, TLB not cleared)
    mmu.set_ttbr0_el1((2ULL << 48u) | l2_pa);
    auto res_b = mmu.translate(mem, 0x5000ULL, AccessKind::Read, 1, &fault);
    CHECK(res_b.has_value() && *res_b == 0x61000ULL, "ASID 2 translate Page 1 must succeed");

    // Verify TLB entries coexist
    const auto entries = mmu.get_tlb_entries_copy();
    CHECK(entries.size() == 2, "TLB must hold entries for both ASID 1 and ASID 2 simultaneously");

    bool has_asid1 = false;
    bool has_asid2 = false;
    for (const auto& e : entries) {
        if (e.asid == 1 && (e.virtual_page << 12u) == 0x0000ULL) has_asid1 = true;
        if (e.asid == 2 && (e.virtual_page << 12u) == 0x4000ULL) has_asid2 = true;
    }
    CHECK(has_asid1 && has_asid2, "Both ASID 1 and ASID 2 entries must be correctly tagged in TLB");

    // Invalidate only ASID 1
    mmu.invalidate_tlb_asid(1);
    CHECK(mmu.get_tlb_entry_count() == 1, "Only ASID 1 entry should be invalidated");
    const auto remaining = mmu.get_tlb_entries_copy();
    CHECK(remaining[0].asid == 2, "Remaining entry must belong to ASID 2");

    std::cout << "[PASS] test_tlb_asid_coexistence_and_tagging\n";
    return true;
}

int main() {
    try {
        std::cout << "=== Running Stage 193.9: ASID and Address-Space Isolation Tests ===\n";
        if (!test_asid_representation_and_context_selection()) return 1;
        if (!test_two_guest_contexts_conflicting_virtual_addresses()) return 1;
        if (!test_tlb_asid_coexistence_and_tagging()) return 1;
        std::cout << "=== All Stage 193.9 Tests Passed (3/3) ===\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION CAUGHT: " << ex.what() << "\n";
        return 1;
    }
}

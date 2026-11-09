#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "cpu/smp_cluster.h"
#include "memory/memory.h"
#include <iostream>
#include <cassert>
#include <vector>

#define STAGE61_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" << #cond << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

namespace {

// Helper to set up a valid 4KB page translation hierarchy
void setup_4kb_page(
    Memory& mem,
    mmu::MMU& mmu,
    std::uint64_t va,
    std::uint64_t pa,
    std::uint16_t asid,
    bool non_global,
    bool read_only,
    bool el0_accessible,
    bool uxn,
    bool pxn)
{
    const std::uint64_t l0_table = 0x1000ULL;
    const std::uint64_t l1_table = 0x2000ULL;
    const std::uint64_t l2_table = 0x3000ULL;
    const std::uint64_t l3_table = 0x4000ULL;

    if (mmu.get_sctlr_el1() != 0x00C50839ULL) {
        mmu.set_sctlr_el1(0x00C50839ULL); // MMU enabled
    }
    if (mmu.get_tcr_el1() != 0x19ULL) {
        mmu.set_tcr_el1(0x19ULL); // 4KB granule, 39-bit VA space
    }
    mmu.set_ttbr0_el1(l0_table | (static_cast<std::uint64_t>(asid) << 48u));

    const std::uint64_t l0_idx = (va >> 39u) & 0x1FFu;
    const std::uint64_t l1_idx = (va >> 30u) & 0x1FFu;
    const std::uint64_t l2_idx = (va >> 21u) & 0x1FFu;
    const std::uint64_t l3_idx = (va >> 12u) & 0x1FFu;

    mem.write_u64(static_cast<std::size_t>(l0_table + l0_idx * 8u), l1_table | 0x3ULL);
    mem.write_u64(static_cast<std::size_t>(l1_table + l1_idx * 8u), l2_table | 0x3ULL);
    mem.write_u64(static_cast<std::size_t>(l2_table + l2_idx * 8u), l3_table | 0x3ULL);

    std::uint64_t l3_desc = (pa & 0x0000FFFFFFFFF000ULL) | 0x3ULL;
    l3_desc |= (1ULL << 10u); // AF = 1
    if (non_global) l3_desc |= (1ULL << 11u); // nG = 1
    std::uint64_t ap_bits = 0;
    if (read_only) ap_bits |= 0x2u;
    if (el0_accessible) ap_bits |= 0x1u;
    l3_desc |= (ap_bits << 6u);
    if (uxn) l3_desc |= (1ULL << 54u);
    if (pxn) l3_desc |= (1ULL << 53u);

    mem.write_u64(static_cast<std::size_t>(l3_table + l3_idx * 8u), l3_desc);
}

// Helper to set up a 16KB page translation
void setup_16kb_page(
    Memory& mem,
    mmu::MMU& mmu,
    std::uint64_t va,
    std::uint64_t pa,
    std::uint16_t asid,
    bool non_global)
{
    const std::uint64_t l2_table = 0x10000ULL; // aligned to 16KB
    const std::uint64_t l3_table = 0x14000ULL;

    mmu.set_sctlr_el1(0x00C50839ULL);
    // TCR_EL1: TG0=16KB (0b10 << 14), T0SZ=16
    mmu.set_tcr_el1((2ULL << 14u) | 16ULL);
    mmu.set_ttbr0_el1(l2_table | (static_cast<std::uint64_t>(asid) << 48u));

    const std::uint64_t l2_idx = (va >> 25u) & 0x7FFu;
    const std::uint64_t l3_idx = (va >> 14u) & 0x7FFu;

    mem.write_u64(static_cast<std::size_t>(l2_table + l2_idx * 8u), l3_table | 0x3ULL);

    std::uint64_t l3_desc = (pa & 0x0000FFFFFFFFC000ULL) | (1ULL << 6u) | 0x3ULL;
    l3_desc |= (1ULL << 10u); // AF = 1
    if (non_global) l3_desc |= (1ULL << 11u);
    mem.write_u64(static_cast<std::size_t>(l3_table + l3_idx * 8u), l3_desc);
}

// Helper to set up a 64KB page translation
void setup_64kb_page(
    Memory& mem,
    mmu::MMU& mmu,
    std::uint64_t va,
    std::uint64_t pa,
    std::uint16_t asid,
    bool non_global)
{
    const std::uint64_t l2_table = 0x20000ULL; // aligned to 64KB
    const std::uint64_t l3_table = 0x30000ULL;

    mmu.set_sctlr_el1(0x00C50839ULL);
    // TCR_EL1: TG0=64KB (0b01 << 14), T0SZ=16
    mmu.set_tcr_el1((1ULL << 14u) | 16ULL);
    mmu.set_ttbr0_el1(l2_table | (static_cast<std::uint64_t>(asid) << 48u));

    const std::uint64_t l2_idx = (va >> 29u) & 0x1FFFu;
    const std::uint64_t l3_idx = (va >> 16u) & 0x1FFFu;

    mem.write_u64(static_cast<std::size_t>(l2_table + l2_idx * 8u), l3_table | 0x3ULL);

    std::uint64_t l3_desc = (pa & 0x0000FFFFFFFF0000ULL) | (1ULL << 6u) | 0x3ULL;
    l3_desc |= (1ULL << 10u); // AF = 1
    if (non_global) l3_desc |= (1ULL << 11u);
    mem.write_u64(static_cast<std::size_t>(l3_table + l3_idx * 8u), l3_desc);
}

} // namespace

// -----------------------------------------------------------------------------
// Test 1: Multi-Granule TLB Caching & Hit Verification
// -----------------------------------------------------------------------------
bool test_multigranule_tlb_caching() {
    std::cout << "[1] test_multigranule_tlb_caching...\n";
    Memory mem(0x200000);
    mmu::MMU mmu;

    // 4KB granule
    setup_4kb_page(mem, mmu, 0x1000ULL, 0x50000ULL, 0x0001u, false, false, false, false, false);
    mmu.reset_tlb_stats();
    STAGE61_ASSERT(mmu.get_tlb_entry_count() == 0, "TLB must be empty initially");

    mmu::TranslationFault fault{};
    auto p1 = mmu.translate(mem, 0x1020ULL, mmu::AccessKind::Read, 1, &fault);
    STAGE61_ASSERT(p1.has_value() && p1.value() == 0x50020ULL, "4KB first translation mismatch");
    STAGE61_ASSERT(mmu.get_tlb_entry_count() == 1, "4KB entry must be inserted into TLB");
    STAGE61_ASSERT(mmu.get_tlb_misses() == 1, "First translation must be a miss");
    STAGE61_ASSERT(mmu.get_tlb_hits() == 0, "First translation cannot be a hit");

    auto p2 = mmu.translate(mem, 0x1040ULL, mmu::AccessKind::Read, 1, &fault);
    STAGE61_ASSERT(p2.has_value() && p2.value() == 0x50040ULL, "4KB second translation mismatch");
    STAGE61_ASSERT(mmu.get_tlb_hits() == 1, "Second translation must hit TLB");
    STAGE61_ASSERT(mmu.get_tlb_misses() == 1, "Miss count must not change on hit");

    // 16KB granule
    setup_16kb_page(mem, mmu, 0x8000ULL, 0x60000ULL, 0x0002u, false);
    mmu.reset_tlb_stats();
    auto p3 = mmu.translate(mem, 0x8100ULL, mmu::AccessKind::Read, 1, &fault);
    STAGE61_ASSERT(p3.has_value() && p3.value() == 0x60100ULL, "16KB first translation mismatch");
    STAGE61_ASSERT(mmu.get_tlb_misses() == 1, "16KB first translation must miss");

    auto p4 = mmu.translate(mem, 0x8200ULL, mmu::AccessKind::Read, 1, &fault);
    STAGE61_ASSERT(p4.has_value() && p4.value() == 0x60200ULL, "16KB second translation mismatch");
    STAGE61_ASSERT(mmu.get_tlb_hits() == 1, "16KB second translation must hit");

    // 64KB granule
    setup_64kb_page(mem, mmu, 0x20000ULL, 0x80000ULL, 0x0003u, false);
    mmu.reset_tlb_stats();
    auto p5 = mmu.translate(mem, 0x20500ULL, mmu::AccessKind::Read, 1, &fault);
    STAGE61_ASSERT(p5.has_value() && p5.value() == 0x80500ULL, "64KB first translation mismatch");
    STAGE61_ASSERT(mmu.get_tlb_misses() == 1, "64KB first translation must miss");

    auto p6 = mmu.translate(mem, 0x20600ULL, mmu::AccessKind::Read, 1, &fault);
    STAGE61_ASSERT(p6.has_value() && p6.value() == 0x80600ULL, "64KB second translation mismatch");
    STAGE61_ASSERT(mmu.get_tlb_hits() == 1, "64KB second translation must hit");

    return true;
}

// -----------------------------------------------------------------------------
// Test 2: RAM Table Corruption Proves TLB Serving
// -----------------------------------------------------------------------------
bool test_ram_corruption_proves_tlb_hit() {
    std::cout << "[2] test_ram_corruption_proves_tlb_hit...\n";
    Memory mem(0x100000);
    mmu::MMU mmu;

    setup_4kb_page(mem, mmu, 0x2000ULL, 0x70000ULL, 0x0010u, false, false, false, false, false);
    mmu::TranslationFault fault{};

    // Initial translation to populate TLB
    auto res1 = mmu.translate(mem, 0x2000ULL, mmu::AccessKind::Read, 1, &fault);
    STAGE61_ASSERT(res1.has_value() && res1.value() == 0x70000ULL, "Initial translation failed");

    // Corrupt Level 3 descriptor in RAM (set to 0 = invalid)
    const std::uint64_t l3_table = 0x4000ULL;
    const std::uint64_t l3_idx = (0x2000ULL >> 12u) & 0x1FFu;
    mem.write_u64(static_cast<std::size_t>(l3_table + l3_idx * 8u), 0ULL);

    // Translation should still succeed via cached TLB entry
    auto res2 = mmu.translate(mem, 0x2008ULL, mmu::AccessKind::Read, 1, &fault);
    STAGE61_ASSERT(res2.has_value() && res2.value() == 0x70008ULL, "TLB must serve cached mapping despite corrupted RAM descriptor");

    // Invalidate TLB
    mmu.invalidate_tlb_all();

    // Now translation must re-walk page tables and fail
    auto res3 = mmu.translate(mem, 0x2008ULL, mmu::AccessKind::Read, 1, &fault);
    STAGE61_ASSERT(!res3.has_value(), "Post-invalidation walk must fail due to corrupted RAM descriptor");
    STAGE61_ASSERT(fault.type == mmu::MmuFaultType::Translation, "Expected Translation fault");

    return true;
}

// -----------------------------------------------------------------------------
// Test 3: ASID Matching & Isolation (nG = 1)
// -----------------------------------------------------------------------------
bool test_asid_matching_and_isolation() {
    std::cout << "[3] test_asid_matching_and_isolation...\n";
    Memory mem(0x100000);
    mmu::MMU mmu;

    // Non-global entry with ASID 0x0042
    setup_4kb_page(mem, mmu, 0x5000ULL, 0x30000ULL, 0x0042u, true, false, false, false, false);
    mmu::TranslationFault fault{};

    auto res1 = mmu.translate(mem, 0x5000ULL, mmu::AccessKind::Read, 1, &fault);
    STAGE61_ASSERT(res1.has_value() && res1.value() == 0x30000ULL, "Initial ASID 0x0042 translation failed");
    STAGE61_ASSERT(mmu.get_tlb_entry_count() == 1, "TLB should have 1 entry");

    // Repopulate and verify entry in TLB has non_global=true and asid=0x0042
    auto entries = mmu.get_tlb_entries_copy();
    STAGE61_ASSERT(entries.size() == 1, "Must have 1 TLB entry");
    STAGE61_ASSERT(entries[0].non_global == true, "Entry must be non-global");
    STAGE61_ASSERT(entries[0].asid == 0x0042u, "Entry ASID must be 0x0042");

    return true;
}

// -----------------------------------------------------------------------------
// Test 4: TCR_EL1.A1 Selection (TTBR0 vs TTBR1 ASID)
// -----------------------------------------------------------------------------
bool test_tcr_a1_asid_selection() {
    std::cout << "[4] test_tcr_a1_asid_selection...\n";
    Memory mem(0x100000);
    mmu::MMU mmu;

    const std::uint64_t l0_table = 0x1000ULL;
    setup_4kb_page(mem, mmu, 0x5000ULL, 0x30000ULL, 0x0011u, true, false, false, false, false);

    // TTBR0 ASID = 0x0011, TTBR1 ASID = 0x0022
    mmu.set_ttbr0_el1(l0_table | (0x0011ULL << 48u));
    mmu.set_ttbr1_el1(l0_table | (0x0022ULL << 48u));

    // When TCR.A1 == 0 (bit 22 = 0): ASID defined by TTBR0
    std::uint64_t tcr = mmu.get_tcr_el1() & ~(1ULL << 22u);
    mmu.set_tcr_el1(tcr);

    mmu::TranslationFault fault{};
    auto res1 = mmu.translate(mem, 0x5000ULL, mmu::AccessKind::Read, 1, &fault);
    STAGE61_ASSERT(res1.has_value(), "Translation with TCR.A1=0 must use TTBR0 ASID 0x0011");

    auto entries = mmu.get_tlb_entries_copy();
    STAGE61_ASSERT(!entries.empty() && entries[0].asid == 0x0011u, "TLB entry must have ASID 0x0011");

    // When TCR.A1 == 1 (bit 22 = 1): ASID defined by TTBR1
    tcr |= (1ULL << 22u);
    mmu.set_tcr_el1(tcr); // invalidates TLB

    auto res2 = mmu.translate(mem, 0x5000ULL, mmu::AccessKind::Read, 1, &fault);
    STAGE61_ASSERT(res2.has_value(), "Translation with TCR.A1=1 must succeed");
    entries = mmu.get_tlb_entries_copy();
    STAGE61_ASSERT(!entries.empty() && entries[0].asid == 0x0022u, "TLB entry must have TTBR1 ASID 0x0022");

    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Global Entries (nG = 0) Match Any ASID
// -----------------------------------------------------------------------------
bool test_global_entries_match_any_asid() {
    std::cout << "[5] test_global_entries_match_any_asid...\n";
    Memory mem(0x100000);
    mmu::MMU mmu;

    // Global entry (non_global = false)
    setup_4kb_page(mem, mmu, 0x8000ULL, 0x40000ULL, 0x0010u, false, false, false, false, false);
    mmu::TranslationFault fault{};

    auto res1 = mmu.translate(mem, 0x8000ULL, mmu::AccessKind::Read, 1, &fault);
    STAGE61_ASSERT(res1.has_value(), "Initial translation failed");

    auto entries = mmu.get_tlb_entries_copy();
    STAGE61_ASSERT(entries.size() == 1, "Must have 1 TLB entry");
    STAGE61_ASSERT(entries[0].non_global == false, "Entry must be global");

    // Corrupt RAM descriptor
    const std::uint64_t l3_table = 0x4000ULL;
    const std::uint64_t l3_idx = (0x8000ULL >> 12u) & 0x1FFu;
    mem.write_u64(static_cast<std::size_t>(l3_table + l3_idx * 8u), 0ULL);

    // Subsequent access must hit TLB regardless of global status
    auto res2 = mmu.translate(mem, 0x8000ULL, mmu::AccessKind::Read, 1, &fault);
    STAGE61_ASSERT(res2.has_value() && res2.value() == 0x40000ULL, "Global entry must hit TLB");

    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Targeted Invalidation - TLBI ASIDE1
// -----------------------------------------------------------------------------
bool test_tlbi_asid_invalidation() {
    std::cout << "[6] test_tlbi_asid_invalidation...\n";
    Memory mem(0x100000);
    mmu::MMU mmu;

    // Set up non-global page 1 (ASID 0x0010)
    setup_4kb_page(mem, mmu, 0x1000ULL, 0x10000ULL, 0x0010u, true, false, false, false, false);
    mmu::TranslationFault fault{};
    mmu.translate(mem, 0x1000ULL, mmu::AccessKind::Read, 1, &fault);

    // Set up non-global page 2 (ASID 0x0020)
    setup_4kb_page(mem, mmu, 0x2000ULL, 0x20000ULL, 0x0020u, true, false, false, false, false);
    mmu.translate(mem, 0x2000ULL, mmu::AccessKind::Read, 1, &fault);

    // Set up global page 3
    setup_4kb_page(mem, mmu, 0x3000ULL, 0x30000ULL, 0x0030u, false, false, false, false, false);
    mmu.translate(mem, 0x3000ULL, mmu::AccessKind::Read, 1, &fault);

    STAGE61_ASSERT(mmu.get_tlb_entry_count() == 3, "Expected 3 entries in TLB");

    // Invalidate ASID 0x0010
    mmu.invalidate_tlb_asid(0x0010u);
    STAGE61_ASSERT(mmu.get_tlb_entry_count() == 2, "ASID 0x0010 must be evicted, 2 entries remaining");

    auto entries = mmu.get_tlb_entries_copy();
    for (const auto& e : entries) {
        if (e.non_global) {
            STAGE61_ASSERT(e.asid == 0x0020u, "Remaining non-global entry must have ASID 0x0020");
        } else {
            STAGE61_ASSERT(e.virtual_page == (0x3000ULL >> 12u), "Global entry must remain");
        }
    }

    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Targeted Invalidation - TLBI VAE1 & TLBI VAAE1
// -----------------------------------------------------------------------------
bool test_tlbi_va_and_vaa_invalidation() {
    std::cout << "[7] test_tlbi_va_and_vaa_invalidation...\n";
    Memory mem(0x100000);
    mmu::MMU mmu;

    // VA 0x1000 (ASID 0x0010, non-global)
    setup_4kb_page(mem, mmu, 0x1000ULL, 0x10000ULL, 0x0010u, true, false, false, false, false);
    mmu::TranslationFault fault{};
    mmu.translate(mem, 0x1000ULL, mmu::AccessKind::Read, 1, &fault);

    // VA 0x2000 (ASID 0x0010, non-global)
    setup_4kb_page(mem, mmu, 0x2000ULL, 0x20000ULL, 0x0010u, true, false, false, false, false);
    mmu.translate(mem, 0x2000ULL, mmu::AccessKind::Read, 1, &fault);

    STAGE61_ASSERT(mmu.get_tlb_entry_count() == 2, "Expected 2 entries in TLB");

    // Invalidate VA 0x1000 for ASID 0x0010
    mmu.invalidate_tlb_va(0x1000ULL, 0x0010u, false);
    STAGE61_ASSERT(mmu.get_tlb_entry_count() == 1, "VA 0x1000 must be evicted");

    auto entries = mmu.get_tlb_entries_copy();
    STAGE61_ASSERT(entries[0].virtual_page == (0x2000ULL >> 12u), "Remaining entry must be VA 0x2000");

    // Invalidate VA 0x2000 via VAA (all ASIDs)
    mmu.invalidate_tlb_vaa(0x2000ULL, false);
    STAGE61_ASSERT(mmu.get_tlb_entry_count() == 0, "VA 0x2000 must be evicted by VAA");

    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Targeted Invalidation - TLBI VMALLE1 (All Stage 1)
// -----------------------------------------------------------------------------
bool test_tlbi_vmalle1_all() {
    std::cout << "[8] test_tlbi_vmalle1_all...\n";
    Memory mem(0x100000);
    mmu::MMU mmu;

    for (std::uint64_t i = 1; i <= 5; ++i) {
        setup_4kb_page(mem, mmu, i * 0x1000ULL, i * 0x10000ULL, static_cast<std::uint16_t>(i), true, false, false, false, false);
        mmu::TranslationFault fault{};
        mmu.translate(mem, i * 0x1000ULL, mmu::AccessKind::Read, 1, &fault);
    }
    STAGE61_ASSERT(mmu.get_tlb_entry_count() == 5, "Expected 5 entries");

    mmu.invalidate_tlb_all();
    STAGE61_ASSERT(mmu.get_tlb_entry_count() == 0, "TLB must be empty after VMALLE1");
    STAGE61_ASSERT(mmu.get_tlb_invalidations() >= 1, "Invalidation counter must increment");

    return true;
}

// -----------------------------------------------------------------------------
// Test 9: Permission & Fault Consistency (TLB Hit vs TLB Miss)
// -----------------------------------------------------------------------------
bool test_permission_fault_consistency() {
    std::cout << "[9] test_permission_fault_consistency...\n";
    Memory mem(0x100000);
    mmu::MMU mmu;

    // Read-only page, EL0 accessible = false, UXN = true, PXN = true
    setup_4kb_page(mem, mmu, 0x9000ULL, 0x40000ULL, 0x0001u, false, true, false, true, true);

    // Miss: write access should raise Permission fault
    mmu::TranslationFault miss_fault{};
    auto res_miss = mmu.translate(mem, 0x9000ULL, mmu::AccessKind::Write, 1, &miss_fault);
    STAGE61_ASSERT(!res_miss.has_value(), "Write to RO page must fail on miss");
    STAGE61_ASSERT(miss_fault.type == mmu::MmuFaultType::Permission, "Fault must be Permission");
    STAGE61_ASSERT(miss_fault.level == 3, "Fault level must be 3");
    STAGE61_ASSERT(miss_fault.is_write == true, "Fault is_write must be true");

    // Populate TLB by successful Read access
    mmu::TranslationFault read_fault{};
    auto res_read = mmu.translate(mem, 0x9000ULL, mmu::AccessKind::Read, 1, &read_fault);
    STAGE61_ASSERT(res_read.has_value(), "Read to RO page must succeed");
    STAGE61_ASSERT(mmu.get_tlb_entry_count() == 1, "TLB must contain 1 entry");

    // Hit: write access should raise identical Permission fault
    mmu::TranslationFault hit_fault{};
    auto res_hit = mmu.translate(mem, 0x9000ULL, mmu::AccessKind::Write, 1, &hit_fault);
    STAGE61_ASSERT(!res_hit.has_value(), "Write to RO page must fail on hit");
    STAGE61_ASSERT(hit_fault.type == miss_fault.type, "Hit fault type must match miss fault type");
    STAGE61_ASSERT(hit_fault.level == miss_fault.level, "Hit fault level must match miss fault level");
    STAGE61_ASSERT(hit_fault.is_write == miss_fault.is_write, "Hit is_write must match miss is_write");
    STAGE61_ASSERT(hit_fault.virtual_address == miss_fault.virtual_address, "Hit VA must match miss VA");

    // Hit: execute access on PXN page at EL1 should raise Permission fault
    mmu::TranslationFault exec_fault{};
    auto res_exec = mmu.translate(mem, 0x9000ULL, mmu::AccessKind::Execute, 1, &exec_fault);
    STAGE61_ASSERT(!res_exec.has_value(), "Execute on PXN page must fail on hit");
    STAGE61_ASSERT(exec_fault.type == mmu::MmuFaultType::Permission, "Exec fault must be Permission");
    STAGE61_ASSERT(exec_fault.is_instruction == true, "Exec fault is_instruction must be true");

    return true;
}

// -----------------------------------------------------------------------------
// Test 10: SMP Cluster TLBI Broadcast Determinism
// -----------------------------------------------------------------------------
bool test_smp_tlbi_broadcast() {
    std::cout << "[10] test_smp_tlbi_broadcast...\n";
    winios::SMPCluster cluster(2, 0x200000);
    auto& mem = cluster.get_memory();
    auto& core0 = cluster.get_core(0);
    auto& core1 = cluster.get_core(1);

    // Set up translation on Core 0 and Core 1
    setup_4kb_page(mem, core0.get_mmu(), 0x1000ULL, 0x50000ULL, 0x0010u, true, false, false, false, false);
    setup_4kb_page(mem, core1.get_mmu(), 0x1000ULL, 0x50000ULL, 0x0010u, true, false, false, false, false);

    mmu::TranslationFault fault{};
    core0.get_mmu().translate(mem, 0x1000ULL, mmu::AccessKind::Read, 1, &fault);
    core1.get_mmu().translate(mem, 0x1000ULL, mmu::AccessKind::Read, 1, &fault);

    STAGE61_ASSERT(core0.get_mmu().get_tlb_entry_count() == 1, "Core 0 TLB must have 1 entry");
    STAGE61_ASSERT(core1.get_mmu().get_tlb_entry_count() == 1, "Core 1 TLB must have 1 entry");

    // Core 0 issues broadcast TLBI by ASID
    cluster.broadcast_tlbi_asid(0, 0x0010u);

    // Core 1's entry for ASID 0x0010 must be invalidated!
    STAGE61_ASSERT(core1.get_mmu().get_tlb_entry_count() == 0, "Core 1 TLB must be invalidated by broadcast");

    // Repopulate and test broadcast_tlbi_all
    core0.get_mmu().translate(mem, 0x1000ULL, mmu::AccessKind::Read, 1, &fault);
    core1.get_mmu().translate(mem, 0x1000ULL, mmu::AccessKind::Read, 1, &fault);
    STAGE61_ASSERT(core1.get_mmu().get_tlb_entry_count() == 1, "Core 1 TLB repopulated");

    cluster.broadcast_tlbi_all(0);
    STAGE61_ASSERT(core1.get_mmu().get_tlb_entry_count() == 0, "Core 1 TLB must be cleared by broadcast_tlbi_all");

    return true;
}

int main() {
    std::cout << "=== Running Stage 61 TLB Architecture Tests ===" << std::endl;

    if (!test_multigranule_tlb_caching()) return 1;
    if (!test_ram_corruption_proves_tlb_hit()) return 2;
    if (!test_asid_matching_and_isolation()) return 3;
    if (!test_tcr_a1_asid_selection()) return 4;
    if (!test_global_entries_match_any_asid()) return 5;
    if (!test_tlbi_asid_invalidation()) return 6;
    if (!test_tlbi_va_and_vaa_invalidation()) return 7;
    if (!test_tlbi_vmalle1_all()) return 8;
    if (!test_permission_fault_consistency()) return 9;
    if (!test_smp_tlbi_broadcast()) return 10;

    std::cout << "stage61_tlb_tests: all 10 test suites passed successfully" << std::endl;
    return 0;
}

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

#include "cpu/mmu.h"
#include "memory/memory.h"

#define MMU_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace mmu;

namespace {

// Helper: Configure standard Apple Silicon 36-bit kernel virtual space in TCR_EL1:
// TG1 = 0b01 (16 KB granule, bits 31:30)
// TG0 = 0b10 (16 KB granule, bits 15:14)
// T1SZ = 28 (36-bit VA, bits 21:16)
// T0SZ = 28 (36-bit VA, bits 5:0)
constexpr std::uint64_t kAppleSiliconTcr16Kb =
    (1ULL << 30u) |  // TG1 = 0b01 (16 KB)
    (2ULL << 14u) |  // TG0 = 0b10 (16 KB)
    (28ULL << 16u) | // T1SZ = 28 (64 - 28 = 36-bit VA)
    (28ULL << 0u);   // T0SZ = 28

// Canonical high-half kernel base address for XNU
constexpr std::uint64_t kXnuKernelBase = 0xFFFFFFF007004000ULL;

// -----------------------------------------------------------------------------
// Suite 1: Apple Silicon TTBR1 16 KB Granule Baseline Translation
// -----------------------------------------------------------------------------
bool test_apple_silicon_ttbr1_16kb_paging_baseline() {
    std::cout << "[1] test_apple_silicon_ttbr1_16kb_paging_baseline...\n";

    Memory mem(0x200000); // 2 MB RAM
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    // 16 KB tables must be aligned on 0x4000 boundaries
    const std::uint64_t l2_table = 0x10000;
    const std::uint64_t l3_table = 0x14000;

    // For kXnuKernelBase (0xFFFFFFF007004000):
    // Bits [35:25] = 0x07004000 >> 25 = 3
    const std::uint64_t l2_idx = (kXnuKernelBase >> 25u) & 0x7FFu;
    MMU_ASSERT(l2_idx == 3, "L2 index is 3");

    // L2 Table entry 3 points to L3 Table (Table descriptor: bits [1:0] = 0b11)
    mem.write_u64(l2_table + l2_idx * 8u, l3_table | 0x3u);

    // Bits [24:14] = (0x07004000 >> 14) & 0x7FF = 0x401 = 1025
    const std::uint64_t l3_idx = (kXnuKernelBase >> 14u) & 0x7FFu;
    MMU_ASSERT(l3_idx == 1025, "L3 index is 1025");

    // L3 Page: PA = 0x80000, AF=1, AP=0b01 (Read/Write), Page descriptor: bits [1:0] = 0b11
    const std::uint64_t target_pa = 0x80000ULL;
    const std::uint64_t l3_desc = target_pa | (1ULL << 10u) | (1ULL << 6u) | 0x3u;
    mem.write_u64(l3_table + l3_idx * 8u, l3_desc);

    mmu.set_ttbr1_el1(l2_table);
    mmu.set_tcr_el1(kAppleSiliconTcr16Kb);
    mmu.set_sctlr_el1(0x00C50839ULL); // MMU enabled

    TranslationFault fault{};
    auto res = mmu.translate(mem, kXnuKernelBase, AccessKind::Read, 1, &fault);
    MMU_ASSERT(res.has_value(), "High-half kernel VA translates successfully");

    // Page offset is 0x4000 & 0x3FFF = 0x0000
    MMU_ASSERT(res.value() == target_pa, "Translated PA matches target physical frame");

    std::cout << "[PASS] Apple Silicon TTBR1 16 KB Granule Baseline Translation\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: KASLR Slide Page-Displacement Translation
// -----------------------------------------------------------------------------
bool test_kaslr_slide_page_displacement() {
    std::cout << "[2] test_kaslr_slide_page_displacement...\n";

    Memory mem(0x400000); // 4 MB RAM
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    const std::uint64_t l2_table = 0x20000;
    const std::uint64_t l3_table = 0x24000;

    // Apply KASLR slide of +256 KB (16 pages of 16 KB)
    const std::uint64_t kaslr_slide = 0x00040000ULL;
    const std::uint64_t slided_va = kXnuKernelBase + kaslr_slide;

    // L2 index remains 3
    const std::uint64_t l2_idx = (slided_va >> 25u) & 0x7FFu;
    MMU_ASSERT(l2_idx == 3, "L2 index unchanged for 256KB slide");
    mem.write_u64(l2_table + l2_idx * 8u, l3_table | 0x3u);

    // L3 index shifts by 16 pages: 1025 + 16 = 1041
    const std::uint64_t l3_idx = (slided_va >> 14u) & 0x7FFu;
    MMU_ASSERT(l3_idx == 1041, "L3 index shifted by 16 entries");

    const std::uint64_t slided_pa = 0xC0000ULL;
    const std::uint64_t l3_desc = slided_pa | (1ULL << 10u) | (1ULL << 6u) | 0x3u;
    mem.write_u64(l3_table + l3_idx * 8u, l3_desc);

    mmu.set_ttbr1_el1(l2_table);
    mmu.set_tcr_el1(kAppleSiliconTcr16Kb);
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};
    auto res = mmu.translate(mem, slided_va, AccessKind::Read, 1, &fault);
    MMU_ASSERT(res.has_value(), "Slided VA translates successfully");
    MMU_ASSERT(res.value() == slided_pa, "Translated PA matches slided physical page");

    std::cout << "[PASS] KASLR Slide Page-Displacement Translation\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: KASLR Slide Level 2 Block Boundary Crossing
// -----------------------------------------------------------------------------
bool test_kaslr_slide_l2_block_displacement() {
    std::cout << "[3] test_kaslr_slide_l2_block_displacement...\n";

    Memory mem(0x400000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    const std::uint64_t l2_table = 0x30000;
    const std::uint64_t l3_table_new = 0x34000;

    // Apply KASLR slide of +32 MB (crosses 32 MB Level 2 block boundary)
    const std::uint64_t kaslr_slide = 0x02000000ULL; // 32 MB
    const std::uint64_t slided_va = kXnuKernelBase + kaslr_slide;

    // L2 index shifts from 3 to 4
    const std::uint64_t l2_idx = (slided_va >> 25u) & 0x7FFu;
    MMU_ASSERT(l2_idx == 4, "L2 index shifted by 1 to entry 4");
    mem.write_u64(l2_table + l2_idx * 8u, l3_table_new | 0x3u);

    // L3 index remains 1025 within new L3 table
    const std::uint64_t l3_idx = (slided_va >> 14u) & 0x7FFu;
    MMU_ASSERT(l3_idx == 1025, "L3 index 1025 within new L3 table");

    const std::uint64_t slided_pa = 0x100000ULL;
    const std::uint64_t l3_desc = slided_pa | (1ULL << 10u) | (1ULL << 6u) | 0x3u;
    mem.write_u64(l3_table_new + l3_idx * 8u, l3_desc);

    mmu.set_ttbr1_el1(l2_table);
    mmu.set_tcr_el1(kAppleSiliconTcr16Kb);
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};
    auto res = mmu.translate(mem, slided_va, AccessKind::Read, 1, &fault);
    MMU_ASSERT(res.has_value(), "L2 boundary crossed VA translates");
    MMU_ASSERT(res.value() == slided_pa, "Translated PA matches 32MB slided target");

    std::cout << "[PASS] KASLR Slide Level 2 Block Boundary Crossing\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Direct 32 MB Level 2 Block Translation
// -----------------------------------------------------------------------------
bool test_direct_32mb_l2_block_translation() {
    std::cout << "[4] test_direct_32mb_l2_block_translation...\n";

    Memory mem(0x200000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    const std::uint64_t l2_table = 0x10000;

    // L2 Block descriptor: bits [1:0] = 0b01 (Block), bits [24:14] = 0, AF=1, AP=0b01 (RW)
    // Physical block base: 0x40000000 (RAM base)
    const std::uint64_t block_base_pa = 0x40000000ULL;
    const std::uint64_t l2_block_desc = block_base_pa | (1ULL << 10u) | (1ULL << 6u) | 0x1u;

    const std::uint64_t l2_idx = (kXnuKernelBase >> 25u) & 0x7FFu;
    mem.write_u64(l2_table + l2_idx * 8u, l2_block_desc);

    mmu.set_ttbr1_el1(l2_table);
    mmu.set_tcr_el1(kAppleSiliconTcr16Kb);
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};
    auto res = mmu.translate(mem, kXnuKernelBase, AccessKind::Read, 1, &fault);
    MMU_ASSERT(res.has_value(), "L2 block translates without L3 table");

    // 32 MB block offset: bits [24:0] of kXnuKernelBase = 0x07004000 & 0x01FFFFFF = 0x01004000
    const std::uint64_t expected_pa = block_base_pa | (kXnuKernelBase & 0x01FFFFFFULL);
    MMU_ASSERT(res.value() == expected_pa, "PA correctly formed from 32MB block base and offset");

    std::cout << "[PASS] Direct 32 MB Level 2 Block Translation\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: Canonical High-Half Bounds & Address Size Faults
// -----------------------------------------------------------------------------
bool test_canonical_high_half_bounds_enforcement() {
    std::cout << "[5] test_canonical_high_half_bounds_enforcement...\n";

    Memory mem(0x200000);
    MMU mmu;
    mmu.reset();

    mmu.set_ttbr1_el1(0x10000);
    mmu.set_tcr_el1(kAppleSiliconTcr16Kb); // T1SZ = 28 -> 36-bit address space
    mmu.set_sctlr_el1(0x00C50839ULL);

    // Non-canonical address where bit 36 is 0 instead of 1:
    // Expected top 28 bits: 0xFFFFFFF000000000ULL
    // Violating address:    0xFFFFFEF000000000ULL
    const std::uint64_t non_canonical_va = 0xFFFFFEF000000000ULL;

    TranslationFault fault{};
    auto res = mmu.translate(mem, non_canonical_va, AccessKind::Read, 1, &fault);
    MMU_ASSERT(!res.has_value(), "Non-canonical address rejected");
    MMU_ASSERT(fault.type == MmuFaultType::AddressSize, "AddressSize fault generated");

    std::cout << "[PASS] Canonical High-Half Bounds & Address Size Faults\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: 16 KB Granule Permission & Privilege Protection
// -----------------------------------------------------------------------------
bool test_16kb_granule_permissions_and_faults() {
    std::cout << "[6] test_16kb_granule_permissions_and_faults...\n";

    Memory mem(0x200000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    const std::uint64_t l2_table = 0x10000;
    const std::uint64_t l3_table = 0x14000;
    const std::uint64_t l2_idx = (kXnuKernelBase >> 25u) & 0x7FFu;
    const std::uint64_t l3_idx = (kXnuKernelBase >> 14u) & 0x7FFu;

    mem.write_u64(l2_table + l2_idx * 8u, l3_table | 0x3u);

    // 1. Read-Only Enforcement: AP = 0b10 (Read-only, bit 7 = 1)
    const std::uint64_t ro_desc = 0x80000ULL | (1ULL << 10u) | (2ULL << 6u) | 0x3u;
    mem.write_u64(l3_table + l3_idx * 8u, ro_desc);

    mmu.set_ttbr1_el1(l2_table);
    mmu.set_tcr_el1(kAppleSiliconTcr16Kb);
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};
    auto res_write = mmu.translate(mem, kXnuKernelBase, AccessKind::Write, 1, &fault);
    MMU_ASSERT(!res_write.has_value(), "Write to read-only page faulted");
    MMU_ASSERT(fault.type == MmuFaultType::Permission, "Permission fault generated on write");

    // 2. Privileged Execute-Never (PXN): bit 53 = 1
    const std::uint64_t pxn_desc = 0x80000ULL | (1ULL << 53u) | (1ULL << 10u) | (1ULL << 6u) | 0x3u;
    mem.write_u64(l3_table + l3_idx * 8u, pxn_desc);

    auto res_exec = mmu.translate(mem, kXnuKernelBase, AccessKind::Execute, 1, &fault);
    MMU_ASSERT(!res_exec.has_value(), "Execution on PXN page faulted");
    MMU_ASSERT(fault.type == MmuFaultType::Permission, "Permission fault on instruction fetch");

    // 3. Access Flag Fault: AF bit 10 = 0
    const std::uint64_t af_desc = 0x80000ULL | (1ULL << 6u) | 0x3u; // AF = 0
    mem.write_u64(l3_table + l3_idx * 8u, af_desc);

    auto res_af = mmu.translate(mem, kXnuKernelBase, AccessKind::Read, 1, &fault);
    MMU_ASSERT(!res_af.has_value(), "Access to AF=0 page faulted");
    MMU_ASSERT(fault.type == MmuFaultType::AccessFlag, "AccessFlag fault generated");

    // 4. Table Alignment Fault: TTBR1 unaligned to 16 KB boundary
    mmu.set_ttbr1_el1(0x10008ULL); // Misaligned
    auto res_align = mmu.translate(mem, kXnuKernelBase, AccessKind::Read, 1, &fault);
    MMU_ASSERT(!res_align.has_value(), "Unaligned TTBR1 faulted");
    MMU_ASSERT(fault.type == MmuFaultType::Alignment, "Alignment fault generated");

    std::cout << "[PASS] 16 KB Granule Permission & Privilege Protection\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: 16 KB Granule TLB Caching and Invalidation
// -----------------------------------------------------------------------------
bool test_16kb_granule_tlb_caching_and_invalidation() {
    std::cout << "[7] test_16kb_granule_tlb_caching_and_invalidation...\n";

    Memory mem(0x200000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(true);

    const std::uint64_t l2_table = 0x10000;
    const std::uint64_t l3_table = 0x14000;
    const std::uint64_t l2_idx = (kXnuKernelBase >> 25u) & 0x7FFu;
    const std::uint64_t l3_idx = (kXnuKernelBase >> 14u) & 0x7FFu;

    mem.write_u64(l2_table + l2_idx * 8u, l3_table | 0x3u);

    const std::uint64_t target_pa = 0xA0000ULL;
    const std::uint64_t l3_desc = target_pa | (1ULL << 10u) | (1ULL << 6u) | 0x3u;
    mem.write_u64(l3_table + l3_idx * 8u, l3_desc);

    mmu.set_ttbr1_el1(l2_table);
    mmu.set_tcr_el1(kAppleSiliconTcr16Kb);
    mmu.set_sctlr_el1(0x00C50839ULL);

    // Initial walk populates TLB
    TranslationFault fault{};
    auto res1 = mmu.translate(mem, kXnuKernelBase, AccessKind::Read, 1, &fault);
    MMU_ASSERT(res1.has_value() && res1.value() == target_pa, "Initial translation successful");

    // Overwrite memory descriptor with invalid entry (simulating unmapped table in RAM)
    mem.write_u64(l3_table + l3_idx * 8u, 0ULL);

    // Subsequent translation should HIT the TLB and succeed
    auto res2 = mmu.translate(mem, kXnuKernelBase, AccessKind::Read, 1, &fault);
    MMU_ASSERT(res2.has_value() && res2.value() == target_pa, "TLB hit returned cached PA despite RAM corruption");

    // Invalidate TLB
    mmu.invalidate_tlb_all();

    // Now translation must re-walk memory, encounter the invalid descriptor, and fault
    auto res3 = mmu.translate(mem, kXnuKernelBase, AccessKind::Read, 1, &fault);
    MMU_ASSERT(!res3.has_value(), "Translation faulted after TLB flush");
    MMU_ASSERT(fault.type == MmuFaultType::Translation, "Translation fault after TLB flush");

    std::cout << "[PASS] 16 KB Granule TLB Caching and Invalidation\n\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::cout << "=== WinIOS XNU MMU & KASLR 16 KB Granule Conformance Tests ===\n\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*test_fn)(), const char* name) {
        std::cout << "Starting " << name << "...\n";
        if (test_fn()) {
            passed++;
            std::cout << "Completed " << name << ": OK\n";
        } else {
            failed++;
            std::cerr << "[FAIL] " << name << "\n";
        }
    };

    run_test(test_apple_silicon_ttbr1_16kb_paging_baseline, "test_apple_silicon_ttbr1_16kb_paging_baseline");
    run_test(test_kaslr_slide_page_displacement, "test_kaslr_slide_page_displacement");
    run_test(test_kaslr_slide_l2_block_displacement, "test_kaslr_slide_l2_block_displacement");
    run_test(test_direct_32mb_l2_block_translation, "test_direct_32mb_l2_block_translation");
    run_test(test_canonical_high_half_bounds_enforcement, "test_canonical_high_half_bounds_enforcement");
    run_test(test_16kb_granule_permissions_and_faults, "test_16kb_granule_permissions_and_faults");
    run_test(test_16kb_granule_tlb_caching_and_invalidation, "test_16kb_granule_tlb_caching_and_invalidation");

    std::cout << "\nXNU MMU & KASLR Conformance Summary: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}

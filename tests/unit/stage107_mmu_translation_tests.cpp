#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include "cpu/mmu.h"
#include "memory/memory.h"

#define STAGE107_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] FAILED: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace mmu;

namespace {

// Standard Apple Silicon 36-bit virtual space TCR_EL1:
// TG1 = 0b01 (16 KB), TG0 = 0b10 (16 KB), T1SZ = 28, T0SZ = 28
constexpr std::uint64_t kAppleTcr36Bit16Kb =
    (1ULL << 30u) |  // TG1 = 0b01 (16 KB)
    (2ULL << 14u) |  // TG0 = 0b10 (16 KB)
    (28ULL << 16u) | // T1SZ = 28 (64 - 28 = 36-bit VA)
    (28ULL << 0u);   // T0SZ = 28 (64 - 28 = 36-bit VA)

// Standard 48-bit virtual space TCR_EL1:
constexpr std::uint64_t kAppleTcr48Bit16Kb =
    (1ULL << 30u) |  // TG1 = 0b01 (16 KB)
    (2ULL << 14u) |  // TG0 = 0b10 (16 KB)
    (16ULL << 16u) | // T1SZ = 16 (64 - 16 = 48-bit VA)
    (16ULL << 0u);   // T0SZ = 16 (64 - 16 = 48-bit VA)

// Canonical high-half kernel base address for XNU
constexpr std::uint64_t kXnuKernelBase = 0xFFFFFFF007004000ULL;

// -----------------------------------------------------------------------------
// Test 1: TCR_EL1 Granule Decode
// -----------------------------------------------------------------------------
bool test_tcr_granule_decode() {
    std::cout << "[RUN] Test 1: TCR_EL1 granule decode ... ";
    MMU mmu;
    mmu.reset();

    mmu.set_tcr_el1(kAppleTcr36Bit16Kb);
    STAGE107_ASSERT(mmu.get_tg0_granule() == GranuleType::Granule16KB, "TG0 must be 16 KB");
    STAGE107_ASSERT(mmu.get_tg1_granule() == GranuleType::Granule16KB, "TG1 must be 16 KB");
    STAGE107_ASSERT(MMU::get_granule_size(GranuleType::Granule16KB) == 16384ULL, "16 KB granule size is 16384 bytes");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Table Alignment Validation
// -----------------------------------------------------------------------------
bool test_table_alignment_validation() {
    std::cout << "[RUN] Test 2: Table alignment validation ... ";
    STAGE107_ASSERT(MMU::validate_table_alignment(0x4000, GranuleType::Granule16KB), "0x4000 is 16 KB aligned");
    STAGE107_ASSERT(MMU::validate_table_alignment(0x8000, GranuleType::Granule16KB), "0x8000 is 16 KB aligned");
    STAGE107_ASSERT(!MMU::validate_table_alignment(0x1000, GranuleType::Granule16KB), "0x1000 is not 16 KB aligned");
    STAGE107_ASSERT(!MMU::validate_table_alignment(0x4008, GranuleType::Granule16KB), "0x4008 is not 16 KB aligned");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: TTBR1 16 KB L3 Page Translation (High-Half Kernel)
// -----------------------------------------------------------------------------
bool test_ttbr1_16kb_l3_page_translation() {
    std::cout << "[RUN] Test 3: TTBR1 16 KB L3 page translation ... ";
    Memory mem(0x200000); // 2 MB RAM
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    const std::uint64_t l2_table = 0x10000;
    const std::uint64_t l3_table = 0x14000;

    // kXnuKernelBase: 0xFFFFFFF007004000
    // L2 index: (0x07004000 >> 25) & 0x7FF = 3
    const std::uint64_t l2_idx = (kXnuKernelBase >> 25u) & 0x7FFu;
    mem.write_u64(l2_table + l2_idx * 8u, l3_table | 0x3u); // Table descriptor

    // L3 index: (0x07004000 >> 14) & 0x7FF = 1025
    const std::uint64_t l3_idx = (kXnuKernelBase >> 14u) & 0x7FFu;
    const std::uint64_t target_pa = 0x80000ULL;
    // AF=1 (bit 10), AP=0b01 (Read/Write, bit 6), Descriptor=Page (bits [1:0] = 0b11)
    const std::uint64_t l3_desc = target_pa | (1ULL << 10u) | (1ULL << 6u) | 0x3u;
    mem.write_u64(l3_table + l3_idx * 8u, l3_desc);

    mmu.set_ttbr1_el1(l2_table);
    mmu.set_tcr_el1(kAppleTcr36Bit16Kb);
    mmu.set_sctlr_el1(0x00C50839ULL); // MMU enabled

    TranslationFault fault{};
    auto res = mmu.translate(mem, kXnuKernelBase, AccessKind::Read, 1, &fault);
    STAGE107_ASSERT(res.has_value(), "Translation must succeed");
    STAGE107_ASSERT(*res == target_pa + (kXnuKernelBase & 0x3FFFULL), "Physical address must match target PA + page offset");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: TTBR0 16 KB L3 Page Translation (Low-Half Userspace)
// -----------------------------------------------------------------------------
bool test_ttbr0_16kb_l3_page_translation() {
    std::cout << "[RUN] Test 4: TTBR0 16 KB L3 page translation ... ";
    Memory mem(0x200000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    const std::uint64_t l2_table = 0x20000;
    const std::uint64_t l3_table = 0x24000;

    const std::uint64_t user_va = 0x0000000010008000ULL; // Low-half user VA
    // L2 index: (0x10008000 >> 25) & 0x7FF = 8
    const std::uint64_t l2_idx = (user_va >> 25u) & 0x7FFu;
    mem.write_u64(l2_table + l2_idx * 8u, l3_table | 0x3u);

    // L3 index: (0x10008000 >> 14) & 0x7FF = 2
    const std::uint64_t l3_idx = (user_va >> 14u) & 0x7FFu;
    const std::uint64_t target_pa = 0x90000ULL;
    // AF=1, AP[2:1]=0b01 (EL0 accessible), bits [1:0] = 0b11
    const std::uint64_t l3_desc = target_pa | (1ULL << 10u) | (1ULL << 6u) | 0x3u;
    mem.write_u64(l3_table + l3_idx * 8u, l3_desc);

    mmu.set_ttbr0_el1(l2_table);
    mmu.set_tcr_el1(kAppleTcr36Bit16Kb);
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};
    auto res = mmu.translate(mem, user_va, AccessKind::Read, 0, &fault);
    STAGE107_ASSERT(res.has_value(), "TTBR0 translation must succeed for EL0");
    STAGE107_ASSERT(*res == target_pa + (user_va & 0x3FFFULL), "PA matches");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: 16 KB Granule L2 32 MB Block Descriptor Translation
// -----------------------------------------------------------------------------
bool test_16kb_granule_l2_block_translation() {
    std::cout << "[RUN] Test 5: 16 KB granule L2 32 MB block translation ... ";
    Memory mem(0x200000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    const std::uint64_t l2_table = 0x10000;
    // VA = 0xFFFFFFF004000000 (L2 index 2)
    const std::uint64_t va = 0xFFFFFFF004000000ULL | 0x00014000ULL;
    const std::uint64_t l2_idx = (va >> 25u) & 0x7FFu;
    STAGE107_ASSERT(l2_idx == 2, "L2 index is 2");

    // L2 Block descriptor: bits [1:0] = 0b01, block size = 32 MB (2^25)
    const std::uint64_t block_pa = 0x40000000ULL; // 1 GB physical base
    const std::uint64_t l2_block_desc = block_pa | (1ULL << 10u) | (1ULL << 6u) | 0x1u; // Block (0b01)
    mem.write_u64(l2_table + l2_idx * 8u, l2_block_desc);

    mmu.set_ttbr1_el1(l2_table);
    mmu.set_tcr_el1(kAppleTcr36Bit16Kb);
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};
    auto res = mmu.translate(mem, va, AccessKind::Read, 1, &fault);
    STAGE107_ASSERT(res.has_value(), "L2 block translation must succeed");
    STAGE107_ASSERT(*res == block_pa + (va & 0x01FFFFFFULL), "Physical address is block base + 32MB offset");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Translation Fault Generation on Invalid Descriptor (Level 2 & Level 3)
// -----------------------------------------------------------------------------
bool test_translation_fault_invalid_descriptor() {
    std::cout << "[RUN] Test 6: Translation fault on invalid descriptor ... ";
    Memory mem(0x200000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    const std::uint64_t l2_table = 0x10000;
    // Leave entire l2_table zeroed (invalid descriptors: bits [1:0] = 0b00)

    mmu.set_ttbr1_el1(l2_table);
    mmu.set_tcr_el1(kAppleTcr36Bit16Kb);
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};
    auto res = mmu.translate(mem, kXnuKernelBase, AccessKind::Read, 1, &fault);
    STAGE107_ASSERT(!res.has_value(), "Translation must fail on invalid descriptor");
    STAGE107_ASSERT(fault.type == MmuFaultType::Translation, "Fault type must be Translation");
    STAGE107_ASSERT(fault.level == 2, "Fault level must be 2");
    STAGE107_ASSERT(fault.fsc() == 0x06, "FSC for Level 2 Translation Fault is 0x06");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Access Flag Fault Generation (AF == 0)
// -----------------------------------------------------------------------------
bool test_access_flag_fault() {
    std::cout << "[RUN] Test 7: Access flag fault generation ... ";
    Memory mem(0x200000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    const std::uint64_t l2_table = 0x10000;
    const std::uint64_t l3_table = 0x14000;

    const std::uint64_t l2_idx = (kXnuKernelBase >> 25u) & 0x7FFu;
    mem.write_u64(l2_table + l2_idx * 8u, l3_table | 0x3u);

    const std::uint64_t l3_idx = (kXnuKernelBase >> 14u) & 0x7FFu;
    // Set AF = 0 (bit 10 = 0)
    const std::uint64_t l3_desc = 0x80000ULL | (0ULL << 10u) | (1ULL << 6u) | 0x3u;
    mem.write_u64(l3_table + l3_idx * 8u, l3_desc);

    mmu.set_ttbr1_el1(l2_table);
    mmu.set_tcr_el1(kAppleTcr36Bit16Kb);
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};
    auto res = mmu.translate(mem, kXnuKernelBase, AccessKind::Read, 1, &fault);
    STAGE107_ASSERT(!res.has_value(), "Translation must fault when AF=0");
    STAGE107_ASSERT(fault.type == MmuFaultType::AccessFlag, "Fault type must be AccessFlag");
    STAGE107_ASSERT(fault.level == 3, "Fault level must be 3");
    STAGE107_ASSERT(fault.fsc() == 0x0B, "FSC for Level 3 Access Flag Fault is 0x0B");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Permission Fault on Write to Read-Only Page
// -----------------------------------------------------------------------------
bool test_permission_fault_read_only() {
    std::cout << "[RUN] Test 8: Permission fault on write to read-only page ... ";
    Memory mem(0x200000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    const std::uint64_t l2_table = 0x10000;
    const std::uint64_t l3_table = 0x14000;

    const std::uint64_t l2_idx = (kXnuKernelBase >> 25u) & 0x7FFu;
    mem.write_u64(l2_table + l2_idx * 8u, l3_table | 0x3u);

    const std::uint64_t l3_idx = (kXnuKernelBase >> 14u) & 0x7FFu;
    // Set AP[2] = 1 (Read-only, bit 7 = 1), AP[1] = 0 (Privileged-only, bit 6 = 0)
    const std::uint64_t l3_desc = 0x80000ULL | (1ULL << 10u) | (1ULL << 7u) | 0x3u;
    mem.write_u64(l3_table + l3_idx * 8u, l3_desc);

    mmu.set_ttbr1_el1(l2_table);
    mmu.set_tcr_el1(kAppleTcr36Bit16Kb);
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};
    // Read succeeds
    auto read_res = mmu.translate(mem, kXnuKernelBase, AccessKind::Read, 1, &fault);
    STAGE107_ASSERT(read_res.has_value(), "Read to read-only page must succeed");

    // Write fails with Permission Fault
    auto write_res = mmu.translate(mem, kXnuKernelBase, AccessKind::Write, 1, &fault);
    STAGE107_ASSERT(!write_res.has_value(), "Write to read-only page must fault");
    STAGE107_ASSERT(fault.type == MmuFaultType::Permission, "Fault type must be Permission");
    STAGE107_ASSERT(fault.level == 3, "Fault level must be 3");
    STAGE107_ASSERT(fault.fsc() == 0x0F, "FSC for Level 3 Permission Fault is 0x0F");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 9: Permission Fault on Unprivileged Access to Kernel Page
// -----------------------------------------------------------------------------
bool test_permission_fault_unprivileged_access() {
    std::cout << "[RUN] Test 9: Permission fault on unprivileged EL0 access ... ";
    Memory mem(0x200000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    const std::uint64_t l2_table = 0x10000;
    const std::uint64_t l3_table = 0x14000;

    const std::uint64_t l2_idx = (kXnuKernelBase >> 25u) & 0x7FFu;
    mem.write_u64(l2_table + l2_idx * 8u, l3_table | 0x3u);

    const std::uint64_t l3_idx = (kXnuKernelBase >> 14u) & 0x7FFu;
    // AP[1] = 0 (Privileged-only, no EL0 access)
    const std::uint64_t l3_desc = 0x80000ULL | (1ULL << 10u) | (0ULL << 6u) | 0x3u;
    mem.write_u64(l3_table + l3_idx * 8u, l3_desc);

    mmu.set_ttbr1_el1(l2_table);
    mmu.set_tcr_el1(kAppleTcr36Bit16Kb);
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};
    // EL0 access must trigger Permission Fault
    auto res = mmu.translate(mem, kXnuKernelBase, AccessKind::Read, 0, &fault);
    STAGE107_ASSERT(!res.has_value(), "EL0 access to kernel page must fault");
    STAGE107_ASSERT(fault.type == MmuFaultType::Permission, "Fault type is Permission");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 10: Execute-Never (UXN/PXN) Instruction Fetch Fault
// -----------------------------------------------------------------------------
bool test_execute_never_fault() {
    std::cout << "[RUN] Test 10: Execute-never instruction fetch fault ... ";
    Memory mem(0x200000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    const std::uint64_t l2_table = 0x10000;
    const std::uint64_t l3_table = 0x14000;

    const std::uint64_t l2_idx = (kXnuKernelBase >> 25u) & 0x7FFu;
    mem.write_u64(l2_table + l2_idx * 8u, l3_table | 0x3u);

    const std::uint64_t l3_idx = (kXnuKernelBase >> 14u) & 0x7FFu;
    // Set PXN = 1 (bit 53) or XN = 1 (bit 54)
    const std::uint64_t l3_desc = 0x80000ULL | (1ULL << 53u) | (1ULL << 54u) | (1ULL << 10u) | (1ULL << 6u) | 0x3u;
    mem.write_u64(l3_table + l3_idx * 8u, l3_desc);

    mmu.set_ttbr1_el1(l2_table);
    mmu.set_tcr_el1(kAppleTcr36Bit16Kb);
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};
    // AccessKind::Execute must trigger Permission Fault
    auto res = mmu.translate(mem, kXnuKernelBase, AccessKind::Execute, 1, &fault);
    STAGE107_ASSERT(!res.has_value(), "Execute fetch on XN page must fault");
    STAGE107_ASSERT(fault.type == MmuFaultType::Permission, "Fault type must be Permission");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 11: Address Size Fault Detection
// -----------------------------------------------------------------------------
bool test_address_size_fault() {
    std::cout << "[RUN] Test 11: Address size fault detection ... ";
    Memory mem(0x200000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    // In 36-bit VA, bits [63:36] must all be 1 for TTBR1 or all 0 for TTBR0
    // An address with mixed upper bits (neither canonical low nor canonical high) faults
    const std::uint64_t non_canonical_va = 0x0000800000000000ULL; // Bit 47 is set, beyond 36 bits

    mmu.set_ttbr0_el1(0x10000);
    mmu.set_tcr_el1(kAppleTcr36Bit16Kb);
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};
    auto res = mmu.translate(mem, non_canonical_va, AccessKind::Read, 1, &fault);
    STAGE107_ASSERT(!res.has_value(), "Non-canonical VA must fault");
    STAGE107_ASSERT(fault.type == MmuFaultType::Translation || fault.type == MmuFaultType::AddressSize,
                    "Fault must be Translation or AddressSize");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 12: 48-bit Virtual Address Space Translation
// -----------------------------------------------------------------------------
bool test_48bit_va_translation() {
    std::cout << "[RUN] Test 12: 48-bit VA translation ... ";
    Memory mem(0x400000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    // 48-bit translation starts at Level 0 for 16 KB granule
    const std::uint64_t l0_table = 0x10000;
    const std::uint64_t l1_table = 0x14000;
    const std::uint64_t l2_table = 0x18000;
    const std::uint64_t l3_table = 0x1C000;

    const std::uint64_t va48 = 0xFFFF800000008000ULL;
    // L0 index: (va48 >> 47u) & 0x1u = 1
    const std::uint64_t l0_idx = (va48 >> 47u) & 0x1u;
    mem.write_u64(l0_table + l0_idx * 8u, l1_table | 0x3u);

    // L1 index: (va48 >> 36u) & 0x7FFu = 0
    const std::uint64_t l1_idx = (va48 >> 36u) & 0x7FFu;
    mem.write_u64(l1_table + l1_idx * 8u, l2_table | 0x3u);

    // L2 index: (va48 >> 25u) & 0x7FFu = 0
    const std::uint64_t l2_idx = (va48 >> 25u) & 0x7FFu;
    mem.write_u64(l2_table + l2_idx * 8u, l3_table | 0x3u);

    // L3 index: (va48 >> 14u) & 0x7FFu = 2
    const std::uint64_t l3_idx = (va48 >> 14u) & 0x7FFu;
    const std::uint64_t target_pa = 0xA0000ULL;
    mem.write_u64(l3_table + l3_idx * 8u, target_pa | (1ULL << 10u) | (1ULL << 6u) | 0x3u);

    mmu.set_ttbr1_el1(l0_table);
    mmu.set_tcr_el1(kAppleTcr48Bit16Kb);
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};
    auto res = mmu.translate(mem, va48, AccessKind::Read, 1, &fault);
    STAGE107_ASSERT(res.has_value(), "48-bit translation must succeed");
    STAGE107_ASSERT(*res == target_pa + (va48 & 0x3FFFULL), "PA matches");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 13: TLB Caching and Invalidation
// -----------------------------------------------------------------------------
bool test_tlb_caching_and_invalidation() {
    std::cout << "[RUN] Test 13: TLB caching and invalidation ... ";
    Memory mem(0x200000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(true);
    mmu.reset_tlb_stats();

    const std::uint64_t l2_table = 0x10000;
    const std::uint64_t l3_table = 0x14000;

    const std::uint64_t l2_idx = (kXnuKernelBase >> 25u) & 0x7FFu;
    mem.write_u64(l2_table + l2_idx * 8u, l3_table | 0x3u);

    const std::uint64_t l3_idx = (kXnuKernelBase >> 14u) & 0x7FFu;
    const std::uint64_t target_pa = 0x80000ULL;
    mem.write_u64(l3_table + l3_idx * 8u, target_pa | (1ULL << 10u) | (1ULL << 6u) | 0x3u);

    mmu.set_ttbr1_el1(l2_table);
    mmu.set_tcr_el1(kAppleTcr36Bit16Kb);
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};
    // First translation: Miss
    auto res1 = mmu.translate(mem, kXnuKernelBase, AccessKind::Read, 1, &fault);
    STAGE107_ASSERT(res1.has_value(), "First translation succeeds");
    STAGE107_ASSERT(mmu.get_tlb_misses() >= 1, "Must record TLB miss");

    // Second translation: Hit
    auto res2 = mmu.translate(mem, kXnuKernelBase, AccessKind::Read, 1, &fault);
    STAGE107_ASSERT(res2.has_value(), "Second translation succeeds");
    STAGE107_ASSERT(mmu.get_tlb_hits() >= 1, "Must record TLB hit");

    // Invalidation
    mmu.invalidate_tlb_all();
    STAGE107_ASSERT(mmu.get_tlb_invalidations() >= 1, "Must record TLB invalidation");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 14: MAIR_EL1 Memory Attribute Classification (Device vs Normal)
// -----------------------------------------------------------------------------
bool test_mair_attribute_classification() {
    std::cout << "[RUN] Test 14: MAIR_EL1 attribute classification ... ";
    MMU mmu;
    mmu.reset();

    // Standard XNU MAIR_EL1 configuration:
    // Attr0: 0x04 (Device-nGnRE)
    // Attr1: 0xFF (Normal Inner/Outer Write-Back Non-transient Read/Write-Alloc)
    mmu.set_mair_el1(0x000000000000FF04ULL);

    STAGE107_ASSERT(mmu.get_mair_attribute(0) == 0x04, "Attr0 is 0x04");
    STAGE107_ASSERT(mmu.is_device_attribute(0x04), "0x04 is Device memory");
    STAGE107_ASSERT(mmu.get_mair_attribute(1) == 0xFF, "Attr1 is 0xFF");
    STAGE107_ASSERT(!mmu.is_device_attribute(0xFF), "0xFF is Normal memory");

    std::cout << "PASS\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 107: MMU Page Table Translation & Unit Test Suite        \n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*test_fn)()) {
        if (test_fn()) {
            passed++;
        } else {
            failed++;
        }
    };

    run_test(test_tcr_granule_decode);
    run_test(test_table_alignment_validation);
    run_test(test_ttbr1_16kb_l3_page_translation);
    run_test(test_ttbr0_16kb_l3_page_translation);
    run_test(test_16kb_granule_l2_block_translation);
    run_test(test_translation_fault_invalid_descriptor);
    run_test(test_access_flag_fault);
    run_test(test_permission_fault_read_only);
    run_test(test_permission_fault_unprivileged_access);
    run_test(test_execute_never_fault);
    run_test(test_address_size_fault);
    run_test(test_48bit_va_translation);
    run_test(test_tlb_caching_and_invalidation);
    run_test(test_mair_attribute_classification);

    std::cout << "=================================================================\n";
    std::cout << " Summary: " << passed << " / " << (passed + failed) << " tests passed.\n";
    std::cout << "=================================================================\n";

    return (failed == 0) ? 0 : 1;
}

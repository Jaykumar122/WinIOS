// ============================================================================
// WinIOS Campaign 266: First Autonomous XNU Boot
// Stages 266.5 - 266.7: ARM64 MMU Architecture, Genuine Page-Table Walks,
// and System Register / TLB Integration Tests
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "memory/memory.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace mmu;

// -----------------------------------------------------------------------------
// Stage 266.5: ARM64 MMU Architecture Audit
// -----------------------------------------------------------------------------
static bool test_stage266_5_mmu_architecture_audit() {
    std::cout << "[Stage 266.5] Auditing ARM64 MMU Architecture & 16 KiB Granule Parameters...\n";

    MMU mmu;

    // 1. Initial Reset State & SCTLR_EL1 Control Bits
    mmu.reset();
    TEST_ASSERT(!mmu.is_mmu_enabled(), "MMU initially disabled after reset");
    TEST_ASSERT(!mmu.is_stage1_enabled(), "Stage 1 initially disabled");
    TEST_ASSERT(mmu.get_sctlr_el1() == 0x00C50838ULL, "SCTLR_EL1 matches architectural reset value");

    // 2. TCR_EL1 Granule & Address-Size Configuration
    // Set TCR_EL1: TG0 = 16KB (0b10 << 14 = 0x8000), TG1 = 16KB (0b01 << 30 = 0x40000000)
    // T0SZ = 16 (48-bit VA space), T1SZ = 16, IPS = 0b101 (48-bit PA)
    std::uint64_t tcr_val = (0x2ULL << 14u) | (0x1ULL << 30u) | 16ULL | (16ULL << 16u) | (0x5ULL << 32u);
    mmu.set_tcr_el1(tcr_val);
    TEST_ASSERT(mmu.get_tg0_granule() == GranuleType::Granule16KB, "TG0 configured for 16 KiB granule");
    TEST_ASSERT(mmu.get_tg1_granule() == GranuleType::Granule16KB, "TG1 configured for 16 KiB granule");
    TEST_ASSERT(MMU::get_granule_size(GranuleType::Granule16KB) == 16384ULL, "16 KiB granule size is exactly 16384 bytes");

    // Table alignment verification
    TEST_ASSERT(MMU::validate_table_alignment(0x00040000ULL, GranuleType::Granule16KB), "0x40000 is 16 KiB aligned");
    TEST_ASSERT(!MMU::validate_table_alignment(0x00042000ULL, GranuleType::Granule16KB), "0x42000 is not 16 KiB aligned");

    // 3. MAIR_EL1 Memory Attribute Indirection Register
    // Attr0 = Normal Cacheable (0xFF), Attr1 = Device-nGnRE (0x04)
    std::uint64_t mair_val = 0x00000000000004FFULL;
    mmu.set_mair_el1(mair_val);
    TEST_ASSERT(mmu.get_mair_attribute(0) == 0xFFu, "Attr0 is Normal Cacheable (0xFF)");
    TEST_ASSERT(mmu.get_mair_attribute(1) == 0x04u, "Attr1 is Device-nGnRE (0x04)");
    TEST_ASSERT(!mmu.is_device_attribute(0xFFu), "0xFF is Normal memory, not Device");
    TEST_ASSERT(mmu.is_device_attribute(0x04u), "0x04 is Device memory");

    // 4. Descriptor Validation for 16 KiB Granule
    DescriptorKind kind = DescriptorKind::Invalid;
    MmuFaultType fault = MmuFaultType::Translation;

    // Level 2 Block (32MB): bits 1:0 = 0b01
    std::uint64_t l2_block_desc = 0x0000000000000701ULL; // Valid, Block, AttrIndx=0
    bool l2_valid = mmu.validate_descriptor_for_granule(2, l2_block_desc, GranuleType::Granule16KB, false, &kind, &fault);
    TEST_ASSERT(l2_valid && kind == DescriptorKind::Block, "Level 2 Block descriptor (32MB) is valid");

    // Level 2 Table: bits 1:0 = 0b11
    std::uint64_t l2_table_desc = 0x0000000000044003ULL; // Points to next level table
    bool l2_tbl_valid = mmu.validate_descriptor_for_granule(2, l2_table_desc, GranuleType::Granule16KB, false, &kind, &fault);
    TEST_ASSERT(l2_tbl_valid && kind == DescriptorKind::Table, "Level 2 Table descriptor is valid");

    // Level 3 Page (16KB): bits 1:0 = 0b11
    std::uint64_t l3_page_desc = 0x0000000000010703ULL; // Valid, Page, AttrIndx=0
    bool l3_valid = mmu.validate_descriptor_for_granule(3, l3_page_desc, GranuleType::Granule16KB, false, &kind, &fault);
    TEST_ASSERT(l3_valid && kind == DescriptorKind::Page, "Level 3 Page descriptor (16KB) is valid");

    // Invalid descriptor (bit 0 = 0)
    std::uint64_t invalid_desc = 0x0000000000000000ULL;
    bool inv_valid = mmu.validate_descriptor_for_granule(2, invalid_desc, GranuleType::Granule16KB, false, &kind, &fault);
    TEST_ASSERT(!inv_valid && kind == DescriptorKind::Invalid && fault == MmuFaultType::Translation, "Invalid descriptor correctly diagnosed as translation fault");

    // 5. Fault Status Code (FSC) Verification
    TranslationFault tf_trans{ MmuFaultType::Translation, 2, 0x1000, AccessKind::Read, false, false, false, 0 };
    TEST_ASSERT(tf_trans.fsc() == 0x06u, "Level 2 Translation fault FSC == 0b000110 (0x06)");

    TranslationFault tf_perm{ MmuFaultType::Permission, 3, 0x1000, AccessKind::Write, true, false, false, 0 };
    TEST_ASSERT(tf_perm.fsc() == 0x0Fu, "Level 3 Permission fault FSC == 0b001111 (0x0F)");

    TranslationFault tf_align{ MmuFaultType::Alignment, 0, 0x1001, AccessKind::Read, false, false, false, 0 };
    TEST_ASSERT(tf_align.fsc() == 0x21u, "Alignment fault FSC == 0x21");

    std::cout << "  [PASS] Stage 266.5 MMU architecture parameters and descriptor rules verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 266.6: Genuine Guest Page-Table Walks
// -----------------------------------------------------------------------------
static bool test_stage266_6_genuine_guest_page_table_walks() {
    std::cout << "[Stage 266.6] Executing Genuine Guest Page-Table Walks (16 KiB Granule)...\n";

    Memory memory(0x800000); // 8 MiB physical memory
    MMU mmu;

    // Enable Stage 1 MMU
    mmu.set_sctlr_el1(mmu.get_sctlr_el1() | 0x1ULL);
    TEST_ASSERT(mmu.is_stage1_enabled(), "Stage 1 translation enabled");

    // Configure TCR_EL1: TG0 = 16KB, 48-bit VA
    mmu.set_tcr_el1((0x2ULL << 14u) | 16ULL);

    // Configure MAIR_EL1: Attr0 = Normal WB (0xFF), Attr1 = Device-nGnRE (0x04)
    mmu.set_mair_el1(0x00000000000004FFULL);

    // Setup L2 Page Table at physical address 0x00040000 (Frame 16, 16 KiB aligned)
    const std::uint64_t pt_base = 0x00040000ULL;
    mmu.set_ttbr0_el1(pt_base);

    // Populate L2 table entries:
    // Slot 0 (VA 0x00000000 .. 0x01FFFFFF, 32MB): Level 2 Block Identity Map [0x0, 32MB)
    // Descriptor: PA=0x0 | Normal Memory (AttrIndx=0) | RW | Non-secure | Block (0x1) = 0x0000000000000701ULL
    memory.write_u64(pt_base + (0 * 8), 0x0000000000000701ULL);

    // Slot 1 (VA 0x02000000 .. 0x03FFFFFF): Read-Only Block (AP[2]=1 -> 0x781)
    memory.write_u64(pt_base + (1 * 8), 0x0000000002000781ULL);

    // Slot 2 (VA 0x04000000 .. 0x05FFFFFF): Execute-Never Block (UXN=1, PXN=1 -> bits 54, 53)
    memory.write_u64(pt_base + (2 * 8), 0x0060000004000701ULL);

    // Slot 3 (VA 0x06000000 .. 0x07FFFFFF): Unmapped (0x0)
    memory.write_u64(pt_base + (3 * 8), 0x0000000000000000ULL);

    // Slot 285 (VA 0x23A000000 .. 0x23BFFFFFF, 32MB block covering AIC2 at 0x23B100000):
    // Descriptor: PA=0x23A000000 | Device-nGnRE (AttrIndx=1) | RW | Block = 0x000000023A000705ULL
    memory.write_u64(pt_base + (285 * 8), 0x000000023A000705ULL);

    // Test 1: Successful translation of VA 0x00008080 (Kernel Text / RAM block)
    TranslationFault fault;
    bool is_device = false;
    auto pa1 = mmu.translate(memory, 0x00008080ULL, AccessKind::Read, 1 /* EL1 */, &fault, &is_device);
    TEST_ASSERT(pa1.has_value() && pa1.value() == 0x00008080ULL, "VA 0x8080 identity translated to PA 0x8080");
    TEST_ASSERT(!is_device, "Memory attribute is Normal Cacheable, not Device");

    // Test 2: Successful translation of VA 0x23B100004 (AIC2 MMIO register)
    auto pa2 = mmu.translate(memory, 0x23B100004ULL, AccessKind::Read, 1 /* EL1 */, &fault, &is_device);
    TEST_ASSERT(pa2.has_value() && pa2.value() == 0x23B100004ULL, "VA 0x23B100004 translated to physical AIC2");
    TEST_ASSERT(is_device, "Memory attribute correctly resolved to Device-nGnRE");

    // Test 3: Unmapped translation fault at VA 0x06001000 (Slot 3)
    auto pa3 = mmu.translate(memory, 0x06001000ULL, AccessKind::Read, 1 /* EL1 */, &fault, &is_device);
    TEST_ASSERT(!pa3.has_value(), "Unmapped address translation must fail");
    TEST_ASSERT(fault.type == MmuFaultType::Translation, "Fault type is Translation fault");
    TEST_ASSERT(fault.level == 2, "Fault occurred at Level 2");
    TEST_ASSERT(fault.fsc() == 0x06u, "Translation fault FSC == 0x06");

    // Test 4: Permission fault on write to read-only block at VA 0x02000000
    auto pa4 = mmu.translate(memory, 0x02000000ULL, AccessKind::Write, 1 /* EL1 */, &fault, &is_device);
    TEST_ASSERT(!pa4.has_value(), "Write to read-only page must fail");
    TEST_ASSERT(fault.type == MmuFaultType::Permission, "Fault type is Permission fault");
    TEST_ASSERT(fault.fsc() == 0x0Eu, "Level 2 Permission fault FSC == 0x0E");

    // Test 5: Permission fault on instruction fetch from execute-never block at VA 0x04000000
    auto pa5 = mmu.translate(memory, 0x04000000ULL, AccessKind::Execute, 1 /* EL1 */, &fault, &is_device);
    TEST_ASSERT(!pa5.has_value(), "Instruction fetch from XN page must fail");
    TEST_ASSERT(fault.type == MmuFaultType::Permission, "Fault type is Permission fault");
    TEST_ASSERT(fault.is_instruction, "Fault marked as instruction fetch abort");

    std::cout << "  [PASS] Stage 266.6 Genuine guest page-table walks and fault diagnostics verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 266.7: MMU System Register & TLB Integration
// -----------------------------------------------------------------------------
static bool test_stage266_7_mmu_system_registers_and_tlb_integration() {
    std::cout << "[Stage 266.7] Testing MMU System Register Dispatch & TLB Invalidation...\n";

    CPU cpu;
    Memory memory(0x800000);

    // Verify CPU access to MMU registers
    cpu.set_sctlr_el1(0x00C50839ULL); // MMU enabled
    TEST_ASSERT(cpu.get_sctlr_el1() == 0x00C50839ULL, "CPU reads updated SCTLR_EL1");
    TEST_ASSERT(cpu.get_mmu().is_mmu_enabled(), "MMU state reflects SCTLR_EL1.M");

    cpu.set_tcr_el1((0x2ULL << 14u) | 16ULL);
    TEST_ASSERT(cpu.get_tcr_el1() == ((0x2ULL << 14u) | 16ULL), "CPU reads updated TCR_EL1");

    cpu.set_ttbr0_el1(0x00040000ULL);
    TEST_ASSERT(cpu.get_ttbr0_el1() == 0x00040000ULL, "CPU reads updated TTBR0_EL1");

    // Setup basic valid mapping in memory
    memory.write_u64(0x00040000ULL, 0x0000000000000701ULL); // Identity 32MB block

    // Exercise TLB insertion and hit counting
    auto& mmu = cpu.get_mmu();
    mmu.reset_tlb_stats();

    TranslationFault fault;
    // 1st access: TLB miss, entry populated
    auto pa1 = mmu.translate(memory, 0x10000ULL, AccessKind::Read, 1, &fault);
    TEST_ASSERT(pa1.has_value() && pa1.value() == 0x10000ULL, "Initial translation succeeded");
    TEST_ASSERT(mmu.get_tlb_misses() == 1, "First access recorded 1 TLB miss");
    TEST_ASSERT(mmu.get_tlb_hits() == 0, "First access recorded 0 TLB hits");

    // 2nd access to same virtual page: TLB hit
    auto pa2 = mmu.translate(memory, 0x10080ULL, AccessKind::Read, 1, &fault);
    TEST_ASSERT(pa2.has_value() && pa2.value() == 0x10080ULL, "Second translation succeeded");
    TEST_ASSERT(mmu.get_tlb_hits() == 1, "Second access recorded 1 TLB hit");

    // 3. Test TLB Invalidation by VA (All-ASID / Global)
    mmu.invalidate_tlb_vaa(0x10000ULL);
    TEST_ASSERT(mmu.get_tlb_invalidations() >= 1, "TLB invalidation recorded");

    // Next access after invalidation should be a miss again
    auto pa3 = mmu.translate(memory, 0x10000ULL, AccessKind::Read, 1, &fault);
    TEST_ASSERT(pa3.has_value(), "Post-invalidation translation succeeded via re-walk");
    TEST_ASSERT(mmu.get_tlb_misses() == 2, "Post-invalidation access recorded second TLB miss");

    // 4. Test Global Invalidation
    mmu.invalidate_tlb_all();
    TEST_ASSERT(mmu.get_tlb_entry_count() == 0, "Global invalidation clears all TLB entries");

    // 5. Multi-Core MMU Isolation Test
    CPU secondary_core;
    secondary_core.reset();
    secondary_core.set_ttbr0_el1(0x00048000ULL); // Independent page table for Core 1
    TEST_ASSERT(cpu.get_ttbr0_el1() != secondary_core.get_ttbr0_el1(), "Core 0 and Core 1 have isolated TTBR0_EL1 registers");

    std::cout << "  [PASS] Stage 266.7 MMU system registers and TLB integration verified.\n";
    return true;
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "Campaign 266 - Stages 266.5..266.7 MMU Architecture Tests\n";
    std::cout << "=================================================================\n";

    if (!test_stage266_5_mmu_architecture_audit()) return 1;
    if (!test_stage266_6_genuine_guest_page_table_walks()) return 1;
    if (!test_stage266_7_mmu_system_registers_and_tlb_integration()) return 1;

    std::cout << "=================================================================\n";
    std::cout << "All Stages 266.5..266.7 Tests Passed Successfully!\n";
    std::cout << "=================================================================\n";
    return 0;
}

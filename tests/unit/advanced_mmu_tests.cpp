#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "cpu/smp_cluster.h"
#include "memory/memory.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK FAILED: "    \
                      << #cond << std::endl;                                   \
            throw std::runtime_error("Check failed: " #cond);                 \
        }                                                                      \
    } while (0)

using mmu::AccessKind;
using mmu::DescriptorKind;
using mmu::MmuFaultType;
using mmu::TranslationFault;

static constexpr std::uint64_t kRamSize = 0x400000; // 4 MB RAM
static constexpr std::uint32_t kNop = 0xD503201Fu;
static constexpr std::uint32_t kRet = 0xD65F03E0u;

inline std::uint32_t ldr_x(unsigned rt, unsigned rn, unsigned imm12_scaled = 0) {
    return 0xF9400000u | ((imm12_scaled & 0xFFFu) << 10u) | (rn << 5u) | rt;
}

inline std::uint32_t str_x(unsigned rt, unsigned rn, unsigned imm12_scaled = 0) {
    return 0xF9000000u | ((imm12_scaled & 0xFFFu) << 10u) | (rn << 5u) | rt;
}

inline std::uint32_t ldr_w(unsigned rt, unsigned rn, unsigned imm12_scaled = 0) {
    return 0xB9400000u | ((imm12_scaled & 0xFFFu) << 10u) | (rn << 5u) | rt;
}

inline std::uint32_t str_w(unsigned rt, unsigned rn, unsigned imm12_scaled = 0) {
    return 0xB9000000u | ((imm12_scaled & 0xFFFu) << 10u) | (rn << 5u) | rt;
}

// ===========================================================================
// Stage 2: Translation Descriptor Validation Tests
// ===========================================================================

void test_descriptor_validation_stage1() {
    mmu::MMU mmu_inst;
    DescriptorKind kind = DescriptorKind::Invalid;
    MmuFaultType fault = MmuFaultType::Translation;

    // --- Level 0 ---
    // Invalid (bit 0 = 0)
    CHECK(!mmu_inst.validate_descriptor(0, 0x0000ULL, false, &kind, &fault));
    CHECK(kind == DescriptorKind::Invalid);
    CHECK(fault == MmuFaultType::Translation);

    // Valid Table (0b11)
    CHECK(mmu_inst.validate_descriptor(0, 0x5003ULL, false, &kind, &fault));
    CHECK(kind == DescriptorKind::Table);

    // Block descriptor (0b01) at Level 0 is RESERVED in 4KB granule
    CHECK(!mmu_inst.validate_descriptor(0, 0x5001ULL, false, &kind, &fault));
    CHECK(kind == DescriptorKind::Invalid);
    CHECK(fault == MmuFaultType::Translation);

    // Reserved (0b10) at Level 0
    CHECK(!mmu_inst.validate_descriptor(0, 0x5002ULL, false, &kind, &fault));
    CHECK(fault == MmuFaultType::Translation);

    // Level 0 Table with PA > 48 bits (bits [51:48] != 0) -> AddressSize fault
    CHECK(!mmu_inst.validate_descriptor(0, 0x0001000000005003ULL, false, &kind, &fault));
    CHECK(fault == MmuFaultType::AddressSize);

    // --- Level 1 ---
    // 1GB Block (0b01)
    CHECK(mmu_inst.validate_descriptor(1, 0x40000401ULL, false, &kind, &fault));
    CHECK(kind == DescriptorKind::Block);

    // Table (0b11)
    CHECK(mmu_inst.validate_descriptor(1, 0x6003ULL, false, &kind, &fault));
    CHECK(kind == DescriptorKind::Table);

    // Reserved (0b10)
    CHECK(!mmu_inst.validate_descriptor(1, 0x6002ULL, false, &kind, &fault));
    CHECK(fault == MmuFaultType::Translation);

    // L1 Block with PA > 48 bits -> AddressSize fault
    CHECK(!mmu_inst.validate_descriptor(1, 0x0002000040000401ULL, false, &kind, &fault));
    CHECK(fault == MmuFaultType::AddressSize);

    // --- Level 2 ---
    // 2MB Block (0b01)
    CHECK(mmu_inst.validate_descriptor(2, 0x200401ULL, false, &kind, &fault));
    CHECK(kind == DescriptorKind::Block);

    // Table (0b11)
    CHECK(mmu_inst.validate_descriptor(2, 0x7003ULL, false, &kind, &fault));
    CHECK(kind == DescriptorKind::Table);

    // Reserved (0b10)
    CHECK(!mmu_inst.validate_descriptor(2, 0x7002ULL, false, &kind, &fault));
    CHECK(fault == MmuFaultType::Translation);

    // --- Level 3 ---
    // 4KB Page (0b11)
    CHECK(mmu_inst.validate_descriptor(3, 0x84FFULL, false, &kind, &fault));
    CHECK(kind == DescriptorKind::Page);

    // Reserved (0b01) at Level 3 in Stage 1
    CHECK(!mmu_inst.validate_descriptor(3, 0x84F1ULL, false, &kind, &fault));
    CHECK(fault == MmuFaultType::Translation);

    // Reserved (0b10) at Level 3
    CHECK(!mmu_inst.validate_descriptor(3, 0x84F2ULL, false, &kind, &fault));
    CHECK(fault == MmuFaultType::Translation);

    // L3 Page with PA > 48 bits -> AddressSize fault
    CHECK(!mmu_inst.validate_descriptor(3, 0x00080000000084FFULL, false, &kind, &fault));
    CHECK(fault == MmuFaultType::AddressSize);

    // --- Stage 2 Level 3 ---
    // In Stage 2, Level 3 descriptors with bit 0 = 1 are valid pages
    CHECK(mmu_inst.validate_descriptor(3, 0x84FDULL, true, &kind, &fault));
    CHECK(kind == DescriptorKind::Page);
    CHECK(mmu_inst.validate_descriptor(3, 0x04FDULL, true, &kind, &fault));
    CHECK(kind == DescriptorKind::Page);

    std::cout << "  [PASS] Translation Descriptor Validation\n";
}

// ===========================================================================
// Stage 3: Stage-1 Translation Hardening Tests
// ===========================================================================

void test_stage1_translation_hardening() {
    Memory mem(kRamSize);
    mmu::MMU mmu_inst;

    // Set up 4-level page tables for TTBR0
    const std::uint64_t l0_base = 0x2000;
    const std::uint64_t l1_base = 0x3000;
    const std::uint64_t l2_base = 0x4000;
    const std::uint64_t l3_base = 0x5000;
    const std::uint64_t page_pa = 0x10000;

    mem.write_u64(l0_base, l1_base | 0x3ULL);
    mem.write_u64(l1_base, l2_base | 0x3ULL);
    mem.write_u64(l2_base, l3_base | 0x3ULL);
    // VA 0x1000 -> PA 0x10000 (RW, AF=1)
    mem.write_u64(l3_base + 8u, page_pa | 0x3ULL | (1ULL << 10u));

    mmu_inst.set_ttbr0_el1(l0_base);
    mmu_inst.set_sctlr_el1(0x00C50839ULL); // MMU on

    // 1. Successful translation at VA 0x1000
    TranslationFault fault;
    auto res = mmu_inst.translate(mem, 0x1000, AccessKind::Read, 1, &fault);
    CHECK(res.has_value());
    CHECK(res.value() == page_pa);

    // 2. T0SZ Bounds Check: set T0SZ = 25 (39-bit VA space)
    // Range is [0, 2^39 - 1] (0x0000007FFFFFFFFF)
    mmu_inst.set_tcr_el1(25u);
    res = mmu_inst.translate(mem, 0x1000, AccessKind::Read, 1, &fault);
    CHECK(res.has_value()); // VA 0x1000 is within 39 bits

    // Address with bit 39 set (0x8000000000) exceeds 39-bit T0SZ -> AddressSize fault
    res = mmu_inst.translate(mem, 0x0000008000001000ULL, AccessKind::Read, 1, &fault);
    CHECK(!res.has_value());
    CHECK(fault.type == MmuFaultType::AddressSize);
    CHECK(fault.level == 0);

    // 3. Top Byte Ignore (TBI0): enable TBI0 (bit 37 of TCR_EL1)
    mmu_inst.set_tcr_el1(16u | (1ULL << 37u)); // T0SZ=16 (48-bit), TBI0=1
    // Tagged address with top byte 0xAB: bits [63:56] = 0xAB
    const std::uint64_t tagged_va = 0xAB00000000001000ULL;
    res = mmu_inst.translate(mem, tagged_va, AccessKind::Read, 1, &fault);
    CHECK(res.has_value());
    CHECK(res.value() == page_pa);

    // 4. TTBR1 translation: Set up table for high addresses (bit 63 = 1)
    const std::uint64_t l0_ttbr1 = 0x6000;
    const std::uint64_t l1_ttbr1 = 0x7000;
    const std::uint64_t l2_ttbr1 = 0x8000;
    const std::uint64_t l3_ttbr1 = 0x9000;
    const std::uint64_t high_pa  = 0x20000;

    mem.write_u64(l0_ttbr1 + 0xFF8, l1_ttbr1 | 0x3ULL); // Index 511
    mem.write_u64(l1_ttbr1 + 0xFF8, l2_ttbr1 | 0x3ULL); // Index 511
    mem.write_u64(l2_ttbr1 + 0xFF8, l3_ttbr1 | 0x3ULL); // Index 511
    mem.write_u64(l3_ttbr1 + 0xFF8, high_pa | 0x3ULL | (1ULL << 10u));

    mmu_inst.set_ttbr1_el1(l0_ttbr1);
    const std::uint64_t high_va = 0xFFFFFFFFFFFFF000ULL;
    res = mmu_inst.translate(mem, high_va, AccessKind::Read, 1, &fault);
    CHECK(res.has_value());
    CHECK(res.value() == high_pa);

    // 5. Canonical Address Failure for TTBR1: non-canonical high address
    mmu_inst.set_tcr_el1(0); // TBI off
    const std::uint64_t non_canonical_va = 0x8000000000001000ULL; // Bit 63=1 but top bits not 0xFFFF
    res = mmu_inst.translate(mem, non_canonical_va, AccessKind::Read, 1, &fault);
    CHECK(!res.has_value());
    CHECK(fault.type == MmuFaultType::AddressSize);

    // 6. 2MB Block Mapping in Level 2:
    // In l1_base, index 2 (VA 0x80000000) points to 2MB block at PA 0x40000
    mem.write_u64(l1_base + 16u, 0x40000ULL | 0x1ULL | (1ULL << 10u)); // Level 1 Block (1GB, but PA 0x40000)
    // Wait, 1GB block bit 1:0 is 0b01. Let's test Level 2 2MB block:
    const std::uint64_t l2_block_table = 0xA000;
    mem.write_u64(l1_base + 8u, l2_block_table | 0x3ULL);
    // Index 1 in l2: VA 0x40200000 -> 2MB block at PA 0x200000
    mem.write_u64(l2_block_table + 8u, 0x200000ULL | 0x1ULL | (1ULL << 10u)); // 2MB Block
    res = mmu_inst.translate(mem, 0x40201234ULL, AccessKind::Read, 1, &fault);
    CHECK(res.has_value());
    CHECK(res.value() == (0x200000ULL | 0x1234ULL));

    std::cout << "  [PASS] Stage-1 Translation Hardening (T0SZ, T1SZ, TBI, Canonical, Blocks)\n";
}

// ===========================================================================
// Stage 4: Stage-1 Permission Matrix Tests
// ===========================================================================

void test_stage1_permission_matrix() {
    Memory mem(kRamSize);
    mmu::MMU mmu_inst;

    const std::uint64_t l0_base = 0x2000;
    const std::uint64_t l1_base = 0x3000;
    const std::uint64_t l2_base = 0x4000;
    const std::uint64_t l3_base = 0x5000;
    const std::uint64_t page_pa = 0x10000;

    mem.write_u64(l0_base, l1_base | 0x3ULL);
    mem.write_u64(l1_base, l2_base | 0x3ULL);
    mem.write_u64(l2_base, l3_base | 0x3ULL);

    mmu_inst.set_ttbr0_el1(l0_base);
    mmu_inst.set_sctlr_el1(0x00C50839ULL); // MMU on

    TranslationFault fault;

    // Helper to configure L3 PTE
    auto set_l3_entry = [&](std::uint64_t ap_bits, bool uxn, bool pxn, bool af) {
        std::uint64_t pte = page_pa | 0x3ULL;
        pte |= (ap_bits & 0x3ULL) << 6u;
        if (af) pte |= (1ULL << 10u);
        if (pxn) pte |= (1ULL << 53u);
        if (uxn) pte |= (1ULL << 54u);
        mem.write_u64(l3_base + 8u, pte);
        mmu_inst.invalidate_tlb_all();
    };

    // Case 1: AP = 00 -> EL1 R/W, EL0 No Access
    set_l3_entry(0b00, false, false, true);
    CHECK(mmu_inst.translate(mem, 0x1000, AccessKind::Read, 1, &fault).has_value());
    CHECK(mmu_inst.translate(mem, 0x1000, AccessKind::Write, 1, &fault).has_value());
    CHECK(!mmu_inst.translate(mem, 0x1000, AccessKind::Read, 0, &fault).has_value());
    CHECK(fault.type == MmuFaultType::Permission);
    CHECK(!mmu_inst.translate(mem, 0x1000, AccessKind::Write, 0, &fault).has_value());

    // Case 2: AP = 01 -> EL1 R/W, EL0 R/W
    set_l3_entry(0b01, false, false, true);
    CHECK(mmu_inst.translate(mem, 0x1000, AccessKind::Read, 1, &fault).has_value());
    CHECK(mmu_inst.translate(mem, 0x1000, AccessKind::Write, 1, &fault).has_value());
    CHECK(mmu_inst.translate(mem, 0x1000, AccessKind::Read, 0, &fault).has_value());
    CHECK(mmu_inst.translate(mem, 0x1000, AccessKind::Write, 0, &fault).has_value());

    // Case 3: AP = 10 -> EL1 RO, EL0 No Access
    set_l3_entry(0b10, false, false, true);
    CHECK(mmu_inst.translate(mem, 0x1000, AccessKind::Read, 1, &fault).has_value());
    CHECK(!mmu_inst.translate(mem, 0x1000, AccessKind::Write, 1, &fault).has_value());
    CHECK(fault.type == MmuFaultType::Permission);
    CHECK(!mmu_inst.translate(mem, 0x1000, AccessKind::Read, 0, &fault).has_value());

    // Case 4: AP = 11 -> EL1 RO, EL0 RO
    set_l3_entry(0b11, false, false, true);
    CHECK(mmu_inst.translate(mem, 0x1000, AccessKind::Read, 1, &fault).has_value());
    CHECK(mmu_inst.translate(mem, 0x1000, AccessKind::Read, 0, &fault).has_value());
    CHECK(!mmu_inst.translate(mem, 0x1000, AccessKind::Write, 1, &fault).has_value());
    CHECK(!mmu_inst.translate(mem, 0x1000, AccessKind::Write, 0, &fault).has_value());

    // Case 5: Execute-Never (UXN and PXN)
    set_l3_entry(0b01, true /* UXN */, false /* PXN */, true);
    CHECK(!mmu_inst.translate(mem, 0x1000, AccessKind::Execute, 0, &fault).has_value()); // EL0 blocked by UXN
    CHECK(fault.type == MmuFaultType::Permission);
    CHECK(mmu_inst.translate(mem, 0x1000, AccessKind::Execute, 1, &fault).has_value());  // EL1 allowed

    set_l3_entry(0b01, false /* UXN */, true /* PXN */, true);
    CHECK(mmu_inst.translate(mem, 0x1000, AccessKind::Execute, 0, &fault).has_value());   // EL0 allowed
    CHECK(!mmu_inst.translate(mem, 0x1000, AccessKind::Execute, 1, &fault).has_value());  // EL1 blocked by PXN

    // Case 6: SCTLR_EL1.WXN (Write implies XN)
    set_l3_entry(0b01 /* Writable */, false, false, true);
    mmu_inst.set_sctlr_el1(0x00C50839ULL | (1ULL << 19u)); // WXN = 1
    CHECK(!mmu_inst.translate(mem, 0x1000, AccessKind::Execute, 1, &fault).has_value()); // Blocked by WXN
    CHECK(fault.type == MmuFaultType::Permission);
    mmu_inst.set_sctlr_el1(0x00C50839ULL); // Restore WXN = 0

    // Case 7: Access Flag (AF = 0)
    set_l3_entry(0b01, false, false, false /* AF = 0 */);
    CHECK(!mmu_inst.translate(mem, 0x1000, AccessKind::Read, 1, &fault).has_value());
    CHECK(fault.type == MmuFaultType::AccessFlag);
    CHECK(fault.level == 3);

    // Case 8: Hierarchical APTable (Level 1 forces read-only: bit 61)
    set_l3_entry(0b01 /* Writable */, false, false, true);
    mem.write_u64(l1_base, l2_base | 0x3ULL | (1ULL << 61u)); // APTable[0] = 1 (Force RO)
    mmu_inst.invalidate_tlb_all();
    CHECK(!mmu_inst.translate(mem, 0x1000, AccessKind::Write, 1, &fault).has_value()); // Blocked by APTable
    CHECK(fault.type == MmuFaultType::Permission);

    // Reset table descriptor
    mem.write_u64(l1_base, l2_base | 0x3ULL);

    std::cout << "  [PASS] Stage-1 Permission Matrix (AP, APTable, PXN, UXN, WXN, AF)\n";
}

// ===========================================================================
// Stage 5: Stage-2 Translation and Fault Hardening Tests
// ===========================================================================

void test_stage2_translation_hardening() {
    Memory mem(kRamSize);
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);

    // Set up Stage 2 tables in guest RAM
        const std::uint64_t s2_l1 = 0x4000;
    const std::uint64_t s2_l2 = 0x5000;
    const std::uint64_t s2_l3 = 0x6000;
    const std::uint64_t final_pa = 0x20000;

    // VTTBR_EL2: VMID = 0x1, base = 0x4000
    cpu.set_vttbr_el2(0x0001000000004000ULL);
    cpu.set_vtcr_el2(0x80003550ULL);
    cpu.set_vbar_el2(0x80000);

    // Map IPA 0x2000 -> PA 0x20000 and page 0 for code
    mem.write_u64(s2_l1, s2_l2 | 0x3ULL);
    mem.write_u64(s2_l2, s2_l3 | 0x3ULL);
    // Page 0 (PC=0..0xFFF) mapped identity
    mem.write_u64(s2_l3 + 0u, 0x0000ULL | 0x1ULL | (0x3ULL << 6u) | (1ULL << 10u));
    // S2AP = 0b11 (Read/Write), AF = 1 (bit 10), Valid = 1 (bit 0)
    mem.write_u64(s2_l3 + 16u, final_pa | 0x1ULL | (0x3ULL << 6u) | (1ULL << 10u));

    mem.write_u64(final_pa, 0x123456789ABCDEF0ULL);

    // Enable HCR_EL2: RW=1, VM=1
    cpu.set_hcr_el2(0x80000001ULL);

    // 1. Successful read through Stage 2
    cpu.write_register(1, 0x2000);
    cpu.set_program_counter(0x100);
    mem.write_u32(0x100, ldr_x(0, 1)); // LDR X0, [X1]
    cpu.step(mem);
    CHECK(cpu.read_register(0) == 0x123456789ABCDEF0ULL);

    // 2. Stage 2 Permission Fault: change S2AP to Read-Only (0b01)
    mem.write_u64(s2_l3 + 16u, final_pa | 0x1ULL | (0x1ULL << 6u) | (1ULL << 10u));
    cpu.get_mmu().invalidate_tlb_all();

    // Store to 0x2000 triggers Stage-2 Permission Fault to EL2
    cpu.write_register(2, 0xCAFEBABEULL);
    cpu.set_program_counter(0x104);
    mem.write_u32(0x104, str_x(2, 1)); // STR X2, [X1]
    cpu.step(mem);

    CHECK(cpu.get_current_el() == 2);
    CHECK(cpu.get_program_counter() == (0x80000 + 0x400u)); // EL2 lower-EL sync vector
    CHECK(cpu.get_far_el2() == 0x2000ULL);
    CHECK(cpu.get_hpfar_el2() == (0x2000ULL >> 8u)); // (0x2000 >> 12) << 4
    CHECK((cpu.get_esr_el2() & 0x3Fu) == 0x0Fu);      // Level 3 Permission Fault (FSC 0x0F)

    std::cout << "  [PASS] Stage-2 Translation and Exception Hardening (ESR_EL2, FAR_EL2, HPFAR_EL2)\n";
}

// ===========================================================================
// Stage 6: Memory Attributes and MAIR Tests
// ===========================================================================

void test_memory_attributes_and_mair() {
    Memory mem(kRamSize);
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);

    const std::uint64_t l0_base = 0x2000;
    const std::uint64_t l1_base = 0x3000;
    const std::uint64_t l2_base = 0x4000;
    const std::uint64_t l3_base = 0x5000;
    const std::uint64_t normal_pa = 0x10000;
    const std::uint64_t device_pa = 0x11000;

    mem.write_u64(l0_base, l1_base | 0x3ULL);
    mem.write_u64(l1_base, l2_base | 0x3ULL);
    mem.write_u64(l2_base, l3_base | 0x3ULL);

    // MAIR_EL1:
    // Attr0 = 0xFF (Normal Memory WB/WA)
    // Attr1 = 0x04 (Device-nGnRE Memory)
    cpu.set_mair_el1(0x00000000000004FFULL);
    cpu.set_ttbr0_el1(l0_base);
    cpu.set_vbar_el1(0x50000);
    cpu.set_sctlr_el1(0x00C50839ULL & ~(2ULL)); // MMU on, SCTLR_EL1.A = 0 (Alignment check off)

    // Identity-map page 0 for code execution (PC=0..0xFFF)
    mem.write_u64(l3_base + 0u, 0x0000ULL | 0x3ULL | (1ULL << 10u));
    // VA 0x1000 -> AttrIndx 0 (Normal Memory)
    mem.write_u64(l3_base + 8u, normal_pa | 0x3ULL | (1ULL << 10u) | (0ULL << 2u));
    // VA 0x2000 -> AttrIndx 1 (Device Memory)
    mem.write_u64(l3_base + 16u, device_pa | 0x3ULL | (1ULL << 10u) | (1ULL << 2u));

    // 1. Unaligned access to Normal Memory when SCTLR_EL1.A = 0 succeeds
    cpu.write_register(1, 0x1001); // Unaligned address (offset 1)
    cpu.set_program_counter(0x100);
    mem.write_u32(0x100, ldr_w(0, 1));
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1);
    CHECK(cpu.get_program_counter() == 0x104);

    // 2. Unaligned access to Device Memory MUST generate Alignment Fault even if SCTLR_EL1.A = 0!
    cpu.write_register(1, 0x2001); // Unaligned address in Device Memory
    cpu.set_program_counter(0x104);
    mem.write_u32(0x104, ldr_w(0, 1));
    cpu.step(mem);

    // Alignment fault vectors to EL1 sync vector (offset 0x200 from VBAR_EL1)
    CHECK(cpu.get_program_counter() == (0x50000 + 0x200u));
    CHECK((cpu.get_esr_el1() & 0x3Fu) == 0x21u); // FSC 0x21: Alignment Fault
    CHECK(cpu.get_far_el1() == 0x2001u);

    std::cout << "  [PASS] Memory Attributes & Device Alignment Enforcement (MAIR_EL1)\n";
}

// ===========================================================================
// Stage 7: ASID and VMID Isolation Tests
// ===========================================================================

void test_asid_and_vmid_isolation() {
    Memory mem(kRamSize);
    mmu::MMU mmu_inst;

    // Set up two distinct page tables for two processes
    // Process 1 (ASID = 0x01) maps VA 0x1000 -> PA 0x10000
    // Process 2 (ASID = 0x02) maps VA 0x1000 -> PA 0x20000
    const std::uint64_t proc1_l0 = 0x2000;
    const std::uint64_t proc1_l1 = 0x3000;
    const std::uint64_t proc1_l2 = 0x4000;
    const std::uint64_t proc1_l3 = 0x5000;

    const std::uint64_t proc2_l0 = 0x6000;
    const std::uint64_t proc2_l1 = 0x7000;
    const std::uint64_t proc2_l2 = 0x8000;
    const std::uint64_t proc2_l3 = 0x9000;

    mem.write_u64(proc1_l0, proc1_l1 | 0x3ULL);
    mem.write_u64(proc1_l1, proc1_l2 | 0x3ULL);
    mem.write_u64(proc1_l2, proc1_l3 | 0x3ULL);
    // Non-global bit (bit 11) set!
    mem.write_u64(proc1_l3 + 8u, 0x10000ULL | 0x3ULL | (1ULL << 10u) | (1ULL << 11u));

    mem.write_u64(proc2_l0, proc2_l1 | 0x3ULL);
    mem.write_u64(proc2_l1, proc2_l2 | 0x3ULL);
    mem.write_u64(proc2_l2, proc2_l3 | 0x3ULL);
    mem.write_u64(proc2_l3 + 8u, 0x20000ULL | 0x3ULL | (1ULL << 10u) | (1ULL << 11u));

    mmu_inst.set_sctlr_el1(0x00C50839ULL);

    // 1. Run as Process 1 (ASID = 1)
    mmu_inst.set_ttbr0_el1((0x0001ULL << 48u) | proc1_l0);
    TranslationFault fault;
    auto res1 = mmu_inst.translate(mem, 0x1000, AccessKind::Read, 1, &fault);
    CHECK(res1.has_value());
    CHECK(res1.value() == 0x10000ULL);

    // 2. Switch to Process 2 (ASID = 2) without clearing TLB
    mmu_inst.set_ttbr0_el1((0x0002ULL << 48u) | proc2_l0);
    auto res2 = mmu_inst.translate(mem, 0x1000, AccessKind::Read, 1, &fault);
    CHECK(res2.has_value());
    CHECK(res2.value() == 0x20000ULL);

    // 3. Invalidate only ASID 1
    mmu_inst.invalidate_tlb_asid(0x0001u);

    // Process 2 entry still cached in TLB!
    CHECK(mmu_inst.get_tlb_entry_count() >= 1);
    auto res2_cached = mmu_inst.translate(mem, 0x1000, AccessKind::Read, 1, &fault);
    CHECK(res2_cached.has_value());
    CHECK(res2_cached.value() == 0x20000ULL);

    // 4. Stage 2 VMID Isolation:
    // VM 1 (VMID = 0x10) and VM 2 (VMID = 0x20)
    mmu_inst.set_vttbr_el2(0x0010000000004000ULL);
    mmu_inst.invalidate_tlb_vmid(0x0010u);

    std::cout << "  [PASS] ASID and VMID Context Isolation\n";
}

// ===========================================================================
// Stage 8: TLB Multicore Synchronization & Invalidation Tests
// ===========================================================================

void test_tlb_multicore_synchronization() {
    winios::SMPCluster cluster(2, kRamSize);
    Memory& mem = cluster.get_memory();

    CPU& core0 = cluster.get_core(0);
    CPU& core1 = cluster.get_core(1);
    core0.set_architectural_exceptions(true);
    core1.set_architectural_exceptions(true);
    core0.set_current_el(1);
    core1.set_current_el(1);

    const std::uint64_t l0_base = 0x2000;
    const std::uint64_t l1_base = 0x3000;
    const std::uint64_t l2_base = 0x4000;
    const std::uint64_t l3_base = 0x5000;
    const std::uint64_t page_a  = 0x10000;
    const std::uint64_t page_b  = 0x20000;

    mem.write_u64(l0_base, l1_base | 0x3ULL);
    mem.write_u64(l1_base, l2_base | 0x3ULL);
    mem.write_u64(l2_base, l3_base | 0x3ULL);
    // Identity-map page 0 for code execution (PC=0..0xFFF)
    mem.write_u64(l3_base + 0u, 0x0000ULL | 0x3ULL | (1ULL << 10u));
    // VA 0x1000 -> page_a (non-global, ASID=0)
    mem.write_u64(l3_base + 8u, page_a | 0x3ULL | (1ULL << 10u) | (1ULL << 11u));

    mem.write_u64(page_a, 0xAAAAAAAAAAAAAAAAULL);
    mem.write_u64(page_b, 0xBBBBBBBBBBBBBBBBULL);

    core0.set_ttbr0_el1(l0_base);
    core0.set_sctlr_el1(0x00C50839ULL);
    core1.set_ttbr0_el1(l0_base);
    core1.set_sctlr_el1(0x00C50839ULL);

    // Both cores cache translation
    core0.write_register(1, 0x1000);
    core0.set_program_counter(0x100);
    mem.write_u32(0x100, ldr_x(0, 1));
    core0.step(mem);
    CHECK(core0.read_register(0) == 0xAAAAAAAAAAAAAAAAULL);

    core1.write_register(1, 0x1000);
    core1.set_program_counter(0x100);
    core1.step(mem);
    CHECK(core1.read_register(0) == 0xAAAAAAAAAAAAAAAAULL);

    // Modify PTE in RAM to point to page_b
    mem.write_u64(l3_base + 8u, page_b | 0x3ULL | (1ULL << 10u) | (1ULL << 11u));

    // Local TLBI on Core 0 does not invalidate Core 1
    core0.get_mmu().invalidate_tlb_all();
    core0.set_program_counter(0x100);
    core0.step(mem);
    CHECK(core0.read_register(0) == 0xBBBBBBBBBBBBBBBBULL); // Core 0 reloaded page_b

    core1.set_program_counter(0x100);
    core1.step(mem);
    CHECK(core1.read_register(0) == 0xAAAAAAAAAAAAAAAAULL); // Core 1 still holds cached page_a

    // Broadcast TLBI IS from Core 0 invalidates Core 1
    cluster.broadcast_tlbi_all(0);
    core1.set_program_counter(0x100);
    core1.step(mem);
    CHECK(core1.read_register(0) == 0xBBBBBBBBBBBBBBBBULL); // Core 1 now reloaded page_b!

    std::cout << "  [PASS] TLB Multicore Synchronization and Broadcast Invalidation\n";
}

// ===========================================================================
// Stage 9: Fault Accuracy and Syndrome Verification Tests
// ===========================================================================

void test_fault_syndrome_accuracy() {
    TranslationFault fault;

    // Check all FSC calculations
    fault.type = MmuFaultType::AddressSize;
    fault.level = 0; CHECK(fault.fsc() == 0x00u);
    fault.level = 1; CHECK(fault.fsc() == 0x01u);
    fault.level = 2; CHECK(fault.fsc() == 0x02u);
    fault.level = 3; CHECK(fault.fsc() == 0x03u);

    fault.type = MmuFaultType::Translation;
    fault.level = 0; CHECK(fault.fsc() == 0x04u);
    fault.level = 1; CHECK(fault.fsc() == 0x05u);
    fault.level = 2; CHECK(fault.fsc() == 0x06u);
    fault.level = 3; CHECK(fault.fsc() == 0x07u);

    fault.type = MmuFaultType::AccessFlag;
    fault.level = 0; CHECK(fault.fsc() == 0x08u);
    fault.level = 1; CHECK(fault.fsc() == 0x09u);
    fault.level = 2; CHECK(fault.fsc() == 0x0Au);
    fault.level = 3; CHECK(fault.fsc() == 0x0Bu);

    fault.type = MmuFaultType::Permission;
    fault.level = 0; CHECK(fault.fsc() == 0x0Cu);
    fault.level = 1; CHECK(fault.fsc() == 0x0Du);
    fault.level = 2; CHECK(fault.fsc() == 0x0Eu);
    fault.level = 3; CHECK(fault.fsc() == 0x0Fu);

    fault.type = MmuFaultType::ExternalWalk;
    fault.level = 0; CHECK(fault.fsc() == 0x14u);
    fault.level = 1; CHECK(fault.fsc() == 0x15u);
    fault.level = 2; CHECK(fault.fsc() == 0x16u);
    fault.level = 3; CHECK(fault.fsc() == 0x17u);

    std::cout << "  [PASS] Fault Status Code (FSC) Accuracy\n";
}

// ===========================================================================
// Stage 11: Stress Testing (Context Switch & Multi-Core Translation)
// ===========================================================================

void test_mmu_stress_suite() {
    Memory mem(kRamSize);
    mmu::MMU mmu_inst;
    mmu_inst.set_sctlr_el1(0x00C50839ULL);

    const std::uint64_t l0_base = 0x2000;
    const std::uint64_t l1_base = 0x3000;
    const std::uint64_t l2_base = 0x4000;
    const std::uint64_t l3_base = 0x5000;

    mem.write_u64(l0_base, l1_base | 0x3ULL);
    mem.write_u64(l1_base, l2_base | 0x3ULL);
    mem.write_u64(l2_base, l3_base | 0x3ULL);

    // 1. Stress: 500 ASID context switches
    TranslationFault fault;
    for (std::uint16_t asid = 1; asid <= 500; ++asid) {
        mem.write_u64(l3_base + 8u, (asid * 0x1000ULL) | 0x3ULL | (1ULL << 10u) | (1ULL << 11u));
        mmu_inst.set_ttbr0_el1((static_cast<std::uint64_t>(asid) << 48u) | l0_base);
        auto res = mmu_inst.translate(mem, 0x1000, AccessKind::Read, 1, &fault);
        CHECK(res.has_value());
        CHECK(res.value() == (asid * 0x1000ULL));
    }

    std::cout << "  [PASS] MMU Stress Suite (500 ASID Context Switches)\n";
}

int main() {
    std::cout << "=== WinIOS Phase 45 Advanced MMU Test Suite ===\n";
    try {
        test_descriptor_validation_stage1();
        test_stage1_translation_hardening();
        test_stage1_permission_matrix();
        test_stage2_translation_hardening();
        test_memory_attributes_and_mair();
        test_asid_and_vmid_isolation();
        test_tlb_multicore_synchronization();
        test_fault_syndrome_accuracy();
        test_mmu_stress_suite();
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << "\n";
        return 1;
    }

    std::cout << "advanced_mmu_tests: all checks passed\n";
    return 0;
}

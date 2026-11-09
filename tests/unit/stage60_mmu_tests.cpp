#include "cpu/mmu.h"
#include "memory/memory.h"
#include "test_harness.h"

#include <cstdint>
#include <iostream>
#include <vector>

using namespace mmu;

namespace {

// ----------------------------------------------------------------------------
// Test 1: Translation Granule Decoding & Table Alignment
// ----------------------------------------------------------------------------
void test_granule_decoding_and_alignment() {
    MMU mmu;

    // Default TCR_EL1 is 0 -> TG0 is 0b00 (4KB), TG1 is 0b00 (4KB)
    CHECK(mmu.get_tg0_granule() == GranuleType::Granule4KB);
    CHECK(mmu.get_tg1_granule() == GranuleType::Granule4KB);

    // Set TG0 = 0b01 (64KB), TG1 = 0b11 (64KB)
    mmu.set_tcr_el1((1ULL << 14u) | (3ULL << 30u));
    CHECK(mmu.get_tg0_granule() == GranuleType::Granule64KB);
    CHECK(mmu.get_tg1_granule() == GranuleType::Granule64KB);

    // Set TG0 = 0b10 (16KB), TG1 = 0b01 (16KB)
    mmu.set_tcr_el1((2ULL << 14u) | (1ULL << 30u));
    CHECK(mmu.get_tg0_granule() == GranuleType::Granule16KB);
    CHECK(mmu.get_tg1_granule() == GranuleType::Granule16KB);

    // Granule sizes
    CHECK(MMU::get_granule_size(GranuleType::Granule4KB) == 4096);
    CHECK(MMU::get_granule_size(GranuleType::Granule16KB) == 16384);
    CHECK(MMU::get_granule_size(GranuleType::Granule64KB) == 65536);

    // Table alignment validation
    CHECK(MMU::validate_table_alignment(0x1000, GranuleType::Granule4KB) == true);
    CHECK(MMU::validate_table_alignment(0x1008, GranuleType::Granule4KB) == false);

    CHECK(MMU::validate_table_alignment(0x4000, GranuleType::Granule16KB) == true);
    CHECK(MMU::validate_table_alignment(0x2000, GranuleType::Granule16KB) == false);

    CHECK(MMU::validate_table_alignment(0x10000, GranuleType::Granule64KB) == true);
    CHECK(MMU::validate_table_alignment(0x8000, GranuleType::Granule64KB) == false);
}

// ----------------------------------------------------------------------------
// Test 2: Descriptor Validation Across All Granules
// ----------------------------------------------------------------------------
void test_descriptor_validation_all_granules() {
    MMU mmu;
    DescriptorKind kind = DescriptorKind::Invalid;
    MmuFaultType fault = MmuFaultType::Translation;

    // 1. 4KB Granule
    // Level 0: Block reserved
    CHECK(!mmu.validate_descriptor_for_granule(0, 0x1001ULL, GranuleType::Granule4KB, false, &kind, &fault));
    CHECK(fault == MmuFaultType::Translation);
    // Level 0: Valid Table
    CHECK(mmu.validate_descriptor_for_granule(0, 0x1003ULL, GranuleType::Granule4KB, false, &kind, &fault));
    CHECK(kind == DescriptorKind::Table);
    // Level 2: Valid 2MB Block
    CHECK(mmu.validate_descriptor_for_granule(2, 0x200001ULL, GranuleType::Granule4KB, false, &kind, &fault));
    CHECK(kind == DescriptorKind::Block);

    // 2. 16KB Granule
    // Level 2: Valid 32MB Block (bits [24:14] == 0)
    CHECK(mmu.validate_descriptor_for_granule(2, 0x02000001ULL, GranuleType::Granule16KB, false, &kind, &fault));
    CHECK(kind == DescriptorKind::Block);
    // Level 2: Unaligned 32MB Block (misaligned bits in [24:14])
    CHECK(!mmu.validate_descriptor_for_granule(2, 0x02004001ULL, GranuleType::Granule16KB, false, &kind, &fault));
    CHECK(fault == MmuFaultType::AddressSize);
    // Level 3: Valid 16KB Page
    CHECK(mmu.validate_descriptor_for_granule(3, 0x00040003ULL, GranuleType::Granule16KB, false, &kind, &fault));
    CHECK(kind == DescriptorKind::Page);

    // 3. 64KB Granule
    // Level 2: Valid 512MB Block (bits [28:16] == 0)
    CHECK(mmu.validate_descriptor_for_granule(2, 0x20000001ULL, GranuleType::Granule64KB, false, &kind, &fault));
    CHECK(kind == DescriptorKind::Block);
    // Level 2: Unaligned 512MB Block (misaligned bits in [28:16])
    CHECK(!mmu.validate_descriptor_for_granule(2, 0x20010001ULL, GranuleType::Granule64KB, false, &kind, &fault));
    CHECK(fault == MmuFaultType::AddressSize);
    // Level 3: Valid 64KB Page
    CHECK(mmu.validate_descriptor_for_granule(3, 0x00050003ULL, GranuleType::Granule64KB, false, &kind, &fault));
    CHECK(kind == DescriptorKind::Page);
}

// ----------------------------------------------------------------------------
// Test 3: 4KB Granule Translation Walks
// ----------------------------------------------------------------------------
void test_4kb_granule_walks() {
    Memory mem(0x200000); // 2 MB RAM
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    // L0 -> L1 -> L2 -> L3 page translation
    const std::uint64_t l0_table = 0x10000;
    const std::uint64_t l1_table = 0x11000;
    const std::uint64_t l2_table = 0x12000;
    const std::uint64_t l3_table = 0x13000;

    mem.write_u64(l0_table, l1_table | 0x3u);
    mem.write_u64(l1_table, l2_table | 0x3u);
    mem.write_u64(l2_table, l3_table | 0x3u);

    // L3 Page: PA = 0x80000, AF=1 (bit 10), AP=0b01 (RW, EL0+EL1), type=0b11
    const std::uint64_t l3_desc = 0x80000ULL | (1ULL << 10u) | (1ULL << 6u) | 0x3u;
    mem.write_u64(l3_table + 5 * 8, l3_desc); // index 5 -> VA = 0x5000

    mmu.set_ttbr0_el1(l0_table);
    mmu.set_tcr_el1(16ULL); // TG0=4KB, T0SZ=16
    mmu.set_sctlr_el1(0x00C50839ULL); // MMU enable

    TranslationFault fault{};
    const auto res = mmu.translate(mem, 0x5042ULL, AccessKind::Read, 0, &fault);
    CHECK(res.has_value());
    CHECK(res.value() == 0x80042ULL);
}

// ----------------------------------------------------------------------------
// Test 4: 16KB Granule Translation Walks
// ----------------------------------------------------------------------------
void test_16kb_granule_walks() {
    Memory mem(0x200000); // 2 MB RAM
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    // 16KB tables must be 16KB aligned (0x4000 boundary)
    const std::uint64_t l2_table = 0x10000;
    const std::uint64_t l3_table = 0x14000;

    // L2 Table pointing to L3
    // index 0 -> VA bits [35:25] == 0
    mem.write_u64(l2_table, l3_table | 0x3u);

    // L3 Page: PA = 0x90000, AF=1, AP=0b01 (RW), type=0b11
    // index 2 -> VA bits [24:14] == 2 -> VA = 2 << 14 = 0x8000
    const std::uint64_t l3_desc = 0x90000ULL | (1ULL << 10u) | (1ULL << 6u) | 0x3u;
    mem.write_u64(l3_table + 2 * 8, l3_desc);

    mmu.set_ttbr0_el1(l2_table);
    // Set TCR_EL1: TG0 = 16KB (0b10 << 14), T0SZ = 16
    mmu.set_tcr_el1((2ULL << 14u) | 16ULL);
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};
    const auto res = mmu.translate(mem, 0x8055ULL, AccessKind::Read, 0, &fault);
    CHECK(res.has_value());
    CHECK(res.value() == 0x90055ULL);
}

// ----------------------------------------------------------------------------
// Test 5: 64KB Granule Translation Walks
// ----------------------------------------------------------------------------
void test_64kb_granule_walks() {
    Memory mem(0x200000); // 2 MB RAM
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    // 64KB tables must be 64KB aligned (0x10000 boundary)
    const std::uint64_t l2_table = 0x20000;
    const std::uint64_t l3_table = 0x30000;

    // L2 Table pointing to L3
    mem.write_u64(l2_table, l3_table | 0x3u);

    // L3 Page: PA = 0xA0000, AF=1, AP=0b01 (RW), type=0b11
    // index 1 -> VA bits [28:16] == 1 -> VA = 1 << 16 = 0x10000
    const std::uint64_t l3_desc = 0xA0000ULL | (1ULL << 10u) | (1ULL << 6u) | 0x3u;
    mem.write_u64(l3_table + 1 * 8, l3_desc);

    mmu.set_ttbr0_el1(l2_table);
    // Set TCR_EL1: TG0 = 64KB (0b01 << 14), T0SZ = 16
    mmu.set_tcr_el1((1ULL << 14u) | 16ULL);
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};
    const auto res = mmu.translate(mem, 0x10077ULL, AccessKind::Read, 0, &fault);
    CHECK(res.has_value());
    CHECK(res.value() == 0xA0077ULL);
}

// ----------------------------------------------------------------------------
// Test 6: Table Alignment Fault Handling
// ----------------------------------------------------------------------------
void test_table_alignment_fault() {
    Memory mem(0x100000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    // TTBR set to 0x12000: 4KB aligned, but NOT 16KB or 64KB aligned
    mmu.set_ttbr0_el1(0x12000);
    mmu.set_tcr_el1((2ULL << 14u) | 16ULL); // TG0 = 16KB
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};
    const auto res = mmu.translate(mem, 0x4000ULL, AccessKind::Read, 0, &fault);
    CHECK(!res.has_value());
    CHECK(fault.type == MmuFaultType::Alignment);
    CHECK(fault.fsc() == 0x21u); // Architectural alignment fault code
}

// ----------------------------------------------------------------------------
// Test 7: Access Permissions (AP[2:1]) Enforcement
// ----------------------------------------------------------------------------
void test_access_permissions_enforcement() {
    Memory mem(0x200000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    const std::uint64_t l0_table = 0x10000;
    const std::uint64_t l1_table = 0x11000;
    const std::uint64_t l2_table = 0x12000;
    const std::uint64_t l3_table = 0x13000;

    mem.write_u64(l0_table, l1_table | 0x3u);
    mem.write_u64(l1_table, l2_table | 0x3u);
    mem.write_u64(l2_table, l3_table | 0x3u);

    // Page 1: Read-Only for EL1 & EL0 (AP = 0b11: bit 7=1, bit 6=1)
    // desc = PA 0x80000 | AF(1<<10) | AP(3<<6) | 0x3
    const std::uint64_t ro_desc = 0x80000ULL | (1ULL << 10u) | (3ULL << 6u) | 0x3u;
    mem.write_u64(l3_table, ro_desc); // VA = 0x0000

    // Page 2: Privileged-only RW (AP = 0b00: bit 7=0, bit 6=0)
    const std::uint64_t priv_desc = 0x90000ULL | (1ULL << 10u) | (0ULL << 6u) | 0x3u;
    mem.write_u64(l3_table + 8, priv_desc); // VA = 0x1000

    mmu.set_ttbr0_el1(l0_table);
    mmu.set_tcr_el1(16ULL);
    mmu.set_sctlr_el1(0x00C50839ULL);

    // 1. Read-only page write violation
    TranslationFault fault1{};
    auto res1 = mmu.translate(mem, 0x0010ULL, AccessKind::Write, 1, &fault1);
    CHECK(!res1.has_value());
    CHECK(fault1.type == MmuFaultType::Permission);

    // 2. Privileged page access from EL0 violation
    TranslationFault fault2{};
    auto res2 = mmu.translate(mem, 0x1010ULL, AccessKind::Read, 0, &fault2);
    CHECK(!res2.has_value());
    CHECK(fault2.type == MmuFaultType::Permission);

    // 3. Privileged page access from EL1 succeeds
    TranslationFault fault3{};
    auto res3 = mmu.translate(mem, 0x1010ULL, AccessKind::Read, 1, &fault3);
    CHECK(res3.has_value());
    CHECK(res3.value() == 0x90010ULL);
}

// ----------------------------------------------------------------------------
// Test 8: Execute-Never (UXN/PXN) Restrictions
// ----------------------------------------------------------------------------
void test_execute_never_restrictions() {
    Memory mem(0x200000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    const std::uint64_t l0_table = 0x10000;
    const std::uint64_t l1_table = 0x11000;
    const std::uint64_t l2_table = 0x12000;
    const std::uint64_t l3_table = 0x13000;

    mem.write_u64(l0_table, l1_table | 0x3u);
    mem.write_u64(l1_table, l2_table | 0x3u);
    mem.write_u64(l2_table, l3_table | 0x3u);

    // Page: UXN=1 (bit 54), PXN=0 (bit 53), AP=0b01 (RW EL0+EL1)
    const std::uint64_t uxn_desc = 0x80000ULL | (1ULL << 54u) | (1ULL << 10u) | (1ULL << 6u) | 0x3u;
    mem.write_u64(l3_table, uxn_desc);

    mmu.set_ttbr0_el1(l0_table);
    mmu.set_tcr_el1(16ULL);
    mmu.set_sctlr_el1(0x00C50839ULL);

    // EL0 execution must fail with Permission fault
    TranslationFault fault1{};
    auto res1 = mmu.translate(mem, 0x0020ULL, AccessKind::Execute, 0, &fault1);
    CHECK(!res1.has_value());
    CHECK(fault1.type == MmuFaultType::Permission);

    // EL1 execution must succeed (PXN is 0)
    TranslationFault fault2{};
    auto res2 = mmu.translate(mem, 0x0020ULL, AccessKind::Execute, 1, &fault2);
    CHECK(res2.has_value());
    CHECK(res2.value() == 0x80020ULL);
}

// ----------------------------------------------------------------------------
// Test 9: Access Flag (AF) Fault Handling
// ----------------------------------------------------------------------------
void test_access_flag_fault() {
    Memory mem(0x200000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    const std::uint64_t l0_table = 0x10000;
    const std::uint64_t l1_table = 0x11000;
    const std::uint64_t l2_table = 0x12000;
    const std::uint64_t l3_table = 0x13000;

    mem.write_u64(l0_table, l1_table | 0x3u);
    mem.write_u64(l1_table, l2_table | 0x3u);
    mem.write_u64(l2_table, l3_table | 0x3u);

    // Page: AF = 0 (bit 10 is clear)
    const std::uint64_t no_af_desc = 0x80000ULL | (0ULL << 10u) | (1ULL << 6u) | 0x3u;
    mem.write_u64(l3_table, no_af_desc);

    mmu.set_ttbr0_el1(l0_table);
    mmu.set_tcr_el1(16ULL);
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};
    auto res = mmu.translate(mem, 0x0030ULL, AccessKind::Read, 1, &fault);
    CHECK(!res.has_value());
    CHECK(fault.type == MmuFaultType::AccessFlag);
    CHECK(fault.level == 3);
    CHECK(fault.fsc() == 0x0Bu); // Access flag level 3 FSC: 0x08 + 3 = 0x0B
}

// ----------------------------------------------------------------------------
// Test 10: Address Size Fault (T0SZ boundary)
// ----------------------------------------------------------------------------
void test_address_size_fault() {
    Memory mem(0x200000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    // T0SZ = 25 -> 64 - 25 = 39-bit virtual address space (max VA = 0x7FFFFFFFFF)
    mmu.set_ttbr0_el1(0x10000);
    mmu.set_tcr_el1(25ULL);
    mmu.set_sctlr_el1(0x00C50839ULL);

    // VA = 0x0000008000000000 (bit 39 set -> exceeds 39-bit space)
    TranslationFault fault{};
    auto res = mmu.translate(mem, 0x0000008000000000ULL, AccessKind::Read, 1, &fault);
    CHECK(!res.has_value());
    CHECK(fault.type == MmuFaultType::AddressSize);
    CHECK(fault.level == 0);
    CHECK(fault.fsc() == 0x00u); // Address size level 0 FSC: 0x00
}

} // namespace

int main() {
    std::cout << "[1] test_granule_decoding_and_alignment...\n";
    test_granule_decoding_and_alignment();

    std::cout << "[2] test_descriptor_validation_all_granules...\n";
    test_descriptor_validation_all_granules();

    std::cout << "[3] test_4kb_granule_walks...\n";
    test_4kb_granule_walks();

    std::cout << "[4] test_16kb_granule_walks...\n";
    test_16kb_granule_walks();

    std::cout << "[5] test_64kb_granule_walks...\n";
    test_64kb_granule_walks();

    std::cout << "[6] test_table_alignment_fault...\n";
    test_table_alignment_fault();

    std::cout << "[7] test_access_permissions_enforcement...\n";
    test_access_permissions_enforcement();

    std::cout << "[8] test_execute_never_restrictions...\n";
    test_execute_never_restrictions();

    std::cout << "[9] test_access_flag_fault...\n";
    test_access_flag_fault();

    std::cout << "[10] test_address_size_fault...\n";
    test_address_size_fault();

    return test::summarize("stage60_mmu_tests");
}

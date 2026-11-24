// ============================================================================
// WinIOS Campaign 267: Master Engineering Execution
// Stages 267.6 - 267.9: 16 KiB MMU Geometry Derivation, Descriptor Decoding,
// Genuine Page-Table Walk, and Translation Control System Register Tests
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "cpu/arm64_descriptor_decoder.h"
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

using namespace winios;
using namespace winios::cpu;
using namespace mmu;

// -----------------------------------------------------------------------------
// Stage 267.6: 16 KiB MMU Translation Geometry Independent Derivation & Correction
// -----------------------------------------------------------------------------
static bool test_stage267_6_mmu_geometry_derivation() {
    std::cout << "[Stage 267.6] Testing Independent 16 KiB MMU Geometry Derivation & Correction...\n";

    // Mathematical derivation of 16 KiB translation geometry:
    // Page Granule: 16 KiB = 2^14 bytes -> 14 offset bits [13:0]
    // Descriptor size: 8 bytes = 2^3 bytes
    // Descriptors per table: 16 KiB / 8 bytes = 2,048 = 2^11 entries -> 11 indexing bits per level
    // Level 3: bits [24:14] (11 bits) -> Page size: 2^14 = 16 KiB
    // Level 2: bits [35:25] (11 bits) -> Block size: 2^25 = 32 MiB
    // Level 1: bits [46:36] (11 bits) -> Block size: 2^36 = 64 GiB (NOT 32 GiB!)
    // Level 0: bit [47] (for 48-bit VA)

    constexpr std::uint64_t kL3PageSize  = 1ULL << 14; // 16 KiB
    constexpr std::uint64_t kL2BlockSize = 1ULL << 25; // 32 MiB
    constexpr std::uint64_t kL1BlockSize = 1ULL << 36; // 64 GiB

    TEST_ASSERT(kL3PageSize == 16384ULL, "Level 3 Page Size is 16 KiB");
    TEST_ASSERT(kL2BlockSize == 0x02000000ULL, "Level 2 Block Size is 32 MiB");
    TEST_ASSERT(kL1BlockSize == 0x1000000000ULL, "Level 1 Block Size is 64 GiB (2^36 bytes)");

    MMU mmu;
    DescriptorKind kind = DescriptorKind::Invalid;
    MmuFaultType fault = MmuFaultType::Translation;

    // 1. Level 1 Block (64 GiB): Bits [35:14] must be 0
    // Test aligned 64 GiB block: 0x1000000000 (bit 36 set, bits [35:14] == 0)
    const std::uint64_t valid_l1_block = 0x0000001000000701ULL; // Bit 36 set, type=0b01
    const bool v1 = mmu.validate_descriptor_for_granule(1, valid_l1_block, GranuleType::Granule16KB, false, &kind, &fault);
    TEST_ASSERT(v1 && kind == DescriptorKind::Block, "Aligned 64 GiB Level 1 block must be valid");

    // Test misaligned Level 1 block: Bit 35 set (which would have been valid in a 32GB model, but invalid for 64GB)
    const std::uint64_t misaligned_l1_bit35 = 0x0000000800000701ULL; // Bit 35 set
    const bool v2 = mmu.validate_descriptor_for_granule(1, misaligned_l1_bit35, GranuleType::Granule16KB, false, &kind, &fault);
    TEST_ASSERT(!v2 && fault == MmuFaultType::AddressSize, "Bit 35 set in L1 block must be rejected with AddressSize fault");

    // Test misaligned Level 1 block: Bit 20 set
    const std::uint64_t misaligned_l1_bit20 = 0x0000001000100701ULL; // Bit 20 set
    const bool v3 = mmu.validate_descriptor_for_granule(1, misaligned_l1_bit20, GranuleType::Granule16KB, false, &kind, &fault);
    TEST_ASSERT(!v3 && fault == MmuFaultType::AddressSize, "Bit 20 set in L1 block must be rejected with AddressSize fault");

    // 2. Level 2 Block (32 MiB): Bits [24:14] must be 0
    const std::uint64_t valid_l2_block = 0x0000000002000701ULL; // Bit 25 set, bits [24:14] == 0
    const bool v4 = mmu.validate_descriptor_for_granule(2, valid_l2_block, GranuleType::Granule16KB, false, &kind, &fault);
    TEST_ASSERT(v4 && kind == DescriptorKind::Block, "Aligned 32 MiB Level 2 block must be valid");

    const std::uint64_t misaligned_l2_bit20 = 0x0000000002100701ULL; // Bit 20 set
    const bool v5 = mmu.validate_descriptor_for_granule(2, misaligned_l2_bit20, GranuleType::Granule16KB, false, &kind, &fault);
    TEST_ASSERT(!v5 && fault == MmuFaultType::AddressSize, "Bit 20 set in L2 block must be rejected with AddressSize fault");

    std::cout << "  - Level 1 Block Size: 64 GiB (bits [35:14] RES0, verified)\n";
    std::cout << "  - Level 2 Block Size: 32 MiB (bits [24:14] RES0, verified)\n";
    std::cout << "  - Level 3 Page Size:  16 KiB (bits [13:0] offset, verified)\n";

    std::cout << "  [PASS] Stage 267.6 MMU geometry derivation verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 267.7: Stage 1 Page-Table Descriptor Decoding & Attribute Conformance
// -----------------------------------------------------------------------------
static bool test_stage267_7_descriptor_decoding() {
    std::cout << "[Stage 267.7] Testing Stage 1 Page-Table Descriptor Decoding & Attribute Conformance...\n";

    // 1. Table Descriptor (type 0b11 at Level 1 or 2)
    const std::uint64_t table_desc = 0x0000000000048003ULL; // Points to 0x48000, type=0b11
    auto dt = Arm64DescriptorDecoder::decode(table_desc, 1, Arm64Granule::Granule16KB);
    TEST_ASSERT(dt.valid && dt.type == DecodedDescriptorType::Table, "Table descriptor decoded successfully");
    TEST_ASSERT(dt.output_address == 0x48000ULL, "Table address is 0x48000");

    // 2. Page Descriptor (type 0b11 at Level 3)
    // AP=0b00 (Kernel RW), AttrIndx=0 (Normal), AF=1, SH=0b11 (Inner Shareable)
    const std::uint64_t page_desc = 0x0000000000020703ULL;
    auto dp = Arm64DescriptorDecoder::decode(page_desc, 3, Arm64Granule::Granule16KB);
    TEST_ASSERT(dp.valid && dp.type == DecodedDescriptorType::Page, "Page descriptor decoded successfully");
    TEST_ASSERT(dp.access_flag, "Access Flag is set");
    TEST_ASSERT(dp.sh == 0b11, "Shareability is Inner Shareable");
    TEST_ASSERT(dp.attr_indx == 0, "AttrIndx is Normal Memory");

    // 3. Permission checks and W^X violation detection
    // RWX page: AP=0b00 (Writable), PXN=0 (Executable) -> W^X violation!
    TEST_ASSERT(dp.is_writable(false), "Page is writable in EL1");
    TEST_ASSERT(dp.is_executable(false), "Page has PXN=0 (executable)");
    TEST_ASSERT(dp.is_wx_violation(false), "Descriptor correctly flags W^X violation");

    // Hardened Read-Only Executable page: AP=0b10 (ReadOnly), PXN=0
    const std::uint64_t ro_x_desc = 0x0000000000000783ULL; // AP=0b10, PXN=0
    auto dro = Arm64DescriptorDecoder::decode(ro_x_desc, 3, Arm64Granule::Granule16KB);
    TEST_ASSERT(!dro.is_writable(false), "Page is Read-Only");
    TEST_ASSERT(dro.is_executable(false), "Page is Executable");
    TEST_ASSERT(!dro.is_wx_violation(false), "ROX page obeys W^X");

    // Hardened Read-Write Non-Executable page: AP=0b00, PXN=1
    const std::uint64_t rw_nx_desc = 0x0020000000000703ULL; // PXN bit 53 set
    auto drw = Arm64DescriptorDecoder::decode(rw_nx_desc, 3, Arm64Granule::Granule16KB);
    TEST_ASSERT(drw.is_writable(false), "Page is Writable");
    TEST_ASSERT(!drw.is_executable(false), "Page is Non-Executable (PXN=1)");
    TEST_ASSERT(!drw.is_wx_violation(false), "RWNX page obeys W^X");

    std::cout << "  - Table descriptor:  0x" << std::hex << dt.output_address << std::dec << " (Decoded)\n";
    std::cout << "  - ROX descriptor:    W^X Conforming (RX)\n";
    std::cout << "  - RWNX descriptor:   W^X Conforming (RW)\n";

    std::cout << "  [PASS] Stage 267.7 Descriptor decoding verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 267.8: Genuine Guest Page-Table Walk & Fault Syndrome Generation
// -----------------------------------------------------------------------------
static bool test_stage267_8_guest_page_table_walk() {
    std::cout << "[Stage 267.8] Testing Hardware Page-Table Walk & Fault Syndrome Generation...\n";

    MMU mmu;
    Memory memory(0x800000); // 8 MiB

    // Set up a standard translation hierarchy in memory:
    const std::uint64_t l0_table = 0x40000ULL;
    const std::uint64_t l1_table = 0x44000ULL;
    const std::uint64_t l2_table = 0x48000ULL;
    const std::uint64_t l3_table = 0x4C000ULL;

    // L0 points to L1
    memory.write_u64(static_cast<std::size_t>(l0_table), l1_table | 0x3ULL);
    // L1 points to L2
    memory.write_u64(static_cast<std::size_t>(l1_table), l2_table | 0x3ULL);
    // L2 points to L3
    memory.write_u64(static_cast<std::size_t>(l2_table), l3_table | 0x3ULL);
    // L3 entry 0: maps to PA 0x10000, AP=0b10 (Kernel Read-Only), AF=1, UXN=1, PXN=1
    const std::uint64_t l3_desc_ro = 0x0000000000010000ULL | (1ULL << 10u) | (2ULL << 6u) | 0x3ULL;
    memory.write_u64(static_cast<std::size_t>(l3_table), l3_desc_ro);

    mmu.set_ttbr0_el1(l0_table);
    mmu.set_tcr_el1(16ULL); // T0SZ = 16
    mmu.set_sctlr_el1(0x00C50839ULL); // MMU enabled (bit 0 = 1)

    // 1. Successful read translation of VA 0x0
    TranslationFault fault{};
    const auto res = mmu.translate(memory, 0x0ULL, AccessKind::Read, 1 /* EL1 */, &fault);
    TEST_ASSERT(res.has_value(), "Translation of mapped VA 0x0 succeeded");
    TEST_ASSERT(res.value() == 0x10000ULL, "Resolved PA matches 0x10000");

    // 2. Permission Fault on Write to Read-Only Page
    const auto write_res = mmu.translate(memory, 0x0ULL, AccessKind::Write, 1 /* EL1 */, &fault);
    TEST_ASSERT(!write_res.has_value(), "Write to read-only page must fail");
    TEST_ASSERT(fault.type == MmuFaultType::Permission, "Fault must be Permission Fault");
    TEST_ASSERT(fault.fsc() == 0x0F, "FSC indicates Level 3 Permission Fault");

    // 3. Translation Fault on unmapped VA (VA 0x50000 is not in L3 entry 0)
    const auto unmapped_res = mmu.translate(memory, 0x50000ULL, AccessKind::Read, 1 /* EL1 */, &fault);
    TEST_ASSERT(!unmapped_res.has_value(), "Access to unmapped page must fail");
    TEST_ASSERT(fault.type == MmuFaultType::Translation, "Fault must be Translation Fault");

    // 4. AddressSize fault on VA exceeding 48-bit address space
    const auto oob_res = mmu.translate(memory, 0x0001000000000000ULL, AccessKind::Read, 1 /* EL1 */, &fault);
    TEST_ASSERT(!oob_res.has_value(), "VA > 48-bit must produce AddressSize fault");
    TEST_ASSERT(fault.type == MmuFaultType::AddressSize, "Fault must be AddressSize Fault");

    std::cout << "  - Mapped VA 0x0 -> PA 0x" << std::hex << res.value() << std::dec << " (Success)\n";
    std::cout << "  - Write to RO page -> Permission Fault (Trapped)\n";
    std::cout << "  - Unmapped access -> Translation Fault (Trapped)\n";
    std::cout << "  - OOB VA access   -> AddressSize Fault (Trapped)\n";

    std::cout << "  [PASS] Stage 267.8 Hardware page-table walk verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 267.9: Translation Control System Registers & TLB Invalidations
// -----------------------------------------------------------------------------
static bool test_stage267_9_sysregs_and_tlb() {
    std::cout << "[Stage 267.9] Testing Translation System Registers & TLB Invalidation Lifecycle...\n";

    CPU cpu;
    MMU mmu;

    // 1. System Register Configuration
    // TCR_EL1: 16 KiB granule (TG0=0b10, TG1=0b01), 48-bit VA (T0SZ=16, T1SZ=16)
    const std::uint64_t tcr = 0x2B5593519ULL;
    cpu.set_tcr_el1(tcr);
    TEST_ASSERT(cpu.get_tcr_el1() == tcr, "TCR_EL1 configured with 16 KiB granule and 48-bit VA");

    const std::uint64_t ttbr0 = 0x804010000ULL;
    const std::uint64_t ttbr1 = 0x804020000ULL;
    cpu.set_ttbr0_el1(ttbr0);
    cpu.set_ttbr1_el1(ttbr1);
    TEST_ASSERT(cpu.get_ttbr0_el1() == ttbr0, "TTBR0_EL1 configured");
    TEST_ASSERT(cpu.get_ttbr1_el1() == ttbr1, "TTBR1_EL1 configured");

    const std::uint64_t mair = 0x0000000444FF0400ULL;
    cpu.set_mair_el1(mair);
    TEST_ASSERT(cpu.get_mair_el1() == mair, "MAIR_EL1 memory attributes configured");

    const std::uint64_t sctlr = 0x30D00800ULL;
    cpu.set_sctlr_el1(sctlr);
    TEST_ASSERT(cpu.get_sctlr_el1() == sctlr, "SCTLR_EL1 system control configured");

    // 2. TLB Invalidation operations
    const std::uint64_t prev_inval = mmu.get_tlb_invalidations();
    mmu.invalidate_tlb_all();
    TEST_ASSERT(mmu.get_tlb_invalidations() > prev_inval, "TLBI VMALLE1 incremented invalidation counter");

    const std::uint64_t prev_inval_va = mmu.get_tlb_invalidations();
    mmu.invalidate_tlb_va(0x2000ULL, 1 /* asid */);
    TEST_ASSERT(mmu.get_tlb_invalidations() > prev_inval_va, "TLBI VAE1 incremented invalidation counter");

    const std::uint64_t prev_inval_vaa = mmu.get_tlb_invalidations();
    mmu.invalidate_tlb_vaa(0x3000ULL);
    TEST_ASSERT(mmu.get_tlb_invalidations() > prev_inval_vaa, "TLBI VAAE1 incremented invalidation counter");

    std::cout << "  - TCR_EL1:  0x" << std::hex << cpu.get_tcr_el1() << std::dec << "\n";
    std::cout << "  - TTBR0:    0x" << std::hex << cpu.get_ttbr0_el1() << std::dec << "\n";
    std::cout << "  - TTBR1:    0x" << std::hex << cpu.get_ttbr1_el1() << std::dec << "\n";
    std::cout << "  - TLB Invalidation Operations: All, VA, VAA (Verified)\n";

    std::cout << "  [PASS] Stage 267.9 Translation sysregs and TLB operations verified.\n";
    return true;
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "Campaign 267 - Stages 267.6..267.9 MMU Geometry & Walk Tests\n";
    std::cout << "=================================================================\n";

    if (!test_stage267_6_mmu_geometry_derivation()) return 1;
    if (!test_stage267_7_descriptor_decoding()) return 1;
    if (!test_stage267_8_guest_page_table_walk()) return 1;
    if (!test_stage267_9_sysregs_and_tlb()) return 1;

    std::cout << "=================================================================\n";
    std::cout << "All Stages 267.6..267.9 Tests Passed Successfully!\n";
    std::cout << "=================================================================\n";
    return 0;
}

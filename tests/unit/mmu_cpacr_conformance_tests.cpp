#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "cpu/sysreg.h"
#include "cpu/darwin_sysreg_compat.h"
#include "cpu/smp_core_state.h"
#include "memory/memory.h"
#include "reference/independent_reference_model.h"
#include "snapshot/smp_snapshot_format.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace mmu;
using namespace winios;
using namespace winios::sysreg;
using namespace winios::darwin;
using namespace winios::reference;
using namespace winios::snapshot;

// =============================================================================
// Suite 1: MmuDescriptorValidationTests
// =============================================================================
bool test_mmu_descriptor_validation() {
    MMU mmu;
    mmu.reset();

    DescriptorKind kind = DescriptorKind::Invalid;
    MmuFaultType fault = MmuFaultType::Translation;

    // 1. Bit 0 == 0 is Invalid (Translation fault)
    TEST_ASSERT(!mmu.validate_descriptor(0, 0x0ULL, false, &kind, &fault), "L0 null descriptor must be invalid");
    TEST_ASSERT(kind == DescriptorKind::Invalid && fault == MmuFaultType::Translation, "Fault must be Translation");
    TEST_ASSERT(!mmu.validate_descriptor(1, 0x2ULL, false, &kind, &fault), "L1 0b10 descriptor must be invalid");
    TEST_ASSERT(kind == DescriptorKind::Invalid && fault == MmuFaultType::Translation, "Fault must be Translation");

    // 2. Level 0: Only Table descriptors (0b11) are valid
    TEST_ASSERT(mmu.validate_descriptor(0, 0x10003ULL, false, &kind, &fault), "L0 Table must be valid");
    TEST_ASSERT(kind == DescriptorKind::Table, "Kind must be Table");
    TEST_ASSERT(!mmu.validate_descriptor(0, 0x10001ULL, false, &kind, &fault), "L0 Block (0b01) must be rejected");
    TEST_ASSERT(kind == DescriptorKind::Invalid && fault == MmuFaultType::Translation, "Fault must be Translation");

    // 3. Level 1: Block (1GB, 0b01) and Table (0b11) are valid
    // Aligned 1GB block (bits [29:12] == 0)
    TEST_ASSERT(mmu.validate_descriptor(1, 0x40000001ULL, false, &kind, &fault), "L1 1GB aligned Block must be valid");
    TEST_ASSERT(kind == DescriptorKind::Block, "Kind must be Block");
    // Misaligned 1GB block (bits [29:12] != 0)
    TEST_ASSERT(!mmu.validate_descriptor(1, 0x40001001ULL, false, &kind, &fault), "L1 Misaligned Block must be rejected");
    TEST_ASSERT(kind == DescriptorKind::Invalid && fault == MmuFaultType::AddressSize, "Fault must be AddressSize");

    TEST_ASSERT(mmu.validate_descriptor(1, 0x10003ULL, false, &kind, &fault), "L1 Table must be valid");
    TEST_ASSERT(kind == DescriptorKind::Table, "Kind must be Table");

    // 4. Level 2: Block (2MB, 0b01) and Table (0b11) are valid
    // Aligned 2MB block (bits [20:12] == 0)
    TEST_ASSERT(mmu.validate_descriptor(2, 0x00200001ULL, false, &kind, &fault), "L2 2MB aligned Block must be valid");
    TEST_ASSERT(kind == DescriptorKind::Block, "Kind must be Block");
    // Misaligned 2MB block (bits [20:12] != 0)
    TEST_ASSERT(!mmu.validate_descriptor(2, 0x00201001ULL, false, &kind, &fault), "L2 Misaligned Block must be rejected");
    TEST_ASSERT(kind == DescriptorKind::Invalid && fault == MmuFaultType::AddressSize, "Fault must be AddressSize");

    TEST_ASSERT(mmu.validate_descriptor(2, 0x10003ULL, false, &kind, &fault), "L2 Table must be valid");
    TEST_ASSERT(kind == DescriptorKind::Table, "Kind must be Table");

    // 5. Level 3: Only Page descriptors (0b11) are valid
    TEST_ASSERT(mmu.validate_descriptor(3, 0x10003ULL, false, &kind, &fault), "L3 Page must be valid");
    TEST_ASSERT(kind == DescriptorKind::Page, "Kind must be Page");
    TEST_ASSERT(!mmu.validate_descriptor(3, 0x10001ULL, false, &kind, &fault), "L3 Block (0b01) must be rejected");
    TEST_ASSERT(kind == DescriptorKind::Invalid && fault == MmuFaultType::Translation, "Fault must be Translation");

    // 6. Address bounds: bits [51:48] != 0 must produce AddressSize fault
    const std::uint64_t pa_overflow = 0x000F000000000003ULL;
    TEST_ASSERT(!mmu.validate_descriptor(3, pa_overflow, false, &kind, &fault), "PA > 48-bit must be rejected");
    TEST_ASSERT(fault == MmuFaultType::AddressSize, "Fault must be AddressSize");

    return true;
}

// =============================================================================
// Suite 2: MmuTranslationDifferentialTests
// =============================================================================
bool test_mmu_translation_differential() {
    Memory mem(0x400000); // 4 MiB memory
    ReferenceMemory ref_mem(0x400000);

    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    const std::uint64_t l0_table = 0x10000;
    const std::uint64_t l1_table = 0x11000;
    const std::uint64_t l2_table = 0x12000;
    const std::uint64_t l3_table = 0x13000;

    auto write_both = [&](std::uint64_t paddr, std::uint64_t val) {
        mem.write_u64(static_cast<std::size_t>(paddr), val);
        ref_mem.write_u64(paddr, val);
    };

    write_both(l0_table, l1_table | 0x3u);
    write_both(l1_table, l2_table | 0x3u);
    write_both(l2_table, l3_table | 0x3u);

    // L3 Page: VA 0x0000 -> PA 0x50000 (RW, AF=1)
    const std::uint64_t l3_desc = 0x50000ULL | (1ULL << 10u) | (1ULL << 6u) | 0x3u;
    write_both(l3_table, l3_desc);

    mmu.set_ttbr0_el1(l0_table);
    mmu.set_tcr_el1(16ULL); // T0SZ = 16
    mmu.set_sctlr_el1(0x00C50839ULL);

    // 1. First byte of page (VA 0x0000)
    TranslationFault fault{};
    auto p1 = mmu.translate(mem, 0x0000ULL, AccessKind::Read, 1, &fault);
    auto r1 = ReferencePageTableWalker::walk_stage1(ref_mem, l0_table, 16ULL, 0x00C50839ULL, 0x0000ULL, 0, 1);
    TEST_ASSERT(p1.has_value() && r1.success, "Page first byte translation must succeed");
    TEST_ASSERT(p1.value() == 0x50000ULL && r1.physical_address == 0x50000ULL, "PA must be 0x50000");

    // 2. Last byte of page (VA 0x0FFF)
    auto p2 = mmu.translate(mem, 0x0FFFULL, AccessKind::Read, 1, &fault);
    auto r2 = ReferencePageTableWalker::walk_stage1(ref_mem, l0_table, 16ULL, 0x00C50839ULL, 0x0FFFULL, 0, 1);
    TEST_ASSERT(p2.has_value() && r2.success, "Page last byte translation must succeed");
    TEST_ASSERT(p2.value() == 0x50FFFULL && r2.physical_address == 0x50FFFULL, "PA must be 0x50FFF");

    // 3. Unmapped page (VA 0x1000) -> Translation fault Level 3
    auto p3 = mmu.translate(mem, 0x1000ULL, AccessKind::Read, 1, &fault);
    auto r3 = ReferencePageTableWalker::walk_stage1(ref_mem, l0_table, 16ULL, 0x00C50839ULL, 0x1000ULL, 0, 1);
    TEST_ASSERT(!p3.has_value() && !r3.success, "Unmapped VA must fail");
    TEST_ASSERT(fault.type == MmuFaultType::Translation && fault.level == 3, "Production must report Translation L3");
    TEST_ASSERT(r3.fault_type == ReferenceMmuFaultType::Translation && r3.fault_level == 3, "Reference must report Translation L3");

    // 4. 2MB Block mapping at L2
    const std::uint64_t l2_block_desc = 0x00200000ULL | (1ULL << 10u) | (1ULL << 6u) | 0x1u; // PA 0x200000
    write_both(l2_table + 1 * 8, l2_block_desc); // VA 0x200000
    auto pb = mmu.translate(mem, 0x200050ULL, AccessKind::Read, 1, &fault);
    auto rb = ReferencePageTableWalker::walk_stage1(ref_mem, l0_table, 16ULL, 0x00C50839ULL, 0x200050ULL, 0, 1);
    TEST_ASSERT(pb.has_value() && rb.success, "2MB block translation must succeed");
    TEST_ASSERT(pb.value() == 0x200050ULL && rb.physical_address == 0x200050ULL, "2MB Block PA must match");

    return true;
}

// =============================================================================
// Suite 3: MmuPermissionTests
// =============================================================================
bool test_mmu_permissions() {
    Memory mem(0x200000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(false);

    const std::uint64_t l0_table = 0x10000;
    const std::uint64_t l1_table = 0x11000;
    const std::uint64_t l2_table = 0x12000;
    const std::uint64_t l3_table = 0x13000;

    mem.write_u64(static_cast<std::size_t>(l0_table), l1_table | 0x3u);
    mem.write_u64(static_cast<std::size_t>(l1_table), l2_table | 0x3u);
    mem.write_u64(static_cast<std::size_t>(l2_table), l3_table | 0x3u);

    mmu.set_ttbr0_el1(l0_table);
    mmu.set_tcr_el1(16ULL);
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};

    // Case 1: Read-Only page (AP[2] = 1, bit 7 = 1)
    // AP = 0b10 (Privileged RO, Unprivileged None) -> AP[2]=1, AP[1]=0
    const std::uint64_t ro_desc = 0x40000ULL | (1ULL << 10u) | (1ULL << 7u) | 0x3u;
    mem.write_u64(static_cast<std::size_t>(l3_table), ro_desc);

    // EL1 Read succeeds
    auto res_ro_read = mmu.translate(mem, 0x0000ULL, AccessKind::Read, 1, &fault);
    TEST_ASSERT(res_ro_read.has_value(), "Privileged read of RO page must succeed");

    // EL1 Write fails with Permission fault Level 3
    auto res_ro_write = mmu.translate(mem, 0x0000ULL, AccessKind::Write, 1, &fault);
    TEST_ASSERT(!res_ro_write.has_value(), "Write to RO page must fail");
    TEST_ASSERT(fault.type == MmuFaultType::Permission && fault.level == 3, "Must be Permission L3");

    // EL0 Read fails because AP[1] = 0 (Unprivileged None)
    auto res_el0_read = mmu.translate(mem, 0x0000ULL, AccessKind::Read, 0, &fault);
    TEST_ASSERT(!res_el0_read.has_value(), "EL0 read without AP[1] must fail");
    TEST_ASSERT(fault.type == MmuFaultType::Permission, "Must be Permission fault");

    // Case 2: Access Flag AF=0 (bit 10 = 0)
    const std::uint64_t no_af_desc = 0x40000ULL | (1ULL << 6u) | 0x3u; // AF=0
    mem.write_u64(static_cast<std::size_t>(l3_table + 8), no_af_desc); // VA 0x1000
    auto res_af = mmu.translate(mem, 0x1000ULL, AccessKind::Read, 1, &fault);
    TEST_ASSERT(!res_af.has_value(), "Access with AF=0 must fault");
    TEST_ASSERT(fault.type == MmuFaultType::AccessFlag && fault.level == 3, "Must be AccessFlag L3");

    // Case 3: Execute-Never UXN (bit 54) and PXN (bit 53)
    const std::uint64_t xn_desc = 0x40000ULL | (1ULL << 10u) | (1ULL << 6u) | (1ULL << 54u) | (1ULL << 53u) | 0x3u;
    mem.write_u64(static_cast<std::size_t>(l3_table + 16), xn_desc); // VA 0x2000
    auto res_exec_el0 = mmu.translate(mem, 0x2000ULL, AccessKind::Execute, 0, &fault);
    TEST_ASSERT(!res_exec_el0.has_value(), "Execution with UXN must fault");
    auto res_exec_el1 = mmu.translate(mem, 0x2000ULL, AccessKind::Execute, 1, &fault);
    TEST_ASSERT(!res_exec_el1.has_value(), "Execution with PXN must fault");

    return true;
}

// =============================================================================
// Suite 4: MmuFaultSyndromeTests
// =============================================================================
bool test_mmu_fault_syndrome() {
    TranslationFault f{};

    // Translation fault Level 3: FSC = 0x04 + 3 = 0x07
    f.type = MmuFaultType::Translation;
    f.level = 3;
    TEST_ASSERT(f.fsc() == 0x07u, "Translation L3 FSC must be 0x07");

    // Access Flag fault Level 2: FSC = 0x08 + 2 = 0x0A
    f.type = MmuFaultType::AccessFlag;
    f.level = 2;
    TEST_ASSERT(f.fsc() == 0x0Au, "Access Flag L2 FSC must be 0x0A");

    // Permission fault Level 3: FSC = 0x0C + 3 = 0x0F
    f.type = MmuFaultType::Permission;
    f.level = 3;
    TEST_ASSERT(f.fsc() == 0x0Fu, "Permission L3 FSC must be 0x0F");

    // Address Size fault Level 0: FSC = 0x00 + 0 = 0x00
    f.type = MmuFaultType::AddressSize;
    f.level = 0;
    TEST_ASSERT(f.fsc() == 0x00u, "Address Size L0 FSC must be 0x00");

    // Data Abort ESR encoding verification:
    // EC = 0x24 (Data Abort from lower EL) -> bits [31:26] = 0x24 -> (0x24 << 26) = 0x90000000
    SynchronousException ex;
    ex.ec = ExceptionClass::DataAbortLower;
    ex.iss = f.fsc();
    ex.instruction_length_4 = true;
    const std::uint32_t esr = ex.esr();
    TEST_ASSERT((esr >> 26u) == 0x24u, "ESR EC must be DataAbortLower (0x24)");
    TEST_ASSERT((esr & 0x3Fu) == 0x00u, "ESR ISS FSC must match");

    return true;
}

// =============================================================================
// Suite 5: TlbInvalidationTests
// =============================================================================
bool test_tlb_invalidation() {
    Memory mem(0x200000);
    MMU mmu;
    mmu.reset();
    mmu.set_tlb_enabled(true);

    const std::uint64_t l0_table = 0x10000;
    const std::uint64_t l1_table = 0x11000;
    const std::uint64_t l2_table = 0x12000;
    const std::uint64_t l3_table = 0x13000;

    mem.write_u64(static_cast<std::size_t>(l0_table), l1_table | 0x3u);
    mem.write_u64(static_cast<std::size_t>(l1_table), l2_table | 0x3u);
    mem.write_u64(static_cast<std::size_t>(l2_table), l3_table | 0x3u);

    // Initial mapping: VA 0x0000 -> PA 0x50000
    mem.write_u64(static_cast<std::size_t>(l3_table), 0x50000ULL | (1ULL << 10u) | (1ULL << 6u) | 0x3u);

    mmu.set_ttbr0_el1(l0_table);
    mmu.set_tcr_el1(16ULL);
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};
    auto r1 = mmu.translate(mem, 0x0000ULL, AccessKind::Read, 1, &fault);
    TEST_ASSERT(r1.has_value() && r1.value() == 0x50000ULL, "Initial translation must hit 0x50000");
    TEST_ASSERT(mmu.get_tlb_entry_count() == 1, "TLB must contain 1 entry");

    // Modify PTE in RAM: VA 0x0000 -> PA 0x60000
    mem.write_u64(static_cast<std::size_t>(l3_table), 0x60000ULL | (1ULL << 10u) | (1ULL << 6u) | 0x3u);

    // Without invalidation, TLB must return cached old translation 0x50000
    auto r2 = mmu.translate(mem, 0x0000ULL, AccessKind::Read, 1, &fault);
    TEST_ASSERT(r2.has_value() && r2.value() == 0x50000ULL, "Cached translation must return old PA 0x50000");

    // Perform architectural TLB invalidation: invalidate all
    mmu.invalidate_tlb_all();
    TEST_ASSERT(mmu.get_tlb_entry_count() == 0, "TLB must be empty after invalidate_all");

    // Next translation must re-walk page table and return new PA 0x60000
    auto r3 = mmu.translate(mem, 0x0000ULL, AccessKind::Read, 1, &fault);
    TEST_ASSERT(r3.has_value() && r3.value() == 0x60000ULL, "Post-invalidation translation must hit new PA 0x60000");

    return true;
}

// =============================================================================
// Suite 6: CpacrAccessControlTests
// =============================================================================
bool test_cpacr_access_control() {
    CPU cpu;
    cpu.reset();

    // 1. Reset value must be 0
    TEST_ASSERT(cpu.get_cpacr_el1() == 0x0ULL, "CPACR_EL1 reset value must be 0");

    // 2. Registry lookup
    const auto* desc = find_descriptor_by_name("CPACR_EL1");
    TEST_ASSERT(desc != nullptr, "CPACR_EL1 must exist in sysreg descriptor registry");
    TEST_ASSERT(desc->key == 0x4082u, "CPACR_EL1 key must be 0x4082");
    TEST_ASSERT(desc->min_el == 1, "CPACR_EL1 min_el must be 1 (EL1)");

    // 3. FPEN evaluation logic
    // FPEN = 0b00: disabled at EL0 and EL1
    cpu.set_cpacr_el1(0x0ULL);
    TEST_ASSERT(!cpu.is_fp_simd_enabled(0), "EL0 must be disabled with FPEN=0b00");
    TEST_ASSERT(!cpu.is_fp_simd_enabled(1), "EL1 must be disabled with FPEN=0b00");

    // FPEN = 0b01: trapped at EL0, enabled at EL1
    cpu.set_cpacr_el1(0x00100000ULL); // bit 20 = 1
    TEST_ASSERT(!cpu.is_fp_simd_enabled(0), "EL0 must be disabled with FPEN=0b01");
    TEST_ASSERT(cpu.is_fp_simd_enabled(1), "EL1 must be enabled with FPEN=0b01");

    // FPEN = 0b10: trapped at EL0 and EL1
    cpu.set_cpacr_el1(0x00200000ULL); // bit 21 = 1
    TEST_ASSERT(!cpu.is_fp_simd_enabled(0), "EL0 must be disabled with FPEN=0b10");
    TEST_ASSERT(!cpu.is_fp_simd_enabled(1), "EL1 must be disabled with FPEN=0b10");

    // FPEN = 0b11: enabled at EL0 and EL1
    cpu.set_cpacr_el1(0x00300000ULL); // bits [21:20] = 0b11
    TEST_ASSERT(cpu.is_fp_simd_enabled(0), "EL0 must be enabled with FPEN=0b11");
    TEST_ASSERT(cpu.is_fp_simd_enabled(1), "EL1 must be enabled with FPEN=0b11");

    return true;
}

// =============================================================================
// Suite 7: FpSimdTrapTests
// =============================================================================
bool test_fp_simd_traps() {
    Memory mem(0x10000);
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x4000ULL);

    // Setup: FADD S2, S0, S1 (0x1E212802u)
    // S0 = 1.0f (0x3F800000), S1 = 2.0f (0x40000000)
    cpu.write_s_register(0, 0x3F800000u);
    cpu.write_s_register(1, 0x40000000u);
    cpu.write_s_register(2, 0x0u);

    // Case 1: Execute with FPEN = 0b00 at EL1 -> Must Trap to EL1 with EC=0x07
    cpu.set_cpacr_el1(0x0ULL); // FPEN = 0b00
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(0x1000ULL);
    mem.write_u32(0x1000, 0x1E212802u); // FADD S2, S0, S1

    cpu.step(mem);

    // Trapped state assertions:
    TEST_ASSERT(cpu.read_s_register(2) == 0x0u, "Trapped FADD must NOT update destination register S2");
    TEST_ASSERT(cpu.get_elr_el1() == 0x1000ULL, "ELR_EL1 must hold faulting instruction PC (0x1000)");
    TEST_ASSERT((cpu.get_esr_el1() >> 26u) == 0x07u, "ESR_EL1 EC must be FpSimdAccessTrap (0x07)");
    TEST_ASSERT(cpu.get_program_counter() == 0x4200ULL, "Trap from EL1h must vector to VBAR_EL1 + 0x200");

    // Case 2: Trap from EL0 with FPEN = 0b01 -> Vector to VBAR_EL1 + 0x400
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x4000ULL);
    cpu.set_cpacr_el1(0x00100000ULL); // FPEN = 0b01 (traps EL0, enables EL1)
    cpu.set_current_el(0);
    cpu.set_program_counter(0x2000ULL);
    mem.write_u32(0x2000, 0x1E212802u); // FADD S2, S0, S1

    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 1, "Exception must transition to EL1");
    TEST_ASSERT(cpu.get_elr_el1() == 0x2000ULL, "ELR_EL1 must hold faulting PC 0x2000");
    TEST_ASSERT((cpu.get_esr_el1() >> 26u) == 0x07u, "ESR_EL1 EC must be 0x07");
    TEST_ASSERT(cpu.get_program_counter() == 0x4400ULL, "Trap from EL0 must vector to VBAR_EL1 + 0x400");

    // Case 3: Enable FPEN = 0b11 at EL0 -> Must execute successfully without trap
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_cpacr_el1(0x00300000ULL); // FPEN = 0b11 (enabled at EL0 and EL1)
    cpu.set_current_el(0);
    cpu.write_s_register(0, 0x3F800000u); // 1.0f
    cpu.write_s_register(1, 0x40000000u); // 2.0f
    cpu.set_program_counter(0x2000ULL);

    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 0, "Execution must remain at EL0");
    TEST_ASSERT(cpu.read_s_register(2) == 0x40400000u, "S2 must hold 3.0f (0x40400000)");
    TEST_ASSERT(cpu.get_program_counter() == 0x2004ULL, "PC must increment normally to 0x2004");

    return true;
}

// =============================================================================
// Suite 8: FpSimdStateTests
// =============================================================================
bool test_fp_simd_state() {
    CPU cpu1;
    CPU cpu2;
    cpu1.reset();
    cpu2.reset();

    // 1. Vector register write/read across sizes
    cpu1.write_vector_register(0, 0x1122334455667788ULL, 0x99AABBCCDDEEFF00ULL);
    const auto v0 = cpu1.read_vector_register(0);
    TEST_ASSERT(v0.low == 0x1122334455667788ULL && v0.high == 0x99AABBCCDDEEFF00ULL, "128-bit vector reg match");

    // 2. FPCR and FPSR
    cpu1.set_fpcr(0x01000000u); // AHP bit
    cpu1.set_fpsr(0x00000008u); // Overflow flag
    TEST_ASSERT(cpu1.get_fpcr() == 0x01000000u, "FPCR match");
    TEST_ASSERT(cpu1.get_fpsr() == 0x00000008u, "FPSR match");

    // 3. Multi-core state isolation
    TEST_ASSERT(cpu2.read_vector_register(0).low == 0ULL, "Core 2 V0 must be zero");
    TEST_ASSERT(cpu2.get_fpcr() == 0u, "Core 2 FPCR must be zero");

    return true;
}

// =============================================================================
// Suite 9: DarwinCompatibilityRegistryTests
// =============================================================================
bool test_darwin_compatibility_registry() {
    const std::size_t count = DarwinCompatibilityRegistry::get_entry_count();
    TEST_ASSERT(count == 20, "Darwin compatibility registry must contain 20 entries");

    // Lookups
    const auto* tpidr_el0 = DarwinCompatibilityRegistry::find_by_name("TPIDR_EL0");
    TEST_ASSERT(tpidr_el0 != nullptr, "TPIDR_EL0 must exist in Darwin registry");
    TEST_ASSERT(tpidr_el0->classification == Classification::GenericArchitectural, "TPIDR_EL0 is GenericArchitectural");

    const auto* commpage_ptr = DarwinCompatibilityRegistry::find_by_key(0x5E83u);
    TEST_ASSERT(commpage_ptr != nullptr, "TPIDRRO_EL0 must be found by key 0x5E83");
    TEST_ASSERT(commpage_ptr->classification == Classification::PubliclyDocumentedDarwin, "TPIDRRO_EL0 is Darwin documented");

    const auto* apsts = DarwinCompatibilityRegistry::find_by_name("APSTS_EL1");
    TEST_ASSERT(apsts != nullptr, "APSTS_EL1 must exist in Darwin research registry");
    TEST_ASSERT(apsts->classification == Classification::ResearchOnly, "APSTS_EL1 is ResearchOnly");
    TEST_ASSERT(apsts->status == CompatibilityStatus::Deferred, "APSTS_EL1 must be Deferred");

    // Verify isolation: research-only entries must NOT exist in the active sysreg registry
    const auto* active_apsts = find_descriptor_by_name("APSTS_EL1");
    TEST_ASSERT(active_apsts == nullptr, "Research-only APSTS_EL1 must NOT be in active CPU sysreg registry");

    // Profile hash determinism
    DarwinProfileConfig cfg1{};
    cfg1.mode = ProfileMode::StandardAArch64;
    DarwinProfileConfig cfg2{};
    cfg2.mode = ProfileMode::StandardAArch64;
    TEST_ASSERT(cfg1.compute_profile_hash() == cfg2.compute_profile_hash(), "Identical profiles must produce identical hash");

    cfg2.mode = ProfileMode::DarwinResearchAudit;
    TEST_ASSERT(cfg1.compute_profile_hash() != cfg2.compute_profile_hash(), "Divergent profile must produce distinct hash");

    return true;
}

// =============================================================================
// Suite 10: Stage37SnapshotReplayTests
// =============================================================================
bool test_stage37_snapshot_replay() {
    CPU cpu;
    cpu.reset();
    cpu.set_cpacr_el1(0x00300000ULL); // FPEN = 0b11
    cpu.set_tpidr_el0(0xCAFE0001ULL);
    cpu.set_tpidr_el1(0xCAFE0002ULL);
    cpu.set_tpidrro_el0(0xCAFE0003ULL);

    SmpCoreState state1;
    state1.capture_from_cpu(cpu);
    TEST_ASSERT(state1.cpacr_el1 == 0x00300000ULL, "Captured CPACR_EL1 must match CPU");

    // Serialize
    const auto buf = serialize_core_architectural_state(state1);
    TEST_ASSERT(buf.size() == kCoreArchPayloadSize, "Serialized core state includes legacy, debug, and PMU blocks");

    // Deserialize into state2
    SmpCoreState state2;
    const auto res = deserialize_core_architectural_state(buf.data(), buf.size(), state2);
    TEST_ASSERT(res.success, "Deserialization must succeed");
    TEST_ASSERT(state2.cpacr_el1 == 0x00300000ULL, "Deserialized CPACR_EL1 must match");
    TEST_ASSERT(state1 == state2, "States must be bitwise identical");

    // Canonical Hash Sensitivity
    const std::uint64_t hash1 = state1.compute_canonical_hash();
    state2.cpacr_el1 ^= 0x00100000ULL;
    const std::uint64_t hash2 = state2.compute_canonical_hash();
    TEST_ASSERT(hash1 != hash2, "Mutating CPACR_EL1 must perturb canonical cluster hash");

    // Backward compatibility: 1056 byte legacy buffer
    SmpCoreState legacy_state;
    const auto legacy_res = deserialize_core_architectural_state(buf.data(), 1056, legacy_state);
    TEST_ASSERT(legacy_res.success, "1056-byte buffer must deserialize gracefully");
    TEST_ASSERT(legacy_state.tpidr_el0 == 0ULL, "Legacy buffer defaults TPIDR to 0");

    // Truncated buffer (< 1056)
    SmpCoreState malformed_state;
    const auto fail_res = deserialize_core_architectural_state(buf.data(), 1000, malformed_state);
    TEST_ASSERT(!fail_res.success, "Buffer < 1056 must fail validation");

    return true;
}

// =============================================================================
// Suite 11: Stage37FuzzTests
// =============================================================================
bool test_stage37_fuzz() {
    MMU mmu;
    mmu.reset();

    // 1. MMU Descriptor Fuzzing (Seed 0x4D4D5531 = "MMU1", 1000 iterations)
    std::mt19937_64 rng_mmu(0x4D4D5531ULL);
    for (int i = 0; i < 1000; ++i) {
        const std::uint64_t desc = rng_mmu();
        const unsigned level = static_cast<unsigned>(desc % 4u);
        DescriptorKind kind;
        MmuFaultType fault;
        // Function must never crash, throw, or loop indefinitely
        mmu.validate_descriptor(level, desc, false, &kind, &fault);
    }

    // 2. CPACR & FPEN Access Control Fuzzing (Seed 0x4650454E = "FPEN", 1000 iterations)
    std::mt19937_64 rng_fpen(0x4650454EULL);
    CPU cpu;
    cpu.reset();
    for (int i = 0; i < 1000; ++i) {
        const std::uint64_t cpacr = rng_fpen();
        const unsigned el = static_cast<unsigned>(rng_fpen() % 2u);
        cpu.set_cpacr_el1(cpacr);
        const bool enabled = cpu.is_fp_simd_enabled(el);
        const std::uint32_t fpen = static_cast<std::uint32_t>((cpacr >> 20u) & 0x3u);
        if (el == 0) {
            TEST_ASSERT(enabled == (fpen == 0b11), "Fuzz FPEN EL0 rule invariant");
        } else {
            TEST_ASSERT(enabled == ((fpen & 1u) != 0), "Fuzz FPEN EL1 rule invariant");
        }
    }

    // 3. Snapshot Truncation / Corruption Fuzzing (Seed 0x534E4150 = "SNAP", 1000 iterations)
    std::mt19937_64 rng_snap(0x534E4150ULL);
    std::vector<std::uint8_t> fuzz_buf(1240);
    for (int i = 0; i < 1000; ++i) {
        for (auto& b : fuzz_buf) b = static_cast<std::uint8_t>(rng_snap() & 0xFFu);
        const std::size_t test_len = static_cast<std::size_t>(rng_snap() % 1240u);
        SmpCoreState fstate;
        const auto sres = deserialize_core_architectural_state(fuzz_buf.data(), test_len, fstate);
        if (test_len < 1056) {
            TEST_ASSERT(!sres.success, "Sub-1056 byte buffer must always fail");
        } else {
            TEST_ASSERT(sres.success, "Valid-sized buffer must deserialize safely");
        }
    }

    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================
int main() {
    std::cout << "========================================================\n";
    std::cout << "WinIOS Phase 52 Stage 37 MMU & CPACR Conformance Suite\n";
    std::cout << "========================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](const char* name, bool (*fn)()) {
        std::cout << "Running " << name << " ... ";
        if (fn()) {
            std::cout << "PASS\n";
            ++passed;
        } else {
            std::cout << "FAIL\n";
            ++failed;
        }
    };

    run_test("MmuDescriptorValidationTests", test_mmu_descriptor_validation);
    run_test("MmuTranslationDifferentialTests", test_mmu_translation_differential);
    run_test("MmuPermissionTests", test_mmu_permissions);
    run_test("MmuFaultSyndromeTests", test_mmu_fault_syndrome);
    run_test("TlbInvalidationTests", test_tlb_invalidation);
    run_test("CpacrAccessControlTests", test_cpacr_access_control);
    run_test("FpSimdTrapTests", test_fp_simd_traps);
    run_test("FpSimdStateTests", test_fp_simd_state);
    run_test("DarwinCompatibilityRegistryTests", test_darwin_compatibility_registry);
    run_test("Stage37SnapshotReplayTests", test_stage37_snapshot_replay);
    run_test("Stage37FuzzTests", test_stage37_fuzz);

    std::cout << "========================================================\n";
    std::cout << "Stage 37 Suite Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "========================================================\n";

    return (failed == 0) ? 0 : 1;
}

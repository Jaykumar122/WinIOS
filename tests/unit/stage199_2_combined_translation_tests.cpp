#include <cassert>
#include <iostream>
#include <vector>

#include "cpu/mmu.h"
#include "memory/memory.h"
#include "memory/stage2_mmu.h"
#include "memory/unified_memory.h"

using namespace mmu;
using namespace winios::mmu;
using namespace winios::memory;

// Helper to set up Stage-1 16KB page translation table in memory
static void setup_stage1_page_16k(
    Memory& mem,
    std::uint64_t ttbr0_pa,
    std::uint64_t va,
    std::uint64_t ipa,
    bool readable,
    bool writable,
    bool el0_accessible,
    bool executable) {

    // 16KB granule:
    (void)readable;
    // L2 index: bits [35:25]
    // L3 index: bits [24:14]
    const std::uint64_t l2_idx = (va >> 25u) & 0x7FFu;
    const std::uint64_t l2_entry = ttbr0_pa + l2_idx * 8u;

    std::uint64_t l3_table_pa = ttbr0_pa + 16384ULL;
    std::uint64_t l2_desc = mem.read_u64(static_cast<std::size_t>(l2_entry));
    if ((l2_desc & 0x3u) == 0x3u) {
        l3_table_pa = l2_desc & 0x0000FFFFFFFFC000ULL;
    } else {
        l2_desc = (l3_table_pa & 0x0000FFFFFFFFC000ULL) | 0x3u;
        mem.write_u64(static_cast<std::size_t>(l2_entry), l2_desc);
    }

    const std::uint64_t l3_idx = (va >> 14u) & 0x7FFu;
    const std::uint64_t l3_entry = l3_table_pa + l3_idx * 8u;

    std::uint64_t l3_desc = (ipa & 0x0000FFFFFFFFC000ULL) | 0x3u;
    l3_desc |= (1ULL << 10u); // AF = 1

    // AP[2:1] at bits [7:6]:
    // AP[2] (bit 7, value 0b10): 1 for Read-Only, 0 for Read/Write
    // AP[1] (bit 6, value 0b01): 1 for EL0 access, 0 for EL1-only
    std::uint64_t ap = 0;
    if (!writable) ap |= 0b10ULL;
    if (el0_accessible) ap |= 0b01ULL;
    l3_desc |= (ap << 6u);

    // UXN/PXN
    if (!executable) {
        l3_desc |= (1ULL << 54u); // UXN
        l3_desc |= (1ULL << 53u); // PXN
    }

    // Normal memory attr index 0
    l3_desc |= (0ULL << 2u);

    mem.write_u64(static_cast<std::size_t>(l3_entry), l3_desc);
}

void test_two_stage_successful_translation() {
    Memory mem(1024 * 1024 * 8); // 8 MB RAM
    MMU mmu;

    // Configure Stage 1:
    // SCTLR_EL1.M = 1
    mmu.set_sctlr_el1(0x00C50839ULL);
    // TCR_EL1: TG0 = 0b10 (16KB), T0SZ = 16 (48-bit VA)
    mmu.set_tcr_el1((0b10ULL << 14u) | 16u);
    mmu.set_ttbr0_el1(0x10000ULL);
    mmu.set_mair_el1(0x00000000000000FFULL); // Normal WB memory at Attr0

    // Configure Stage 2:
    // HCR_EL2.VM = 1
    mmu.set_hcr_el2(0x1ULL);
    // VTCR_EL2: TG0 = 0b10 (16KB), SL0 = 0b01 (Level 2), T0SZ = 16
    mmu.set_vtcr_el2(0x80000000ULL | (0b10ULL << 14u) | (0b01ULL << 6u) | 16u);
    mmu.set_vttbr_el2((10ULL << 48u) | 0x30000ULL);

    const std::uint64_t va  = 0x10000000ULL;
    const std::uint64_t ipa = 0x00080000ULL;
    const std::uint64_t pa  = 0x00200000ULL;

    // Stage 1 maps VA -> IPA (RW, EL1)
    setup_stage1_page_16k(mem, 0x10000ULL, va, ipa, true, true, false, true);

    // Stage 2 maps IPA -> PA (RW)
    Stage2TranslationEngine::map_page_16k(mem, 0x30000ULL, ipa, pa, true, true, true);

    // Translate VA
    TranslationFault fault{};
    auto res_pa = mmu.translate(mem, va + 0x48, AccessKind::Read, 1, &fault);
    assert(res_pa.has_value());
    assert(res_pa.value() == pa + 0x48);

    auto res_write = mmu.translate(mem, va + 0x100, AccessKind::Write, 1, &fault);
    assert(res_write.has_value());
    assert(res_write.value() == pa + 0x100);

    std::cout << "[PASS] test_two_stage_successful_translation" << std::endl;
}

void test_permission_combination_and_fault_classification() {
    Memory mem(1024 * 1024 * 8);
    MMU mmu;

    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((0b10ULL << 14u) | 16u);
    mmu.set_ttbr0_el1(0x10000ULL);
    mmu.set_mair_el1(0xFFULL);

    mmu.set_hcr_el2(0x1ULL);
    mmu.set_vtcr_el2(0x80000000ULL | (0b10ULL << 14u) | (0b01ULL << 6u) | 16u);
    mmu.set_vttbr_el2((1ULL << 48u) | 0x30000ULL);

    // Scenario 1: Stage-1 is RW, Stage-2 is RO
    const std::uint64_t va1  = 0x20000000ULL;
    const std::uint64_t ipa1 = 0x000A0000ULL;
    const std::uint64_t pa1  = 0x00300000ULL;

    setup_stage1_page_16k(mem, 0x10000ULL, va1, ipa1, true, true, false, true); // Stage 1 RW
    Stage2TranslationEngine::map_page_16k(mem, 0x30000ULL, ipa1, pa1, true, false, true); // Stage 2 RO

    // Read succeeds
    TranslationFault f1{};
    auto r1 = mmu.translate(mem, va1, AccessKind::Read, 1, &f1);
    assert(r1.has_value());

    // Write fails with Stage-2 Permission fault
    auto w1 = mmu.translate(mem, va1, AccessKind::Write, 1, &f1);
    assert(!w1.has_value());
    assert(f1.is_stage2);
    assert(f1.fault_ipa == ipa1);
    assert(f1.type == MmuFaultType::Permission);

    // Scenario 2: Stage-1 is RO, Stage-2 is RW
    const std::uint64_t va2  = 0x30000000ULL;
    const std::uint64_t ipa2 = 0x000C0000ULL;
    const std::uint64_t pa2  = 0x00400000ULL;

    setup_stage1_page_16k(mem, 0x10000ULL, va2, ipa2, true, false, false, true); // Stage 1 RO
    Stage2TranslationEngine::map_page_16k(mem, 0x30000ULL, ipa2, pa2, true, true, true);  // Stage 2 RW

    TranslationFault f2{};
    auto w2 = mmu.translate(mem, va2, AccessKind::Write, 1, &f2);
    assert(!w2.has_value());
    // Must be Stage-1 fault!
    assert(!f2.is_stage2);
    assert(f2.type == MmuFaultType::Permission);

    // Scenario 3: Unmapped Stage 2 produces Stage-2 Translation fault
    const std::uint64_t va3  = 0x40000000ULL;
    const std::uint64_t ipa3 = 0x000E0000ULL; // not mapped in Stage 2
    setup_stage1_page_16k(mem, 0x10000ULL, va3, ipa3, true, true, false, true);

    TranslationFault f3{};
    auto r3 = mmu.translate(mem, va3, AccessKind::Read, 1, &f3);
    assert(!r3.has_value());
    assert(f3.is_stage2);
    assert(f3.fault_ipa == ipa3);
    assert(f3.type == MmuFaultType::Translation);

    std::cout << "[PASS] test_permission_combination_and_fault_classification" << std::endl;
}

void test_unified_memory_view_stage2_abort() {
    Memory mem(1024 * 1024 * 8);
    MMU mmu;

    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((0b10ULL << 14u) | 16u);
    mmu.set_ttbr0_el1(0x10000ULL);
    mmu.set_mair_el1(0xFFULL);

    mmu.set_hcr_el2(0x1ULL);
    mmu.set_vtcr_el2(0x80000000ULL | (0b10ULL << 14u) | (0b01ULL << 6u) | 16u);
    mmu.set_vttbr_el2((5ULL << 48u) | 0x30000ULL);

    const std::uint64_t va  = 0x50000000ULL;
    const std::uint64_t ipa = 0x000F0000ULL;
    const std::uint64_t pa  = 0x00500000ULL;

    setup_stage1_page_16k(mem, 0x10000ULL, va, ipa, true, true, false, true);
    // Stage 2 is RO
    Stage2TranslationEngine::map_page_16k(mem, 0x30000ULL, ipa, pa, true, false, true);

    UnifiedMemoryView uview(mem, &mmu, 1 /* current_el */);

    // Reading u32 succeeds
    mem.write_u32(static_cast<std::size_t>(pa), 0xCAFEBABE);
    std::uint32_t val = uview.read_u32(va);
    assert(val == 0xCAFEBABE);
    (void)val;

    // Writing u32 must throw SynchronousExceptionSignal targeting EL2
    bool caught_exception = false;
    try {
        uview.write_u32(va, 0x12345678);
    } catch (const SynchronousExceptionSignal& sig) {
        caught_exception = true;
        assert(sig.exception.target_el == 2);
        assert(sig.exception.has_fault_ipa);
        assert(sig.exception.fault_ipa == ipa);
        (void)sig;
    }
    assert(caught_exception);

    std::cout << "[PASS] test_unified_memory_view_stage2_abort" << std::endl;
}

int main() {
    std::cout << "--- Stage 199.2 Combined Stage-1 and Stage-2 Translation Tests ---" << std::endl;
    test_two_stage_successful_translation();
    test_permission_combination_and_fault_classification();
    test_unified_memory_view_stage2_abort();
    std::cout << "All Stage 199.2 tests PASSED!" << std::endl;
    return 0;
}

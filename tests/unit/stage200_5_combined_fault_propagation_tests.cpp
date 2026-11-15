// ==============================================================================
// WinIOS Campaign 200 Stage 5: Combined Stage-1/Stage-2 Fault Propagation Tests
// ==============================================================================
// Verifies:
// 1. Stage-1 translation fault delivery (unmapped VA, is_stage2 = false)
// 2. Stage-2 translation fault delivery (unmapped IPA, is_stage2 = true, fault_ipa)
// 3. Stage-1 permission fault (RO in S1, RW in S2 -> S1 fault)
// 4. Stage-2 permission fault (RW in S1, RO in S2 -> S2 fault)
// 5. Combined execute-never permissions (S1 exec + S2 XN -> S2 fault)
// 6. EL0/EL1 privilege permission fault at Stage 1
// 7. Fault metadata integrity (virtual_address, fault_ipa, access, is_write, is_instruction)
// 8. Memory side-effect isolation (failed translations perform no memory writes)
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>

#include "cpu/mmu.h"
#include "memory/memory.h"
#include "memory/stage2_mmu.h"

using namespace mmu;
using namespace winios::mmu;

static void setup_stage1_page_16k(
    Memory& mem,
    std::uint64_t ttbr0_pa,
    std::uint64_t va,
    std::uint64_t ipa,
    bool writable,
    bool el0_accessible,
    bool executable) {

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

    std::uint64_t ap = 0;
    if (!writable) ap |= 0b10ULL;
    if (el0_accessible) ap |= 0b01ULL;
    l3_desc |= (ap << 6u);

    if (!executable) {
        l3_desc |= (1ULL << 54u); // UXN
        l3_desc |= (1ULL << 53u); // PXN
    }

    l3_desc |= (0ULL << 2u); // Normal memory index 0
    mem.write_u64(static_cast<std::size_t>(l3_entry), l3_desc);
}

static void configure_two_stage_environment(MMU& mmu, std::uint64_t s1_base, std::uint64_t s2_base, std::uint16_t vmid = 1) {
    // SCTLR_EL1.M = 1
    mmu.set_sctlr_el1(0x00C50839ULL);
    // TCR_EL1: TG0 = 16KB (0b10), T0SZ = 16
    mmu.set_tcr_el1((0b10ULL << 14u) | 16u);
    mmu.set_ttbr0_el1(s1_base);
    mmu.set_mair_el1(0xFFULL);

    // HCR_EL2.VM = 1
    mmu.set_hcr_el2(0x1ULL);
    // VTCR_EL2: TG0 = 16KB (0b10), SL0 = Level 2 (0b01), T0SZ = 16
    mmu.set_vtcr_el2(0x80000000ULL | (0b10ULL << 14u) | (0b01ULL << 6u) | 16u);
    mmu.set_vttbr_el2((static_cast<std::uint64_t>(vmid) << 48u) | s2_base);
}

static void test_stage1_translation_fault_propagation() {
    Memory mem(1024 * 1024 * 8);
    MMU mmu;
    configure_two_stage_environment(mmu, 0x10000ULL, 0x30000ULL);

    // VA 0x10000000 is completely unmapped in Stage-1
    const std::uint64_t unmapped_va = 0x10000000ULL;
    TranslationFault fault{};

    auto res = mmu.translate(mem, unmapped_va, AccessKind::Read, 1, &fault);
    assert(!res.has_value());
    assert(!fault.is_stage2);
    assert(fault.type == MmuFaultType::Translation);
    assert(fault.virtual_address == unmapped_va);

    std::cout << "[PASS] test_stage1_translation_fault_propagation\n";
}

static void test_stage2_translation_fault_propagation() {
    Memory mem(1024 * 1024 * 8);
    MMU mmu;
    configure_two_stage_environment(mmu, 0x10000ULL, 0x30000ULL);

    const std::uint64_t va  = 0x20000000ULL;
    const std::uint64_t ipa = 0x00080000ULL;

    // Stage 1 maps VA -> IPA
    setup_stage1_page_16k(mem, 0x10000ULL, va, ipa, true, true, true);
    // Stage 2 does NOT map IPA (unmapped in Stage 2)

    TranslationFault fault{};
    auto res = mmu.translate(mem, va + 0x100, AccessKind::Read, 1, &fault);
    assert(!res.has_value());
    assert(fault.is_stage2);
    assert(fault.type == MmuFaultType::Translation);
    assert(fault.fault_ipa == ipa + 0x100 || fault.fault_ipa == ipa);
    assert(fault.virtual_address == va + 0x100);

    std::cout << "[PASS] test_stage2_translation_fault_propagation\n";
}

static void test_stage1_permission_fault_propagation() {
    Memory mem(1024 * 1024 * 8);
    MMU mmu;
    configure_two_stage_environment(mmu, 0x10000ULL, 0x30000ULL);

    const std::uint64_t va  = 0x30000000ULL;
    const std::uint64_t ipa = 0x00090000ULL;
    const std::uint64_t pa  = 0x00400000ULL;

    // Stage 1 is Read-Only, Stage 2 is Read-Write
    setup_stage1_page_16k(mem, 0x10000ULL, va, ipa, false, true, true); // S1 RO
    const bool s2_ok = Stage2TranslationEngine::map_page_16k(mem, 0x30000ULL, ipa, pa, true, true, true); // S2 RW
    assert(s2_ok);
    (void)s2_ok;

    TranslationFault fault{};
    // Read succeeds
    auto r_res = mmu.translate(mem, va, AccessKind::Read, 1, &fault);
    assert(r_res.has_value());
    assert(r_res.value() == pa);

    // Write fails at Stage 1
    auto w_res = mmu.translate(mem, va, AccessKind::Write, 1, &fault);
    assert(!w_res.has_value());
    assert(!fault.is_stage2);
    assert(fault.type == MmuFaultType::Permission);
    assert(fault.is_write);
    assert(fault.virtual_address == va);

    std::cout << "[PASS] test_stage1_permission_fault_propagation\n";
}

static void test_stage2_permission_fault_propagation() {
    Memory mem(1024 * 1024 * 8);
    MMU mmu;
    configure_two_stage_environment(mmu, 0x10000ULL, 0x30000ULL);

    const std::uint64_t va  = 0x40000000ULL;
    const std::uint64_t ipa = 0x000A0000ULL;
    const std::uint64_t pa  = 0x00500000ULL;

    // Stage 1 is Read-Write, Stage 2 is Read-Only
    setup_stage1_page_16k(mem, 0x10000ULL, va, ipa, true, true, true); // S1 RW
    const bool s2_ok = Stage2TranslationEngine::map_page_16k(mem, 0x30000ULL, ipa, pa, true, false, true); // S2 RO
    assert(s2_ok);
    (void)s2_ok;

    TranslationFault fault{};
    // Read succeeds
    auto r_res = mmu.translate(mem, va + 0x40, AccessKind::Read, 1, &fault);
    assert(r_res.has_value());
    assert(r_res.value() == pa + 0x40);

    // Write passes Stage 1, fails at Stage 2
    auto w_res = mmu.translate(mem, va + 0x40, AccessKind::Write, 1, &fault);
    assert(!w_res.has_value());
    assert(fault.is_stage2);
    assert(fault.type == MmuFaultType::Permission);
    assert(fault.is_write);
    assert(fault.fault_ipa == ipa + 0x40 || fault.fault_ipa == ipa);

    std::cout << "[PASS] test_stage2_permission_fault_propagation\n";
}

static void test_execute_never_combination() {
    Memory mem(1024 * 1024 * 8);
    MMU mmu;
    configure_two_stage_environment(mmu, 0x10000ULL, 0x30000ULL);

    const std::uint64_t va  = 0x50000000ULL;
    const std::uint64_t ipa = 0x000B0000ULL;
    const std::uint64_t pa  = 0x00600000ULL;

    // Case A: Stage 1 is non-executable, Stage 2 is executable
    setup_stage1_page_16k(mem, 0x10000ULL, va, ipa, true, true, false); // S1 non-exec
    const bool s2_ok1 = Stage2TranslationEngine::map_page_16k(mem, 0x30000ULL, ipa, pa, true, true, true); // S2 exec
    assert(s2_ok1);
    (void)s2_ok1;

    TranslationFault fault{};
    auto ex1 = mmu.translate(mem, va, AccessKind::Execute, 1, &fault);
    assert(!ex1.has_value());
    assert(!fault.is_stage2); // Failed at S1
    assert(fault.type == MmuFaultType::Permission);
    assert(fault.is_instruction);

    // Case B: Stage 1 is executable, Stage 2 is non-executable (XN)
    const std::uint64_t va2  = 0x60000000ULL;
    const std::uint64_t ipa2 = 0x000C0000ULL;
    const std::uint64_t pa2  = 0x00700000ULL;

    setup_stage1_page_16k(mem, 0x10000ULL, va2, ipa2, true, true, true); // S1 exec
    const bool s2_ok2 = Stage2TranslationEngine::map_page_16k(mem, 0x30000ULL, ipa2, pa2, true, true, false); // S2 non-exec
    assert(s2_ok2);
    (void)s2_ok2;

    auto ex2 = mmu.translate(mem, va2, AccessKind::Execute, 1, &fault);
    assert(!ex2.has_value());
    assert(fault.is_stage2); // Failed at S2
    assert(fault.type == MmuFaultType::Permission);
    assert(fault.is_instruction);

    std::cout << "[PASS] test_execute_never_combination\n";
}

static void test_el0_privilege_fault_at_stage1() {
    Memory mem(1024 * 1024 * 8);
    MMU mmu;
    configure_two_stage_environment(mmu, 0x10000ULL, 0x30000ULL);

    const std::uint64_t va  = 0x70000000ULL;
    const std::uint64_t ipa = 0x000D0000ULL;
    const std::uint64_t pa  = 0x00800000ULL;

    // Stage 1: EL1 only (el0_accessible = false)
    setup_stage1_page_16k(mem, 0x10000ULL, va, ipa, true, false, true);
    const bool s2_ok = Stage2TranslationEngine::map_page_16k(mem, 0x30000ULL, ipa, pa, true, true, true);
    assert(s2_ok);
    (void)s2_ok;

    TranslationFault fault{};
    // EL1 access succeeds
    auto el1_res = mmu.translate(mem, va, AccessKind::Read, 1, &fault);
    assert(el1_res.has_value());
    assert(el1_res.value() == pa);

    // EL0 access fails at Stage 1
    auto el0_res = mmu.translate(mem, va, AccessKind::Read, 0, &fault);
    assert(!el0_res.has_value());
    assert(!fault.is_stage2);
    assert(fault.type == MmuFaultType::Permission);

    std::cout << "[PASS] test_el0_privilege_fault_at_stage1\n";
}

static void test_fault_isolation_no_side_effects() {
    Memory mem(1024 * 1024 * 16);
    MMU mmu;
    configure_two_stage_environment(mmu, 0x10000ULL, 0x30000ULL);

    const std::uint64_t va  = 0x80000000ULL;
    const std::uint64_t ipa = 0x000E0000ULL;
    const std::uint64_t pa  = 0x00900000ULL;

    mem.write_u32(static_cast<std::size_t>(pa), 0xCAFEBABE);

    // Stage 1 RW, Stage 2 RO
    setup_stage1_page_16k(mem, 0x10000ULL, va, ipa, true, true, true);
    const bool s2_ok = Stage2TranslationEngine::map_page_16k(mem, 0x30000ULL, ipa, pa, true, false, true);
    assert(s2_ok);
    (void)s2_ok;

    TranslationFault fault{};
    auto w_res = mmu.translate(mem, va, AccessKind::Write, 1, &fault);
    assert(!w_res.has_value());
    assert(fault.is_stage2);

    // Target memory remains uncorrupted
    assert(mem.read_u32(static_cast<std::size_t>(pa)) == 0xCAFEBABE);

    std::cout << "[PASS] test_fault_isolation_no_side_effects\n";
}

int main() {
    std::cerr << "=== Running Stage 200.5 Combined Fault Propagation Tests ===" << std::endl;
    std::cerr << "Running test 1..." << std::endl;
    test_stage1_translation_fault_propagation();
    std::cerr << "Running test 2..." << std::endl;
    test_stage2_translation_fault_propagation();
    std::cerr << "Running test 3..." << std::endl;
    test_stage1_permission_fault_propagation();
    std::cerr << "Running test 4..." << std::endl;
    test_stage2_permission_fault_propagation();
    std::cerr << "Running test 5..." << std::endl;
    test_execute_never_combination();
    std::cerr << "Running test 6..." << std::endl;
    test_el0_privilege_fault_at_stage1();
    std::cerr << "Running test 7..." << std::endl;
    test_fault_isolation_no_side_effects();
    std::cerr << "All Stage 200.5 tests passed successfully!" << std::endl;
    return 0;
}

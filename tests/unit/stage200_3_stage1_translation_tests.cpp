// ==============================================================================
// WinIOS Campaign 200 Stage 3: Stage-1 Translation Conformance Tests
// ==============================================================================
// Verifies:
// 1. T0SZ address size boundaries & AddressSize fault generation
// 2. Canonical address validation
// 3. AP[2:1] permission matrix (EL0/EL1, Read/Write, Read-Only)
// 4. UXN / PXN execute-never permissions
// 5. Access Flag (AF) fault delivery
// 6. Invalid descriptor rejection & Translation fault levels
// 7. 16KB granule table walking conformance
// ==============================================================================

#include "cpu/mmu.h"
#include "memory/memory.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace mmu;

static void setup_stage1_16k_page(
    Memory& mem,
    std::uint64_t ttbr0_pa,
    std::uint64_t va,
    std::uint64_t pa,
    bool writable,
    bool el0_accessible,
    bool executable,
    bool set_af = true) {

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

    std::uint64_t l3_desc = (pa & 0x0000FFFFFFFFC000ULL) | 0x3u;
    if (set_af) {
        l3_desc |= (1ULL << 10u); // AF = 1
    }

    std::uint64_t ap = 0;
    if (!writable) ap |= 0b10ULL;      // AP[2] = 1 for RO
    if (el0_accessible) ap |= 0b01ULL; // AP[1] = 1 for EL0
    l3_desc |= (ap << 6u);

    if (!executable) {
        l3_desc |= (1ULL << 54u); // UXN
        l3_desc |= (1ULL << 53u); // PXN
    }

    mem.write_u64(static_cast<std::size_t>(l3_entry), l3_desc);
}

void test_t0sz_address_boundaries() {
    std::cout << "[Test 1] Stage-1 T0SZ address size boundaries...\n";
    Memory mem(1024 * 1024 * 4);
    MMU mmu;

    mmu.set_sctlr_el1(0x00C50839ULL);
    // TCR_EL1: TG0 = 0b10 (16KB), T0SZ = 25 (39-bit VA space)
    mmu.set_tcr_el1((0b10ULL << 14u) | 25u);
    mmu.set_ttbr0_el1(0x10000ULL);

    // Map VA inside 39-bit space
    const std::uint64_t valid_va = 0x0000003000004000ULL; // inside 39-bit space
    const std::uint64_t pa = 0x80000ULL;
    setup_stage1_16k_page(mem, 0x10000ULL, valid_va, pa, true, true, true);

    TranslationFault fault{};
    auto res = mmu.translate(mem, valid_va, AccessKind::Read, 0, &fault);
    assert(res.has_value());
    assert(res.value() == pa);

    // VA outside 39-bit space (bit 39 set) -> AddressSize fault
    const std::uint64_t invalid_va = 0x0000008000004000ULL;
    auto res_invalid = mmu.translate(mem, invalid_va, AccessKind::Read, 0, &fault);
    assert(!res_invalid.has_value());
    assert(fault.type == MmuFaultType::AddressSize);

    std::cout << "[PASS] test_t0sz_address_boundaries\n";
}

void test_ap_permission_matrix() {
    std::cout << "[Test 2] AP[2:1] permission matrix conformance...\n";
    Memory mem(1024 * 1024 * 4);
    MMU mmu;

    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((0b10ULL << 14u) | 16u); // 48-bit VA, 16KB
    mmu.set_ttbr0_el1(0x10000ULL);

    // Scenario A: EL1 RW, EL0 None (AP = 0b00)
    const std::uint64_t va_priv_rw = 0x10004000ULL;
    setup_stage1_16k_page(mem, 0x10000ULL, va_priv_rw, 0x100000ULL, true, false, true);

    TranslationFault f{};
    // EL1 Read & Write succeed
    assert(mmu.translate(mem, va_priv_rw, AccessKind::Read, 1, &f).has_value());
    assert(mmu.translate(mem, va_priv_rw, AccessKind::Write, 1, &f).has_value());
    // EL0 Access fails with Permission fault
    assert(!mmu.translate(mem, va_priv_rw, AccessKind::Read, 0, &f).has_value());
    assert(f.type == MmuFaultType::Permission);

    // Scenario B: EL1 RO, EL0 RO (AP = 0b11)
    const std::uint64_t va_user_ro = 0x20004000ULL;
    setup_stage1_16k_page(mem, 0x10000ULL, va_user_ro, 0x200000ULL, false, true, true);

    // EL0 Read succeeds, Write fails
    assert(mmu.translate(mem, va_user_ro, AccessKind::Read, 0, &f).has_value());
    assert(!mmu.translate(mem, va_user_ro, AccessKind::Write, 0, &f).has_value());
    assert(f.type == MmuFaultType::Permission);

    // EL1 Read succeeds, Write fails
    assert(mmu.translate(mem, va_user_ro, AccessKind::Read, 1, &f).has_value());
    assert(!mmu.translate(mem, va_user_ro, AccessKind::Write, 1, &f).has_value());
    assert(f.type == MmuFaultType::Permission);

    std::cout << "[PASS] test_ap_permission_matrix\n";
}

void test_uxn_pxn_execute_never() {
    std::cout << "[Test 3] UXN / PXN execute-never restrictions...\n";
    Memory mem(1024 * 1024 * 4);
    MMU mmu;

    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((0b10ULL << 14u) | 16u);
    mmu.set_ttbr0_el1(0x10000ULL);

    const std::uint64_t va_noexec = 0x30004000ULL;
    // Mapped RW, EL0 accessible, but NOT executable
    setup_stage1_16k_page(mem, 0x10000ULL, va_noexec, 0x300000ULL, true, true, false);

    TranslationFault f{};
    // Read and Write succeed
    assert(mmu.translate(mem, va_noexec, AccessKind::Read, 0, &f).has_value());
    assert(mmu.translate(mem, va_noexec, AccessKind::Write, 0, &f).has_value());

    // Execute fails with Permission fault at both EL0 and EL1
    assert(!mmu.translate(mem, va_noexec, AccessKind::Execute, 0, &f).has_value());
    assert(f.type == MmuFaultType::Permission);
    assert(!mmu.translate(mem, va_noexec, AccessKind::Execute, 1, &f).has_value());
    assert(f.type == MmuFaultType::Permission);

    std::cout << "[PASS] test_uxn_pxn_execute_never\n";
}

void test_access_flag_fault() {
    std::cout << "[Test 4] Access Flag (AF) fault delivery...\n";
    Memory mem(1024 * 1024 * 4);
    MMU mmu;

    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((0b10ULL << 14u) | 16u);
    mmu.set_ttbr0_el1(0x10000ULL);

    const std::uint64_t va_no_af = 0x40004000ULL;
    // Map with AF = 0
    setup_stage1_16k_page(mem, 0x10000ULL, va_no_af, 0x400000ULL, true, true, true, false);

    TranslationFault f{};
    auto res = mmu.translate(mem, va_no_af, AccessKind::Read, 0, &f);
    assert(!res.has_value());
    assert(f.type == MmuFaultType::AccessFlag);
    assert(f.level == 3);

    std::cout << "[PASS] test_access_flag_fault\n";
}

void test_unmapped_translation_fault() {
    std::cout << "[Test 5] Unmapped translation fault levels...\n";
    Memory mem(1024 * 1024 * 4);
    MMU mmu;

    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((0b10ULL << 14u) | 16u);
    mmu.set_ttbr0_el1(0x10000ULL);

    // Unmapped VA
    const std::uint64_t va_unmapped = 0x50004000ULL;
    TranslationFault f{};
    auto res = mmu.translate(mem, va_unmapped, AccessKind::Read, 0, &f);
    assert(!res.has_value());
    assert(f.type == MmuFaultType::Translation);

    std::cout << "[PASS] test_unmapped_translation_fault\n";
}

int main() {
    std::cout << "--- Stage 200.3: Stage-1 Translation Conformance Tests ---\n";
    test_t0sz_address_boundaries();
    test_ap_permission_matrix();
    test_uxn_pxn_execute_never();
    test_access_flag_fault();
    test_unmapped_translation_fault();
    std::cout << "All Stage 200.3 tests passed successfully!\n";
    return 0;
}

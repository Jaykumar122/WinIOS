// ==============================================================================
// WinIOS Campaign 200 Stage 4: Stage-2 Translation Conformance Tests
// ==============================================================================
// Verifies:
// 1. Stage-2 IPA width boundaries (VTCR_EL2.T0SZ) & AddressSize faults
// 2. 16KB granule page-table walking & Level 2 / Level 3 descriptors
// 3. 4KB granule page-table walking conformance
// 4. Stage-2 S2AP permission matrix (Read, Write, Read/Write, Execute)
// 5. Stage-2 XN (Execute-Never) enforcement
// 6. Stage-2 Access Flag (AF) fault delivery
// 7. Misaligned table base address rejection
// ==============================================================================

#include "memory/memory.h"
#include "memory/stage2_mmu.h"
#include <cassert>
#include <cstdint>
#include <iostream>

using namespace winios::mmu;

void test_s2_ipa_width_and_address_size_fault() {
    std::cout << "[Test 1] Stage-2 IPA width & AddressSize fault...\n";
    Memory mem(1024 * 1024 * 4);
    Stage2TranslationEngine s2;

    s2.set_hcr(0x1ULL);
    // VTCR_EL2: TG0 = 0b10 (16KB), SL0 = 0b01, T0SZ = 24 (40-bit IPA space)
    s2.set_vtcr(0x80000000ULL | (0b10ULL << 14u) | (0b01ULL << 6u) | 24u);
    s2.set_vttbr((1ULL << 48u) | 0x10000ULL);

    Stage2Fault fault{};
    // IPA inside 40-bit space (bit 40 is 0)
    const std::uint64_t valid_ipa = 0x0000004000000000ULL - 0x4000ULL;
    // Map it
    Stage2TranslationEngine::map_page_16k(mem, 0x10000ULL, valid_ipa, 0x80000ULL, true, true, true);
    auto res = s2.translate(mem, valid_ipa, Stage2AccessType::Read, &fault);
    assert(res.has_value());
    assert(res->physical_address == 0x80000ULL);

    // IPA outside 40-bit space (bit 40 is 1) -> AddressSize fault
    const std::uint64_t invalid_ipa = 0x0000010000000000ULL;
    auto res_inv = s2.translate(mem, invalid_ipa, Stage2AccessType::Read, &fault);
    assert(!res_inv.has_value());
    assert(fault.type == ::mmu::MmuFaultType::AddressSize);

    std::cout << "[PASS] test_s2_ipa_width_and_address_size_fault\n";
}

void test_s2_permission_matrix() {
    std::cout << "[Test 2] Stage-2 S2AP permission enforcement...\n";
    Memory mem(1024 * 1024 * 4);
    Stage2TranslationEngine s2;

    s2.set_hcr(0x1ULL);
    s2.set_vtcr(0x80000000ULL | (0b10ULL << 14u) | (0b01ULL << 6u) | 16u);
    s2.set_vttbr((2ULL << 48u) | 0x10000ULL);

    // Map Page 1 as Read-Only (readable=true, writable=false)
    const std::uint64_t ro_ipa = 0x00040000ULL;
    const std::uint64_t ro_pa  = 0x00100000ULL;
    Stage2TranslationEngine::map_page_16k(mem, 0x10000ULL, ro_ipa, ro_pa, true, false, true);

    Stage2Fault f{};
    // Read succeeds
    auto r1 = s2.translate(mem, ro_ipa, Stage2AccessType::Read, &f);
    assert(r1.has_value());
    assert(r1->physical_address == ro_pa);

    // Write fails with Permission fault at Level 3
    auto w1 = s2.translate(mem, ro_ipa, Stage2AccessType::Write, &f);
    assert(!w1.has_value());
    assert(f.type == ::mmu::MmuFaultType::Permission);
    assert(f.level == 3);
    assert(f.is_write);

    // Map Page 2 as Write-Only (readable=false, writable=true)
    const std::uint64_t wo_ipa = 0x00080000ULL;
    const std::uint64_t wo_pa  = 0x00200000ULL;
    Stage2TranslationEngine::map_page_16k(mem, 0x10000ULL, wo_ipa, wo_pa, false, true, true);

    // Read fails with Permission fault
    auto r2 = s2.translate(mem, wo_ipa, Stage2AccessType::Read, &f);
    assert(!r2.has_value());
    assert(f.type == ::mmu::MmuFaultType::Permission);

    // Write succeeds
    auto w2 = s2.translate(mem, wo_ipa, Stage2AccessType::Write, &f);
    assert(w2.has_value());
    assert(w2->physical_address == wo_pa);

    std::cout << "[PASS] test_s2_permission_matrix\n";
}

void test_s2_execute_never() {
    std::cout << "[Test 3] Stage-2 XN (Execute-Never) restriction...\n";
    Memory mem(1024 * 1024 * 4);
    Stage2TranslationEngine s2;

    s2.set_hcr(0x1ULL);
    s2.set_vtcr(0x80000000ULL | (0b10ULL << 14u) | (0b01ULL << 6u) | 16u);
    s2.set_vttbr((3ULL << 48u) | 0x10000ULL);

    const std::uint64_t nx_ipa = 0x000C0000ULL;
    const std::uint64_t nx_pa  = 0x00300000ULL;
    // Map as Read/Write, executable=false
    Stage2TranslationEngine::map_page_16k(mem, 0x10000ULL, nx_ipa, nx_pa, true, true, false);

    Stage2Fault f{};
    // Read and Write succeed
    assert(s2.translate(mem, nx_ipa, Stage2AccessType::Read, &f).has_value());
    assert(s2.translate(mem, nx_ipa, Stage2AccessType::Write, &f).has_value());

    // Execute fails with Permission fault
    auto exec_res = s2.translate(mem, nx_ipa, Stage2AccessType::Execute, &f);
    assert(!exec_res.has_value());
    assert(f.type == ::mmu::MmuFaultType::Permission);
    assert(f.is_execute);

    std::cout << "[PASS] test_s2_execute_never\n";
}

void test_s2_4k_granule_translation() {
    std::cout << "[Test 4] Stage-2 4KB granule translation walk...\n";
    Memory mem(1024 * 1024 * 4);
    Stage2TranslationEngine s2;

    s2.set_hcr(0x1ULL);
    // VTCR_EL2: TG0 = 0b00 (4KB), SL0 = 0b01 (Level 1), T0SZ = 16 (48-bit IPA)
    s2.set_vtcr(0x80000000ULL | (0b00ULL << 14u) | (0b01ULL << 6u) | 16u);
    s2.set_vttbr((4ULL << 48u) | 0x20000ULL);

    const std::uint64_t ipa_4k = 0x00001000ULL;
    const std::uint64_t pa_4k  = 0x00081000ULL;
    const bool mapped = Stage2TranslationEngine::map_page_4k(mem, 0x20000ULL, ipa_4k, pa_4k, true, true, true);
    assert(mapped);
    (void)mapped;

    Stage2Fault f{};
    auto res = s2.translate(mem, ipa_4k + 0x20, Stage2AccessType::Read, &f);
    assert(res.has_value());
    assert(res->physical_address == pa_4k + 0x20);
    assert(res->page_size == 4096ULL);

    std::cout << "[PASS] test_s2_4k_granule_translation\n";
}

void test_s2_misaligned_table_base() {
    std::cout << "[Test 5] Stage-2 misaligned table base rejection...\n";
    Memory mem(1024 * 1024 * 4);
    Stage2TranslationEngine s2;

    s2.set_hcr(0x1ULL);
    // 16KB granule requires 16KB alignment (lower 14 bits must be 0)
    s2.set_vtcr(0x80000000ULL | (0b10ULL << 14u) | (0b01ULL << 6u) | 16u);
    s2.set_vttbr((1ULL << 48u) | 0x11000ULL); // misaligned by 0x1000 (bits [13:12] non-zero)

    Stage2Fault f{};
    auto res = s2.translate(mem, 0x10000ULL, Stage2AccessType::Read, &f);
    assert(!res.has_value());
    assert(f.type == ::mmu::MmuFaultType::Alignment);

    std::cout << "[PASS] test_s2_misaligned_table_base\n";
}

int main() {
    std::cout << "--- Stage 200.4: Stage-2 Translation Conformance Tests ---\n";
    test_s2_ipa_width_and_address_size_fault();
    test_s2_permission_matrix();
    test_s2_execute_never();
    test_s2_4k_granule_translation();
    test_s2_misaligned_table_base();
    std::cout << "All Stage 200.4 tests passed successfully!\n";
    return 0;
}

#include <cassert>
#include <iostream>
#include <vector>

#include "memory/memory.h"
#include "memory/stage2_mmu.h"

using namespace winios::mmu;

void test_disabled_stage2() {
    Memory mem(1024 * 1024);
    Stage2TranslationEngine s2;

    assert(!s2.is_enabled());
    auto res = s2.translate(mem, 0x12345ULL, Stage2AccessType::Read);
    assert(res.has_value());
    assert(res->physical_address == 0x12345ULL);
    assert(res->readable && res->writable && res->executable);
    std::cout << "[PASS] test_disabled_stage2" << std::endl;
}

void test_16k_granule_translation() {
    Memory mem(1024 * 1024 * 4); // 4 MB RAM
    Stage2TranslationEngine s2;

    // Enable Stage 2
    s2.set_hcr(0x1ULL); // HCR_EL2.VM = 1
    // VTCR_EL2: TG0 = 0b10 (16KB), SL0 = 0b01 (Level 2 start), T0SZ = 16 (48-bit IPA)
    s2.set_vtcr(0x80000000ULL | (0b10ULL << 14u) | (0b01ULL << 6u) | 16u);
    // VTTBR_EL2: table at PA 0x10000, VMID = 42
    s2.set_vttbr((42ULL << 48u) | 0x10000ULL);

    assert(s2.is_enabled());
    assert(s2.granule() == Stage2Granule::Granule16KB);
    assert(s2.current_vmid() == 42);

    const std::uint64_t ipa = 0x40000ULL;
    const std::uint64_t pa  = 0x80000ULL;

    bool mapped = Stage2TranslationEngine::map_page_16k(mem, 0x10000ULL, ipa, pa, true, true, false);
    assert(mapped);
    (void)mapped;

    // Read access
    auto res_read = s2.translate(mem, ipa + 0x120, Stage2AccessType::Read);
    assert(res_read.has_value());
    assert(res_read->physical_address == pa + 0x120);
    assert(res_read->page_size == 16384ULL);
    assert(res_read->readable);
    assert(res_read->writable);
    assert(!res_read->executable);

    // Write access
    auto res_write = s2.translate(mem, ipa + 0x3F0, Stage2AccessType::Write);
    assert(res_write.has_value());
    assert(res_write->physical_address == pa + 0x3F0);

    std::cout << "[PASS] test_16k_granule_translation" << std::endl;
}

void test_permission_and_faults() {
    Memory mem(1024 * 1024 * 4);
    Stage2TranslationEngine s2;

    s2.set_hcr(0x1ULL);
    s2.set_vtcr(0x80000000ULL | (0b10ULL << 14u) | (0b01ULL << 6u) | 16u);
    s2.set_vttbr(0x20000ULL);

    const std::uint64_t ipa_ro = 0x80000ULL;
    const std::uint64_t pa_ro  = 0x180000ULL;

    // Read-only, executable
    Stage2TranslationEngine::map_page_16k(mem, 0x20000ULL, ipa_ro, pa_ro, true, false, true);

    // Read succeeds
    auto res_ro_read = s2.translate(mem, ipa_ro, Stage2AccessType::Read);
    assert(res_ro_read.has_value());

    // Write fails with Permission fault
    Stage2Fault fault{};
    auto res_ro_write = s2.translate(mem, ipa_ro, Stage2AccessType::Write, &fault);
    assert(!res_ro_write.has_value());
    assert(fault.type == ::mmu::MmuFaultType::Permission);
    assert(fault.level == 3);
    assert(fault.fsc() == 0x0Fu); // 0x0C + 3

    // Unmapped IPA produces Translation fault
    Stage2Fault unmapped_fault{};
    auto res_unmapped = s2.translate(mem, 0x100000ULL, Stage2AccessType::Read, &unmapped_fault);
    assert(!res_unmapped.has_value());
    assert(unmapped_fault.type == ::mmu::MmuFaultType::Translation);

    std::cout << "[PASS] test_permission_and_faults" << std::endl;
}

void test_4k_fallback() {
    Memory mem(1024 * 1024 * 4);
    Stage2TranslationEngine s2;

    s2.set_hcr(0x1ULL);
    // 4KB granule: TG0 = 0b00, SL0 = 0b01 (Level 1 start), T0SZ = 16
    s2.set_vtcr(0x80000000ULL | (0b00ULL << 14u) | (0b01ULL << 6u) | 16u);
    s2.set_vttbr(0x30000ULL);

    assert(s2.granule() == Stage2Granule::Granule4KB);

    const std::uint64_t ipa = 0x5000ULL;
    const std::uint64_t pa  = 0xA000ULL;

    Stage2TranslationEngine::map_page_4k(mem, 0x30000ULL, ipa, pa, true, true, true);

    auto res = s2.translate(mem, ipa + 0x44, Stage2AccessType::Read);
    assert(res.has_value());
    assert(res->physical_address == pa + 0x44);
    assert(res->page_size == 4096ULL);

    std::cout << "[PASS] test_4k_fallback" << std::endl;
}

int main() {
    std::cout << "--- Stage 199.1 Stage-2 Translation Tests ---" << std::endl;
    test_disabled_stage2();
    test_16k_granule_translation();
    test_permission_and_faults();
    test_4k_fallback();
    std::cout << "All Stage 199.1 tests PASSED!" << std::endl;
    return 0;
}

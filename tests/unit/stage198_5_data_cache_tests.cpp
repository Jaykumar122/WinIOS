// =============================================================================
// WinIOS Campaign 198 Stage 198.5: Data Cache Maintenance Operations Tests
// =============================================================================

#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "memory/memory.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

#define TEST_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "Assertion failed: " #expr " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::abort(); \
        } \
    } while (0)

using namespace winios;

namespace {

void setup_cache_test_mmu(Memory& mem, mmu::MMU& mmu, std::uint64_t ttbr_base = 0x10000) {
    // Attr0: Normal WB (0xFF), Attr1: Device-nGnRnE (0x00)
    mmu.set_mair_el1(0x00FFULL);
    mmu.set_tcr_el1((2ULL << 14u) | 28ULL); // TG0=16KB, T0SZ=28
    mmu.set_ttbr0_el1(ttbr_base);
    // SCTLR_EL1: M=1, C=1, DZE=1 (bit 14), UCI=1 (bit 26)
    mmu.set_sctlr_el1(0x5ULL | (1ULL << 14u) | (1ULL << 26u));

    const std::uint64_t l3_base = ttbr_base + 0x4000;
    mem.write_u64(ttbr_base, l3_base | 0x3ULL);

    // Page 0 (0x0000 - 0x3FFF): RW Normal (AP=0b01 -> EL0 RW) -> PA 0x40000
    // AP[2:1] = 0b01 (bit 6=1, bit 7=0)
    mem.write_u64(l3_base + 0, 0x40000ULL | 0x3ULL | (1ULL << 10u) | (1ULL << 6u));

    // Page 1 (0x4000 - 0x7FFF): Read-Only Normal (AP=0b11 -> EL0 RO) -> PA 0x44000
    // AP[2:1] = 0b11 (bit 6=1, bit 7=1)
    mem.write_u64(l3_base + 8, 0x44000ULL | 0x3ULL | (1ULL << 10u) | (3ULL << 6u));

    // Page 2 (0x8000 - 0xBFFF): Device-nGnRnE (AttrIndx=1) -> PA 0x48000
    mem.write_u64(l3_base + 16, 0x48000ULL | 0x3ULL | (1ULL << 10u) | (1ULL << 6u) | (1ULL << 2u));
}

void test_dc_zva_execution_and_privilege() {
    std::cout << "[Test 1] DC ZVA execution, block alignment, and DZE privilege...\n";
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(1024 * 1024);
    setup_cache_test_mmu(mem, cpu.get_mmu());

    // Populate Page 0 with 0xAA bytes
    for (std::size_t i = 0; i < 64; ++i) {
        mem.write_u8(0x40100 + i, 0xAA);
    }
    TEST_ASSERT(mem.read_u64(0x40100) == 0xAAAAAAAAAAAAAAAAULL);

    // Instruction: DC ZVA, X0 (op1=3, CRn=7, CRm=4, op2=1) -> 0xD50B7420
    // At VA 0x1000 (code)
    // Map code page: Page 0 includes 0x1000
    const std::uint32_t dc_zva_x0 = 0xD50B7420u;
    mem.write_u32(0x40000 + 0x1000, dc_zva_x0);

    // Set X0 to VA 0x120 (within 64-byte block 0x100-0x13F)
    cpu.write_register(0, 0x120);
    cpu.set_program_counter(0x1000);
    cpu.set_current_el(0);

    // SCTLR_EL1.DZE is 1 -> Should succeed
    cpu.step(mem);

    // Verify 64 bytes at PA 0x40100 are now 0
    for (std::size_t i = 0; i < 64; i += 8) {
        TEST_ASSERT(mem.read_u64(0x40100 + i) == 0ULL);
    }

    // Now disable SCTLR_EL1.DZE (clear bit 14)
    cpu.get_mmu().set_sctlr_el1(cpu.get_mmu().get_sctlr_el1() & ~(1ULL << 14u));

    // Attempt DC ZVA from EL0 with DZE = 0 -> Takes architectural Undefined Instruction exception to EL1
    cpu.set_program_counter(0x1000);
    cpu.set_current_el(0);
    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 1);
    TEST_ASSERT((cpu.get_esr_el1() >> 26u) == 0x00u); // EC 0x00: Undefined Instruction
    TEST_ASSERT(cpu.get_elr_el1() == 0x1000);

    std::cout << "  -> PASSED\n";
}

void test_dc_maintenance_permissions_and_translation() {
    std::cout << "[Test 2] DC CVAC/CVAU/CIVAC permissions and translation enforcement...\n";
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(1024 * 1024);
    setup_cache_test_mmu(mem, cpu.get_mmu());

    // DC CVAC, X0: 0xD50B7A20
    // DC CIVAC, X0: 0xD50B7E20
    const std::uint32_t dc_cvac_x0 = 0xD50B7A20u;
    const std::uint32_t dc_civac_x0 = 0xD50B7E20u;
    mem.write_u32(0x40000 + 0x1000, dc_cvac_x0);
    mem.write_u32(0x40000 + 0x1004, dc_civac_x0);

    cpu.set_current_el(0);

    // 1. DC CVAC to Read-Only Page (VA 0x4000) -> Requires Read permission -> Should SUCCEED
    cpu.write_register(0, 0x4000);
    cpu.set_program_counter(0x1000);
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x1004);

    // 2. DC CIVAC to Read-Only Page (VA 0x4000) -> Requires Write permission -> Generates Permission Fault to EL1
    cpu.write_register(0, 0x4000);
    cpu.set_program_counter(0x1004);
    cpu.set_current_el(0);
    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 1);
    TEST_ASSERT((cpu.get_esr_el1() & 0x3Fu) == 0x0Fu); // FSC 0x0F: Permission fault Level 3
    TEST_ASSERT(cpu.get_far_el1() == 0x4000);

    // 3. DC CVAC to Unmapped Page (VA 0x100000) -> Generates Translation Fault to EL1
    cpu.write_register(0, 0x100000);
    cpu.set_program_counter(0x1000);
    cpu.set_current_el(0);
    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 1);
    const auto fsc = cpu.get_esr_el1() & 0x3Fu;
    TEST_ASSERT(fsc >= 0x04u && fsc <= 0x07u); // FSC Translation fault
    TEST_ASSERT(cpu.get_far_el1() == 0x100000);

    std::cout << "  -> PASSED\n";
}

void test_el0_uci_control() {
    std::cout << "[Test 3] SCTLR_EL1.UCI control over EL0 cache maintenance...\n";
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(1024 * 1024);
    setup_cache_test_mmu(mem, cpu.get_mmu());

    // DC CVAC, X0: 0xD50B7A20
    mem.write_u32(0x40000 + 0x1000, 0xD50B7A20u);

    // Clear SCTLR_EL1.UCI (bit 26 = 0)
    cpu.get_mmu().set_sctlr_el1(cpu.get_mmu().get_sctlr_el1() & ~(1ULL << 26u));

    cpu.set_current_el(0);
    cpu.write_register(0, 0x200);
    cpu.set_program_counter(0x1000);
    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 1);
    TEST_ASSERT((cpu.get_esr_el1() >> 26u) == 0x00u); // Undefined instruction
    TEST_ASSERT(cpu.get_elr_el1() == 0x1000);

    std::cout << "  -> PASSED\n";
}

} // namespace

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::cout << "====================================================\n";
    std::cout << "Stage 198.5: Data Cache Maintenance Operations Tests\n";
    std::cout << "====================================================\n";

    test_dc_zva_execution_and_privilege();
    test_dc_maintenance_permissions_and_translation();
    test_el0_uci_control();

    std::cout << "\nAll Stage 198.5 Data Cache Maintenance tests passed successfully!\n";
    return 0;
}

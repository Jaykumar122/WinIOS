// =============================================================================
// WinIOS Campaign 198 Stage 198.6: Instruction Cache Synchronization & PoU Tests
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

void setup_icache_test_mmu(Memory& mem, mmu::MMU& mmu, std::uint64_t ttbr_base = 0x10000) {
    mmu.set_mair_el1(0x00FFULL); // Normal WB
    mmu.set_tcr_el1((2ULL << 14u) | 28ULL); // TG0=16KB, T0SZ=28
    mmu.set_ttbr0_el1(ttbr_base);
    mmu.set_sctlr_el1(0x5ULL | (1ULL << 14u) | (1ULL << 26u)); // M=1, C=1, DZE=1, UCI=1

    const std::uint64_t l3_base = ttbr_base + 0x4000;
    mem.write_u64(ttbr_base, l3_base | 0x3ULL);

    // Page 0 (0x0000 - 0x3FFF): EL0/EL1 RWX (AP=0b01, UXN=0, PXN=0) -> PA 0x40000
    mem.write_u64(l3_base + 0, 0x40000ULL | 0x3ULL | (1ULL << 10u) | (1ULL << 6u));
}

void test_self_modifying_code_sequence() {
    std::cout << "[Test 1] Self-modifying code protocol (STR -> DC CVAU -> DSB -> IC IVAU -> DSB -> ISB)...\n";
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(1024 * 1024);
    setup_icache_test_mmu(mem, cpu.get_mmu());

    // Target code address: VA 0x2000 (PA 0x42000)
    // Initial instruction at 0x2000: ADD X0, X0, #1 (0x91000400)
    // Followed by NOP (0xD503201F)
    mem.write_u32(0x42000, 0x91000400u);
    mem.write_u32(0x42004, 0xD503201Fu);

    // Execution sequence at VA 0x1000 (PA 0x41000):
    // 0x1000: MOVZ W1, #0x2800 (low half of 0x91002800 -> ADD X0, X0, #10)
    // 0x1004: MOVK W1, #0x9100, LSL #16
    // 0x1008: MOVZ X2, #0x2000 (target address)
    // 0x100C: STR W1, [X2]     (Write modified instruction)
    // 0x1010: DC CVAU, X2      (0xD50B7B22: Clean to PoU)
    // 0x1014: DSB ISH          (0xD5033BBF)
    // 0x1018: IC IVAU, X2      (0xD50B7522: Invalidate instruction cache to PoU)
    // 0x101C: DSB ISH          (0xD5033BBF)
    // 0x1020: ISB              (0xD5033FDF)
    // 0x1024: BR X2            (0xD61F0040: Branch to modified code)
    const std::uint64_t code_pa = 0x41000;
    mem.write_u32(code_pa + 0x00, 0x52850001u); // MOVZ W1, #0x2800
    mem.write_u32(code_pa + 0x04, 0x72B22001u); // MOVK W1, #0x9100, LSL #16 (W1 = 0x91002800)
    mem.write_u32(code_pa + 0x08, 0xD2840002u); // MOVZ X2, #0x2000
    mem.write_u32(code_pa + 0x0C, 0xB9000041u); // STR W1, [X2]
    mem.write_u32(code_pa + 0x10, 0xD50B7B22u); // DC CVAU, X2
    mem.write_u32(code_pa + 0x14, 0xD5033BBFu); // DSB ISH
    mem.write_u32(code_pa + 0x18, 0xD50B7522u); // IC IVAU, X2
    mem.write_u32(code_pa + 0x1C, 0xD5033BBFu); // DSB ISH
    mem.write_u32(code_pa + 0x20, 0xD5033FDFu); // ISB
    mem.write_u32(code_pa + 0x24, 0xD61F0040u); // BR X2

    cpu.set_current_el(0);
    cpu.set_program_counter(0x1000);
    cpu.write_register(0, 100); // Initial X0 = 100

    // Step through modification and branch: 10 instructions
    for (int i = 0; i < 10; ++i) {
        cpu.step(mem);
    }
    // PC should now be at target VA 0x2000
    TEST_ASSERT(cpu.get_program_counter() == 0x2000);

    // Step at 0x2000 (modified instruction: ADD X0, X0, #10)
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 110);
    TEST_ASSERT(cpu.get_program_counter() == 0x2004);

    std::cout << "  -> PASSED\n";
}

void test_ic_iallu_privilege_enforcement() {
    std::cout << "[Test 2] IC IALLU / IC IALLUIS unconditional EL0 restriction...\n";
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(1024 * 1024);
    setup_icache_test_mmu(mem, cpu.get_mmu());

    // IC IALLU: 0xD508751F (op1=0, CRn=7, CRm=5, op2=0, rt=31)
    // IC IALLUIS: 0xD508711F (op1=0, CRn=7, CRm=1, op2=0, rt=31)
    mem.write_u32(0x40000 + 0x1000, 0xD508751Fu); // IC IALLU
    mem.write_u32(0x40000 + 0x1004, 0xD508711Fu); // IC IALLUIS

    // Even with SCTLR_EL1.UCI = 1, IC IALLU executed at EL0 MUST trap with Undefined Instruction (EC 0x00)
    cpu.set_current_el(0);
    cpu.set_program_counter(0x1000);
    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 1);
    TEST_ASSERT((cpu.get_esr_el1() >> 26u) == 0x00u); // EC 0x00
    TEST_ASSERT(cpu.get_elr_el1() == 0x1000);

    // Same for IC IALLUIS at EL0
    cpu.set_current_el(0);
    cpu.set_program_counter(0x1004);
    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 1);
    TEST_ASSERT((cpu.get_esr_el1() >> 26u) == 0x00u);
    TEST_ASSERT(cpu.get_elr_el1() == 0x1004);

    // At EL1, both must succeed cleanly
    cpu.set_current_el(1);
    cpu.set_program_counter(0x1000);
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x1004);

    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x1008);

    std::cout << "  -> PASSED\n";
}

void test_ic_ivau_uci_control() {
    std::cout << "[Test 3] IC IVAU SCTLR_EL1.UCI control and translation fault...\n";
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(1024 * 1024);
    setup_icache_test_mmu(mem, cpu.get_mmu());

    // IC IVAU, X0: 0xD50B7520 (op1=3, CRn=7, CRm=5, op2=1, rt=0)
    mem.write_u32(0x40000 + 0x1000, 0xD50B7520u);

    // 1. With UCI=1 at EL0, IC IVAU on mapped page succeeds
    cpu.set_current_el(0);
    cpu.write_register(0, 0x2000);
    cpu.set_program_counter(0x1000);
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x1004);

    // 2. Clear SCTLR_EL1.UCI (bit 26 = 0)
    cpu.get_mmu().set_sctlr_el1(cpu.get_mmu().get_sctlr_el1() & ~(1ULL << 26u));

    // Now IC IVAU at EL0 must trap with Undefined Instruction (EC 0x00)
    cpu.set_program_counter(0x1000);
    cpu.step(mem);
    TEST_ASSERT(cpu.get_current_el() == 1);
    TEST_ASSERT((cpu.get_esr_el1() >> 26u) == 0x00u);

    // 3. Restore UCI=1, target unmapped address 0x200000 -> Must fault with Translation Fault
    cpu.get_mmu().set_sctlr_el1(cpu.get_mmu().get_sctlr_el1() | (1ULL << 26u));
    cpu.set_current_el(0);
    cpu.write_register(0, 0x200000);
    cpu.set_program_counter(0x1000);
    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 1);
    const auto fsc = cpu.get_esr_el1() & 0x3Fu;
    TEST_ASSERT(fsc >= 0x04u && fsc <= 0x07u);
    TEST_ASSERT(cpu.get_far_el1() == 0x200000);

    std::cout << "  -> PASSED\n";
}

} // namespace

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::cout << "====================================================\n";
    std::cout << "Stage 198.6: Instruction Cache Sync & PoU Tests\n";
    std::cout << "====================================================\n";

    test_self_modifying_code_sequence();
    test_ic_iallu_privilege_enforcement();
    test_ic_ivau_uci_control();

    std::cout << "\nAll Stage 198.6 Instruction Cache Sync tests passed successfully!\n";
    return 0;
}

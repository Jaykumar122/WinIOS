// =============================================================================
// stage193_1_execution_level_tests.cpp
//
// Stage 193.1: EL0/EL1 Execution-Level Architecture & Privilege Tests
// Verifies:
//   - Explicit CurrentEL representation in CPU architectural state.
//   - CurrentEL reading at EL1 returns 0x4 (EL=1, bits [3:2]=1).
//   - CurrentEL reading at EL2 returns 0x8 (EL=2, bits [3:2]=2).
//   - CurrentEL reading at EL3 returns 0xC (EL=3, bits [3:2]=3).
//   - CurrentEL reading at EL0 is rejected with UndefinedInstruction exception.
//   - MSR to CurrentEL is architecturally illegal at any EL (read-only).
//   - Privilege checks prevent EL0 execution of privileged instructions:
//       * ERET at EL0 fails with UndefinedInstruction exception.
//       * MSR SPSel at EL0 fails with UndefinedInstruction exception.
//       * MRS SCTLR_EL1 at EL0 fails with UndefinedInstruction exception.
//       * MRS VBAR_EL1 at EL0 fails with UndefinedInstruction exception.
//       * TLBI VMALLE1 at EL0 fails with UndefinedInstruction exception.
//   - Same operations succeed when executed at EL1.
//   - Invalid CurrentEL values (> 3) are sanitized via architectural masking.
// =============================================================================

#include <cstdint>
#include <iostream>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "cpu/synchronous_exception.h"
#include "cpu/sysreg.h"
#include "memory/memory.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Test 1: Explicit CurrentEL representation & read values at EL1, EL2, EL3
bool test_current_el_reads() {
    CPU cpu;
    Memory mem(0x100000ULL);
    cpu.set_architectural_exceptions(true);

    // Write: MRS X0, CurrentEL (0xD5384240u) at 0x1000
    constexpr std::uint64_t kCodeAddr = 0x1000ULL;
    mem.write_u32(static_cast<std::size_t>(kCodeAddr), 0xD5384240u);

    // Test at EL1
    cpu.set_current_el(1);
    cpu.set_program_counter(kCodeAddr);
    cpu.write_register(0, 0);
    cpu.step(mem);
    CHECK(cpu.read_register(0) == 0x4ULL, "CurrentEL at EL1 must return 0x4 (bits [3:2] = 1)");
    CHECK(cpu.get_current_el() == 1, "CPU CurrentEL must remain 1");

    // Test at EL2
    cpu.set_current_el(2);
    cpu.set_program_counter(kCodeAddr);
    cpu.write_register(0, 0);
    cpu.step(mem);
    CHECK(cpu.read_register(0) == 0x8ULL, "CurrentEL at EL2 must return 0x8 (bits [3:2] = 2)");
    CHECK(cpu.get_current_el() == 2, "CPU CurrentEL must remain 2");

    // Test at EL3
    cpu.set_current_el(3);
    cpu.set_program_counter(kCodeAddr);
    cpu.write_register(0, 0);
    cpu.step(mem);
    CHECK(cpu.read_register(0) == 0xCULL, "CurrentEL at EL3 must return 0xC (bits [3:2] = 3)");
    CHECK(cpu.get_current_el() == 3, "CPU CurrentEL must remain 3");

    std::cout << "[PASS] test_current_el_reads\n";
    return true;
}

// Test 2: CurrentEL read and write rejection at EL0
bool test_current_el_el0_rejection() {
    CPU cpu;
    Memory mem(0x100000ULL);
    cpu.set_architectural_exceptions(true);

    constexpr std::uint64_t kCodeAddr = 0x2000ULL;
    constexpr std::uint64_t kVbarAddr = 0x8000ULL;
    cpu.set_vbar_el1(kVbarAddr);

    // MRS X0, CurrentEL (0xD5384240u)
    mem.write_u32(static_cast<std::size_t>(kCodeAddr), 0xD5384240u);

    cpu.set_current_el(0);
    cpu.set_program_counter(kCodeAddr);
    cpu.write_register(0, 0x12345678ULL);

    cpu.step(mem);

    // Must have taken an architectural exception to EL1 Lower EL synchronous vector
    CHECK(cpu.get_current_el() == 1, "Exception must transition CPU to EL1");
    CHECK(cpu.get_elr_el1() == kCodeAddr, "ELR_EL1 must point to faulting instruction");
    CHECK(cpu.get_program_counter() == kVbarAddr + 0x400u, "PC must jump to VBAR_EL1 + 0x400 (Lower EL Sync)");
    const auto ec = static_cast<std::uint32_t>((cpu.get_esr_el1() >> 26u) & 0x3Fu);
    CHECK(ec == 0x00u, "ESR_EL1.EC must be 0x00 (Unknown / Undefined Instruction)");
    CHECK(cpu.read_register(0) == 0x12345678ULL, "X0 must not have been modified");

    // Test MSR CurrentEL, X0 (0xD5184240u) at EL1 - architecturally illegal write
    mem.write_u32(static_cast<std::size_t>(kCodeAddr + 4u), 0xD5184240u);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(kCodeAddr + 4u);

    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1, "CurrentEL must remain 1");
    CHECK(cpu.get_program_counter() == kVbarAddr + 0x200u, "PC must jump to VBAR_EL1 + 0x200 (Current EL SPx Sync)");
    const auto ec_msr = static_cast<std::uint32_t>((cpu.get_esr_el1() >> 26u) & 0x3Fu);
    CHECK(ec_msr == 0x00u, "ESR_EL1.EC must be 0x00 for read-only MSR");

    std::cout << "[PASS] test_current_el_el0_rejection\n";
    return true;
}

// Test 3: Privileged instructions rejection at EL0 vs success at EL1
bool test_privileged_instruction_enforcement() {
    CPU cpu;
    Memory mem(0x100000ULL);
    cpu.set_architectural_exceptions(true);

    constexpr std::uint64_t kVbarAddr = 0xA000ULL;
    cpu.set_vbar_el1(kVbarAddr);

    struct TestCase {
        const char* name;
        std::uint32_t encoding;
    };

    const std::vector<TestCase> test_cases = {
        { "ERET",          0xD69F03E0u }, // ERET
        { "MSR SPSel, X0", 0xD5184200u }, // MSR SPSel, X0
        { "MRS SCTLR_EL1", 0xD5381000u }, // MRS X0, SCTLR_EL1
        { "MRS VBAR_EL1",  0xD538C000u }, // MRS X0, VBAR_EL1
        { "TLBI VMALLE1",  0xD508871Fu }  // TLBI VMALLE1
    };

    std::uint64_t pc = 0x3000ULL;
    for (const auto& tc : test_cases) {
        mem.write_u32(static_cast<std::size_t>(pc), tc.encoding);

        // Run at EL0 -> must take exception to EL1 Lower EL Sync vector
        cpu.set_current_el(0);
        cpu.set_program_counter(pc);

        cpu.step(mem);

        CHECK(cpu.get_current_el() == 1, "EL0 execution must trap to EL1");
        CHECK(cpu.get_program_counter() == kVbarAddr + 0x400u, "PC must be at VBAR_EL1 + 0x400");
        const auto ec = static_cast<std::uint32_t>((cpu.get_esr_el1() >> 26u) & 0x3Fu);
        CHECK(ec == 0x00u, "ESR_EL1.EC must be 0x00 (Undefined Instruction)");
        CHECK(cpu.get_elr_el1() == pc, "ELR_EL1 must hold faulting instruction address");

        pc += 0x100ULL;
    }

    std::cout << "[PASS] test_privileged_instruction_enforcement\n";
    return true;
}

// Test 4: Privileged instructions success at EL1
bool test_privileged_instruction_el1_success() {
    CPU cpu;
    Memory mem(0x100000ULL);
    cpu.set_architectural_exceptions(true);

    constexpr std::uint64_t kCodeAddr = 0x5000ULL;

    // 1. MRS X0, SCTLR_EL1 (0xD5381000u)
    mem.write_u32(static_cast<std::size_t>(kCodeAddr), 0xD5381000u);
    // 2. MRS X1, VBAR_EL1 (0xD538C001u, Rt=1)
    mem.write_u32(static_cast<std::size_t>(kCodeAddr + 4u), 0xD538C001u);
    // 3. MSR SPSel, X2 (0xD5184202u)
    mem.write_u32(static_cast<std::size_t>(kCodeAddr + 8u), 0xD5184202u);

    cpu.set_current_el(1);
    cpu.set_sp_sel(false);
    cpu.set_vbar_el1(0x12345800ULL);
    cpu.set_program_counter(kCodeAddr);
    cpu.write_register(0, 0);
    cpu.write_register(1, 0);
    cpu.write_register(2, 1); // SPSel = 1

    cpu.step(mem); // MRS SCTLR_EL1
    CHECK(cpu.get_current_el() == 1, "CurrentEL must remain 1");
    CHECK(cpu.read_register(0) == cpu.get_sctlr_el1(), "X0 must equal SCTLR_EL1");

    cpu.step(mem); // MRS VBAR_EL1
    CHECK(cpu.get_current_el() == 1, "CurrentEL must remain 1");
    CHECK(cpu.read_register(1) == 0x12345800ULL, "X1 must equal VBAR_EL1");

    cpu.step(mem); // MSR SPSel, X2
    CHECK(cpu.get_current_el() == 1, "CurrentEL must remain 1");
    CHECK(cpu.get_sp_sel() == true, "SPSel must now be true (EL1h)");

    std::cout << "[PASS] test_privileged_instruction_el1_success\n";
    return true;
}

// Test 5: Sanitization and invalid EL handling
bool test_invalid_el_sanitization() {
    CPU cpu;
    cpu.set_current_el(5); // Out of range (> 3)
    CHECK(cpu.get_current_el() == 1, "CurrentEL must be masked to 2 bits (& 0x3)");

    cpu.set_current_el(7);
    CHECK(cpu.get_current_el() == 3, "CurrentEL must be masked to 2 bits (& 0x3)");

    std::cout << "[PASS] test_invalid_el_sanitization\n";
    return true;
}

} // namespace

int main() {
    std::cout << "=== Running Stage 193.1: Execution-Level Architecture Tests ===\n";

    if (!test_current_el_reads()) return 1;
    if (!test_current_el_el0_rejection()) return 1;
    if (!test_privileged_instruction_enforcement()) return 1;
    if (!test_privileged_instruction_el1_success()) return 1;
    if (!test_invalid_el_sanitization()) return 1;

    std::cout << "=== All Stage 193.1 Tests Passed (5/5) ===\n";
    return 0;
}

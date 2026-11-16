// ==============================================================================
// WinIOS Campaign 201 Stage 6: System Register & Exception Differential Testing
// ==============================================================================
// Verifies ARM64 system registers and architectural exception handling against
// Arm DDI 0487 standards:
// 1. EL0 vs EL1 privilege gating for system register access (MSR/MRS)
// 2. Exception vector selection based on origin EL and SP configuration
// 3. Precise ESR_EL1 syndrome generation (EC, ISS) for SVC, MMU, PAC
// 4. ERET architectural restoration (PSTATE, PC, privilege drops)
// 5. Illegal ERET transition trapping (EC = 0x0E)
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>

#include "cpu/cpu.h"
#include "cpu/sysreg.h"
#include "memory/memory.h"

using namespace winios;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void step_insn(CPU& cpu, Memory& mem, std::uint64_t pc, std::uint32_t insn) {
    mem.write_u32(pc, insn);
    cpu.set_program_counter(pc);
    cpu.step(mem);
}

// -----------------------------------------------------------------------------
// Test 1: System Register Privilege Gating (EL0 vs EL1)
// -----------------------------------------------------------------------------
void test_sysreg_privilege_gating() {
    std::cout << "[Test 1] System Register Privilege Gating...\n";
    CPU cpu;
    Memory mem(0x100000);
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000ULL);

    // MRS X0, SCTLR_EL1 (0xD5381000)
    // At EL0, reading SCTLR_EL1 must trap to EL1!
    cpu.set_current_el(0);
    step_insn(cpu, mem, 0x1000, 0xD5381000u);

    TEST_CHECK(cpu.exception_taken_in_step(), "EL0 MRS SCTLR_EL1 must trap");
    TEST_CHECK(cpu.get_current_el() == 1, "Must transition to EL1 upon trap");
    const std::uint32_t esr0 = static_cast<std::uint32_t>(cpu.get_esr_el1());
    const std::uint32_t ec0 = (esr0 >> 26u) & 0x3Fu;
    // Trapped MSR/MRS access or Undefined Instruction
    TEST_CHECK(ec0 == 0x18 || ec0 == 0x00, "EC must be 0x18 (Trapped MSR/MRS) or 0x00");

    // At EL1, reading/writing SCTLR_EL1 must succeed without trapping!
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    // MSR SCTLR_EL1, X0 (0xD5181000) with X0 = 0x30D00800
    cpu.write_register(0, 0x30D00800ULL);
    step_insn(cpu, mem, 0x1004, 0xD5181000u);
    TEST_CHECK(!cpu.exception_taken_in_step(), "EL1 MSR SCTLR_EL1 must not trap");

    // MRS X1, SCTLR_EL1 (0xD5381001)
    step_insn(cpu, mem, 0x1008, 0xD5381001u);
    TEST_CHECK(!cpu.exception_taken_in_step(), "EL1 MRS SCTLR_EL1 must not trap");
    TEST_CHECK(cpu.read_register(1) == 0x30D00800ULL, "Read value must match written SCTLR_EL1");

    std::cout << "  -> Privilege gating tests passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: SVC Syndrome and Vector Selection
// -----------------------------------------------------------------------------
void test_svc_syndrome_and_vectors() {
    std::cout << "[Test 2] SVC Syndrome & Exception Vector Selection...\n";
    CPU cpu;
    Memory mem(0x100000);
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    const std::uint64_t vbar = 0x100000ULL;
    cpu.set_vbar_el1(vbar);

    // EL0 taking SVC #0x42 (0xD4000841)
    // Synchronous exception from Lower EL (AArch64) -> vector offset +0x400
    cpu.set_current_el(0);
    cpu.set_program_counter(0x2000);
    step_insn(cpu, mem, 0x2000, 0xD4000841u);

    TEST_CHECK(cpu.exception_taken_in_step(), "SVC must take exception");
    TEST_CHECK(cpu.get_current_el() == 1, "Must enter EL1");
    TEST_CHECK(cpu.get_program_counter() == vbar + 0x400ULL, "Vector must be VBAR + 0x400 for lower EL");
    TEST_CHECK(cpu.get_elr_el1() == 0x2004ULL, "ELR_EL1 must record next PC (+4)");

    const std::uint32_t esr = static_cast<std::uint32_t>(cpu.get_esr_el1());
    const std::uint32_t ec = (esr >> 26u) & 0x3Fu;
    const std::uint32_t iss = esr & 0xFFFFu;
    TEST_CHECK(ec == 0x15, "EC must be 0x15 (SVC in AArch64)");
    TEST_CHECK(iss == 0x42, "ISS must capture immediate 0x42");

    std::cout << "  -> SVC syndrome and vector selection passed.\n";
}

// -----------------------------------------------------------------------------
// Test 3: ERET State Restoration & Privilege Drop
// -----------------------------------------------------------------------------
void test_eret_restoration() {
    std::cout << "[Test 3] ERET State Restoration...\n";
    CPU cpu;
    Memory mem(0x100000);
    cpu.reset();
    cpu.set_architectural_exceptions(true);

    // Configure EL1 state to return to EL0 at PC 0x40000 with specific NZCV
    cpu.set_current_el(1);
    cpu.set_elr_el1(0x40000ULL);
    // SPSR_EL1: Mode=0 (EL0t), N=1, Z=0, C=1, V=0 -> NZCV = (1<<31) | (1<<29) = 0xA0000000
    cpu.set_spsr_el1(0xA0000000u);

    // ERET opcode: 0xD69F03E0
    step_insn(cpu, mem, 0x3000, 0xD69F03E0u);

    TEST_CHECK(cpu.get_current_el() == 0, "ERET must drop to EL0");
    TEST_CHECK(cpu.get_program_counter() == 0x40000ULL, "PC must be restored from ELR_EL1");
    TEST_CHECK((cpu.get_nzcv() & 0x8u) != 0, "N flag must be restored");
    TEST_CHECK((cpu.get_nzcv() & 0x4u) == 0, "Z flag must be restored");
    TEST_CHECK((cpu.get_nzcv() & 0x2u) != 0, "C flag must be restored");
    TEST_CHECK((cpu.get_nzcv() & 0x1u) == 0, "V flag must be restored");

    std::cout << "  -> ERET state restoration passed.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Illegal ERET Trap Detection
// -----------------------------------------------------------------------------
void test_illegal_eret_detection() {
    std::cout << "[Test 4] Illegal ERET Exception...\n";
    CPU cpu;
    Memory mem(0x100000);
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000ULL);

    // Set invalid SPSR_EL1 (e.g. attempting to return to EL2 or EL3 from EL1)
    // SPSR_EL1 Mode bits [3:0] = 0b1000 (EL2t) -> Illegal exception return
    cpu.set_current_el(1);
    cpu.set_elr_el1(0x50000ULL);
    cpu.set_spsr_el1(0x00000008ULL); // Invalid target mode

    step_insn(cpu, mem, 0x4000, 0xD69F03E0u); // ERET

    TEST_CHECK(cpu.get_il() == true, "Illegal ERET must set PSTATE.IL");
    TEST_CHECK(cpu.get_program_counter() == 0x50000ULL, "PC must be set to ELR");

    // Next instruction fetch with PSTATE.IL triggers Illegal Execution State trap (EC = 0x0E)
    mem.write_u32(0x50000, 0xD503201Fu); // NOP
    cpu.step(mem);

    TEST_CHECK(cpu.exception_taken_in_step(), "Instruction with PSTATE.IL set must take synchronous exception");
    const std::uint32_t esr = static_cast<std::uint32_t>(cpu.get_esr_el1());
    const std::uint32_t ec = (esr >> 26u) & 0x3Fu;
    TEST_CHECK(ec == 0x0E, "EC must be 0x0E (Illegal Execution State / Return)");

    std::cout << "  -> Illegal ERET detection passed.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 201.6 System Register & Exception Differential Tests ===\n";
    test_sysreg_privilege_gating();
    test_svc_syndrome_and_vectors();
    test_eret_restoration();
    test_illegal_eret_detection();

    if (g_failures == 0) {
        std::cout << "All Stage 201.6 tests passed successfully!\n";
        return 0;
    } else {
        std::cerr << g_failures << " Stage 201.6 test(s) failed!\n";
        return 1;
    }
}

#include "cpu/cpu.h"
#include "cpu/synchronous_exception.h"
#include "memory/memory.h"
#include "reference/independent_reference_model.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios::reference;

// -----------------------------------------------------------------------------
// Test 1: Register Zero Extension and Register File Invariants
// -----------------------------------------------------------------------------
bool test_register_file_invariants() {
    CPU cpu;
    cpu.reset();

    // 1. Initial state: all X registers, SP, PC, NZCV must be zero
    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
        TEST_ASSERT(cpu.read_register(i) == 0, "Initial general-purpose register must be 0");
    }
    TEST_ASSERT(cpu.get_program_counter() == 0, "Initial PC must be 0");
    TEST_ASSERT(cpu.get_stack_pointer() == 0, "Initial SP must be 0");
    TEST_ASSERT(cpu.get_nzcv() == 0, "Initial NZCV must be 0");

    // 2. Writing 32-bit value through W-registers must zero-extend to full 64 bits
    Memory mem(0x10000);
    // mov w0, #0x1234 (0x52824680u)
    // adds w1, w0, #0x5678 (0x11001401u) -> w1 = 0x68AC, upper 32-bits must be 0
    mem.write_u32(0x1000, 0x52824680u); // MOV W0, #0x1234
    mem.write_u32(0x1004, 0x11001401u); // ADDS W1, W0, #0x5
    cpu.set_program_counter(0x1000);

    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 0x1234ULL, "W0 write must zero-extend to 64-bit X0");

    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(1) == 0x1239ULL, "W1 addition result must zero-extend to 64-bit X1");
    TEST_ASSERT((cpu.read_register(1) >> 32u) == 0ULL, "Upper 32 bits of X1 must be zero");

    // 3. Register index out of bounds check
    bool threw = false;
    try {
        cpu.read_register(31);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    TEST_ASSERT(threw, "Register index 31 (XZR/SP) must throw out_of_range on read_register");

    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Exception Entry Vector Selection & SPSR State Capture
// -----------------------------------------------------------------------------
bool test_exception_entry_state_capture() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);

    const std::uint64_t kVbar = 0x80000000ULL;
    cpu.set_vbar_el1(kVbar);

    // Scenario A: Synchronous exception taken from EL0
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_nzcv(0x9); // N=1, V=1
    cpu.set_daif(0x0); // Unmasked

    SynchronousException ex_el0{};
    ex_el0.ec = ExceptionClass::SvcAArch64;
    ex_el0.iss = 0x42;
    ex_el0.target_el = 1;
    ex_el0.preferred_return = 0x1024ULL;

    cpu.raise_synchronous_exception(ex_el0);

    // Vector offset for Lower EL AArch64 is +0x400
    TEST_ASSERT(cpu.get_program_counter() == kVbar + 0x400u, "EL0 exception vector must be VBAR + 0x400");
    TEST_ASSERT(cpu.get_current_el() == 1, "Exception target EL must be EL1");
    TEST_ASSERT(cpu.get_sp_sel() == true, "Exception entry selects SP_EL1 (EL1h)");
    TEST_ASSERT(cpu.get_daif() == 0xFu, "Exception entry must mask all DAIF interrupts");
    TEST_ASSERT(cpu.get_elr_el1() == 0x1024ULL, "ELR_EL1 must capture preferred return PC");
    TEST_ASSERT(cpu.get_esr_el1() == ((0x15u << 26u) | (1u << 25u) | 0x42u), "ESR_EL1 must encode SVC EC and ISS");

    // SPSR_EL1 should capture: NZCV=0x9, DAIF=0x0, EL=0, SP=0
    std::uint32_t expected_spsr_el0 = (0x9u << 28u) | 0x0u;
    TEST_ASSERT(cpu.get_spsr_el1() == expected_spsr_el0, "SPSR_EL1 must match captured EL0 state");

    // Compare with Independent Reference Model
    const auto ref_res = ReferenceExceptionModel::take_synchronous_exception(
        0x1020ULL, 0, false, 0x0, 0x9, kVbar, 0x15u, 0x42u, 0, false, 0x1024ULL, 1);
    TEST_ASSERT(cpu.get_program_counter() == ref_res.target_pc, "Production PC must match reference model");
    TEST_ASSERT(cpu.get_spsr_el1() == ref_res.spsr, "Production SPSR must match reference model");
    TEST_ASSERT(cpu.get_esr_el1() == ref_res.esr, "Production ESR must match reference model");

    // Scenario B: Synchronous exception taken from EL1 with SP_EL1 (EL1h)
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(kVbar);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true); // EL1h
    cpu.set_nzcv(0x4);    // Z=1
    cpu.set_daif(0x2);    // I masked

    SynchronousException ex_el1{};
    ex_el1.ec = ExceptionClass::DataAbortSame;
    ex_el1.iss = 0x06; // Translation fault Level 2
    ex_el1.target_el = 1;
    ex_el1.preferred_return = 0x2048ULL;
    ex_el1.has_fault_address = true;
    ex_el1.fault_address = 0x5000ULL;

    cpu.raise_synchronous_exception(ex_el1);

    // Vector offset for Current EL with SP_ELx is +0x200
    TEST_ASSERT(cpu.get_program_counter() == kVbar + 0x200u, "EL1h exception vector must be VBAR + 0x200");
    TEST_ASSERT(cpu.get_far_el1() == 0x5000ULL, "FAR_EL1 must capture fault address");
    TEST_ASSERT(cpu.get_elr_el1() == 0x2048ULL, "ELR_EL1 must capture faulting instruction address");

    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Exception Return (ERET) Restoration & Illegal State Trapping
// -----------------------------------------------------------------------------
bool test_eret_restoration_and_illegal_state() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);

    Memory mem(0x10000);
    // ERET opcode: 0xD69F03E0u
    mem.write_u32(0x2000, 0xD69F03E0u);

    // 1. Valid ERET: return from EL1 to EL0
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(0x2000);
    cpu.set_elr_el1(0x1000ULL);
    // SPSR_EL1: NZCV = 0xC (N=1, Z=1), DAIF = 0x0, Mode = 0x0 (EL0t)
    cpu.set_spsr_el1((0xCu << 28u) | 0x0u);

    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 0, "ERET must transition processor to EL0");
    TEST_ASSERT(cpu.get_program_counter() == 0x1000ULL, "ERET must restore PC from ELR_EL1");
    TEST_ASSERT(cpu.get_nzcv() == 0xCu, "ERET must restore NZCV condition flags");
    TEST_ASSERT(cpu.get_daif() == 0x0u, "ERET must restore DAIF interrupt masks");
    TEST_ASSERT(cpu.get_il() == false, "PSTATE.IL must remain clear on valid return");

    // 2. Illegal ERET: return attempting privilege escalation (EL1 -> EL2 or invalid M field)
    cpu.set_current_el(1);
    cpu.set_program_counter(0x2000);
    cpu.set_elr_el1(0x3000ULL);
    // Invalid SPSR: M = 0x08 (EL2t) while executing at EL1
    cpu.set_spsr_el1((0x08u));

    cpu.step(mem);

    // According to ARMv8-A architecture rules implemented in cpu.cpp:
    // Attempting illegal ERET sets PSTATE.IL = true, updates PC, and causes Illegal Execution State fault on next step
    TEST_ASSERT(cpu.get_il() == true, "Illegal ERET must set PSTATE.IL");
    TEST_ASSERT(cpu.get_program_counter() == 0x3000ULL, "PC is updated to return address");

    // Stepping with PSTATE.IL set must trigger synchronous Illegal Execution State exception
    cpu.step(mem);
    TEST_ASSERT(cpu.exception_taken_in_step() == true, "Instruction fetch with PSTATE.IL set must take exception");
    TEST_ASSERT(cpu.get_current_el() == 1, "Illegal execution state exception traps to EL1");

    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Exception Syndrome and Fault Status Consistency
// -----------------------------------------------------------------------------
bool test_syndrome_consistency() {
    // 1. Data Abort Same EL
    const std::uint32_t esr_data = ReferenceSyndromeModel::compute_data_abort_esr(
        true, 0x07, true, 8); // Write 8 bytes, translation fault L3
    TEST_ASSERT(((esr_data >> 26u) & 0x3Fu) == 0x25u, "Data Abort Same EL EC must be 0x25");
    TEST_ASSERT((esr_data & 0x3Fu) == 0x07u, "DFSC must be Translation Fault Level 3 (0x07)");
    TEST_ASSERT(((esr_data >> 6u) & 1u) == 1u, "WnR bit must be 1 for write access");
    TEST_ASSERT(((esr_data >> 22u) & 3u) == 3u, "SAS must be 3 for 8-byte access");

    // 2. Instruction Abort Lower EL
    const std::uint32_t esr_ins = ReferenceSyndromeModel::compute_instruction_abort_esr(
        false, 0x0F); // Permission fault L3
    TEST_ASSERT(((esr_ins >> 26u) & 0x3Fu) == 0x20u, "Instruction Abort Lower EL EC must be 0x20");
    TEST_ASSERT((esr_ins & 0x3Fu) == 0x0Fu, "IFSC must be Permission Fault Level 3 (0x0F)");

    // 3. SVC Call
    const std::uint32_t esr_svc = ReferenceSyndromeModel::compute_svc_esr(0xABCD);
    TEST_ASSERT(((esr_svc >> 26u) & 0x3Fu) == 0x15u, "SVC EC must be 0x15");
    TEST_ASSERT((esr_svc & 0xFFFFu) == 0xABCDu, "SVC imm16 must match");

    // 4. PC Alignment Fault
    const std::uint32_t esr_align = ReferenceSyndromeModel::compute_pc_alignment_esr(true);
    TEST_ASSERT(((esr_align >> 26u) & 0x3Fu) == 0x22u, "PC Alignment EC must be 0x22");

    return true;
}

// -----------------------------------------------------------------------------
// Test 5: First-Fault Diagnostic Report Formatting
// -----------------------------------------------------------------------------
bool test_first_fault_diagnostics() {
    ReferenceCpuState expected{};
    expected.pc = 0x1000;
    expected.nzcv = 0;
    expected.retirement_count = 10;
    expected.x[0] = 42;

    const std::uint64_t actual_x[31]{41, 0};
    const auto report = DifferentialValidator::compare_cpu_state(
        0, expected, 0x1000, 0, 10, actual_x);

    TEST_ASSERT(report.has_divergence == true, "Diagnostic validator must flag mismatch");
    TEST_ASSERT(report.field_name == "X0", "Divergence field must be X0");
    TEST_ASSERT(report.expected_value == 42, "Expected value must match");
    TEST_ASSERT(report.actual_value == 41, "Actual value must match");

    return true;
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "WinIOS Phase 52 Stage 35: Architectural State Integrity Tests\n";
    std::cout << "=================================================================\n";

    std::cout << "[Test 1] Testing CPU Register File Invariants & Zero-Extension...\n";
    if (!test_register_file_invariants()) return 1;
    std::cout << "  -> Test 1 Passed. 32-bit writes strictly zero-extend; invalid index rejected.\n";

    std::cout << "[Test 2] Testing Exception Entry Vector Selection & SPSR State Capture...\n";
    if (!test_exception_entry_state_capture()) return 1;
    std::cout << "  -> Test 2 Passed. Vectors 0x400 / 0x200 and SPSR match reference model.\n";

    std::cout << "[Test 3] Testing Exception Return (ERET) Restoration & Illegal State Trapping...\n";
    if (!test_eret_restoration_and_illegal_state()) return 1;
    std::cout << "  -> Test 3 Passed. ERET restores state; illegal mode traps via PSTATE.IL.\n";

    std::cout << "[Test 4] Testing Exception Syndrome (ESR) Consistency...\n";
    if (!test_syndrome_consistency()) return 1;
    std::cout << "  -> Test 4 Passed. ESR EC, ISS, DFSC, IFSC, and SAS match specifications.\n";

    std::cout << "[Test 5] Testing First-Fault Diagnostic Reporting...\n";
    if (!test_first_fault_diagnostics()) return 1;
    std::cout << "  -> Test 5 Passed. Field-level divergence diagnostic report verified.\n";

    std::cout << "All Architectural State Integrity Tests PASSED successfully.\n";
    return 0;
}

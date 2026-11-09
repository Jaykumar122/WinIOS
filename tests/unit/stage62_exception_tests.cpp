#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "cpu/synchronous_exception.h"
#include "memory/memory.h"
#include <iostream>
#include <cassert>
#include <vector>

#define STAGE62_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" << #cond << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

namespace {

// Helper to set up a CPU in EL0 with architectural exceptions enabled
void setup_cpu_el0(CPU& cpu, std::uint64_t pc = 0x40001000ULL, std::uint64_t vbar = 0x80000000ULL) {
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(0);
    cpu.set_program_counter(pc);
    cpu.set_vbar_el1(vbar);
}

// Helper to set up a CPU in EL1 with architectural exceptions enabled
void setup_cpu_el1(CPU& cpu, bool sp_sel = true, std::uint64_t pc = 0x40002000ULL, std::uint64_t vbar = 0x80000000ULL) {
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_sp_sel(sp_sel);
    cpu.set_program_counter(pc);
    cpu.set_vbar_el1(vbar);
}

} // namespace

// -----------------------------------------------------------------------------
// Suite 1: SVC Exception Entry & Return Roundtrip
// -----------------------------------------------------------------------------
bool test_svc_entry_and_return() {
    std::cout << "[1] test_svc_entry_and_return...\n";
    Memory mem(0x100000);
    CPU cpu;
    setup_cpu_el0(cpu, 0x1000ULL, 0x80000ULL);

    // Write SVC #0x42 instruction (0xD4000841) at 0x1000
    // SVC encoding: 1101 0100 000 imm16 000 01
    // imm16 = 0x42 -> (0x42 << 5) = 0x840 -> 0xD4000001 | 0x840 = 0xD4000841
    const std::uint32_t svc_insn = 0xD4000841u;
    mem.write_u32(0x1000, svc_insn);

    // Initial state: EL0, DAIF = 0, NZCV = 0
    cpu.set_daif(0x0u);
    cpu.set_nzcv(0x0u);

    // Execute step
    cpu.step(mem);

    // Verify exception taken to EL1
    STAGE62_ASSERT(cpu.get_current_el() == 1, "Target EL must be EL1");
    STAGE62_ASSERT(cpu.get_daif() == 0xFu, "DAIF must be masked (0xF)");
    STAGE62_ASSERT(cpu.get_elr_el1() == 0x1004ULL, "ELR_EL1 must be PC + 4 for SVC");
    STAGE62_ASSERT(cpu.get_program_counter() == 0x80400ULL, "PC must be VBAR + 0x400 (Lower EL AArch64 Sync)");

    // Verify ESR_EL1
    const std::uint32_t esr = cpu.get_esr_el1();
    const std::uint32_t ec = (esr >> 26u) & 0x3Fu;
    const std::uint32_t il = (esr >> 25u) & 0x1u;
    const std::uint32_t iss = esr & 0xFFFFu;
    STAGE62_ASSERT(ec == 0x15u, "SVC EC must be 0x15 (SvcAArch64)");
    STAGE62_ASSERT(il == 1u, "Instruction length IL must be 1 for 32-bit");
    STAGE62_ASSERT(iss == 0x42u, "SVC imm16 ISS must be 0x42");

    // Verify SPSR_EL1
    const std::uint32_t spsr = cpu.get_spsr_el1();
    STAGE62_ASSERT((spsr & 0x1Fu) == 0x00u, "SPSR M[4:0] must be EL0t (0x00)");

    // Now execute ERET at handler PC 0x80400
    // ERET opcode: 0xD69F03E0
    mem.write_u32(0x80400, 0xD69F03E0u);
    cpu.step(mem);

    // Verify CPU returned to EL0 at 0x1004
    STAGE62_ASSERT(cpu.get_current_el() == 0, "Current EL must be restored to EL0");
    STAGE62_ASSERT(cpu.get_program_counter() == 0x1004ULL, "PC must be restored to 0x1004");
    STAGE62_ASSERT(cpu.get_daif() == 0x0u, "DAIF must be restored to 0x0");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Data Abort with FAR & Syndrome Details
// -----------------------------------------------------------------------------
bool test_data_abort_syndrome_and_far() {
    std::cout << "[2] test_data_abort_syndrome_and_far...\n";
    CPU cpu;
    setup_cpu_el1(cpu, true, 0x2000ULL, 0x80000ULL);

    // Create a Data Abort from Same EL: Store (write) to 0xA0005000
    SynchronousException exc = make_data_abort(
        true,                             // same_el
        AccessType::Write,                // access
        FaultStatus::PermissionLevel3,    // fsc
        0xA0005000ULL,                    // fault_address
        0x2000ULL,                        // instruction_address
        4                                 // access_size
    );

    cpu.raise_synchronous_exception(exc);

    STAGE62_ASSERT(cpu.get_current_el() == 1, "Must remain in EL1");
    STAGE62_ASSERT(cpu.get_program_counter() == 0x80200ULL, "PC must be VBAR + 0x200 (Same EL SPx Sync)");
    STAGE62_ASSERT(cpu.get_elr_el1() == 0x2000ULL, "ELR must be faulting PC");
    STAGE62_ASSERT(cpu.get_far_el1() == 0xA0005000ULL, "FAR_EL1 must hold fault address");

    const std::uint32_t esr = cpu.get_esr_el1();
    const std::uint32_t ec = (esr >> 26u) & 0x3Fu;
    STAGE62_ASSERT(ec == 0x25u, "DataAbortSame EC must be 0x25");
    STAGE62_ASSERT((esr & (1ULL << 6u)) != 0, "WnR bit must be 1 for write");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Instruction Abort & Fetch Faults
// -----------------------------------------------------------------------------
bool test_instruction_abort_syndrome() {
    std::cout << "[3] test_instruction_abort_syndrome...\n";
    CPU cpu;
    setup_cpu_el0(cpu, 0x3000ULL, 0x90000ULL);

    // Instruction Abort from Lower EL (e.g. unmapped fetch address)
    SynchronousException exc = make_instruction_abort(
        false,                            // same_el = false (from lower EL)
        FaultStatus::TranslationLevel2,   // fsc
        0x3000ULL,                        // fault_address
        4                                 // access_size
    );

    cpu.raise_synchronous_exception(exc);

    STAGE62_ASSERT(cpu.get_current_el() == 1, "Must enter EL1");
    STAGE62_ASSERT(cpu.get_program_counter() == 0x90400ULL, "PC must be VBAR + 0x400 (Lower EL Sync)");
    STAGE62_ASSERT(cpu.get_elr_el1() == 0x3000ULL, "ELR must be faulting PC");
    STAGE62_ASSERT(cpu.get_far_el1() == 0x3000ULL, "FAR_EL1 must hold faulting fetch PC");

    const std::uint32_t esr = cpu.get_esr_el1();
    const std::uint32_t ec = (esr >> 26u) & 0x3Fu;
    STAGE62_ASSERT(ec == 0x20u, "InstructionAbortLower EC must be 0x20");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Undefined Instruction Exception
// -----------------------------------------------------------------------------
bool test_undefined_instruction_exception() {
    std::cout << "[4] test_undefined_instruction_exception...\n";
    Memory mem(0x100000);
    CPU cpu;
    setup_cpu_el0(cpu, 0x4000ULL, 0x60000ULL);

    // Unallocated opcode (e.g. 0x00000000)
    mem.write_u32(0x4000, 0x00000000u);

    cpu.step(mem);

    STAGE62_ASSERT(cpu.get_current_el() == 1, "Must enter EL1");
    STAGE62_ASSERT(cpu.get_program_counter() == 0x60400ULL, "PC must be VBAR + 0x400");
    STAGE62_ASSERT(cpu.get_elr_el1() == 0x4000ULL, "ELR must be faulting PC");

    const std::uint32_t esr = cpu.get_esr_el1();
    const std::uint32_t ec = (esr >> 26u) & 0x3Fu;
    STAGE62_ASSERT(ec == 0x00u, "Undefined instruction EC must be 0x00 (Unknown)");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: Alignment Fault Exception
// -----------------------------------------------------------------------------
bool test_alignment_fault_exception() {
    std::cout << "[5] test_alignment_fault_exception...\n";
    CPU cpu;
    setup_cpu_el1(cpu, true, 0x5000ULL, 0x70000ULL);

    SynchronousException exc = make_data_abort(
        true,                             // same_el
        AccessType::Write,                // access
        FaultStatus::Alignment,           // fsc (0x21)
        0x12345ULL,                       // fault_address
        0x5000ULL,                        // instruction_address
        4                                 // access_size
    );

    cpu.raise_synchronous_exception(exc);

    STAGE62_ASSERT(cpu.get_far_el1() == 0x12345ULL, "FAR_EL1 must hold misaligned address");
    const std::uint32_t esr = cpu.get_esr_el1();
    const std::uint32_t fsc = esr & 0x3Fu;
    STAGE62_ASSERT(fsc == 0x21u, "FSC must be 0x21 (Alignment Fault)");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Vector Selection Full Matrix
// -----------------------------------------------------------------------------
bool test_vector_selection_matrix() {
    std::cout << "[6] test_vector_selection_matrix...\n";

    // Current EL with SP0
    STAGE62_ASSERT(compute_vector_offset(1, 1, false, ExceptionType::Synchronous) == 0x000u, "Sync SP0");
    STAGE62_ASSERT(compute_vector_offset(1, 1, false, ExceptionType::IRQ)         == 0x080u, "IRQ SP0");
    STAGE62_ASSERT(compute_vector_offset(1, 1, false, ExceptionType::FIQ)         == 0x100u, "FIQ SP0");
    STAGE62_ASSERT(compute_vector_offset(1, 1, false, ExceptionType::SError)      == 0x180u, "SError SP0");

    // Current EL with SPx
    STAGE62_ASSERT(compute_vector_offset(1, 1, true, ExceptionType::Synchronous)  == 0x200u, "Sync SPx");
    STAGE62_ASSERT(compute_vector_offset(1, 1, true, ExceptionType::IRQ)          == 0x280u, "IRQ SPx");
    STAGE62_ASSERT(compute_vector_offset(1, 1, true, ExceptionType::FIQ)          == 0x300u, "FIQ SPx");
    STAGE62_ASSERT(compute_vector_offset(1, 1, true, ExceptionType::SError)       == 0x380u, "SError SPx");

    // Lower EL using AArch64
    STAGE62_ASSERT(compute_vector_offset(0, 1, false, ExceptionType::Synchronous) == 0x400u, "Lower A64 Sync");
    STAGE62_ASSERT(compute_vector_offset(0, 1, false, ExceptionType::IRQ)         == 0x480u, "Lower A64 IRQ");
    STAGE62_ASSERT(compute_vector_offset(0, 1, false, ExceptionType::FIQ)         == 0x500u, "Lower A64 FIQ");
    STAGE62_ASSERT(compute_vector_offset(0, 1, false, ExceptionType::SError)      == 0x580u, "Lower A64 SError");

    // Lower EL to EL2
    STAGE62_ASSERT(compute_vector_offset(1, 2, true, ExceptionType::Synchronous)  == 0x400u, "EL1 to EL2 Sync");
    STAGE62_ASSERT(compute_vector_offset(1, 2, true, ExceptionType::IRQ)          == 0x480u, "EL1 to EL2 IRQ");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: ERET Privilege Enforcement at EL0
// -----------------------------------------------------------------------------
bool test_eret_privilege_at_el0() {
    std::cout << "[7] test_eret_privilege_at_el0...\n";
    Memory mem(0x100000);
    CPU cpu;
    setup_cpu_el0(cpu, 0x6000ULL, 0x80000ULL);

    // ERET at EL0
    mem.write_u32(0x6000, 0xD69F03E0u);

    cpu.step(mem);

    // Must trap to EL1 as undefined instruction
    STAGE62_ASSERT(cpu.get_current_el() == 1, "ERET at EL0 must trap to EL1");
    STAGE62_ASSERT(cpu.get_program_counter() == 0x80400ULL, "PC must be VBAR + 0x400");
    const std::uint32_t esr = cpu.get_esr_el1();
    STAGE62_ASSERT(((esr >> 26u) & 0x3Fu) == 0x00u, "EC must be 0x00 (Unknown/Undef)");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: ERET Illegal Return State & PSTATE.IL
// -----------------------------------------------------------------------------
bool test_eret_illegal_state_pstate_il() {
    std::cout << "[8] test_eret_illegal_state_pstate_il...\n";
    Memory mem(0x100000);
    CPU cpu;
    setup_cpu_el1(cpu, true, 0x7000ULL, 0x80000ULL);

    // Set up SPSR_EL1 with illegal target EL (e.g. M[3:2] = 2 -> targeting EL2 from EL1)
    // SPSR M = 0b1000 (EL2t) -> illegal from EL1
    cpu.set_spsr_el1(0x08u);
    cpu.set_elr_el1(0x20000ULL);

    mem.write_u32(0x7000, 0xD69F03E0u); // ERET
    cpu.step(mem);

    // Architectural illegal exception return sets PSTATE.IL = 1
    STAGE62_ASSERT(cpu.get_il() == true, "PSTATE.IL must be set on illegal ERET");
    STAGE62_ASSERT(cpu.get_program_counter() == 0x20000ULL, "PC must be set to ELR");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: SPSR Register Bit Integrity across Entry and Return
// -----------------------------------------------------------------------------
bool test_spsr_integrity_roundtrip() {
    std::cout << "[9] test_spsr_integrity_roundtrip...\n";
    Memory mem(0x100000);
    CPU cpu;
    setup_cpu_el0(cpu, 0x1000ULL, 0x80000ULL);

    // Set specific flags in EL0: N=1, Z=0, C=1, V=0 -> NZCV = 0xA
    cpu.set_nzcv(0xAu);
    cpu.set_daif(0x5u); // D=0, A=1, I=0, F=1

    // Write SVC instruction at 0x1000
    mem.write_u32(0x1000, 0xD4000021u); // SVC #1
    cpu.step(mem);

    // SPSR_EL1 must hold NZCV = 0xA in [31:28] and DAIF = 0x5 in [9:6]
    const std::uint32_t spsr = cpu.get_spsr_el1();
    STAGE62_ASSERT(((spsr >> 28u) & 0xFu) == 0xAu, "SPSR NZCV must be 0xA");
    STAGE62_ASSERT(((spsr >> 6u) & 0xFu) == 0x5u, "SPSR DAIF must be 0x5");

    // Clear flags in EL1 handler
    cpu.set_nzcv(0x0u);
    cpu.set_daif(0xFu);

    // Execute ERET
    mem.write_u32(cpu.get_program_counter(), 0xD69F03E0u);
    cpu.step(mem);

    // Verify restored flags
    STAGE62_ASSERT(cpu.get_nzcv() == 0xAu, "Restored NZCV must be 0xA");
    STAGE62_ASSERT(cpu.get_daif() == 0x5u, "Restored DAIF must be 0x5");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: Multi-Level Exception Isolation (EL0 -> EL1 -> EL2)
// -----------------------------------------------------------------------------
bool test_multilevel_exception_isolation() {
    std::cout << "[10] test_multilevel_exception_isolation...\n";
    Memory mem(0x100000);
    CPU cpu;
    setup_cpu_el0(cpu, 0x1000ULL, 0x80000ULL);
    cpu.set_vbar_el2(0x90000ULL);

    // 1. Take SVC to EL1
    mem.write_u32(0x1000, 0xD4000021u);
    cpu.step(mem);
    STAGE62_ASSERT(cpu.get_current_el() == 1, "Must be EL1");
    STAGE62_ASSERT(cpu.get_elr_el1() == 0x1004ULL, "EL1 ELR must be 0x1004");

    // 2. In EL1, execute HVC to EL2
    // HVC #0x10 opcode: 0xD4000202u
    const std::uint64_t el1_pc = cpu.get_program_counter();
    mem.write_u32(el1_pc, 0xD4000202u);
    cpu.step(mem);

    STAGE62_ASSERT(cpu.get_current_el() == 2, "Must enter EL2");
    STAGE62_ASSERT(cpu.get_elr_el2() == el1_pc + 4u, "EL2 ELR must be EL1 PC + 4");
    STAGE62_ASSERT(cpu.get_elr_el1() == 0x1004ULL, "EL1 ELR must remain 0x1004 (isolated)");

    // 3. ERET from EL2 returns to EL1
    const std::uint64_t el2_pc = cpu.get_program_counter();
    mem.write_u32(el2_pc, 0xD69F03E0u);
    cpu.step(mem);

    STAGE62_ASSERT(cpu.get_current_el() == 1, "Must return to EL1");
    STAGE62_ASSERT(cpu.get_program_counter() == el1_pc + 4u, "Must return to EL1 PC + 4");

    // 4. ERET from EL1 returns to EL0
    mem.write_u32(el1_pc + 4u, 0xD69F03E0u);
    cpu.step(mem);

    STAGE62_ASSERT(cpu.get_current_el() == 0, "Must return to EL0");
    STAGE62_ASSERT(cpu.get_program_counter() == 0x1004ULL, "Must return to 0x1004");

    return true;
}

int main() {
    std::cout << "=== Running Stage 62 Exception Architecture Tests ===" << std::endl;

    if (!test_svc_entry_and_return()) return 1;
    if (!test_data_abort_syndrome_and_far()) return 2;
    if (!test_instruction_abort_syndrome()) return 3;
    if (!test_undefined_instruction_exception()) return 4;
    if (!test_alignment_fault_exception()) return 5;
    if (!test_vector_selection_matrix()) return 6;
    if (!test_eret_privilege_at_el0()) return 7;
    if (!test_eret_illegal_state_pstate_il()) return 8;
    if (!test_spsr_integrity_roundtrip()) return 9;
    if (!test_multilevel_exception_isolation()) return 10;

    std::cout << "stage62_exception_tests: all 10 test suites passed successfully" << std::endl;
    return 0;
}

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/synchronous_exception.h"
#include "memory/memory.h"
#include "runtime/darwin_userspace_engine.h"

using namespace winios;
using namespace winios::runtime;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Helper to encode SVC #imm16: 0xD4000001 | (imm16 << 5)
std::uint32_t encode_svc(std::uint16_t imm16) {
    return 0xD4000001u | (static_cast<std::uint32_t>(imm16) << 5u);
}

// ERET instruction: 0xD69F03E0
constexpr std::uint32_t kEretInstruction = 0xD69F03E0u;

void setup_test_cpu(CPU& cpu, Memory& mem, std::uint64_t vbar_el1 = 0x10000) {
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(vbar_el1);

    // Populate vector slots with NOPs
    for (std::uint64_t offset = 0; offset < 0x800; offset += 0x80) {
        if (vbar_el1 + offset + 4 <= mem.size()) {
            mem.write_u32(vbar_el1 + offset, 0xD503201Fu); // NOP
        }
    }
}

// Test 1: Conformance of ARM64 Vector Table Offsets (XNU locore.s)
bool test_xnu_arm64_vector_offsets() {
    std::cout << "[Test 1] Conformance of ARM64 Vector Table Offsets (XNU locore.s)...\n";

    // 1. Lower EL using AArch64 (Origin EL0 -> Target EL1)
    TEST_ASSERT(compute_vector_offset(0, 1, false, ExceptionType::Synchronous) == 0x400u,
                "Lower EL AArch64 Synchronous vector must be +0x400");
    TEST_ASSERT(compute_vector_offset(0, 1, false, ExceptionType::IRQ) == 0x480u,
                "Lower EL AArch64 IRQ vector must be +0x480");
    TEST_ASSERT(compute_vector_offset(0, 1, false, ExceptionType::FIQ) == 0x500u,
                "Lower EL AArch64 FIQ vector must be +0x500");
    TEST_ASSERT(compute_vector_offset(0, 1, false, ExceptionType::SError) == 0x580u,
                "Lower EL AArch64 SError vector must be +0x580");

    // 2. Current EL with SPx (Origin EL1 -> Target EL1, sp_sel = true)
    TEST_ASSERT(compute_vector_offset(1, 1, true, ExceptionType::Synchronous) == 0x200u,
                "Current EL SPx Synchronous vector must be +0x200");
    TEST_ASSERT(compute_vector_offset(1, 1, true, ExceptionType::IRQ) == 0x280u,
                "Current EL SPx IRQ vector must be +0x280");
    TEST_ASSERT(compute_vector_offset(1, 1, true, ExceptionType::FIQ) == 0x300u,
                "Current EL SPx FIQ vector must be +0x300");
    TEST_ASSERT(compute_vector_offset(1, 1, true, ExceptionType::SError) == 0x380u,
                "Current EL SPx SError vector must be +0x380");

    // 3. Current EL with SP0 (Origin EL1 -> Target EL1, sp_sel = false)
    TEST_ASSERT(compute_vector_offset(1, 1, false, ExceptionType::Synchronous) == 0x000u,
                "Current EL SP0 Synchronous vector must be +0x000");
    TEST_ASSERT(compute_vector_offset(1, 1, false, ExceptionType::IRQ) == 0x080u,
                "Current EL SP0 IRQ vector must be +0x080");
    TEST_ASSERT(compute_vector_offset(1, 1, false, ExceptionType::FIQ) == 0x100u,
                "Current EL SP0 FIQ vector must be +0x100");
    TEST_ASSERT(compute_vector_offset(1, 1, false, ExceptionType::SError) == 0x180u,
                "Current EL SP0 SError vector must be +0x180");

    return true;
}

// Test 2: XNU SVC Syndrome Encoding and PSTATE Preservation (locore.s / trap.c)
bool test_xnu_svc_syndrome_encoding() {
    std::cout << "[Test 2] XNU SVC Syndrome Encoding and PSTATE Preservation...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    setup_test_cpu(cpu, mem, 0x10000);

    // Initial state: EL0, SP_EL0=0x40000, SP_EL1=0x50000, NZCV=0xA (N=1, C=1)
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_sp_el0(0x40000);
    cpu.set_sp_el1(0x50000);
    cpu.set_stack_pointer(0x40000);
    cpu.set_program_counter(0x2000);
    cpu.set_nzcv(0xAu);

    // Encode SVC #0x80 (Darwin BSD syscall trap)
    mem.write_u32(0x2000, encode_svc(0x80));
    cpu.step(mem);

    // 1. Transition to EL1
    TEST_ASSERT(cpu.get_current_el() == 1, "Must transition to EL1");
    TEST_ASSERT(cpu.get_program_counter() == 0x10400, "PC must branch to VBAR_EL1 + 0x400");
    TEST_ASSERT(cpu.get_elr_el1() == 0x2004, "ELR_EL1 must point to instruction after SVC (0x2004)");

    // 2. ESR_EL1 validation: EC = 0x15, IL = 1, ISS = 0x80
    std::uint32_t esr = cpu.get_esr_el1();
    TEST_ASSERT(((esr >> 26u) & 0x3Fu) == 0x15u, "ESR_EL1 EC must be 0x15");
    TEST_ASSERT(((esr >> 25u) & 0x1u) == 1u, "ESR_EL1 IL must be 1");
    TEST_ASSERT((esr & 0xFFFFu) == 0x80u, "ESR_EL1 ISS must match SVC immediate (0x80)");

    // 3. SPSR_EL1 validation: Mode = 0 (EL0t), NZCV = 0xA preserved, DAIF masked in EL1
    std::uint32_t spsr = cpu.get_spsr_el1();
    TEST_ASSERT((spsr & 0x1Fu) == 0x0u, "SPSR_EL1 mode must be EL0t (0b00000)");
    TEST_ASSERT(((spsr >> 28u) & 0xFu) == 0xAu, "SPSR_EL1 NZCV must preserve 0xA");

    // 4. Test SVC #0x81 (Darwin Mach trap)
    cpu.set_current_el(0);
    cpu.set_program_counter(0x3000);
    mem.write_u32(0x3000, encode_svc(0x81));
    cpu.step(mem);

    esr = cpu.get_esr_el1();
    TEST_ASSERT(((esr >> 26u) & 0x3Fu) == 0x15u, "ESR_EL1 EC must be 0x15 for SVC #0x81");
    TEST_ASSERT((esr & 0xFFFFu) == 0x81u, "ESR_EL1 ISS must match SVC immediate 0x81");

    return true;
}

// Test 3: XNU Undefined Instruction Syndrome (EC = 0x00)
bool test_xnu_undefined_instruction_syndrome() {
    std::cout << "[Test 3] XNU Undefined Instruction Syndrome (EC = 0x00)...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    setup_test_cpu(cpu, mem, 0x10000);

    cpu.set_current_el(0);
    cpu.set_sp_el0(0x40000);
    cpu.set_sp_el1(0x50000);
    cpu.set_stack_pointer(0x40000);
    cpu.set_program_counter(0x4000);

    // Opcode 0x00000000 is architecturally undefined in A64
    mem.write_u32(0x4000, 0x00000000u);
    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 1, "Must transition to EL1 on undefined opcode");
    TEST_ASSERT(cpu.get_program_counter() == 0x10400, "Must branch to Lower EL sync vector");
    TEST_ASSERT(cpu.get_elr_el1() == 0x4000, "ELR_EL1 must point to faulting instruction");

    const std::uint32_t esr = cpu.get_esr_el1();
    const std::uint32_t ec = (esr >> 26u) & 0x3Fu;
    TEST_ASSERT(ec == 0x00u, "ESR_EL1 EC must be 0x00 for Undefined Instruction");

    return true;
}

// Test 4: XNU Data Abort Syndrome (EC = 0x24) and FAR_EL1 Recording
bool test_xnu_data_abort_syndrome() {
    std::cout << "[Test 4] XNU Data Abort Syndrome (EC = 0x24) and FAR_EL1 Recording...\n";

    // Create a small memory space (64KB)
    Memory mem(64 * 1024);
    CPU cpu;
    setup_test_cpu(cpu, mem, 0x8000);

    cpu.set_current_el(0);
    cpu.set_program_counter(0x1000);

    // LDR X0, [X1] where X1 points out-of-bounds (0xDEADBEEF0ULL)
    // 0xF9400020 = ldr x0, [x1]
    mem.write_u32(0x1000, 0xF9400020u);
    cpu.write_register(1, 0xDEADBEEF0ULL);

    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 1, "Must transition to EL1 on Data Abort");
    TEST_ASSERT(cpu.get_program_counter() == 0x8400, "Must branch to Lower EL sync vector");
    TEST_ASSERT(cpu.get_elr_el1() == 0x1000, "ELR_EL1 must point to faulting load instruction");

    const std::uint32_t esr = cpu.get_esr_el1();
    const std::uint32_t ec = (esr >> 26u) & 0x3Fu;
    TEST_ASSERT(ec == 0x24u, "ESR_EL1 EC must be 0x24 for Data Abort from lower EL");
    TEST_ASSERT(cpu.get_far_el1() == 0xDEADBEEF0ULL, "FAR_EL1 must record exact faulting virtual address");

    return true;
}

// Test 5: XNU Instruction Abort Syndrome (EC = 0x20) and FAR_EL1 Recording
bool test_xnu_instruction_abort_syndrome() {
    std::cout << "[Test 5] XNU Instruction Abort Syndrome (EC = 0x20) and FAR_EL1 Recording...\n";

    Memory mem(64 * 1024);
    CPU cpu;
    setup_test_cpu(cpu, mem, 0x8000);

    // Execute directly from an out-of-bounds address (0xF0000000ULL)
    cpu.set_current_el(0);
    cpu.set_program_counter(0xF0000000ULL);

    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 1, "Must transition to EL1 on Instruction Abort");
    TEST_ASSERT(cpu.get_program_counter() == 0x8400, "Must branch to Lower EL sync vector");
    TEST_ASSERT(cpu.get_elr_el1() == 0xF0000000ULL, "ELR_EL1 must point to faulting PC");

    const std::uint32_t esr = cpu.get_esr_el1();
    const std::uint32_t ec = (esr >> 26u) & 0x3Fu;
    TEST_ASSERT(ec == 0x20u, "ESR_EL1 EC must be 0x20 for Instruction Abort from lower EL");
    TEST_ASSERT(cpu.get_far_el1() == 0xF0000000ULL, "FAR_EL1 must record exact faulting PC");

    return true;
}

// Test 6: XNU ERET State Restoration and EL0 Return
bool test_xnu_eret_state_restoration() {
    std::cout << "[Test 6] XNU ERET State Restoration and EL0 Return...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    setup_test_cpu(cpu, mem, 0x10000);

    // Simulate kernel handler in EL1 executing ERET
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_sp_el0(0x40000);
    cpu.set_sp_el1(0x50000);
    cpu.set_stack_pointer(0x50000);

    // Program ELR_EL1 to return to 0x2004 in user code
    cpu.set_elr_el1(0x2004);

    // SPSR_EL1: Mode 0 (EL0t), NZCV = 0x4 (Z=1), DAIF = 0
    std::uint32_t spsr = (0x4u << 28u); // Z=1, mode=EL0
    cpu.set_spsr_el1(spsr);

    // Place ERET at 0x10400
    cpu.set_program_counter(0x10400);
    mem.write_u32(0x10400, kEretInstruction);

    // Step ERET instruction
    cpu.step(mem);

    // 1. Current EL must be restored to EL0
    TEST_ASSERT(cpu.get_current_el() == 0, "Current EL must be 0 after ERET");

    // 2. PC must be restored to ELR_EL1 (0x2004)
    TEST_ASSERT(cpu.get_program_counter() == 0x2004, "PC must be restored from ELR_EL1 (0x2004)");

    // 3. NZCV must match SPSR_EL1 (0x4)
    TEST_ASSERT(cpu.get_nzcv() == 0x4u, "NZCV must be restored from SPSR_EL1 (Z=1)");

    // 4. SP must be SP_EL0
    TEST_ASSERT(cpu.get_sp_sel() == false, "SPSel must be 0 (EL0t)");
    TEST_ASSERT(cpu.get_stack_pointer() == 0x40000, "Active stack pointer must be SP_EL0 (0x40000)");

    return true;
}

// Test 7: Multi-Step Exception Re-entrancy and Syscall Round Trip
bool test_xnu_multistep_exception_cycle() {
    std::cout << "[Test 7] Multi-Step Exception Re-entrancy and Syscall Round Trip...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    setup_test_cpu(cpu, mem, 0x10000);

    // 1. Set up userspace at 0x2000
    // User code:
    // 0x2000: mov x0, #42
    // 0x2004: mov x16, #1
    // 0x2008: svc #0x80
    // 0x200C: add x0, x0, #1
    // 0x2010: nop
    mem.write_u32(0x2000, 0xD2800540u); // mov x0, #42
    mem.write_u32(0x2004, 0xD2800030u); // mov x16, #1
    mem.write_u32(0x2008, encode_svc(0x80));
    mem.write_u32(0x200C, 0x91000400u); // add x0, x0, #1
    mem.write_u32(0x2010, 0xD503201Fu); // nop

    // Kernel exception handler at 0x10400:
    // 0x10400: add x0, x0, #100  (kernel modifies return value)
    // 0x10404: eret
    mem.write_u32(0x10400, 0x91019000u); // add x0, x0, #100
    mem.write_u32(0x10404, kEretInstruction);

    cpu.set_current_el(0);
    cpu.set_sp_el0(0x40000);
    cpu.set_sp_el1(0x50000);
    cpu.set_stack_pointer(0x40000);
    cpu.set_program_counter(0x2000);

    // Step 1: mov x0, #42
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 42, "X0 must be 42");
    TEST_ASSERT(cpu.get_current_el() == 0, "Must be EL0");

    // Step 2: mov x16, #1
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(16) == 1, "X16 must be 1");

    // Step 3: svc #0x80 -> traps to 0x10400 at EL1
    cpu.step(mem);
    TEST_ASSERT(cpu.get_current_el() == 1, "Must be EL1");
    TEST_ASSERT(cpu.get_program_counter() == 0x10400, "PC must be at vector 0x10400");
    TEST_ASSERT(cpu.get_elr_el1() == 0x200C, "ELR_EL1 must point to user instruction at 0x200C");

    // Step 4: Kernel executes: add x0, x0, #100
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 142, "X0 must be 142 in kernel handler");
    TEST_ASSERT(cpu.get_program_counter() == 0x10404, "PC must be at 0x10404");

    // Step 5: Kernel executes ERET -> returns to EL0 at 0x200C
    cpu.step(mem);
    TEST_ASSERT(cpu.get_current_el() == 0, "Must return to EL0");
    TEST_ASSERT(cpu.get_program_counter() == 0x200C, "PC must return to 0x200C");
    TEST_ASSERT(cpu.read_register(0) == 142, "X0 must retain 142 upon EL0 return");

    // Step 6: User code executes: add x0, x0, #1
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 143, "X0 must be 143 after user resume");
    TEST_ASSERT(cpu.get_program_counter() == 0x2010, "PC must advance to 0x2010");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 157: Apple XNU Exception & SVC Source Conformance\n";
    std::cout << "=========================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](auto test_fn, const char* name) {
        if (test_fn()) {
            std::cout << "[PASS] " << name << "\n";
            passed++;
        } else {
            std::cout << "[FAIL] " << name << "\n";
            failed++;
        }
    };

    run_test(test_xnu_arm64_vector_offsets, "ARM64 Vector Table Offsets Conformance");
    run_test(test_xnu_svc_syndrome_encoding, "SVC Syndrome Encoding and PSTATE Preservation");
    run_test(test_xnu_undefined_instruction_syndrome, "Undefined Instruction Syndrome (EC = 0x00)");
    run_test(test_xnu_data_abort_syndrome, "Data Abort Syndrome (EC = 0x24) & FAR_EL1");
    run_test(test_xnu_instruction_abort_syndrome, "Instruction Abort Syndrome (EC = 0x20) & FAR_EL1");
    run_test(test_xnu_eret_state_restoration, "ERET State Restoration and EL0 Return");
    run_test(test_xnu_multistep_exception_cycle, "Multi-Step Exception Re-entrancy & Syscall Round Trip");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}

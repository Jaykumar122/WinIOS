#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/sysreg.h"
#include "memory/memory.h"

using namespace winios;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Helper to encode MRS Xt, <sysreg>
std::uint32_t encode_mrs(std::uint8_t op0, std::uint8_t op1, std::uint8_t crn, std::uint8_t crm, std::uint8_t op2, std::uint32_t rt) {
    const std::uint32_t o0 = (op0 == 2) ? 0u : 1u;
    return 0xD5300000u | (o0 << 19u) | (static_cast<std::uint32_t>(op1 & 7u) << 16u) |
           (static_cast<std::uint32_t>(crn & 15u) << 12u) | (static_cast<std::uint32_t>(crm & 15u) << 8u) |
           (static_cast<std::uint32_t>(op2 & 7u) << 5u) | (rt & 31u);
}

// Helper to encode MSR <sysreg>, Xt
std::uint32_t encode_msr(std::uint8_t op0, std::uint8_t op1, std::uint8_t crn, std::uint8_t crm, std::uint8_t op2, std::uint32_t rt) {
    const std::uint32_t o0 = (op0 == 2) ? 0u : 1u;
    return 0xD5100000u | (o0 << 19u) | (static_cast<std::uint32_t>(op1 & 7u) << 16u) |
           (static_cast<std::uint32_t>(crn & 15u) << 12u) | (static_cast<std::uint32_t>(crm & 15u) << 8u) |
           (static_cast<std::uint32_t>(op2 & 7u) << 5u) | (rt & 31u);
}

// Setup CPU in EL0 Userspace with basic RAM and VBAR_EL1 vector table
void setup_el0_environment(CPU& cpu, Memory& mem, std::uint64_t code_addr = 0x1000, std::uint64_t vbar_el1 = 0x8000) {
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(vbar_el1);

    // Place NOPs at synchronous vector offsets in VBAR_EL1:
    // +0x400: Synchronous lower EL with AArch64
    // +0x000: Synchronous current EL with SP0
    // +0x200: Synchronous current EL with SPx
    mem.write_u32(vbar_el1 + 0x000, 0xD503201Fu); // NOP
    mem.write_u32(vbar_el1 + 0x200, 0xD503201Fu); // NOP
    mem.write_u32(vbar_el1 + 0x400, 0xD503201Fu); // NOP

    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_stack_pointer(0x20000);
    cpu.set_sp_el0(0x20000);
    cpu.set_sp_el1(0x30000);
    cpu.set_program_counter(code_addr);
}

// Test 1: Valid EL0 Instruction Execution, PC Advancement, and Retirement Tracking
bool test_el0_valid_instruction_execution() {
    std::cout << "[Test 1] Valid EL0 instruction execution & retirement tracking...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    setup_el0_environment(cpu, mem, 0x1000);

    // Assembly:
    // 0x1000: MOV X0, #42       (0xD2800540)
    // 0x1004: ADD X1, X0, #8    (0x91002001) -> X1 = 50
    // 0x1008: SUB X2, X1, #10   (0xD1002822) -> X2 = 40
    mem.write_u32(0x1000, 0xD2800540u);
    mem.write_u32(0x1004, 0x91002001u);
    mem.write_u32(0x1008, 0xD1002822u);

    TEST_ASSERT(cpu.get_current_el() == 0, "CPU must start in EL0");
    TEST_ASSERT(cpu.get_instruction_retirement_count() == 0, "Retirement counter must be 0 initially");

    cpu.step(mem);
    TEST_ASSERT(cpu.get_current_el() == 0, "CPU must remain in EL0");
    TEST_ASSERT(cpu.read_register(0) == 42, "X0 must equal 42");
    TEST_ASSERT(cpu.get_program_counter() == 0x1004, "PC must advance to 0x1004");
    TEST_ASSERT(cpu.get_instruction_retirement_count() == 1, "Retirement count must be 1");

    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(1) == 50, "X1 must equal 50");
    TEST_ASSERT(cpu.get_program_counter() == 0x1008, "PC must advance to 0x1008");
    TEST_ASSERT(cpu.get_instruction_retirement_count() == 2, "Retirement count must be 2");

    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(2) == 40, "X2 must equal 40");
    TEST_ASSERT(cpu.get_program_counter() == 0x100C, "PC must advance to 0x100C");
    TEST_ASSERT(cpu.get_instruction_retirement_count() == 3, "Retirement count must be 3");

    return true;
}

// Test 2: EL0 vs EL1 Stack Pointer Separation (SP_EL0 vs SP_EL1)
bool test_el0_stack_pointer_separation() {
    std::cout << "[Test 2] EL0 vs EL1 stack pointer separation...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    setup_el0_environment(cpu, mem, 0x1000);

    TEST_ASSERT(cpu.get_current_el() == 0, "CPU must be in EL0");
    TEST_ASSERT(cpu.get_sp_sel() == false, "SPSel must be 0 (SP_EL0) in EL0");
    TEST_ASSERT(cpu.get_stack_pointer() == 0x20000, "Active SP must be SP_EL0 (0x20000)");

    // Modify SP in EL0
    cpu.set_stack_pointer(0x21000);
    TEST_ASSERT(cpu.get_sp_el0() == 0x21000, "SP_EL0 must update to 0x21000");
    TEST_ASSERT(cpu.get_sp_el1() == 0x30000, "SP_EL1 must remain unchanged at 0x30000");

    // Deliver an architectural exception to EL1
    // In EL1 with SPSel=1, stack pointer must switch to SP_EL1
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    TEST_ASSERT(cpu.get_stack_pointer() == 0x30000, "Active SP in EL1 must be SP_EL1 (0x30000)");

    // Modify SP in EL1
    cpu.set_stack_pointer(0x35000);
    TEST_ASSERT(cpu.get_sp_el1() == 0x35000, "SP_EL1 must update to 0x35000");
    TEST_ASSERT(cpu.get_sp_el0() == 0x21000, "SP_EL0 must still be 0x21000");

    // Return to EL0
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    TEST_ASSERT(cpu.get_stack_pointer() == 0x21000, "Active SP in EL0 must be restored to 0x21000");

    return true;
}

// Test 3: EL0 User Sysreg Access (NZCV and TPIDR_EL0)
bool test_el0_user_sysreg_access() {
    std::cout << "[Test 3] EL0 user sysreg access (NZCV, TPIDR_EL0)...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    setup_el0_environment(cpu, mem, 0x1000);

    // 1. MSR TPIDR_EL0, X0: set thread pointer to 0xCAFE1234
    // 2. MRS X1, TPIDR_EL0: read thread pointer into X1
    // 3. MSR NZCV, X2: set flags N=1, Z=0, C=1, V=0 (0xA0000000)
    // 4. MRS X3, NZCV: read flags into X3
    cpu.write_register(0, 0xCAFE1234ULL);
    cpu.write_register(2, 0xA0000000ULL);

    const std::uint32_t msr_tpidr = encode_msr(3, 3, 13, 0, 2, 0); // MSR TPIDR_EL0, X0
    const std::uint32_t mrs_tpidr = encode_mrs(3, 3, 13, 0, 2, 1); // MRS X1, TPIDR_EL0
    const std::uint32_t msr_nzcv  = encode_msr(3, 3, 4, 2, 0, 2);  // MSR NZCV, X2
    const std::uint32_t mrs_nzcv  = encode_mrs(3, 3, 4, 2, 0, 3);  // MRS X3, NZCV

    mem.write_u32(0x1000, msr_tpidr);
    mem.write_u32(0x1004, mrs_tpidr);
    mem.write_u32(0x1008, msr_nzcv);
    mem.write_u32(0x100C, mrs_nzcv);

    cpu.step(mem);
    TEST_ASSERT(cpu.get_tpidr_el0() == 0xCAFE1234ULL, "TPIDR_EL0 must be written");
    TEST_ASSERT(cpu.get_instruction_retirement_count() == 1, "MSR TPIDR_EL0 must retire");

    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(1) == 0xCAFE1234ULL, "X1 must contain TPIDR_EL0 value");
    TEST_ASSERT(cpu.get_instruction_retirement_count() == 2, "MRS TPIDR_EL0 must retire");

    cpu.step(mem);
    TEST_ASSERT(cpu.get_nzcv() == 0xAu, "NZCV must be 0xA (N=1, C=1)");
    TEST_ASSERT(cpu.get_instruction_retirement_count() == 3, "MSR NZCV must retire");

    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(3) == 0xA0000000ULL, "X3 must contain NZCV shifted value");
    TEST_ASSERT(cpu.get_instruction_retirement_count() == 4, "MRS NZCV must retire");

    return true;
}

// Test 4: Privileged Instruction Rejection: ERET at EL0
bool test_el0_reject_eret() {
    std::cout << "[Test 4] Privileged instruction rejection: ERET at EL0...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    setup_el0_environment(cpu, mem, 0x1000, 0x8000);

    // Write ERET (0xD69F03E0) at 0x1000
    mem.write_u32(0x1000, 0xD69F03E0u);

    cpu.step(mem);

    // ERET at EL0 must trigger an Undefined Instruction exception to EL1
    TEST_ASSERT(cpu.get_current_el() == 1, "Exception must transition CPU to EL1");
    TEST_ASSERT(cpu.get_program_counter() == 0x8000 + 0x400, "PC must vector to VBAR_EL1 + 0x400 (lower EL sync)");
    TEST_ASSERT(cpu.get_elr_el1() == 0x1000, "ELR_EL1 must point to faulting instruction at 0x1000");

    const std::uint32_t esr = cpu.get_esr_el1();
    const std::uint32_t ec = (esr >> 26u) & 0x3Fu;
    TEST_ASSERT(ec == 0x00u, "ESR_EL1 EC must be 0x00 (Unknown / Undefined instruction)");
    TEST_ASSERT(cpu.get_instruction_retirement_count() == 0, "Rejected ERET must NOT retire");

    return true;
}

// Test 5: Privileged Instruction Rejection: HVC and SMC at EL0
bool test_el0_reject_hvc_smc() {
    std::cout << "[Test 5] Privileged instruction rejection: HVC and SMC at EL0...\n";

    Memory mem(1024 * 1024);
    CPU cpu;

    // Test HVC at EL0
    setup_el0_environment(cpu, mem, 0x1000, 0x8000);
    mem.write_u32(0x1000, 0xD4000002u); // HVC #0
    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 1, "HVC at EL0 must trap to EL1");
    TEST_ASSERT(cpu.get_program_counter() == 0x8400, "PC must vector to VBAR_EL1 + 0x400");
    TEST_ASSERT(cpu.get_elr_el1() == 0x1000, "ELR_EL1 must point to 0x1000");
    TEST_ASSERT(((cpu.get_esr_el1() >> 26u) & 0x3Fu) == 0x00u, "HVC at EL0 must report Undefined EC=0x00");
    TEST_ASSERT(cpu.get_instruction_retirement_count() == 0, "Rejected HVC must NOT retire");

    // Test SMC at EL0
    setup_el0_environment(cpu, mem, 0x2000, 0x8000);
    mem.write_u32(0x2000, 0xD4000003u); // SMC #0
    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 1, "SMC at EL0 must trap to EL1");
    TEST_ASSERT(cpu.get_program_counter() == 0x8400, "PC must vector to VBAR_EL1 + 0x400");
    TEST_ASSERT(cpu.get_elr_el1() == 0x2000, "ELR_EL1 must point to 0x2000");
    TEST_ASSERT(((cpu.get_esr_el1() >> 26u) & 0x3Fu) == 0x00u, "SMC at EL0 must report Undefined EC=0x00");
    TEST_ASSERT(cpu.get_instruction_retirement_count() == 0, "Rejected SMC must NOT retire");

    return true;
}

// Test 6: Privileged Sysreg Access Rejection at EL0 (SCTLR_EL1, TTBR0_EL1, VBAR_EL1)
bool test_el0_reject_privileged_sysregs() {
    std::cout << "[Test 6] Privileged sysreg access rejection at EL0 (SCTLR, TTBR0, VBAR)...\n";

    Memory mem(1024 * 1024);
    CPU cpu;

    // 1. MRS X0, SCTLR_EL1 at EL0 (s3_0_c1_c0_0)
    setup_el0_environment(cpu, mem, 0x1000, 0x8000);
    const std::uint32_t mrs_sctlr = encode_mrs(3, 0, 1, 0, 0, 0);
    mem.write_u32(0x1000, mrs_sctlr);
    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 1, "Privileged MRS must trap to EL1");
    TEST_ASSERT(cpu.get_program_counter() == 0x8400, "PC must vector to VBAR_EL1 + 0x400");
    TEST_ASSERT(((cpu.get_esr_el1() >> 26u) & 0x3Fu) == 0x00u, "MRS SCTLR_EL1 at EL0 must report Undefined");
    TEST_ASSERT(cpu.get_instruction_retirement_count() == 0, "Trapped MRS must NOT retire");

    // 2. MSR TTBR0_EL1, X0 at EL0 (s3_0_c2_c0_0)
    setup_el0_environment(cpu, mem, 0x1000, 0x8000);
    const std::uint32_t msr_ttbr0 = encode_msr(3, 0, 2, 0, 0, 0);
    mem.write_u32(0x1000, msr_ttbr0);
    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 1, "Privileged MSR must trap to EL1");
    TEST_ASSERT(((cpu.get_esr_el1() >> 26u) & 0x3Fu) == 0x00u, "MSR TTBR0_EL1 at EL0 must report Undefined");
    TEST_ASSERT(cpu.get_instruction_retirement_count() == 0, "Trapped MSR must NOT retire");

    // 3. MRS X0, VBAR_EL1 at EL0 (s3_0_c12_c0_0)
    setup_el0_environment(cpu, mem, 0x1000, 0x8000);
    const std::uint32_t mrs_vbar = encode_mrs(3, 0, 12, 0, 0, 0);
    mem.write_u32(0x1000, mrs_vbar);
    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 1, "Privileged MRS VBAR must trap to EL1");
    TEST_ASSERT(((cpu.get_esr_el1() >> 26u) & 0x3Fu) == 0x00u, "MRS VBAR_EL1 at EL0 must report Undefined");
    TEST_ASSERT(cpu.get_instruction_retirement_count() == 0, "Trapped MRS must NOT retire");

    return true;
}

// Test 7: MSR SPSel at EL0 Rejection
bool test_el0_reject_msr_spsel() {
    std::cout << "[Test 7] Privileged MSR SPSel rejection at EL0...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    setup_el0_environment(cpu, mem, 0x1000, 0x8000);

    // MSR SPSel, #1: 0xD50041BFu
    // op1=0, crm=1, op2=5 -> 0xD500401F | (1<<8) | (5<<5) = 0xD50041BFu
    mem.write_u32(0x1000, 0xD50041BFu);
    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 1, "MSR SPSel at EL0 must trap to EL1");
    TEST_ASSERT(((cpu.get_esr_el1() >> 26u) & 0x3Fu) == 0x00u, "MSR SPSel at EL0 must report Undefined");
    TEST_ASSERT(cpu.get_instruction_retirement_count() == 0, "Trapped MSR SPSel must NOT retire");

    return true;
}

// Test 8: Register Preservation Across Exception Entry and Return
bool test_register_preservation_across_exception() {
    std::cout << "[Test 8] Register preservation across exception entry and return...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    setup_el0_environment(cpu, mem, 0x1000, 0x8000);

    // Set distinctive values in registers X0..X30
    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
        cpu.write_register(i, 0x1000000000000000ULL | (static_cast<std::uint64_t>(i) * 0x0101010101010101ULL));
    }

    // Trigger SVC #0x80
    mem.write_u32(0x1000, 0xD4001001u); // SVC #0x80
    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 1, "SVC must transition to EL1");
    TEST_ASSERT(cpu.get_elr_el1() == 0x1004, "ELR_EL1 must be preferred return address (0x1004)");

    // Verify all registers X0..X30 retain exact values
    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
        std::uint64_t expected = 0x1000000000000000ULL | (static_cast<std::uint64_t>(i) * 0x0101010101010101ULL);
        TEST_ASSERT(cpu.read_register(i) == expected, "Register X" + std::to_string(i) + " must be preserved");
    }

    // Emulate kernel returning to EL0 via ERET
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_program_counter(cpu.get_elr_el1());

    TEST_ASSERT(cpu.get_current_el() == 0, "CPU must be restored to EL0");
    TEST_ASSERT(cpu.get_program_counter() == 0x1004, "PC must be at return address 0x1004");

    return true;
}

// Test 9: Memory Access Boundary & Instruction Abort Handling
bool test_el0_memory_access_fault() {
    std::cout << "[Test 9] Memory access boundary & instruction abort handling...\n";

    Memory mem(64 * 1024); // 64 KB RAM
    CPU cpu;
    setup_el0_environment(cpu, mem, 0x1000, 0x8000);

    // Point PC to an address beyond physical RAM limit
    cpu.set_program_counter(0x200000); // 2 MB (outside 64 KB)
    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 1, "Instruction fetch abort must trap to EL1");
    TEST_ASSERT(cpu.get_program_counter() == 0x8400, "PC must vector to VBAR_EL1 + 0x400");
    const std::uint32_t ec = (cpu.get_esr_el1() >> 26u) & 0x3Fu;
    TEST_ASSERT(ec == 0x20u || ec == 0x21u, "ESR_EL1 EC must indicate Instruction Abort (0x20 or 0x21)");
    TEST_ASSERT(cpu.get_instruction_retirement_count() == 0, "Aborted instruction fetch must NOT retire");

    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  WINIOS STAGE 142: EL0 ARCHITECTURAL CONFORMANCE TEST SUITE      \n";
    std::cout << "=================================================================\n\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*fn)(), const char* name) {
        if (fn()) {
            std::cout << "  -> PASS: " << name << "\n\n";
            passed++;
        } else {
            std::cout << "  -> FAIL: " << name << "\n\n";
            failed++;
        }
    };

    run_test(test_el0_valid_instruction_execution, "Valid EL0 Instruction Execution & Retirement");
    run_test(test_el0_stack_pointer_separation, "EL0 vs EL1 Stack Pointer Separation");
    run_test(test_el0_user_sysreg_access, "EL0 User Sysreg Access (NZCV, TPIDR_EL0)");
    run_test(test_el0_reject_eret, "Privileged Instruction Rejection: ERET at EL0");
    run_test(test_el0_reject_hvc_smc, "Privileged Instruction Rejection: HVC and SMC at EL0");
    run_test(test_el0_reject_privileged_sysregs, "Privileged Sysreg Rejection: SCTLR, TTBR0, VBAR");
    run_test(test_el0_reject_msr_spsel, "Privileged MSR SPSel Rejection at EL0");
    run_test(test_register_preservation_across_exception, "Register Preservation Across EL Transitions");
    run_test(test_el0_memory_access_fault, "Memory Access Boundary & Instruction Abort");

    std::cout << "=================================================================\n";
    std::cout << "STAGE 142 RESULTS: " << passed << " PASSED, " << failed << " FAILED\n";
    std::cout << "=================================================================\n";

    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

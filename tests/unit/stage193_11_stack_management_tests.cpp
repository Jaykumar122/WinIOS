// ============================================================================
// Stage 193.11: Stack Management and Context Preservation Tests
// ============================================================================

#include <cstdint>
#include <iostream>

#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "memory/memory.h"
#include "cpu/synchronous_exception.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace mmu;

static bool test_el0_el1_stack_switching_and_preservation() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x400000ULL); // 4MB

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    const std::uint64_t user_sp = 0x00007FFFF000ULL;
    const std::uint64_t kernel_sp = 0x000000050000ULL; // PA 0x50000
    constexpr std::uint64_t kUserStackCanary = 0xA1B2C3D4E5F60718ULL;

    cpu.set_sp_el0(user_sp);
    cpu.set_sp_el1(kernel_sp);

    mem.write_u64(0x20000ULL, kUserStackCanary);

    // Set CPU to EL0
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    CHECK(cpu.get_stack_pointer() == user_sp, "Stack pointer at EL0 must be user_sp");

    // Guest EL0 executes SVC #0x10 (0xD4000201u) at 0x1000
    const std::uint64_t user_code_va = 0x1000ULL;
    mem.write_u32(static_cast<std::size_t>(user_code_va), 0xD4000201u);

    cpu.set_program_counter(user_code_va);
    cpu.write_register(0, 0x1111);

    // Step: EL0 SVC traps to EL1
    cpu.step(mem);

    CHECK(cpu.get_current_el() == 1, "Must enter EL1");
    CHECK(cpu.get_sp_sel() == true, "Must automatically switch to SP_EL1 (EL1h)");
    CHECK(cpu.get_stack_pointer() == kernel_sp, "Stack pointer in EL1 handler must be kernel_sp");
    CHECK(cpu.get_sp_el0() == user_sp, "SP_EL0 must remain completely intact across exception entry");
    CHECK(cpu.get_program_counter() == vbar + 0x400ULL, "PC jumped to Lower EL Sync vector");

    // In EL1 handler: write ERET (0xD69F03E0u) at handler PC
    const std::uint64_t handler_pc = vbar + 0x400ULL;
    mem.write_u32(static_cast<std::size_t>(handler_pc), 0xD69F03E0u); // ERET

    // Step: EL1 executes ERET
    cpu.step(mem);

    CHECK(cpu.get_current_el() == 0, "Must return to EL0");
    CHECK(cpu.get_sp_sel() == false, "Must restore SPSel for EL0");
    CHECK(cpu.get_stack_pointer() == user_sp, "Stack pointer after ERET must be user_sp");
    CHECK(cpu.get_program_counter() == user_code_va + 4u, "PC restored to instruction after SVC");
    CHECK(cpu.read_register(0) == 0x1111, "Register state preserved");
    CHECK(mem.read_u64(0x20000ULL) == kUserStackCanary, "Memory preserved");

    std::cout << "[PASS] test_el0_el1_stack_switching_and_preservation\n";
    return true;
}

static bool test_spsel_selection_at_el1() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x400000ULL);

    const std::uint64_t sp_el0_val = 0x10000ULL;
    const std::uint64_t sp_el1_val = 0x20000ULL;

    cpu.set_current_el(1);
    cpu.set_sp_el0(sp_el0_val);
    cpu.set_sp_el1(sp_el1_val);

    // Initial state: sp_sel_ == true (EL1h)
    cpu.set_sp_sel(true);
    CHECK(cpu.get_stack_pointer() == sp_el1_val, "EL1 with SPSel=1 must use SP_EL1");

    // Guest EL1 executes MSR SPSel, #0 (0xD5144200u) at 0x1000
    // MSR SPSel is encoded as MSR SPSel, Xt:
    // Bits [31:22] = 1101 0101 0001 (0xD51)
    // op0=3, op1=0, crn=4, crm=2, op2=0 -> 0xD5184200 | Rt
    // Or direct C++ setter test:
    cpu.set_sp_sel(false);
    CHECK(cpu.get_stack_pointer() == sp_el0_val, "EL1 with SPSel=0 (EL1t) must use SP_EL0");

    cpu.set_sp_sel(true);
    CHECK(cpu.get_stack_pointer() == sp_el1_val, "EL1 with SPSel=1 (EL1h) must restore SP_EL1");

    std::cout << "[PASS] test_spsel_selection_at_el1\n";
    return true;
}

static bool test_sp_alignment_fault_at_el0_and_el1() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x400000ULL);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    MMU& mmu = cpu.get_mmu();
    // Enable SCTLR_EL1.SA0 (bit 4) and SCTLR_EL1.SA (bit 3)
    mmu.set_sctlr_el1(0x00C50838ULL | (1ULL << 4u) | (1ULL << 3u));
    CHECK(mmu.is_sp_alignment_check_enabled(0), "SA0 must be enabled");
    CHECK(mmu.is_sp_alignment_check_enabled(1), "SA must be enabled");

    // 1. EL0 Unaligned SP: SP_EL0 = 0x20005 (not 16-byte aligned)
    // Instruction: LDR X0, [SP] (0xF94003E0u)
    const std::uint64_t code_va = 0x1000ULL;
    mem.write_u32(static_cast<std::size_t>(code_va), 0xF94003E0u);

    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_sp_el0(0x20005ULL);
    cpu.set_program_counter(code_va);

    cpu.step(mem);

    CHECK(cpu.get_current_el() == 1, "Unaligned EL0 SP access must trap to EL1");
    CHECK(cpu.get_program_counter() == vbar + 0x400ULL, "Must jump to Lower EL Sync vector");
    CHECK(cpu.get_elr_el1() == code_va, "ELR_EL1 must point to faulting LDR [SP]");
    auto esr = cpu.get_esr_el1();
    auto ec = static_cast<std::uint32_t>((esr >> 26u) & 0x3Fu);
    CHECK(ec == 0x26u, "EC must be 0x26 (SPAlignmentFault)");

    // 2. EL1 Unaligned SP: SP_EL1 = 0x30008 (not 16-byte aligned)
    // Instruction: LDR X0, [SP] (0xF94003E0u) at 0x2000
    const std::uint64_t kernel_code_va = 0x2000ULL;
    mem.write_u32(static_cast<std::size_t>(kernel_code_va), 0xF94003E0u);

    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_sp_el1(0x30008ULL);
    cpu.set_program_counter(kernel_code_va);

    cpu.step(mem);

    CHECK(cpu.get_current_el() == 1, "Unaligned EL1 SP access must remain at EL1");
    CHECK(cpu.get_program_counter() == vbar + 0x200ULL, "Must jump to Current EL SPx Sync vector");
    CHECK(cpu.get_elr_el1() == kernel_code_va, "ELR_EL1 must point to faulting LDR [SP]");
    esr = cpu.get_esr_el1();
    ec = static_cast<std::uint32_t>((esr >> 26u) & 0x3Fu);
    CHECK(ec == 0x26u, "EC must be 0x26 (SPAlignmentFault)");

    std::cout << "[PASS] test_sp_alignment_fault_at_el0_and_el1\n";
    return true;
}

static bool test_nested_exception_context_preservation() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x400000ULL);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    // Initial state: EL0 client at 0x1000
    const std::uint64_t user_pc = 0x1000ULL;
    mem.write_u32(static_cast<std::size_t>(user_pc), 0xD4000001u); // SVC #0

    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_sp_el0(0x70000ULL);
    cpu.set_sp_el1(0x90000ULL);
    cpu.set_program_counter(user_pc);
    cpu.write_register(0, 0xFEED);

    // Step 1: User SVC #0 -> traps to EL1 vector +0x400
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1, "Entered EL1");
    CHECK(cpu.get_program_counter() == vbar + 0x400ULL, "Lower EL Sync vector");

    // Outer handler saves ELR_EL1 and SPSR_EL1
    const std::uint64_t saved_user_elr = cpu.get_elr_el1();
    const std::uint32_t saved_user_spsr = cpu.get_spsr_el1();
    CHECK(saved_user_elr == user_pc + 4u, "Outer ELR must be user return PC");

    // Step 2: In EL1 handler, an undefined instruction is executed (simulating nested exception)
    const std::uint64_t handler_fault_pc = vbar + 0x400ULL;
    mem.write_u32(static_cast<std::size_t>(handler_fault_pc), 0x00000000u); // Undefined encoding

    cpu.step(mem);

    CHECK(cpu.get_current_el() == 1, "Must remain at EL1 after nested exception");
    CHECK(cpu.get_program_counter() == vbar + 0x200ULL, "Jumped to Current EL SPx Sync vector");
    CHECK(cpu.get_elr_el1() == handler_fault_pc, "Inner ELR must point to nested faulting PC");

    // Step 3: Nested handler resolves and returns via ERET to outer handler
    mem.write_u32(static_cast<std::size_t>(vbar + 0x200ULL), 0xD69F03E0u); // ERET
    // Point inner return to instruction after fault: handler_fault_pc + 4
    cpu.set_elr_el1(handler_fault_pc + 4u);

    cpu.step(mem);

    CHECK(cpu.get_program_counter() == handler_fault_pc + 4u, "Resumed outer handler");

    // Step 4: Outer handler restores saved user ELR and SPSR, and executes ERET to return to EL0
    cpu.set_elr_el1(saved_user_elr);
    cpu.set_spsr_el1(saved_user_spsr);
    mem.write_u32(static_cast<std::size_t>(handler_fault_pc + 4u), 0xD69F03E0u); // ERET

    cpu.step(mem);

    CHECK(cpu.get_current_el() == 0, "Returned cleanly to EL0");
    CHECK(cpu.get_program_counter() == user_pc + 4u, "User PC resumed");
    CHECK(cpu.get_stack_pointer() == 0x70000ULL, "User SP_EL0 restored");
    CHECK(cpu.read_register(0) == 0xFEED, "User register X0 intact");

    std::cout << "[PASS] test_nested_exception_context_preservation\n";
    return true;
}

int main() {
    try {
        std::cout << "=== Running Stage 193.11: Stack Management and Context Preservation Tests ===\n";
        if (!test_el0_el1_stack_switching_and_preservation()) return 1;
        if (!test_spsel_selection_at_el1()) return 1;
        if (!test_sp_alignment_fault_at_el0_and_el1()) return 1;
        if (!test_nested_exception_context_preservation()) return 1;
        std::cout << "=== All Stage 193.11 Tests Passed (4/4) ===\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION CAUGHT: " << ex.what() << "\n";
        return 1;
    }
}

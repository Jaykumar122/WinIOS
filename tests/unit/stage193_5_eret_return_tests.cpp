// ============================================================================
// Stage 193.5: ERET and Exception Return Tests
// ============================================================================

#include <cstdint>
#include <iostream>

#include "cpu/cpu.h"
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

static bool test_complete_roundtrip_el0_to_el1_and_eret_to_el0() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x200000ULL);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    // EL0 user code at 0x4000:
    // 0x4000: MOV X0, #42        (0xD2800540u)
    // 0x4004: SVC #0             (0xD4000001u)
    // 0x4008: ADD X0, X0, #1     (0x91000400u)
    // 0x400C: NOP                (0xD503201Fu)
    const std::uint64_t user_code = 0x4000ULL;
    mem.write_u32(static_cast<std::size_t>(user_code + 0), 0xD2800540u);
    mem.write_u32(static_cast<std::size_t>(user_code + 4), 0xD4000001u);
    mem.write_u32(static_cast<std::size_t>(user_code + 8), 0x91000400u);
    mem.write_u32(static_cast<std::size_t>(user_code + 12), 0xD503201Fu);

    // Vector handler at vbar + 0x400 (Lower EL AArch64 Sync):
    // 0x80400: ADD X0, X0, #100  (0x91019000u)
    // 0x80404: ERET              (0xD69F03E0u)
    const std::uint64_t handler_addr = vbar + 0x400ULL;
    mem.write_u32(static_cast<std::size_t>(handler_addr + 0), 0x91019000u);
    mem.write_u32(static_cast<std::size_t>(handler_addr + 4), 0xD69F03E0u);

    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_program_counter(user_code);
    cpu.write_register(0, 0);

    // Step 1: Execute MOV X0, #42 at EL0
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 0, "Must be at EL0");
    CHECK(cpu.get_program_counter() == user_code + 4, "PC must advance to SVC");
    CHECK(cpu.read_register(0) == 42, "X0 must be 42");

    // Step 2: Execute SVC #0 at EL0 -> exception taken to EL1
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1, "Must enter EL1");
    CHECK(cpu.get_sp_sel() == true, "Must select SP_EL1");
    CHECK(cpu.get_program_counter() == handler_addr, "PC must be at vector handler");
    CHECK(cpu.get_elr_el1() == user_code + 8, "ELR_EL1 must point to instruction after SVC");
    CHECK(cpu.read_register(0) == 42, "X0 unmodified by exception entry");

    // Step 3: Execute ADD X0, X0, #100 in EL1 handler
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1, "Must remain at EL1");
    CHECK(cpu.get_program_counter() == handler_addr + 4, "PC must advance to ERET");
    CHECK(cpu.read_register(0) == 142, "X0 must be 142");

    // Step 4: Execute ERET in EL1 handler -> returns to EL0
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 0, "Must return to EL0");
    CHECK(cpu.get_sp_sel() == false, "Must use SP_EL0 at EL0");
    CHECK(cpu.get_program_counter() == user_code + 8, "PC must restore to instruction after SVC (0x4008)");
    CHECK(cpu.read_register(0) == 142, "X0 value preserved across return");

    // Step 5: Execute ADD X0, X0, #1 at EL0 (resumed guest code)
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 0, "Must remain at EL0");
    CHECK(cpu.get_program_counter() == user_code + 12, "PC must advance to NOP");
    CHECK(cpu.read_register(0) == 143, "X0 must be incremented to 143");

    // Step 6: Execute NOP at EL0
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 0, "Must remain at EL0");
    CHECK(cpu.get_program_counter() == user_code + 16, "PC must advance past NOP");

    std::cout << "[PASS] test_complete_roundtrip_el0_to_el1_and_eret_to_el0\n";
    return true;
}

static bool test_pstate_and_flags_preservation() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x200000ULL);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    const std::uint64_t user_code = 0x4000ULL;
    // 0x4000: SVC #0 (0xD4000001u)
    mem.write_u32(static_cast<std::size_t>(user_code), 0xD4000001u);
    // 0x4004: NOP    (0xD503201Fu)
    mem.write_u32(static_cast<std::size_t>(user_code + 4), 0xD503201Fu);

    // Vector handler at vbar + 0x400:
    // 0x80400: ERET (0xD69F03E0u)
    const std::uint64_t handler_addr = vbar + 0x400ULL;
    mem.write_u32(static_cast<std::size_t>(handler_addr), 0xD69F03E0u);

    cpu.set_current_el(0);
    cpu.set_program_counter(user_code);
    // Set specific NZCV flags at EL0: N=1, Z=0, C=1, V=0 -> 0b1010 = 0xA
    cpu.set_nzcv(0xAu);
    cpu.set_daif(0x0u); // Unmasked at EL0

    // Step 1: Execute SVC #0
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1, "Must enter EL1");
    // Verify SPSR_EL1 captured pre-exception NZCV and DAIF
    const std::uint32_t spsr = cpu.get_spsr_el1();
    CHECK(((spsr >> 28u) & 0xFu) == 0xAu, "SPSR_EL1 NZCV must record 0xA");
    CHECK(((spsr >> 6u) & 0xFu) == 0x0u, "SPSR_EL1 DAIF must record 0x0");
    // Exception entry must mask DAIF
    CHECK(cpu.get_daif() == 0xFu, "DAIF must be masked upon exception entry");

    // EL1 handler modifies current NZCV and DAIF before returning
    cpu.set_nzcv(0x5u); // N=0, Z=1, C=0, V=1
    cpu.set_daif(0x2u);

    // Step 2: Execute ERET
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 0, "Must return to EL0");
    // PSTATE NZCV and DAIF must be restored from SPSR_EL1
    CHECK(cpu.get_nzcv() == 0xAu, "NZCV must be restored to 0xA from SPSR_EL1");
    CHECK(cpu.get_daif() == 0x0u, "DAIF must be restored to 0x0 from SPSR_EL1");

    std::cout << "[PASS] test_pstate_and_flags_preservation\n";
    return true;
}

static bool test_stack_pointer_isolation_and_restoration() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x200000ULL);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    const std::uint64_t user_code = 0x4000ULL;
    mem.write_u32(static_cast<std::size_t>(user_code), 0xD4000001u); // SVC #0
    mem.write_u32(static_cast<std::size_t>(user_code + 4), 0xD503201Fu); // NOP

    const std::uint64_t handler_addr = vbar + 0x400ULL;
    mem.write_u32(static_cast<std::size_t>(handler_addr), 0xD69F03E0u); // ERET

    // Configure distinct stack pointers
    const std::uint64_t user_sp = 0x00007FFFF000ULL;
    const std::uint64_t kernel_sp = 0xFFFF80000000ULL;
    cpu.set_sp_el0(user_sp);
    cpu.set_sp_el1(kernel_sp);

    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_program_counter(user_code);

    CHECK(cpu.get_stack_pointer() == user_sp, "EL0 must use SP_EL0");

    // Step 1: Execute SVC #0 -> enters EL1
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1, "Must enter EL1");
    CHECK(cpu.get_sp_sel() == true, "Must select SP_EL1");
    CHECK(cpu.get_stack_pointer() == kernel_sp, "EL1 must use SP_EL1");

    // Modify SP_EL1 in handler
    cpu.set_stack_pointer(kernel_sp - 0x40ULL);
    CHECK(cpu.get_stack_pointer() == kernel_sp - 0x40ULL, "SP_EL1 modified in EL1");
    CHECK(cpu.get_sp_el0() == user_sp, "SP_EL0 preserved untouched");

    // Step 2: Execute ERET -> returns to EL0
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 0, "Must return to EL0");
    CHECK(cpu.get_stack_pointer() == user_sp, "SP restored to SP_EL0 at EL0");
    CHECK(cpu.get_sp_el1() == kernel_sp - 0x40ULL, "SP_EL1 preserved for future exceptions");

    std::cout << "[PASS] test_stack_pointer_isolation_and_restoration\n";
    return true;
}

static bool test_el0_eret_privilege_rejection() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x200000ULL);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    // EL0 user code at 0x4000 attempts to execute ERET (0xD69F03E0u)
    const std::uint64_t user_code = 0x4000ULL;
    mem.write_u32(static_cast<std::size_t>(user_code), 0xD69F03E0u);

    cpu.set_current_el(0);
    cpu.set_program_counter(user_code);

    // Step: ERET at EL0 must trap
    cpu.step(mem);

    CHECK(cpu.get_current_el() == 1, "Trapped to EL1");
    CHECK(cpu.get_program_counter() == vbar + 0x400ULL, "PC must jump to Lower EL Sync vector");
    CHECK(cpu.get_elr_el1() == user_code, "ELR_EL1 must point to faulting ERET");
    const auto ec = static_cast<std::uint32_t>((cpu.get_esr_el1() >> 26u) & 0x3Fu);
    CHECK(ec == 0x00u, "ESR_EL1 EC must be UndefinedInstruction (0x00)");

    std::cout << "[PASS] test_el0_eret_privilege_rejection\n";
    return true;
}

static bool test_illegal_exception_return_states() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x200000ULL);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    // 1. EL1 ERET with SPSR targeting EL2 (target_el > current_el)
    // Code at 0x10000: ERET (0xD69F03E0u)
    const std::uint64_t code_addr = 0x10000ULL;
    mem.write_u32(static_cast<std::size_t>(code_addr), 0xD69F03E0u);

    // Target PC where instruction fetch will fail due to PSTATE.IL
    const std::uint64_t return_pc = 0x20000ULL;
    mem.write_u32(static_cast<std::size_t>(return_pc), 0xD503201Fu); // NOP

    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(code_addr);
    cpu.set_elr_el1(return_pc);
    // SPSR_EL1 targeting EL2h (M = 0x09)
    cpu.set_spsr_el1(0x09u);

    // Step 1: Execute ERET -> invalid return state sets PSTATE.IL = 1
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == return_pc, "PC updated to return address");

    // Step 2: Next instruction fetch at return_pc with PSTATE.IL=1 causes IllegalState exception (EC 0x0E)
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1, "Exception taken to EL1");
    CHECK(cpu.get_program_counter() == vbar + 0x200ULL, "PC must jump to Current EL SPx Sync vector");
    const auto ec = static_cast<std::uint32_t>((cpu.get_esr_el1() >> 26u) & 0x3Fu);
    CHECK(ec == 0x0Eu, "ESR_EL1 EC must be IllegalExecutionState (0x0E)");

    // 2. EL1 ERET with SPSR containing invalid reserved M field (e.g. M = 0x02)
    mem.write_u32(static_cast<std::size_t>(code_addr), 0xD69F03E0u);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(code_addr);
    cpu.set_elr_el1(return_pc);
    cpu.set_spsr_el1(0x02u); // Reserved mode

    // Step 1: Execute ERET
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == return_pc, "PC updated to return address");

    // Step 2: Next instruction fetch triggers IllegalExecutionState
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1, "Exception taken to EL1");
    CHECK(cpu.get_program_counter() == vbar + 0x200ULL, "PC must jump to Current EL SPx Sync vector");
    const auto ec2 = static_cast<std::uint32_t>((cpu.get_esr_el1() >> 26u) & 0x3Fu);
    CHECK(ec2 == 0x0Eu, "ESR_EL1 EC must be IllegalExecutionState (0x0E)");

    std::cout << "[PASS] test_illegal_exception_return_states\n";
    return true;
}

int main() {
    try {
        std::cout << "=== Running Stage 193.5: ERET and Exception Return Tests ===\n";
        if (!test_complete_roundtrip_el0_to_el1_and_eret_to_el0()) return 1;
        if (!test_pstate_and_flags_preservation()) return 1;
        if (!test_stack_pointer_isolation_and_restoration()) return 1;
        if (!test_el0_eret_privilege_rejection()) return 1;
        if (!test_illegal_exception_return_states()) return 1;
        std::cout << "=== All Stage 193.5 Tests Passed (5/5) ===\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION CAUGHT: " << ex.what() << "\n";
        return 1;
    }
}

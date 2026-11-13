// =============================================================================
// stage193_3_syndrome_register_tests.cpp
//
// Stage 193.3: Exception Syndrome Register (ESR_EL1) & Fault Architecture Tests
// Verifies:
//   - Architectural ESR_EL1 bit layout (EC [31:26], IL [25], ISS [24:0]).
//   - Data Abort syndromes: ISV (bit 24), SAS (bits [23:22]), SRT (bits [20:16]),
//     WnR (bit 6), and DFSC (bits [5:0]).
//   - Instruction Abort syndromes: IFSC (bits [5:0]) across translation levels.
//   - Guest instruction access to ESR_EL1, FAR_EL1, ELR_EL1, SPSR_EL1 at EL1:
//       * MRS X0, ESR_EL1 (0xD5385200u)
//       * MSR ESR_EL1, X1 (0xD5185201u)
//       * MRS X2, FAR_EL1 (0xD5386002u)
//       * MSR FAR_EL1, X3 (0xD5186003u)
//       * MRS X4, ELR_EL1 (0xD5384024u)
//       * MSR ELR_EL1, X5 (0xD5184025u)
//       * MRS X6, SPSR_EL1 (0xD5384006u)
//       * MSR SPSR_EL1, X7 (0xD5184007u)
//   - Rejection of guest access to these registers when executed at EL0.
// =============================================================================

#include <cstdint>
#include <iostream>

#include "cpu/cpu.h"
#include "cpu/synchronous_exception.h"
#include "memory/memory.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Test 1: Data Abort syndrome bitfield construction
bool test_data_abort_syndrome_fields() {
    // Write access, 8 bytes (SAS=3), SRT=5, ISV=true, PermissionLevel2 fault
    const auto ex = make_data_abort(
        false, AccessType::Write, FaultStatus::PermissionLevel2,
        0x10002000ULL, 0x8000ULL, 8u, true, 5u);

    const auto esr = ex.esr();
    const auto ec = (esr >> 26u) & 0x3Fu;
    const auto il = (esr >> 25u) & 1u;
    const auto isv = (esr >> 24u) & 1u;
    const auto sas = (esr >> 22u) & 0x3u;
    const auto srt = (esr >> 16u) & 0x1Fu;
    const auto wnr = (esr >> 6u) & 1u;
    const auto dfsc = esr & 0x3Fu;

    CHECK(ec == static_cast<std::uint32_t>(ExceptionClass::DataAbortLower), "EC must be DataAbortLower (0x24)");
    CHECK(il == 1u, "IL must be 1 (32-bit instruction)");
    CHECK(isv == 1u, "ISV must be 1 (syndrome valid)");
    CHECK(sas == 3u, "SAS must be 3 (8 bytes / 64-bit access)");
    CHECK(srt == 5u, "SRT must be 5 (register X5)");
    CHECK(wnr == 1u, "WnR must be 1 (Write)");
    CHECK(dfsc == static_cast<std::uint32_t>(FaultStatus::PermissionLevel2), "DFSC must be PermissionLevel2 (0x0E)");

    std::cout << "[PASS] test_data_abort_syndrome_fields\n";
    return true;
}

// Test 2: Instruction Abort syndrome bitfields
bool test_instruction_abort_syndrome_fields() {
    const auto ex = make_instruction_abort(
        false, FaultStatus::TranslationLevel1, 0x4000ULL, 4u);

    const auto esr = ex.esr();
    const auto ec = (esr >> 26u) & 0x3Fu;
    const auto il = (esr >> 25u) & 1u;
    const auto ifsc = esr & 0x3Fu;

    CHECK(ec == static_cast<std::uint32_t>(ExceptionClass::InstructionAbortLower), "EC must be InstructionAbortLower (0x20)");
    CHECK(il == 1u, "IL must be 1");
    CHECK(ifsc == static_cast<std::uint32_t>(FaultStatus::TranslationLevel1), "IFSC must be TranslationLevel1 (0x05)");
    CHECK(ex.has_fault_address == true, "Must have fault address");
    CHECK(ex.fault_address == 0x4000ULL, "Fault address must be fetch address");

    std::cout << "[PASS] test_instruction_abort_syndrome_fields\n";
    return true;
}

// Test 3: Guest EL1 instruction access to ESR_EL1, FAR_EL1, ELR_EL1, SPSR_EL1
bool test_guest_sysreg_access_el1() {
    CPU cpu;
    Memory mem(0x100000ULL);
    cpu.set_architectural_exceptions(true);

    constexpr std::uint64_t kCode = 0x1000ULL;
    // 1. MRS X0, ESR_EL1   (0xD5385200u)
    mem.write_u32(static_cast<std::size_t>(kCode), 0xD5385200u);
    // 2. MSR ESR_EL1, X1   (0xD5185201u)
    mem.write_u32(static_cast<std::size_t>(kCode + 4u), 0xD5185201u);
    // 3. MRS X2, FAR_EL1   (0xD5386002u)
    mem.write_u32(static_cast<std::size_t>(kCode + 8u), 0xD5386002u);
    // 4. MSR FAR_EL1, X3   (0xD5186003u)
    mem.write_u32(static_cast<std::size_t>(kCode + 12u), 0xD5186003u);
    // 5. MRS X4, ELR_EL1   (0xD5384024u)
    mem.write_u32(static_cast<std::size_t>(kCode + 16u), 0xD5384024u);
    // 6. MSR ELR_EL1, X5   (0xD5184025u)
    mem.write_u32(static_cast<std::size_t>(kCode + 20u), 0xD5184025u);
    // 7. MRS X6, SPSR_EL1  (0xD5384006u)
    mem.write_u32(static_cast<std::size_t>(kCode + 24u), 0xD5384006u);
    // 8. MSR SPSR_EL1, X7  (0xD5184007u)
    mem.write_u32(static_cast<std::size_t>(kCode + 28u), 0xD5184007u);

    cpu.set_current_el(1);
    cpu.set_program_counter(kCode);

    // Initial state
    cpu.set_esr_el1(0x92000044u);
    cpu.set_far_el1(0xCAFEF00D1234ULL);
    cpu.set_elr_el1(0x7FFF0000ULL);
    cpu.set_spsr_el1(0x60000000u);

    // New values to write via MSR
    cpu.write_register(1, 0x86000000ULL); // New ESR
    cpu.write_register(3, 0xBEEFCAFE5678ULL); // New FAR
    cpu.write_register(5, 0x80004000ULL); // New ELR
    cpu.write_register(7, 0x80000005ULL); // New SPSR

    // Execute 1: MRS X0, ESR_EL1
    cpu.step(mem);
    CHECK(cpu.read_register(0) == 0x92000044ULL, "X0 must equal initial ESR_EL1");

    // Execute 2: MSR ESR_EL1, X1
    cpu.step(mem);
    CHECK(cpu.get_esr_el1() == 0x86000000u, "ESR_EL1 must be updated by MSR");

    // Execute 3: MRS X2, FAR_EL1
    cpu.step(mem);
    CHECK(cpu.read_register(2) == 0xCAFEF00D1234ULL, "X2 must equal initial FAR_EL1");

    // Execute 4: MSR FAR_EL1, X3
    cpu.step(mem);
    CHECK(cpu.get_far_el1() == 0xBEEFCAFE5678ULL, "FAR_EL1 must be updated by MSR");

    // Execute 5: MRS X4, ELR_EL1
    cpu.step(mem);
    CHECK(cpu.read_register(4) == 0x7FFF0000ULL, "X4 must equal initial ELR_EL1");

    // Execute 6: MSR ELR_EL1, X5
    cpu.step(mem);
    CHECK(cpu.get_elr_el1() == 0x80004000ULL, "ELR_EL1 must be updated by MSR");

    // Execute 7: MRS X6, SPSR_EL1
    cpu.step(mem);
    CHECK(cpu.read_register(6) == 0x60000000ULL, "X6 must equal initial SPSR_EL1");

    // Execute 8: MSR SPSR_EL1, X7
    cpu.step(mem);
    CHECK(cpu.get_spsr_el1() == 0x80000005u, "SPSR_EL1 must be updated by MSR");

    std::cout << "[PASS] test_guest_sysreg_access_el1\n";
    return true;
}

// Test 4: Rejection of ESR_EL1, FAR_EL1, ELR_EL1, SPSR_EL1 at EL0
bool test_rejection_at_el0() {
    CPU cpu;
    Memory mem(0x100000ULL);
    cpu.set_architectural_exceptions(true);

    constexpr std::uint64_t kVbar = 0x30000ULL;
    cpu.set_vbar_el1(kVbar);

    const std::vector<std::uint32_t> el1_sysreg_insts = {
        0xD5385200u, // MRS X0, ESR_EL1
        0xD5185200u, // MSR ESR_EL1, X0
        0xD5386000u, // MRS X0, FAR_EL1
        0xD5186000u, // MSR FAR_EL1, X0
        0xD5384020u, // MRS X0, ELR_EL1
        0xD5184020u, // MSR ELR_EL1, X0
        0xD5384000u, // MRS X0, SPSR_EL1
        0xD5184000u  // MSR SPSR_EL1, X0
    };

    std::uint64_t pc = 0x5000ULL;
    for (const auto inst : el1_sysreg_insts) {
        mem.write_u32(static_cast<std::size_t>(pc), inst);

        cpu.set_current_el(0);
        cpu.set_program_counter(pc);

        cpu.step(mem);

        CHECK(cpu.get_current_el() == 1, "Must trap to EL1");
        CHECK(cpu.get_program_counter() == kVbar + 0x400u, "PC must jump to Lower EL Sync vector");
        CHECK(cpu.get_elr_el1() == pc, "ELR_EL1 must hold faulting instruction PC");
        const auto ec = (cpu.get_esr_el1() >> 26u) & 0x3Fu;
        CHECK(ec == 0x00u, "ESR_EL1.EC must be 0x00 (Undefined Instruction)");

        pc += 0x100ULL;
    }

    std::cout << "[PASS] test_rejection_at_el0\n";
    return true;
}

} // namespace

int main() {
    std::cout << "=== Running Stage 193.3: Exception Syndrome Register Tests ===\n";

    if (!test_data_abort_syndrome_fields()) return 1;
    if (!test_instruction_abort_syndrome_fields()) return 1;
    if (!test_guest_sysreg_access_el1()) return 1;
    if (!test_rejection_at_el0()) return 1;

    std::cout << "=== All Stage 193.3 Tests Passed (4/4) ===\n";
    return 0;
}

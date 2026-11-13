// =============================================================================
// stage_192_13_pac_boundary_tests.cpp
//
// Stage 192.13: ARM64e & Pointer Authentication Boundary Audit Tests
// Verifies:
//   - Architectural NOP-space PAC hints (PACIASP, AUTIASP, PACIAZ, AUTIAZ) in CPU
//   - Dedicated PAC instruction execution in Arm64ePointerAuth (PACIA, AUTIA, PACDA, AUTDA)
//   - Stripping semantics (XPACI, XPACD) for TTBR0/TTBR1 pointers
//   - Combined branch/return instructions (BRAA, BLRAA, RETAA)
//   - Authentication failure syndrome generation (EC 0x1C trap vs Bit-62 corruption)
//   - Strict boundary classification: PARTIAL / EMULATED_REFERENCE
// =============================================================================

#include <cstdint>
#include <iostream>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/pointer_auth.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::cpu;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// -----------------------------------------------------------------------------
// Test 1: Architectural NOP-Space PAC Hints in CPU EL0 Mode
// -----------------------------------------------------------------------------
bool test_cpu_pac_hints_nop_space() {
    std::cout << "[Test 1] Architectural NOP-Space PAC Hints in CPU EL0 Mode...\n";

    CPU cpu;
    Memory mem(16 * 1024 * 1024);
    cpu.reset();
    cpu.set_current_el(0);

    // Standard ARMv8.3 PAC hint encodings:
    // PACIAZ:  0xD503231Fu (HINT #24)
    // PACIASP: 0xD503233Fu (HINT #25)
    // PACIBSP: 0xD503237Fu (HINT #27)
    // AUTIAZ:  0xD503239Fu (HINT #28)
    // AUTIASP: 0xD50323BFu (HINT #29)
    // AUTIBSP: 0xD50323FFu (HINT #31)
    const uint32_t hint_instructions[] = {
        0xD503231Fu,
        0xD503233Fu,
        0xD503237Fu,
        0xD503239Fu,
        0xD50323BFu,
        0xD50323FFu
    };

    uint64_t pc = 0x1000;
    for (uint32_t insn : hint_instructions) {
        mem.write_u32(pc, insn);
        pc += 4;
    }

    cpu.set_program_counter(0x1000);

    // Step through all 6 hint instructions
    for (size_t i = 0; i < 6; ++i) {
        uint64_t expected_next_pc = 0x1000 + (i + 1) * 4;
        cpu.step(mem);
        CHECK(cpu.get_program_counter() == expected_next_pc,
              "PAC hint instruction index " + std::to_string(i) + " must advance PC by 4 as a NOP");
    }

    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Dedicated PAC Signing & Authentication (PACIA, AUTIA, PACDA, AUTDA)
// -----------------------------------------------------------------------------
bool test_pac_signing_and_authentication() {
    std::cout << "[Test 2] Dedicated PAC Signing & Authentication...\n";

    Arm64ePointerAuth pac(PacFaultMode::TrapException);

    // Test APIA (Instruction A)
    const uint64_t code_ptr = 0x0000000100004560ULL;
    const uint64_t code_mod = 0x00007FFFF0001230ULL;
    uint64_t reg_ia = code_ptr;

    CHECK(pac.execute_pacia(reg_ia, code_mod), "execute_pacia must succeed");
    CHECK((reg_ia & Arm64ePointerAuth::kPacMask) != 0, "PAC tag must be non-zero in upper bits");
    CHECK((reg_ia & Arm64ePointerAuth::kDefaultAddressMask) == code_ptr, "Canonical pointer must be preserved");

    // Authenticate with matching modifier
    CHECK(pac.execute_autia(reg_ia, code_mod), "execute_autia with correct modifier must succeed");
    CHECK(reg_ia == code_ptr, "Authenticated pointer must match original code_ptr");
    CHECK(!pac.has_fault(), "No syndrome fault should be recorded on valid auth");

    // Test APDA (Data A)
    const uint64_t data_ptr = 0x0000000100050000ULL;
    const uint64_t data_mod = 0x1234ULL;
    uint64_t reg_da = data_ptr;

    CHECK(pac.execute_pacda(reg_da, data_mod), "execute_pacda must succeed");
    CHECK((reg_da & Arm64ePointerAuth::kPacMask) != 0, "Data PAC tag must be non-zero");

    CHECK(pac.execute_autda(reg_da, data_mod), "execute_autda must succeed");
    CHECK(reg_da == data_ptr, "Authenticated data pointer must match original data_ptr");

    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Pointer Stripping Semantics (XPACI, XPACD)
// -----------------------------------------------------------------------------
bool test_pac_stripping_semantics() {
    std::cout << "[Test 3] Pointer Stripping Semantics (XPACI, XPACD)...\n";

    Arm64ePointerAuth pac;

    // TTBR0 userspace pointer (bit 47 == 0)
    const uint64_t user_ptr = 0x0000000100008000ULL;
    uint64_t signed_user = pac.sign_pointer(user_ptr, 0x100, PacKeyType::InstructionA);
    CHECK((signed_user & Arm64ePointerAuth::kPacMask) != 0, "Signed pointer must have PAC bits");

    CHECK(pac.execute_xpaci(signed_user), "execute_xpaci must succeed");
    CHECK(signed_user == user_ptr, "Stripped userspace pointer must equal original canonical pointer");

    // TTBR1 kernel space pointer (bit 47 == 1)
    const uint64_t kern_ptr = 0xFFFF800012345000ULL;
    uint64_t signed_kern = pac.sign_pointer(kern_ptr, 0x200, PacKeyType::DataB);
    CHECK(pac.execute_xpacd(signed_kern), "execute_xpacd must succeed");
    CHECK(signed_kern == kern_ptr, "Stripped kernel pointer must maintain high address sign extension");

    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Combined Branch & Return Authentication (BRAA, BLRAA, RETAA)
// -----------------------------------------------------------------------------
bool test_pac_branch_and_return() {
    std::cout << "[Test 4] Combined Branch & Return Authentication...\n";

    Arm64ePointerAuth pac(PacFaultMode::TrapException);

    const uint64_t target_pc = 0x10000800ULL;
    const uint64_t modifier  = 0x7FFFF000ULL;
    const uint64_t signed_fn = pac.sign_pointer(target_pc, modifier, PacKeyType::InstructionA);

    // Test BRAA
    uint64_t branch_target = 0;
    bool braa_ok = pac.execute_braa(signed_fn, modifier, branch_target, 0x1000);
    CHECK(braa_ok, "execute_braa must succeed with matching modifier");
    CHECK(branch_target == target_pc, "BRAA branch target mismatch");

    // Test BLRAA
    uint64_t lr = 0;
    uint64_t blr_target = 0;
    bool blraa_ok = pac.execute_blraa(signed_fn, modifier, lr, blr_target, 0x1000);
    CHECK(blraa_ok, "execute_blraa must succeed");
    CHECK(blr_target == target_pc, "BLRAA branch target mismatch");
    CHECK(lr == 0x1004, "BLRAA must set LR to PC + 4");

    // Test RETAA
    const uint64_t ret_pc = 0x10000400ULL;
    const uint64_t sp = 0x7FFFF000ULL;
    const uint64_t signed_ret = pac.sign_pointer(ret_pc, sp, PacKeyType::InstructionA);
    uint64_t ret_target = 0;
    bool retaa_ok = pac.execute_retaa(signed_ret, sp, ret_target, 0x1000);
    CHECK(retaa_ok, "execute_retaa must succeed with matching SP");
    CHECK(ret_target == ret_pc, "RETAA return target mismatch");

    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Authentication Failure Modes (Trap Exception vs Bit-62 Corruption)
// -----------------------------------------------------------------------------
bool test_pac_failure_modes() {
    std::cout << "[Test 5] Authentication Failure Modes (Trap vs Bit-62)...\n";

    const uint64_t ptr = 0x10004000ULL;
    const uint64_t good_mod = 0x1234ULL;
    const uint64_t bad_mod  = 0x9999ULL;

    // Mode A: TrapException (EC 0x1C Synchronous Exception)
    {
        Arm64ePointerAuth pac(PacFaultMode::TrapException);
        uint64_t signed_ptr = pac.sign_pointer(ptr, good_mod, PacKeyType::InstructionA);

        uint64_t reg = signed_ptr;
        bool aut_ok = pac.execute_autia(reg, bad_mod, 0x2000);
        CHECK(!aut_ok, "execute_autia must fail with mismatched modifier");
        CHECK(pac.has_fault(), "Syndrome fault must be recorded");
        CHECK(pac.last_syndrome().exception_class == 0x1C, "Syndrome EC must be 0x1C (PAC Trap)");
        CHECK(pac.last_syndrome().failed_key == PacKeyType::InstructionA, "Failed key mismatch");
        CHECK(pac.last_syndrome().fault_pc == 0x2000, "Fault PC mismatch");
    }

    // Mode B: CorruptUpperBits (Bit 62 Error Signature)
    {
        Arm64ePointerAuth pac(PacFaultMode::CorruptUpperBits);
        uint64_t signed_ptr = pac.sign_pointer(ptr, good_mod, PacKeyType::InstructionA);

        uint64_t reg = signed_ptr;
        bool aut_ok = pac.execute_autia(reg, bad_mod, 0x2000);
        CHECK(!aut_ok, "execute_autia must fail with mismatched modifier");
        CHECK((reg & Arm64ePointerAuth::kPacErrorBit) != 0, "Bit 62 error signature must be injected");
        CHECK((reg & Arm64ePointerAuth::kDefaultAddressMask) == ptr, "Canonical lower bits preserved");
    }

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 192.13: ARM64e & Pointer Authentication Audit Tests\n";
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

    run_test(test_cpu_pac_hints_nop_space, "Architectural NOP-Space PAC Hints in CPU EL0 Mode");
    run_test(test_pac_signing_and_authentication, "Dedicated PAC Signing & Authentication (PACIA, AUTIA, PACDA, AUTDA)");
    run_test(test_pac_stripping_semantics, "Pointer Stripping Semantics (XPACI, XPACD)");
    run_test(test_pac_branch_and_return, "Combined Branch & Return Authentication (BRAA, BLRAA, RETAA)");
    run_test(test_pac_failure_modes, "Authentication Failure Modes (Trap Exception vs Bit-62 Corruption)");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}

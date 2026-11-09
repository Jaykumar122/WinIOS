#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "cpu/pointer_auth.h"

using namespace winios::cpu;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Test 1: Key Separation across all 4 primary PAC keys (APIA, APIB, APDA, APDB)
bool test_key_separation() {
    std::cout << "[Test 1] Key Separation (APIA, APIB, APDA, APDB)...\n";

    Arm64ePointerAuth pac;
    const std::uint64_t target_ptr = 0x0000000100018000ULL;
    const std::uint64_t modifier   = 0x00007FFFFFE00100ULL;

    std::uint64_t reg_ia = target_ptr;
    std::uint64_t reg_ib = target_ptr;
    std::uint64_t reg_da = target_ptr;
    std::uint64_t reg_db = target_ptr;

    TEST_ASSERT(pac.execute_pacia(reg_ia, modifier), "execute_pacia must succeed");
    TEST_ASSERT(pac.execute_pacib(reg_ib, modifier), "execute_pacib must succeed");
    TEST_ASSERT(pac.execute_pacda(reg_da, modifier), "execute_pacda must succeed");
    TEST_ASSERT(pac.execute_pacdb(reg_db, modifier), "execute_pacdb must succeed");

    // All signed pointers must be distinct from one another and original
    TEST_ASSERT(reg_ia != target_ptr, "reg_ia must be signed");
    TEST_ASSERT(reg_ib != target_ptr, "reg_ib must be signed");
    TEST_ASSERT(reg_da != target_ptr, "reg_da must be signed");
    TEST_ASSERT(reg_db != target_ptr, "reg_db must be signed");

    TEST_ASSERT(reg_ia != reg_ib, "APIA and APIB must produce distinct PAC tags");
    TEST_ASSERT(reg_ia != reg_da, "APIA and APDA must produce distinct PAC tags");
    TEST_ASSERT(reg_ia != reg_db, "APIA and APDB must produce distinct PAC tags");
    TEST_ASSERT(reg_ib != reg_da, "APIB and APDA must produce distinct PAC tags");
    TEST_ASSERT(reg_ib != reg_db, "APIB and APDB must produce distinct PAC tags");
    TEST_ASSERT(reg_da != reg_db, "APDA and APDB must produce distinct PAC tags");

    // Each must only authenticate with its matching key
    TEST_ASSERT(pac.execute_autia(reg_ia, modifier), "AUTIA must authenticate reg_ia");
    TEST_ASSERT(reg_ia == target_ptr, "AUTIA must restore original pointer");

    TEST_ASSERT(pac.execute_autib(reg_ib, modifier), "AUTIB must authenticate reg_ib");
    TEST_ASSERT(reg_ib == target_ptr, "AUTIB must restore original pointer");

    TEST_ASSERT(pac.execute_autda(reg_da, modifier), "AUTDA must authenticate reg_da");
    TEST_ASSERT(reg_da == target_ptr, "AUTDA must restore original pointer");

    TEST_ASSERT(pac.execute_autdb(reg_db, modifier), "AUTDB must authenticate reg_db");
    TEST_ASSERT(reg_db == target_ptr, "AUTDB must restore original pointer");

    std::cout << "  -> PASS: Key Separation Verified across 4 PAC Keys\n";
    return true;
}

// Test 2: Address and Discriminator Diversity (Context Register vs Stack Pointer vs Zero)
bool test_discriminator_diversity() {
    std::cout << "[Test 2] Address & Discriminator Diversity (Context Reg vs SP vs Zero)...\n";

    Arm64ePointerAuth pac;
    const std::uint64_t ptr = 0x0000000100024000ULL;
    const std::uint64_t context_reg = 0x0000000180004000ULL;
    const std::uint64_t sp_mod      = 0x00007FFFFFE01000ULL;

    std::uint64_t reg_ctx = ptr;
    std::uint64_t reg_sp  = ptr;
    std::uint64_t reg_z   = ptr;

    TEST_ASSERT(pac.execute_pacia(reg_ctx, context_reg), "pacia with context register");
    TEST_ASSERT(pac.execute_pacia(reg_sp, sp_mod), "pacia with SP");
    TEST_ASSERT(pac.execute_paciza(reg_z), "paciza with zero modifier");

    TEST_ASSERT(reg_ctx != reg_sp, "Context reg and SP discriminators must yield different signatures");
    TEST_ASSERT(reg_ctx != reg_z, "Context reg and Zero discriminators must yield different signatures");
    TEST_ASSERT(reg_sp != reg_z, "SP and Zero discriminators must yield different signatures");

    // Authenticate zero modifier
    TEST_ASSERT(pac.execute_autiza(reg_z), "autiza must restore zero-modifier pointer");
    TEST_ASSERT(reg_z == ptr, "autiza pointer matches original");

    std::cout << "  -> PASS: Discriminator Diversity Verified\n";
    return true;
}

// Test 3: XPACI and XPACD Stripping without Authentication
bool test_xpac_stripping() {
    std::cout << "[Test 3] XPACI and XPACD Canonical Stripping...\n";

    Arm64ePointerAuth pac;
    // Lower address (EL0 userland)
    const std::uint64_t user_ptr = 0x0000000100030000ULL;
    std::uint64_t reg_i = user_ptr;
    std::uint64_t reg_d = user_ptr;

    (void)pac.execute_pacia(reg_i, 0x1234ULL);
    (void)pac.execute_pacda(reg_d, 0x5678ULL);

    TEST_ASSERT(pac.execute_xpaci(reg_i), "xpaci must succeed");
    TEST_ASSERT(reg_i == user_ptr, "xpaci must strip PAC without checking tag");

    TEST_ASSERT(pac.execute_xpacd(reg_d), "xpacd must succeed");
    TEST_ASSERT(reg_d == user_ptr, "xpacd must strip PAC without checking tag");

    // Upper address (EL1 kernel TTBR1)
    const std::uint64_t kern_ptr = 0xFFFF800020004000ULL;
    std::uint64_t reg_k = kern_ptr;
    (void)pac.execute_pacia(reg_k, 0x9999ULL);
    TEST_ASSERT(pac.execute_xpaci(reg_k), "xpaci must succeed on upper address");
    TEST_ASSERT(reg_k == kern_ptr, "xpaci on upper address must preserve canonical 1s extension");

    std::cout << "  -> PASS: XPACI & XPACD Stripping Verified\n";
    return true;
}

// Test 4: FPAC Exception Trapping (EC=0x1C) on Discriminator Mismatch
bool test_fpac_discriminator_mismatch() {
    std::cout << "[Test 4] FPAC Exception Trapping on Discriminator Mismatch...\n";

    Arm64ePointerAuth pac(PacFaultMode::TrapException);
    const std::uint64_t ptr = 0x0000000100040000ULL;
    const std::uint64_t correct_mod = 0x1111ULL;
    const std::uint64_t tampered_mod = 0x2222ULL;
    const std::uint64_t test_pc = 0x0000000100001080ULL;

    std::uint64_t reg = ptr;
    (void)pac.execute_pacia(reg, correct_mod);

    // Attempt to authenticate with tampered discriminator
    bool success = pac.execute_autia(reg, tampered_mod, test_pc);
    TEST_ASSERT(!success, "autia with tampered discriminator must return false");
    TEST_ASSERT(pac.has_fault(), "Fault syndrome must be active");
    TEST_ASSERT(pac.last_syndrome().exception_class == 0x1C, "Syndrome must be EC 0x1C (PAC Trap)");
    TEST_ASSERT(pac.last_syndrome().fault_pc == test_pc, "Syndrome must record fault PC");
    TEST_ASSERT(pac.last_syndrome().failed_key == PacKeyType::InstructionA, "Failed key must be InstructionA");
    TEST_ASSERT(reg == 0, "Trapped register must return 0 in TrapException mode");

    std::cout << "  -> PASS: FPAC Trap on Discriminator Mismatch Verified\n";
    return true;
}

// Test 5: FPAC Exception Trapping on Pointer Tag Tampering
bool test_fpac_tag_corruption() {
    std::cout << "[Test 5] FPAC Exception Trapping on Corrupted Pointer Tag...\n";

    Arm64ePointerAuth pac(PacFaultMode::TrapException);
    const std::uint64_t ptr = 0x0000000100050000ULL;
    const std::uint64_t mod = 0xABCDULL;

    std::uint64_t reg = ptr;
    (void)pac.execute_pacda(reg, mod);

    // Corrupt one bit in the upper PAC tag (e.g. bit 52)
    reg ^= (1ULL << 52u);

    bool success = pac.execute_autda(reg, mod, 0x10002000ULL);
    TEST_ASSERT(!success, "autda on corrupted PAC tag must fail");
    TEST_ASSERT(pac.has_fault(), "Fault syndrome must be active");
    TEST_ASSERT(pac.last_syndrome().exception_class == 0x1C, "EC must be 0x1C");
    TEST_ASSERT(pac.last_syndrome().failed_key == PacKeyType::DataA, "Failed key must be DataA");

    std::cout << "  -> PASS: FPAC Trap on Tag Corruption Verified\n";
    return true;
}

// Test 6: Fallback Bit Corruption Mode (Bit 62 Inversion)
bool test_bit_corruption_mode() {
    std::cout << "[Test 6] Fallback Bit Corruption Mode (Bit 62 Inversion)...\n";

    Arm64ePointerAuth pac(PacFaultMode::CorruptUpperBits);
    const std::uint64_t ptr = 0x0000000100060000ULL;
    const std::uint64_t mod = 0x7777ULL;

    std::uint64_t reg = ptr;
    (void)pac.execute_pacia(reg, mod);

    // Corrupt PAC tag
    reg ^= (1ULL << 50u);

    bool success = pac.execute_autia(reg, mod);
    TEST_ASSERT(!success, "autia must report failure on invalid tag");
    TEST_ASSERT(pac.has_fault(), "Fault syndrome must be set");

    // In bit corruption mode, bit 62 must be set to ensure downstream page fault
    TEST_ASSERT((reg & Arm64ePointerAuth::kPacErrorBit) != 0, "Bit 62 error signature must be injected");
    TEST_ASSERT((reg & Arm64ePointerAuth::kDefaultAddressMask) == ptr, "Canonical lower bits preserved");

    std::cout << "  -> PASS: Bit Corruption Mode Verified\n";
    return true;
}

// Test 7: Branch & Link with Authentication (BLRAA / RETAA) Callstack Integrity
bool test_callstack_integrity() {
    std::cout << "[Test 7] Callstack Integrity with BLRAA and RETAA...\n";

    Arm64ePointerAuth pac(PacFaultMode::TrapException);
    const std::uint64_t callee_addr = 0x0000000100080000ULL;
    const std::uint64_t callee_ctx  = 0x0000000180001000ULL; // Class method vtable context
    const std::uint64_t caller_pc   = 0x0000000100010020ULL;
    const std::uint64_t stack_sp    = 0x00007FFFFFE02000ULL;

    // 1. Sign callee target address with APIA
    std::uint64_t signed_callee = pac.sign_pointer(callee_addr, callee_ctx, PacKeyType::InstructionA);

    // 2. Perform BLRAA
    std::uint64_t lr = 0;
    std::uint64_t branch_target = 0;
    bool blr_ok = pac.execute_blraa(signed_callee, callee_ctx, lr, branch_target, caller_pc);
    TEST_ASSERT(blr_ok, "BLRAA must succeed with valid signature");
    TEST_ASSERT(branch_target == callee_addr, "Branch target must match authenticated address");
    TEST_ASSERT(lr == caller_pc + 4, "LR must be caller_pc + 4");

    // 3. Callee signs LR on stack entry using SP
    std::uint64_t signed_lr = pac.sign_pointer(lr, stack_sp, PacKeyType::InstructionA);

    // 4. Clean RETAA returns to caller
    std::uint64_t ret_target = 0;
    bool ret_ok = pac.execute_retaa(signed_lr, stack_sp, ret_target, callee_addr + 0x40);
    TEST_ASSERT(ret_ok, "RETAA must authenticate LR cleanly");
    TEST_ASSERT(ret_target == lr, "Return target matches caller return address");

    // 5. Stack smashing attack: Attacker overwrites signed LR with forged ROP gadget
    const std::uint64_t rop_gadget = 0x0000000100099999ULL;
    std::uint64_t forged_lr = pac.sign_pointer(rop_gadget, stack_sp, PacKeyType::InstructionB); // Wrong key!
    std::uint64_t forged_ret = 0;
    bool forged_ok = pac.execute_retaa(forged_lr, stack_sp, forged_ret, callee_addr + 0x40);
    TEST_ASSERT(!forged_ok, "RETAA must reject forged return address");
    TEST_ASSERT(pac.has_fault(), "PAC trap must be generated for stack smash attempt");
    TEST_ASSERT(pac.last_syndrome().exception_class == 0x1C, "EC must be 0x1C");

    std::cout << "  -> PASS: BLRAA and RETAA Callstack Integrity Thwarts ROP\n";
    return true;
}

// Test 8: Data Pointer Authentication (Vtable & Function Pointer Security)
bool test_vtable_and_data_pointer_security() {
    std::cout << "[Test 8] Vtable & Data Pointer Security with APDA/APDB...\n";

    Arm64ePointerAuth pac(PacFaultMode::TrapException);
    const std::uint64_t vtable_entry = 0x0000000100071000ULL;
    const std::uint64_t class_id     = 0x4150504CULL; // 'APPL'

    // Sign vtable pointer with APDA
    std::uint64_t signed_vtable = vtable_entry;
    TEST_ASSERT(pac.execute_pacda(signed_vtable, class_id), "pacda must succeed");
    TEST_ASSERT(signed_vtable != vtable_entry, "vtable pointer signed");

    // Authenticate with matching class ID
    std::uint64_t auth_vtable = signed_vtable;
    TEST_ASSERT(pac.execute_autda(auth_vtable, class_id), "autda must succeed");
    TEST_ASSERT(auth_vtable == vtable_entry, "vtable pointer restored");

    // Authenticate with wrong class ID (Type confusion attack)
    std::uint64_t confused_vtable = signed_vtable;
    bool confusion_ok = pac.execute_autda(confused_vtable, 0xDEADBEEFULL);
    TEST_ASSERT(!confusion_ok, "autda must reject mismatched type discriminator");
    TEST_ASSERT(pac.has_fault(), "Type confusion caught by PAC");

    std::cout << "  -> PASS: Vtable / Type Confusion Thwarted by APDA\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  WINIOS STAGE 149: ARM64E PAC CONFORMANCE TEST SUITE            \n";
    std::cout << "  Target: iPhone 15 / Apple A16 Bionic (T8120) / ARM64e          \n";
    std::cout << "  Hardware: ARMv8.6-A PAC2 + FPAC Conformance Architecture       \n";
    std::cout << "=================================================================\n\n";

    int passed = 0;
    int total = 8;

    if (test_key_separation()) passed++;
    if (test_discriminator_diversity()) passed++;
    if (test_xpac_stripping()) passed++;
    if (test_fpac_discriminator_mismatch()) passed++;
    if (test_fpac_tag_corruption()) passed++;
    if (test_bit_corruption_mode()) passed++;
    if (test_callstack_integrity()) passed++;
    if (test_vtable_and_data_pointer_security()) passed++;

    std::cout << "\n=================================================================\n";
    std::cout << "STAGE 149 RESULTS: " << passed << " PASSED, " << (total - passed) << " FAILED\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}

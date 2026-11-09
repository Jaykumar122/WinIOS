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

bool test_pointer_stripping() {
    std::cout << "[Test 1] Pointer Stripping & Canonical Canonicalization...\n";

    // Lower address space (Userspace / TTBR0)
    std::uint64_t user_ptr = 0x0000000100001000ULL;
    std::uint64_t signed_user = user_ptr | (0xABCDULL << 48);
    std::uint64_t stripped_user = Arm64ePointerAuth::strip_pointer(signed_user);
    TEST_ASSERT(stripped_user == user_ptr, "Lower space PAC must strip to 48-bit address with zero top bits");

    // Upper address space (Kernel / TTBR1)
    std::uint64_t kern_ptr = 0xFFFF800012345000ULL;
    std::uint64_t signed_kern = (kern_ptr & 0x0000FFFFFFFFFFFFULL) | (0x1234ULL << 48);
    std::uint64_t stripped_kern = Arm64ePointerAuth::strip_pointer(signed_kern);
    TEST_ASSERT(stripped_kern == kern_ptr, "Upper space PAC must strip to canonical 1s extension");

    std::cout << "  Passed (Canonical stripping verified)\n";
    return true;
}

bool test_pacia_and_autia_roundtrip() {
    std::cout << "[Test 2] PACIA and AUTIA Sign/Auth Roundtrip...\n";

    Arm64ePointerAuth pac;
    std::uint64_t orig = 0x100004568ULL;
    std::uint64_t modifier = 0x7FFFFFE00100ULL; // e.g. Stack frame pointer

    std::uint64_t reg = orig;
    bool ok_sign = pac.execute_pacia(reg, modifier);
    TEST_ASSERT(ok_sign, "execute_pacia must succeed");
    TEST_ASSERT(reg != orig, "Signed pointer must differ from original");
    TEST_ASSERT((reg & Arm64ePointerAuth::kPacMask) != 0, "PAC field must be populated");

    bool ok_auth = pac.execute_autia(reg, modifier);
    TEST_ASSERT(ok_auth, "execute_autia must succeed for matching key & modifier");
    TEST_ASSERT(!pac.has_fault(), "No fault syndrome should be recorded");
    TEST_ASSERT(reg == orig, "Authenticated pointer must match original exactly");

    std::cout << "  Passed (Signature: 0x" << std::hex << (reg >> 48) << std::dec << ")\n";
    return true;
}

bool test_pacib_and_autib_roundtrip() {
    std::cout << "[Test 3] PACIB and AUTIB Key B Differentiation...\n";

    Arm64ePointerAuth pac;
    std::uint64_t ptr = 0x100004568ULL;
    std::uint64_t modifier = 0x5555ULL;

    std::uint64_t reg_a = ptr;
    TEST_ASSERT(pac.execute_pacia(reg_a, modifier), "pacia must succeed");

    std::uint64_t reg_b = ptr;
    TEST_ASSERT(pac.execute_pacib(reg_b, modifier), "pacib must succeed");

    TEST_ASSERT(reg_a != reg_b, "Key A and Key B must produce distinct PAC tags");

    bool ok_auth = pac.execute_autib(reg_b, modifier);
    TEST_ASSERT(ok_auth, "AUTIB must authenticate Key B pointer");
    TEST_ASSERT(reg_b == ptr, "Pointer restored cleanly");

    std::cout << "  Passed (Key A != Key B verified)\n";
    return true;
}

bool test_pacda_and_autda_data_pointers() {
    std::cout << "[Test 4] PACDA and AUTDA Data Pointer Authentication...\n";

    Arm64ePointerAuth pac;
    std::uint64_t data_ptr = 0x180020000ULL; // Data segment / heap
    std::uint64_t modifier = 0x12345678ULL;

    std::uint64_t reg = data_ptr;
    TEST_ASSERT(pac.execute_pacda(reg, modifier), "pacda must succeed");
    TEST_ASSERT((reg & Arm64ePointerAuth::kPacMask) != 0, "PACDA must inject PAC");

    bool ok_auth = pac.execute_autda(reg, modifier);
    TEST_ASSERT(ok_auth, "AUTDA must succeed");
    TEST_ASSERT(reg == data_ptr, "Data pointer restored cleanly");

    std::cout << "  Passed (Data Key A verified)\n";
    return true;
}

bool test_pacdb_and_autdb_data_pointers() {
    std::cout << "[Test 5] PACDB and AUTDB Data Pointer Key B...\n";

    Arm64ePointerAuth pac;
    std::uint64_t data_ptr = 0x180020000ULL;
    std::uint64_t modifier = 0x9999ULL;

    std::uint64_t reg = data_ptr;
    TEST_ASSERT(pac.execute_pacdb(reg, modifier), "pacdb must succeed");
    TEST_ASSERT(reg != data_ptr, "PACDB must sign pointer");

    bool ok_auth = pac.execute_autdb(reg, modifier);
    TEST_ASSERT(ok_auth, "AUTDB must succeed");
    TEST_ASSERT(reg == data_ptr, "Data pointer restored cleanly");

    std::cout << "  Passed (Data Key B verified)\n";
    return true;
}

bool test_paciza_and_autiza_zero_modifier() {
    std::cout << "[Test 6] PACIZA and AUTIZA Zero-Modifier Instructions...\n";

    Arm64ePointerAuth pac;
    std::uint64_t fn_ptr = 0x100001000ULL;

    std::uint64_t reg = fn_ptr;
    TEST_ASSERT(pac.execute_paciza(reg), "paciza must succeed");
    TEST_ASSERT(reg != fn_ptr, "PACIZA must sign with modifier=0");

    bool ok_auth = pac.execute_autiza(reg);
    TEST_ASSERT(ok_auth, "AUTIZA must authenticate modifier=0");
    TEST_ASSERT(reg == fn_ptr, "Function pointer restored cleanly");

    std::cout << "  Passed (Zero-modifier PAC verified)\n";
    return true;
}

bool test_pac_cross_key_tamper_rejection() {
    std::cout << "[Test 7] Cross-Key Tampering Rejection...\n";

    Arm64ePointerAuth pac(PacFaultMode::TrapException);
    std::uint64_t ptr = 0x100008000ULL;
    std::uint64_t mod = 0x42ULL;

    std::uint64_t reg = ptr;
    TEST_ASSERT(pac.execute_pacia(reg, mod), "pacia must succeed"); // Signed with APIAKey

    // Attempt to authenticate with APIBKey
    bool ok_auth = pac.execute_autib(reg, mod);
    TEST_ASSERT(!ok_auth, "AUTIB must fail on pointer signed with APIAKey");
    TEST_ASSERT(pac.has_fault(), "Fault syndrome must be set");
    TEST_ASSERT(pac.last_syndrome().failed_key == PacKeyType::InstructionB, "Failed key must be InstructionB");
    TEST_ASSERT(pac.last_syndrome().exception_class == 0x1C, "EC must be 0x1C (PAC trap)");

    std::cout << "  Passed (Cross-key forgery rejected with EC 0x1C)\n";
    return true;
}

bool test_pac_modifier_tamper_rejection() {
    std::cout << "[Test 8] Context Modifier Tampering Rejection...\n";

    Arm64ePointerAuth pac(PacFaultMode::TrapException);
    std::uint64_t ptr = 0x100008000ULL;
    std::uint64_t valid_mod = 0x1000ULL;
    std::uint64_t bad_mod   = 0x2000ULL; // Tampered context

    std::uint64_t reg = ptr;
    TEST_ASSERT(pac.execute_pacda(reg, valid_mod), "pacda must succeed");

    bool ok_auth = pac.execute_autda(reg, bad_mod);
    TEST_ASSERT(!ok_auth, "AUTDA must fail with tampered modifier");
    TEST_ASSERT(pac.has_fault(), "Fault syndrome must be recorded");

    std::cout << "  Passed (Modifier mismatch caught)\n";
    return true;
}

bool test_fault_mode_comparison() {
    std::cout << "[Test 9] Fault Mode Comparison: Bit Corruption vs FPAC Trap...\n";

    std::uint64_t ptr = 0x100001000ULL;
    std::uint64_t mod = 0x123ULL;

    // 1. Bit Corruption Mode (Pre-ARMv8.6)
    {
        Arm64ePointerAuth pac(PacFaultMode::CorruptUpperBits);
        std::uint64_t reg = ptr;
        TEST_ASSERT(pac.execute_pacia(reg, mod), "pacia must succeed");

        // Tamper pointer
        reg ^= (0x1ULL << 48);
        (void)pac.execute_autia(reg, mod);

        // Bit 62 error bit must be set
        TEST_ASSERT((reg & Arm64ePointerAuth::kPacErrorBit) != 0, "Bit 62 error signature must be injected");
        TEST_ASSERT((reg & Arm64ePointerAuth::kDefaultAddressMask) == ptr, "Base address preserved");
    }

    // 2. FPAC Trap Mode (Apple A16 Bionic hardware default)
    {
        Arm64ePointerAuth pac(PacFaultMode::TrapException);
        std::uint64_t reg = ptr;
        TEST_ASSERT(pac.execute_pacia(reg, mod), "pacia must succeed");

        reg ^= (0x1ULL << 48);
        bool ok = pac.execute_autia(reg, mod);
        TEST_ASSERT(!ok, "FPAC mode must immediately fail");
        TEST_ASSERT(pac.has_fault(), "FPAC must register architectural fault syndrome");
        TEST_ASSERT(reg == 0, "Target returned null in trap mode");
    }

    std::cout << "  Passed (Both FPAC and bit corruption modes verified)\n";
    return true;
}

bool test_branch_and_return_pac() {
    std::cout << "[Test 10] Branch & Return Instructions (BRAA, BLRAA, RETAA)...\n";

    Arm64ePointerAuth pac(PacFaultMode::TrapException);
    std::uint64_t target_func = 0x100002000ULL;
    std::uint64_t modifier    = 0x4000ULL;

    // 1. BRAA
    std::uint64_t signed_target = pac.sign_pointer(target_func, modifier, PacKeyType::InstructionA);
    std::uint64_t bra_dest = 0;
    bool ok_bra = pac.execute_braa(signed_target, modifier, bra_dest);
    TEST_ASSERT(ok_bra, "BRAA must authenticate valid signed branch target");
    TEST_ASSERT(bra_dest == target_func, "Branch destination must match target");

    // 2. BLRAA
    std::uint64_t lr = 0;
    std::uint64_t blr_dest = 0;
    std::uint64_t call_pc = 0x100001040ULL;
    bool ok_blr = pac.execute_blraa(signed_target, modifier, lr, blr_dest, call_pc);
    TEST_ASSERT(ok_blr, "BLRAA must succeed");
    TEST_ASSERT(blr_dest == target_func, "BLRAA destination must match target");
    TEST_ASSERT(lr == call_pc + 4, "BLRAA must record return address in LR");

    // 3. RETAA
    std::uint64_t sp = 0x7FFFFFE00000ULL;
    std::uint64_t signed_lr = pac.sign_pointer(lr, sp, PacKeyType::InstructionA);
    std::uint64_t ret_dest = 0;
    bool ok_ret = pac.execute_retaa(signed_lr, sp, ret_dest);
    TEST_ASSERT(ok_ret, "RETAA must authenticate LR using SP modifier");
    TEST_ASSERT(ret_dest == lr, "Return destination must equal LR");

    // 4. RETAA Tamper (Stack smash / ROP attempt)
    std::uint64_t rop_gadget = 0x100009999ULL;
    std::uint64_t forged_lr = pac.sign_pointer(rop_gadget, sp, PacKeyType::InstructionB); // Wrong key!
    std::uint64_t forged_dest = 0;
    bool ok_forged = pac.execute_retaa(forged_lr, sp, forged_dest);
    TEST_ASSERT(!ok_forged, "RETAA must thwart ROP attempt with wrong key");
    TEST_ASSERT(pac.has_fault(), "Fault recorded for ROP attempt");

    std::cout << "  Passed (Thwarted ROP gadget tampering during RETAA)\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  WINIOS STAGE 122: ARM64e POINTER AUTHENTICATION (PAC) TESTS    \n";
    std::cout << "  Platform: iPhone 15 / Apple A16 Bionic (T8120) / ARM64e        \n";
    std::cout << "  Hardware Spec: ARMv8.6-A PAC2 + FPAC Architectural Model       \n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int total = 10;

    if (test_pointer_stripping()) passed++;
    if (test_pacia_and_autia_roundtrip()) passed++;
    if (test_pacib_and_autib_roundtrip()) passed++;
    if (test_pacda_and_autda_data_pointers()) passed++;
    if (test_pacdb_and_autdb_data_pointers()) passed++;
    if (test_paciza_and_autiza_zero_modifier()) passed++;
    if (test_pac_cross_key_tamper_rejection()) passed++;
    if (test_pac_modifier_tamper_rejection()) passed++;
    if (test_fault_mode_comparison()) passed++;
    if (test_branch_and_return_pac()) passed++;

    std::cout << "-----------------------------------------------------------------\n";
    std::cout << "Stage 122 Tests Complete: " << passed << " / " << total << " passed\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}

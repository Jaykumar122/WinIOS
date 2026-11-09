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

// Test 1: PAC Instruction and Data Signing (PACIA, PACIB, PACDA, PACDB)
bool test_pac_signing_diversity() {
    std::cout << "[Test 1] PAC Instruction & Data Signing Diversity (PACIA, PACIB, PACDA, PACDB)...\n";

    Arm64ePointerAuth pac;
    const std::uint64_t raw_ptr = 0x0000000100048000ULL;
    const std::uint64_t modifier = 0x00007FFFFFE02000ULL;

    std::uint64_t ptr_ia = raw_ptr;
    std::uint64_t ptr_ib = raw_ptr;
    std::uint64_t ptr_da = raw_ptr;
    std::uint64_t ptr_db = raw_ptr;

    TEST_ASSERT(pac.execute_pacia(ptr_ia, modifier), "execute_pacia must succeed");
    TEST_ASSERT(pac.execute_pacib(ptr_ib, modifier), "execute_pacib must succeed");
    TEST_ASSERT(pac.execute_pacda(ptr_da, modifier), "execute_pacda must succeed");
    TEST_ASSERT(pac.execute_pacdb(ptr_db, modifier), "execute_pacdb must succeed");

    // All signed pointers must have PAC tags in bits [63:48]
    TEST_ASSERT((ptr_ia & Arm64ePointerAuth::kPacMask) != 0, "ptr_ia must have non-zero PAC tag");
    TEST_ASSERT((ptr_ib & Arm64ePointerAuth::kPacMask) != 0, "ptr_ib must have non-zero PAC tag");
    TEST_ASSERT((ptr_da & Arm64ePointerAuth::kPacMask) != 0, "ptr_da must have non-zero PAC tag");
    TEST_ASSERT((ptr_db & Arm64ePointerAuth::kPacMask) != 0, "ptr_db must have non-zero PAC tag");

    // All tags must be mutually distinct (cryptographic separation)
    TEST_ASSERT(ptr_ia != ptr_ib, "APIA and APIB tags must differ");
    TEST_ASSERT(ptr_ia != ptr_da, "APIA and APDA tags must differ");
    TEST_ASSERT(ptr_ia != ptr_db, "APIA and APDB tags must differ");
    TEST_ASSERT(ptr_ib != ptr_da, "APIB and APDA tags must differ");
    TEST_ASSERT(ptr_ib != ptr_db, "APIB and APDB tags must differ");
    TEST_ASSERT(ptr_da != ptr_db, "APDA and APDB tags must differ");

    // Raw address bits [47:0] must be perfectly preserved
    TEST_ASSERT((ptr_ia & Arm64ePointerAuth::kDefaultAddressMask) == raw_ptr, "ptr_ia must preserve address bits");
    TEST_ASSERT((ptr_ib & Arm64ePointerAuth::kDefaultAddressMask) == raw_ptr, "ptr_ib must preserve address bits");
    TEST_ASSERT((ptr_da & Arm64ePointerAuth::kDefaultAddressMask) == raw_ptr, "ptr_da must preserve address bits");
    TEST_ASSERT((ptr_db & Arm64ePointerAuth::kDefaultAddressMask) == raw_ptr, "ptr_db must preserve address bits");

    return true;
}

// Test 2: Zero-Modifier PAC Variants (PACIZA, PACIZB, PACDZA, PACDZB)
bool test_pac_zero_modifier_variants() {
    std::cout << "[Test 2] Zero-Modifier PAC Variants (PACIZA, PACIZB, PACDZA, PACDZB)...\n";

    Arm64ePointerAuth pac;
    const std::uint64_t raw_ptr = 0x0000000100084000ULL;

    std::uint64_t p_iza = raw_ptr;
    std::uint64_t p_izb = raw_ptr;
    std::uint64_t p_dza = raw_ptr;
    std::uint64_t p_dzb = raw_ptr;

    TEST_ASSERT(pac.execute_paciza(p_iza), "execute_paciza must succeed");
    TEST_ASSERT(pac.execute_pacizb(p_izb), "execute_pacizb must succeed");
    TEST_ASSERT(pac.execute_pacdza(p_dza), "execute_pacdza must succeed");
    TEST_ASSERT(pac.execute_pacdzb(p_dzb), "execute_pacdzb must succeed");

    // Authenticate with matching zero-modifier auth instructions
    TEST_ASSERT(pac.execute_autiza(p_iza), "execute_autiza must succeed");
    TEST_ASSERT(p_iza == raw_ptr, "AUTIZA must restore canonical pointer");

    TEST_ASSERT(pac.execute_autizb(p_izb), "execute_autizb must succeed");
    TEST_ASSERT(p_izb == raw_ptr, "AUTIZB must restore canonical pointer");

    TEST_ASSERT(pac.execute_autdza(p_dza), "execute_autdza must succeed");
    TEST_ASSERT(p_dza == raw_ptr, "AUTDZA must restore canonical pointer");

    TEST_ASSERT(pac.execute_autdzb(p_dzb), "execute_autdzb must succeed");
    TEST_ASSERT(p_dzb == raw_ptr, "AUTDZB must restore canonical pointer");

    return true;
}

// Test 3: Authentic Pointer Restoration (AUTIA, AUTIB, AUTDA, AUTDB)
bool test_authentic_pointer_restoration() {
    std::cout << "[Test 3] Authentic Pointer Restoration (AUTIA, AUTIB, AUTDA, AUTDB)...\n";

    Arm64ePointerAuth pac;
    const std::uint64_t raw_ptr = 0x0000000180004000ULL;
    const std::uint64_t modifier = 0x00007FFFFFE04000ULL;

    std::uint64_t ptr_ia = raw_ptr;
    std::uint64_t ptr_ib = raw_ptr;
    std::uint64_t ptr_da = raw_ptr;
    std::uint64_t ptr_db = raw_ptr;

    TEST_ASSERT(pac.execute_pacia(ptr_ia, modifier), "pacia ia");
    TEST_ASSERT(pac.execute_pacib(ptr_ib, modifier), "pacib ib");
    TEST_ASSERT(pac.execute_pacda(ptr_da, modifier), "pacda da");
    TEST_ASSERT(pac.execute_pacdb(ptr_db, modifier), "pacdb db");

    TEST_ASSERT(pac.execute_autia(ptr_ia, modifier), "AUTIA must succeed on authentic pointer");
    TEST_ASSERT(ptr_ia == raw_ptr, "ptr_ia must be restored to raw_ptr");

    TEST_ASSERT(pac.execute_autib(ptr_ib, modifier), "AUTIB must succeed on authentic pointer");
    TEST_ASSERT(ptr_ib == raw_ptr, "ptr_ib must be restored to raw_ptr");

    TEST_ASSERT(pac.execute_autda(ptr_da, modifier), "AUTDA must succeed on authentic pointer");
    TEST_ASSERT(ptr_da == raw_ptr, "ptr_da must be restored to raw_ptr");

    TEST_ASSERT(pac.execute_autdb(ptr_db, modifier), "AUTDB must succeed on authentic pointer");
    TEST_ASSERT(ptr_db == raw_ptr, "ptr_db must be restored to raw_ptr");

    return true;
}

// Test 4: Apple A16 FPAC Synchronous Trap Generation (EC = 0x1C)
bool test_fpac_synchronous_trap_generation() {
    std::cout << "[Test 4] Apple A16 FPAC Synchronous Trap Generation (EC = 0x1C)...\n";

    Arm64ePointerAuth pac(PacFaultMode::TrapException);
    const std::uint64_t raw_ptr = 0x0000000100018000ULL;
    const std::uint64_t modifier = 0x00007FFFFFE01000ULL;

    std::uint64_t signed_ptr = raw_ptr;
    TEST_ASSERT(pac.execute_pacia(signed_ptr, modifier), "pacia in test 4");

    // Tamper with signed pointer (flip bit in PAC tag)
    const std::uint64_t expected_corrupted = signed_ptr ^ (1ULL << 56u);
    std::uint64_t corrupted_ptr = expected_corrupted;

    // Authenticate with faulty pointer at PC = 0x1000400
    bool auth_res = pac.execute_autia(corrupted_ptr, modifier, 0x1000400ULL);
    TEST_ASSERT(!auth_res, "Authentication of tampered pointer must fail");

    const auto& syn = pac.last_syndrome();
    TEST_ASSERT(syn.has_fault, "FPAC syndrome has_fault must be true");
    TEST_ASSERT(syn.exception_class == 0x1C, "FPAC syndrome EC must be 0x1C (Synchronous PAC Failure Trap)");
    TEST_ASSERT(syn.fault_pc == 0x1000400ULL, "FPAC fault_pc must match instruction PC");
    TEST_ASSERT(syn.bad_pointer == expected_corrupted, "FPAC bad_pointer must record corrupted pointer");

    return true;
}

// Test 5: Pre-ARMv8.6 Error Bit Injection (PacFaultMode::CorruptUpperBits)
bool test_pre_armv86_error_bit_injection() {
    std::cout << "[Test 5] Pre-ARMv8.6 Error Bit Injection (CorruptUpperBits)...\n";

    Arm64ePointerAuth pac(PacFaultMode::CorruptUpperBits);
    const std::uint64_t raw_ptr = 0x0000000100020000ULL;
    const std::uint64_t modifier = 0x00007FFFFFE02000ULL;

    std::uint64_t signed_ptr = raw_ptr;
    TEST_ASSERT(pac.execute_pacia(signed_ptr, modifier), "pacia in test 4");

    // Corrupt the pointer
    std::uint64_t corrupted_ptr = signed_ptr ^ (1ULL << 50u);

    bool auth_res = pac.execute_autia(corrupted_ptr, modifier, 0x2000);
    TEST_ASSERT(!auth_res, "Authentication must return false");

    // In CorruptUpperBits mode, the returned pointer has bit 62 (kPacErrorBit) asserted
    TEST_ASSERT((corrupted_ptr & Arm64ePointerAuth::kPacErrorBit) != 0,
                "Error bit 62 must be asserted in upper PAC field on failure");

    return true;
}

// Test 6: XPACI and XPACD Tag Stripping Conformance
bool test_xpac_tag_stripping() {
    std::cout << "[Test 6] XPACI & XPACD Tag Stripping Conformance...\n";

    Arm64ePointerAuth pac;
    const std::uint64_t raw_ptr = 0x0000000100030000ULL;
    const std::uint64_t modifier = 0x00007FFFFFE03000ULL;

    std::uint64_t ptr_i = raw_ptr;
    std::uint64_t ptr_d = raw_ptr;

    TEST_ASSERT(pac.execute_pacia(ptr_i, modifier), "pacia in test 6");
    TEST_ASSERT(pac.execute_pacda(ptr_d, modifier), "pacda in test 6");

    TEST_ASSERT(ptr_i != raw_ptr, "ptr_i must be signed");
    TEST_ASSERT(ptr_d != raw_ptr, "ptr_d must be signed");

    TEST_ASSERT(pac.execute_xpaci(ptr_i), "execute_xpaci must succeed");
    TEST_ASSERT(ptr_i == raw_ptr, "XPACI must restore original pointer by stripping tag");

    TEST_ASSERT(pac.execute_xpacd(ptr_d), "execute_xpacd must succeed");
    TEST_ASSERT(ptr_d == raw_ptr, "XPACD must restore original pointer by stripping tag");

    return true;
}

// Test 7: Architectural Key Isolation & Rotation
bool test_key_isolation_and_rotation() {
    std::cout << "[Test 7] Architectural Key Isolation & Rotation...\n";

    Arm64ePointerAuth pac;
    const std::uint64_t raw_ptr = 0x0000000100050000ULL;
    const std::uint64_t modifier = 0x00007FFFFFE05000ULL;

    std::uint64_t signed_ptr = raw_ptr;
    TEST_ASSERT(pac.execute_pacia(signed_ptr, modifier), "pacia in test 4");

    // Rotate the APIA key (simulate process switch or key regeneration)
    pac.set_key(PacKeyType::InstructionA, 0x1111222233334444ULL, 0x5555666677778888ULL);

    // Authentication with old signature using new key MUST FAIL
    bool auth_res = pac.execute_autia(signed_ptr, modifier, 0x3000);
    TEST_ASSERT(!auth_res, "Pointer signed with old key must fail auth under rotated key");

    // Re-signing with new key produces new signature
    std::uint64_t new_signed_ptr = raw_ptr;
    TEST_ASSERT(pac.execute_pacia(new_signed_ptr, modifier), "pacia in test 7");
    TEST_ASSERT(new_signed_ptr != signed_ptr, "Re-signing with new key must produce distinct PAC tag");

    // Authenticating new signed pointer under new key must succeed
    TEST_ASSERT(pac.execute_autia(new_signed_ptr, modifier, 0x3000), "Auth with new key must succeed");
    TEST_ASSERT(new_signed_ptr == raw_ptr, "Original pointer must be restored");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 161: ARM64e PAC and FPAC Independent Verification\n";
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

    run_test(test_pac_signing_diversity, "PAC Instruction & Data Signing Diversity");
    run_test(test_pac_zero_modifier_variants, "Zero-Modifier PAC Variants");
    run_test(test_authentic_pointer_restoration, "Authentic Pointer Restoration");
    run_test(test_fpac_synchronous_trap_generation, "Apple A16 FPAC Synchronous Trap (EC = 0x1C)");
    run_test(test_pre_armv86_error_bit_injection, "Pre-ARMv8.6 Error Bit Injection");
    run_test(test_xpac_tag_stripping, "XPACI & XPACD Tag Stripping");
    run_test(test_key_isolation_and_rotation, "Architectural Key Isolation & Rotation");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}

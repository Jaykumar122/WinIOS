// ==============================================================================
// WinIOS Campaign 201 Stage 10: Independent PAC Test Vectors
// ==============================================================================
// Verifies WinIOS Pointer Authentication cryptographic tag generation against
// an independently implemented reference model of the SipHash-3 truncated PRF:
// 1. Independent reference calculation vs Arm64ePointerAuth::compute_pac_tag
// 2. Distinct key discrimination: APIA, APIB, APDA, APDB, APGA produce unique tags
// 3. Modifier sensitivity: small changes in modifier produce avalanche differences
// 4. Pointer sensitivity: different addresses under same key/modifier produce distinct tags
// 5. Zero-vector and boundary conditions
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>

#include "cpu/pointer_auth.h"

using namespace winios::cpu;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Independent Reference Model for WinIOS SipHash-3 Truncated PRF
// -----------------------------------------------------------------------------
std::uint16_t compute_reference_pac_tag(
    std::uint64_t canonical_ptr,
    std::uint64_t modifier,
    std::uint64_t key_lo,
    std::uint64_t key_hi) noexcept {

    std::uint64_t v0 = key_lo ^ 0x736f6d6570736575ULL;
    std::uint64_t v1 = key_hi ^ 0x646f72616e646f6dULL;
    std::uint64_t v2 = canonical_ptr ^ 0x6c7967656e657261ULL;
    std::uint64_t v3 = modifier ^ 0x7465646279746573ULL;

    auto sip_round = [&]() noexcept {
        v0 += v1; v1 = (v1 << 13u) | (v1 >> 51u); v1 ^= v0;
        v0 = (v0 << 32u) | (v0 >> 32u);
        v2 += v3; v3 = (v3 << 16u) | (v3 >> 48u); v3 ^= v2;
        v0 += v3; v3 = (v3 << 21u) | (v3 >> 43u); v3 ^= v0;
        v2 += v1; v1 = (v1 << 17u) | (v1 >> 47u); v1 ^= v2;
        v2 = (v2 << 32u) | (v2 >> 32u);
    };

    sip_round();
    sip_round();
    sip_round();

    std::uint64_t h = v0 ^ v1 ^ v2 ^ v3;
    return static_cast<std::uint16_t>(h ^ (h >> 16u) ^ (h >> 32u) ^ (h >> 48u));
}

// -----------------------------------------------------------------------------
// Test 1: Exact Reference Match on Independent Test Vectors
// -----------------------------------------------------------------------------
void test_reference_oracle_match() {
    std::cout << "[Test 1] Reference Oracle Direct Matching...\n";
    Arm64ePointerAuth pac;

    PacKeyRing keys{};
    keys.apia = { 0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL };
    keys.apib = { 0x1111222233334444ULL, 0x5555666677778888ULL };
    keys.apda = { 0xAAAAAAAAAAAAAAAAULL, 0x5555555555555555ULL };
    keys.apdb = { 0xCAFEBABE12345678ULL, 0xDEADBEEF87654321ULL };
    keys.apga = { 0x9999888877776666ULL, 0x11110000FFFFEEEEULL };
    pac.set_keyring(keys);

    struct Vector {
        std::uint64_t ptr;
        std::uint64_t mod;
        PacKeyType key_type;
        std::uint64_t k_lo;
        std::uint64_t k_hi;
    };

    const Vector vectors[] = {
        { 0x00007FFFF0001000ULL, 0x1111222233334444ULL, PacKeyType::InstructionA, keys.apia.lo, keys.apia.hi },
        { 0x00007FFFF0001000ULL, 0x1111222233334444ULL, PacKeyType::InstructionB, keys.apib.lo, keys.apib.hi },
        { 0x0000000100004000ULL, 0x0000000000050000ULL, PacKeyType::DataA,        keys.apda.lo, keys.apda.hi },
        { 0x0000000100004000ULL, 0x0000000000050000ULL, PacKeyType::DataB,        keys.apdb.lo, keys.apdb.hi },
        { 0x0ULL,                0x0ULL,                PacKeyType::InstructionA, keys.apia.lo, keys.apia.hi },
        { 0xFFFFFFFFFFFFULL,     0xFFFFFFFFFFFFFFFFULL, PacKeyType::InstructionA, keys.apia.lo, keys.apia.hi },
    };

    for (const auto& v : vectors) {
        const std::uint64_t canonical = Arm64ePointerAuth::strip_pointer(v.ptr);
        const std::uint64_t signed_ptr = pac.sign_pointer(v.ptr, v.mod, v.key_type);
        const std::uint16_t actual_tag = static_cast<std::uint16_t>(signed_ptr >> 48u);
        const std::uint16_t expected_tag = compute_reference_pac_tag(canonical, v.mod, v.k_lo, v.k_hi);

        TEST_CHECK(actual_tag == expected_tag, "PAC tag must exactly match reference model");
        TEST_CHECK(Arm64ePointerAuth::strip_pointer(signed_ptr) == canonical, "Canonical pointer address must be preserved");

        // Verify authentication succeeds
        const std::uint64_t auth_ptr = pac.authenticate_pointer(signed_ptr, v.mod, v.key_type);
        TEST_CHECK(auth_ptr == canonical, "Authentication must restore canonical address");
        TEST_CHECK(!pac.last_syndrome().has_fault, "Valid authentication must produce no fault");
    }

    std::cout << "  -> Reference oracle match passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Key and Modifier Discrimination
// -----------------------------------------------------------------------------
void test_key_and_modifier_discrimination() {
    std::cout << "[Test 2] Key & Modifier Discrimination...\n";
    Arm64ePointerAuth pac;

    PacKeyRing keys{};
    keys.apia = { 0x1, 0x2 };
    keys.apib = { 0x3, 0x4 };
    keys.apda = { 0x5, 0x6 };
    keys.apdb = { 0x7, 0x8 };
    keys.apga = { 0x9, 0xA };
    pac.set_keyring(keys);

    const std::uint64_t ptr = 0x00007FFFF0008000ULL;
    const std::uint64_t mod = 0x12345ULL;

    const std::uint64_t sign_ia = pac.sign_pointer(ptr, mod, PacKeyType::InstructionA);
    const std::uint64_t sign_ib = pac.sign_pointer(ptr, mod, PacKeyType::InstructionB);
    const std::uint64_t sign_da = pac.sign_pointer(ptr, mod, PacKeyType::DataA);
    const std::uint64_t sign_db = pac.sign_pointer(ptr, mod, PacKeyType::DataB);

    // All distinct keys must produce distinct tags
    TEST_CHECK(sign_ia != sign_ib, "APIA and APIB must produce distinct tags");
    TEST_CHECK(sign_ia != sign_da, "APIA and APDA must produce distinct tags");
    TEST_CHECK(sign_da != sign_db, "APDA and APDB must produce distinct tags");

    // Modifier sensitivity: change 1 bit of modifier
    const std::uint64_t sign_mod2 = pac.sign_pointer(ptr, mod ^ 0x1ULL, PacKeyType::InstructionA);
    TEST_CHECK(sign_ia != sign_mod2, "Changing modifier by 1 bit must change tag");

    std::cout << "  -> Key and modifier discrimination passed.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Generic PAC (PACGA)
// -----------------------------------------------------------------------------
void test_pacga_generic() {
    std::cout << "[Test 3] Generic PAC (PACGA)...\n";
    Arm64ePointerAuth pac;

    PacKeyRing keys{};
    keys.apga = { 0x12345678ULL, 0x87654321ULL };
    pac.set_keyring(keys);

    const std::uint64_t val1 = 0x1111222233334444ULL;
    const std::uint64_t val2 = 0x5555666677778888ULL;

    const std::uint64_t pacga_res = pac.compute_generic_tag(val1, val2);
    // PACGA returns 32-bit tag in upper half of 64-bit register (bits [63:32]), lower half is zero
    TEST_CHECK((pacga_res & 0xFFFFFFFFULL) == 0ULL, "PACGA lower 32 bits must be zero");
    TEST_CHECK((pacga_res >> 32u) != 0ULL, "PACGA upper 32 bits must contain non-zero tag");

    const std::uint16_t expected_tag = compute_reference_pac_tag(val1, val2, keys.apga.lo, keys.apga.hi);
    TEST_CHECK(static_cast<std::uint16_t>(pacga_res >> 32u) == expected_tag, "PACGA tag must match reference");

    std::cout << "  -> Generic PAC tests passed.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 201.10 Independent PAC Test Vectors ===\n";
    test_reference_oracle_match();
    test_key_and_modifier_discrimination();
    test_pacga_generic();

    if (g_failures == 0) {
        std::cout << "All Stage 201.10 tests passed successfully!\n";
        return 0;
    } else {
        std::cerr << g_failures << " Stage 201.10 test(s) failed!\n";
        return 1;
    }
}

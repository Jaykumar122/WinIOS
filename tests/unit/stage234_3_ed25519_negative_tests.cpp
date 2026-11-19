#include "crypto/ed25519.h"
#include "crypto/sha512.h"
#include "security/security_utils.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace winios;
using namespace winios::crypto;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ")\n"; \
            std::exit(1); \
        } \
    } while (0)

// Helper: L_ORDER bytes (Little Endian)
// 0x1000000000000000000000000000000014def9dea2f79cd65812631a5cf5d3ed
static const std::uint8_t kLOrderBytes[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
    0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
};

// P_MOD bytes (Little Endian): 2^255 - 19
static const std::uint8_t kPModBytes[32] = {
    0xed, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f
};

// -----------------------------------------------------------------------------
// Test 1: Non-Canonical and Out-of-Bounds Public Keys (y >= p and Not on Curve)
// -----------------------------------------------------------------------------
static void test_invalid_public_keys() {
    std::cout << "[Stage 234.3] Test 1: Invalid Public Key Encodings...\n";

    // Valid baseline signature and message
    Ed25519PublicKey valid_pub{};
    Ed25519PrivateKey valid_priv{};
    TEST_ASSERT(Ed25519::generate_keypair(valid_pub, valid_priv), "Generate baseline keypair");
    std::string msg = "Valid test message for Ed25519 negative tests";
    Ed25519Signature valid_sig{};
    Ed25519::sign(valid_sig, msg.data(), msg.size(), valid_pub, valid_priv);
    TEST_ASSERT(Ed25519::verify(valid_sig, msg.data(), msg.size(), valid_pub), "Baseline verification succeeds");

    // Case 1a: Public key y == p (2^255 - 19)
    Ed25519PublicKey pub_y_p{};
    std::memcpy(pub_y_p.data(), kPModBytes, 32);
    TEST_ASSERT(!Ed25519::verify(valid_sig, msg.data(), msg.size(), pub_y_p), "Reject public key with y == p");

    // Case 1b: Public key y == p + 1 (2^255 - 18)
    Ed25519PublicKey pub_y_p_plus_1 = pub_y_p;
    pub_y_p_plus_1[0] = 0xee;
    TEST_ASSERT(!Ed25519::verify(valid_sig, msg.data(), msg.size(), pub_y_p_plus_1), "Reject public key with y == p + 1");

    // Case 1c: Public key y == 2^255 - 1 (all 0xFF with bit 255 cleared)
    Ed25519PublicKey pub_y_max{};
    pub_y_max.fill(0xFF);
    pub_y_max[31] &= 0x7F; // y = 2^255 - 1 > p
    TEST_ASSERT(!Ed25519::verify(valid_sig, msg.data(), msg.size(), pub_y_max), "Reject public key with y == 2^255 - 1");

    // Case 1d: y on interval [0, p-1] but not a valid quadratic residue (not on Curve25519)
    // y = 2 has (2^2 - 1)/(d*2^2 + 1) = 3/(4d + 1) which is a known non-residue in F_p
    Ed25519PublicKey pub_not_on_curve{};
    pub_not_on_curve[0] = 0x02;
    TEST_ASSERT(!Ed25519::verify(valid_sig, msg.data(), msg.size(), pub_not_on_curve), "Reject public key not on curve");

    std::cout << "  -> All invalid public-key encodings rejected.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Invalid R-Point Encodings in Signature
// -----------------------------------------------------------------------------
static void test_invalid_r_points() {
    std::cout << "[Stage 234.3] Test 2: Invalid R-Point Encodings in Signature...\n";

    Ed25519PublicKey pub{};
    Ed25519PrivateKey priv{};
    TEST_ASSERT(Ed25519::generate_keypair(pub, priv), "Generate keypair");
    std::string msg = "Message for R-point verification";
    Ed25519Signature sig{};
    Ed25519::sign(sig, msg.data(), msg.size(), pub, priv);

    // Case 2a: R with y == p
    Ed25519Signature sig_r_p = sig;
    std::memcpy(sig_r_p.data(), kPModBytes, 32);
    TEST_ASSERT(!Ed25519::verify(sig_r_p, msg.data(), msg.size(), pub), "Reject signature with R.y == p");

    // Case 2b: R with y == 2^255 - 1
    Ed25519Signature sig_r_max = sig;
    for (int i = 0; i < 31; ++i) sig_r_max[i] = 0xFF;
    sig_r_max[31] = 0x7F; // y = 2^255 - 1
    TEST_ASSERT(!Ed25519::verify(sig_r_max, msg.data(), msg.size(), pub), "Reject signature with R.y == 2^255 - 1");

    // Case 2c: R not on curve (y = 2)
    Ed25519Signature sig_r_non_curve = sig;
    std::memset(sig_r_non_curve.data(), 0, 32);
    sig_r_non_curve[0] = 0x02;
    TEST_ASSERT(!Ed25519::verify(sig_r_non_curve, msg.data(), msg.size(), pub), "Reject signature with R not on curve");

    std::cout << "  -> All invalid R-point encodings rejected.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Non-Canonical S Values (S >= L and S == 0) & Malleability Defense
// -----------------------------------------------------------------------------
static void test_non_canonical_s_values() {
    std::cout << "[Stage 234.3] Test 3: Non-Canonical S Values and Malleability Defense...\n";

    Ed25519PublicKey pub{};
    Ed25519PrivateKey priv{};
    TEST_ASSERT(Ed25519::generate_keypair(pub, priv), "Generate keypair");
    std::string msg = "Test message for S-scalar bounds";
    Ed25519Signature sig{};
    Ed25519::sign(sig, msg.data(), msg.size(), pub, priv);
    TEST_ASSERT(Ed25519::verify(sig, msg.data(), msg.size(), pub), "Valid signature verifies");

    // Case 3a: S == 0 (must be rejected)
    Ed25519Signature sig_s_zero = sig;
    std::memset(sig_s_zero.data() + 32, 0, 32);
    TEST_ASSERT(!Ed25519::verify(sig_s_zero, msg.data(), msg.size(), pub), "Reject S == 0");

    // Case 3b: S == L (group order: RFC 8032 Section 5.1.7 mandates S < L)
    Ed25519Signature sig_s_L = sig;
    std::memcpy(sig_s_L.data() + 32, kLOrderBytes, 32);
    TEST_ASSERT(!Ed25519::verify(sig_s_L, msg.data(), msg.size(), pub), "Reject S == L");

    // Case 3c: S == L + 1
    Ed25519Signature sig_s_L_plus_1 = sig_s_L;
    sig_s_L_plus_1[32] = static_cast<std::uint8_t>(sig_s_L_plus_1[32] + 1);
    TEST_ASSERT(!Ed25519::verify(sig_s_L_plus_1, msg.data(), msg.size(), pub), "Reject S == L + 1");

    // Case 3d: S == 2^256 - 1 (all 0xFF)
    Ed25519Signature sig_s_max = sig;
    std::memset(sig_s_max.data() + 32, 0xFF, 32);
    TEST_ASSERT(!Ed25519::verify(sig_s_max, msg.data(), msg.size(), pub), "Reject S == 2^256 - 1");

    std::cout << "  -> All non-canonical S-scalar values rejected (Malleability defense verified).\n";
}

// -----------------------------------------------------------------------------
// Test 4: Small-Order and Degenerate Points
// -----------------------------------------------------------------------------
static void test_small_order_points() {
    std::cout << "[Stage 234.3] Test 4: Small-Order and Identity Point Rejection...\n";

    Ed25519PublicKey pub{};
    Ed25519PrivateKey priv{};
    TEST_ASSERT(Ed25519::generate_keypair(pub, priv), "Generate keypair");
    std::string msg = "Identity point test message";
    Ed25519Signature sig{};
    Ed25519::sign(sig, msg.data(), msg.size(), pub, priv);

    // Case 4a: Identity Point as Public Key (y = 1, x = 0: 0x01, 0x00...0x00)
    Ed25519PublicKey pub_identity{};
    pub_identity[0] = 0x01;
    TEST_ASSERT(!Ed25519::verify(sig, msg.data(), msg.size(), pub_identity), "Reject identity public key");

    // Case 4b: Identity Point as R in Signature
    Ed25519Signature sig_r_identity = sig;
    std::memset(sig_r_identity.data(), 0, 32);
    sig_r_identity[0] = 0x01;
    TEST_ASSERT(!Ed25519::verify(sig_r_identity, msg.data(), msg.size(), pub), "Reject identity R-point in signature");

    // Case 4c: All-zero point (y = 0, x = 0 is NOT on Curve25519: 0^2 - 0^2 = 0 != 1)
    Ed25519PublicKey pub_zero{};
    TEST_ASSERT(!Ed25519::verify(sig, msg.data(), msg.size(), pub_zero), "Reject all-zero public key");

    std::cout << "  -> Small-order and identity points rejected.\n";
}

// -----------------------------------------------------------------------------
// Test 5: Message Boundary Sizes & Repeated Malformed Input Stress
// -----------------------------------------------------------------------------
static void test_boundary_messages_and_stress() {
    std::cout << "[Stage 234.3] Test 5: Boundary Messages & Malformed Input Stress (100 Iterations)...\n";

    Ed25519PublicKey pub{};
    Ed25519PrivateKey priv{};
    TEST_ASSERT(Ed25519::generate_keypair(pub, priv), "Generate keypair");

    // 5a: Empty message
    {
        Ed25519Signature empty_sig{};
        Ed25519::sign(empty_sig, nullptr, 0, pub, priv);
        TEST_ASSERT(Ed25519::verify(empty_sig, nullptr, 0, pub), "Empty message signature verified");
    }

    // 5b: Large 64 KB message
    {
        std::vector<std::uint8_t> large_msg(65536, 0xAA);
        Ed25519Signature large_sig{};
        Ed25519::sign(large_sig, large_msg.data(), large_msg.size(), pub, priv);
        TEST_ASSERT(Ed25519::verify(large_sig, large_msg.data(), large_msg.size(), pub), "64 KB message signature verified");
    }

    // 5c: 100 Malformed Input Permutations
    std::mt19937_64 rng(0x234C0DE);
    for (int iter = 0; iter < 100; ++iter) {
        Ed25519Signature junk_sig{};
        Ed25519PublicKey junk_pub{};
        for (std::size_t i = 0; i < 64; ++i) {
            junk_sig[i] = static_cast<std::uint8_t>(rng() & 0xFF);
        }
        for (std::size_t i = 0; i < 32; ++i) {
            junk_pub[i] = static_cast<std::uint8_t>(rng() & 0xFF);
        }

        const char* test_payload = "Adversarial stress payload";
        // Must safely return false without crashing or throwing
        bool result = Ed25519::verify(junk_sig, test_payload, std::strlen(test_payload), junk_pub);
        // It is astronomically improbable that random 64-byte bytes form a valid signature on a random key
        TEST_ASSERT(!result, "Random signature and key rejected");
    }

    std::cout << "  -> 100 malformed permutations safely rejected.\n";
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  Stage 234.3: Ed25519 Negative and Malformed Input Test Suite   \n";
    std::cout << "=================================================================\n";

    test_invalid_public_keys();
    test_invalid_r_points();
    test_non_canonical_s_values();
    test_small_order_points();
    test_boundary_messages_and_stress();

    std::cout << "\n=================================================================\n";
    std::cout << "  ALL ED25519 NEGATIVE AND MALFORMED TESTS PASSED!               \n";
    std::cout << "=================================================================\n";
    return 0;
}

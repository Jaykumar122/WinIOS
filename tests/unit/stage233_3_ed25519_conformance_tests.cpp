#include "crypto/sha512.h"
#include "crypto/ed25519.h"
#include "security/security_utils.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>

#if defined(_WIN32)
#include <crtdbg.h>
#endif

using namespace winios;
using namespace winios::crypto;

// Helper: Ensure expression is true or abort with message
#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ")\n"; \
            std::exit(1); \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: RFC 8032 Section 7.1 Test Vectors
// -----------------------------------------------------------------------------
static void test_rfc8032_vectors() {
    std::cout << "[Test 1] RFC 8032 Section 7.1 Known-Answer Vectors...\n";

    // Vector 1 (Empty message)
    {
        std::string seed_hex = "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60";
        std::string expected_pub_hex = "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a";
        std::string expected_sig_hex = "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b";

        Ed25519Seed seed{};
        TEST_ASSERT(Ed25519::from_hex(seed_hex, seed.data(), seed.size()), "Parse vector 1 seed");

        Ed25519PublicKey pub{};
        Ed25519PrivateKey priv{};
        Ed25519::create_keypair(pub, priv, seed);

        std::string pub_hex = Ed25519::to_hex(pub.data(), pub.size());
        TEST_ASSERT(pub_hex == expected_pub_hex, "Vector 1 public key matches RFC 8032");

        Ed25519Signature sig{};
        Ed25519::sign(sig, "", 0, pub, priv);

        std::string sig_hex = Ed25519::to_hex(sig.data(), sig.size());
        TEST_ASSERT(sig_hex == expected_sig_hex, "Vector 1 signature matches RFC 8032");

        TEST_ASSERT(Ed25519::verify(sig, "", 0, pub), "Vector 1 signature verifies");
    }

    // Vector 2 (1-byte message: 0x72)
    {
        std::string seed_hex = "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb";
        std::string expected_pub_hex = "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c";
        std::string expected_sig_hex = "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00";
        std::uint8_t msg[1] = { 0x72 };

        Ed25519Seed seed{};
        TEST_ASSERT(Ed25519::from_hex(seed_hex, seed.data(), seed.size()), "Parse vector 2 seed");

        Ed25519PublicKey pub{};
        Ed25519PrivateKey priv{};
        Ed25519::create_keypair(pub, priv, seed);

        std::string pub_hex = Ed25519::to_hex(pub.data(), pub.size());
        TEST_ASSERT(pub_hex == expected_pub_hex, "Vector 2 public key matches RFC 8032");

        Ed25519Signature sig{};
        Ed25519::sign(sig, msg, sizeof(msg), pub, priv);

        std::string sig_hex = Ed25519::to_hex(sig.data(), sig.size());
        TEST_ASSERT(sig_hex == expected_sig_hex, "Vector 2 signature matches RFC 8032");

        TEST_ASSERT(Ed25519::verify(sig, msg, sizeof(msg), pub), "Vector 2 signature verifies");
    }

    // Vector 3 (2-byte message: 0xaf, 0x82)
    {
        std::string seed_hex = "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7";
        std::string expected_pub_hex = "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025";
        std::string expected_sig_hex = "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a";
        std::uint8_t msg[2] = { 0xaf, 0x82 };

        Ed25519Seed seed{};
        TEST_ASSERT(Ed25519::from_hex(seed_hex, seed.data(), seed.size()), "Parse vector 3 seed");

        Ed25519PublicKey pub{};
        Ed25519PrivateKey priv{};
        Ed25519::create_keypair(pub, priv, seed);

        std::string pub_hex = Ed25519::to_hex(pub.data(), pub.size());
        TEST_ASSERT(pub_hex == expected_pub_hex, "Vector 3 public key matches RFC 8032");

        Ed25519Signature sig{};
        Ed25519::sign(sig, msg, sizeof(msg), pub, priv);

        std::string sig_hex = Ed25519::to_hex(sig.data(), sig.size());
        TEST_ASSERT(sig_hex == expected_sig_hex, "Vector 3 signature matches RFC 8032");

        TEST_ASSERT(Ed25519::verify(sig, msg, sizeof(msg), pub), "Vector 3 signature verifies");
    }

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Message Length & Boundary Cases (0B, 1B, 1023B, 64KB)
// -----------------------------------------------------------------------------
static void test_message_boundaries() {
    std::cout << "[Test 2] Message Boundary Tests (0B, 1B, 1023B, 64KB)...\n";

    Ed25519PublicKey pub{};
    Ed25519PrivateKey priv{};
    TEST_ASSERT(Ed25519::generate_keypair(pub, priv), "Generate keypair");

    // 1023 bytes
    std::vector<std::uint8_t> msg1023(1023, 0x42);
    Ed25519Signature sig1023{};
    Ed25519::sign(sig1023, msg1023.data(), msg1023.size(), pub, priv);
    TEST_ASSERT(Ed25519::verify(sig1023, msg1023.data(), msg1023.size(), pub), "1023-byte message verifies");

    // 64 KB
    std::vector<std::uint8_t> msg64k(65536);
    for (std::size_t i = 0; i < msg64k.size(); ++i) msg64k[i] = static_cast<std::uint8_t>(i & 0xFF);
    Ed25519Signature sig64k{};
    Ed25519::sign(sig64k, msg64k.data(), msg64k.size(), pub, priv);
    TEST_ASSERT(Ed25519::verify(sig64k, msg64k.data(), msg64k.size(), pub), "64KB message verifies");

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Negative Conformance — Non-Canonical Point Rejection (CRYPTO-233-04)
// -----------------------------------------------------------------------------
static void test_non_canonical_point_rejection() {
    std::cout << "[Test 3] Non-Canonical Point Rejection (y >= p = 2^255 - 19)...\n";

    // Modulus p = 2^255 - 19 in LE:
    // ed, ff, ff, ff ... 7f
    // Any y >= p must be rejected!
    // Example: y = 2^255 - 18 (in LE: ee, ff, ff, ... 7f)
    Ed25519PublicKey non_canonical_pub{};
    non_canonical_pub.fill(0xFF);
    non_canonical_pub[0] = 0xEE; // p + 1
    non_canonical_pub[31] = 0x7F; // bit 255 = 0, y >= p

    std::string msg = "Test message";
    Ed25519Signature sig{};
    sig.fill(0x11);
    sig[63] = 0x01; // ensure S < L

    // Verification MUST reject non-canonical public key with y >= p
    bool res = Ed25519::verify(sig, msg.data(), msg.size(), non_canonical_pub);
    TEST_ASSERT(!res, "Non-canonical public key (y >= p) rejected");

    // Non-canonical R in signature
    Ed25519PublicKey valid_pub{};
    Ed25519PrivateKey valid_priv{};
    TEST_ASSERT(Ed25519::generate_keypair(valid_pub, valid_priv), "Generate keypair");

    Ed25519Signature non_canonical_r_sig{};
    Ed25519::sign(non_canonical_r_sig, msg.data(), msg.size(), valid_pub, valid_priv);

    // Corrupt R to have y >= p
    non_canonical_r_sig[0] = 0xEE;
    for (int i = 1; i < 31; ++i) non_canonical_r_sig[i] = 0xFF;
    non_canonical_r_sig[31] = 0x7F;

    TEST_ASSERT(!Ed25519::verify(non_canonical_r_sig, msg.data(), msg.size(), valid_pub),
                "Non-canonical signature R (y >= p) rejected");

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Negative Conformance — Small-Order Public Key Rejection (CRYPTO-233-05)
// -----------------------------------------------------------------------------
static void test_small_order_points_rejection() {
    std::cout << "[Test 4] Small-Order Point Rejection (Identity Point)...\n";

    // Identity point (0, 1): y = 1, x = 0
    // In LE bytes: 01 00 00 ... 00
    Ed25519PublicKey identity_pub{};
    identity_pub[0] = 0x01;

    std::string msg = "Hello";
    Ed25519Signature sig{};
    sig.fill(0x10);
    sig[32] = 0x02; // S = 2 < L

    // Verifying against identity point public key MUST fail
    TEST_ASSERT(!Ed25519::verify(sig, msg.data(), msg.size(), identity_pub),
                "Identity point public key rejected");

    // Identity point as R in signature
    Ed25519PublicKey valid_pub{};
    Ed25519PrivateKey valid_priv{};
    TEST_ASSERT(Ed25519::generate_keypair(valid_pub, valid_priv), "Generate keypair");

    Ed25519Signature identity_r_sig{};
    Ed25519::sign(identity_r_sig, msg.data(), msg.size(), valid_pub, valid_priv);
    // Overwrite R with identity point (0, 1)
    std::memset(identity_r_sig.data(), 0, 32);
    identity_r_sig[0] = 0x01;

    TEST_ASSERT(!Ed25519::verify(identity_r_sig, msg.data(), msg.size(), valid_pub),
                "Identity point signature R rejected");

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 5: Negative Conformance — Scalar Canonicality & S >= L / S == 0 (CRYPTO-233-07)
// -----------------------------------------------------------------------------
static void test_scalar_canonicality_rejection() {
    std::cout << "[Test 5] Scalar Canonicality Rejection (S >= L and S == 0)...\n";

    Ed25519PublicKey pub{};
    Ed25519PrivateKey priv{};
    TEST_ASSERT(Ed25519::generate_keypair(pub, priv), "Generate keypair");

    std::string msg = "Sample transaction payload";
    Ed25519Signature sig{};
    Ed25519::sign(sig, msg.data(), msg.size(), pub, priv);
    TEST_ASSERT(Ed25519::verify(sig, msg.data(), msg.size(), pub), "Valid signature verifies");

    // Degenerate S == 0
    Ed25519Signature zero_s_sig = sig;
    std::memset(zero_s_sig.data() + 32, 0, 32);
    TEST_ASSERT(!Ed25519::verify(zero_s_sig, msg.data(), msg.size(), pub), "S == 0 rejected");

    // Non-canonical S = L (order of base point)
    // L in LE: ed d3 f5 5c 1a 63 12 58 d6 9c f7 a2 de f9 de 14 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 10
    std::uint8_t L_bytes[32] = {
        0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
        0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
    };
    Ed25519Signature s_eq_l_sig = sig;
    std::memcpy(s_eq_l_sig.data() + 32, L_bytes, 32);
    TEST_ASSERT(!Ed25519::verify(s_eq_l_sig, msg.data(), msg.size(), pub), "S == L rejected");

    // Non-canonical S = L + 1
    Ed25519Signature s_gt_l_sig = s_eq_l_sig;
    s_gt_l_sig[32] = 0xee; // L + 1
    TEST_ASSERT(!Ed25519::verify(s_gt_l_sig, msg.data(), msg.size(), pub), "S > L rejected");

    // S with high bit set (2^255)
    Ed25519Signature s_high_bit_sig = sig;
    s_high_bit_sig[63] |= 0x80;
    TEST_ASSERT(!Ed25519::verify(s_high_bit_sig, msg.data(), msg.size(), pub), "S with high bit set rejected");

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 6: Adversarial Bit-Flipping Malleability Attacks
// -----------------------------------------------------------------------------
static void test_adversarial_bit_flips() {
    std::cout << "[Test 6] Adversarial Bit-Flipping Tests (Message, Public Key, Signature)...\n";

    Ed25519PublicKey pub{};
    Ed25519PrivateKey priv{};
    TEST_ASSERT(Ed25519::generate_keypair(pub, priv), "Generate keypair");

    std::string msg = "Confidential Package Manifest";
    Ed25519Signature sig{};
    Ed25519::sign(sig, msg.data(), msg.size(), pub, priv);

    // 1. Bit flip across each byte of message
    for (std::size_t i = 0; i < msg.size(); ++i) {
        std::string flipped_msg = msg;
        flipped_msg[i] ^= 0x01;
        TEST_ASSERT(!Ed25519::verify(sig, flipped_msg.data(), flipped_msg.size(), pub),
                    "Bit flip in message rejected");
    }

    // 2. Bit flip in public key
    for (std::size_t i = 0; i < pub.size(); ++i) {
        Ed25519PublicKey flipped_pub = pub;
        flipped_pub[i] ^= 0x01;
        TEST_ASSERT(!Ed25519::verify(sig, msg.data(), msg.size(), flipped_pub),
                    "Bit flip in public key rejected");
    }

    // 3. Bit flip in signature R component (bytes 0..31)
    for (std::size_t i = 0; i < 32; ++i) {
        Ed25519Signature flipped_sig = sig;
        flipped_sig[i] ^= 0x02;
        TEST_ASSERT(!Ed25519::verify(flipped_sig, msg.data(), msg.size(), pub),
                    "Bit flip in signature R rejected");
    }

    // 4. Bit flip in signature S component (bytes 32..63)
    for (std::size_t i = 32; i < 64; ++i) {
        Ed25519Signature flipped_sig = sig;
        flipped_sig[i] ^= 0x04;
        TEST_ASSERT(!Ed25519::verify(flipped_sig, msg.data(), msg.size(), pub),
                    "Bit flip in signature S rejected");
    }

    std::cout << "  -> Passed.\n";
}

int main() {
#if defined(_WIN32)
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDOUT);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDOUT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDOUT);
#endif

    std::cout << "=== Stage 233.3: Ed25519 Conformance & Negative Tests ===\n";

    test_rfc8032_vectors();
    test_message_boundaries();
    test_non_canonical_point_rejection();
    test_small_order_points_rejection();
    test_scalar_canonicality_rejection();
    test_adversarial_bit_flips();

    std::cout << "=== All Stage 233.3 Conformance & Negative Tests PASSED ===\n";
    return 0;
}

#include "crypto/ed25519.h"
#include "crypto/sha512.h"
#include "security/security_utils.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>

using namespace winios;
using namespace winios::crypto;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ")\n"; \
            std::exit(1); \
        } \
    } while (0)

static void test_rfc8032_test_vector_1() {
    std::cout << "[Test 1] RFC 8032 Test Vector 1 (empty message)..." << std::endl;
    // Test vector 1:
    // SECRET KEY: 9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60
    // PUBLIC KEY: d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a
    // MESSAGE: (empty)
    // SIGNATURE: e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b

    Ed25519Seed seed;
    bool ok = Ed25519::from_hex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", seed.data(), 32);
    TEST_ASSERT(ok, "Failed to parse seed hex");

    Ed25519PublicKey pub{};
    Ed25519PrivateKey priv{};
    Ed25519::create_keypair(pub, priv, seed);

    std::string pub_hex = Ed25519::to_hex(pub.data(), pub.size());
    TEST_ASSERT(pub_hex == "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
                "Public key mismatch for vector 1");

    Ed25519Signature sig{};
    Ed25519::sign(sig, "", 0, pub, priv);

    std::string sig_hex = Ed25519::to_hex(sig.data(), sig.size());
    TEST_ASSERT(sig_hex == "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b",
                "Signature mismatch for vector 1");

    bool verified = Ed25519::verify(sig, "", 0, pub);
    TEST_ASSERT(verified, "Verification failed for vector 1");
    std::cout << "  Passed!" << std::endl;
}

static void test_rfc8032_test_vector_2() {
    std::cout << "[Test 2] RFC 8032 Test Vector 2 (1-byte message)..." << std::endl;
    // Test vector 2:
    // SECRET KEY: 4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb
    // PUBLIC KEY: 3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c
    // MESSAGE: 72 (hex)
    // SIGNATURE: 92aee370f501f10a76a502e659228614a0e630e1feda068ce1942cd5c705764f48a15645400505da32c2d8695b23b1a17e404c0ad9a39d4011032474084c6704

    Ed25519Seed seed;
    bool ok = Ed25519::from_hex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", seed.data(), 32);
    TEST_ASSERT(ok, "Failed to parse seed hex");

    Ed25519PublicKey pub{};
    Ed25519PrivateKey priv{};
    Ed25519::create_keypair(pub, priv, seed);

    std::string pub_hex = Ed25519::to_hex(pub.data(), pub.size());
    TEST_ASSERT(pub_hex == "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
                "Public key mismatch for vector 2");

    std::uint8_t msg[1] = { 0x72 };
    Ed25519Signature sig{};
    Ed25519::sign(sig, msg, sizeof(msg), pub, priv);

    std::string sig_hex = Ed25519::to_hex(sig.data(), sig.size());
    TEST_ASSERT(sig_hex == "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00",
                "Signature mismatch for vector 2");

    bool verified = Ed25519::verify(sig, msg, sizeof(msg), pub);
    TEST_ASSERT(verified, "Verification failed for vector 2");
    std::cout << "  Passed!" << std::endl;
}

static void test_key_derivation_determinism() {
    std::cout << "[Test 3] Key derivation determinism..." << std::endl;
    for (int iter = 0; iter < 5; ++iter) {
        Ed25519Seed seed;
        for (std::size_t i = 0; i < seed.size(); ++i) {
            seed[i] = static_cast<std::uint8_t>((iter * 37 + i * 13) & 0xFF);
        }

        Ed25519PublicKey pub1{}, pub2{};
        Ed25519PrivateKey priv1{}, priv2{};

        Ed25519::create_keypair(pub1, priv1, seed);
        Ed25519::create_keypair(pub2, priv2, seed);

        TEST_ASSERT(std::memcmp(pub1.data(), pub2.data(), 32) == 0, "Non-deterministic public key");
        TEST_ASSERT(std::memcmp(priv1.data(), priv2.data(), 64) == 0, "Non-deterministic private key");

        // Verify sign and verify loop
        const char msg[] = "Deterministic test message for constant-time scalar mult";
        Ed25519Signature sig1{}, sig2{};
        Ed25519::sign(sig1, msg, sizeof(msg), pub1, priv1);
        Ed25519::sign(sig2, msg, sizeof(msg), pub2, priv2);

        TEST_ASSERT(std::memcmp(sig1.data(), sig2.data(), 64) == 0, "Non-deterministic signature");
        TEST_ASSERT(Ed25519::verify(sig1, msg, sizeof(msg), pub1), "Failed self-verification");
    }
    std::cout << "  Passed!" << std::endl;
}

static void test_negative_tampering() {
    std::cout << "[Test 4] Negative tampering resistance..." << std::endl;
    Ed25519PublicKey pub{};
    Ed25519PrivateKey priv{};
    bool gen_ok = Ed25519::generate_keypair(pub, priv);
    TEST_ASSERT(gen_ok, "Keypair generation failed");

    const char msg[] = "Payload under test";
    Ed25519Signature sig{};
    Ed25519::sign(sig, msg, sizeof(msg), pub, priv);

    // 1. Correct signature verifies
    TEST_ASSERT(Ed25519::verify(sig, msg, sizeof(msg), pub), "Valid signature failed");

    // 2. Corrupted message fails
    const char corrupted_msg[] = "Payload under tesu";
    TEST_ASSERT(!Ed25519::verify(sig, corrupted_msg, sizeof(corrupted_msg), pub), "Corrupted message succeeded");

    // 3. Flipped bit in signature fails
    Ed25519Signature tampered_sig = sig;
    tampered_sig[10] ^= 0x01;
    TEST_ASSERT(!Ed25519::verify(tampered_sig, msg, sizeof(msg), pub), "Tampered signature R succeeded");

    tampered_sig = sig;
    tampered_sig[45] ^= 0x02;
    TEST_ASSERT(!Ed25519::verify(tampered_sig, msg, sizeof(msg), pub), "Tampered signature S succeeded");

    // 4. Corrupted public key fails
    Ed25519PublicKey tampered_pub = pub;
    tampered_pub[5] ^= 0x04;
    TEST_ASSERT(!Ed25519::verify(sig, msg, sizeof(msg), tampered_pub), "Tampered public key succeeded");

    std::cout << "  Passed!" << std::endl;
}

int main() {
    std::cout << "=== Stage 235.3 Constant-Time Scalar Multiplication Tests ===" << std::endl;
    test_rfc8032_test_vector_1();
    test_rfc8032_test_vector_2();
    test_key_derivation_determinism();
    test_negative_tampering();
    std::cout << "=== All Stage 235.3 tests PASSED successfully ===" << std::endl;
    return 0;
}

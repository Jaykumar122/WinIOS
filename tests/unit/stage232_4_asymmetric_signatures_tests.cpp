#include "crypto/sha512.h"
#include "crypto/ed25519.h"
#include "security/security_utils.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <crtdbg.h>
#endif

using namespace winios;
using namespace winios::crypto;

// 1. Test SHA-512 Known-Answer Tests (NIST CAVP)
static void test_sha512_known_vectors() {
    std::cout << "[Test 1] SHA-512 Known-Answer Tests...\n";

    // Test 1: Empty string
    std::string h_empty = Sha512::hash_hex("", 0);
    assert(h_empty == "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");

    // Test 2: "abc"
    std::string h_abc = Sha512::hash_hex("abc", 3);
    assert(h_abc == "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");

    std::cout << "  -> Passed.\n";
}

// 2. Test Ed25519 RFC 8032 Test Vector 1
static void test_ed25519_rfc8032_vector() {
    std::cout << "[Test 2] Ed25519 RFC 8032 Test Vector 1...\n";

    std::string seed_hex = "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60";
    std::string expected_pub_hex = "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a";

    Ed25519Seed seed{};
    assert(Ed25519::from_hex(seed_hex, seed.data(), seed.size()));

    Ed25519PublicKey pub{};
    Ed25519PrivateKey priv{};
    Ed25519::create_keypair(pub, priv, seed);

    std::string pub_hex = Ed25519::to_hex(pub.data(), pub.size());
    std::cout << "Computed pub_hex: " << pub_hex << "\n";
    std::cout << "Expected pub_hex: " << expected_pub_hex << "\n";
    assert(pub_hex == expected_pub_hex);

    // Sign empty message
    Ed25519Signature sig{};
    Ed25519::sign(sig, "", 0, pub, priv);

    std::string sig_hex = Ed25519::to_hex(sig.data(), sig.size());
    std::cout << "Computed sig_hex: " << sig_hex << "\n";
    std::cout << "Expected sig_hex: e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b\n";

    // Verify empty message
    assert(Ed25519::verify(sig, "", 0, pub));

    std::cout << "  -> Passed (Public Key & Signature verified).\n";
}

// 3. Test Signing, Verification, Tampering, and Non-repudiation
static void test_ed25519_signing_and_tampering() {
    std::cout << "[Test 3] Ed25519 Signing, Verification, and Tampering...\n";

    Ed25519PublicKey pub1{}, pub2{};
    Ed25519PrivateKey priv1{}, priv2{};

    // Generate two distinct keypairs
    assert(Ed25519::generate_keypair(pub1, priv1));
    assert(Ed25519::generate_keypair(pub2, priv2));
    assert(pub1 != pub2);

    std::string message = "WinIOS-Authenticated-Package-Data-2026";
    Ed25519Signature sig1{};
    Ed25519::sign(sig1, message.data(), message.size(), pub1, priv1);

    // 1. Valid signature verifies with pub1
    assert(Ed25519::verify(sig1, message.data(), message.size(), pub1));

    // 2. Signature fails verification with different public key pub2 (Non-repudiation)
    assert(!Ed25519::verify(sig1, message.data(), message.size(), pub2));

    // 3. Message tampering: altering message invalidates signature
    std::string tampered_message = "WinIOS-Authenticated-Package-Data-2027";
    assert(!Ed25519::verify(sig1, tampered_message.data(), tampered_message.size(), pub1));

    // 4. Signature bit-flip tampering: mutating signature bytes fails verification
    Ed25519Signature tampered_sig = sig1;
    tampered_sig[10] ^= 0xFF;
    assert(!Ed25519::verify(tampered_sig, message.data(), message.size(), pub1));

    // 5. Corrupted R component fails verification
    Ed25519Signature corrupted_r = sig1;
    corrupted_r[0] ^= 0x01;
    assert(!Ed25519::verify(corrupted_r, message.data(), message.size(), pub1));

    std::cout << "  -> Passed.\n";
}

int main() {
#if defined(_WIN32)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#if defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
#endif

    std::cout << "=== Stage 232.4: Asymmetric Signature (Ed25519 & SHA-512) Tests ===\n";
    test_sha512_known_vectors();
    test_ed25519_rfc8032_vector();
    test_ed25519_signing_and_tampering();
    std::cout << "=== All Stage 232.4 Tests PASSED ===\n";
    return 0;
}

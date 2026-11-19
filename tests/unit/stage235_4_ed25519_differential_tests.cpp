#include "crypto/ed25519.h"
#include "crypto/sha512.h"
#include "security/security_utils.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>

using namespace winios;
using namespace winios::crypto;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ")\n"; \
            std::exit(1); \
        } \
    } while (0)

// RFC 8032 Section 7.1 Vector 3
static void test_rfc8032_vector_3() {
    std::cout << "[Test 1] RFC 8032 Vector 3 (2-byte message)...\n";
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
    TEST_ASSERT(pub_hex == expected_pub_hex, "Vector 3 public key matches");

    Ed25519Signature sig{};
    Ed25519::sign(sig, msg, sizeof(msg), pub, priv);

    std::string sig_hex = Ed25519::to_hex(sig.data(), sig.size());
    TEST_ASSERT(sig_hex == expected_sig_hex, "Vector 3 signature matches RFC 8032");

    TEST_ASSERT(Ed25519::verify(sig, msg, sizeof(msg), pub), "Vector 3 signature verifies");
    std::cout << "  Passed!\n";
}

// Differential cross-check with 4 Python cryptography reference vectors
static void test_differential_python_vectors() {
    std::cout << "[Test 2] Differential vectors from OpenSSL/cryptography...\n";
    struct PyVec {
        const char* seed;
        const char* pub;
        const char* msg_hex;
        std::size_t msg_len;
        const char* sig;
    };

    static const PyVec kVecs[] = {
        {
            "0dc825b7477f4db5199971ccdadcb86668203e918a665b86334ffeffb23b4bdd",
            "1b2e9677ee1f8edfa769f55623ff1171847612febbdb994efa88e55730e9682a",
            "",
            0,
            "0cb6780f79644e95f12993c79cf72711b41f819e82b310ec0009995f72b21cb186c969153207fd7198fbb6e3ffade9a829c2f48368cf82032deb093eefd0e70a"
        },
        {
            "f3e59358cc2912076ec14efde6fdcb8770be9d92afe68d500494bef31f7ca81e",
            "e36d6a221f34145be9eaf9342cad73ec68dbd85176c6df2987e2b9b2f3f8d741",
            "61",
            1,
            "fe7e550294b0a18d013f069b1b316708ec0b16c70444f92022a84dcba8e87804e4d71d891f9516b64c0a6eff3a086be58aa12d627e05a10f067ede3f9d8fb00c"
        },
        {
            "fbc4d3c908488d6e58f61e0a5bb11b859c6e156a4a0c2b342ffcd15d579be2b7",
            "0248903dbf5f3be8cdccacb4d97b69f4da6cb7607359398fbe755e7948cd78be",
            "48656c6c6f2c2057696e494f5320506c6174666f726d21",
            23,
            "3a3125b5868f78ecec15ab2579808b820408ed7e6c1c966341148c081c64b469d473084a76be6fe4f3572905ee6713602c3f023caba4212c20c066e7fabb820d"
        },
        {
            "61faec68df42175e21b3985137117db797fef7eb231b0bad86e6b97e597ad6e5",
            "08a893c47e410cb7ad7ed65a2e1df50afb8a4a137e799ea5c17f9b972d1e0301",
            "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
            32,
            "0a1ca94bd3aea530bd6a20567bca4614850f7262cc93c2e6bf238ae01f295e50bbcbd846963dfbb5d858ba1b9bc39c60395a3509907f955624e07d436cb7ca05"
        }
    };

    for (const auto& v : kVecs) {
        Ed25519Seed seed{};
        TEST_ASSERT(Ed25519::from_hex(v.seed, seed.data(), 32), "Parse seed");

        Ed25519PublicKey pub{};
        Ed25519PrivateKey priv{};
        Ed25519::create_keypair(pub, priv, seed);

        std::string pub_hex = Ed25519::to_hex(pub.data(), 32);
        TEST_ASSERT(pub_hex == v.pub, "Differential public key match");

        std::vector<std::uint8_t> raw_msg(v.msg_len);
        if (v.msg_len > 0) {
            TEST_ASSERT(Ed25519::from_hex(v.msg_hex, raw_msg.data(), v.msg_len), "Parse msg_hex");
        }

        Ed25519Signature sig{};
        Ed25519::sign(sig, raw_msg.data(), raw_msg.size(), pub, priv);

        std::string sig_hex = Ed25519::to_hex(sig.data(), 64);
        TEST_ASSERT(sig_hex == v.sig, "Differential signature match");

        TEST_ASSERT(Ed25519::verify(sig, raw_msg.data(), raw_msg.size(), pub), "Differential verify match");
    }
    std::cout << "  Passed!\n";
}

// Adversarial test: Small order points, degenerate points, non-canonical encodings
static void test_adversarial_rejections() {
    std::cout << "[Test 3] Adversarial points and non-canonical encodings...\n";

    Ed25519PublicKey pub{};
    Ed25519PrivateKey priv{};
    TEST_ASSERT(Ed25519::generate_keypair(pub, priv), "Generate keypair");

    const char msg[] = "Security check";
    Ed25519Signature sig{};
    Ed25519::sign(sig, msg, sizeof(msg), pub, priv);

    // 1. Identity public key (y = 1, x = 0)
    Ed25519PublicKey identity_pub{};
    identity_pub[0] = 1;
    TEST_ASSERT(!Ed25519::verify(sig, msg, sizeof(msg), identity_pub),
                "Identity public key must be rejected");

    // 2. Small-order point of order 2: (0, -1) -> y = p - 1 = 2^255 - 20
    Ed25519PublicKey order2_pub{};
    order2_pub[0] = 0xEC; // 256 - 20 = 236 = 0xEC
    for (int i = 1; i < 31; ++i) order2_pub[i] = 0xFF;
    order2_pub[31] = 0x7F;
    TEST_ASSERT(!Ed25519::verify(sig, msg, sizeof(msg), order2_pub),
                "Order 2 public key must be rejected or fail verification");

    // 3. Non-canonical y-coordinate: y = p (2^255 - 19)
    Ed25519PublicKey noncanon_pub{};
    noncanon_pub[0] = 0xED;
    for (int i = 1; i < 31; ++i) noncanon_pub[i] = 0xFF;
    noncanon_pub[31] = 0x7F; // bit 255 is 0, so y = 2^255 - 19 == p
    TEST_ASSERT(!Ed25519::verify(sig, msg, sizeof(msg), noncanon_pub),
                "Non-canonical y >= p must be rejected");

    // 4. Non-canonical S-values: S >= L must be rejected
    // L = 2^252 + 27742317777372353535851937790883648493
    // L bytes: ed d3 f5 5c 1a 63 12 58 d6 9c f7 a2 de f9 de 14 00...00 10
    Ed25519Signature noncanon_sig = sig;
    std::uint8_t l_bytes[32] = {
        0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
        0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
    };
    std::memcpy(noncanon_sig.data() + 32, l_bytes, 32);
    TEST_ASSERT(!Ed25519::verify(noncanon_sig, msg, sizeof(msg), pub),
                "Signature with S == L must be rejected");

    // S == 0 must be rejected
    std::memset(noncanon_sig.data() + 32, 0, 32);
    TEST_ASSERT(!Ed25519::verify(noncanon_sig, msg, sizeof(msg), pub),
                "Signature with S == 0 must be rejected");

    std::cout << "  Passed!\n";
}

// Stress test: 64 KB large payload signing and verification
static void test_large_message() {
    std::cout << "[Test 4] 64 KB large payload signing & verification...\n";
    std::vector<std::uint8_t> large_msg(64 * 1024);
    for (std::size_t i = 0; i < large_msg.size(); ++i) {
        large_msg[i] = static_cast<std::uint8_t>((i * 101 + 17) & 0xFF);
    }

    Ed25519PublicKey pub{};
    Ed25519PrivateKey priv{};
    TEST_ASSERT(Ed25519::generate_keypair(pub, priv), "Generate keypair");

    Ed25519Signature sig{};
    Ed25519::sign(sig, large_msg.data(), large_msg.size(), pub, priv);

    TEST_ASSERT(Ed25519::verify(sig, large_msg.data(), large_msg.size(), pub),
                "64 KB message failed verification");

    // Mutate byte in large message
    large_msg[32768] ^= 0x01;
    TEST_ASSERT(!Ed25519::verify(sig, large_msg.data(), large_msg.size(), pub),
                "Mutated large message must fail verification");
    std::cout << "  Passed!\n";
}

int main() {
    std::cout << "=== Stage 235.4 Ed25519 Differential & Adversarial Tests ===" << std::endl;
    test_rfc8032_vector_3();
    test_differential_python_vectors();
    test_adversarial_rejections();
    test_large_message();
    std::cout << "=== All Stage 235.4 tests PASSED successfully ===" << std::endl;
    return 0;
}

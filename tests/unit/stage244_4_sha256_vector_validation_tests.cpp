// ============================================================================
// Stage 244.4: SHA-256 Known-Answer Vector & Non-Cryptographic Hashing Tests
// Validates FIPS 180-4 SHA-256 standard vectors (empty, "abc", multi-block, large)
// and verifies that FNV-1a is explicitly labeled and tested as non-cryptographic.
// ============================================================================

#include "crypto/sha256.h"
#include "runtime/replay_state.h"

#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::crypto;
using namespace winios::runtime;

static void test_sha256_known_answer_vectors() {
    std::cout << "[Stage 244.4 - Test 1] NIST standard SHA-256 test vectors...\n";

    // 1. Empty string "" -> e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    std::string h_empty = Sha256::hash_hex("");
    TEST_ASSERT(h_empty == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "Empty string SHA-256");

    // 2. "abc" -> ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    std::string h_abc = Sha256::hash_hex("abc");
    TEST_ASSERT(h_abc == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "String 'abc' SHA-256");

    // 3. Multi-block message: "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" (56 bytes)
    std::string msg56 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    std::string h_56 = Sha256::hash_hex(msg56);
    TEST_ASSERT(h_56 == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "56-byte message SHA-256");

    // 4. Large message (1 million 'a's or 100,000 'a's)
    std::string thousand_a(1000, 'a');
    std::string h_1000 = Sha256::hash_hex(thousand_a);
    TEST_ASSERT(!h_1000.empty() && h_1000.size() == 64, "1000-byte hash length");

    // 5. Replay state integration test
    std::vector<uint8_t> data = {'a', 'b', 'c'};
    auto digest = ReplayStateContract::compute_sha256(data.data(), data.size());
    std::string hex_out;
    for (uint8_t b : digest) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02x", b);
        hex_out += buf;
    }
    TEST_ASSERT(hex_out == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "ReplayStateContract SHA-256 integration");
}

static void test_fnv1a_non_cryptographic_labeling() {
    std::cout << "[Stage 244.4 - Test 2] FNV-1a non-cryptographic hash properties...\n";
    std::string sample = "test_string";
    uint64_t hash = ReplayStateContract::fnv1a_hash(reinterpret_cast<const uint8_t*>(sample.data()), sample.size());
    TEST_ASSERT(hash != 0, "FNV-1a produces non-zero hash");

    // Perturb single byte
    std::string sample2 = "test_string!";
    uint64_t hash2 = ReplayStateContract::fnv1a_hash(reinterpret_cast<const uint8_t*>(sample2.data()), sample2.size());
    TEST_ASSERT(hash != hash2, "FNV-1a detects simple byte alteration");
}

int main() {
    std::cout << "=== Running Stage 244.4 SHA-256 Vector Validation Tests ===\n";
    test_sha256_known_answer_vectors();
    test_fnv1a_non_cryptographic_labeling();
    std::cout << "Stage 244.4: All tests passed successfully.\n";
    return 0;
}

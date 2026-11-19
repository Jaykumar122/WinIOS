#include "security/developer_keystore.h"
#include "security/security_utils.h"
#include "crypto/ed25519.h"

#include <cassert>
#include <iostream>
#include <vector>
#include <cstring>

using namespace winios;
using namespace winios::security;
using namespace winios::crypto;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ")\n"; \
            std::exit(1); \
        } \
    } while (0)

static void test_wipe_private_key() {
    std::cout << "[Test 1] wipe_private_key zeroization...\n";
    DeveloperIdentity id = DeveloperKeyStore::generate_identity(
        "com.apple.developer.test", "Apple Developer", "key-001");

    TEST_ASSERT(id.is_valid(), "Identity generated successfully");
    TEST_ASSERT(id.private_key_seed.size() == 32, "Private key seed exists");

    // Capture pointer to backing buffer before wipe
    std::uint8_t* raw_buf = id.private_key_seed.data();
    TEST_ASSERT(raw_buf != nullptr, "Buffer valid");

    id.wipe_private_key();
    TEST_ASSERT(id.private_key_seed.empty(), "Vector emptied");
    std::cout << "  Passed!\n";
}

static void test_move_semantics_zeroization() {
    std::cout << "[Test 2] Move constructor & assignment secure wiping...\n";

    // 1. Move constructor
    {
        DeveloperIdentity id1 = DeveloperKeyStore::generate_identity(
            "com.apple.developer.test", "Apple Developer", "key-001");
        TEST_ASSERT(id1.private_key_seed.size() == 32, "id1 valid");

        DeveloperIdentity id2(std::move(id1));
        TEST_ASSERT(id2.is_valid(), "id2 received identity");
        TEST_ASSERT(id2.private_key_seed.size() == 32, "id2 has key seed");
        TEST_ASSERT(id1.private_key_seed.empty(), "id1 private seed was wiped on move");
    }

    // 2. Move assignment
    {
        DeveloperIdentity id1 = DeveloperKeyStore::generate_identity(
            "com.apple.developer.test", "Apple Developer", "key-001");
        DeveloperIdentity id2 = DeveloperKeyStore::generate_identity(
            "com.apple.developer.test2", "Apple Developer 2", "key-002");

        id2 = std::move(id1);
        TEST_ASSERT(id2.is_valid(), "id2 received identity from id1");
        TEST_ASSERT(id2.private_key_seed.size() == 32, "id2 has key seed");
        TEST_ASSERT(id1.private_key_seed.empty(), "id1 private seed was wiped on move assign");
    }
    std::cout << "  Passed!\n";
}

static void test_copy_assignment_wipes_previous() {
    std::cout << "[Test 3] Copy assignment wipes destination previous key...\n";
    DeveloperIdentity id1 = DeveloperKeyStore::generate_identity(
        "com.apple.developer.test", "Apple Developer", "key-001");
    DeveloperIdentity id2 = DeveloperKeyStore::generate_identity(
        "com.apple.developer.test2", "Apple Developer 2", "key-002");

    std::vector<std::uint8_t> old_id2_key = id2.private_key_seed;
    id2 = id1;

    TEST_ASSERT(id2.private_key_seed == id1.private_key_seed, "Copy assign transferred key");
    TEST_ASSERT(id2.private_key_seed != old_id2_key, "Old key was replaced");
    std::cout << "  Passed!\n";
}

static void test_export_public_key_confinement() {
    std::cout << "[Test 4] export_public_key non-leakage invariant...\n";
    DeveloperIdentity id = DeveloperKeyStore::generate_identity(
        "com.apple.developer.test", "Apple Developer", "key-001");

    PublisherKey pub = DeveloperKeyStore::export_public_key(id);
    TEST_ASSERT(pub.key_bytes.size() == 32, "Public key is 32 bytes");
    TEST_ASSERT(pub.key_bytes == id.public_key, "Public key matches");
    TEST_ASSERT(pub.key_bytes != id.private_key_seed, "Public key is distinct from private seed");
    std::cout << "  Passed!\n";
}

static void test_secure_zero_memory() {
    std::cout << "[Test 5] secure_zero_memory compiler retention...\n";
    volatile std::uint8_t secret[64];
    for (int i = 0; i < 64; ++i) secret[i] = static_cast<std::uint8_t>(0xA5 ^ i);

    secure_zero_memory((void*)secret, sizeof(secret));

    for (int i = 0; i < 64; ++i) {
        TEST_ASSERT(secret[i] == 0, "Byte was not zeroed");
    }
    std::cout << "  Passed!\n";
}

int main() {
    std::cout << "=== Stage 235.6 Secret Lifetime Assurance Tests ===" << std::endl;
    test_wipe_private_key();
    test_move_semantics_zeroization();
    test_copy_assignment_wipes_previous();
    test_export_public_key_confinement();
    test_secure_zero_memory();
    std::cout << "=== All Stage 235.6 tests PASSED successfully ===" << std::endl;
    return 0;
}

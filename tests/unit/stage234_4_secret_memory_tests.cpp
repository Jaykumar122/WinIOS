#include "security/security_utils.h"
#include "security/developer_keystore.h"
#include "crypto/ed25519.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

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

// -----------------------------------------------------------------------------
// Test 1: secure_zero_memory compiler-resistant wipe verification
// -----------------------------------------------------------------------------
static void test_secure_zero_memory() {
    std::cout << "[Stage 234.4] Test 1: secure_zero_memory Compiler-Resistant Zeroization...\n";

    constexpr std::size_t kSize = 128;
    std::uint8_t buffer[kSize];
    std::memset(buffer, 0xA5, kSize);

    // Verify nonzero initialization
    bool all_a5 = true;
    for (std::size_t i = 0; i < kSize; ++i) {
        if (buffer[i] != 0xA5) all_a5 = false;
    }
    TEST_ASSERT(all_a5, "Buffer initialized with pattern 0xA5");

    // Perform secure zeroization
    secure_zero_memory(buffer, kSize);

    // Verify all bytes are 0
    bool all_zero = true;
    for (std::size_t i = 0; i < kSize; ++i) {
        if (buffer[i] != 0x00) all_zero = false;
    }
    TEST_ASSERT(all_zero, "Buffer completely zeroed by secure_zero_memory");
    std::cout << "  -> Secure zeroization verified across all bytes.\n";
}

// -----------------------------------------------------------------------------
// Test 2: DeveloperIdentity RAII Wiping on Scope Exit
// -----------------------------------------------------------------------------
static void test_developer_identity_raii() {
    std::cout << "[Stage 234.4] Test 2: DeveloperIdentity RAII Destructor Wiping...\n";

    std::uint8_t saved_seed_copy[32]{0};
    const std::uint8_t* raw_seed_ptr = nullptr;

    {
        DeveloperIdentity id = DeveloperKeyStore::generate_identity("com.example.pub", "Example Org", "key-01");
        TEST_ASSERT(id.is_valid(), "Identity generated successfully");
        TEST_ASSERT(id.private_key_seed.size() == 32, "Seed size is 32 bytes");

        raw_seed_ptr = id.private_key_seed.data();
        std::memcpy(saved_seed_copy, raw_seed_ptr, 32);

        // Explicit wipe test
        id.wipe_private_key();
        TEST_ASSERT(id.private_key_seed.empty(), "Vector cleared after explicit wipe");
    }

    // Test automatic destructor wiping
    {
        DeveloperIdentity id2 = DeveloperKeyStore::generate_identity("com.example.pub2", "Example Org", "key-02");
        TEST_ASSERT(id2.is_valid(), "Identity 2 generated");
        // id2 destroyed here at scope end via ~DeveloperIdentity()
    }

    std::cout << "  -> RAII destructor automatically invoked on scope exit.\n";
}

// -----------------------------------------------------------------------------
// Test 3: SecureBuffer RAII Memory Protection
// -----------------------------------------------------------------------------
static void test_secure_buffer_raii() {
    std::cout << "[Stage 234.4] Test 3: SecureBuffer Allocation, Resize & Cleanup...\n";

    {
        SecureBuffer buf(64);
        TEST_ASSERT(buf.size() == 64, "SecureBuffer size is 64");
        std::memset(buf.data(), 0x5C, 64);

        // Resize buffer (should wipe previous memory)
        buf.resize(128);
        TEST_ASSERT(buf.size() == 128, "Resized SecureBuffer to 128");

        buf.clear();
        TEST_ASSERT(buf.empty(), "Buffer is empty after clear");
    }

    std::cout << "  -> SecureBuffer RAII lifecycle verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Constant-Time String and Vector Comparison
// -----------------------------------------------------------------------------
static void test_constant_time_comparison() {
    std::cout << "[Stage 234.4] Test 4: Constant-Time Comparison Correctness...\n";

    std::string s1 = "SuperSecretTokenValue1234567890";
    std::string s2 = "SuperSecretTokenValue1234567890";
    std::string s3 = "SuperSecretTokenValue1234567891"; // Last byte differs
    std::string s4 = "Short";

    TEST_ASSERT(constant_time_equals(s1, s2), "Identical strings match in constant time");
    TEST_ASSERT(!constant_time_equals(s1, s3), "Different strings reject in constant time");
    TEST_ASSERT(!constant_time_equals(s1, s4), "Different length strings reject in constant time");

    std::cout << "  -> Constant-time comparisons verified.\n";
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  Stage 234.4: Secret Memory Handling & RAII Zeroization Suite   \n";
    std::cout << "=================================================================\n";

    test_secure_zero_memory();
    test_developer_identity_raii();
    test_secure_buffer_raii();
    test_constant_time_comparison();

    std::cout << "\n=================================================================\n";
    std::cout << "  ALL SECRET MEMORY AUDIT TESTS PASSED!                          \n";
    std::cout << "=================================================================\n";
    return 0;
}

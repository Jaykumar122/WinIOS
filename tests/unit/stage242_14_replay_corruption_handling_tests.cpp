// ============================================================================
// Stage 242.14: Replay Artifact Corruption, Bounds Checking & Safe Rejection Tests
// Adversarially exercises truncated replay payloads, corrupted magic, out-of-bounds
// counts, integer overflows, and ensures corrupted input safely fails without crash.
// ============================================================================

#include "runtime/replay_state.h"

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::runtime;

static void test_corrupted_magic_and_version() {
    std::cout << "[Stage 242.14 - Test 1] Corrupted magic and unsupported version rejection...\n";
    ReplayStateContract valid;
    auto bytes = valid.serialize();

    // Corrupt magic bytes
    auto bad_magic = bytes;
    bad_magic[0] ^= 0xFF;
    auto res1 = ReplayStateContract::deserialize(bad_magic.data(), bad_magic.size());
    TEST_ASSERT(!res1.has_value(), "Corrupted magic must be rejected");

    // Corrupt version bytes (at offset 8)
    auto bad_version = bytes;
    bad_version[8] = 99; // Version 99
    auto res2 = ReplayStateContract::deserialize(bad_version.data(), bad_version.size());
    TEST_ASSERT(!res2.has_value(), "Unsupported version must be rejected");
}

static void test_truncated_artifacts() {
    std::cout << "[Stage 242.14 - Test 2] Truncated replay artifact rejection...\n";
    ReplayStateContract valid;
    valid.memory.page_hashes.push_back({0x1000, 0x1234, 1});
    valid.syscalls.push_back({1, 2, 3, 4, 5, 0});
    auto bytes = valid.serialize();

    // Truncate at various arbitrary lengths
    std::vector<size_t> trunc_lengths = {0, 4, 10, 25, bytes.size() / 2, bytes.size() - 1};
    for (size_t len : trunc_lengths) {
        auto res = ReplayStateContract::deserialize(bytes.data(), len);
        TEST_ASSERT(!res.has_value(), "Truncated buffer must safely fail deserialization");
    }
}

static void test_hostile_alloc_overflow() {
    std::cout << "[Stage 242.14 - Test 3] Out-of-bounds page count and integer overflow bounds...\n";
    ReplayStateContract valid;
    auto bytes = valid.serialize();

    // The page count is located after CPU state and memory header
    // CPU: 31*8 + 8 + 8 + 1 + 8 + 4 = 277 bytes
    // Memory header: 8 + 2 + 8 = 18 bytes
    // Total offset to page count = 8(magic) + 4(ver) + 277 + 18 = 307
    // Let's find and modify the page count to 0xFFFFFFFF
    auto hostile_bytes = bytes;
    for (size_t i = 100; i + 4 <= hostile_bytes.size(); ++i) {
        // If we inject a massive 0x7FFFFFFF count into any 4-byte field
        auto modified = hostile_bytes;
        uint32_t huge_count = 0x7FFFFFFF;
        std::memcpy(modified.data() + i, &huge_count, 4);
        auto res = ReplayStateContract::deserialize(modified.data(), modified.size());
        // Must either fail cleanly or return nullopt, never crash or allocate gigabytes
        (void)res;
    }
    // Also test maximum payload limit
    std::vector<uint8_t> huge_buffer(kMaxReplayPayloadBytes + 1, 0);
    auto res_overflow = ReplayStateContract::deserialize(huge_buffer.data(), huge_buffer.size());
    TEST_ASSERT(!res_overflow.has_value(), "Buffer exceeding max payload bytes must be rejected");
}

int main() {
    std::cout << "=== Running Stage 242.14 Replay Corruption Handling Tests ===\n";
    test_corrupted_magic_and_version();
    test_truncated_artifacts();
    test_hostile_alloc_overflow();
    std::cout << "Stage 242.14: All tests passed successfully.\n";
    return 0;
}

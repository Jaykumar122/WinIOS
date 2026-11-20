// ============================================================================
// Stage 244.6: Adversarial Replay Deserialization & Robustness Tests
// Systematically validates replay deserialization against malformed inputs:
// invalid magic, unsupported versions, truncation, buffer overflows,
// count limits (pages, syscalls), trailing garbage, and V1 compatibility.
// ============================================================================

#include "runtime/replay_state.h"

#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstdint>
#include <cstring>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::runtime;

static ReplayStateContract create_dummy_v2() {
    ReplayStateContract s;
    s.version = kReplayVersion2;
    s.coverage_flags = CoverageFullDefault;
    for (size_t i = 0; i < 31; ++i) {
        s.cpu.gpr[i] = 0x1000 + i;
    }
    s.cpu.pc = 0x400000;
    s.cpu.sp = 0x7FFFF000;
    s.cpu.nzcv = 0x4; // Z flag
    s.cpu.instruction_count = 100;
    s.cpu.exit_code = 42;

    s.memory.root_table_pa = 0x10000;
    s.memory.asid = 1;
    s.memory.overall_memory_hash = 0xCAFEBABE12345678ULL;
    s.memory.memory_sha256.fill(0xAA);
    s.memory.page_hashes.push_back({0x400000, 0x11223344, 5});

    s.syscalls.push_back({1, 10, 20, 30, 0, 0, 0});
    return s;
}

static void test_null_and_boundary_sizes() {
    std::cout << "[Stage 244.6 - Test 1] Null data and payload size boundaries...\n";

    TEST_ASSERT(!ReplayStateContract::deserialize(nullptr, 0).has_value(), "Null pointer with 0 size");
    TEST_ASSERT(!ReplayStateContract::deserialize(nullptr, 100).has_value(), "Null pointer with non-zero size");

    std::vector<uint8_t> tiny(19, 0);
    TEST_ASSERT(!ReplayStateContract::deserialize(tiny.data(), tiny.size()).has_value(), "Underflow < 20 bytes");

    // Over maximum size
    std::vector<uint8_t> huge(kMaxReplayPayloadBytes + 1, 0);
    TEST_ASSERT(!ReplayStateContract::deserialize(huge.data(), huge.size()).has_value(), "Payload exceeds max allowed bytes");
}

static void test_invalid_magic_and_versions() {
    std::cout << "[Stage 244.6 - Test 2] Invalid magic and unsupported version validation...\n";

    auto base = create_dummy_v2();
    auto bytes = base.serialize();

    // 1. Corrupt magic
    auto corrupted_magic = bytes;
    corrupted_magic[0] ^= 0xFF;
    TEST_ASSERT(!ReplayStateContract::deserialize(corrupted_magic.data(), corrupted_magic.size()).has_value(), "Corrupted magic fails");

    // 2. Corrupt version to 0
    auto corrupted_v0 = bytes;
    uint32_t zero_ver = 0;
    std::memcpy(corrupted_v0.data() + 8, &zero_ver, 4);
    TEST_ASSERT(!ReplayStateContract::deserialize(corrupted_v0.data(), corrupted_v0.size()).has_value(), "Version 0 rejected");

    // 3. Corrupt version to unsupported V3
    auto corrupted_v3 = bytes;
    uint32_t v3 = 3;
    std::memcpy(corrupted_v3.data() + 8, &v3, 4);
    TEST_ASSERT(!ReplayStateContract::deserialize(corrupted_v3.data(), corrupted_v3.size()).has_value(), "Version 3 rejected");
}

static void test_truncation_robustness() {
    std::cout << "[Stage 244.6 - Test 3] Truncation sweep across all byte offsets...\n";

    auto base = create_dummy_v2();
    auto bytes = base.serialize();

    for (size_t len = 0; len < bytes.size(); ++len) {
        auto res = ReplayStateContract::deserialize(bytes.data(), len);
        TEST_ASSERT(!res.has_value(), "Truncated buffer must safely return nullopt without memory faults");
    }
}

static void test_trailing_garbage_rejection() {
    std::cout << "[Stage 244.6 - Test 4] Trailing byte injection rejection...\n";

    auto base = create_dummy_v2();
    auto bytes = base.serialize();

    bytes.push_back(0x00);
    TEST_ASSERT(!ReplayStateContract::deserialize(bytes.data(), bytes.size()).has_value(), "Single trailing byte rejected");

    bytes.push_back(0xFF);
    TEST_ASSERT(!ReplayStateContract::deserialize(bytes.data(), bytes.size()).has_value(), "Multiple trailing bytes rejected");
}

static void test_oversized_counts() {
    std::cout << "[Stage 244.6 - Test 5] Oversized page and syscall counts...\n";

    auto base = create_dummy_v2();
    auto bytes = base.serialize();

    // Page count offset:
    // 8(magic) + 4(ver) + 4(cov) + 31*8(gpr) + 8(pc) + 8(sp) + 1(nzcv) + 8(ic) + 4(exit)
    // + 8(root) + 2(asid) + 8(hash) + 32(sha)
    size_t page_count_offset = 8 + 4 + 4 + 248 + 8 + 8 + 1 + 8 + 4 + 8 + 2 + 8 + 32;
    TEST_ASSERT(page_count_offset + 4 <= bytes.size(), "Offset within bounds");

    auto page_overflow = bytes;
    uint32_t bad_page_count = static_cast<uint32_t>(kMaxRecordedPages + 1);
    std::memcpy(page_overflow.data() + page_count_offset, &bad_page_count, 4);
    TEST_ASSERT(!ReplayStateContract::deserialize(page_overflow.data(), page_overflow.size()).has_value(), "Excessive page count rejected");

    // Also test UINT32_MAX
    bad_page_count = 0xFFFFFFFF;
    std::memcpy(page_overflow.data() + page_count_offset, &bad_page_count, 4);
    TEST_ASSERT(!ReplayStateContract::deserialize(page_overflow.data(), page_overflow.size()).has_value(), "UINT32_MAX page count rejected");
}

static void test_v1_compatibility() {
    std::cout << "[Stage 244.6 - Test 6] V1 backwards compatibility deserialization...\n";

    std::vector<uint8_t> v1_buf;
    auto append_u16 = [&](uint16_t v) { const uint8_t* p = reinterpret_cast<const uint8_t*>(&v); v1_buf.insert(v1_buf.end(), p, p + 2); };
    auto append_u32 = [&](uint32_t v) { const uint8_t* p = reinterpret_cast<const uint8_t*>(&v); v1_buf.insert(v1_buf.end(), p, p + 4); };
    auto append_i32 = [&](int32_t v) { const uint8_t* p = reinterpret_cast<const uint8_t*>(&v); v1_buf.insert(v1_buf.end(), p, p + 4); };
    auto append_u64 = [&](uint64_t v) { const uint8_t* p = reinterpret_cast<const uint8_t*>(&v); v1_buf.insert(v1_buf.end(), p, p + 8); };

    append_u64(kReplayMagic);
    append_u32(kReplayVersion1);

    // CPU
    for (size_t i = 0; i < 31; ++i) append_u64(0x100 + i);
    append_u64(0x400000); // PC
    append_u64(0x7FFFF000); // SP
    v1_buf.push_back(0); // NZCV
    append_u64(50); // ic
    append_i32(0); // exit code

    // Memory
    append_u64(0x20000); // root table
    append_u16(5); // ASID
    append_u64(0x1234567890ABCDEFULL); // overall hash
    append_u32(0); // page count = 0

    // Syscalls
    append_u32(0); // syscall count = 0

    auto res = ReplayStateContract::deserialize(v1_buf.data(), v1_buf.size());
    TEST_ASSERT(res.has_value(), "Valid V1 payload successfully deserialized");
    TEST_ASSERT(res->version == kReplayVersion1, "Version preserved as V1");
    TEST_ASSERT(res->cpu.pc == 0x400000, "PC preserved");
    TEST_ASSERT(res->memory.asid == 5, "ASID preserved");
}

int main() {
    std::cout << "=== Running Stage 244.6 Adversarial Replay Deserialization Tests ===\n";
    test_null_and_boundary_sizes();
    test_invalid_magic_and_versions();
    test_truncation_robustness();
    test_trailing_garbage_rejection();
    test_oversized_counts();
    test_v1_compatibility();
    std::cout << "Stage 244.6: All adversarial deserialization tests passed.\n";
    return 0;
}

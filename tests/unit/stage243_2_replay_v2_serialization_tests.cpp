// ============================================================================
// Stage 243.2: Replay V2 Serialization, Backward Compatibility & Diagnostics Tests
// Tests Replay V2 format, coverage flags, 256-bit memory digests, V1 artifact
// backward compatibility, bounds enforcement, and non-mutating diagnostics.
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

static void test_replay_v2_serialization_and_digest() {
    std::cout << "[Stage 243.2 - Test 1] Replay V2 format with 256-bit digest and coverage flags...\n";
    ReplayStateContract orig;
    orig.version = kReplayVersion2;
    orig.coverage_flags = CoverageFullDefault;
    orig.cpu.pc = 0x10000;
    orig.cpu.sp = 0x7FFF0000;
    orig.cpu.gpr[0] = 42;
    orig.cpu.instruction_count = 1500;
    orig.memory.root_table_pa = 0x200000;
    orig.memory.asid = 5;
    orig.memory.overall_memory_hash = 0x123456789ABCDEF0ULL;

    std::vector<uint8_t> sample_bytes = {0x11, 0x22, 0x33, 0x44, 0x55};
    orig.memory.memory_sha256 = ReplayStateContract::compute_digest256(sample_bytes.data(), sample_bytes.size());
    orig.memory.page_hashes.push_back({0x10000, 0xAABBCCDD, 0x7});
    orig.syscalls.push_back({4, 1, 0x10000, 10, 10, 0, 0});

    auto bytes = orig.serialize();
    TEST_ASSERT(!bytes.empty(), "V2 Serialized bytes");

    auto restored_opt = ReplayStateContract::deserialize(bytes.data(), bytes.size());
    TEST_ASSERT(restored_opt.has_value(), "V2 Deserialization succeeds");
    TEST_ASSERT(restored_opt->version == kReplayVersion2, "Version matches 2");
    TEST_ASSERT(restored_opt->coverage_flags == CoverageFullDefault, "Coverage flags preserved");
    TEST_ASSERT(restored_opt->memory.memory_sha256 == orig.memory.memory_sha256, "256-bit digest matches");

    auto div = ReplayStateContract::compare(orig, *restored_opt);
    TEST_ASSERT(!div.has_diverged, "V2 roundtrip matches completely");
}

static void test_v1_backward_compatibility() {
    std::cout << "[Stage 243.2 - Test 2] V1 artifact backward compatibility...\n";
    // Construct a raw V1 serialized payload
    std::vector<uint8_t> v1_buf;
    auto append_u64 = [&](uint64_t v) {
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        v1_buf.insert(v1_buf.end(), p, p + 8);
    };
    auto append_u32 = [&](uint32_t v) {
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        v1_buf.insert(v1_buf.end(), p, p + 4);
    };
    auto append_u16 = [&](uint16_t v) {
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        v1_buf.insert(v1_buf.end(), p, p + 2);
    };

    append_u64(kReplayMagic);
    append_u32(kReplayVersion1); // Version 1

    // CPU (31 GPRs, PC, SP, NZCV, instructions, exit)
    for (size_t i = 0; i < 31; ++i) append_u64(i * 10);
    append_u64(0x8000); // PC
    append_u64(0x7FFF0000); // SP
    v1_buf.push_back(0); // NZCV
    append_u64(500); // instructions
    append_u32(0); // exit code

    // Memory (root_table_pa, asid, overall_hash, page_count)
    append_u64(0x100000);
    append_u16(10);
    append_u64(0x9999888877776666ULL);
    append_u32(0); // 0 page hashes

    // Syscalls (count = 0)
    append_u32(0);

    auto restored_opt = ReplayStateContract::deserialize(v1_buf.data(), v1_buf.size());
    TEST_ASSERT(restored_opt.has_value(), "V1 artifact deserializes successfully under V2 loader");
    TEST_ASSERT(restored_opt->version == kReplayVersion1, "Preserved as version 1");
    TEST_ASSERT(restored_opt->cpu.pc == 0x8000, "PC preserved from V1");
}

static void test_bounds_and_bounded_diagnostics() {
    std::cout << "[Stage 243.2 - Test 3] Bounds enforcement and divergence diagnostic safety...\n";
    ReplayStateContract a;
    a.cpu.instruction_count = 100;
    ReplayStateContract b;
    b.cpu.instruction_count = 200;

    auto div = ReplayStateContract::compare(a, b);
    TEST_ASSERT(div.has_diverged, "Must detect divergence in instruction count");
    TEST_ASSERT(!div.field_name.empty(), "Field name present");
    TEST_ASSERT(div.details.size() < 512, "Diagnostic details must be bounded");
}

int main() {
    std::cout << "=== Running Stage 243.2 Replay V2 Serialization Tests ===\n";
    test_replay_v2_serialization_and_digest();
    test_v1_backward_compatibility();
    test_bounds_and_bounded_diagnostics();
    std::cout << "Stage 243.2: All tests passed successfully.\n";
    return 0;
}

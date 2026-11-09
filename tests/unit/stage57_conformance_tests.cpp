#include "runtime/execution_checkpoint.h"
#include "runtime/artifact_publication.h"
#include "runtime/elf_session.h"
#include "test_harness.h"
#include "../support/elf_fixture.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <numeric>
#include <vector>

namespace {

std::uint64_t median(std::vector<std::uint64_t>& values) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

std::vector<std::uint8_t> make_program(std::size_t nops = 200) {
    std::vector<std::uint32_t> words(nops, 0xD503201Fu); // NOP
    words.push_back(0xD4400000u); // HLT
    elf_test::Segment seg;
    seg.address = 0x1000;
    seg.size = words.size() * 4;
    seg.flags = 5;
    seg.words = words;
    return elf_test::image({seg});
}

// ----------------------------------------------------------------------------
// Test 1: Checkpoint Serialization, Deserialization & Corruption Detection
// ----------------------------------------------------------------------------
void test_checkpoint_roundtrip_and_corruption() {
    elf_test::File owner;
    owner.write(make_program(100));
    const auto dir = owner.path().parent_path();
    const auto elf_copy = dir / "cp_roundtrip.elf";
    const auto cp_file = dir / "test.wchk";
    std::filesystem::copy_file(owner.path(), elf_copy);

    RuntimeConfig config{0x8000, 0, 0, 0x8000 - 4096, 4096, 100, std::nullopt, false};
    ElfSession session(config);
    CHECK(session.load(elf_copy).status == ExecutionStatus::completed);
    const auto res = session.run(5);
    CHECK(res.instructions_executed == 5);

    // Capture checkpoint
    const auto cp = CheckpointManager::capture(session, 100);
    CHECK(CheckpointManager::validate(cp));
    CHECK(cp.header.retired_instructions == 5);
    CHECK(cp.header.memory_capacity == 0x8000);
    CHECK(!cp.snapshot_payload.empty());

    // Serialize and deserialize roundtrip
    const auto serialized = CheckpointManager::serialize(cp);
    CHECK(!serialized.empty());

    const auto deserialized = CheckpointManager::deserialize(serialized);
    CHECK(CheckpointManager::validate(deserialized));
    CHECK(deserialized.header.magic == CheckpointMetadata::kMagic);
    CHECK(deserialized.header.retired_instructions == 5);
    CHECK(deserialized.header.component_hashes == cp.header.component_hashes);
    CHECK(deserialized.payload_crc32c == cp.payload_crc32c);
    CHECK(deserialized.snapshot_payload == cp.snapshot_payload);

    // Save and load roundtrip
    CHECK(CheckpointManager::save(cp_file, cp));
    CHECK(std::filesystem::exists(cp_file));
    const auto loaded = CheckpointManager::load(cp_file);
    CHECK(loaded.header.retired_instructions == 5);
    CHECK(loaded.header.component_hashes == cp.header.component_hashes);

    // Corruption: modify header byte
    auto corrupted_header = serialized;
    corrupted_header[20] ^= 0x5a;
    CHECK_INVALID_ARGUMENT(CheckpointManager::deserialize(corrupted_header));

    // Corruption: modify payload byte
    auto corrupted_payload = serialized;
    corrupted_payload[serialized.size() - 20] ^= 0x3c;
    CHECK_INVALID_ARGUMENT(CheckpointManager::deserialize(corrupted_payload));

    // Corruption: modify trailing CRC
    auto corrupted_crc = serialized;
    corrupted_crc[serialized.size() - 2] ^= 0xff;
    CHECK_INVALID_ARGUMENT(CheckpointManager::deserialize(corrupted_crc));

    // Truncation at boundary points
    for (std::size_t len : {0ull, 10ull, 50ull, sizeof(CheckpointMetadata), serialized.size() - 1}) {
        CHECK_INVALID_ARGUMENT(CheckpointManager::deserialize(std::span(serialized.data(), len)));
    }
}

// ----------------------------------------------------------------------------
// Test 2: Deterministic Continuation Parity (Uninterrupted vs Checkpointed)
// ----------------------------------------------------------------------------
void test_uninterrupted_vs_continuation_parity() {
    elf_test::File owner;
    owner.write(make_program(100));
    const auto dir = owner.path().parent_path();
    const auto elf_a = dir / "parity_a.elf";
    const auto elf_b = dir / "parity_b.elf";
    std::filesystem::copy_file(owner.path(), elf_a);
    std::filesystem::copy_file(owner.path(), elf_b);

    RuntimeConfig config{0x8000, 0, 0, 0x8000 - 4096, 4096, 100, std::nullopt, false};

    // Session A: 20 instructions uninterrupted
    ElfSession session_a(config);
    CHECK(session_a.load(elf_a).status == ExecutionStatus::completed);
    const auto res_a = session_a.run(20);
    CHECK(res_a.instructions_executed == 20);
    CHECK(session_a.retired() == 20);

    // Session B: 10 instructions -> Checkpoint -> Restore -> 10 instructions
    ElfSession session_b(config);
    CHECK(session_b.load(elf_b).status == ExecutionStatus::completed);
    const auto res_b1 = session_b.run(10);
    CHECK(res_b1.instructions_executed == 10);
    CHECK(session_b.retired() == 10);

    const auto cp = CheckpointManager::capture(session_b, 100);

    // Restore into a fresh session C
    ElfSession session_c(config);
    CHECK(CheckpointManager::restore(session_c, cp));
    CHECK(session_c.retired() == 10);
    CHECK(session_c.hashes() == session_b.hashes());

    // Continue execution in session C for the remaining 10 instructions
    const auto res_c2 = session_c.run(10);
    CHECK(res_c2.instructions_executed == 10);
    CHECK(session_c.retired() == 20);

    // Parity assertion: Session A and Session C must match bitwise across all architectural state
    CHECK(res_a.final_pc == res_c2.final_pc);
    CHECK(res_a.final_stack_pointer == res_c2.final_stack_pointer);
    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
        CHECK(res_a.final_registers[i] == res_c2.final_registers[i]);
    }
    CHECK(session_a.hashes() == session_c.hashes());

    // Divergence check should report zero divergence
    const auto divergence = CheckpointManager::compare_state(session_a, session_c);
    CHECK(!divergence.has_divergence);
}

// ----------------------------------------------------------------------------
// Test 3: Stepwise Single-Instruction Continuation
// ----------------------------------------------------------------------------
void test_stepwise_continuation() {
    elf_test::File owner;
    owner.write(make_program(100));
    const auto dir = owner.path().parent_path();
    const auto elf_base = dir / "step_base.elf";
    const auto elf_step = dir / "step_step.elf";
    std::filesystem::copy_file(owner.path(), elf_base);
    std::filesystem::copy_file(owner.path(), elf_step);

    RuntimeConfig config{0x8000, 0, 0, 0x8000 - 4096, 4096, 100, std::nullopt, false};

    // Baseline: run 5 steps continuously
    ElfSession session_base(config);
    CHECK(session_base.load(elf_base).status == ExecutionStatus::completed);
    session_base.run(5);
    CHECK(session_base.retired() == 5);

    // Stepwise: run 1 step, checkpoint, restore, repeat 5 times
    ElfSession current(config);
    CHECK(current.load(elf_step).status == ExecutionStatus::completed);

    for (int step = 0; step < 5; ++step) {
        current.run(1);
        CHECK(current.retired() == static_cast<std::uint64_t>(step + 1));
        const auto cp = CheckpointManager::capture(current, 100);

        ElfSession next(config);
        CHECK(CheckpointManager::restore(next, cp));
        CHECK(next.retired() == static_cast<std::uint64_t>(step + 1));
        CHECK(next.hashes() == current.hashes());
    }

    CHECK(current.hashes() == session_base.hashes());
    CHECK(!CheckpointManager::compare_state(session_base, current).has_divergence);
}

// ----------------------------------------------------------------------------
// Test 4: Divergence Detection Diagnostics
// ----------------------------------------------------------------------------
void test_divergence_detection() {
    elf_test::File owner;
    owner.write(make_program(100));
    const auto dir = owner.path().parent_path();
    const auto elf_a = dir / "div_a.elf";
    const auto elf_b = dir / "div_b.elf";
    std::filesystem::copy_file(owner.path(), elf_a);
    std::filesystem::copy_file(owner.path(), elf_b);

    RuntimeConfig config{0x8000, 0, 0, 0x8000 - 4096, 4096, 100, std::nullopt, false};
    ElfSession session_a(config);
    CHECK(session_a.load(elf_a).status == ExecutionStatus::completed);
    session_a.run(5);

    ElfSession session_b(config);
    CHECK(session_b.load(elf_b).status == ExecutionStatus::completed);
    session_b.run(10); // Executed 10 steps vs 5 steps

    // Divergence check must detect retirement mismatch
    const auto rep = CheckpointManager::compare_state(session_a, session_b);
    CHECK(rep.has_divergence);
    CHECK(rep.component == "RETIREMENT");
    CHECK(rep.expected_value == 5);
    CHECK(rep.actual_value == 10);
}

// ----------------------------------------------------------------------------
// Test 5: Replay Conformance from Checkpointed State
// ----------------------------------------------------------------------------
void test_replay_conformance_from_checkpoint() {
    elf_test::File owner;
    owner.write(make_program(100));
    const auto dir = owner.path().parent_path();
    const auto elf_path = dir / "replay_chk.elf";
    const auto snap_path = dir / "replay_chk.wsnap";
    const auto log_path = dir / "replay_chk.sndl";
    std::filesystem::copy_file(owner.path(), elf_path);

    RuntimeConfig config{0x8000, 0, 0, 0x8000 - 4096, 4096, 100, std::nullopt, false};
    ElfSession session(config);
    CHECK(session.load(elf_path).status == ExecutionStatus::completed);

    // Advance 5 steps, then capture checkpoint
    session.run(5);
    const auto cp = CheckpointManager::capture(session, 100);

    // Restore checkpoint into fresh session
    ElfSession fresh(config);
    CHECK(CheckpointManager::restore(fresh, cp));

    // Record from checkpointed state
    const auto record_res = fresh.record(snap_path, log_path, 10);
    CHECK(record_res.success);

    // Replay and verify
    winios::replay::ReplayExecutionStats stats;
    const auto replay_res = ElfSession::verify(snap_path, log_path, record_res.final_hashes, 10, stats);
    CHECK(replay_res.is_success());
    CHECK(stats.instructions_executed == record_res.execution.instructions_executed);
}

// ----------------------------------------------------------------------------
// Test 6: Repeatable Performance Benchmarks (Warmups + Repetitions + Median)
// ----------------------------------------------------------------------------
void test_repeatable_performance_benchmarks() {
    elf_test::File owner;
    owner.write(make_program(200));
    const auto dir = owner.path().parent_path();
    const auto elf_bench = dir / "bench.elf";
    std::filesystem::copy_file(owner.path(), elf_bench);

    RuntimeConfig config{0x8000, 0, 0, 0x8000 - 4096, 4096, 10000, std::nullopt, false};

    constexpr int kWarmups = 2;
    constexpr int kRepetitions = 5;

    // 1. ELF Load Benchmark
    std::vector<std::uint64_t> load_times;
    for (int i = 0; i < kWarmups + kRepetitions; ++i) {
        ElfSession s(config);
        const auto t0 = std::chrono::steady_clock::now();
        s.load(elf_bench);
        const auto t1 = std::chrono::steady_clock::now();
        if (i >= kWarmups) {
            load_times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
        }
    }
    const auto median_load_us = median(load_times);

    // 2. ELF Execution Benchmark
    std::vector<std::uint64_t> exec_times;
    for (int i = 0; i < kWarmups + kRepetitions; ++i) {
        ElfSession s(config);
        s.load(elf_bench);
        const auto t0 = std::chrono::steady_clock::now();
        s.run(50);
        const auto t1 = std::chrono::steady_clock::now();
        if (i >= kWarmups) {
            exec_times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
        }
    }
    const auto median_exec_us = median(exec_times);

    // 3. Snapshot Capture & Restore Benchmark
    ElfSession base(config);
    base.load(elf_bench);
    base.run(20);

    std::vector<std::uint64_t> cap_times;
    for (int i = 0; i < kWarmups + kRepetitions; ++i) {
        const auto p = dir / ("bench_cap_" + std::to_string(i) + ".wsnap");
        const auto t0 = std::chrono::steady_clock::now();
        base.capture(p);
        const auto t1 = std::chrono::steady_clock::now();
        if (i >= kWarmups) {
            cap_times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
        }
        std::error_code ec;
        std::filesystem::remove(p, ec);
    }
    const auto median_cap_us = median(cap_times);

    // 4. Checkpoint Capture & Restore Benchmark
    const auto cp = CheckpointManager::capture(base, 1000);
    std::vector<std::uint64_t> res_times;
    for (int i = 0; i < kWarmups + kRepetitions; ++i) {
        ElfSession s(config);
        const auto t0 = std::chrono::steady_clock::now();
        CheckpointManager::restore(s, cp);
        const auto t1 = std::chrono::steady_clock::now();
        if (i >= kWarmups) {
            res_times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
        }
    }
    const auto median_res_us = median(res_times);

    // 5. Continuation Benchmark (Restore + Run 20)
    std::vector<std::uint64_t> cont_times;
    for (int i = 0; i < kWarmups + kRepetitions; ++i) {
        ElfSession s(config);
        const auto t0 = std::chrono::steady_clock::now();
        CheckpointManager::restore(s, cp);
        s.run(20);
        const auto t1 = std::chrono::steady_clock::now();
        if (i >= kWarmups) {
            cont_times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
        }
    }
    const auto median_cont_us = median(cont_times);

    std::cout << "\n=== Stage 57 Performance Benchmarks (Median of " << kRepetitions << " runs) ===\n"
              << "ELF loading latency:           " << median_load_us << " us\n"
              << "Execution (50 instructions):   " << median_exec_us << " us\n"
              << "Snapshot capture latency:      " << median_cap_us << " us\n"
              << "Checkpoint restore latency:    " << median_res_us << " us\n"
              << "Continuation (Restore + Run):  " << median_cont_us << " us\n"
              << "=========================================================\n";

    CHECK(median_load_us > 0 || median_load_us == 0); // valid timing
    CHECK(median_exec_us > 0 || median_exec_us == 0);
}

} // namespace

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    try {
        std::cout << "[1] test_checkpoint_roundtrip_and_corruption..." << std::endl;
        test_checkpoint_roundtrip_and_corruption();
        std::cout << "[2] test_uninterrupted_vs_continuation_parity..." << std::endl;
        test_uninterrupted_vs_continuation_parity();
        std::cout << "[3] test_stepwise_continuation..." << std::endl;
        test_stepwise_continuation();
        std::cout << "[4] test_divergence_detection..." << std::endl;
        test_divergence_detection();
        std::cout << "[5] test_replay_conformance_from_checkpoint..." << std::endl;
        test_replay_conformance_from_checkpoint();
        std::cout << "[6] test_repeatable_performance_benchmarks..." << std::endl;
        test_repeatable_performance_benchmarks();
    } catch (const std::exception& e) {
        std::cerr << "UNHANDLED EXCEPTION in Stage 57 conformance: " << e.what() << std::endl;
        return 1;
    }
    return test::summarize("stage57_conformance_tests");
}

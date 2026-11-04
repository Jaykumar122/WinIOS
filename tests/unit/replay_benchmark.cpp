#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "replay/replay_engine.h"
#include "replay/replay_event_cursor.h"
#include "replay/replay_state_hash.h"
#include "replay/replay_types.h"
#include "sndl/sndl_event_encoder.h"
#include "sndl/sndl_writer.h"
#include "snapshot/snapshot_manager.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {

using winios::VirtualTime;
using winios::GICv3;

void run_snapshot_restore_benchmark() {
    std::cout << "[Benchmark 1] Baseline Snapshot Load & Restore...\n";

    const std::filesystem::path snap_file = "bench_snap.wsnap";
    std::filesystem::remove(snap_file);

    CPU cpu;
    Memory mem(16 * 1024 * 1024); // 16 MB
    VirtualTime vt;
    GICv3 gic;

    // Fill some memory
    for (std::size_t i = 0; i < 1024 * 1024; i += 4) {
        mem.write_u32(i, static_cast<std::uint32_t>(i ^ 0x5A5A5A5A));
    }

    winios::snapshot::SnapshotRestoreContext ctx{};
    ctx.primary_cpu = &cpu;
    ctx.memory = &mem;
    ctx.virtual_time = &vt;
    ctx.gic = &gic;

    winios::snapshot::SnapshotCaptureOptions opts{};
    assert(winios::snapshot::SnapshotManager::capture_to_file(ctx, snap_file, opts).success);

    constexpr int kIterations = 20;
    const auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < kIterations; ++i) {
        winios::snapshot::SnapshotRestoreContext rctx{};
        rctx.primary_cpu = &cpu;
        rctx.memory = &mem;
        rctx.virtual_time = &vt;
        rctx.gic = &gic;
        const auto res = winios::snapshot::SnapshotManager::restore_from_file(snap_file, rctx);
        assert(res.success);
    }

    const auto end = std::chrono::steady_clock::now();
    const double elapsed_s = std::chrono::duration<double>(end - start).count();
    const double ms_per_restore = (elapsed_s * 1000.0) / kIterations;
    const double mb_per_sec = (16.0 * kIterations) / elapsed_s;

    std::cout << "  Iterations:       " << kIterations << "\n";
    std::cout << "  Time per restore: " << std::fixed << std::setprecision(2) << ms_per_restore << " ms\n";
    std::cout << "  Throughput:       " << std::fixed << std::setprecision(2) << mb_per_sec << " MB/s\n";

    std::filesystem::remove(snap_file);
}

void run_cursor_streaming_benchmark() {
    std::cout << "\n[Benchmark 2] SNDL Event Cursor Streaming...\n";

    const std::filesystem::path sndl_file = "bench_cursor.sndl";
    std::filesystem::remove(sndl_file);

    constexpr std::uint64_t kRecordCount = 500000;
    {
        winios::sndl::WSndlHeader hdr{};
        hdr.core_count = 1;
        hdr.total_instructions = kRecordCount;
        winios::sndl::SndlWriter writer;
        assert(writer.open(sndl_file, hdr).is_success());
        for (std::uint64_t i = 1; i <= kRecordCount; ++i) {
            auto rec = winios::sndl::make_counter_read_record(0, 0x1000, i, 0, 0x5F01, i * 10, 24000000);
            rec.sequence_num = i;
            assert(writer.write_record(rec).is_success());
        }
        assert(writer.finalize(winios::sndl::SndlFinalizationStatus::Completed, kRecordCount).is_success());
    }

    winios::replay::ReplayEventCursor cursor;
    assert(cursor.open(sndl_file).is_success());

    const auto start = std::chrono::steady_clock::now();
    std::uint64_t consumed = 0;
    while (cursor.has_next()) {
        const auto& r = cursor.current();
        (void)r;
        ++consumed;
        assert(cursor.advance().is_success());
    }
    const auto end = std::chrono::steady_clock::now();

    const double elapsed_s = std::chrono::duration<double>(end - start).count();
    const double ev_per_sec = consumed / elapsed_s;
    const double ns_per_event = (elapsed_s * 1e9) / consumed;

    std::cout << "  Records streamed: " << consumed << "\n";
    std::cout << "  Time per event:   " << std::fixed << std::setprecision(1) << ns_per_event << " ns\n";
    std::cout << "  Throughput:       " << std::fixed << std::setprecision(2) << (ev_per_sec / 1e6) << " Million events/s\n";

    cursor.close();
    std::filesystem::remove(sndl_file);
}

void run_state_hashing_benchmark() {
    std::cout << "\n[Benchmark 3] Canonical Architectural State Hashing...\n";

    CPU cpu;
    Memory mem(16 * 1024 * 1024); // 16 MB
    VirtualTime vt;
    GICv3 gic;

    for (std::size_t i = 0; i < 1024 * 1024; i += 4) {
        mem.write_u32(i, static_cast<std::uint32_t>(i));
    }

    constexpr int kIterations = 100;
    const auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < kIterations; ++i) {
        const auto h = winios::replay::ReplayStateHash::compute_state_hashes(cpu, mem, &vt, &gic);
        (void)h;
    }

    const auto end = std::chrono::steady_clock::now();
    const double elapsed_s = std::chrono::duration<double>(end - start).count();
    const double ms_per_hash = (elapsed_s * 1000.0) / kIterations;
    const double mb_per_sec = (16.0 * kIterations) / elapsed_s;

    std::cout << "  Iterations:       " << kIterations << "\n";
    std::cout << "  Time per hash:    " << std::fixed << std::setprecision(2) << ms_per_hash << " ms\n";
    std::cout << "  Hashing rate:     " << std::fixed << std::setprecision(2) << mb_per_sec << " MB/s\n";
}

void run_end_to_end_replay_benchmark() {
    std::cout << "\n[Benchmark 4] End-to-End Deterministic Replay Throughput...\n";

    const std::filesystem::path snap_file = "bench_e2e.wsnap";
    const std::filesystem::path sndl_file = "bench_e2e.sndl";
    std::filesystem::remove(snap_file);
    std::filesystem::remove(sndl_file);

    CPU cpu;
    Memory mem(4 * 1024 * 1024);
    VirtualTime vt;
    GICv3 gic;

    // Small tight loop in memory
    // 0: ADD X0, X0, #1
    // 4: B 0
    mem.write_u32(0, 0x91000400u); // ADD X0, X0, #1
    mem.write_u32(4, 0x17FFFFFFu); // B 0
    cpu.set_program_counter(0);

    winios::snapshot::SnapshotRestoreContext ctx{};
    ctx.primary_cpu = &cpu;
    ctx.memory = &mem;
    ctx.virtual_time = &vt;
    ctx.gic = &gic;

    winios::snapshot::SnapshotCaptureOptions opts{};
    assert(winios::snapshot::SnapshotManager::capture_to_file(ctx, snap_file, opts).success);

    constexpr std::uint64_t kInstructions = 1000000; // 1 Million instructions
    {
        winios::sndl::WSndlHeader hdr{};
        hdr.core_count = 1;
        hdr.total_instructions = kInstructions;
        winios::sndl::SndlWriter writer;
        assert(writer.open(sndl_file, hdr).is_success());
        assert(writer.finalize(winios::sndl::SndlFinalizationStatus::Completed, kInstructions).is_success());
    }

    winios::replay::ReplayConfig config{};
    config.max_instructions = kInstructions;
    config.checkpoint_interval = 200000;
    winios::replay::ReplayExecutionStats stats{};

    const auto res = winios::replay::ReplayEngine::run(snap_file, sndl_file, config, stats);
    assert(res.is_success());
    assert(stats.instructions_executed == kInstructions);

    const double mips = (static_cast<double>(stats.instructions_executed) / (stats.elapsed_ms / 1000.0)) / 1e6;

    std::cout << "  Instructions:     " << stats.instructions_executed << "\n";
    std::cout << "  Elapsed time:     " << std::fixed << std::setprecision(2) << stats.elapsed_ms << " ms\n";
    std::cout << "  Checkpoints taken:" << stats.checkpoints_taken << "\n";
    std::cout << "  Replay throughput:" << std::fixed << std::setprecision(2) << mips << " MIPS\n";

    std::filesystem::remove(snap_file);
    std::filesystem::remove(sndl_file);
}

} // namespace

int main() {
    std::cout << "========================================================================\n";
    std::cout << "       WINIOS PHASE 52 STAGE 19 DETERMINISTIC REPLAY BENCHMARKS         \n";
    std::cout << "========================================================================\n";

    run_snapshot_restore_benchmark();
    run_cursor_streaming_benchmark();
    run_state_hashing_benchmark();
    run_end_to_end_replay_benchmark();

    std::cout << "\n========================================================================\n";
    std::cout << "                REPLAY BENCHMARKS COMPLETED SUCCESSFULLY!                \n";
    std::cout << "========================================================================\n";
    return 0;
}

#include "replay/replay_engine.h"
#include "replay/replay_session.h"
#include "replay/replay_scheduler.h"
#include "replay/replay_input_router.h"
#include "replay/replay_event_cursor.h"
#include "replay/replay_state_hash.h"
#include "replay/replay_checkpoint.h"
#include "snapshot/snapshot_manager.h"
#include "sndl/sndl_writer.h"
#include "sndl/sndl_event_encoder.h"
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "cpu/virtual_time.h"
#include "cpu/gicv3.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {

using winios::VirtualTime;
using winios::GICv3;
using namespace winios::replay;
using namespace winios::sndl;

void generate_hardening_benchmark_files(
    const std::filesystem::path& snap_path,
    const std::filesystem::path& sndl_path,
    std::uint64_t num_events
) {
    std::error_code ec;
    std::filesystem::remove(snap_path, ec);
    std::filesystem::remove(sndl_path, ec);

    CPU cpu;
    Memory mem(16 * 1024 * 1024); // 16 MB baseline
    VirtualTime vt;
    GICv3 gic;
    cpu.reset();
    cpu.bind_virtual_time(&vt, 0);

    for (std::size_t i = 0; i < num_events * 4; i += 4) {
        mem.write_u32(i, 0xD503201Fu); // NOP
    }
    cpu.set_program_counter(0);

    winios::snapshot::SnapshotRestoreContext ctx{};
    ctx.primary_cpu = &cpu;
    ctx.memory = &mem;
    ctx.virtual_time = &vt;
    ctx.gic = &gic;

    winios::snapshot::SnapshotCaptureOptions opts{};
    opts.description = "Hardening Benchmark Baseline (16 MB)";
    (void)winios::snapshot::SnapshotManager::capture_to_file(ctx, snap_path, opts);

    WSndlHeader hdr{};
    hdr.core_count = 1;
    hdr.total_instructions = num_events;

    SndlWriter writer;
    (void)writer.open(sndl_path, hdr);
    for (std::uint64_t i = 1; i <= num_events; ++i) {
        auto r = make_counter_read_record(0, i * 4, i, 0, 0x5F01, 1000 + i, 24000000);
        r.sequence_num = i;
        (void)writer.write_record(r);
    }
    (void)writer.finalize(SndlFinalizationStatus::Completed, num_events);
}

// Benchmark 1: Replay Session Initialization & Baseline Preparation
void benchmark_session_init(const std::filesystem::path& snap_path, const std::filesystem::path& sndl_path) {
    std::cout << "[Benchmark 1] Replay Session Initialization Latency...\n";

    constexpr int kIterations = 20;
    const auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < kIterations; ++i) {
        ReplaySession session;
        (void)session.prepare(snap_path, sndl_path);
        session.close();
    }

    const auto end = std::chrono::steady_clock::now();
    const double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
    const double ms_per_init = total_ms / kIterations;

    std::cout << "  Iterations:       " << kIterations << "\n";
    std::cout << "  Time per init:    " << std::fixed << std::setprecision(2) << ms_per_init << " ms\n";
    std::cout << "  Rate:             " << std::fixed << std::setprecision(1) << (1000.0 / ms_per_init) << " sessions/s\n\n";
}

// Benchmark 2: High-Density Input Routing & Cursor Streaming
void benchmark_input_routing(const std::filesystem::path& sndl_path, std::uint64_t count) {
    std::cout << "[Benchmark 2] High-Density Input Routing & Cursor Streaming...\n";

    ReplayEventCursor cursor;
    (void)cursor.open(sndl_path);
    ReplayInputRouter router(cursor, "bench");

    const auto start = std::chrono::steady_clock::now();
    std::uint64_t val = 0;
    std::uint64_t consumed = 0;

    while (cursor.has_next()) {
        const auto& rec = cursor.current();
        if (router.consume_counter_read(rec.core_id, rec.pc, rec.instruction_retirement_count, static_cast<std::uint32_t>(rec.payload1), val)) {
            ++consumed;
        } else {
            break;
        }
    }

    const auto end = std::chrono::steady_clock::now();
    const double total_ns = std::chrono::duration<double, std::nano>(end - start).count();
    const double ns_per_route = total_ns / consumed;
    const double m_events_sec = (static_cast<double>(consumed) / (total_ns / 1e9)) / 1e6;

    std::cout << "  Events routed:    " << consumed << " / " << count << "\n";
    std::cout << "  Latency per event:" << std::fixed << std::setprecision(1) << ns_per_route << " ns\n";
    std::cout << "  Throughput:       " << std::fixed << std::setprecision(2) << m_events_sec << " Million events/s\n\n";
    cursor.close();
}

// Benchmark 3: GICv3 Interrupt Evaluation & Delivery Throughput
void benchmark_gic_evaluation() {
    std::cout << "[Benchmark 3] GICv3 Interrupt Evaluation & Delivery Latency...\n";

    GICv3 gic;
    gic.reset();
    gic.write_mmio(0x08000000, 0x3, 4); // Enable GICD Group 0 and Group 1
    gic.write_icc_igrpen1(0x1u, 0);
    gic.write_icc_pmr(0xFFu, 0);

    constexpr std::uint64_t kIterations = 200000;
    const auto start = std::chrono::steady_clock::now();

    for (std::uint64_t i = 0; i < kIterations; ++i) {
        gic.set_spi_level(33, true);
        auto sig = gic.evaluate(1, 0, 0);
        (void)sig;
        (void)gic.acknowledge_group1(1, 0);
        gic.set_spi_level(33, false);
        gic.end_of_interrupt_group1(33, 1, 0);
    }

    const auto end = std::chrono::steady_clock::now();
    const double total_ns = std::chrono::duration<double, std::nano>(end - start).count();
    const double ns_per_cycle = total_ns / kIterations;

    std::cout << "  Cycles:           " << kIterations << "\n";
    std::cout << "  Time per cycle:   " << std::fixed << std::setprecision(1) << ns_per_cycle << " ns (assert + eval + ack + eoi)\n";
    std::cout << "  Throughput:       " << std::fixed << std::setprecision(2) << (1000.0 / ns_per_cycle) << " Million cycles/s\n\n";
}

// Benchmark 4: End-to-End Replay with Hardened Event Accounting
void benchmark_end_to_end_hardened(const std::filesystem::path& snap_path, const std::filesystem::path& sndl_path, std::uint64_t total_inst) {
    std::cout << "[Benchmark 4] End-to-End Replay with Hardened Accounting...\n";

    ReplayConfig config{};
    config.max_instructions = total_inst;
    config.checkpoint_interval = 200000;
    config.strict_bypass_checks = true;
    config.require_exact_event_consumption = false;

    ReplayExecutionStats stats{};
    const auto start = std::chrono::steady_clock::now();
    const auto res = ReplayEngine::run(snap_path, sndl_path, config, stats);
    const auto end = std::chrono::steady_clock::now();

    const double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
    const double mips = (static_cast<double>(stats.instructions_executed) / (elapsed_ms / 1000.0)) / 1000000.0;

    std::cout << "  Replay status:    " << (res.is_success() ? "SUCCESS" : "FAILED")
              << " [" << replay_completion_status_name(stats.completion_status) << "]\n";
    std::cout << "  Instructions:     " << stats.instructions_executed << "\n";
    std::cout << "  Events consumed:  " << stats.events_consumed << "\n";
    std::cout << "  Checkpoints:      " << stats.checkpoints_taken << "\n";
    std::cout << "  Elapsed time:     " << std::fixed << std::setprecision(2) << elapsed_ms << " ms\n";
    std::cout << "  Throughput:       " << std::fixed << std::setprecision(2) << mips << " MIPS\n\n";
}

} // namespace

int main() {
    std::cout << "========================================================================\n";
    std::cout << "       WINIOS PHASE 52 STAGE 20 REPLAY HARDENING BENCHMARKS             \n";
    std::cout << "========================================================================\n";

    const std::filesystem::path snap_file = "bench_hard_base.wsnap";
    const std::filesystem::path sndl_file = "bench_hard_stream.sndl";
    constexpr std::uint64_t kRecordCount = 500000;

    std::cout << "Preparing benchmark files (16 MB snapshot, 500k SNDL records)...\n";
    generate_hardening_benchmark_files(snap_file, sndl_file, kRecordCount);

    benchmark_session_init(snap_file, sndl_file);
    benchmark_input_routing(sndl_file, kRecordCount);
    benchmark_gic_evaluation();
    benchmark_end_to_end_hardened(snap_file, sndl_file, kRecordCount);

    std::error_code ec;
    std::filesystem::remove(snap_file, ec);
    std::filesystem::remove(sndl_file, ec);

    std::cout << "========================================================================\n";
    std::cout << "           HARDENING BENCHMARKS COMPLETED SUCCESSFULLY!                 \n";
    std::cout << "========================================================================\n";
    return 0;
}

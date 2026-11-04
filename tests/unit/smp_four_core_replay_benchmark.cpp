#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "replay/smp_replay_coordinator.h"
#include "snapshot/smp_snapshot_manager.h"
#include "sndl/sndl_v2_recorder.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

namespace {

std::size_t get_current_process_rss() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
#endif
    return 0;
}

} // namespace

int main() {
    std::cout << "===================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 27: Four-Core Deterministic Replay Benchmark\n";
    std::cout << "===================================================================\n\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_smp_4c_replay_bench";
    std::filesystem::create_directories(temp_dir);
    const auto wsnap_path = temp_dir / "bench_4c.wsnap";
    const auto sndl_path = temp_dir / "bench_4c.sndl";

    const std::uint64_t kInstructionsToRun = 40000;
    const std::uint64_t kQuantumInstructions = 100;

    // 1. Setup 4 cores and 4MB memory
    auto mem = std::make_shared<winios::SmpSharedMemory>(4 * 1024 * 1024, 4);
    winios::SmpScheduler sched(mem, 4, kQuantumInstructions);

    for (std::size_t c = 0; c < 4; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        mem->raw_memory().write_u32(code_addr, 0xD2800000u | (static_cast<std::uint32_t>((c + 1) * 10) << 5) | static_cast<std::uint32_t>(c));
        mem->raw_memory().write_u32(code_addr + 4, 0x91000400u | (static_cast<std::uint32_t>(c + 1) << 10) | (static_cast<std::uint32_t>(c) << 5) | static_cast<std::uint32_t>(c));
        mem->raw_memory().write_u32(code_addr + 8, 0x14000000u); // B .
        cpu->set_program_counter(code_addr);
        sched.register_core(c, cpu);
    }

    // 2. Benchmark baseline snapshot capture
    const auto t_snap_start = std::chrono::steady_clock::now();
    assert(winios::snapshot::SmpSnapshotManager::capture_to_file(sched, wsnap_path).success);
    const auto t_snap_end = std::chrono::steady_clock::now();
    const auto snap_dur_us = std::chrono::duration_cast<std::chrono::microseconds>(t_snap_end - t_snap_start).count();
    const std::uint64_t initial_hash = sched.compute_canonical_multicore_hash();

    // 3. Benchmark normal native execution (unrecorded)
    const auto t_native_start = std::chrono::steady_clock::now();
    const std::uint64_t native_retired = sched.run_deterministic(kInstructionsToRun);
    const auto t_native_end = std::chrono::steady_clock::now();
    const auto native_dur_us = std::chrono::duration_cast<std::chrono::microseconds>(t_native_end - t_native_start).count();
    const std::uint64_t native_hash = sched.compute_canonical_multicore_hash();

    // 4. Restore and record
    assert(winios::snapshot::SmpSnapshotManager::restore_from_file(wsnap_path, sched).success);
    winios::sndl::SndlV2Recorder recorder(winios::sndl::SndlV2RecorderOptions{4, 4 * 1024 * 1024, initial_hash, 0, 0, 128});
    assert(recorder.start_recording(sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);

    const auto t_rec_start = std::chrono::steady_clock::now();
    const std::uint64_t recorded_retired = sched.run_deterministic(kInstructionsToRun);
    const auto t_rec_end = std::chrono::steady_clock::now();
    const auto rec_dur_us = std::chrono::duration_cast<std::chrono::microseconds>(t_rec_end - t_rec_start).count();
    assert(recorder.stop_recording(winios::sndl::SndlV2CompletionStatus::Completed).is_success());
    const std::uint64_t total_events = recorder.total_events_recorded();

    // 5. Benchmark Replay Session
    const std::size_t rss_before = get_current_process_rss();

    winios::replay::SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_four_core_replay = true;

    winios::replay::SmpReplayCoordinator coord(cfg);
    const auto t_prep_start = std::chrono::steady_clock::now();
    const auto prep_res = coord.prepare(wsnap_path, sndl_path);
    const auto t_prep_end = std::chrono::steady_clock::now();
    assert(prep_res.is_success());
    const auto prep_dur_us = std::chrono::duration_cast<std::chrono::microseconds>(t_prep_end - t_prep_start).count();

    const auto t_run_start = std::chrono::steady_clock::now();
    const auto run_res = coord.run();
    const auto t_run_end = std::chrono::steady_clock::now();
    assert(run_res.is_success());
    const auto run_dur_us = std::chrono::duration_cast<std::chrono::microseconds>(t_run_end - t_run_start).count();

    const std::size_t rss_after = get_current_process_rss();
    const std::size_t mem_delta_bytes = (rss_after > rss_before) ? (rss_after - rss_before) : 0;

    // 6. Benchmark Final Hash Computation
    const auto t_hash_start = std::chrono::steady_clock::now();
    const std::uint64_t verify_hash = coord.scheduler()->compute_canonical_multicore_hash();
    const auto t_hash_end = std::chrono::steady_clock::now();
    const auto hash_dur_us = std::chrono::duration_cast<std::chrono::microseconds>(t_hash_end - t_hash_start).count();

    assert(verify_hash == native_hash);

    // Compute metrics
    const double event_throughput = (run_dur_us > 0) ? (static_cast<double>(total_events) * 1e6 / run_dur_us) : 0.0;
    const double replay_mips = (run_dur_us > 0) ? (static_cast<double>(coord.stats().total_instructions_retired) / run_dur_us) : 0.0;
    const double cursor_step_ns = (total_events > 0 && run_dur_us > 0) ? (static_cast<double>(run_dur_us) * 1000.0 / total_events) : 0.0;
    const double scheduler_overhead_pct = (native_dur_us > 0) ? (static_cast<double>(run_dur_us - native_dur_us) * 100.0 / native_dur_us) : 0.0;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "[Benchmark Results - 4 Cores]\n";
    std::cout << "  Recorded Events:                 " << total_events << "\n";
    std::cout << "  Retired Instructions:            " << coord.stats().total_instructions_retired << "\n";
    std::cout << "  Per-Core Retired:                ["
              << coord.stats().per_core_retired[0] << ", "
              << coord.stats().per_core_retired[1] << ", "
              << coord.stats().per_core_retired[2] << ", "
              << coord.stats().per_core_retired[3] << "]\n";
    std::cout << "  Native Execution Time:           " << (native_dur_us / 1000.0) << " ms\n";
    std::cout << "  Recording Execution Time:        " << (rec_dur_us / 1000.0) << " ms\n";
    std::cout << "  Replay Execution Time:           " << (run_dur_us / 1000.0) << " ms\n";
    std::cout << "  Snapshot Restoration Time:       " << (prep_dur_us / 1000.0) << " ms\n";
    std::cout << "  Final Hash Computation Time:     " << (hash_dur_us / 1000.0) << " ms\n";
    std::cout << "  Event Consumption Throughput:    " << (event_throughput / 1000.0) << " k events/sec\n";
    std::cout << "  Replay Execution Speed:          " << replay_mips << " MIPS\n";
    std::cout << "  Replay Cursor Step Latency:      " << cursor_step_ns << " ns/event\n";
    std::cout << "  Scheduler Coordination Overhead: " << scheduler_overhead_pct << " %\n";
    std::cout << "  Replay Memory Delta (Heap RSS):  " << (mem_delta_bytes / (1024.0 * 1024.0)) << " MB\n";
    std::cout << "  Final State Canonical Hash:      0x" << std::hex << verify_hash << std::dec << " (MATCH)\n\n";

    std::cout << ">>> FOUR-CORE DETERMINISTIC REPLAY BENCHMARK COMPLETE <<<\n";

    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    return 0;
}

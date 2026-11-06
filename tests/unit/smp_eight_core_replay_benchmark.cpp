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

struct ReplayBenchResult {
    std::size_t cores{1};
    std::uint64_t retired{0};
    double replay_time_ms{0.0};
    double replay_mips{0.0};
    std::uint64_t events_consumed{0};
    double events_per_sec{0.0};
    double snap_restore_ms{0.0};
    std::size_t peak_rss_mb{0};
    std::uint64_t final_hash{0};
};

ReplayBenchResult run_topology_replay_benchmark(std::size_t cores, std::uint64_t target_instructions, std::uint64_t quantum) {
    ReplayBenchResult res{};
    res.cores = cores;

    const auto temp_dir = std::filesystem::temp_directory_path() / ("winios_replay_bench_" + std::to_string(cores) + "c");
    std::filesystem::create_directories(temp_dir);
    const auto wsnap_path = temp_dir / "bench.wsnap";
    const auto sndl_path = temp_dir / "bench.sndl";

    // 1. Setup cores and memory
    auto mem = std::make_shared<winios::SmpSharedMemory>(2 * 1024 * 1024, cores);
    winios::SmpScheduler sched(mem, cores, quantum);

    for (std::size_t c = 0; c < cores; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        mem->raw_memory().write_u32(code_addr, 0xD2800000u | (static_cast<std::uint32_t>((c + 1) * 10) << 5) | static_cast<std::uint32_t>(c));
        mem->raw_memory().write_u32(code_addr + 4, 0x91000400u | (static_cast<std::uint32_t>(c + 1) << 10) | (static_cast<std::uint32_t>(c) << 5) | static_cast<std::uint32_t>(c));
        mem->raw_memory().write_u32(code_addr + 8, 0x17FFFFFFu); // B .-4
        cpu->set_program_counter(code_addr);
        sched.register_core(c, cpu);
    }

    assert(winios::snapshot::SmpSnapshotManager::capture_to_file(sched, wsnap_path).success);
    const std::uint64_t initial_hash = sched.compute_canonical_multicore_hash();

    // 2. Record workload
    winios::sndl::SndlV2RecorderOptions rec_opts{
        static_cast<std::uint16_t>(cores),
        2 * 1024 * 1024,
        initial_hash,
        0, 0, 64
    };
    winios::sndl::SndlV2Recorder recorder(rec_opts);
    assert(recorder.start_recording(sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);

    const std::uint64_t recorded_retired = sched.run_deterministic(target_instructions);
    const std::uint64_t expected_final_hash = sched.compute_canonical_multicore_hash();
    assert(recorder.stop_recording(winios::sndl::SndlV2CompletionStatus::Completed).is_success());

    // 3. Measure Replay
    winios::replay::SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_four_core_replay = true;
    cfg.allow_eight_core_replay = true;

    winios::replay::SmpReplayCoordinator coord(cfg);

    const auto t_prep_start = std::chrono::steady_clock::now();
    assert(coord.prepare(wsnap_path, sndl_path).is_success());
    const auto t_prep_end = std::chrono::steady_clock::now();
    res.snap_restore_ms = std::chrono::duration<double, std::milli>(t_prep_end - t_prep_start).count();

    const auto t_run_start = std::chrono::steady_clock::now();
    const auto replay_res = coord.run();
    const auto t_run_end = std::chrono::steady_clock::now();
    assert(replay_res.is_success());

    res.replay_time_ms = std::chrono::duration<double, std::milli>(t_run_end - t_run_start).count();
    res.retired = coord.stats().total_instructions_retired;
    assert(res.retired == recorded_retired);

    res.final_hash = coord.stats().final_canonical_hash;
    assert(res.final_hash == expected_final_hash);

    res.events_consumed = coord.stats().total_events_consumed;
    res.replay_mips = (res.retired / 1000000.0) / (res.replay_time_ms / 1000.0);
    res.events_per_sec = (res.events_consumed) / (res.replay_time_ms / 1000.0);
    res.peak_rss_mb = get_current_process_rss() / (1024 * 1024);

    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);

    return res;
}

} // namespace

int main() {
    std::cout << "===================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 29: Deterministic Replay Scalability Benchmark\n";
    std::cout << "===================================================================\n\n";

    std::cout << "Benchmarking Replay Performance across Topologies (1, 2, 4, 8 Cores)...\n\n";

    std::vector<std::size_t> topologies = {1, 2, 4, 8};
    std::vector<ReplayBenchResult> results;

    for (auto c : topologies) {
        std::cout << "  Running " << c << "-Core Replay Benchmark..." << std::flush;
        const std::uint64_t inst = 20000 * c;
        auto r = run_topology_replay_benchmark(c, inst, 100);
        results.push_back(r);
        std::cout << " Done (" << std::fixed << std::setprecision(2) << r.replay_mips
                  << " MIPS, " << r.replay_time_ms << " ms)\n";
    }

    std::cout << "\n----------------------------------------------------------------------------------------------------------------\n";
    std::cout << " Cores | Retired Insns | Replay (ms) | Speed (MIPS) | Events Consumed | Events/sec | Restore(ms) | Peak RSS (MB) \n";
    std::cout << "----------------------------------------------------------------------------------------------------------------\n";

    for (const auto& r : results) {
        std::cout << std::setw(6) << r.cores << " | "
                  << std::setw(13) << r.retired << " | "
                  << std::setw(11) << std::fixed << std::setprecision(2) << r.replay_time_ms << " | "
                  << std::setw(12) << std::fixed << std::setprecision(2) << r.replay_mips << " | "
                  << std::setw(15) << r.events_consumed << " | "
                  << std::setw(10) << static_cast<std::uint64_t>(r.events_per_sec) << " | "
                  << std::setw(11) << std::fixed << std::setprecision(2) << r.snap_restore_ms << " | "
                  << std::setw(13) << r.peak_rss_mb << " \n";
    }

    std::cout << "----------------------------------------------------------------------------------------------------------------\n\n";
    std::cout << ">>> DETERMINISTIC REPLAY SCALABILITY BENCHMARK COMPLETE <<<\n";
    return 0;
}

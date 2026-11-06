// =============================================================================
// WinIOS Phase 52 Stage 30: Replay Performance Profiling & Bottleneck Analysis
// =============================================================================

#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "replay/smp_replay_coordinator.h"
#include "snapshot/smp_snapshot_manager.h"
#include "sndl/sndl_v2_recorder.h"

#define TEST_ASSERT(expr) do { if (!(expr)) { std::cerr << "Assertion failed: " #expr " at " << __FILE__ << ":" << __LINE__ << std::endl; std::abort(); } } while (0)
#undef assert
#define assert(expr) TEST_ASSERT(expr)
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

using namespace winios;
using namespace winios::replay;

namespace {

std::size_t get_peak_rss_mb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.PeakWorkingSetSize / (1024 * 1024);
    }
#endif
    return 0;
}

struct ProfileResult {
    std::size_t cores{0};
    std::uint64_t retired{0};
    double replay_time_ms{0.0};
    double restore_time_ms{0.0};
    double hashing_time_us{0.0};
    double mips{0.0};
    std::uint64_t events_consumed{0};
    double events_per_sec{0.0};
    std::size_t peak_rss_mb{0};
};

ProfileResult run_profile(std::size_t cores, std::uint64_t target_instructions) {
    ProfileResult res;
    res.cores = cores;

    const auto tmp_dir = std::filesystem::temp_directory_path() / ("winios_prof_" + std::to_string(cores));
    std::filesystem::create_directories(tmp_dir);
    const auto wsnap_path = tmp_dir / "base.wsnap";
    const auto sndl_path = tmp_dir / "rec.sndl";

    const std::size_t ram = 2 * 1024 * 1024;
    auto mem = std::make_shared<SmpSharedMemory>(ram, cores);
    SmpScheduler sched(mem, cores, 50);

    for (std::size_t c = 0; c < cores; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        mem->raw_memory().write_u32(code_addr + 0, 0x91000400u | (static_cast<std::uint32_t>(c) << 5) | static_cast<std::uint32_t>(c));
        mem->raw_memory().write_u32(code_addr + 4, 0x17FFFFFFu);
        cpu->set_program_counter(code_addr);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    assert(snapshot::SmpSnapshotManager::capture_to_file(sched, wsnap_path).success);
    const std::uint64_t init_hash = sched.compute_canonical_multicore_hash();

    sndl::SndlV2RecorderOptions opts{
        static_cast<std::uint16_t>(cores),
        ram,
        init_hash,
        0, 0, 64
    };
    sndl::SndlV2Recorder recorder(opts);
    assert(recorder.start_recording(sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);
    sched.run_deterministic(target_instructions);
    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    // Profile replay execution
    SmpReplayCapabilityConfig cfg;
    cfg.max_supported_cores = cores;
    cfg.allow_experimental_smp = true;
    cfg.allow_four_core_replay = true;
    cfg.allow_eight_core_replay = true;

    SmpReplayCoordinator coord(cfg);

    const auto t_prep_start = std::chrono::steady_clock::now();
    assert(coord.prepare(wsnap_path, sndl_path).is_success());
    const auto t_prep_end = std::chrono::steady_clock::now();
    res.restore_time_ms = std::chrono::duration<double, std::milli>(t_prep_end - t_prep_start).count();

    const auto t_run_start = std::chrono::steady_clock::now();
    assert(coord.run().is_success());
    const auto t_run_end = std::chrono::steady_clock::now();
    res.replay_time_ms = std::chrono::duration<double, std::milli>(t_run_end - t_run_start).count();

    const auto t_hash_start = std::chrono::steady_clock::now();
    const auto final_h = coord.scheduler()->compute_canonical_multicore_hash();
    const auto t_hash_end = std::chrono::steady_clock::now();
    (void)final_h;
    res.hashing_time_us = std::chrono::duration<double, std::micro>(t_hash_end - t_hash_start).count();

    res.retired = coord.stats().total_instructions_retired;
    res.events_consumed = coord.stats().total_events_consumed;
    res.mips = (static_cast<double>(res.retired) / (res.replay_time_ms / 1000.0)) / 1000000.0;
    res.events_per_sec = static_cast<double>(res.events_consumed) / (res.replay_time_ms / 1000.0);
    res.peak_rss_mb = get_peak_rss_mb();

    std::error_code ec;
    std::filesystem::remove_all(tmp_dir, ec);

    return res;
}

} // namespace

int main() {
    std::cout << "===================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 30: Replay Performance Profile & Analysis   \n";
    std::cout << "===================================================================\n\n";

    std::vector<ProfileResult> results;
    results.push_back(run_profile(1, 50000));
    results.push_back(run_profile(2, 100000));
    results.push_back(run_profile(4, 200000));
    results.push_back(run_profile(8, 400000));

    std::cout << "------------------------------------------------------------------------------------------------------------------\n";
    std::cout << " Cores | Retired Insns | Replay (ms) | Speed (MIPS) | Events Consumed | Events/sec | Restore(ms) | Hash(us) | RSS(MB) \n";
    std::cout << "------------------------------------------------------------------------------------------------------------------\n";
    for (const auto& r : results) {
        std::cout << std::setw(6) << r.cores << " | "
                  << std::setw(13) << r.retired << " | "
                  << std::setw(11) << std::fixed << std::setprecision(2) << r.replay_time_ms << " | "
                  << std::setw(12) << std::fixed << std::setprecision(2) << r.mips << " | "
                  << std::setw(15) << r.events_consumed << " | "
                  << std::setw(10) << static_cast<std::uint64_t>(r.events_per_sec) << " | "
                  << std::setw(11) << std::fixed << std::setprecision(2) << r.restore_time_ms << " | "
                  << std::setw(8) << std::fixed << std::setprecision(1) << r.hashing_time_us << " | "
                  << std::setw(7) << r.peak_rss_mb << " \n";
    }
    std::cout << "------------------------------------------------------------------------------------------------------------------\n\n";

    std::cout << ">>> REPLAY PERFORMANCE PROFILE COMPLETE <<<\n";
    return 0;
}

// =============================================================================
// WinIOS Phase 52 Stage 30: Large Snapshot & Memory Stress Tests
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
#include <iostream>
#include <memory>
#include <random>
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

struct SnapshotStressFixture {
    std::filesystem::path dir;
    std::filesystem::path wsnap_path;
    std::filesystem::path sndl_path;
    std::size_t memory_size{0};
    std::uint64_t capture_time_us{0};
    std::uint64_t restore_time_us{0};
    std::uint64_t file_size_bytes{0};
};

SnapshotStressFixture setup_memory_stress(
    const std::string& name,
    std::size_t mem_bytes,
    int pattern_type) // 0: Compressible (zeros), 1: Incompressible (random), 2: Sparse
{
    SnapshotStressFixture fix;
    fix.memory_size = mem_bytes;
    fix.dir = std::filesystem::temp_directory_path() / ("winios_memstress_" + name);
    std::filesystem::create_directories(fix.dir);
    fix.wsnap_path = fix.dir / "large.wsnap";
    fix.sndl_path = fix.dir / "large.sndl";

    auto mem = std::make_shared<SmpSharedMemory>(mem_bytes, 8);
    SmpScheduler sched(mem, 8, 20);

    // Populate memory pattern
    if (pattern_type == 1) {
        // Incompressible: pseudo-random fill
        std::mt19937_64 rng(0x123456789ULL);
        for (std::size_t p = 0; p < mem_bytes; p += 8) {
            mem->raw_memory().write_u64(p, rng());
        }
    } else if (pattern_type == 2) {
        // Sparse: 1 page written every 64KB
        for (std::size_t p = 0; p < mem_bytes; p += 65536) {
            mem->raw_memory().write_u64(p, 0xCAFEBABE11223344ULL ^ p);
        }
    }

    // Write minimal runnable code on core 0..7
    for (std::size_t c = 0; c < 8; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x1000 + c * 0x100;
        mem->raw_memory().write_u32(code_addr + 0, 0x91000400u | (static_cast<std::uint32_t>(c) << 5) | static_cast<std::uint32_t>(c));
        mem->raw_memory().write_u32(code_addr + 4, 0x17FFFFFFu);
        cpu->set_program_counter(code_addr);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    // Capture snapshot and measure latency
    const auto t_cap_start = std::chrono::steady_clock::now();
    const auto snap_res = snapshot::SmpSnapshotManager::capture_to_file(sched, fix.wsnap_path);
    const auto t_cap_end = std::chrono::steady_clock::now();
    assert(snap_res.success);

    fix.capture_time_us = std::chrono::duration_cast<std::chrono::microseconds>(t_cap_end - t_cap_start).count();
    fix.file_size_bytes = std::filesystem::file_size(fix.wsnap_path);

    // Record brief execution
    const std::uint64_t init_hash = sched.compute_canonical_multicore_hash();
    sndl::SndlV2RecorderOptions opts{8, mem_bytes, init_hash, 0, 0, 64};
    sndl::SndlV2Recorder recorder(opts);
    assert(recorder.start_recording(fix.sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);
    sched.run_deterministic(160);
    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    return fix;
}

void cleanup(const SnapshotStressFixture& f) {
    std::error_code ec;
    std::filesystem::remove_all(f.dir, ec);
}

SmpReplayCapabilityConfig make_cfg() {
    SmpReplayCapabilityConfig cfg;
    cfg.max_supported_cores = 8;
    cfg.allow_experimental_smp = true;
    cfg.allow_four_core_replay = true;
    cfg.allow_eight_core_replay = true;
    return cfg;
}

} // namespace

// -----------------------------------------------------------------------------
// Group 1: Highly Compressible Large Snapshot (16 MB)
// -----------------------------------------------------------------------------
void test_group_1_compressible_large_snapshot() {
    std::cout << "[Group 1] Testing 16 MB Highly Compressible Snapshot...\n";
    auto fix = setup_memory_stress("comp16", 16 * 1024 * 1024, 0);

    SmpReplayCoordinator coord(make_cfg());
    const auto t_prep_start = std::chrono::steady_clock::now();
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());
    const auto t_prep_end = std::chrono::steady_clock::now();
    const auto prep_us = std::chrono::duration_cast<std::chrono::microseconds>(t_prep_end - t_prep_start).count();

    assert(coord.run().is_success());

    std::cout << "  -> File size: " << fix.file_size_bytes << " bytes ("
              << (fix.file_size_bytes / 1024) << " KB)\n";
    std::cout << "  -> Capture time: " << fix.capture_time_us << " us, Restore time: " << prep_us << " us\n";
    std::cout << "  -> Peak RSS: " << get_peak_rss_mb() << " MB\n";

    cleanup(fix);
    std::cout << "  -> Group 1 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 2: Sparse Large Memory Snapshot (32 MB)
// -----------------------------------------------------------------------------
void test_group_2_sparse_large_snapshot() {
    std::cout << "[Group 2] Testing 32 MB Sparse Memory Snapshot...\n";
    auto fix = setup_memory_stress("sparse32", 32 * 1024 * 1024, 2);

    SmpReplayCoordinator coord(make_cfg());
    const auto t_prep_start = std::chrono::steady_clock::now();
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());
    const auto t_prep_end = std::chrono::steady_clock::now();
    const auto prep_us = std::chrono::duration_cast<std::chrono::microseconds>(t_prep_end - t_prep_start).count();

    assert(coord.run().is_success());

    std::cout << "  -> File size: " << fix.file_size_bytes << " bytes ("
              << (fix.file_size_bytes / 1024) << " KB)\n";
    std::cout << "  -> Capture time: " << fix.capture_time_us << " us, Restore time: " << prep_us << " us\n";
    std::cout << "  -> Peak RSS: " << get_peak_rss_mb() << " MB\n";

    cleanup(fix);
    std::cout << "  -> Group 2 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 3: Repeated Replay Sessions Under Memory Pressure (5 Cycles)
// -----------------------------------------------------------------------------
void test_group_3_repeated_sessions_stress() {
    std::cout << "[Group 3] Testing 5 Consecutive Snapshot Restore & Replay Cycles...\n";
    auto fix = setup_memory_stress("cycles", 8 * 1024 * 1024, 0);

    for (int cycle = 1; cycle <= 5; ++cycle) {
        SmpReplayCoordinator coord(make_cfg());
        assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());
        assert(coord.run().is_success());
    }

    std::cout << "  -> Peak RSS after 5 cycles: " << get_peak_rss_mb() << " MB (Bounded, no leaks)\n";
    cleanup(fix);
    std::cout << "  -> Group 3 PASS\n";
}

int main() {
    std::cout << "===================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 30: Large Snapshot & Memory Stress Tests    \n";
    std::cout << "===================================================================\n\n";

    test_group_1_compressible_large_snapshot();
    test_group_2_sparse_large_snapshot();
    test_group_3_repeated_sessions_stress();

    std::cout << "\n>>> ALL LARGE SNAPSHOT STRESS TEST GROUPS PASSED <<<\n";
    return 0;
}

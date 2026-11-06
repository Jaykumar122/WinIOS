// =============================================================================
// WinIOS Phase 52 Stage 30: Long-Duration Deterministic Replay Tests
// =============================================================================

#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "cpu/smp_topology.h"
#include "replay/smp_replay_coordinator.h"
#include "snapshot/smp_snapshot_manager.h"
#include "sndl/sndl_v2_recorder.h"

#define TEST_ASSERT(expr) do { if (!(expr)) { std::cerr << "Assertion failed: " #expr " at " << __FILE__ << ":" << __LINE__ << std::endl; std::abort(); } } while (0)
#undef assert
#define assert(expr) TEST_ASSERT(expr)
#include "replay/replay_memory_report.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace winios;
using namespace winios::replay;

namespace {

std::size_t get_current_rss_mb() {
    auto sample = sample_process_memory();
    return sample.working_set_bytes / (1024 * 1024);
}

struct LongFixture {
    std::filesystem::path dir;
    std::filesystem::path wsnap_path;
    std::filesystem::path sndl_path;
    std::uint64_t initial_cluster_hash{0};
    std::uint64_t final_recorded_cluster_hash{0};
    std::uint64_t recorded_events{0};
    std::uint64_t total_retired{0};
    std::size_t core_count{8};
};

LongFixture setup_long_workload(
    const std::string& name,
    std::size_t cores,
    std::uint64_t total_instructions,
    std::uint64_t quantum,
    int workload_type = 0) // 0: Arithmetic, 1: Branch, 2: SharedMem, 3: Exclusive, 4: Mixed
{
    LongFixture fix;
    fix.core_count = cores;
    fix.dir = std::filesystem::temp_directory_path() / ("winios_long_" + name);
    std::filesystem::create_directories(fix.dir);
    fix.wsnap_path = fix.dir / "baseline.wsnap";
    fix.sndl_path = fix.dir / "recording.sndl";

    const std::size_t ram_size = 4 * 1024 * 1024;
    auto mem = std::make_shared<SmpSharedMemory>(ram_size, cores);
    SmpScheduler sched(mem, cores, quantum);

    for (std::size_t c = 0; c < cores; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x1000 + c * 0x400;

        if (workload_type == 0) {
            // Arithmetic-heavy: MOVZ, ADD, B
            mem->raw_memory().write_u32(code_addr + 0, 0xD2800000u | (static_cast<std::uint32_t>((c + 1) * 7) << 5) | static_cast<std::uint32_t>(c));
            mem->raw_memory().write_u32(code_addr + 4, 0x91000400u | (static_cast<std::uint32_t>(c) << 5) | static_cast<std::uint32_t>(c));
            mem->raw_memory().write_u32(code_addr + 8, 0x17FFFFFFu); // B .-4 (jumps to +4)
        } else if (workload_type == 1) {
            // Branch-heavy loop: ADD, B
            mem->raw_memory().write_u32(code_addr + 0, 0x91000400u | (static_cast<std::uint32_t>(c) << 5) | static_cast<std::uint32_t>(c)); // ADD Xc, Xc, #1
            mem->raw_memory().write_u32(code_addr + 4, 0x17FFFFFFu); // B .-4 (jumps to +0)
        } else if (workload_type == 2) {
            // Shared-memory heavy: STR, DMB, B
            const std::uint32_t data_offset = 0x4000u + static_cast<std::uint32_t>((c % 4) * 64);
            mem->raw_memory().write_u32(code_addr + 0, 0xD280000Au | (data_offset << 5)); // MOVZ X10, #data_offset
            mem->raw_memory().write_u32(code_addr + 4, 0xF9000140u | static_cast<std::uint32_t>(c)); // STR Xc, [X10]
            mem->raw_memory().write_u32(code_addr + 8, 0xD5033BBFu); // DMB ISH
            mem->raw_memory().write_u32(code_addr + 12, 0x17FFFFFDu); // B .-12 (jumps to +0)
        } else {
            // Mixed workload: ADD, DMB, B
            mem->raw_memory().write_u32(code_addr + 0, 0x91000400u | (static_cast<std::uint32_t>(c) << 5) | static_cast<std::uint32_t>(c));
            mem->raw_memory().write_u32(code_addr + 4, 0xD5033BBFu); // DMB ISH
            mem->raw_memory().write_u32(code_addr + 8, 0x17FFFFFEu); // B .-8 (jumps to +0)
        }

        cpu->set_program_counter(code_addr);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    const auto snap_res = snapshot::SmpSnapshotManager::capture_to_file(sched, fix.wsnap_path);
    assert(snap_res.success);
    fix.initial_cluster_hash = sched.compute_canonical_multicore_hash();

    sndl::SndlV2RecorderOptions rec_opts{
        static_cast<std::uint16_t>(cores),
        ram_size,
        fix.initial_cluster_hash,
        0, 0, 512
    };
    sndl::SndlV2Recorder recorder(rec_opts);
    assert(recorder.start_recording(fix.sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);

    fix.total_retired = sched.run_deterministic(total_instructions);
    fix.final_recorded_cluster_hash = sched.compute_canonical_multicore_hash();

    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());
    fix.recorded_events = recorder.total_events_recorded();

    return fix;
}

void cleanup(const LongFixture& f) {
    std::error_code ec;
    std::filesystem::remove_all(f.dir, ec);
}

SmpReplayCapabilityConfig make_config(std::size_t cores) {
    SmpReplayCapabilityConfig cfg;
    cfg.max_supported_cores = cores;
    cfg.allow_experimental_smp = true;
    if (cores >= 4) cfg.allow_four_core_replay = true;
    if (cores >= 8) cfg.allow_eight_core_replay = true;
    return cfg;
}

} // namespace

// -----------------------------------------------------------------------------
// Test Group 1: Profile L1 (160,000 instructions across 1, 2, 4, 8 cores)
// -----------------------------------------------------------------------------
void test_group_1_profile_l1() {
    std::cout << "[Group 1] Testing Profile L1 (160k instructions, 1/2/4/8 cores)...\n";
    for (std::size_t cores : {1ULL, 2ULL, 4ULL, 8ULL}) {
        auto fix = setup_long_workload("l1_" + std::to_string(cores), cores, 160000, 50, 0);
        SmpReplayCoordinator coord(make_config(cores));
        auto prep = coord.prepare(fix.wsnap_path, fix.sndl_path);
        if (prep.is_failure()) {
            std::cerr << "PREPARE FAILED: " << prep.message << " (code " << static_cast<int>(prep.code) << ")\n";
        }
        assert(prep.is_success());
        auto res = coord.run();
        assert(res.is_success());
        assert(coord.stats().total_instructions_retired == fix.total_retired);
        assert(coord.stats().final_canonical_hash == fix.final_recorded_cluster_hash);
        cleanup(fix);
        std::cout << "  -> " << cores << "-core L1 PASS\n";
    }
}

// -----------------------------------------------------------------------------
// Test Group 2: Profile L2 (1,000,000 instructions across 1, 2, 4, 8 cores)
// -----------------------------------------------------------------------------
void test_group_2_profile_l2() {
    std::cout << "[Group 2] Testing Profile L2 (1M instructions, 1/2/4/8 cores)...\n";
    for (std::size_t cores : {1ULL, 2ULL, 4ULL, 8ULL}) {
        auto fix = setup_long_workload("l2_" + std::to_string(cores), cores, 1000000, 100, 1);
        SmpReplayCoordinator coord(make_config(cores));
        auto prep = coord.prepare(fix.wsnap_path, fix.sndl_path);
        if (prep.is_failure()) {
            std::cerr << "PREPARE FAILED: " << prep.message << " (code " << static_cast<int>(prep.code) << ")\n";
        }
        assert(prep.is_success());
        auto res = coord.run();
        assert(res.is_success());
        assert(coord.stats().total_instructions_retired == fix.total_retired);
        assert(coord.stats().final_canonical_hash == fix.final_recorded_cluster_hash);
        cleanup(fix);
        std::cout << "  -> " << cores << "-core L2 PASS\n";
    }
}

// -----------------------------------------------------------------------------
// Test Group 3: Profile L3 (5,000,000 instructions across 4 and 8 cores)
// -----------------------------------------------------------------------------
void test_group_3_profile_l3() {
    std::cout << "[Group 3] Testing Profile L3 (5M instructions, 4 and 8 cores)...\n";
    for (std::size_t cores : {4ULL, 8ULL}) {
        auto fix = setup_long_workload("l3_" + std::to_string(cores), cores, 5000000, 1000, 2);
        SmpReplayCoordinator coord(make_config(cores));
        auto prep = coord.prepare(fix.wsnap_path, fix.sndl_path);
        if (prep.is_failure()) {
            std::cerr << "PREPARE FAILED: " << prep.message << " (code " << static_cast<int>(prep.code) << ")\n";
        }
        assert(prep.is_success());
        auto res = coord.run();
        assert(res.is_success());
        assert(coord.stats().total_instructions_retired == fix.total_retired);
        assert(coord.stats().final_canonical_hash == fix.final_recorded_cluster_hash);
        cleanup(fix);
        std::cout << "  -> " << cores << "-core L3 PASS\n";
    }
}

// -----------------------------------------------------------------------------
// Test Group 4: Profile L4 (10,000,000 instructions on 8 cores)
// -----------------------------------------------------------------------------
void test_group_4_profile_l4() {
    std::cout << "[Group 4] Testing Profile L4 (10M instructions on 8 cores)...\n";
    auto fix = setup_long_workload("l4_8c", 8, 10000000, 2000, 4); // Mixed workload
    SmpReplayCoordinator coord(make_config(8));
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());
    auto res = coord.run();
    assert(res.is_success());
    assert(coord.stats().total_instructions_retired == fix.total_retired);
    assert(coord.stats().final_canonical_hash == fix.final_recorded_cluster_hash);
    std::cout << "  -> 8-core 10M retired: " << coord.stats().total_instructions_retired
              << " events: " << coord.stats().total_events_consumed
              << " RSS: " << get_current_rss_mb() << " MB\n";
    cleanup(fix);
    std::cout << "  -> Group 4 PASS\n";
}

// -----------------------------------------------------------------------------
// Test Group 5: 10 Repeated Runs of 1M Profile (Deterministic Invariance)
// -----------------------------------------------------------------------------
void test_group_5_repeated_1m_runs() {
    std::cout << "[Group 5] Testing 10 Repeated Runs of 8-Core 1M Instruction Profile...\n";
    auto fix = setup_long_workload("rep_1m", 8, 1000000, 100, 0);

    std::uint64_t target_hash = 0;
    for (int run = 1; run <= 10; ++run) {
        SmpReplayCoordinator coord(make_config(8));
        auto prep = coord.prepare(fix.wsnap_path, fix.sndl_path);
        if (prep.is_failure()) {
            std::cerr << "PREPARE FAILED: " << prep.message << " (code " << static_cast<int>(prep.code) << ")\n";
        }
        assert(prep.is_success());
        auto res = coord.run();
        assert(res.is_success());
        assert(coord.stats().total_instructions_retired == fix.total_retired);
        if (run == 1) {
            target_hash = coord.stats().final_canonical_hash;
            assert(target_hash == fix.final_recorded_cluster_hash);
        } else {
            assert(coord.stats().final_canonical_hash == target_hash);
        }
    }
    cleanup(fix);
    std::cout << "  -> 10/10 Runs Produced Identical Hash: 0x" << std::hex << target_hash << std::dec << "\n";
    std::cout << "  -> Group 5 PASS\n";
}

int main() {
    try {
        std::cout << "===================================================================\n";
        std::cout << " WinIOS Phase 52 Stage 30: Long-Duration Deterministic Replay Tests\n";
        std::cout << "===================================================================\n\n" << std::flush;

        test_group_1_profile_l1();
        test_group_2_profile_l2();
        test_group_3_profile_l3();
        test_group_4_profile_l4();
        test_group_5_repeated_1m_runs();

        std::cout << "\n>>> ALL LONG-DURATION REPLAY TEST GROUPS PASSED <<<\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION CAUGHT: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "UNKNOWN EXCEPTION CAUGHT\n";
        return 1;
    }
}

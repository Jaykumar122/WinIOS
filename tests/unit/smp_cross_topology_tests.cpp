// =============================================================================
// WinIOS Phase 52 Stage 30: Cross-Topology Compatibility Tests
// =============================================================================

#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "cpu/smp_topology.h"
#include "replay/smp_replay_coordinator.h"
#include "snapshot/smp_snapshot_manager.h"
#include "sndl/sndl_v2_recorder.h"
#include "sndl/sndl_v2_writer.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace winios;
using namespace winios::replay;

namespace {

struct TopologyFixture {
    std::filesystem::path dir;
    std::filesystem::path wsnap_path;
    std::filesystem::path sndl_path;
    std::uint64_t initial_cluster_hash{0};
    std::size_t core_count{1};
};

TopologyFixture setup_fixture(const std::string& name, std::size_t cores) {
    TopologyFixture fix;
    fix.core_count = cores;
    fix.dir = std::filesystem::temp_directory_path() / ("winios_topo_" + name);
    std::filesystem::create_directories(fix.dir);
    fix.wsnap_path = fix.dir / "snap.wsnap";
    fix.sndl_path = fix.dir / "rec.sndl";

    const std::size_t ram = 1024 * 1024;
    auto mem = std::make_shared<SmpSharedMemory>(ram, cores);
    SmpScheduler sched(mem, cores, 20);

    for (std::size_t c = 0; c < cores; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x1000 + c * 0x200;
        mem->raw_memory().write_u32(code_addr + 0, 0x91000400u | (static_cast<std::uint32_t>(c) << 5) | static_cast<std::uint32_t>(c));
        mem->raw_memory().write_u32(code_addr + 4, 0x17FFFFFFu);
        cpu->set_program_counter(code_addr);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    assert(snapshot::SmpSnapshotManager::capture_to_file(sched, fix.wsnap_path).success);
    fix.initial_cluster_hash = sched.compute_canonical_multicore_hash();

    sndl::SndlV2RecorderOptions opts{
        static_cast<std::uint16_t>(cores),
        ram,
        fix.initial_cluster_hash,
        0, 0, 64
    };
    sndl::SndlV2Recorder recorder(opts);
    assert(recorder.start_recording(fix.sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);
    sched.run_deterministic(cores * 40);
    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    return fix;
}

void cleanup(const TopologyFixture& f) {
    std::error_code ec;
    std::filesystem::remove_all(f.dir, ec);
}

SmpReplayCapabilityConfig make_cfg(std::size_t max_cores = 8) {
    SmpReplayCapabilityConfig cfg;
    cfg.max_supported_cores = max_cores;
    cfg.allow_experimental_smp = true;
    cfg.allow_four_core_replay = true;
    cfg.allow_eight_core_replay = true;
    return cfg;
}

} // namespace

// -----------------------------------------------------------------------------
// Group 1: Valid Cross-Topology Pairs (1c, 2c, 4c, 8c)
// -----------------------------------------------------------------------------
void test_group_1_valid_topologies() {
    std::cout << "[Group 1] Testing Valid Topology Combinations (1c, 2c, 4c, 8c)...\n";
    for (std::size_t c : {1ULL, 2ULL, 4ULL, 8ULL}) {
        auto fix = setup_fixture("valid_" + std::to_string(c), c);
        SmpReplayCoordinator coord(make_cfg(c));
        auto prep = coord.prepare(fix.wsnap_path, fix.sndl_path);
        assert(prep.is_success());
        auto run = coord.run();
        assert(run.is_success());
        cleanup(fix);
        std::cout << "  -> " << c << "-core valid pair PASS\n";
    }
}

// -----------------------------------------------------------------------------
// Group 2: Invalid Snapshot/Recording Mismatches
// -----------------------------------------------------------------------------
void test_group_2_mismatched_topologies() {
    std::cout << "[Group 2] Testing Invalid Cross-Topology Pair Rejections...\n";
    auto fix1 = setup_fixture("mismatch_1", 1);
    auto fix2 = setup_fixture("mismatch_2", 2);
    auto fix4 = setup_fixture("mismatch_4", 4);
    auto fix8 = setup_fixture("mismatch_8", 8);

    // 1-core snapshot with 8-core recording
    {
        SmpReplayCoordinator coord(make_cfg());
        auto prep = coord.prepare(fix1.wsnap_path, fix8.sndl_path);
        assert(prep.is_failure());
        assert(prep.code == ReplayErrorCode::UnsupportedConfiguration);
        assert(coord.stats().total_instructions_retired == 0);
    }

    // 2-core snapshot with 4-core recording
    {
        SmpReplayCoordinator coord(make_cfg());
        auto prep = coord.prepare(fix2.wsnap_path, fix4.sndl_path);
        assert(prep.is_failure());
        assert(prep.code == ReplayErrorCode::UnsupportedConfiguration);
        assert(coord.stats().total_instructions_retired == 0);
    }

    // 4-core snapshot with 8-core recording
    {
        SmpReplayCoordinator coord(make_cfg());
        auto prep = coord.prepare(fix4.wsnap_path, fix8.sndl_path);
        assert(prep.is_failure());
        assert(prep.code == ReplayErrorCode::UnsupportedConfiguration);
        assert(coord.stats().total_instructions_retired == 0);
    }

    // 8-core snapshot with 4-core recording
    {
        SmpReplayCoordinator coord(make_cfg());
        auto prep = coord.prepare(fix8.wsnap_path, fix4.sndl_path);
        assert(prep.is_failure());
        assert(prep.code == ReplayErrorCode::UnsupportedConfiguration);
        assert(coord.stats().total_instructions_retired == 0);
    }

    cleanup(fix1);
    cleanup(fix2);
    cleanup(fix4);
    cleanup(fix8);
    std::cout << "  -> Group 2 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 3: Preparation Rejection Guarantees
// -----------------------------------------------------------------------------
void test_group_3_preparation_guarantees() {
    std::cout << "[Group 3] Testing Preparation Rejection Guarantees (Zero Guest Exec, Frozen Cursor)...\n";
    auto fix = setup_fixture("guar", 4);

    // Test rejection due to lack of 4-core capability
    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_four_core_replay = false; // Intentionally disabled!

    SmpReplayCoordinator coord(cfg);
    auto prep = coord.prepare(fix.wsnap_path, fix.sndl_path);
    assert(prep.is_failure());
    assert(prep.code == ReplayErrorCode::UnsupportedConfiguration);

    // Guarantees:
    assert(coord.state() == SmpReplayState::Unsupported);
    assert(coord.stats().total_instructions_retired == 0);
    assert(coord.stats().total_events_consumed == 0);

    cleanup(fix);
    std::cout << "  -> Group 3 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 4: Recording with Unsupported Topology Metadata (>8 cores)
// -----------------------------------------------------------------------------
void test_group_4_unsupported_topology_metadata() {
    std::cout << "[Group 4] Testing Rejection of >8 Core Metadata...\n";
    auto fix = setup_fixture("topo16", 8);

    // Write a 16-core header
    {
        sndl::WSndlV2Header hdr{};
        hdr.magic = sndl::kWSndlV2Magic;
        hdr.version = sndl::kWSndlVersion2;
        hdr.core_count = 16; // Unsupported!
        hdr.memory_capacity = 1024 * 1024;
        hdr.base_snapshot_hash = fix.initial_cluster_hash;
        hdr.initial_global_sequence = 1;

        sndl::SndlV2Writer writer;
        assert(writer.open(fix.sndl_path, hdr).is_success());
        assert(writer.finalize(sndl::SndlV2CompletionStatus::Completed).is_success());
    }

    SmpReplayCoordinator coord(make_cfg());
    auto prep = coord.prepare(fix.wsnap_path, fix.sndl_path);
    assert(prep.is_failure());
    assert(prep.code == ReplayErrorCode::UnsupportedConfiguration);
    assert(coord.stats().total_instructions_retired == 0);

    cleanup(fix);
    std::cout << "  -> Group 4 PASS\n";
}

int main() {
    std::cout << "===================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 30: Cross-Topology Compatibility Tests       \n";
    std::cout << "===================================================================\n\n";

    test_group_1_valid_topologies();
    test_group_2_mismatched_topologies();
    test_group_3_preparation_guarantees();
    test_group_4_unsupported_topology_metadata();

    std::cout << "\n>>> ALL CROSS-TOPOLOGY TEST GROUPS PASSED <<<\n";
    return 0;
}

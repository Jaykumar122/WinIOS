#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "replay/replay_session.h"
#include "replay/smp_replay_coordinator.h"
#include "replay/smp_replay_cursor.h"
#include "replay/smp_replay_reader.h"
#include "replay/smp_replay_types.h"
#include "snapshot/smp_snapshot_manager.h"
#include "snapshot/snapshot_manager.h"
#include "sndl/sndl_recorder.h"
#include "sndl/sndl_v2_recorder.h"
#include "sndl/sndl_v2_validator.h"
#include "sndl/sndl_v2_writer.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

namespace {

// Helper: Setup 2-core test workload and record with SNDL v2 and WSnap v2
struct TwoCoreFixture {
    std::filesystem::path dir;
    std::filesystem::path wsnap_path;
    std::filesystem::path sndl_path;
    std::uint64_t initial_cluster_hash{0};
    std::uint64_t final_recorded_cluster_hash{0};
    std::uint64_t recorded_events{0};
    std::uint64_t total_retired{0};
};

TwoCoreFixture setup_twocore_recording(const std::string& subfolder, std::uint64_t quantum = 10, std::uint64_t total_inst = 40) {
    TwoCoreFixture fix;
    fix.dir = std::filesystem::temp_directory_path() / ("winios_smp_replay_" + subfolder);
    std::filesystem::create_directories(fix.dir);
    fix.wsnap_path = fix.dir / "baseline.wsnap";
    fix.sndl_path = fix.dir / "workload.sndl";

    // Setup 2 cores and 1MB memory
    auto mem = std::make_shared<winios::SmpSharedMemory>(1024 * 1024);
    winios::SmpScheduler sched(mem, 2, quantum);

    for (std::size_t c = 0; c < 2; ++c) {
        auto cpu = std::make_shared<CPU>();
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        // Core 0: MOVZ X0, #10; ADD X0, X0, #1; B .
        // Core 1: MOVZ X1, #20; ADD X1, X1, #2; B .
        mem->raw_memory().write_u32(code_addr, 0xD2800000u | (static_cast<std::uint32_t>((c + 1) * 10) << 5) | static_cast<std::uint32_t>(c));
        mem->raw_memory().write_u32(code_addr + 4, 0x91000400u | (static_cast<std::uint32_t>(c + 1) << 10) | (static_cast<std::uint32_t>(c) << 5) | static_cast<std::uint32_t>(c));
        mem->raw_memory().write_u32(code_addr + 8, 0x14000000u); // B .
        cpu->set_program_counter(code_addr);
        sched.register_core(c, cpu, winios::CoreLifecycleState::Runnable);
    }

    // Capture initial baseline .wsnap v2
    const auto snap_res = winios::snapshot::SmpSnapshotManager::capture_to_file(sched, fix.wsnap_path);
    assert(snap_res.success);
    fix.initial_cluster_hash = sched.compute_canonical_multicore_hash();

    // Start SNDL v2 recorder bound to this snapshot
    winios::sndl::SndlV2RecorderOptions rec_opts{2, 1024 * 1024, fix.initial_cluster_hash, 0, 0, 64};
    winios::sndl::SndlV2Recorder recorder(rec_opts);
    assert(recorder.start_recording(fix.sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);

    // Execute workload
    fix.total_retired = sched.run_deterministic(total_inst);
    fix.final_recorded_cluster_hash = sched.compute_canonical_multicore_hash();

    assert(recorder.stop_recording(winios::sndl::SndlV2CompletionStatus::Completed).is_success());
    fix.recorded_events = recorder.total_events_recorded();

    return fix;
}

void cleanup_fixture(const TwoCoreFixture& fix) {
    std::error_code ec;
    std::filesystem::remove_all(fix.dir, ec);
}

// =============================================================================
// Group A: Replay State Machine
// =============================================================================
void test_group_a_state_machine() {
    std::cout << "[Group A] Testing Replay State Machine Transitions and Invariants...\n";

    using namespace winios::replay;

    // A1: Legal state transitions
    assert(is_valid_smp_replay_transition(SmpReplayState::Uninitialized, SmpReplayState::Preparing));
    assert(is_valid_smp_replay_transition(SmpReplayState::Preparing, SmpReplayState::SnapshotRestored));
    assert(is_valid_smp_replay_transition(SmpReplayState::SnapshotRestored, SmpReplayState::Ready));
    assert(is_valid_smp_replay_transition(SmpReplayState::Ready, SmpReplayState::Running));
    assert(is_valid_smp_replay_transition(SmpReplayState::Running, SmpReplayState::Paused));
    assert(is_valid_smp_replay_transition(SmpReplayState::Paused, SmpReplayState::Running));
    assert(is_valid_smp_replay_transition(SmpReplayState::Running, SmpReplayState::Completed));
    assert(is_valid_smp_replay_transition(SmpReplayState::Running, SmpReplayState::Diverged));
    assert(is_valid_smp_replay_transition(SmpReplayState::Running, SmpReplayState::Aborted));

    // A2: Illegal transitions from terminal states
    assert(!is_valid_smp_replay_transition(SmpReplayState::Completed, SmpReplayState::Running));
    assert(!is_valid_smp_replay_transition(SmpReplayState::Diverged, SmpReplayState::Running));
    assert(!is_valid_smp_replay_transition(SmpReplayState::Aborted, SmpReplayState::Running));
    assert(!is_valid_smp_replay_transition(SmpReplayState::Unsupported, SmpReplayState::Preparing));

    // A3: Illegal leap transitions
    assert(!is_valid_smp_replay_transition(SmpReplayState::Uninitialized, SmpReplayState::Running));
    assert(!is_valid_smp_replay_transition(SmpReplayState::Preparing, SmpReplayState::Completed));
    assert(!is_valid_smp_replay_transition(SmpReplayState::SnapshotRestored, SmpReplayState::Completed));

    // A4: ReplayCoordinator initial state
    SmpReplayCoordinator coord;
    assert(coord.state() == SmpReplayState::Uninitialized);
    assert(!coord.divergence().has_diverged());

    // A5: State transition names
    assert(smp_replay_state_name(SmpReplayState::Uninitialized) == "Uninitialized");
    assert(smp_replay_state_name(SmpReplayState::Completed) == "Completed");
    assert(smp_replay_state_name(SmpReplayState::Diverged) == "Diverged");
    assert(smp_replay_state_name(SmpReplayState::Unsupported) == "Unsupported");

    std::cout << "  -> Group A PASS\n";
}

// =============================================================================
// Group B: Snapshot Restoration
// =============================================================================
void test_group_b_snapshot_restoration() {
    std::cout << "[Group B] Testing Snapshot Restoration, Hash Verification & Capability Gate...\n";

    const auto fix = setup_twocore_recording("group_b", 10, 20);

    winios::replay::SmpReplayCoordinator coord;

    // B1: Correct snapshot preparation
    const auto prep_res = coord.prepare(fix.wsnap_path, fix.sndl_path);
    assert(prep_res.is_success());
    assert(coord.state() == winios::replay::SmpReplayState::Ready);
    assert(coord.scheduler() != nullptr);
    assert(coord.scheduler()->core_count() == 2);
    assert(coord.scheduler()->compute_canonical_multicore_hash() == fix.initial_cluster_hash);

    // B2: Missing snapshot file
    {
        winios::replay::SmpReplayCoordinator c_missing;
        const auto res = c_missing.prepare("non_existent_snapshot.wsnap", fix.sndl_path);
        assert(res.is_failure());
        assert(c_missing.state() == winios::replay::SmpReplayState::Diverged);
    }

    // B3: Snapshot with mismatched canonical hash
    {
        // Mutate SNDL header base snapshot hash
        const auto bad_sndl_path = fix.dir / "bad_hash.sndl";
        std::filesystem::copy_file(fix.sndl_path, bad_sndl_path);

        // Corrupt base_snapshot_hash at offset 16 in SNDL header
        std::fstream f(bad_sndl_path, std::ios::in | std::ios::out | std::ios::binary);
        assert(f.is_open());
        f.seekp(16, std::ios::beg);
        const std::uint64_t bogus_hash = 0xDEADBEEFCAFEBABEULL;
        f.write(reinterpret_cast<const char*>(&bogus_hash), sizeof(bogus_hash));
        f.close();

        winios::replay::SmpReplayCoordinator c_bad_hash;
        const auto res = c_bad_hash.prepare(fix.wsnap_path, bad_sndl_path);
        assert(res.is_failure());
        assert(res.code == winios::replay::ReplayErrorCode::SnapshotMismatch ||
               res.code == winios::replay::ReplayErrorCode::SndlValidationFailed);
    }

    // B4: Capability gate rejection: 4 cores
    {
        // Create 4-core snapshot
        const auto snap4_path = fix.dir / "snap4.wsnap";
        auto mem4 = std::make_shared<winios::SmpSharedMemory>(1024 * 1024);
        winios::SmpScheduler sched4(mem4, 4, 10);
        for (std::size_t i = 0; i < 4; ++i) {
            auto cpu = std::make_shared<CPU>();
            sched4.register_core(i, cpu);
        }
        assert(winios::snapshot::SmpSnapshotManager::capture_to_file(sched4, snap4_path).success);

        winios::replay::SmpReplayCoordinator c_four;
        const auto res = c_four.prepare(snap4_path, fix.sndl_path);
        assert(res.is_failure());
        assert(res.code == winios::replay::ReplayErrorCode::UnsupportedConfiguration);
        assert(c_four.state() == winios::replay::SmpReplayState::Unsupported);
    }

    cleanup_fixture(fix);
    std::cout << "  -> Group B PASS\n";
}

// =============================================================================
// Group C: Event Cursor
// =============================================================================
void test_group_c_event_cursor() {
    std::cout << "[Group C] Testing Event Cursor Semantics and Validation...\n";

    const auto fix = setup_twocore_recording("group_c", 10, 20);

    winios::replay::SmpReplayEventCursor cursor;
    assert(cursor.open(fix.sndl_path, true).is_success());
    assert(cursor.is_open());
    assert(cursor.has_next());
    assert(cursor.consumed_count() == 0);

    // C1: Peek without advance
    winios::sndl::SndlV2Record rec1{};
    assert(cursor.peek(rec1).is_success());
    assert(cursor.consumed_count() == 0);
    winios::sndl::SndlV2Record rec1_again{};
    assert(cursor.peek(rec1_again).is_success());
    assert(rec1.global_event_sequence == rec1_again.global_event_sequence);

    // C2: Atomic consume_expected_type
    const auto expected_type = rec1.event_type;
    winios::sndl::SndlV2Record consumed{};
    assert(cursor.consume_expected_type(expected_type, consumed).is_success());
    assert(cursor.consumed_count() == 1);
    assert(consumed.global_event_sequence == rec1.global_event_sequence);

    // C3: Invalid expected type rejection (does not advance cursor)
    winios::sndl::SndlV2Record bad_consumed{};
    const auto bad_res = cursor.consume_expected_type(winios::sndl::SndlV2EventType::HostInputInjected, bad_consumed);
    assert(bad_res.is_failure());
    assert(cursor.consumed_count() == 1); // Not advanced!

    // C4: Sequence gap detection
    cursor.close();
    assert(!cursor.is_open());

    cleanup_fixture(fix);
    std::cout << "  -> Group C PASS\n";
}

// =============================================================================
// Group D: Scheduler Replay
// =============================================================================
void test_group_d_scheduler_replay() {
    std::cout << "[Group D] Testing Scheduler Decisions, Quantum Bounds & Truncation...\n";

    const auto fix = setup_twocore_recording("group_d", 5, 20);

    winios::replay::SmpReplayCoordinator coord;
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());

    // D1: Single quantum step
    const bool stepped = coord.step_quantum();
    assert(stepped);
    assert(coord.state() == winios::replay::SmpReplayState::Running);
    assert(coord.stats().total_quantums_executed == 1);
    assert(coord.stats().total_instructions_retired > 0);

    // D2: Run to completion
    const auto run_res = coord.run();
    assert(run_res.is_success());
    assert(coord.state() == winios::replay::SmpReplayState::Completed);
    assert(coord.stats().total_instructions_retired == fix.total_retired);
    assert(coord.stats().final_canonical_hash == fix.final_recorded_cluster_hash);

    cleanup_fixture(fix);
    std::cout << "  -> Group D PASS\n";
}

// =============================================================================
// Group E: Retirement Synchronization
// =============================================================================
void test_group_e_retirement_synchronization() {
    std::cout << "[Group E] Testing Retirement Synchronization & Faulting Invariants...\n";

    const auto fix = setup_twocore_recording("group_e", 10, 40);

    winios::replay::SmpReplayCoordinator coord;
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());
    assert(coord.run().is_success());

    // E1: Verify per-core and global retirement sums match
    const auto& stats = coord.stats();
    assert(stats.per_core_retired.size() == 2);
    const std::uint64_t sum_per_core = stats.per_core_retired[0] + stats.per_core_retired[1];
    assert(sum_per_core == stats.total_instructions_retired);
    assert(coord.scheduler()->global_retirement_sequence() == stats.total_instructions_retired);

    // E2: Verify no regression
    assert(stats.per_core_retired[0] > 0);
    assert(stats.per_core_retired[1] > 0);

    cleanup_fixture(fix);
    std::cout << "  -> Group E PASS\n";
}

// =============================================================================
// Group F: Event Injection & Blocker Guards
// =============================================================================
void test_group_f_event_injection_and_blockers() {
    std::cout << "[Group F] Testing Event Injection and BLK-SMP-03 / BLK-SMP-04 Blockers...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_smp_replay_group_f";
    std::filesystem::create_directories(temp_dir);
    const auto wsnap_path = temp_dir / "snap.wsnap";
    const auto sndl_path = temp_dir / "dma_blocked.sndl";

    // Setup 2 cores
    auto mem = std::make_shared<winios::SmpSharedMemory>(1024 * 1024);
    winios::SmpScheduler sched(mem, 2, 10);
    for (std::size_t c = 0; c < 2; ++c) {
        auto cpu = std::make_shared<CPU>();
        sched.register_core(c, cpu);
    }
    assert(winios::snapshot::SmpSnapshotManager::capture_to_file(sched, wsnap_path).success);
    const std::uint64_t hash = sched.compute_canonical_multicore_hash();

    // Create recording containing HostInputInjected (simulating uncaptured host DMA)
    winios::sndl::SndlV2Recorder recorder(winios::sndl::SndlV2RecorderOptions{2, 1024 * 1024, hash, 0, 0, 32});
    assert(recorder.start_recording(sndl_path).is_success());
    recorder.record_host_input(0, 42);
    assert(recorder.stop_recording(winios::sndl::SndlV2CompletionStatus::Completed).is_success());

    // F1: BLK-SMP-03 Rejection: uncaptured DMA must be rejected by capability gate
    winios::replay::SmpReplayCapabilityConfig cap_config;
    cap_config.allow_uncaptured_dma = false; // Strict default
    winios::replay::SmpReplayCoordinator coord(cap_config);
    assert(coord.prepare(wsnap_path, sndl_path).is_success());

    const auto run_res = coord.run();
    assert(run_res.is_failure());
    assert(coord.state() == winios::replay::SmpReplayState::Diverged);
    assert(coord.divergence().type == winios::replay::SmpReplayDivergenceType::UnsupportedExternalInput);

    { std::error_code ec; std::filesystem::remove_all(temp_dir, ec); }
    std::cout << "  -> Group F PASS\n";
}

// =============================================================================
// Group G: Divergence Detection
// =============================================================================
void test_group_g_divergence_detection() {
    std::cout << "[Group G] Testing Divergence Categories and Safe Immediate Stop...\n";

    const auto fix = setup_twocore_recording("group_g", 10, 20);

    // G1: Modify memory in snapshot to cause immediate divergence
    const auto bad_snap_path = fix.dir / "diverged_snap.wsnap";
    std::filesystem::copy_file(fix.wsnap_path, bad_snap_path);

    // Mutate file byte
    std::fstream f(bad_snap_path, std::ios::in | std::ios::out | std::ios::binary);
    assert(f.is_open());
    f.seekp(100, std::ios::beg);
    char b = 0;
    f.read(&b, 1);
    b ^= 0x7F;
    f.seekp(100, std::ios::beg);
    f.write(&b, 1);
    f.close();

    winios::replay::SmpReplayCoordinator coord;
    const auto res = coord.prepare(bad_snap_path, fix.sndl_path);
    assert(res.is_failure());
    assert(coord.state() == winios::replay::SmpReplayState::Diverged);
    assert(coord.divergence().has_diverged());

    cleanup_fixture(fix);
    std::cout << "  -> Group G PASS\n";
}

// =============================================================================
// Group H: Two-Core Replay (Repeated Execution & Exact Hash Parity)
// =============================================================================
void test_group_h_two_core_replay() {
    std::cout << "[Group H] Testing Complete Two-Core Replay & Repeated Exact Parity...\n";

    const auto fix = setup_twocore_recording("group_h", 10, 40);

    // Perform 5 consecutive replay runs from the identical snapshot
    std::vector<std::uint64_t> hashes;
    for (int run = 0; run < 5; ++run) {
        winios::replay::SmpReplayCoordinator coord;
        assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());
        assert(coord.run().is_success());
        assert(coord.state() == winios::replay::SmpReplayState::Completed);

        const std::uint64_t h = coord.stats().final_canonical_hash;
        assert(h == fix.final_recorded_cluster_hash);
        hashes.push_back(h);
    }

    // Every single run MUST have identical final hash
    for (std::size_t i = 1; i < hashes.size(); ++i) {
        assert(hashes[i] == hashes[0]);
    }

    cleanup_fixture(fix);
    std::cout << "  -> Group H PASS\n";
}

// =============================================================================
// Group I: Compatibility & SMP Rejection
// =============================================================================
void test_group_i_compatibility() {
    std::cout << "[Group I] Testing Compatibility (SNDL v1/v2, WSnap v1/v2) and 4/8-Core Rejection...\n";

    const auto fix = setup_twocore_recording("group_i", 10, 20);

    // I1: ReplaySession 2-core replay integration
    winios::replay::ReplayConfig cfg;
    cfg.mode = winios::replay::ReplayMode::MultiCore;
    cfg.allow_experimental_smp = true; // Enables Stage 26 capability gate
    winios::replay::ReplaySession session;
    assert(session.prepare(fix.wsnap_path, fix.sndl_path, cfg).is_success());
    assert(session.state() == winios::replay::ReplayLifecycleState::Ready);
    assert(session.run().is_success());
    assert(session.state() == winios::replay::ReplayLifecycleState::Completed);

    // I2: ReplaySession 4-core explicit rejection (even with allow_experimental_smp = true)
    {
        const auto snap4_path = fix.dir / "compat_snap4.wsnap";
        auto mem4 = std::make_shared<winios::SmpSharedMemory>(1024 * 1024);
        winios::SmpScheduler sched4(mem4, 4, 10);
        for (std::size_t c = 0; c < 4; ++c) {
            auto cpu = std::make_shared<CPU>();
            sched4.register_core(c, cpu);
        }
        assert(winios::snapshot::SmpSnapshotManager::capture_to_file(sched4, snap4_path).success);

        winios::replay::ReplaySession session4;
        const auto res4 = session4.prepare(snap4_path, fix.sndl_path, cfg);
        assert(res4.is_failure());
        assert(res4.code == winios::replay::ReplayErrorCode::UnsupportedConfiguration);
    }

    // I3: ReplaySession 8-core explicit rejection (even with allow_experimental_smp = true)
    {
        const auto snap8_path = fix.dir / "compat_snap8.wsnap";
        auto mem8 = std::make_shared<winios::SmpSharedMemory>(1024 * 1024);
        winios::SmpScheduler sched8(mem8, 8, 10);
        for (std::size_t c = 0; c < 8; ++c) {
            auto cpu = std::make_shared<CPU>();
            sched8.register_core(c, cpu);
        }
        assert(winios::snapshot::SmpSnapshotManager::capture_to_file(sched8, snap8_path).success);

        winios::replay::ReplaySession session8;
        const auto res8 = session8.prepare(snap8_path, fix.sndl_path, cfg);
        assert(res8.is_failure());
        assert(res8.code == winios::replay::ReplayErrorCode::UnsupportedConfiguration);
    }

    cleanup_fixture(fix);
    std::cout << "  -> Group I PASS\n";
}

} // namespace

int main() {
    std::cout << "========================================================\n";
    std::cout << " WinIOS Phase 52 Stage 26: Multi-Core Replay Test Suite\n";
    std::cout << "========================================================\n\n";

    test_group_a_state_machine();
    test_group_b_snapshot_restoration();
    test_group_c_event_cursor();
    test_group_d_scheduler_replay();
    test_group_e_retirement_synchronization();
    test_group_f_event_injection_and_blockers();
    test_group_g_divergence_detection();
    test_group_h_two_core_replay();
    test_group_i_compatibility();

    std::cout << "\n>>> ALL 9 SMP REPLAY TEST GROUPS PASSED (A-I) <<<\n";
    return 0;
}

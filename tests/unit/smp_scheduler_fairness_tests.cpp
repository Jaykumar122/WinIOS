// =============================================================================
// WinIOS Phase 52 Stage 30: Scheduler Fairness & Starvation Analysis Tests
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
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace winios;
using namespace winios::replay;

namespace {

struct Fixture {
    std::filesystem::path dir;
    std::filesystem::path wsnap_path;
    std::filesystem::path sndl_path;
};

void cleanup(const Fixture& f) {
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
// Group 1: Equal Workload Round-Robin Fairness across 8 Cores
// -----------------------------------------------------------------------------
void test_group_1_equal_workload_fairness() {
    std::cout << "[Group 1] Testing Equal Workload Scheduling Fairness (8 Cores)...\n";
    Fixture fix;
    fix.dir = std::filesystem::temp_directory_path() / "winios_fair_grp1";
    std::filesystem::create_directories(fix.dir);
    fix.wsnap_path = fix.dir / "snap.wsnap";
    fix.sndl_path = fix.dir / "rec.sndl";

    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 8);
    const std::uint64_t quantum = 20;
    SmpScheduler sched(mem, 8, quantum);

    for (std::size_t c = 0; c < 8; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x1000 + c * 0x100;
        mem->raw_memory().write_u32(code_addr + 0, 0x91000400u | (static_cast<std::uint32_t>(c) << 5) | static_cast<std::uint32_t>(c));
        mem->raw_memory().write_u32(code_addr + 4, 0x17FFFFFFu);
        cpu->set_program_counter(code_addr);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    assert(snapshot::SmpSnapshotManager::capture_to_file(sched, fix.wsnap_path).success);
    const std::uint64_t init_hash = sched.compute_canonical_multicore_hash();

    sndl::SndlV2RecorderOptions opts{8, 1024 * 1024, init_hash, 0, 0, 64};
    sndl::SndlV2Recorder recorder(opts);
    assert(recorder.start_recording(fix.sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);

    // Run 1600 total instructions (exactly 10 slices of 20 insns per core = 200 per core)
    sched.run_deterministic(1600);
    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    // Replay and verify fairness metrics
    SmpReplayCoordinator coord(make_cfg());
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());
    assert(coord.run().is_success());

    for (std::size_t c = 0; c < 8; ++c) {
        assert(coord.stats().per_core_retired[c] == 200);
    }

    cleanup(fix);
    std::cout << "  -> Group 1 PASS (All 8 Cores Received Exactly 200 Instructions)\n";
}

// -----------------------------------------------------------------------------
// Group 2: 1 Busy Core and 7 Waiting Cores (WFI State Handling)
// -----------------------------------------------------------------------------
void test_group_2_waiting_cores_fairness() {
    std::cout << "[Group 2] Testing 1 Active Core & 7 Waiting Cores (WFI Ineligibility)...\n";
    Fixture fix;
    fix.dir = std::filesystem::temp_directory_path() / "winios_fair_grp2";
    std::filesystem::create_directories(fix.dir);
    fix.wsnap_path = fix.dir / "snap.wsnap";
    fix.sndl_path = fix.dir / "rec.sndl";

    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 8);
    SmpScheduler sched(mem, 8, 20);

    for (std::size_t c = 0; c < 8; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x1000 + c * 0x100;
        mem->raw_memory().write_u32(code_addr + 0, 0x91000400u | (static_cast<std::uint32_t>(c) << 5) | static_cast<std::uint32_t>(c));
        mem->raw_memory().write_u32(code_addr + 4, 0x17FFFFFFu);
        cpu->set_program_counter(code_addr);

        if (c == 0) {
            sched.register_core(c, cpu, CoreLifecycleState::Runnable);
        } else {
            // Cores 1..7 are in WaitingForInterrupt
            sched.register_core(c, cpu, CoreLifecycleState::WaitingForInterrupt);
        }
    }

    assert(snapshot::SmpSnapshotManager::capture_to_file(sched, fix.wsnap_path).success);
    const std::uint64_t init_hash = sched.compute_canonical_multicore_hash();

    sndl::SndlV2RecorderOptions opts{8, 1024 * 1024, init_hash, 0, 0, 64};
    sndl::SndlV2Recorder recorder(opts);
    assert(recorder.start_recording(fix.sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);

    // Run 200 instructions: All instructions should go to Core 0 without stalling on waiting cores
    sched.run_deterministic(200);
    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    SmpReplayCoordinator coord(make_cfg());
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());
    assert(coord.run().is_success());

    assert(coord.stats().per_core_retired[0] == 200);
    for (std::size_t c = 1; c < 8; ++c) {
        assert(coord.stats().per_core_retired[c] == 0);
    }

    cleanup(fix);
    std::cout << "  -> Group 2 PASS (Active Core Dispatched Smoothly; Waiting Cores Untouched)\n";
}

// -----------------------------------------------------------------------------
// Group 3: Core Wake-up & Dynamic Rescheduling
// -----------------------------------------------------------------------------
void test_group_3_core_wakeup() {
    std::cout << "[Group 3] Testing Core Wake-up from WFI and Schedulability Restoration...\n";
    Fixture fix;
    fix.dir = std::filesystem::temp_directory_path() / "winios_fair_grp3";
    std::filesystem::create_directories(fix.dir);
    fix.wsnap_path = fix.dir / "snap.wsnap";
    fix.sndl_path = fix.dir / "rec.sndl";

    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 8);
    SmpScheduler sched(mem, 8, 20);

    for (std::size_t c = 0; c < 8; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x1000 + c * 0x100;
        mem->raw_memory().write_u32(code_addr + 0, 0x91000400u | (static_cast<std::uint32_t>(c) << 5) | static_cast<std::uint32_t>(c));
        mem->raw_memory().write_u32(code_addr + 4, 0x17FFFFFFu);
        cpu->set_program_counter(code_addr);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    assert(snapshot::SmpSnapshotManager::capture_to_file(sched, fix.wsnap_path).success);
    const std::uint64_t init_hash = sched.compute_canonical_multicore_hash();

    sndl::SndlV2RecorderOptions opts{8, 1024 * 1024, init_hash, 0, 0, 64};
    sndl::SndlV2Recorder recorder(opts);
    assert(recorder.start_recording(fix.sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);

    // Put core 3 to WFI, run 160 instructions
    sched.get_slot(3).state.lifecycle = CoreLifecycleState::WaitingForInterrupt;
    sched.run_deterministic(160);

    // Wake core 3 back up to Runnable, run another 160 instructions
    sched.get_slot(3).state.lifecycle = CoreLifecycleState::Runnable;
    sched.run_deterministic(160);

    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    SmpReplayCoordinator coord(make_cfg());
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());
    assert(coord.run().is_success());

    assert(coord.stats().per_core_retired[3] > 0);
    cleanup(fix);
    std::cout << "  -> Group 3 PASS (Core Woke Up and Re-entered Round-Robin Order)\n";
}

int main() {
    std::cout << "===================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 30: Scheduler Fairness & Starvation Tests   \n";
    std::cout << "===================================================================\n\n";

    test_group_1_equal_workload_fairness();
    test_group_2_waiting_cores_fairness();
    test_group_3_core_wakeup();

    std::cout << "\n>>> ALL SCHEDULER FAIRNESS TEST GROUPS PASSED <<<\n";
    return 0;
}

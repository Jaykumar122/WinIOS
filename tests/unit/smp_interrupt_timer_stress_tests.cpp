// =============================================================================
// WinIOS Phase 52 Stage 30: Interrupt Storm & Simultaneous Timer Stress Tests
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

struct StressFixture {
    std::filesystem::path dir;
    std::filesystem::path wsnap_path;
    std::filesystem::path sndl_path;
    std::uint64_t initial_hash{0};
    std::uint64_t final_hash{0};
};

StressFixture setup_fixture(const std::string& name) {
    StressFixture fix;
    fix.dir = std::filesystem::temp_directory_path() / ("winios_storm_" + name);
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
    fix.initial_hash = sched.compute_canonical_multicore_hash();

    sndl::SndlV2RecorderOptions opts{8, 1024 * 1024, fix.initial_hash, 0, 0, 64};
    sndl::SndlV2Recorder recorder(opts);
    assert(recorder.start_recording(fix.sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);

    // Simulate sustained interrupt storm: send 50 SGIs across all cores
    for (std::uint32_t i = 0; i < 50; ++i) {
        const std::size_t src = i % 8;
        const std::size_t dst = (i + 1) % 8;
        const std::uint32_t sgi_id = i % 16;
        sched.interrupt_router().request_sgi(src, 1u << dst, sgi_id);
        recorder.record_external_interrupt_asserted(static_cast<std::uint32_t>(src), sgi_id, dst);
        recorder.record_interrupt_routed(sgi_id, src, dst);
        sched.run_deterministic(16);
    }

    // Simulate 8 simultaneous virtual timer expirations
    for (std::size_t c = 0; c < 8; ++c) {
        recorder.record_timer_expired(c, 0, 1000000);
    }
    sched.run_deterministic(80);

    fix.final_hash = sched.compute_canonical_multicore_hash();
    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    return fix;
}

void cleanup(const StressFixture& f) {
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
// Group 1: Interrupt Storm Execution & Replay
// -----------------------------------------------------------------------------
void test_group_1_interrupt_storm_replay() {
    std::cout << "[Group 1] Testing 50-Event Interrupt Storm Replay Across 8 Cores...\n";
    auto fix = setup_fixture("storm");

    SmpReplayCoordinator coord(make_cfg());
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());
    assert(coord.run().is_success());
    assert(coord.stats().final_canonical_hash == fix.final_hash);

    cleanup(fix);
    std::cout << "  -> Group 1 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 2: Repeated 10-Run Determinism of Storm & Timer Workload
// -----------------------------------------------------------------------------
void test_group_2_repeated_storm_runs() {
    std::cout << "[Group 2] Testing 10 Repeated Runs of Combined Storm & Timer Workload...\n";
    auto fix = setup_fixture("rep_storm");

    for (int run = 1; run <= 10; ++run) {
        SmpReplayCoordinator coord(make_cfg());
        assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());
        assert(coord.run().is_success());
        assert(coord.stats().final_canonical_hash == fix.final_hash);
    }

    cleanup(fix);
    std::cout << "  -> Group 2 PASS (10/10 Runs Bit-for-Bit Identical Final State)\n";
}

int main() {
    std::cout << "===================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 30: Interrupt Storm & Timer Stress Tests    \n";
    std::cout << "===================================================================\n\n";

    test_group_1_interrupt_storm_replay();
    test_group_2_repeated_storm_runs();

    std::cout << "\n>>> ALL INTERRUPT & TIMER STRESS TEST GROUPS PASSED <<<\n";
    return 0;
}

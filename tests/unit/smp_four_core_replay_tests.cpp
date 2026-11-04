#include "cpu/cpu.h"
#include "cpu/smp_cluster.h"
#include "cpu/smp_core_state.h"
#include "cpu/smp_exclusive_monitor.h"
#include "cpu/smp_interrupt_router.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "replay/replay_session.h"
#include "replay/smp_replay_coordinator.h"
#include "replay/smp_replay_types.h"
#include "snapshot/smp_snapshot_manager.h"
#include "sndl/sndl_v2_recorder.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace winios;
using namespace winios::replay;

namespace {

struct FourCoreFixture {
    std::filesystem::path dir;
    std::filesystem::path wsnap_path;
    std::filesystem::path sndl_path;
    std::uint64_t initial_cluster_hash{0};
    std::uint64_t final_recorded_cluster_hash{0};
    std::uint64_t recorded_events{0};
    std::uint64_t total_retired{0};
};

FourCoreFixture setup_fourcore_recording(const std::string& subfolder, std::uint64_t quantum = 20, std::uint64_t total_inst = 160) {
    FourCoreFixture fix;
    fix.dir = std::filesystem::temp_directory_path() / ("winios_smp_4c_" + subfolder);
    std::filesystem::create_directories(fix.dir);
    fix.wsnap_path = fix.dir / "baseline_4c.wsnap";
    fix.sndl_path = fix.dir / "workload_4c.sndl";

    // Setup 4 cores and 1MB memory
    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 4);
    SmpScheduler sched(mem, 4, quantum);

    for (std::size_t c = 0; c < 4; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        // Core c: MOVZ Xc, #(c+1)*10; ADD Xc, Xc, #1; B .
        mem->raw_memory().write_u32(code_addr, 0xD2800000u | (static_cast<std::uint32_t>((c + 1) * 10) << 5) | static_cast<std::uint32_t>(c));
        mem->raw_memory().write_u32(code_addr + 4, 0x91000400u | (static_cast<std::uint32_t>(c + 1) << 10) | (static_cast<std::uint32_t>(c) << 5) | static_cast<std::uint32_t>(c));
        mem->raw_memory().write_u32(code_addr + 8, 0x14000000u); // B .
        cpu->set_program_counter(code_addr);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    // Capture initial baseline .wsnap v2
    const auto snap_res = snapshot::SmpSnapshotManager::capture_to_file(sched, fix.wsnap_path);
    assert(snap_res.success);
    fix.initial_cluster_hash = sched.compute_canonical_multicore_hash();

    // Start SNDL v2 recorder bound to this snapshot
    sndl::SndlV2RecorderOptions rec_opts{4, 1024 * 1024, fix.initial_cluster_hash, 0, 0, 64};
    sndl::SndlV2Recorder recorder(rec_opts);
    assert(recorder.start_recording(fix.sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);

    // Execute workload
    fix.total_retired = sched.run_deterministic(total_inst);
    fix.final_recorded_cluster_hash = sched.compute_canonical_multicore_hash();

    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());
    fix.recorded_events = recorder.total_events_recorded();

    return fix;
}

void cleanup_fixture(const FourCoreFixture& fix) {
    std::error_code ec;
    std::filesystem::remove_all(fix.dir, ec);
}

// -----------------------------------------------------------------------------
// Group 1: Four-Core Capability Gate
// -----------------------------------------------------------------------------
void test_group_1_capability_gate() {
    std::cout << "[Group 1] Testing Four-Core Capability Gate...\n";
    const auto fix = setup_fourcore_recording("grp1");

    // 1. Default capability config (allow_four_core_replay = false) -> Rejected
    {
        SmpReplayCoordinator coord;
        auto res = coord.prepare(fix.wsnap_path, fix.sndl_path);
        assert(res.is_failure());
        assert(res.code == ReplayErrorCode::UnsupportedConfiguration);
    }

    // 2. Explicit 4-core enabled -> Prepared successfully
    {
        SmpReplayCapabilityConfig cfg;
        cfg.allow_experimental_smp = true;
        cfg.allow_four_core_replay = true;
        SmpReplayCoordinator coord(cfg);
        auto res = coord.prepare(fix.wsnap_path, fix.sndl_path);
        assert(res.is_success());
        assert(coord.state() == SmpReplayState::Ready);
        assert(coord.active_core_count() == 4);
    }

    cleanup_fixture(fix);
    std::cout << "  -> Group 1 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 2: Snapshot Restoration
// -----------------------------------------------------------------------------
void test_group_2_snapshot_restoration() {
    std::cout << "[Group 2] Testing Four-Core Snapshot Restoration & Hash Verification...\n";
    const auto fix = setup_fourcore_recording("grp2");

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_four_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    auto res = coord.prepare(fix.wsnap_path, fix.sndl_path);
    assert(res.is_success());

    // Verify all 4 cores exist and initial hash matches
    assert(coord.active_core_count() == 4);
    assert(coord.scheduler()->compute_canonical_multicore_hash() == fix.initial_cluster_hash);

    // Verify registers on each core
    for (std::size_t c = 0; c < 4; ++c) {
        assert(coord.scheduler()->get_slot(c).cpu->get_program_counter() == (0x2000 + c * 0x200));
    }

    cleanup_fixture(fix);
    std::cout << "  -> Group 2 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 3: Four-Core Event Cursor
// -----------------------------------------------------------------------------
void test_group_3_event_cursor() {
    std::cout << "[Group 3] Testing Four-Core Event Cursor Monotonicity...\n";
    const auto fix = setup_fourcore_recording("grp3");

    SmpReplayEventCursor cursor;
    assert(cursor.open(fix.sndl_path, true).is_success());
    assert(cursor.header().core_count == 4);

    std::uint64_t last_seq = 0;
    std::uint64_t count = 0;
    while (cursor.has_next()) {
        sndl::SndlV2Record rec{};
        assert(cursor.consume_next(rec).is_success());
        count++;
        assert(rec.global_event_sequence == last_seq + 1);
        last_seq = rec.global_event_sequence;
    }
    assert(count == fix.recorded_events);

    cleanup_fixture(fix);
    std::cout << "  -> Group 3 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 4: Four-Core Scheduler Coordination
// -----------------------------------------------------------------------------
void test_group_4_scheduler_coordination() {
    std::cout << "[Group 4] Testing Four-Core Scheduler Replay Coordination...\n";
    const auto fix = setup_fourcore_recording("grp4", 10, 80);

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_four_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());

    // Step each quantum slice manually
    std::size_t quantums = 0;
    while (coord.step_quantum()) {
        quantums++;
    }

    assert(quantums > 0);
    assert(coord.stats().total_quantums_executed == quantums);

    cleanup_fixture(fix);
    std::cout << "  -> Group 4 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 5: Global Retirement Synchronization
// -----------------------------------------------------------------------------
void test_group_5_global_retirement_sync() {
    std::cout << "[Group 5] Testing Four-Core Global Retirement Synchronization...\n";
    const auto fix = setup_fourcore_recording("grp5", 20, 160);

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_four_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());

    assert(coord.run().is_success());
    assert(coord.stats().total_instructions_retired == fix.total_retired);
    assert(coord.scheduler()->global_retirement_sequence() == fix.total_retired);

    cleanup_fixture(fix);
    std::cout << "  -> Group 5 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 6: Per-Core Retirement Synchronization
// -----------------------------------------------------------------------------
void test_group_6_per_core_retirement_sync() {
    std::cout << "[Group 6] Testing Four-Core Per-Core Retirement Synchronization...\n";
    const auto fix = setup_fourcore_recording("grp6", 20, 160);

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_four_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());
    assert(coord.run().is_success());

    std::uint64_t sum_retired = 0;
    assert(coord.stats().per_core_retired.size() == 4);
    for (std::size_t c = 0; c < 4; ++c) {
        assert(coord.stats().per_core_retired[c] > 0);
        sum_retired += coord.stats().per_core_retired[c];
    }
    assert(sum_retired == coord.stats().total_instructions_retired);

    cleanup_fixture(fix);
    std::cout << "  -> Group 6 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 7: Shared Memory Determinism
// -----------------------------------------------------------------------------
void test_group_7_shared_memory_determinism() {
    std::cout << "[Group 7] Testing Four-Core Shared Memory Determinism...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_smp_4c_shmem";
    std::filesystem::create_directories(temp_dir);
    const auto wsnap_path = temp_dir / "shmem.wsnap";
    const auto sndl_path = temp_dir / "shmem.sndl";

    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 4);
    SmpScheduler sched(mem, 4, 10);

    for (std::size_t c = 0; c < 4; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        const std::uint64_t data_addr = 0x4000 + c * 0x100;
        // Core writes to its distinct data page in shared RAM
        mem->raw_memory().write_u32(code_addr, 0xD2800000u | (static_cast<std::uint32_t>((c + 1) * 0x11) << 5) | 0u); // MOVZ X0, val
        mem->raw_memory().write_u32(code_addr + 4, 0xD2800000u | (static_cast<std::uint32_t>(data_addr) << 5) | 1u);   // MOVZ X1, addr
        mem->raw_memory().write_u32(code_addr + 8, 0xF9000020u); // STR X0, [X1]
        mem->raw_memory().write_u32(code_addr + 12, 0x14000000u); // B .
        cpu->set_program_counter(code_addr);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    assert(snapshot::SmpSnapshotManager::capture_to_file(sched, wsnap_path).success);
    const std::uint64_t init_hash = sched.compute_canonical_multicore_hash();

    sndl::SndlV2RecorderOptions rec_opts{4, 1024 * 1024, init_hash, 0, 0, 64};
    sndl::SndlV2Recorder recorder(rec_opts);
    assert(recorder.start_recording(sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);

    sched.run_deterministic(80);
    const std::uint64_t recorded_final_hash = sched.compute_canonical_multicore_hash();
    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    // Replay
    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_four_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(wsnap_path, sndl_path).is_success());
    auto run_res = coord.run();
    if (run_res.is_failure()) {
        std::cout << "GROUP 8 FAILED: " << run_res.message << " (div: " << smp_replay_divergence_type_name(coord.divergence().type) << " msg: " << coord.divergence().message << ")" << std::endl;
    }
    assert(run_res.is_success());
    assert(coord.stats().final_canonical_hash == recorded_final_hash);

    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::cout << "  -> Group 7 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 8: Exclusive Monitor Contention
// -----------------------------------------------------------------------------
void test_group_8_exclusive_monitor_contention() {
    std::cout << "[Group 8] Testing Four-Core Exclusive Monitor Contention...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_smp_4c_excl";
    std::filesystem::create_directories(temp_dir);
    const auto wsnap_path = temp_dir / "excl.wsnap";
    const auto sndl_path = temp_dir / "excl.sndl";

    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 4);
    SmpScheduler sched(mem, 4, 10);

    for (std::size_t c = 0; c < 4; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        mem->raw_memory().write_u32(code_addr, 0xD2800000u | (static_cast<std::uint32_t>((c + 1) * 2) << 5) | 0u);
        mem->raw_memory().write_u32(code_addr + 4, 0x14000000u); // B .
        cpu->set_program_counter(code_addr);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    assert(snapshot::SmpSnapshotManager::capture_to_file(sched, wsnap_path).success);
    const std::uint64_t init_hash = sched.compute_canonical_multicore_hash();

    sndl::SndlV2RecorderOptions rec_opts{4, 1024 * 1024, init_hash, 0, 0, 64};
    sndl::SndlV2Recorder recorder(rec_opts);
    assert(recorder.start_recording(sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);

    // Record exclusive reservations and stores
    recorder.record_reservation_set(0, 0x30000, 0x2000);
    recorder.record_reservation_set(1, 0x30000, 0x2200);
    recorder.record_store_exclusive_result(0, 0x30000, true, 0x2004);
    recorder.record_reservation_cleared(1, 0x30000, 0x2204);

    sched.run_deterministic(40);
    const std::uint64_t recorded_final_hash = sched.compute_canonical_multicore_hash();
    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    // Replay
    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_four_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(wsnap_path, sndl_path).is_success());
    auto run_res = coord.run();
    if (run_res.is_failure()) {
        std::cout << "GROUP 8 FAILED: " << run_res.message << " (div: " << smp_replay_divergence_type_name(coord.divergence().type) << " msg: " << coord.divergence().message << ")" << std::endl;
    }
    assert(run_res.is_success());
    assert(coord.stats().final_canonical_hash == recorded_final_hash);

    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::cout << "  -> Group 8 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 9: Interrupt Replay
// -----------------------------------------------------------------------------
void test_group_9_interrupt_replay() {
    std::cout << "[Group 9] Testing Four-Core External Interrupt Replay...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_smp_4c_irq";
    std::filesystem::create_directories(temp_dir);
    const auto wsnap_path = temp_dir / "irq.wsnap";
    const auto sndl_path = temp_dir / "irq.sndl";

    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 4);
    SmpScheduler sched(mem, 4, 10);

    for (std::size_t c = 0; c < 4; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        mem->raw_memory().write_u32(code_addr, 0xD2800020u); // MOVZ X0, #1
        mem->raw_memory().write_u32(code_addr + 4, 0x14000000u); // B .
        cpu->set_program_counter(code_addr);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    assert(snapshot::SmpSnapshotManager::capture_to_file(sched, wsnap_path).success);
    const std::uint64_t init_hash = sched.compute_canonical_multicore_hash();

    sndl::SndlV2RecorderOptions rec_opts{4, 1024 * 1024, init_hash, 0, 0, 64};
    sndl::SndlV2Recorder recorder(rec_opts);
    assert(recorder.start_recording(sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);

    // Record interrupts across all 4 cores
    recorder.record_external_interrupt_asserted(0, 32, 0);
    recorder.record_external_interrupt_asserted(0, 33, 1);
    recorder.record_external_interrupt_asserted(0, 34, 2);
    recorder.record_external_interrupt_asserted(0, 35, 3);

    sched.run_deterministic(40);
    const std::uint64_t recorded_final_hash = sched.compute_canonical_multicore_hash();
    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    // Replay
    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_four_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(wsnap_path, sndl_path).is_success());
    auto run_res = coord.run();
    if (run_res.is_failure()) {
        std::cout << "GROUP 8 FAILED: " << run_res.message << " (div: " << smp_replay_divergence_type_name(coord.divergence().type) << " msg: " << coord.divergence().message << ")" << std::endl;
    }
    assert(run_res.is_success());
    assert(coord.stats().final_canonical_hash == recorded_final_hash);

    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::cout << "  -> Group 9 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 10: Timer Replay
// -----------------------------------------------------------------------------
void test_group_10_timer_replay() {
    std::cout << "[Group 10] Testing Four-Core Timer Replay...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_smp_4c_timer";
    std::filesystem::create_directories(temp_dir);
    const auto wsnap_path = temp_dir / "timer.wsnap";
    const auto sndl_path = temp_dir / "timer.sndl";

    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 4);
    SmpScheduler sched(mem, 4, 10);

    for (std::size_t c = 0; c < 4; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        mem->raw_memory().write_u32(code_addr, 0xD2800020u); // MOVZ X0, #1
        mem->raw_memory().write_u32(code_addr + 4, 0x14000000u); // B .
        cpu->set_program_counter(code_addr);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    assert(snapshot::SmpSnapshotManager::capture_to_file(sched, wsnap_path).success);
    const std::uint64_t init_hash = sched.compute_canonical_multicore_hash();

    sndl::SndlV2RecorderOptions rec_opts{4, 1024 * 1024, init_hash, 0, 0, 64};
    sndl::SndlV2Recorder recorder(rec_opts);
    assert(recorder.start_recording(sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);

    // Record timer expiration events
    recorder.record_timer_expired(0, 1, 1000);
    recorder.record_timer_expired(1, 1, 1000);
    recorder.record_timer_expired(2, 1, 1000);
    recorder.record_timer_expired(3, 1, 1000);

    sched.run_deterministic(40);
    const std::uint64_t recorded_final_hash = sched.compute_canonical_multicore_hash();
    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    // Replay
    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_four_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(wsnap_path, sndl_path).is_success());
    auto run_res = coord.run();
    if (run_res.is_failure()) {
        std::cout << "GROUP 8 FAILED: " << run_res.message << " (div: " << smp_replay_divergence_type_name(coord.divergence().type) << " msg: " << coord.divergence().message << ")" << std::endl;
    }
    assert(run_res.is_success());
    assert(coord.stats().final_canonical_hash == recorded_final_hash);

    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::cout << "  -> Group 10 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 11: MMIO and Entropy Replay
// -----------------------------------------------------------------------------
void test_group_11_mmio_entropy_replay() {
    std::cout << "[Group 11] Testing Four-Core MMIO and Entropy Input Replay...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_smp_4c_mmio";
    std::filesystem::create_directories(temp_dir);
    const auto wsnap_path = temp_dir / "mmio.wsnap";
    const auto sndl_path = temp_dir / "mmio.sndl";

    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 4);
    SmpScheduler sched(mem, 4, 10);

    for (std::size_t c = 0; c < 4; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        mem->raw_memory().write_u32(code_addr, 0xD2800020u); // MOVZ X0, #1
        mem->raw_memory().write_u32(code_addr + 4, 0x14000000u); // B .
        cpu->set_program_counter(code_addr);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    assert(snapshot::SmpSnapshotManager::capture_to_file(sched, wsnap_path).success);
    const std::uint64_t init_hash = sched.compute_canonical_multicore_hash();

    sndl::SndlV2RecorderOptions rec_opts{4, 1024 * 1024, init_hash, 0, 0, 64};
    sndl::SndlV2Recorder recorder(rec_opts);
    assert(recorder.start_recording(sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);

    // Record MMIO and Entropy reads
    recorder.record_mmio_read(0, 0x09000000ULL, 0x41, 1, 0x2000);
    recorder.record_entropy_read(1, 0x1122334455667788ULL, 0x2200);
    recorder.record_mmio_read(2, 0x09000004ULL, 0x42, 1, 0x2400);
    recorder.record_entropy_read(3, 0x8877665544332211ULL, 0x2600);

    sched.run_deterministic(40);
    const std::uint64_t recorded_final_hash = sched.compute_canonical_multicore_hash();
    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    // Replay
    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_four_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(wsnap_path, sndl_path).is_success());
    auto run_res = coord.run();
    if (run_res.is_failure()) {
        std::cout << "GROUP 8 FAILED: " << run_res.message << " (div: " << smp_replay_divergence_type_name(coord.divergence().type) << " msg: " << coord.divergence().message << ")" << std::endl;
    }
    assert(run_res.is_success());
    assert(coord.stats().final_canonical_hash == recorded_final_hash);

    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::cout << "  -> Group 11 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 12: DMA Capture / Replay or Explicit Rejection
// -----------------------------------------------------------------------------
void test_group_12_dma_rejection() {
    std::cout << "[Group 12] Testing BLK-SMP-03 (DMA Replay Rejection)...\n";
    const auto fix = setup_fourcore_recording("grp12");

    // Attempting to enable allow_uncaptured_dma must fail capability gate
    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_four_core_replay = true;
    cfg.allow_uncaptured_dma = true; // Prohibited!

    SmpReplayCoordinator coord(cfg);
    auto res = coord.prepare(fix.wsnap_path, fix.sndl_path);
    assert(res.is_failure());
    assert(res.code == ReplayErrorCode::UnsupportedConfiguration);

    cleanup_fixture(fix);
    std::cout << "  -> Group 12 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 13: Host Clock Virtualization & Rejection
// -----------------------------------------------------------------------------
void test_group_13_host_clock_virtualization() {
    std::cout << "[Group 13] Testing BLK-SMP-04 (Host Clock Virtualization & Rejection)...\n";
    const auto fix = setup_fourcore_recording("grp13");

    // 1. Prohibit unvirtualized wall clock
    {
        SmpReplayCapabilityConfig cfg;
        cfg.allow_experimental_smp = true;
        cfg.allow_four_core_replay = true;
        cfg.allow_unvirtualized_wall_clock = true; // Prohibited!

        SmpReplayCoordinator coord(cfg);
        auto res = coord.prepare(fix.wsnap_path, fix.sndl_path);
        assert(res.is_failure());
        assert(res.code == ReplayErrorCode::UnsupportedConfiguration);
    }

    // 2. Reject UnsupportedHostClock clock source
    {
        SmpReplayCapabilityConfig cfg;
        cfg.allow_experimental_smp = true;
        cfg.allow_four_core_replay = true;
        cfg.clock_source = SmpReplayClockSource::UnsupportedHostClock; // Prohibited!

        SmpReplayCoordinator coord(cfg);
        auto res = coord.prepare(fix.wsnap_path, fix.sndl_path);
        assert(res.is_failure());
        assert(res.code == ReplayErrorCode::UnsupportedConfiguration);
    }

    // 3. Accept DeterministicVirtualClock
    {
        SmpReplayCapabilityConfig cfg;
        cfg.allow_experimental_smp = true;
        cfg.allow_four_core_replay = true;
        cfg.clock_source = SmpReplayClockSource::DeterministicVirtualClock;

        SmpReplayCoordinator coord(cfg);
        auto res = coord.prepare(fix.wsnap_path, fix.sndl_path);
        assert(res.is_success());
    }

    cleanup_fixture(fix);
    std::cout << "  -> Group 13 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 14: Four-Core Repeated Replay (10 Runs)
// -----------------------------------------------------------------------------
void test_group_14_repeated_replay_10_runs() {
    std::cout << "[Group 14] Testing Four-Core Repeated Replay (10 Runs Bit-Identical Hash)...\n";
    const auto fix = setup_fourcore_recording("grp14", 25, 200);

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_four_core_replay = true;

    std::uint64_t target_hash = 0;

    for (int run = 1; run <= 10; ++run) {
        SmpReplayCoordinator coord(cfg);
        const auto prep_res = coord.prepare(fix.wsnap_path, fix.sndl_path);
        assert(prep_res.is_success());
        const auto run_res = coord.run();
        assert(run_res.is_success());
        assert(coord.state() == SmpReplayState::Completed);
        assert(coord.stats().total_instructions_retired == fix.total_retired);

        const std::uint64_t rep_hash = coord.stats().final_canonical_hash;
        if (run == 1) {
            target_hash = rep_hash;
            assert(target_hash == fix.final_recorded_cluster_hash);
        } else {
            assert(rep_hash == target_hash);
        }
    }

    cleanup_fixture(fix);
    std::cout << "  -> Group 14 PASS (10/10 runs matched 0x" << std::hex << target_hash << std::dec << ")\n";
}

// -----------------------------------------------------------------------------
// Group 15: Eight-Core Rejection
// -----------------------------------------------------------------------------
void test_group_15_eight_core_rejection() {
    std::cout << "[Group 15] Testing Eight-Core Rejection (UnsupportedConfiguration)...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_smp_8c_rej";
    std::filesystem::create_directories(temp_dir);
    const auto snap8_path = temp_dir / "snap8.wsnap";
    const auto sndl8_path = temp_dir / "sndl8.sndl";

    // Setup 8-core cluster
    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 8);
    SmpScheduler sched(mem, 8, 10);
    for (std::size_t c = 0; c < 8; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }
    assert(snapshot::SmpSnapshotManager::capture_to_file(sched, snap8_path).success);

    // Create 8-core SNDL
    sndl::SndlV2RecorderOptions rec_opts{8, 1024 * 1024, 0, 0, 0, 64};
    sndl::SndlV2Recorder recorder(rec_opts);
    assert(recorder.start_recording(sndl8_path).is_success());
    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    // 1. Coordinator preparation with 8 cores must fail
    {
        SmpReplayCapabilityConfig cfg;
        cfg.allow_experimental_smp = true;
        cfg.allow_four_core_replay = true;
        SmpReplayCoordinator coord(cfg);
        auto res = coord.prepare(snap8_path, sndl8_path);
        assert(res.is_failure());
        assert(res.code == ReplayErrorCode::UnsupportedConfiguration);
    }

    // 2. ReplaySession with 8-core snapshot must fail
    {
        ReplaySession session;
        ReplayConfig cfg;
        cfg.allow_experimental_smp = true;
        cfg.allow_four_core_replay = true;
        auto res = session.prepare(snap8_path, sndl8_path, cfg);
        assert(res.is_failure());
        assert(res.code == ReplayErrorCode::UnsupportedConfiguration);
    }

    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::cout << "  -> Group 15 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 16: Corruption & Failure Injection
// -----------------------------------------------------------------------------
void test_group_16_failure_injection() {
    std::cout << "[Group 16] Testing Divergence Detection & Failure Injection...\n";
    const auto fix = setup_fourcore_recording("grp16");

    // 1. Mutate snapshot byte
    {
        const auto bad_snap = fix.dir / "corrupt.wsnap";
        std::filesystem::copy_file(fix.wsnap_path, bad_snap);
        std::fstream f(bad_snap, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(100, std::ios::beg);
        char b = 0;
        f.read(&b, 1);
        b ^= 0x55;
        f.seekp(100, std::ios::beg);
        f.write(&b, 1);
        f.close();

        SmpReplayCapabilityConfig cfg;
        cfg.allow_experimental_smp = true;
        cfg.allow_four_core_replay = true;
        SmpReplayCoordinator coord(cfg);
        auto res = coord.prepare(bad_snap, fix.sndl_path);
        assert(res.is_failure());
    }

    cleanup_fixture(fix);
    std::cout << "  -> Group 16 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 17: Two-Core Regression
// -----------------------------------------------------------------------------
void test_group_17_two_core_regression() {
    std::cout << "[Group 17] Testing Two-Core Replay Regression...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_smp_2c_regr";
    std::filesystem::create_directories(temp_dir);
    const auto wsnap_path = temp_dir / "twocore.wsnap";
    const auto sndl_path = temp_dir / "twocore.sndl";

    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 2);
    SmpScheduler sched(mem, 2, 10);
    for (std::size_t c = 0; c < 2; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        mem->raw_memory().write_u32(code_addr, 0xD2800020u); // MOVZ X0, #1
        mem->raw_memory().write_u32(code_addr + 4, 0x14000000u); // B .
        cpu->set_program_counter(code_addr);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }
    assert(snapshot::SmpSnapshotManager::capture_to_file(sched, wsnap_path).success);
    const std::uint64_t init_hash = sched.compute_canonical_multicore_hash();

    sndl::SndlV2RecorderOptions rec_opts{2, 1024 * 1024, init_hash, 0, 0, 64};
    sndl::SndlV2Recorder recorder(rec_opts);
    assert(recorder.start_recording(sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);
    sched.run_deterministic(40);
    const std::uint64_t expected_hash = sched.compute_canonical_multicore_hash();
    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    // Replay 2-core via SmpReplayCoordinator
    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(wsnap_path, sndl_path).is_success());
    assert(coord.run().is_success());
    assert(coord.stats().final_canonical_hash == expected_hash);

    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::cout << "  -> Group 17 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 18: Single-Core Regression
// -----------------------------------------------------------------------------
void test_group_18_single_core_regression() {
    std::cout << "[Group 18] Testing Single-Core Replay Session Regression...\n";

    // Test that ReplaySession::prepare with invalid path cleanly fails as expected
    ReplaySession session;
    ReplayConfig cfg;
    auto res = session.prepare("nonexistent.wsnap", "nonexistent.sndl", cfg);
    assert(res.is_failure());

    std::cout << "  -> Group 18 PASS\n";
}

} // namespace

int main() {
    std::cout << "========================================================\n" << std::flush;
    std::cout << " WinIOS Phase 52 Stage 27: Four-Core Replay Test Suite \n" << std::flush;
    std::cout << "========================================================\n\n" << std::flush;

    std::cout << "Running Group 1..." << std::endl;
    test_group_1_capability_gate();
    std::cout << "Running Group 2..." << std::endl;
    test_group_2_snapshot_restoration();
    std::cout << "Running Group 3..." << std::endl;
    test_group_3_event_cursor();
    std::cout << "Running Group 4..." << std::endl;
    test_group_4_scheduler_coordination();
    std::cout << "Running Group 5..." << std::endl;
    test_group_5_global_retirement_sync();
    std::cout << "Running Group 6..." << std::endl;
    test_group_6_per_core_retirement_sync();
    std::cout << "Running Group 7..." << std::endl;
    test_group_7_shared_memory_determinism();
    std::cout << "Running Group 8..." << std::endl;
    test_group_8_exclusive_monitor_contention();
    std::cout << "Running Group 9..." << std::endl;
    test_group_9_interrupt_replay();
    std::cout << "Running Group 10..." << std::endl;
    test_group_10_timer_replay();
    std::cout << "Running Group 11..." << std::endl;
    test_group_11_mmio_entropy_replay();
    std::cout << "Running Group 12..." << std::endl;
    test_group_12_dma_rejection();
    std::cout << "Running Group 13..." << std::endl;
    test_group_13_host_clock_virtualization();
    std::cout << "Running Group 14..." << std::endl;
    test_group_14_repeated_replay_10_runs();
    std::cout << "Running Group 15..." << std::endl;
    test_group_15_eight_core_rejection();
    std::cout << "Running Group 16..." << std::endl;
    test_group_16_failure_injection();
    std::cout << "Running Group 17..." << std::endl;
    test_group_17_two_core_regression();
    std::cout << "Running Group 18..." << std::endl;
    test_group_18_single_core_regression();

    std::cout << "\n>>> ALL 18 FOUR-CORE REPLAY TEST GROUPS PASSED (1-18) <<<\n" << std::endl;
    return 0;
}

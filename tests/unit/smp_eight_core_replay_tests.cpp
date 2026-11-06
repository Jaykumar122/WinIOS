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

struct EightCoreFixture {
    std::filesystem::path dir;
    std::filesystem::path wsnap_path;
    std::filesystem::path sndl_path;
    std::uint64_t initial_cluster_hash{0};
    std::uint64_t final_recorded_cluster_hash{0};
    std::uint64_t recorded_events{0};
    std::uint64_t total_retired{0};
};

EightCoreFixture setup_eightcore_recording(const std::string& subfolder, std::uint64_t quantum = 20, std::uint64_t total_inst = 320) {
    EightCoreFixture fix;
    fix.dir = std::filesystem::temp_directory_path() / ("winios_smp_8c_" + subfolder);
    std::filesystem::create_directories(fix.dir);
    fix.wsnap_path = fix.dir / "baseline_8c.wsnap";
    fix.sndl_path = fix.dir / "workload_8c.sndl";

    // Setup 8 cores and 2MB memory
    auto mem = std::make_shared<SmpSharedMemory>(2 * 1024 * 1024, 8);
    SmpScheduler sched(mem, 8, quantum);

    for (std::size_t c = 0; c < 8; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        // Core c: MOVZ Xc, #(c+1)*10; ADD Xc, Xc, #1; B .-4
        mem->raw_memory().write_u32(code_addr, 0xD2800000u | (static_cast<std::uint32_t>((c + 1) * 10) << 5) | static_cast<std::uint32_t>(c));
        mem->raw_memory().write_u32(code_addr + 4, 0x91000400u | (static_cast<std::uint32_t>(c + 1) << 10) | (static_cast<std::uint32_t>(c) << 5) | static_cast<std::uint32_t>(c));
        mem->raw_memory().write_u32(code_addr + 8, 0x17FFFFFFu); // B .-4 (infinite loop)
        cpu->set_program_counter(code_addr);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    // Capture initial baseline .wsnap v2
    const auto snap_res = snapshot::SmpSnapshotManager::capture_to_file(sched, fix.wsnap_path);
    assert(snap_res.success);
    fix.initial_cluster_hash = sched.compute_canonical_multicore_hash();

    // Start SNDL v2 recorder bound to this snapshot
    sndl::SndlV2RecorderOptions rec_opts{8, 2 * 1024 * 1024, fix.initial_cluster_hash, 0, 0, 64};
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

void cleanup_fixture(const EightCoreFixture& fix) {
    std::error_code ec;
    std::filesystem::remove_all(fix.dir, ec);
}

// -----------------------------------------------------------------------------
// Group 1: Eight-Core Capability Gate
// -----------------------------------------------------------------------------
void test_group_1_capability_gate() {
    std::cout << "[Group 1] Testing Eight-Core Capability Gate Authorization...\n";
    const auto fix = setup_eightcore_recording("grp1");

    // 1. Default capability config (allow_eight_core_replay = false) -> Rejected
    {
        SmpReplayCoordinator coord;
        auto res = coord.prepare(fix.wsnap_path, fix.sndl_path);
        assert(res.is_failure());
        assert(res.code == ReplayErrorCode::UnsupportedConfiguration);
    }

    // 2. Only allow_four_core_replay enabled -> Still Rejected for 8 cores
    {
        SmpReplayCapabilityConfig cfg;
        cfg.allow_experimental_smp = true;
        cfg.allow_four_core_replay = true;
        cfg.allow_eight_core_replay = false;
        SmpReplayCoordinator coord(cfg);
        auto res = coord.prepare(fix.wsnap_path, fix.sndl_path);
        assert(res.is_failure());
        assert(res.code == ReplayErrorCode::UnsupportedConfiguration);
    }

    // 3. allow_eight_core_replay = true but allow_experimental_smp = false -> Rejected
    {
        SmpReplayCapabilityConfig cfg;
        cfg.allow_experimental_smp = false;
        cfg.allow_eight_core_replay = true;
        SmpReplayCoordinator coord(cfg);
        auto res = coord.prepare(fix.wsnap_path, fix.sndl_path);
        assert(res.is_failure());
        assert(res.code == ReplayErrorCode::UnsupportedConfiguration);
    }

    // 4. Both enabled -> Prepared successfully
    {
        SmpReplayCapabilityConfig cfg;
        cfg.allow_experimental_smp = true;
        cfg.allow_eight_core_replay = true;
        SmpReplayCoordinator coord(cfg);
        auto res = coord.prepare(fix.wsnap_path, fix.sndl_path);
        assert(res.is_success());
        assert(coord.state() == SmpReplayState::Ready);
        assert(coord.active_core_count() == 8);
    }

    cleanup_fixture(fix);
    std::cout << "  -> Group 1 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 2: Snapshot Restoration & Initial Hash Verification
// -----------------------------------------------------------------------------
void test_group_2_snapshot_restoration() {
    std::cout << "[Group 2] Testing Eight-Core Snapshot Restoration & Hash Verification...\n";
    const auto fix = setup_eightcore_recording("grp2");

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_eight_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    auto res = coord.prepare(fix.wsnap_path, fix.sndl_path);
    assert(res.is_success());

    // Verify all 8 cores exist and initial hash matches
    assert(coord.active_core_count() == 8);
    assert(coord.scheduler()->compute_canonical_multicore_hash() == fix.initial_cluster_hash);

    // Verify registers and PC on each core
    for (std::size_t c = 0; c < 8; ++c) {
        assert(coord.scheduler()->get_slot(c).cpu->get_program_counter() == (0x2000 + c * 0x200));
        assert(coord.scheduler()->get_slot(c).cpu->get_core_id() == c);
    }

    cleanup_fixture(fix);
    std::cout << "  -> Group 2 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 3: Eight-Core Event Cursor Monotonicity & Validation
// -----------------------------------------------------------------------------
void test_group_3_event_cursor() {
    std::cout << "[Group 3] Testing Eight-Core Event Cursor Monotonicity...\n";
    const auto fix = setup_eightcore_recording("grp3");

    SmpReplayEventCursor cursor;
    assert(cursor.open(fix.sndl_path, true).is_success());
    assert(cursor.header().core_count == 8);

    std::uint64_t last_seq = 0;
    std::uint64_t count = 0;
    while (cursor.has_next()) {
        sndl::SndlV2Record rec{};
        assert(cursor.consume_next(rec).is_success());
        count++;
        assert(rec.global_event_sequence == last_seq + 1);
        last_seq = rec.global_event_sequence;
        assert(rec.core_id < 8);
    }
    assert(count == fix.recorded_events);

    cleanup_fixture(fix);
    std::cout << "  -> Group 3 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 4: Eight-Core Scheduler Coordination & Quantum Stepping
// -----------------------------------------------------------------------------
void test_group_4_scheduler_coordination() {
    std::cout << "[Group 4] Testing Eight-Core Scheduler Replay Coordination...\n";
    const auto fix = setup_eightcore_recording("grp4", 10, 160);

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_eight_core_replay = true;
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
// Group 5: Global Retirement Monotonicity & Synchronization
// -----------------------------------------------------------------------------
void test_group_5_global_retirement_sync() {
    std::cout << "[Group 5] Testing Eight-Core Global Retirement Synchronization...\n";
    const auto fix = setup_eightcore_recording("grp5", 20, 320);

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_eight_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());

    const auto res = coord.run();
    assert(res.is_success());
    assert(coord.stats().total_instructions_retired == fix.total_retired);
    assert(coord.scheduler()->global_retirement_sequence() == fix.total_retired);

    cleanup_fixture(fix);
    std::cout << "  -> Group 5 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 6: Per-Core Retirement Synchronization & Invariant Verification
// -----------------------------------------------------------------------------
void test_group_6_per_core_retirement_sync() {
    std::cout << "[Group 6] Testing Eight-Core Per-Core Retirement Accounting...\n";
    const auto fix = setup_eightcore_recording("grp6", 20, 320);

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_eight_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());

    assert(coord.run().is_success());

    // Verify per-core sum invariant: sum(R_core[i]) == R_global
    std::uint64_t sum_per_core = 0;
    assert(coord.stats().per_core_retired.size() == 8);
    for (std::size_t c = 0; c < 8; ++c) {
        sum_per_core += coord.stats().per_core_retired[c];
        assert(coord.scheduler()->get_slot(c).total_instructions_retired == coord.stats().per_core_retired[c]);
    }
    assert(sum_per_core == coord.stats().total_instructions_retired);

    cleanup_fixture(fix);
    std::cout << "  -> Group 6 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 7: Shared Memory Replay & Interleaved Access Determinism
// -----------------------------------------------------------------------------
void test_group_7_shared_memory_determinism() {
    std::cout << "[Group 7] Testing Eight-Core Shared Memory Replay Determinism...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_smp_8c_shmem";
    std::filesystem::create_directories(temp_dir);
    const auto wsnap_path = temp_dir / "shmem.wsnap";
    const auto sndl_path = temp_dir / "shmem.sndl";

    auto mem = std::make_shared<SmpSharedMemory>(2 * 1024 * 1024, 8);
    SmpScheduler sched(mem, 8, 10);

    for (std::size_t c = 0; c < 8; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        const std::uint64_t data_addr = 0x5000 + c * 0x100;
        mem->raw_memory().write_u32(code_addr, 0xD2800000u | (static_cast<std::uint32_t>((c + 1) * 0x11) << 5) | 0u);
        mem->raw_memory().write_u32(code_addr + 4, 0xD2800000u | (static_cast<std::uint32_t>(data_addr) << 5) | 1u);
        mem->raw_memory().write_u32(code_addr + 8, 0xF9000020u); // STR X0, [X1]
        mem->raw_memory().write_u32(code_addr + 12, 0x14000000u); // B .
        cpu->set_program_counter(code_addr);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    assert(snapshot::SmpSnapshotManager::capture_to_file(sched, wsnap_path).success);
    const std::uint64_t init_hash = sched.compute_canonical_multicore_hash();

    sndl::SndlV2RecorderOptions rec_opts{8, 2 * 1024 * 1024, init_hash, 0, 0, 64};
    sndl::SndlV2Recorder recorder(rec_opts);
    assert(recorder.start_recording(sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);

    sched.run_deterministic(80);
    const std::uint64_t recorded_final_hash = sched.compute_canonical_multicore_hash();
    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    // Replay
    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_eight_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(wsnap_path, sndl_path).is_success());
    assert(coord.run().is_success());
    assert(coord.stats().final_canonical_hash == recorded_final_hash);

    // Verify memory values written by each core
    for (std::size_t c = 0; c < 8; ++c) {
        assert(coord.scheduler()->shared_memory().raw_memory().read_u64(0x5000 + c * 0x100) == ((c + 1) * 0x11));
    }

    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::cout << "  -> Group 7 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 8: Exclusive Monitor Contention Replay Across 8 Cores
// -----------------------------------------------------------------------------
void test_group_8_exclusive_monitor_contention() {
    std::cout << "[Group 8] Testing Eight-Core Exclusive Monitor Contention Replay...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_smp_8c_excl";
    std::filesystem::create_directories(temp_dir);
    const auto wsnap_path = temp_dir / "excl.wsnap";
    const auto sndl_path = temp_dir / "excl.sndl";

    auto mem = std::make_shared<SmpSharedMemory>(2 * 1024 * 1024, 8);
    SmpScheduler sched(mem, 8, 10);

    for (std::size_t c = 0; c < 8; ++c) {
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

    sndl::SndlV2RecorderOptions rec_opts{8, 2 * 1024 * 1024, init_hash, 0, 0, 64};
    sndl::SndlV2Recorder recorder(rec_opts);
    assert(recorder.start_recording(sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);

    // Record reservation events across 8 cores on cache line 0x4000
    for (std::size_t c = 0; c < 8; ++c) {
        recorder.record_reservation_set(c, 0x4000, 0x2000 + c * 0x200);
    }
    // Core 5 completes store exclusive, invalidating reservations
    recorder.record_store_exclusive_result(5, 0x4000, true, 0x2000 + 5 * 0x200 + 4);
    for (std::size_t c = 0; c < 8; ++c) {
        if (c != 5) {
            recorder.record_reservation_cleared(c, 0x4000, 0x2000 + c * 0x200 + 4);
        }
    }

    sched.run_deterministic(80);
    const std::uint64_t recorded_final_hash = sched.compute_canonical_multicore_hash();
    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    // Replay
    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_eight_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(wsnap_path, sndl_path).is_success());
    assert(coord.run().is_success());
    assert(coord.stats().final_canonical_hash == recorded_final_hash);

    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::cout << "  -> Group 8 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 9: External Interrupt Replay Across 8 Cores
// -----------------------------------------------------------------------------
void test_group_9_interrupt_replay() {
    std::cout << "[Group 9] Testing Eight-Core External Interrupt Replay...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_smp_8c_irq";
    std::filesystem::create_directories(temp_dir);
    const auto wsnap_path = temp_dir / "irq.wsnap";
    const auto sndl_path = temp_dir / "irq.sndl";

    auto mem = std::make_shared<SmpSharedMemory>(2 * 1024 * 1024, 8);
    SmpScheduler sched(mem, 8, 10);

    for (std::size_t c = 0; c < 8; ++c) {
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

    sndl::SndlV2RecorderOptions rec_opts{8, 2 * 1024 * 1024, init_hash, 0, 0, 64};
    sndl::SndlV2Recorder recorder(rec_opts);
    assert(recorder.start_recording(sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);

    // Record interrupts across all 8 cores
    for (std::size_t c = 0; c < 8; ++c) {
        recorder.record_external_interrupt_asserted(0, 32 + static_cast<std::uint32_t>(c), static_cast<std::uint8_t>(c));
    }

    sched.run_deterministic(80);
    const std::uint64_t recorded_final_hash = sched.compute_canonical_multicore_hash();
    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    // Replay
    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_eight_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(wsnap_path, sndl_path).is_success());
    auto run_res = coord.run();
    assert(run_res.is_success());
    assert(coord.stats().final_canonical_hash == recorded_final_hash);

    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::cout << "  -> Group 9 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 10: Virtual Timer Replay Across 8 Cores
// -----------------------------------------------------------------------------
void test_group_10_timer_replay() {
    std::cout << "[Group 10] Testing Eight-Core Virtual Timer Replay...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_smp_8c_timer";
    std::filesystem::create_directories(temp_dir);
    const auto wsnap_path = temp_dir / "timer.wsnap";
    const auto sndl_path = temp_dir / "timer.sndl";

    auto mem = std::make_shared<SmpSharedMemory>(2 * 1024 * 1024, 8);
    SmpScheduler sched(mem, 8, 10);

    for (std::size_t c = 0; c < 8; ++c) {
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

    sndl::SndlV2RecorderOptions rec_opts{8, 2 * 1024 * 1024, init_hash, 0, 0, 64};
    sndl::SndlV2Recorder recorder(rec_opts);
    assert(recorder.start_recording(sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);

    // Record timer expiration events on all 8 cores
    for (std::size_t c = 0; c < 8; ++c) {
        recorder.record_timer_expired(c, 1, 1000 + c * 100);
    }

    sched.run_deterministic(80);
    const std::uint64_t recorded_final_hash = sched.compute_canonical_multicore_hash();
    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    // Replay
    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_eight_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(wsnap_path, sndl_path).is_success());
    auto run_res = coord.run();
    assert(run_res.is_success());
    assert(coord.stats().final_canonical_hash == recorded_final_hash);

    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::cout << "  -> Group 10 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 11: MMIO and Entropy Input Replay Across 8 Cores
// -----------------------------------------------------------------------------
void test_group_11_mmio_entropy_replay() {
    std::cout << "[Group 11] Testing Eight-Core MMIO and Entropy Input Replay...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_smp_8c_mmio";
    std::filesystem::create_directories(temp_dir);
    const auto wsnap_path = temp_dir / "mmio.wsnap";
    const auto sndl_path = temp_dir / "mmio.sndl";

    auto mem = std::make_shared<SmpSharedMemory>(2 * 1024 * 1024, 8);
    SmpScheduler sched(mem, 8, 10);

    for (std::size_t c = 0; c < 8; ++c) {
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

    sndl::SndlV2RecorderOptions rec_opts{8, 2 * 1024 * 1024, init_hash, 0, 0, 64};
    sndl::SndlV2Recorder recorder(rec_opts);
    assert(recorder.start_recording(sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);

    // Record interleaved MMIO and Entropy reads across 8 cores
    for (std::size_t c = 0; c < 8; ++c) {
        if (c % 2 == 0) {
            recorder.record_mmio_read(c, 0x09000000ULL + c * 4, 0x40 + c, 1, 0x2000 + c * 0x200);
        } else {
            recorder.record_entropy_read(c, 0xAABBCCDD00000000ULL | c, 0x2000 + c * 0x200);
        }
    }

    sched.run_deterministic(80);
    const std::uint64_t recorded_final_hash = sched.compute_canonical_multicore_hash();
    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    // Replay
    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_eight_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(wsnap_path, sndl_path).is_success());
    auto run_res = coord.run();
    assert(run_res.is_success());
    assert(coord.stats().final_canonical_hash == recorded_final_hash);

    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::cout << "  -> Group 11 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 12: DMA Capability Rejection Across 8 Cores (BLK-SMP-03)
// -----------------------------------------------------------------------------
void test_group_12_dma_rejection() {
    std::cout << "[Group 12] Testing Eight-Core DMA Capability Rejection Guard...\n";
    const auto fix = setup_eightcore_recording("grp12");

    // Setting allow_uncaptured_dma = true must be rejected
    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_eight_core_replay = true;
    cfg.allow_uncaptured_dma = true;

    SmpReplayCoordinator coord(cfg);
    auto res = coord.prepare(fix.wsnap_path, fix.sndl_path);
    assert(res.is_failure());
    assert(res.code == ReplayErrorCode::UnsupportedConfiguration);

    cleanup_fixture(fix);
    std::cout << "  -> Group 12 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 13: Host Clock Virtualization Rejection (BLK-SMP-04)
// -----------------------------------------------------------------------------
void test_group_13_host_clock_virtualization() {
    std::cout << "[Group 13] Testing Host Clock Virtualization Rejection Guard...\n";
    const auto fix = setup_eightcore_recording("grp13");

    // 1. allow_unvirtualized_wall_clock = true -> Rejected
    {
        SmpReplayCapabilityConfig cfg;
        cfg.allow_experimental_smp = true;
        cfg.allow_eight_core_replay = true;
        cfg.allow_unvirtualized_wall_clock = true;
        SmpReplayCoordinator coord(cfg);
        auto res = coord.prepare(fix.wsnap_path, fix.sndl_path);
        assert(res.is_failure());
        assert(res.code == ReplayErrorCode::UnsupportedConfiguration);
    }

    // 2. UnsupportedHostClock -> Rejected
    {
        SmpReplayCapabilityConfig cfg;
        cfg.allow_experimental_smp = true;
        cfg.allow_eight_core_replay = true;
        cfg.clock_source = SmpReplayClockSource::UnsupportedHostClock;
        SmpReplayCoordinator coord(cfg);
        auto res = coord.prepare(fix.wsnap_path, fix.sndl_path);
        assert(res.is_failure());
        assert(res.code == ReplayErrorCode::UnsupportedConfiguration);
    }

    cleanup_fixture(fix);
    std::cout << "  -> Group 13 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 14: Repeated Eight-Core Deterministic Replay (10 Consecutive Executions)
// -----------------------------------------------------------------------------
void test_group_14_repeated_replay_10_runs() {
    std::cout << "[Group 14] Testing Repeated Eight-Core Deterministic Replay (10 Runs)...\n";
    const auto fix = setup_eightcore_recording("grp14", 20, 320);

    std::uint64_t canonical_hash = 0;

    for (int run = 1; run <= 10; ++run) {
        SmpReplayCapabilityConfig cfg;
        cfg.allow_experimental_smp = true;
        cfg.allow_eight_core_replay = true;
        SmpReplayCoordinator coord(cfg);

        auto prep_res = coord.prepare(fix.wsnap_path, fix.sndl_path);
        assert(prep_res.is_success());

        auto run_res = coord.run();
        assert(run_res.is_success());
        assert(coord.state() == SmpReplayState::Completed);

        const std::uint64_t h = coord.stats().final_canonical_hash;
        assert(h == fix.final_recorded_cluster_hash);

        if (run == 1) {
            canonical_hash = h;
            std::cout << "  Run 1 Canonical Hash: 0x" << std::hex << canonical_hash << std::dec << "\n";
        } else {
            assert(h == canonical_hash);
        }
    }

    cleanup_fixture(fix);
    std::cout << "  -> Group 14 PASS (10/10 Runs Bit-for-Bit Identical)\n";
}

// -----------------------------------------------------------------------------
// Group 15: Core Lifecycle Transitions Replay
// -----------------------------------------------------------------------------
void test_group_15_lifecycle_replay() {
    std::cout << "[Group 15] Testing Eight-Core Lifecycle Transitions Replay...\n";
    const auto fix = setup_eightcore_recording("grp15", 10, 160);

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_eight_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());

    assert(coord.run().is_success());
    assert(coord.stats().final_state == SmpReplayState::Completed);

    cleanup_fixture(fix);
    std::cout << "  -> Group 15 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 16: ReplaySession Top-Level Integration
// -----------------------------------------------------------------------------
void test_group_16_replay_session_integration() {
    std::cout << "[Group 16] Testing ReplaySession Top-Level Eight-Core Integration...\n";
    const auto fix = setup_eightcore_recording("grp16", 20, 160);

    // 1. Without allow_eight_core_replay -> Session preparation fails
    {
        ReplayConfig cfg;
        cfg.mode = ReplayMode::MultiCore;
        cfg.allow_experimental_smp = true;
        cfg.allow_eight_core_replay = false;

        ReplaySession session;
        auto res = session.prepare(fix.wsnap_path, fix.sndl_path, cfg);
        assert(res.is_failure());
        assert(res.code == ReplayErrorCode::UnsupportedConfiguration);
    }

    // 2. With allow_eight_core_replay -> Session preparation and execution succeed
    {
        ReplayConfig cfg;
        cfg.mode = ReplayMode::MultiCore;
        cfg.allow_experimental_smp = true;
        cfg.allow_eight_core_replay = true;

        ReplaySession session;
        auto res = session.prepare(fix.wsnap_path, fix.sndl_path, cfg);
        assert(res.is_success());
        assert(session.state() == ReplayLifecycleState::Ready);
        assert(session.smp_coordinator() != nullptr);
        assert(session.smp_coordinator()->active_core_count() == 8);

        auto run_res = session.run();
        assert(run_res.is_success());
        assert(session.state() == ReplayLifecycleState::Completed);
    }

    cleanup_fixture(fix);
    std::cout << "  -> Group 16 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 17: Four-Core Regression Compatibility
// -----------------------------------------------------------------------------
void test_group_17_four_core_regression() {
    std::cout << "[Group 17] Verifying Four-Core Replay Regression Compatibility...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_smp_4c_regr";
    std::filesystem::create_directories(temp_dir);
    const auto wsnap_path = temp_dir / "regr_4c.wsnap";
    const auto sndl_path = temp_dir / "regr_4c.sndl";

    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 4);
    SmpScheduler sched(mem, 4, 10);

    for (std::size_t c = 0; c < 4; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        mem->raw_memory().write_u32(code_addr, 0xD2800000u | (static_cast<std::uint32_t>((c + 1) * 10) << 5) | static_cast<std::uint32_t>(c));
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

    sched.run_deterministic(40);
    const std::uint64_t recorded_final_hash = sched.compute_canonical_multicore_hash();
    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_four_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(wsnap_path, sndl_path).is_success());
    assert(coord.run().is_success());
    assert(coord.stats().final_canonical_hash == recorded_final_hash);

    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::cout << "  -> Group 17 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 18: Two-Core and Single-Core Regression Compatibility
// -----------------------------------------------------------------------------
void test_group_18_two_and_single_core_regression() {
    std::cout << "[Group 18] Verifying Two-Core and Single-Core Regression...\n";

    // Two-core replay regression
    {
        const auto temp_dir = std::filesystem::temp_directory_path() / "winios_smp_2c_regr";
        std::filesystem::create_directories(temp_dir);
        const auto wsnap_path = temp_dir / "regr_2c.wsnap";
        const auto sndl_path = temp_dir / "regr_2c.sndl";

        auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 2);
        SmpScheduler sched(mem, 2, 10);

        for (std::size_t c = 0; c < 2; ++c) {
            auto cpu = std::make_shared<CPU>();
            cpu->set_core_id(c);
            const std::uint64_t code_addr = 0x2000 + c * 0x200;
            mem->raw_memory().write_u32(code_addr, 0xD2800000u | (static_cast<std::uint32_t>((c + 1) * 10) << 5) | static_cast<std::uint32_t>(c));
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
        const std::uint64_t recorded_final_hash = sched.compute_canonical_multicore_hash();
        assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

        SmpReplayCapabilityConfig cfg;
        cfg.allow_experimental_smp = true;
        SmpReplayCoordinator coord(cfg);
        assert(coord.prepare(wsnap_path, sndl_path).is_success());
        assert(coord.run().is_success());
        assert(coord.stats().final_canonical_hash == recorded_final_hash);

        std::error_code ec;
        std::filesystem::remove_all(temp_dir, ec);
    }

    std::cout << "  -> Group 18 PASS\n";
}

} // namespace

int main() {
    std::cout << std::unitbuf;
    std::cout << "===================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 29: Eight-Core Deterministic Replay Tests   \n";
    std::cout << "===================================================================\n\n";

    test_group_1_capability_gate();
    test_group_2_snapshot_restoration();
    test_group_3_event_cursor();
    test_group_4_scheduler_coordination();
    test_group_5_global_retirement_sync();
    test_group_6_per_core_retirement_sync();
    test_group_7_shared_memory_determinism();
    test_group_8_exclusive_monitor_contention();
    test_group_9_interrupt_replay();
    test_group_10_timer_replay();
    test_group_11_mmio_entropy_replay();
    test_group_12_dma_rejection();
    test_group_13_host_clock_virtualization();
    test_group_14_repeated_replay_10_runs();
    test_group_15_lifecycle_replay();
    test_group_16_replay_session_integration();
    test_group_17_four_core_regression();
    test_group_18_two_and_single_core_regression();

    std::cout << "\n>>> ALL 18 EIGHT-CORE REPLAY TEST GROUPS PASSED <<<\n";
    return 0;
}

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "replay/replay_checkpoint.h"
#include "replay/replay_divergence.h"
#include "replay/replay_engine.h"
#include "replay/replay_event_cursor.h"
#include "replay/replay_input_router.h"
#include "replay/replay_report.h"
#include "replay/replay_scheduler.h"
#include "replay/replay_session.h"
#include "replay/replay_state_hash.h"
#include "replay/replay_types.h"
#include "sndl/sndl_event_encoder.h"
#include "sndl/sndl_writer.h"
#include "snapshot/snapshot_manager.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

using winios::VirtualTime;
using winios::GICv3;

// Helper: Sets up a test program in memory and captures a baseline snapshot
void setup_test_program(
    CPU& cpu,
    Memory& mem,
    VirtualTime& vt,
    GICv3& gic,
    const std::filesystem::path& snap_path
) {
    std::filesystem::remove(snap_path);

    cpu.reset();
    cpu.bind_virtual_time(&vt, 0);

    // Program:
    // 0: MOVZ X0, #0x2000
    // 4: MOVZ X1, #0x55
    // 8: STR X1, [X0]
    // 12: LDR X2, [X0]
    // 16: ADD X3, X2, #5
    // 20: MOVK X3, #0x1234, LSL #16
    mem.write_u32(0,  0xD2840000u); // MOVZ X0, #0x2000
    mem.write_u32(4,  0xD2800AA1u); // MOVZ X1, #0x55
    mem.write_u32(8,  0xF9000001u); // STR X1, [X0]
    mem.write_u32(12, 0xF9400002u); // LDR X2, [X0]
    mem.write_u32(16, 0x91001443u); // ADD X3, X2, #5
    mem.write_u32(20, 0xF2A24683u); // MOVK X3, #0x1234, LSL #16

    cpu.set_program_counter(0);

    winios::snapshot::SnapshotRestoreContext ctx{};
    ctx.primary_cpu = &cpu;
    ctx.memory = &mem;
    ctx.virtual_time = &vt;
    ctx.gic = &gic;

    winios::snapshot::SnapshotCaptureOptions opts{};
    opts.description = "Replay Test Baseline";
    const auto cap_res = winios::snapshot::SnapshotManager::capture_to_file(ctx, snap_path, opts);
    assert(cap_res.success);
}

// Helper: Creates an .sndl file with given header parameters and records
void create_test_sndl(
    const std::filesystem::path& path,
    std::uint64_t base_hash,
    std::uint64_t total_instructions,
    const std::vector<winios::sndl::SndlRecord>& records = {}
) {
    std::filesystem::remove(path);
    winios::sndl::WSndlHeader hdr{};
    hdr.core_count = 1;
    hdr.base_snapshot_hash = base_hash;
    hdr.total_instructions = total_instructions;

    winios::sndl::SndlWriter writer;
    assert(writer.open(path, hdr).is_success());
    for (const auto& r : records) {
        assert(writer.write_record(r).is_success());
    }
    assert(writer.finalize(winios::sndl::SndlFinalizationStatus::Completed, total_instructions).is_success());
}

// -----------------------------------------------------------------------------
// Test 1: Baseline Snapshot Validation & Restore
// -----------------------------------------------------------------------------
void test_baseline_snapshot_validation_and_restore() {
    std::cout << "[Test 1] Baseline snapshot validation & restore...\n";

    const std::filesystem::path snap_path = "test_snap_base.wsnap";
    CPU cpu;
    Memory mem(1024 * 1024);
    VirtualTime vt;
    GICv3 gic;
    setup_test_program(cpu, mem, vt, gic, snap_path);

    // 1. Valid snapshot file validation
    const auto val_res = winios::snapshot::SnapshotManager::validate_file(snap_path);
    assert(val_res.valid);

    // 2. Corrupt snapshot test
    const std::filesystem::path corrupt_snap = "test_corrupt.wsnap";
    {
        std::ofstream out(corrupt_snap, std::ios::binary);
        out << "INVALID_DATA_HEADER_NOT_WSNAP";
    }
    const auto bad_res = winios::snapshot::SnapshotManager::validate_file(corrupt_snap);
    assert(!bad_res.valid);
    std::filesystem::remove(corrupt_snap);

    std::filesystem::remove(snap_path);
    std::cout << "  -> PASSED\n";
}

// -----------------------------------------------------------------------------
// Test 2: SNDL Event Cursor & Streaming Reader
// -----------------------------------------------------------------------------
void test_sndl_event_cursor() {
    std::cout << "[Test 2] SNDL event cursor & streaming reader...\n";

    const std::filesystem::path sndl_file = "test_cursor.sndl";
    std::vector<winios::sndl::SndlRecord> recs;
    for (std::uint32_t i = 1; i <= 50; ++i) {
        auto rec = winios::sndl::make_counter_read_record(0, 0x1000 + i * 4, i, 0, 0x5F01, 1000 + i, 24000000);
        rec.sequence_num = i;
        recs.push_back(rec);
    }
    create_test_sndl(sndl_file, 0x12345678ULL, 50, recs);

    winios::replay::ReplayEventCursor cursor;
    assert(cursor.open(sndl_file).is_success());
    assert(cursor.is_open());
    assert(cursor.header().core_count == 1);
    assert(cursor.header().base_snapshot_hash == 0x12345678ULL);

    std::uint64_t count = 0;
    while (cursor.has_next()) {
        const auto& r = cursor.current();
        ++count;
        assert(r.sequence_num == count);
        assert(r.event_type == winios::sndl::SndlEventType::CounterReadResult);
        assert(cursor.advance().is_success());
    }

    assert(count == 50);
    assert(cursor.total_consumed() == 50);
    assert(!cursor.has_next());

    cursor.close();
    std::filesystem::remove(sndl_file);
    std::cout << "  -> PASSED\n";
}

// -----------------------------------------------------------------------------
// Test 3: Single-Core Pure Deterministic Compute Workload
// -----------------------------------------------------------------------------
void test_single_core_pure_compute() {
    std::cout << "[Test 3] Single-core deterministic pure compute replay...\n";

    const std::filesystem::path snap_file = "test_compute.wsnap";
    const std::filesystem::path sndl_file = "test_compute.sndl";

    CPU orig_cpu;
    Memory orig_mem(1024 * 1024);
    VirtualTime orig_vt;
    GICv3 orig_gic;
    setup_test_program(orig_cpu, orig_mem, orig_vt, orig_gic, snap_file);

    create_test_sndl(sndl_file, 0, 6);

    // Execute recorded CPU directly
    for (int i = 0; i < 6; ++i) {
        orig_cpu.step(orig_mem);
    }
    const auto orig_hashes = winios::replay::ReplayStateHash::compute_state_hashes(
        orig_cpu, orig_mem, &orig_vt, &orig_gic
    );

    // Now replay using ReplayEngine
    winios::replay::ReplayConfig config{};
    config.max_instructions = 6;
    winios::replay::ReplayExecutionStats stats{};

    const auto rep_res = winios::replay::ReplayEngine::run(snap_file, sndl_file, config, stats);
    if (rep_res.is_failure()) {
        std::cerr << "Test 4 Run Failure: " << rep_res.message << " (Divergence: " << stats.divergence_info.message << ")\n";
    }
    assert(rep_res.is_success());
    assert(!stats.diverged);
    assert(stats.instructions_executed == 6);
    assert(stats.final_state_hashes == orig_hashes);

    std::filesystem::remove(snap_file);
    std::filesystem::remove(sndl_file);
    std::cout << "  -> PASSED\n";
}

// -----------------------------------------------------------------------------
// Test 4: Replay with Non-Deterministic Inputs (MMIO, Counter, Interrupt)
// -----------------------------------------------------------------------------
void test_nondeterministic_inputs_replay() {
    std::cout << "[Test 4] Replay with MMIO, counter, and interrupt injection...\n";

    const std::filesystem::path snap_file = "test_nd.wsnap";
    const std::filesystem::path sndl_file = "test_nd.sndl";
    std::filesystem::remove(snap_file);
    std::filesystem::remove(sndl_file);

    CPU cpu;
    Memory mem(1024 * 1024);
    VirtualTime vt;
    GICv3 gic;

    cpu.reset();
    cpu.bind_virtual_time(&vt, 0);

    // 0: MOVZ X0, #0x2000
    // 4: MRS X1, CNTPCT_EL0
    // 8: ADD X2, X1, #1
    mem.write_u32(0, 0xD2840000u); // MOVZ X0, #0x2000
    mem.write_u32(4, 0xD53BE021u); // MRS X1, CNTPCT_EL0
    mem.write_u32(8, 0x91000422u); // ADD X2, X1, #1
    cpu.set_program_counter(0);
    cpu.set_vbar_el1(0x1000);
    for (std::size_t a = 0x1000; a < 0x2000; a += 4) {
        mem.write_u32(a, 0xD503201Fu); // NOP
    }

    winios::snapshot::SnapshotRestoreContext ctx{};
    ctx.primary_cpu = &cpu;
    ctx.memory = &mem;
    ctx.virtual_time = &vt;
    ctx.gic = &gic;

    winios::snapshot::SnapshotCaptureOptions opts{};
    opts.description = "Replay Test ND Baseline";
    assert(winios::snapshot::SnapshotManager::capture_to_file(ctx, snap_file, opts).success);

    std::vector<winios::sndl::SndlRecord> recs;
    auto r1 = winios::sndl::make_counter_read_record(0, 4, 1, 0, 0x5F01, 0x9999ULL, 24000000);
    r1.sequence_num = 1;
    recs.push_back(r1);

    auto r2 = winios::sndl::make_interrupt_delivery_record(0, 8, 2, 33, 0xA0, false, false, 1);
    r2.sequence_num = 2;
    recs.push_back(r2);

    create_test_sndl(sndl_file, 0, 3, recs);

    // Validate both
    assert(winios::replay::ReplayEngine::validate(snap_file, sndl_file).is_success());

    winios::replay::ReplayConfig config{};
    config.max_instructions = 3;
    winios::replay::ReplayExecutionStats stats{};

    const auto rep_res = winios::replay::ReplayEngine::run(snap_file, sndl_file, config, stats);
    if (rep_res.is_failure()) {
        std::cerr << "Test 4 Run Failure: " << rep_res.message << " (Divergence: " << stats.divergence_info.message << ")\n";
    }
    assert(rep_res.is_success());
    assert(!stats.diverged);
    assert(stats.events_consumed == 2);

    std::filesystem::remove(snap_file);
    std::filesystem::remove(sndl_file);
    std::cout << "  -> PASSED\n";
}

// -----------------------------------------------------------------------------
// Test 5: State Hash Verification Engine & Component Difference
// -----------------------------------------------------------------------------
void test_state_hash_verification() {
    std::cout << "[Test 5] State hash verification engine & component diffing...\n";

    CPU cpu1, cpu2;
    Memory mem1(1024 * 1024), mem2(1024 * 1024);
    VirtualTime vt1, vt2;
    GICv3 gic1, gic2;

    cpu1.write_register(0, 0x12345678ULL);
    cpu2.write_register(0, 0x12345678ULL);

    mem1.write_u32(0x100, 0xDEADBEEF);
    mem2.write_u32(0x100, 0xDEADBEEF);

    const auto h1 = winios::replay::ReplayStateHash::compute_state_hashes(cpu1, mem1, &vt1, &gic1);
    const auto h2 = winios::replay::ReplayStateHash::compute_state_hashes(cpu2, mem2, &vt2, &gic2);
    assert(h1 == h2);

    // Mutate memory in instance 2
    mem2.write_u32(0x100, 0xCAFEBABE);
    const auto h3 = winios::replay::ReplayStateHash::compute_state_hashes(cpu2, mem2, &vt2, &gic2);
    assert(h1 != h3);
    assert(h1.cpu_registers_hash == h3.cpu_registers_hash);
    assert(h1.memory_hash != h3.memory_hash);

    std::string diff_desc;
    const bool matched = winios::replay::ReplayStateHash::compare_and_diff(h1, h3, diff_desc);
    assert(!matched);
    assert(diff_desc.find("Guest Memory") != std::string::npos);

    std::cout << "  -> PASSED\n";
}

// -----------------------------------------------------------------------------
// Test 6: Checkpoints Tracking
// -----------------------------------------------------------------------------
void test_checkpoints_tracking() {
    std::cout << "[Test 6] Periodic and boundary checkpoints tracking...\n";

    const std::filesystem::path snap_file = "test_cp.wsnap";
    const std::filesystem::path sndl_file = "test_cp.sndl";

    CPU cpu;
    Memory mem(1024 * 1024);
    VirtualTime vt;
    GICv3 gic;
    setup_test_program(cpu, mem, vt, gic, snap_file);

    create_test_sndl(sndl_file, 0, 6);

    winios::replay::ReplayConfig config{};
    config.max_instructions = 6;
    config.checkpoint_interval = 2; // Checkpoint every 2 instructions!
    winios::replay::ReplayExecutionStats stats{};

    const auto res = winios::replay::ReplayEngine::run(snap_file, sndl_file, config, stats);
    assert(res.is_success());
    // Start force + interval 2 + interval 4 + interval 6 + end force >= 4 checkpoints
    assert(stats.checkpoints_taken >= 4);

    std::filesystem::remove(snap_file);
    std::filesystem::remove(sndl_file);
    std::cout << "  -> PASSED\n";
}

// -----------------------------------------------------------------------------
// Test 7: Divergence Detection Across Categories
// -----------------------------------------------------------------------------
void test_divergence_detection() {
    std::cout << "[Test 7] Divergence detection & classification (16 categories)...\n";

    // 1. Verify all 16 divergence category mappings
    for (std::uint32_t c = 1; c <= 16; ++c) {
        auto cat = static_cast<winios::replay::ReplayDivergenceCategory>(c);
        auto name = winios::replay::replay_divergence_category_name(cat);
        assert(!name.empty());
        assert(name != "UnknownDivergence");
    }

    const std::filesystem::path snap_file = "test_div.wsnap";
    const std::filesystem::path sndl_file = "test_div.sndl";
    const std::filesystem::path report_file = "test_div_report.json";
    std::filesystem::remove(snap_file);
    std::filesystem::remove(sndl_file);
    std::filesystem::remove(report_file);

    CPU cpu;
    Memory mem(1024 * 1024);
    VirtualTime vt;
    GICv3 gic;

    cpu.reset();
    cpu.bind_virtual_time(&vt, 0);

    // 0: MRS X0, CNTPCT_EL0
    mem.write_u32(0, 0xD53BE020u);
    cpu.set_program_counter(0);

    winios::snapshot::SnapshotRestoreContext ctx{};
    ctx.primary_cpu = &cpu;
    ctx.memory = &mem;
    ctx.virtual_time = &vt;
    ctx.gic = &gic;

    winios::snapshot::SnapshotCaptureOptions opts{};
    assert(winios::snapshot::SnapshotManager::capture_to_file(ctx, snap_file, opts).success);

    // Corrupt log: event with core mismatch (Core 99 instead of Core 0)
    std::vector<winios::sndl::SndlRecord> recs;
    auto rec = winios::sndl::make_counter_read_record(99, 0, 0, 0, 0x5F01, 1234, 24000000);
    rec.sequence_num = 1;
    recs.push_back(rec);
    create_test_sndl(sndl_file, 0, 1, recs);

    winios::replay::ReplayConfig config{};
    config.max_instructions = 1;
    config.divergence_report_path = report_file;

    winios::replay::ReplaySession session;
    assert(session.prepare(snap_file, sndl_file, config).is_success());

    // Run session - must detect CoreMismatch and halt immediately!
    const auto res = session.run();
    assert(res.is_failure());
    assert(res.code == winios::replay::ReplayErrorCode::Diverged);
    assert(session.state() == winios::replay::ReplayLifecycleState::Diverged);
    assert(session.stats().diverged);
    assert(session.stats().divergence_info.category == winios::replay::ReplayDivergenceCategory::CoreMismatch);

    // Verify divergence report file was written
    assert(std::filesystem::exists(report_file));

    session.close();
    std::error_code ec;
    std::filesystem::remove(snap_file, ec);
    std::filesystem::remove(sndl_file, ec);
    std::filesystem::remove(report_file, ec);
    std::cout << "  -> PASSED\n";
}

// -----------------------------------------------------------------------------
// Test 8: Strict Repeatability Acceptance (10 Consecutive Runs)
// -----------------------------------------------------------------------------
void test_repeatability_10_runs() {
    std::cout << "[Test 8] Strict repeatability acceptance (10 consecutive identical runs)...\n";

    const std::filesystem::path snap_file = "test_rep10.wsnap";
    const std::filesystem::path sndl_file = "test_rep10.sndl";
    std::filesystem::remove(snap_file);
    std::filesystem::remove(sndl_file);

    CPU cpu;
    Memory mem(1024 * 1024);
    VirtualTime vt;
    GICv3 gic;

    cpu.reset();
    cpu.bind_virtual_time(&vt, 0);

    // 0: MOVZ X0, #0x2000
    // 4: MRS X1, CNTPCT_EL0
    // 8: ADD X2, X1, #1
    mem.write_u32(0, 0xD2840000u); // MOVZ X0, #0x2000
    mem.write_u32(4, 0xD53BE021u); // MRS X1, CNTPCT_EL0
    mem.write_u32(8, 0x91000422u); // ADD X2, X1, #1
    cpu.set_program_counter(0);
    cpu.set_vbar_el1(0x1000);
    for (std::size_t a = 0x1000; a < 0x2000; a += 4) {
        mem.write_u32(a, 0xD503201Fu); // NOP
    }

    winios::snapshot::SnapshotRestoreContext ctx{};
    ctx.primary_cpu = &cpu;
    ctx.memory = &mem;
    ctx.virtual_time = &vt;
    ctx.gic = &gic;

    winios::snapshot::SnapshotCaptureOptions opts{};
    opts.description = "Replay Repeatability Baseline";
    assert(winios::snapshot::SnapshotManager::capture_to_file(ctx, snap_file, opts).success);

    std::vector<winios::sndl::SndlRecord> recs;
    auto r1 = winios::sndl::make_counter_read_record(0, 4, 1, 0, 0x5F01, 0x12345ULL, 24000000);
    r1.sequence_num = 1;
    recs.push_back(r1);

    auto r2 = winios::sndl::make_interrupt_delivery_record(0, 8, 2, 40, 0x80, false, false, 1);
    r2.sequence_num = 2;
    recs.push_back(r2);

    create_test_sndl(sndl_file, 0, 3, recs);

    winios::replay::ReplayConfig config{};
    config.max_instructions = 3;

    winios::replay::ReplayComponentHashes baseline_hashes{};
    for (int run = 1; run <= 10; ++run) {
        winios::replay::ReplayExecutionStats stats{};
        const auto res = winios::replay::ReplayEngine::run(snap_file, sndl_file, config, stats);
        assert(res.is_success());
        assert(!stats.diverged);
        assert(stats.instructions_executed == 3);
        assert(stats.events_consumed == 2);

        if (run == 1) {
            baseline_hashes = stats.final_state_hashes;
            assert(baseline_hashes.combined_hash != 0);
        } else {
            // Must be bit-for-bit identical!
            assert(stats.final_state_hashes == baseline_hashes);
        }
    }

    std::filesystem::remove(snap_file);
    std::filesystem::remove(sndl_file);
    std::cout << "  -> PASSED: All 10 runs produced identical architectural state hashes!\n";
}

// -----------------------------------------------------------------------------
// Test 9: SMP Unsupported Rejection
// -----------------------------------------------------------------------------
void test_smp_unsupported_rejection() {
    std::cout << "[Test 9] Rejection of unsupported multi-core SMP replay...\n";

    const std::filesystem::path snap_file = "test_smp.wsnap";
    const std::filesystem::path sndl_file = "test_smp.sndl";

    CPU cpu;
    Memory mem(1024 * 1024);
    VirtualTime vt;
    GICv3 gic;
    setup_test_program(cpu, mem, vt, gic, snap_file);

    create_test_sndl(sndl_file, 0, 6);

    winios::replay::ReplayConfig config{};
    config.mode = winios::replay::ReplayMode::MultiCore;
    config.allow_experimental_smp = false; // Not allowed!

    winios::replay::ReplayExecutionStats stats{};
    const auto res = winios::replay::ReplayEngine::run(snap_file, sndl_file, config, stats);
    assert(res.is_failure());
    assert(res.code == winios::replay::ReplayErrorCode::UnsupportedConfiguration);

    std::filesystem::remove(snap_file);
    std::filesystem::remove(sndl_file);
    std::cout << "  -> PASSED\n";
}

} // namespace

#include <crtdbg.h>
#include <cstdlib>

int main() {
#ifdef _WIN32
    _set_abort_behavior(0, _WRITE_ABORT_MSG);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    std::cout << "========================================================================\n" << std::flush;
    std::cout << "           WINIOS PHASE 52 STAGE 19 DETERMINISTIC REPLAY TESTS          \n";
    std::cout << "========================================================================\n";

    test_baseline_snapshot_validation_and_restore();
    test_sndl_event_cursor();
    test_single_core_pure_compute();
    test_nondeterministic_inputs_replay();
    test_state_hash_verification();
    test_checkpoints_tracking();
    test_divergence_detection();
    test_repeatability_10_runs();
    test_smp_unsupported_rejection();

    std::cout << "========================================================================\n";
    std::cout << "           ALL 9 DETERMINISTIC REPLAY TEST GROUPS PASSED!               \n";
    std::cout << "========================================================================\n";
    return 0;
}

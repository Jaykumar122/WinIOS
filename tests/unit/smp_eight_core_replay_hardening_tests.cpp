#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "replay/replay_session.h"
#include "replay/smp_replay_coordinator.h"
#include "replay/smp_replay_types.h"
#include "snapshot/smp_snapshot_manager.h"
#include "sndl/sndl_v2_format.h"
#include "sndl/sndl_v2_recorder.h"
#include "sndl/sndl_v2_writer.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace winios;
using namespace winios::replay;

namespace {

struct HardeningFixture {
    std::filesystem::path dir;
    std::filesystem::path wsnap_path;
    std::filesystem::path sndl_path;
    std::uint64_t initial_cluster_hash{0};
    std::uint64_t final_cluster_hash{0};
};

HardeningFixture create_valid_8c_fixture(const std::string& name) {
    HardeningFixture fix;
    fix.dir = std::filesystem::temp_directory_path() / ("winios_hard_8c_" + name);
    std::filesystem::create_directories(fix.dir);
    fix.wsnap_path = fix.dir / "snap.wsnap";
    fix.sndl_path = fix.dir / "log.sndl";

    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 8);
    SmpScheduler sched(mem, 8, 10);

    for (std::size_t c = 0; c < 8; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        mem->raw_memory().write_u32(code_addr, 0xD2800000u | (static_cast<std::uint32_t>((c + 1) * 10) << 5) | static_cast<std::uint32_t>(c));
        mem->raw_memory().write_u32(code_addr + 4, 0x14000000u); // B .
        cpu->set_program_counter(code_addr);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    assert(snapshot::SmpSnapshotManager::capture_to_file(sched, fix.wsnap_path).success);
    fix.initial_cluster_hash = sched.compute_canonical_multicore_hash();

    sndl::SndlV2RecorderOptions rec_opts{8, 1024 * 1024, fix.initial_cluster_hash, 0, 0, 64};
    sndl::SndlV2Recorder recorder(rec_opts);
    assert(recorder.start_recording(fix.sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);

    sched.run_deterministic(40);
    fix.final_cluster_hash = sched.compute_canonical_multicore_hash();
    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    return fix;
}

void cleanup(const HardeningFixture& fix) {
    std::error_code ec;
    std::filesystem::remove_all(fix.dir, ec);
}

// -----------------------------------------------------------------------------
// Group 1: Corrupted Snapshot Rejection
// -----------------------------------------------------------------------------
void test_group_1_corrupted_snapshot() {
    std::cout << "[Group 1] Testing Corrupted Snapshot Rejection...\n";
    auto fix = create_valid_8c_fixture("grp1");

    // Corrupt first 16 bytes of snapshot
    {
        std::fstream f(fix.wsnap_path, std::ios::in | std::ios::out | std::ios::binary);
        char junk[16] = {0};
        f.write(junk, 16);
    }

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_eight_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    auto res = coord.prepare(fix.wsnap_path, fix.sndl_path);
    assert(res.is_failure());
    assert(res.code == ReplayErrorCode::SnapshotValidationFailed);

    cleanup(fix);
    std::cout << "  -> Group 1 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 2: Core Count Mismatch between Snapshot and SNDL
// -----------------------------------------------------------------------------
void test_group_2_core_count_mismatch() {
    std::cout << "[Group 2] Testing Core Count Mismatch Rejection...\n";
    auto fix8 = create_valid_8c_fixture("grp2_8c");

    // Create a 4-core SNDL file
    const auto dir4 = std::filesystem::temp_directory_path() / "winios_hard_8c_grp2_4c";
    std::filesystem::create_directories(dir4);
    const auto sndl4_path = dir4 / "log4.sndl";
    {
        sndl::WSndlV2Header hdr{};
        hdr.core_count = 4;
        hdr.memory_capacity = 1024 * 1024;
        hdr.base_snapshot_hash = fix8.initial_cluster_hash;
        std::vector<std::uint8_t> buf(64, 0);
        sndl::encode_sndl_v2_header(hdr, buf.data());
        std::ofstream f(sndl4_path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), 64);
    }

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_four_core_replay = true;
    cfg.allow_eight_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    auto res = coord.prepare(fix8.wsnap_path, sndl4_path);
    assert(res.is_failure());
    assert(res.code == ReplayErrorCode::UnsupportedConfiguration);

    cleanup(fix8);
    std::error_code ec;
    std::filesystem::remove_all(dir4, ec);
    std::cout << "  -> Group 2 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 3: Out-of-Bounds Core ID in Event Stream
// -----------------------------------------------------------------------------
void test_group_3_out_of_bounds_core_id() {
    std::cout << "[Group 3] Testing Out-of-Bounds Core ID Event Rejection...\n";
    auto fix = create_valid_8c_fixture("grp3");

    // Write an SNDL recording where CoreSelected has out-of-bounds core_id = 8
    {
        sndl::WSndlV2Header hdr{};
        hdr.magic = sndl::kWSndlV2Magic;
        hdr.version = sndl::kWSndlVersion2;
        hdr.core_count = 8;
        hdr.memory_capacity = 1024 * 1024;
        hdr.base_snapshot_hash = fix.initial_cluster_hash;
        hdr.initial_global_sequence = 1;

        sndl::SndlV2Writer writer;
        assert(writer.open(fix.sndl_path, hdr).is_success());

        sndl::SndlV2Record r{};
        r.event_type = sndl::SndlV2EventType::CoreSelected;
        r.core_id = 8; // Out-of-bounds!
        r.global_event_sequence = 1;
        assert(writer.write_record(r).is_success());
        assert(writer.finalize(sndl::SndlV2CompletionStatus::Completed).is_success());
    }

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_eight_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());

    auto run_res = coord.run();
    assert(run_res.is_failure());
    assert(coord.divergence().has_diverged());
    assert(coord.divergence().type == SmpReplayDivergenceType::CoreMismatch);

    cleanup(fix);
    std::cout << "  -> Group 3 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 4: Non-Monotonic Event Sequence Detection
// -----------------------------------------------------------------------------
void test_group_4_non_monotonic_sequence() {
    std::cout << "[Group 4] Testing Non-Monotonic Event Sequence Detection...\n";
    auto fix = create_valid_8c_fixture("grp4");

    // Write records where sequence is non-monotonic
    {
        sndl::WSndlV2Header hdr{};
        hdr.magic = sndl::kWSndlV2Magic;
        hdr.version = sndl::kWSndlVersion2;
        hdr.core_count = 8;
        hdr.memory_capacity = 1024 * 1024;
        hdr.base_snapshot_hash = fix.initial_cluster_hash;
        hdr.initial_global_sequence = 1;

        sndl::SndlV2Writer writer;
        assert(writer.open(fix.sndl_path, hdr).is_success());

        sndl::SndlV2Record r1{};
        r1.event_type = sndl::SndlV2EventType::CoreSelected;
        r1.core_id = 0;
        r1.global_event_sequence = 1;
        assert(writer.write_record(r1).is_success());

        sndl::SndlV2Record r2{};
        r2.event_type = sndl::SndlV2EventType::QuantumStart;
        r2.core_id = 0;
        r2.global_event_sequence = 999; // Monotonicity violation!
        r2.payload1 = 10;
        assert(writer.write_record(r2).is_success());
        assert(writer.finalize(sndl::SndlV2CompletionStatus::Completed).is_success());
    }

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_eight_core_replay = true;
    cfg.enforce_strict_monotonicity = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());

    auto run_res = coord.run();
    assert(run_res.is_failure());
    assert(coord.divergence().has_diverged());

    cleanup(fix);
    std::cout << "  -> Group 4 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 5: Premature Stream Exhaustion / Missing Event
// -----------------------------------------------------------------------------
void test_group_5_premature_stream_exhaustion() {
    std::cout << "[Group 5] Testing Premature Stream Exhaustion / Missing Event...\n";
    auto fix = create_valid_8c_fixture("grp5");

    // Write CoreSelected without QuantumStart
    {
        sndl::WSndlV2Header hdr{};
        hdr.magic = sndl::kWSndlV2Magic;
        hdr.version = sndl::kWSndlVersion2;
        hdr.core_count = 8;
        hdr.memory_capacity = 1024 * 1024;
        hdr.base_snapshot_hash = fix.initial_cluster_hash;
        hdr.initial_global_sequence = 1;

        sndl::SndlV2Writer writer;
        assert(writer.open(fix.sndl_path, hdr).is_success());

        sndl::SndlV2Record r1{};
        r1.event_type = sndl::SndlV2EventType::CoreSelected;
        r1.core_id = 0;
        r1.global_event_sequence = 1;
        assert(writer.write_record(r1).is_success());
        assert(writer.finalize(sndl::SndlV2CompletionStatus::Completed).is_success());
    }

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_eight_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());

    auto run_res = coord.run();
    assert(run_res.is_failure());
    assert(coord.divergence().has_diverged());

    cleanup(fix);
    std::cout << "  -> Group 5 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 6: Quantum Truncation Reason & Retirement Mismatch
// -----------------------------------------------------------------------------
void test_group_6_retirement_mismatch() {
    std::cout << "[Group 6] Testing Quantum Instruction Retirement Mismatch...\n";
    auto fix = create_valid_8c_fixture("grp6");

    // Write valid stream but with mutated QuantumEnd payload1
    {
        sndl::WSndlV2Header hdr{};
        hdr.magic = sndl::kWSndlV2Magic;
        hdr.version = sndl::kWSndlVersion2;
        hdr.core_count = 8;
        hdr.memory_capacity = 1024 * 1024;
        hdr.base_snapshot_hash = fix.initial_cluster_hash;
        hdr.initial_global_sequence = 1;

        sndl::SndlV2Writer writer;
        assert(writer.open(fix.sndl_path, hdr).is_success());

        sndl::SndlV2Record r1{};
        r1.event_type = sndl::SndlV2EventType::CoreSelected;
        r1.core_id = 0;
        r1.global_event_sequence = 1;
        assert(writer.write_record(r1).is_success());

        sndl::SndlV2Record r2{};
        r2.event_type = sndl::SndlV2EventType::QuantumStart;
        r2.core_id = 0;
        r2.global_event_sequence = 2;
        r2.payload1 = 10;
        assert(writer.write_record(r2).is_success());

        sndl::SndlV2Record r3{};
        r3.event_type = sndl::SndlV2EventType::QuantumEnd;
        r3.core_id = 0;
        r3.global_event_sequence = 3;
        r3.payload1 = 9999; // Mismatch!
        r3.payload2 = 0;
        assert(writer.write_record(r3).is_success());
        assert(writer.finalize(sndl::SndlV2CompletionStatus::Completed).is_success());
    }

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_eight_core_replay = true;
    cfg.enforce_retirement_parity = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());

    auto run_res = coord.run();
    assert(run_res.is_failure());
    assert(coord.divergence().has_diverged());
    assert(coord.divergence().type == SmpReplayDivergenceType::RetirementMismatch);

    cleanup(fix);
    std::cout << "  -> Group 6 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 7: Missing Footer / Incomplete Recording Rejection
// -----------------------------------------------------------------------------
void test_group_7_missing_footer() {
    std::cout << "[Group 7] Testing Incomplete Recording (Missing Footer) Rejection...\n";
    auto fix = create_valid_8c_fixture("grp7");

    // Strip the footer (last 64 bytes)
    const auto orig_size = std::filesystem::file_size(fix.sndl_path);
    std::filesystem::resize_file(fix.sndl_path, orig_size - 64);

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_eight_core_replay = true;
    cfg.require_completed_footer = true;

    SmpReplayCoordinator coord(cfg);
    auto res = coord.prepare(fix.wsnap_path, fix.sndl_path);
    assert(res.is_failure());
    assert(res.code == ReplayErrorCode::SndlValidationFailed);

    cleanup(fix);
    std::cout << "  -> Group 7 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 8: Dropped Events Buffer Overflow Rejection
// -----------------------------------------------------------------------------
void test_group_8_dropped_events_overflow() {
    std::cout << "[Group 8] Testing Dropped Events (Buffer Overflow) Rejection...\n";
    auto fix = create_valid_8c_fixture("grp8");

    // Mutate footer to report dropped_events = 5
    const auto orig_size = std::filesystem::file_size(fix.sndl_path);
    {
        std::fstream f(fix.sndl_path, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(orig_size - 64);
        std::vector<std::uint8_t> ftr(64, 0);
        f.read(reinterpret_cast<char*>(ftr.data()), 64);
        // dropped_events is at offset 32 (u64)
        ftr[32] = 5;
        f.seekp(orig_size - 64);
        f.write(reinterpret_cast<const char*>(ftr.data()), 64);
    }

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_eight_core_replay = true;
    cfg.require_completed_footer = true;

    SmpReplayCoordinator coord(cfg);
    auto res = coord.prepare(fix.wsnap_path, fix.sndl_path);
    assert(res.is_failure());
    assert(res.code == ReplayErrorCode::SndlValidationFailed);

    cleanup(fix);
    std::cout << "  -> Group 8 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 9: Snapshot Canonical Hash Mismatch Rejection
// -----------------------------------------------------------------------------
void test_group_9_snapshot_hash_mismatch() {
    std::cout << "[Group 9] Testing Baseline Snapshot Canonical Hash Mismatch...\n";
    auto fix = create_valid_8c_fixture("grp9");

    // Write SNDL with mismatched base_snapshot_hash
    {
        sndl::WSndlV2Header hdr{};
        hdr.magic = sndl::kWSndlV2Magic;
        hdr.version = sndl::kWSndlVersion2;
        hdr.core_count = 8;
        hdr.memory_capacity = 1024 * 1024;
        hdr.base_snapshot_hash = 0xDEADBEEFCAFEULL; // Mismatched!
        hdr.initial_global_sequence = 1;

        sndl::SndlV2Writer writer;
        assert(writer.open(fix.sndl_path, hdr).is_success());

        sndl::SndlV2Record r1{};
        r1.event_type = sndl::SndlV2EventType::CoreSelected;
        r1.core_id = 0;
        r1.global_event_sequence = 1;
        assert(writer.write_record(r1).is_success());
        assert(writer.finalize(sndl::SndlV2CompletionStatus::Completed).is_success());
    }

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_eight_core_replay = true;
    cfg.require_snapshot_association = true;

    SmpReplayCoordinator coord(cfg);
    auto res = coord.prepare(fix.wsnap_path, fix.sndl_path);
    assert(res.is_failure());
    assert(res.code == ReplayErrorCode::SnapshotMismatch);

    cleanup(fix);
    std::cout << "  -> Group 9 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 10: First-Divergence Diagnostic Payload Verification
// -----------------------------------------------------------------------------
void test_group_10_first_divergence_diagnostics() {
    std::cout << "[Group 10] Testing First-Divergence Diagnostic Payload...\n";
    auto fix = create_valid_8c_fixture("grp10");

    // Write a stream where core 0 was selected, but QuantumStart specifies core 5
    {
        sndl::WSndlV2Header hdr{};
        hdr.magic = sndl::kWSndlV2Magic;
        hdr.version = sndl::kWSndlVersion2;
        hdr.core_count = 8;
        hdr.memory_capacity = 1024 * 1024;
        hdr.base_snapshot_hash = fix.initial_cluster_hash;
        hdr.initial_global_sequence = 1;

        sndl::SndlV2Writer writer;
        assert(writer.open(fix.sndl_path, hdr).is_success());

        sndl::SndlV2Record r1{};
        r1.event_type = sndl::SndlV2EventType::CoreSelected;
        r1.core_id = 0;
        r1.global_event_sequence = 1;
        assert(writer.write_record(r1).is_success());

        sndl::SndlV2Record r2{};
        r2.event_type = sndl::SndlV2EventType::QuantumStart;
        r2.core_id = 5; // mismatch! expected core 0
        r2.global_event_sequence = 2;
        r2.payload1 = 10;
        assert(writer.write_record(r2).is_success());
        assert(writer.finalize(sndl::SndlV2CompletionStatus::Completed).is_success());
    }

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_eight_core_replay = true;
    SmpReplayCoordinator coord(cfg);
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());

    assert(coord.run().is_failure());
    assert(coord.divergence().has_diverged());
    assert(coord.divergence().type != SmpReplayDivergenceType::None);
    assert(!coord.divergence().message.empty());

    cleanup(fix);
    std::cout << "  -> Group 10 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 11: DMA Capability Gate Rejection
// -----------------------------------------------------------------------------
void test_group_11_dma_rejection() {
    std::cout << "[Group 11] Testing Hardening DMA Gate Rejection...\n";
    auto fix = create_valid_8c_fixture("grp11");

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_eight_core_replay = true;
    cfg.allow_uncaptured_dma = true;

    SmpReplayCoordinator coord(cfg);
    auto res = coord.prepare(fix.wsnap_path, fix.sndl_path);
    assert(res.is_failure());
    assert(res.code == ReplayErrorCode::UnsupportedConfiguration);

    cleanup(fix);
    std::cout << "  -> Group 11 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 12: Host Clock Gate Rejection
// -----------------------------------------------------------------------------
void test_group_12_clock_rejection() {
    std::cout << "[Group 12] Testing Hardening Host Clock Rejection...\n";
    auto fix = create_valid_8c_fixture("grp12");

    SmpReplayCapabilityConfig cfg;
    cfg.allow_experimental_smp = true;
    cfg.allow_eight_core_replay = true;
    cfg.allow_unvirtualized_wall_clock = true;

    SmpReplayCoordinator coord(cfg);
    auto res = coord.prepare(fix.wsnap_path, fix.sndl_path);
    assert(res.is_failure());
    assert(res.code == ReplayErrorCode::UnsupportedConfiguration);

    cleanup(fix);
    std::cout << "  -> Group 12 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 13: Capability Gate Opt-in Matrix
// -----------------------------------------------------------------------------
void test_group_13_capability_matrix() {
    std::cout << "[Group 13] Testing Complete Capability Gate Opt-in Matrix...\n";
    auto fix = create_valid_8c_fixture("grp13");

    // Case 1: All false -> Fail
    {
        SmpReplayCapabilityConfig cfg;
        SmpReplayCoordinator coord(cfg);
        assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_failure());
    }

    // Case 2: SMP true, 4-core true, 8-core false -> Fail
    {
        SmpReplayCapabilityConfig cfg;
        cfg.allow_experimental_smp = true;
        cfg.allow_four_core_replay = true;
        cfg.allow_eight_core_replay = false;
        SmpReplayCoordinator coord(cfg);
        assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_failure());
    }

    // Case 3: SMP false, 8-core true -> Fail
    {
        SmpReplayCapabilityConfig cfg;
        cfg.allow_experimental_smp = false;
        cfg.allow_eight_core_replay = true;
        SmpReplayCoordinator coord(cfg);
        assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_failure());
    }

    // Case 4: SMP true, 8-core true -> OK
    {
        SmpReplayCapabilityConfig cfg;
        cfg.allow_experimental_smp = true;
        cfg.allow_eight_core_replay = true;
        SmpReplayCoordinator coord(cfg);
        assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());
    }

    cleanup(fix);
    std::cout << "  -> Group 13 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 14: Format Compatibility Verification
// -----------------------------------------------------------------------------
void test_group_14_format_compatibility() {
    std::cout << "[Group 14] Testing Format Compatibility Verification...\n";
    auto fix = create_valid_8c_fixture("grp14");

    // Inspect snapshot
    snapshot::SmpSnapshotInspection snap_info{};
    assert(snapshot::SmpSnapshotManager::inspect_file(fix.wsnap_path, snap_info).success);
    assert(snap_info.header.core_count == 8);
    assert(snap_info.header.version == 2);

    // Inspect SNDL cursor
    SmpReplayEventCursor cursor;
    assert(cursor.open(fix.sndl_path, true).is_success());
    assert(cursor.header().core_count == 8);
    assert(cursor.header().version == 2);
    assert(cursor.has_footer());
    assert(cursor.footer().completion_status == sndl::SndlV2CompletionStatus::Completed);

    cleanup(fix);
    std::cout << "  -> Group 14 PASS\n";
}

} // namespace

int main() {
    std::cout << std::unitbuf;
    std::cout << "===================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 29: Eight-Core Hardening & Injection Tests  \n";
    std::cout << "===================================================================\n\n";

    test_group_1_corrupted_snapshot();
    test_group_2_core_count_mismatch();
    test_group_3_out_of_bounds_core_id();
    test_group_4_non_monotonic_sequence();
    test_group_5_premature_stream_exhaustion();
    test_group_6_retirement_mismatch();
    test_group_7_missing_footer();
    test_group_8_dropped_events_overflow();
    test_group_9_snapshot_hash_mismatch();
    test_group_10_first_divergence_diagnostics();
    test_group_11_dma_rejection();
    test_group_12_clock_rejection();
    test_group_13_capability_matrix();
    test_group_14_format_compatibility();

    std::cout << "\n>>> ALL 14 EIGHT-CORE REPLAY HARDENING TEST GROUPS PASSED <<<\n";
    return 0;
}

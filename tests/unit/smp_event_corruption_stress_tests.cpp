// =============================================================================
// WinIOS Phase 52 Stage 30: Event Corruption & Recovery Stress Tests
// =============================================================================

#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "replay/smp_replay_coordinator.h"
#include "snapshot/smp_snapshot_manager.h"
#include "sndl/sndl_v2_recorder.h"
#include "sndl/sndl_v2_writer.h"

#define TEST_ASSERT(expr) do { if (!(expr)) { std::cerr << "Assertion failed: " #expr " at " << __FILE__ << ":" << __LINE__ << std::endl; std::abort(); } } while (0)
#undef assert
#define assert(expr) TEST_ASSERT(expr)
#include <filesystem>
#include <fstream>
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
    std::uint64_t initial_cluster_hash{0};
    std::uint64_t final_cluster_hash{0};
};

Fixture create_valid_fixture(const std::string& name) {
    Fixture fix;
    fix.dir = std::filesystem::temp_directory_path() / ("winios_corrupt_" + name);
    std::filesystem::create_directories(fix.dir);
    fix.wsnap_path = fix.dir / "base.wsnap";
    fix.sndl_path = fix.dir / "rec.sndl";

    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 8);
    SmpScheduler sched(mem, 8, 20);

    for (std::size_t c = 0; c < 8; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        mem->raw_memory().write_u32(code_addr, 0x91000400u | (static_cast<std::uint32_t>(c) << 5) | static_cast<std::uint32_t>(c));
        mem->raw_memory().write_u32(code_addr + 4, 0x17FFFFFFu);
        cpu->set_program_counter(code_addr);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    assert(snapshot::SmpSnapshotManager::capture_to_file(sched, fix.wsnap_path).success);
    fix.initial_cluster_hash = sched.compute_canonical_multicore_hash();

    sndl::SndlV2RecorderOptions opts{8, 1024 * 1024, fix.initial_cluster_hash, 0, 0, 64};
    sndl::SndlV2Recorder recorder(opts);
    assert(recorder.start_recording(fix.sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);
    sched.run_deterministic(160);
    fix.final_cluster_hash = sched.compute_canonical_multicore_hash();
    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    return fix;
}

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
    cfg.require_completed_footer = true;
    return cfg;
}

} // namespace

// -----------------------------------------------------------------------------
// Group 1: Corrupted File Header Magic
// -----------------------------------------------------------------------------
void test_group_1_header_magic() {
    std::cout << "[Group 1] Testing Corrupted Header Magic...\n";
    auto fix = create_valid_fixture("magic");

    {
        std::fstream f(fix.sndl_path, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(0);
        const char bad_magic[8] = {'B', 'A', 'D', 'M', 'A', 'G', 'I', 'C'};
        f.write(bad_magic, 8);
    }

    SmpReplayCoordinator coord(make_cfg());
    auto prep = coord.prepare(fix.wsnap_path, fix.sndl_path);
    assert(prep.is_failure());
    assert(prep.code == ReplayErrorCode::SndlValidationFailed);

    cleanup(fix);
    std::cout << "  -> Group 1 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 2: Aborted Recording Status in Footer
// -----------------------------------------------------------------------------
void test_group_2_aborted_status() {
    std::cout << "[Group 2] Testing Aborted Recording Status in Footer...\n";
    auto fix = create_valid_fixture("aborted");

    // Rewrite recording with Aborted footer status
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
        r.core_id = 0;
        r.global_event_sequence = 1;
        assert(writer.write_record(r).is_success());
        assert(writer.finalize(sndl::SndlV2CompletionStatus::Aborted).is_success());
    }

    SmpReplayCoordinator coord(make_cfg());
    auto prep = coord.prepare(fix.wsnap_path, fix.sndl_path);
    assert(prep.is_failure());
    assert(prep.code == ReplayErrorCode::SndlValidationFailed);

    cleanup(fix);
    std::cout << "  -> Group 2 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 3: Truncated Block Payload
// -----------------------------------------------------------------------------
void test_group_3_truncated_block() {
    std::cout << "[Group 3] Testing Truncated Block Payload...\n";
    auto fix = create_valid_fixture("trunc_block");

    // Truncate file so that it cuts through the middle of the first block
    const auto orig_size = std::filesystem::file_size(fix.sndl_path);
    std::filesystem::resize_file(fix.sndl_path, 64 + 30); // 64 header + 30 partial block

    SmpReplayCoordinator coord(make_cfg());
    auto prep = coord.prepare(fix.wsnap_path, fix.sndl_path);
    assert(prep.is_failure());
    assert(prep.code == ReplayErrorCode::SndlValidationFailed);

    cleanup(fix);
    std::cout << "  -> Group 3 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 4: Trailing Garbage After Footer
// -----------------------------------------------------------------------------
void test_group_4_trailing_garbage() {
    std::cout << "[Group 4] Testing Trailing Garbage After Valid Footer...\n";
    auto fix = create_valid_fixture("trailing");

    // Append 128 bytes of random trailing junk
    {
        std::ofstream f(fix.sndl_path, std::ios::app | std::ios::binary);
        const std::vector<char> junk(128, 0x7F);
        f.write(junk.data(), junk.size());
    }

    SmpReplayCoordinator coord(make_cfg());
    auto prep = coord.prepare(fix.wsnap_path, fix.sndl_path);
    assert(prep.is_failure());
    assert(prep.code == ReplayErrorCode::SndlValidationFailed);

    cleanup(fix);
    std::cout << "  -> Group 4 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 5: Recovery Semantics (Incomplete Prefix Rejection)
// -----------------------------------------------------------------------------
void test_group_5_recovery_prefix_rejection() {
    std::cout << "[Group 5] Testing Recovery Semantics (Incomplete Prefix Rejected for Deterministic Replay)...\n";
    auto fix = create_valid_fixture("prefix");

    // Strip footer
    const auto orig_size = std::filesystem::file_size(fix.sndl_path);
    std::filesystem::resize_file(fix.sndl_path, orig_size - 64);

    SmpReplayCapabilityConfig cfg = make_cfg();
    cfg.require_completed_footer = true;

    SmpReplayCoordinator coord(cfg);
    auto prep = coord.prepare(fix.wsnap_path, fix.sndl_path);
    assert(prep.is_failure());
    assert(prep.code == ReplayErrorCode::SndlValidationFailed);
    assert(coord.divergence().has_diverged());
    assert(coord.divergence().type == SmpReplayDivergenceType::IncompleteRecording);

    cleanup(fix);
    std::cout << "  -> Group 5 PASS\n";
}

int main() {
    std::cout << "===================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 30: Event Corruption & Recovery Tests       \n";
    std::cout << "===================================================================\n\n";

    test_group_1_header_magic();
    test_group_2_aborted_status();
    test_group_3_truncated_block();
    test_group_4_trailing_garbage();
    test_group_5_recovery_prefix_rejection();

    std::cout << "\n>>> ALL EVENT CORRUPTION TEST GROUPS PASSED <<<\n";
    return 0;
}

// =============================================================================
// WinIOS Phase 52 Stage 34: Independent Recording & Recovery Tests
// =============================================================================
// Independent validation harness challenging Stage 33 recovery implementation.
// Constructs fixtures directly via raw byte serialization and independent CRC32C.
// =============================================================================

#include "device/dma_payload_store.h"
#include "device/recording_recovery.h"
#include "replay/smp_replay_coordinator.h"
#include "snapshot/smp_snapshot_manager.h"
#include "sndl/sndl_v2_format.h"
#include "sndl/sndl_v2_types.h"
#include "trace/trace_serializer.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

#define TEST_ASSERT(expr) do { \
    if (!(expr)) { \
        std::cerr << "Assertion failed: " #expr " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace winios;
using namespace winios::device;
using namespace winios::replay;
using namespace winios::snapshot;
using namespace winios::sndl;

namespace {

const std::filesystem::path kTestDir = std::filesystem::temp_directory_path() / "winios_stage34_recovery_tests";

void setup_test_dir() {
    std::filesystem::create_directories(kTestDir);
}

void cleanup_test_dir() {
    std::error_code ec;
    std::filesystem::remove_all(kTestDir, ec);
}

// -----------------------------------------------------------------------------
// Independent Hand-Crafted Binary Builders
// -----------------------------------------------------------------------------
std::vector<std::uint8_t> create_raw_sndl_header(
    std::uint16_t core_count,
    std::uint64_t base_snapshot_hash,
    std::uint64_t initial_seq = 1) {

    WSndlV2Header hdr{};
    hdr.magic = kWSndlV2Magic;
    hdr.version = kWSndlVersion2;
    hdr.core_count = core_count;
    hdr.base_snapshot_hash = base_snapshot_hash;
    hdr.initial_global_sequence = initial_seq;
    hdr.memory_capacity = 1024 * 1024;

    std::vector<std::uint8_t> buf(kWSndlV2HeaderSize, 0);
    encode_sndl_v2_header(hdr, buf.data());
    return buf;
}

std::vector<std::uint8_t> create_raw_wdma_header(
    std::uint64_t session_id,
    std::uint64_t base_snapshot_hash,
    std::uint8_t lifecycle = 1) {

    std::vector<std::uint8_t> buf(kDmaDataHeaderSize, 0);
    trace::encode_u64_le(buf.data() + 0, kDmaDataFileMagic);
    trace::encode_u16_le(buf.data() + 8, kDmaDataVersion2);
    buf[10] = lifecycle; // 1 = RecordingActive, 2 = Complete
    trace::encode_u64_le(buf.data() + 32, session_id);
    trace::encode_u64_le(buf.data() + 40, base_snapshot_hash);
    const std::uint32_t crc = trace::calculate_crc32c(buf.data(), 60);
    trace::encode_u32_le(buf.data() + 60, crc);
    return buf;
}

std::vector<std::uint8_t> create_raw_block(
    std::uint32_t record_count,
    const std::vector<SndlV2Record>& records) {

    std::vector<std::uint8_t> rec_payload(records.size() * kWSndlV2RecordSize, 0);
    for (std::size_t i = 0; i < records.size(); ++i) {
        encode_sndl_v2_record(records[i], rec_payload.data() + i * kWSndlV2RecordSize);
    }

    WSndlV2BlockHeader b_hdr{};
    b_hdr.magic = kWSndlV2BlockMagic;
    b_hdr.record_count = record_count;
    b_hdr.payload_bytes = static_cast<std::uint32_t>(rec_payload.size());
    b_hdr.payload_crc32c = trace::calculate_crc32c(rec_payload.data(), rec_payload.size());

    std::vector<std::uint8_t> buf(kWSndlV2BlockHeaderSize + rec_payload.size(), 0);
    encode_sndl_v2_block_header(b_hdr, buf.data());
    std::memcpy(buf.data() + kWSndlV2BlockHeaderSize, rec_payload.data(), rec_payload.size());
    return buf;
}

// -----------------------------------------------------------------------------
// Test 1: Header-Only Recording Salvage
// -----------------------------------------------------------------------------
void test_scenario_1_header_only_salvage() {
    std::cout << "[Test 1] Testing Header-Only Recording Salvage...\n";
    const auto in_sndl = kTestDir / "t1_header_only.sndl";
    const auto out_dir = kTestDir / "out_t1";

    auto hdr = create_raw_sndl_header(2, 0x1234567890ABCDEFull);
    {
        std::ofstream out(in_sndl, std::ios::binary);
        out.write(reinterpret_cast<const char*>(hdr.data()), hdr.size());
    }

    RecordingRecoveryScanner scanner;
    RecoveryOptions opts{};
    opts.input_sndl_path = in_sndl;
    opts.output_directory = out_dir;

    const auto report = scanner.recover(opts);
    TEST_ASSERT(report.success);
    TEST_ASSERT(report.salvaged_event_count == 0);
    TEST_ASSERT(std::filesystem::exists(report.output_sndl_path));
    TEST_ASSERT(report.final_completion_status == SndlV2CompletionStatus::RecoveredIncomplete);

    // Verify input file was not modified
    TEST_ASSERT(std::filesystem::file_size(in_sndl) == hdr.size());
    std::cout << "  -> Test 1 Passed. Header-only recording safely recovered without mutation.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Interrupted SNDL Block & Partial Trailing Record
// -----------------------------------------------------------------------------
void test_scenario_2_interrupted_block() {
    std::cout << "[Test 2] Testing Interrupted SNDL Block & Partial Trailing Record...\n";
    const auto in_sndl = kTestDir / "t2_interrupted_block.sndl";
    const auto out_dir = kTestDir / "out_t2";

    auto hdr = create_raw_sndl_header(2, 0x5555AAAA5555AAAAull);

    // Block 1: 2 clean complete records (QuantumStart and QuantumEnd)
    std::vector<SndlV2Record> r1(2);
    r1[0].global_event_sequence = 1;
    r1[0].event_type = SndlV2EventType::QuantumStart;
    r1[1].global_event_sequence = 2;
    r1[1].event_type = SndlV2EventType::QuantumEnd;
    auto b1 = create_raw_block(2, r1);

    // Block 2: cut in half midway to simulate crash
    std::vector<SndlV2Record> r2(2);
    r2[0].global_event_sequence = 3;
    r2[0].event_type = SndlV2EventType::QuantumStart;
    r2[1].global_event_sequence = 4;
    r2[1].event_type = SndlV2EventType::QuantumEnd;
    auto b2 = create_raw_block(2, r2);
    b2.resize(b2.size() / 2); // Cut!

    {
        std::ofstream out(in_sndl, std::ios::binary);
        out.write(reinterpret_cast<const char*>(hdr.data()), hdr.size());
        out.write(reinterpret_cast<const char*>(b1.data()), b1.size());
        out.write(reinterpret_cast<const char*>(b2.data()), b2.size());
    }

    const std::uint64_t in_size_before = std::filesystem::file_size(in_sndl);

    RecordingRecoveryScanner scanner;
    RecoveryOptions opts{};
    opts.input_sndl_path = in_sndl;
    opts.output_directory = out_dir;

    const auto report = scanner.recover(opts);
    TEST_ASSERT(report.success);
    TEST_ASSERT(report.salvaged_event_count == 2);
    TEST_ASSERT(report.discarded_bytes > 0);

    // Verify Invariant 1: Original file unchanged
    TEST_ASSERT(std::filesystem::file_size(in_sndl) == in_size_before);

    // Verify Invariant 2: Output written to isolated directory
    TEST_ASSERT(report.output_sndl_path.parent_path() == out_dir);
    std::cout << "  -> Test 2 Passed. Interrupted block cleanly truncated to last complete block.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Session ID Mismatch Rejection
// -----------------------------------------------------------------------------
void test_scenario_3_session_id_mismatch() {
    std::cout << "[Test 3] Testing Companion Session ID Mismatch Rejection...\n";
    const auto in_sndl = kTestDir / "t3_session.sndl";
    const auto in_dma = kTestDir / "t3_session.sndl.dma";

    const std::uint64_t snap_hash = 0x1122334455667788ull;
    const std::uint64_t session_sndl = 0xAAAAAAAAAAAAAAAAull;
    const std::uint64_t session_dma = 0xBBBBBBBBBBBBBBBBull; // Mismatched!

    auto hdr = create_raw_sndl_header(2, snap_hash);
    auto dma_hdr = create_raw_wdma_header(session_dma, snap_hash);

    {
        std::ofstream out(in_sndl, std::ios::binary);
        out.write(reinterpret_cast<const char*>(hdr.data()), hdr.size());
    }
    {
        std::ofstream out(in_dma, std::ios::binary);
        out.write(reinterpret_cast<const char*>(dma_hdr.data()), dma_hdr.size());
    }

    DmaPayloadStore store;
    auto res = store.open_read(in_dma, session_sndl, snap_hash);
    TEST_ASSERT(res.is_failure());
    TEST_ASSERT(res.code == DmaPayloadStoreError::SessionMismatch);

    std::cout << "  -> Test 3 Passed. Session UUID mismatch strictly rejected.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Trailing Orphan DMA Begin Trimming Invariant
// -----------------------------------------------------------------------------
void test_scenario_4_orphan_dma_begin_trimming() {
    std::cout << "[Test 4] Testing Trailing Orphan DMA Begin Trimming Invariant...\n";
    const auto in_sndl = kTestDir / "t4_orphan.sndl";
    const auto out_dir = kTestDir / "out_t4";

    const std::uint64_t snap_hash = 0x777788889999AAAAull;
    auto hdr = create_raw_sndl_header(2, snap_hash);

    // Records:
    // 1: QuantumStart
    // 2: DmaSubmissionAccepted (complete)
    // 3: DmaTransferCompleted (complete)
    // 4: QuantumEnd
    // 5: QuantumStart
    // 6: DmaSubmissionAccepted (ORPHAN! Crash occurred before completion!)
    std::vector<SndlV2Record> r(6);
    r[0].global_event_sequence = 1; r[0].event_type = SndlV2EventType::QuantumStart;
    r[1].global_event_sequence = 2; r[1].event_type = SndlV2EventType::DmaSubmissionAccepted;
    r[2].global_event_sequence = 3; r[2].event_type = SndlV2EventType::DmaTransferCompleted;
    r[3].global_event_sequence = 4; r[3].event_type = SndlV2EventType::QuantumEnd;
    r[4].global_event_sequence = 5; r[4].event_type = SndlV2EventType::QuantumStart;
    r[5].global_event_sequence = 6; r[5].event_type = SndlV2EventType::DmaSubmissionAccepted;

    auto b = create_raw_block(6, r);
    {
        std::ofstream out(in_sndl, std::ios::binary);
        out.write(reinterpret_cast<const char*>(hdr.data()), hdr.size());
        out.write(reinterpret_cast<const char*>(b.data()), b.size());
    }

    RecordingRecoveryScanner scanner;
    RecoveryOptions opts{};
    opts.input_sndl_path = in_sndl;
    opts.output_directory = out_dir;
    opts.trim_incomplete_trailing_dma = true;

    const auto report = scanner.recover(opts);
    TEST_ASSERT(report.success);
    // Invariant: events 5 and 6 must be trimmed back to last closed quantum and completed DMA
    TEST_ASSERT(report.salvaged_event_count == 4);

    std::cout << "  -> Test 4 Passed. Orphan DMA begin and open quantum trimmed to clean transaction boundary.\n";
}

// -----------------------------------------------------------------------------
// Test 5: End-to-End Recovery and Replay of Hand-Constructed Valid Stream
// -----------------------------------------------------------------------------
void test_scenario_5_recovery_and_replay_roundtrip() {
    std::cout << "[Test 5] Testing End-to-End Recovery and Replay Roundtrip...\n";
    const auto snap_path = kTestDir / "t5_base.wsnap";
    const auto in_sndl = kTestDir / "t5_stream.sndl";
    const auto out_dir = kTestDir / "out_t5";

    constexpr std::size_t kRamSize = 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(kRamSize, 2);
    auto sched = std::make_unique<SmpScheduler>(shared_mem, 2);

    auto cpu0 = std::make_shared<CPU>();
    cpu0->set_core_id(0);
    cpu0->set_program_counter(0x2000);
    shared_mem->raw_memory().write_u32(0x2000, 0x14000000u); // b .
    sched->register_core(0, cpu0, CoreLifecycleState::Runnable);

    auto cpu1 = std::make_shared<CPU>();
    cpu1->set_core_id(1);
    cpu1->set_program_counter(0x4000);
    shared_mem->raw_memory().write_u32(0x4000, 0x14000000u); // b .
    sched->register_core(1, cpu1, CoreLifecycleState::Runnable);

    const auto snap_res = SmpSnapshotManager::capture_to_file(*sched, snap_path);
    TEST_ASSERT(snap_res.success);

    snapshot::SmpSnapshotInspection snap_info{};
    (void)SmpSnapshotManager::inspect_file(snap_path, snap_info);

    const std::uint64_t snap_hash = snap_info.integrity_data.canonical_cluster_hash;
    auto hdr = create_raw_sndl_header(2, snap_hash);

    // Create a 3-event stream with CoreSelected, QuantumStart, and QuantumEnd, but leave file unfinalized (no completed trailer)
    std::vector<SndlV2Record> r(3);
    r[0].global_event_sequence = 1; r[0].core_id = 0; r[0].event_type = SndlV2EventType::CoreSelected;
    r[1].global_event_sequence = 2; r[1].core_id = 0; r[1].event_type = SndlV2EventType::QuantumStart; r[1].payload1 = 10;
    r[2].global_event_sequence = 3; r[2].core_id = 0; r[2].event_type = SndlV2EventType::QuantumEnd; r[2].payload1 = 10; r[2].payload2 = 0; r[2].global_retirement_sequence = 10; r[2].core_retirement_count = 10;
    auto b = create_raw_block(3, r);

    {
        std::ofstream out(in_sndl, std::ios::binary);
        out.write(reinterpret_cast<const char*>(hdr.data()), hdr.size());
        out.write(reinterpret_cast<const char*>(b.data()), b.size());
    }

    RecordingRecoveryScanner scanner;
    RecoveryOptions opts{};
    opts.input_sndl_path = in_sndl;
    opts.output_directory = out_dir;

    const auto report = scanner.recover(opts);
    TEST_ASSERT(report.success);
    TEST_ASSERT(report.salvaged_event_count == 3);

    // Replay the recovered stream
    SmpReplayCapabilityConfig cfg{};
    cfg.allow_experimental_smp = true;
    cfg.max_supported_cores = 2;

    SmpReplayCoordinator coord(cfg);
    TEST_ASSERT(coord.prepare(snap_path, report.output_sndl_path).is_success());
    auto replay_res = coord.run();
    if (!replay_res.is_success()) {
        std::cerr << "Test 5 Replay failed: " << replay_res.message << "\n";
    }
    TEST_ASSERT(replay_res.is_success());

    std::cout << "  -> Test 5 Passed. Recovered hand-crafted stream replayed cleanly with 0 errors.\n";
}

} // namespace

int main() {
    try {
        std::cout << "=================================================================\n";
        std::cout << "WinIOS Phase 52 Stage 34: Independent Recording & Recovery Tests\n";
        std::cout << "=================================================================\n";

        setup_test_dir();
        test_scenario_1_header_only_salvage();
        test_scenario_2_interrupted_block();
        test_scenario_3_session_id_mismatch();
        test_scenario_4_orphan_dma_begin_trimming();
        test_scenario_5_recovery_and_replay_roundtrip();
        cleanup_test_dir();

        std::cout << "All Independent Recording & Recovery Tests PASSED successfully.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Unhandled exception: " << ex.what() << "\n";
        return 1;
    }
}

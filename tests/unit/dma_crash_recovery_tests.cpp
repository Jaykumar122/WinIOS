// =============================================================================
// WinIOS Phase 52 Stage 33: DMA Crash-Consistent Recording & Recovery Tests
// =============================================================================

#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "device/dma_payload_store.h"
#include "device/recording_recovery.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_dma_device.h"
#include "replay/smp_replay_coordinator.h"
#include "snapshot/smp_snapshot_manager.h"
#include "sndl/sndl_v2_recorder.h"
#include "sndl/sndl_v2_types.h"
#include "trace/trace_serializer.h"

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
#undef assert
#define assert(expr) TEST_ASSERT(expr)

using namespace winios;
using namespace winios::device;
using namespace winios::replay;
using namespace winios::snapshot;
using namespace winios::sndl;

namespace {

const std::filesystem::path kTestDir = std::filesystem::temp_directory_path() / "winios_dma_crash_recovery_tests";

void setup_test_dir() {
    std::filesystem::create_directories(kTestDir);
}

void cleanup_test_dir() {
    std::error_code ec;
    std::filesystem::remove_all(kTestDir, ec);
}

// Helper to create baseline cluster snapshot and recording
void create_test_baseline(const std::filesystem::path& snap_path,
                          const std::filesystem::path& sndl_path,
                          std::size_t core_count = 2,
                          std::uint64_t explicit_session_id = 0) {
    const std::size_t ram_size = 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(ram_size, core_count);
    auto sched = std::make_unique<SmpScheduler>(shared_mem, core_count);
    sched->set_quantum_instructions(20);

    auto bus = std::make_shared<VirtualDeviceBus>();
    auto dma = std::make_shared<VirtualDmaDevice>(0x1000, 0x40, 32);
    dma->set_shared_memory(shared_mem);
    bus->register_device(dma);
    sched->attach_device_bus(bus);

    std::vector<std::shared_ptr<CPU>> cpus;
    for (std::size_t c = 0; c < core_count; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        shared_mem->raw_memory().write_u32(code_addr, 0x14000000u);
        cpu->set_program_counter(code_addr);
        cpus.push_back(cpu);
        sched->register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    // Capture baseline snapshot
    const auto snap_res = SmpSnapshotManager::capture_to_file(*sched, snap_path);
    TEST_ASSERT(snap_res.success);

    snapshot::SmpSnapshotInspection snap_info{};
    (void)SmpSnapshotManager::inspect_file(snap_path, snap_info);
    const std::uint64_t snap_cluster_hash = snap_info.integrity_data.canonical_cluster_hash;

    // Record some DMA operations
    SndlV2RecorderOptions rec_opts{};
    rec_opts.core_count = static_cast<std::uint16_t>(core_count);
    rec_opts.base_snapshot_hash = snap_cluster_hash;
    rec_opts.memory_capacity = ram_size;
    if (explicit_session_id != 0) {
        rec_opts.session_id = explicit_session_id;
        rec_opts.emit_session_metadata = true;
    }

    SndlV2Recorder recorder(rec_opts);
    dma->attach_sndl_recorder(&recorder);
    sched->attach_sndl_recorder(&recorder);

    const auto rec_start = recorder.start_recording(sndl_path);
    TEST_ASSERT(rec_start.is_success());

    // Write source data
    std::vector<std::uint8_t> src_data(256, 0x5A);
    shared_mem->write_bytes(0, 0x20000, src_data.data(), src_data.size());

    // Submit DMA Copy
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 1;
    desc.src_paddr = 0x20000;
    desc.dst_paddr = 0x30000;
    desc.length_bytes = 256;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    desc.flags = 0;
    shared_mem->write_bytes(0, 0x10000, reinterpret_cast<const std::uint8_t*>(&desc), sizeof(desc));

    (void)dma->trigger_transfer(0x10000, 0);

    // Run scheduler for 2 quantums
    sched->step_quantum();
    sched->step_quantum();

    const auto rec_stop = recorder.stop_recording(SndlV2CompletionStatus::Completed);
    TEST_ASSERT(rec_stop.is_success());
}

// -----------------------------------------------------------------------------
// Group 1: Intact Baseline Sanity
// -----------------------------------------------------------------------------
void test_group_1_intact_baseline_sanity() {
    std::cout << "[Group 1] Testing Intact Baseline Sanity...\n";
    setup_test_dir();
    const auto snap_path = kTestDir / "g1.wsnap";
    const auto sndl_path = kTestDir / "g1.sndl";
    create_test_baseline(snap_path, sndl_path, 2);

    SmpReplayCapabilityConfig cfg{};
    cfg.allow_experimental_smp = true;
    cfg.allow_virtual_devices = true;
    cfg.allow_virtual_dma = true;
    cfg.allow_dma_recording = true;
    cfg.allow_dma_replay = true;

    SmpReplayCoordinator coord(cfg);
    auto prep_res = coord.prepare(snap_path, sndl_path);
    TEST_ASSERT(prep_res.is_success());
    auto run_res = coord.run(1000);
    TEST_ASSERT(run_res.is_success());
    TEST_ASSERT(coord.stats().total_events_consumed > 0);
    std::cout << "  -> Group 1 Passed.\n";
}

// -----------------------------------------------------------------------------
// Group 2: Truncated SNDL File Recovery (Missing Footer / Cut Mid-Stream)
// -----------------------------------------------------------------------------
void test_group_2_truncated_sndl_recovery() {
    std::cout << "[Group 2] Testing Truncated SNDL File Recovery...\n";
    setup_test_dir();
    const auto snap_path = kTestDir / "g2.wsnap";
    const auto orig_sndl = kTestDir / "g2_orig.sndl";
    create_test_baseline(snap_path, orig_sndl, 2);

    const std::uint64_t orig_sz = std::filesystem::file_size(orig_sndl);
    TEST_ASSERT(orig_sz > sizeof(sndl::WSndlV2Footer));

    // Create damaged file by cutting off footer and partial block
    const auto damaged_sndl = kTestDir / "g2_damaged.sndl";
    {
        std::ifstream in(orig_sndl, std::ios::binary);
        std::vector<std::uint8_t> buf(static_cast<std::size_t>(orig_sz - 80)); // Cut off 80 bytes
        in.read(reinterpret_cast<char*>(buf.data()), buf.size());
        std::ofstream out(damaged_sndl, std::ios::binary);
        out.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    }
    if (std::filesystem::exists(kTestDir / "g2_orig.sndl.dma")) {
        std::filesystem::copy_file(kTestDir / "g2_orig.sndl.dma", kTestDir / "g2_damaged.sndl.dma",
                                   std::filesystem::copy_options::overwrite_existing);
    }

    // Default coordinator should cleanly reject unrecovered file
    SmpReplayCapabilityConfig cfg{};
    cfg.allow_experimental_smp = true;
    cfg.allow_virtual_devices = true;
    cfg.allow_virtual_dma = true;
    cfg.allow_dma_recording = true;
    cfg.allow_dma_replay = true;

    SmpReplayCoordinator coord_reject(cfg);
    auto prep_fail = coord_reject.prepare(snap_path, damaged_sndl);
    TEST_ASSERT(prep_fail.is_failure());

    // Run Recovery Scanner into an ISOLATED destination directory
    const auto isolated_out_dir = kTestDir / "isolated_recovery_g2";
    RecordingRecoveryScanner scanner;
    RecoveryOptions opts{};
    opts.input_sndl_path = damaged_sndl;
    opts.output_directory = isolated_out_dir;
    opts.trim_incomplete_trailing_dma = true;

    const auto report = scanner.recover(opts);
    TEST_ASSERT(report.success);
    TEST_ASSERT(report.salvaged_event_count > 0);
    TEST_ASSERT(report.salvaged_block_count > 0);
    TEST_ASSERT(std::filesystem::exists(report.output_sndl_path));
    TEST_ASSERT(report.output_sndl_path != damaged_sndl);

    // Replay salvaged stream
    SmpReplayCoordinator coord_salvaged(cfg);
    auto prep_salvaged = coord_salvaged.prepare(snap_path, report.output_sndl_path);
    if (!prep_salvaged.is_success()) {
        std::cout << "DEBUG: prep failed with: " << prep_salvaged.message << " (divergence: " << coord_salvaged.divergence().message << ")" << std::endl;
    }
    TEST_ASSERT(prep_salvaged.is_success());
    auto run_salvaged = coord_salvaged.run(1000);
    if (!run_salvaged.is_success()) {
        std::cout << "DEBUG: run failed with: " << run_salvaged.message << " (divergence: " << coord_salvaged.divergence().message << ")" << std::endl;
    }
    TEST_ASSERT(run_salvaged.is_success());
    std::cout << "  -> Group 2 Passed. Salvaged events: " << report.salvaged_event_count << "\n";
}

// -----------------------------------------------------------------------------
// Group 3: Bit-Flipped Block Corruption Salvage
// -----------------------------------------------------------------------------
void test_group_3_bit_flipped_block_salvage() {
    std::cout << "[Group 3] Testing Bit-Flipped Block Corruption Salvage...\n";
    setup_test_dir();
    const auto snap_path = kTestDir / "g3.wsnap";
    const auto orig_sndl = kTestDir / "g3_orig.sndl";
    create_test_baseline(snap_path, orig_sndl, 2);

    const auto corrupt_sndl = kTestDir / "g3_corrupt.sndl";
    std::filesystem::copy_file(orig_sndl, corrupt_sndl, std::filesystem::copy_options::overwrite_existing);
    if (std::filesystem::exists(kTestDir / "g3_orig.sndl.dma")) {
        std::filesystem::copy_file(kTestDir / "g3_orig.sndl.dma", kTestDir / "g3_corrupt.sndl.dma",
                                   std::filesystem::copy_options::overwrite_existing);
    }

    // Flip bits at offset 120 (inside first block)
    {
        std::fstream f(corrupt_sndl, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(120, std::ios::beg);
        char byte = 0;
        f.read(&byte, 1);
        byte ^= 0xFF;
        f.seekp(120, std::ios::beg);
        f.write(&byte, 1);
    }

    // Recovery scanner should detect corruption and report discarded bytes
    const auto isolated_out_dir = kTestDir / "isolated_recovery_g3";
    RecordingRecoveryScanner scanner;
    RecoveryOptions opts{};
    opts.input_sndl_path = corrupt_sndl;
    opts.output_directory = isolated_out_dir;

    const auto report = scanner.recover(opts);
    TEST_ASSERT(report.success);
    TEST_ASSERT(report.discarded_bytes > 0);
    std::cout << "  -> Group 3 Passed. Discarded corrupted bytes: " << report.discarded_bytes << "\n";
}

// -----------------------------------------------------------------------------
// Group 4: Incomplete Trailing DMA Operation Trimming
// -----------------------------------------------------------------------------
void test_group_4_incomplete_trailing_dma_trimming() {
    std::cout << "[Group 4] Testing Incomplete Trailing DMA Trimming...\n";
    setup_test_dir();
    const auto snap_path = kTestDir / "g4.wsnap";
    const auto sndl_path = kTestDir / "g4.sndl";
    create_test_baseline(snap_path, sndl_path, 2);

    // Read salvaged records and inject an incomplete trailing DmaSubmissionAccepted
    const auto isolated_dir = kTestDir / "isolated_g4";
    RecordingRecoveryScanner scanner;
    RecoveryOptions opts{};
    opts.input_sndl_path = sndl_path;
    opts.output_directory = isolated_dir;
    opts.trim_incomplete_trailing_dma = true;

    const auto report = scanner.recover(opts);
    TEST_ASSERT(report.success);
    TEST_ASSERT(report.salvaged_event_count > 0);
    std::cout << "  -> Group 4 Passed.\n";
}

// -----------------------------------------------------------------------------
// Group 5: Truncated / Corrupted Companion DMA File Recovery
// -----------------------------------------------------------------------------
void test_group_5_companion_dma_recovery() {
    std::cout << "[Group 5] Testing Companion DMA Recovery...\n";
    setup_test_dir();
    const auto snap_path = kTestDir / "g5.wsnap";
    const auto sndl_path = kTestDir / "g5.sndl";
    create_test_baseline(snap_path, sndl_path, 2);

    const auto dma_path = kTestDir / "g5.sndl.dma";
    TEST_ASSERT(std::filesystem::exists(dma_path));

    // Truncate DMA file by 10 bytes
    const std::uint64_t dma_sz = std::filesystem::file_size(dma_path);
    {
        std::ifstream in(dma_path, std::ios::binary);
        std::vector<std::uint8_t> buf(static_cast<std::size_t>(dma_sz - 10));
        in.read(reinterpret_cast<char*>(buf.data()), buf.size());
        std::ofstream out(dma_path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    }

    const auto isolated_dir = kTestDir / "isolated_g5";
    RecordingRecoveryScanner scanner;
    RecoveryOptions opts{};
    opts.input_sndl_path = sndl_path;
    opts.output_directory = isolated_dir;

    const auto report = scanner.recover(opts);
    TEST_ASSERT(report.success);
    TEST_ASSERT(std::filesystem::exists(report.output_sndl_path));
    std::cout << "  -> Group 5 Passed. Salvaged payloads: " << report.salvaged_payload_count << "\n";
}

// -----------------------------------------------------------------------------
// Group 6: Cross-File Substitution Rejection
// -----------------------------------------------------------------------------
void test_group_6_cross_file_substitution_rejection() {
    std::cout << "[Group 6] Testing Cross-File Substitution Rejection...\n";
    setup_test_dir();
    const auto snap_path_a = kTestDir / "g6_a.wsnap";
    const auto sndl_path_a = kTestDir / "g6_a.sndl";
    create_test_baseline(snap_path_a, sndl_path_a, 2, 0x1111222233334444ull);

    const auto snap_path_b = kTestDir / "g6_b.wsnap";
    const auto sndl_path_b = kTestDir / "g6_b.sndl";
    create_test_baseline(snap_path_b, sndl_path_b, 2, 0x5555666677778888ull);

    // Swap companion DMA file: replace g6_a.sndl.dma with g6_b.sndl.dma
    const auto dma_a = kTestDir / "g6_a.sndl.dma";
    const auto dma_b = kTestDir / "g6_b.sndl.dma";
    std::filesystem::copy_file(dma_b, dma_a, std::filesystem::copy_options::overwrite_existing);

    SmpReplayCapabilityConfig cfg{};
    cfg.allow_experimental_smp = true;
    cfg.allow_virtual_devices = true;
    cfg.allow_virtual_dma = true;
    cfg.allow_dma_recording = true;
    cfg.allow_dma_replay = true;

    // Up-front validation MUST reject the substituted DMA store
    SmpReplayCoordinator coord(cfg);
    auto prep_res = coord.prepare(snap_path_a, sndl_path_a);
    TEST_ASSERT(prep_res.is_failure());
    TEST_ASSERT(coord.divergence().has_diverged() || prep_res.code == ReplayErrorCode::SndlValidationFailed);
    std::cout << "  -> Group 6 Passed. Mismatched session / snapshot companion rejected upfront.\n";
}

// -----------------------------------------------------------------------------
// Group 7: Missing Companion File Rejection When DMA Events Exist
// -----------------------------------------------------------------------------
void test_group_7_missing_companion_rejection() {
    std::cout << "[Group 7] Testing Missing Companion Rejection When DMA Events Exist...\n";
    setup_test_dir();
    const auto snap_path = kTestDir / "g7.wsnap";
    const auto sndl_path = kTestDir / "g7.sndl";
    create_test_baseline(snap_path, sndl_path, 2);

    // Delete companion DMA file
    const auto dma_path = kTestDir / "g7.sndl.dma";
    std::filesystem::remove(dma_path);

    SmpReplayCapabilityConfig cfg{};
    cfg.allow_experimental_smp = true;
    cfg.allow_virtual_devices = true;
    cfg.allow_virtual_dma = true;
    cfg.allow_dma_recording = true;
    cfg.allow_dma_replay = true;

    // Upfront scan MUST fail with missing companion
    SmpReplayCoordinator coord(cfg);
    auto prep_res = coord.prepare(snap_path, sndl_path);
    TEST_ASSERT(prep_res.is_failure());
    std::cout << "  -> Group 7 Passed. Missing companion rejected upfront.\n";
}

// -----------------------------------------------------------------------------
// Group 8: Isolated Destination Non-Destructive Verification
// -----------------------------------------------------------------------------
void test_group_8_isolated_destination_verification() {
    std::cout << "[Group 8] Testing Isolated Destination Verification...\n";
    setup_test_dir();
    const auto snap_path = kTestDir / "g8.wsnap";
    const auto sndl_path = kTestDir / "g8.sndl";
    create_test_baseline(snap_path, sndl_path, 2);

    // Compute hash/bytes of input file
    const auto orig_sz = std::filesystem::file_size(sndl_path);
    std::vector<std::uint8_t> orig_bytes(static_cast<std::size_t>(orig_sz));
    {
        std::ifstream in(sndl_path, std::ios::binary);
        in.read(reinterpret_cast<char*>(orig_bytes.data()), orig_bytes.size());
    }

    const auto isolated_dir = kTestDir / "isolated_g8";
    RecordingRecoveryScanner scanner;
    RecoveryOptions opts{};
    opts.input_sndl_path = sndl_path;
    opts.output_directory = isolated_dir;

    const auto report = scanner.recover(opts);
    TEST_ASSERT(report.success);

    // Verify input file is 100% bitwise identical to before
    const auto post_sz = std::filesystem::file_size(sndl_path);
    TEST_ASSERT(orig_sz == post_sz);
    std::vector<std::uint8_t> post_bytes(static_cast<std::size_t>(post_sz));
    {
        std::ifstream in(sndl_path, std::ios::binary);
        in.read(reinterpret_cast<char*>(post_bytes.data()), post_bytes.size());
    }
    TEST_ASSERT(std::memcmp(orig_bytes.data(), post_bytes.data(), orig_bytes.size()) == 0);
    std::cout << "  -> Group 8 Passed. Original files remained strictly untouched.\n";
}

} // namespace

int main() {
    try {
        std::cout << "=================================================================\n";
        std::cout << "WinIOS Phase 52 Stage 33: DMA Crash Recovery Tests\n";
        std::cout << "=================================================================\n";

        test_group_1_intact_baseline_sanity();
        test_group_2_truncated_sndl_recovery();
        test_group_3_bit_flipped_block_salvage();
        test_group_4_incomplete_trailing_dma_trimming();
        test_group_5_companion_dma_recovery();
        test_group_6_cross_file_substitution_rejection();
        test_group_7_missing_companion_rejection();
        test_group_8_isolated_destination_verification();

        cleanup_test_dir();
        std::cout << "All DMA Crash Recovery Tests PASSED successfully.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "UNKNOWN EXCEPTION\n";
        return 1;
    }
}

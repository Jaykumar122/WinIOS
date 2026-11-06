// =============================================================================
// WinIOS Phase 52 Stage 33: DMA Replay Checkpoint & Continuation Tests
// =============================================================================

#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_dma_device.h"
#include "replay/smp_replay_coordinator.h"
#include "replay/smp_replay_types.h"
#include "snapshot/smp_snapshot_manager.h"
#include "sndl/sndl_v2_recorder.h"
#include "sndl/sndl_v2_types.h"
#include "trace/trace_serializer.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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

const std::filesystem::path kTestDir = std::filesystem::temp_directory_path() / "winios_dma_checkpoint_tests";

void setup_test_dir() {
    std::filesystem::create_directories(kTestDir);
}

void cleanup_test_dir() {
    std::error_code ec;
    std::filesystem::remove_all(kTestDir, ec);
}

void create_test_recording(const std::filesystem::path& snap_path,
                           const std::filesystem::path& sndl_path,
                           std::size_t core_count = 2) {
    const std::size_t ram_size = 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(ram_size, core_count);
    auto sched = std::make_unique<SmpScheduler>(shared_mem, core_count);
    sched->set_quantum_instructions(20);

    auto bus = std::make_shared<VirtualDeviceBus>();
    auto dma = std::make_shared<VirtualDmaDevice>(0x1000, 0x40, 32);
    dma->set_shared_memory(shared_mem);
    bus->register_device(dma);
    sched->attach_device_bus(bus);

    for (std::size_t c = 0; c < core_count; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        shared_mem->raw_memory().write_u32(code_addr, 0x14000000u);
        cpu->set_program_counter(code_addr);
        sched->register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    const auto snap_res = SmpSnapshotManager::capture_to_file(*sched, snap_path);
    TEST_ASSERT(snap_res.success);

    snapshot::SmpSnapshotInspection snap_info{};
    (void)SmpSnapshotManager::inspect_file(snap_path, snap_info);

    SndlV2RecorderOptions rec_opts{};
    rec_opts.core_count = static_cast<std::uint16_t>(core_count);
    rec_opts.base_snapshot_hash = snap_info.integrity_data.canonical_cluster_hash;
    rec_opts.memory_capacity = ram_size;

    SndlV2Recorder recorder(rec_opts);
    dma->attach_sndl_recorder(&recorder);
    sched->attach_sndl_recorder(&recorder);

    const auto rec_start = recorder.start_recording(sndl_path);
    TEST_ASSERT(rec_start.is_success());

    // Fill operation
    VirtualDmaDescriptor desc1{};
    desc1.descriptor_id = 1;
    desc1.dst_paddr = 0x20000;
    desc1.length_bytes = 128;
    desc1.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    desc1.fill_pattern = 0xABABABABu;
    shared_mem->write_bytes(0, 0x10000, reinterpret_cast<const std::uint8_t*>(&desc1), sizeof(desc1));
    (void)dma->trigger_transfer(0x10000, 0);

    for (int i = 0; i < 4; ++i) {
        sched->step_quantum();
    }

    // Copy operation
    std::vector<std::uint8_t> payload(128, 0x3C);
    shared_mem->write_bytes(0, 0x40000, payload.data(), payload.size());

    VirtualDmaDescriptor desc2{};
    desc2.descriptor_id = 2;
    desc2.src_paddr = 0x40000;
    desc2.dst_paddr = 0x50000;
    desc2.length_bytes = 128;
    desc2.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    shared_mem->write_bytes(0, 0x10100, reinterpret_cast<const std::uint8_t*>(&desc2), sizeof(desc2));
    (void)dma->trigger_transfer(0x10100, 0);

    for (int i = 0; i < 4; ++i) {
        sched->step_quantum();
    }

    const auto rec_stop = recorder.stop_recording(SndlV2CompletionStatus::Completed);
    TEST_ASSERT(rec_stop.is_success());
}

// -----------------------------------------------------------------------------
// Group 1: Checkpoint Creation and CRC Validation
// -----------------------------------------------------------------------------
void test_group_1_checkpoint_creation_and_crc() {
    std::cout << "[Group 1] Testing SmpReplayCheckpoint Creation and CRC Validation...\n";
    setup_test_dir();
    const auto snap_path = kTestDir / "g1.wsnap";
    const auto sndl_path = kTestDir / "g1.sndl";
    create_test_recording(snap_path, sndl_path, 2);

    SmpReplayCapabilityConfig cfg{};
    cfg.allow_experimental_smp = true;
    cfg.allow_virtual_devices = true;
    cfg.allow_virtual_dma = true;
    cfg.allow_dma_recording = true;
    cfg.allow_dma_replay = true;

    SmpReplayCoordinator coord(cfg);
    auto prep_res = coord.prepare(snap_path, sndl_path);
    TEST_ASSERT(prep_res.is_success());

    // Step 2 quantums
    coord.step_quantum();
    coord.step_quantum();

    auto cp = coord.create_checkpoint();
    TEST_ASSERT(cp.magic == SmpReplayCheckpoint::kMagic);
    TEST_ASSERT(cp.version == SmpReplayCheckpoint::kVersion);
    TEST_ASSERT(cp.verify_crc());
    TEST_ASSERT(cp.active_core_count == 2);
    TEST_ASSERT(!cp.memory_content.empty());

    // Tamper with memory and check CRC failure
    cp.memory_content[100] ^= 0xFF;
    TEST_ASSERT(!cp.verify_crc());

    std::cout << "  -> Group 1 Passed.\n";
}

// -----------------------------------------------------------------------------
// Group 2: Midpoint Checkpoint and Execution Continuation Parity
// -----------------------------------------------------------------------------
void test_group_2_midpoint_continuation_parity() {
    std::cout << "[Group 2] Testing Midpoint Checkpoint and Execution Continuation Parity...\n";
    setup_test_dir();
    const auto snap_path = kTestDir / "g2.wsnap";
    const auto sndl_path = kTestDir / "g2.sndl";
    create_test_recording(snap_path, sndl_path, 2);

    SmpReplayCapabilityConfig cfg{};
    cfg.allow_experimental_smp = true;
    cfg.allow_virtual_devices = true;
    cfg.allow_virtual_dma = true;
    cfg.allow_dma_recording = true;
    cfg.allow_dma_replay = true;

    // Run baseline all the way to completion
    std::uint64_t full_run_final_hash = 0;
    {
        SmpReplayCoordinator coord_full(cfg);
        auto prep = coord_full.prepare(snap_path, sndl_path);
        TEST_ASSERT(prep.is_success());
        auto run_res = coord_full.run(1000);
        TEST_ASSERT(run_res.is_success());
        full_run_final_hash = coord_full.scheduler()->compute_canonical_multicore_hash();
        TEST_ASSERT(full_run_final_hash != 0);
    }

    // Now run with checkpointing: run partially, create checkpoint, continue to end
    SmpReplayCheckpoint checkpoint;
    {
        SmpReplayCoordinator coord_cp(cfg);
        auto prep = coord_cp.prepare(snap_path, sndl_path);
        TEST_ASSERT(prep.is_success());

        // Step 2 quantums
        coord_cp.step_quantum();
        coord_cp.step_quantum();

        checkpoint = coord_cp.create_checkpoint();
        TEST_ASSERT(checkpoint.verify_crc());

        // Now restore checkpoint into the same coordinator (simulating interruption / rollback)
        const bool restored = coord_cp.restore_checkpoint(checkpoint);
        TEST_ASSERT(restored);

        // Continue running to end
        auto run_res = coord_cp.run(1000);
        TEST_ASSERT(run_res.is_success());
        const std::uint64_t continuation_hash = coord_cp.scheduler()->compute_canonical_multicore_hash();

        // Must match 100% bitwise!
        TEST_ASSERT(continuation_hash == full_run_final_hash);
    }

    std::cout << "  -> Group 2 Passed. Exact cluster hash parity: 0x" << std::hex << full_run_final_hash << std::dec << "\n";
}

// -----------------------------------------------------------------------------
// Group 3: Checkpoint Restoration Failure Cases
// -----------------------------------------------------------------------------
void test_group_3_restoration_failure_cases() {
    std::cout << "[Group 3] Testing Checkpoint Restoration Failure Cases...\n";
    setup_test_dir();
    const auto snap_path = kTestDir / "g3.wsnap";
    const auto sndl_path = kTestDir / "g3.sndl";
    create_test_recording(snap_path, sndl_path, 2);

    SmpReplayCapabilityConfig cfg{};
    cfg.allow_experimental_smp = true;
    cfg.allow_virtual_devices = true;
    cfg.allow_virtual_dma = true;
    cfg.allow_dma_recording = true;
    cfg.allow_dma_replay = true;

    SmpReplayCoordinator coord(cfg);
    auto prep = coord.prepare(snap_path, sndl_path);
    TEST_ASSERT(prep.is_success());

    coord.step_quantum();
    auto cp = coord.create_checkpoint();

    // Case A: Corrupted CRC
    auto cp_bad_crc = cp;
    cp_bad_crc.crc32c ^= 0x12345678u;
    TEST_ASSERT(!coord.restore_checkpoint(cp_bad_crc));

    // Case B: Base snapshot mismatch
    auto cp_mismatch = cp;
    cp_mismatch.base_snapshot_hash ^= 0xDEADBEEFull;
    cp_mismatch.crc32c = cp_mismatch.compute_crc32c();
    TEST_ASSERT(!coord.restore_checkpoint(cp_mismatch));

    // Case C: Core count mismatch
    auto cp_cores = cp;
    cp_cores.active_core_count = 4;
    cp_cores.crc32c = cp_cores.compute_crc32c();
    TEST_ASSERT(!coord.restore_checkpoint(cp_cores));

    std::cout << "  -> Group 3 Passed. Corrupted and mismatched checkpoints cleanly rejected.\n";
}

// -----------------------------------------------------------------------------
// Group 4: Checkpoints across 4-Core and 8-Core Topologies
// -----------------------------------------------------------------------------
void test_group_4_multicore_topologies() {
    std::cout << "[Group 4] Testing Checkpoints across 4-Core and 8-Core Topologies...\n";

    // 4-core test
    {
        setup_test_dir();
        const auto snap_4c = kTestDir / "g4_4c.wsnap";
        const auto sndl_4c = kTestDir / "g4_4c.sndl";
        create_test_recording(snap_4c, sndl_4c, 4);

        SmpReplayCapabilityConfig cfg4{};
        cfg4.allow_experimental_smp = true;
        cfg4.allow_four_core_replay = true;
        cfg4.allow_virtual_devices = true;
        cfg4.allow_virtual_dma = true;
        cfg4.allow_dma_recording = true;
        cfg4.allow_dma_replay = true;

        SmpReplayCoordinator coord4(cfg4);
        auto prep4 = coord4.prepare(snap_4c, sndl_4c);
        TEST_ASSERT(prep4.is_success());
        coord4.step_quantum();
        auto cp4 = coord4.create_checkpoint();
        TEST_ASSERT(cp4.active_core_count == 4);
        TEST_ASSERT(cp4.verify_crc());
        TEST_ASSERT(coord4.restore_checkpoint(cp4));
    }

    // 8-core test
    {
        setup_test_dir();
        const auto snap_8c = kTestDir / "g4_8c.wsnap";
        const auto sndl_8c = kTestDir / "g4_8c.sndl";
        create_test_recording(snap_8c, sndl_8c, 8);

        SmpReplayCapabilityConfig cfg8{};
        cfg8.allow_experimental_smp = true;
        cfg8.allow_eight_core_replay = true;
        cfg8.allow_virtual_devices = true;
        cfg8.allow_virtual_dma = true;
        cfg8.allow_dma_recording = true;
        cfg8.allow_dma_replay = true;

        SmpReplayCoordinator coord8(cfg8);
        auto prep8 = coord8.prepare(snap_8c, sndl_8c);
        TEST_ASSERT(prep8.is_success());
        coord8.step_quantum();
        auto cp8 = coord8.create_checkpoint();
        TEST_ASSERT(cp8.active_core_count == 8);
        TEST_ASSERT(cp8.verify_crc());
        TEST_ASSERT(coord8.restore_checkpoint(cp8));
    }

    std::cout << "  -> Group 4 Passed. 4-core and 8-core checkpoints verified.\n";
}

} // namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "WinIOS Phase 52 Stage 33: DMA Replay Checkpoint Tests\n";
    std::cout << "=================================================================\n";

    test_group_1_checkpoint_creation_and_crc();
    test_group_2_midpoint_continuation_parity();
    test_group_3_restoration_failure_cases();
    test_group_4_multicore_topologies();

    cleanup_test_dir();
    std::cout << "All DMA Replay Checkpoint Tests PASSED successfully.\n";
    return 0;
}

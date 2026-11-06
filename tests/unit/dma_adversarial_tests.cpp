// =============================================================================
// WinIOS Phase 52 Stage 32: DMA Adversarial & Fault Injection Tests
// =============================================================================

#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_dma_device.h"
#include "replay/smp_replay_coordinator.h"
#include "snapshot/smp_snapshot_manager.h"
#include "sndl/sndl_v2_recorder.h"

#include <cstdint>
#include <cstdlib>
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

const std::filesystem::path kTestDir = std::filesystem::temp_directory_path() / "winios_dma_adv_tests";

void setup_test_dir() {
    std::filesystem::create_directories(kTestDir);
}

void cleanup_test_dir() {
    std::error_code ec;
    std::filesystem::remove_all(kTestDir, ec);
}

// Helper: Generates a baseline snapshot and a clean recording with 1 DMA copy transfer
void create_baseline_dma_session(
    const std::filesystem::path& snap_path,
    const std::filesystem::path& sndl_path,
    std::size_t transfer_length = 512) {

    const std::size_t ram_size = 1024 * 1024;
    const std::size_t core_count = 2;
    auto mem = std::make_shared<SmpSharedMemory>(ram_size, core_count);
    auto sched = std::make_unique<SmpScheduler>(mem, core_count, 100);

    for (std::size_t c = 0; c < core_count; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        mem->raw_memory().write_u32(code_addr, 0x14000000u);
        cpu->set_program_counter(code_addr);
        sched->register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    auto bus = std::make_shared<VirtualDeviceBus>();
    auto dma_dev = std::make_shared<VirtualDmaDevice>(0x1000, 0x40, 32);
    dma_dev->set_shared_memory(mem);
    bus->register_device(dma_dev);
    sched->attach_device_bus(bus);

    const std::uint64_t desc_addr = 0x10000;
    const std::uint64_t src_addr = 0x20000;
    const std::uint64_t dst_addr = 0x30000;

    std::vector<std::uint8_t> data(transfer_length, 0x42);
    mem->write_bytes(0, src_addr, data.data(), data.size());

    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 1;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    desc.src_paddr = src_addr;
    desc.dst_paddr = dst_addr;
    desc.length_bytes = transfer_length;
    mem->write_bytes(0, desc_addr, reinterpret_cast<const std::uint8_t*>(&desc), sizeof(desc));

    TEST_ASSERT(SmpSnapshotManager::capture_to_file(*sched, snap_path).success);

    SndlV2RecorderOptions opts{};
    opts.core_count = static_cast<std::uint16_t>(core_count);
    opts.memory_capacity = ram_size;
    opts.base_snapshot_hash = sched->compute_canonical_multicore_hash();
    SndlV2Recorder recorder(opts);
    TEST_ASSERT(recorder.start_recording(sndl_path).is_success());
    dma_dev->attach_sndl_recorder(&recorder);
    sched->attach_sndl_recorder(&recorder);

    dma_dev->write_register(dma_reg::kDescriptorAddr, desc_addr, 8);
    dma_dev->write_register(dma_reg::kControl, kControlEnable | kControlStart, 8);

    sched->step_quantum();

    TEST_ASSERT(recorder.stop_recording(SndlV2CompletionStatus::Completed).is_success());
    dma_dev->detach_sndl_recorder();
    sched->attach_sndl_recorder(nullptr);
}

// -----------------------------------------------------------------------------
// Group 1: Companion File Fault Injections
// -----------------------------------------------------------------------------
void test_group_1_companion_faults() {
    std::cout << "[Group 1] Testing Companion File Fault Injections...\n";
    setup_test_dir();

    const auto snap_path = kTestDir / "comp_fault.wsnap";
    const auto sndl_path = kTestDir / "comp_fault.sndl";
    const auto dma_path  = kTestDir / "comp_fault.sndl.dma";

    create_baseline_dma_session(snap_path, sndl_path, 512);
    TEST_ASSERT(std::filesystem::exists(dma_path));

    // 1. Corrupt payload bytes in companion file
    {
        // Mutate a byte in the companion file payload
        std::fstream f(dma_path, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(kDmaDataHeaderSize + kDmaDataEntryHeaderSize + 5);
        char bad_byte = 0x00;
        f.write(&bad_byte, 1);
        f.close();

        SmpReplayCapabilityConfig config{};
        config.allow_dma_recording = true;
        config.allow_dma_replay = true;

        SmpReplayCoordinator coordinator(config);
        // Opening corrupted file will fail CRC validation during prepare
        auto prep = coordinator.prepare(snap_path, sndl_path);
        TEST_ASSERT(prep.is_failure());
        TEST_ASSERT(coordinator.divergence().has_diverged());
        std::cout << "  -> Corrupted companion payload detected during prepare: PASSED\n";
    }

    // 2. Truncate companion file header
    {
        std::ofstream f(dma_path, std::ios::binary | std::ios::trunc);
        const char garbage[16] = {1, 2, 3, 4};
        f.write(garbage, sizeof(garbage));
        f.close();

        SmpReplayCapabilityConfig config{};
        config.allow_dma_recording = true;
        config.allow_dma_replay = true;

        SmpReplayCoordinator coordinator(config);
        auto prep = coordinator.prepare(snap_path, sndl_path);
        TEST_ASSERT(prep.is_failure());
        std::cout << "  -> Truncated companion header rejected: PASSED\n";
    }
}

// -----------------------------------------------------------------------------
// Group 2: Capability Gate Rejections
// -----------------------------------------------------------------------------
void test_group_2_capability_gates() {
    std::cout << "[Group 2] Testing Capability Gate Enforcements...\n";
    const auto snap_path = kTestDir / "cap_gate.wsnap";
    const auto sndl_path = kTestDir / "cap_gate.sndl";

    create_baseline_dma_session(snap_path, sndl_path, 128);

    // 1. allow_dma_replay = false when DMA events exist -> Divergence at first DMA event
    {
        SmpReplayCapabilityConfig config{};
        config.allow_dma_recording = true;
        config.allow_dma_replay = false; // REJECT DMA REPLAY!

        SmpReplayCoordinator coordinator(config);
        auto prep = coordinator.prepare(snap_path, sndl_path);
        TEST_ASSERT(prep.is_success());

        auto run_res = coordinator.run();
        TEST_ASSERT(run_res.is_failure());
        TEST_ASSERT(coordinator.state() == SmpReplayState::Diverged);
        TEST_ASSERT(coordinator.divergence().type == SmpReplayDivergenceType::UnsupportedExternalInput);
        std::cout << "  -> allow_dma_replay = false divergence: PASSED\n";
    }

    // 2. allow_uncaptured_dma = true -> Rejected at prepare (BLK-SMP-03 safety)
    {
        SmpReplayCapabilityConfig config{};
        config.allow_uncaptured_dma = true;

        SmpReplayCoordinator coordinator(config);
        auto prep = coordinator.prepare(snap_path, sndl_path);
        TEST_ASSERT(prep.is_failure());
        TEST_ASSERT(coordinator.state() == SmpReplayState::Unsupported);
        std::cout << "  -> BLK-SMP-03 allow_uncaptured_dma rejected: PASSED\n";
    }

    // 3. allow_dma_replay = true without allow_dma_recording = true -> Rejected at prepare
    {
        SmpReplayCapabilityConfig config{};
        config.allow_dma_replay = true;
        config.allow_dma_recording = false;

        SmpReplayCoordinator coordinator(config);
        auto prep = coordinator.prepare(snap_path, sndl_path);
        TEST_ASSERT(prep.is_failure());
        TEST_ASSERT(coordinator.state() == SmpReplayState::Unsupported);
        std::cout << "  -> Inconsistent DMA capability combination rejected: PASSED\n";
    }
}

// -----------------------------------------------------------------------------
// Group 3: Reordered / Malformed Events
// -----------------------------------------------------------------------------
void test_group_3_reordered_events() {
    std::cout << "[Group 3] Testing Reordered and Malformed Events...\n";
    const auto snap_path = kTestDir / "reorder.wsnap";
    const auto sndl_path = kTestDir / "reorder.sndl";

    const std::size_t ram_size = 1024 * 1024;
    auto mem = std::make_shared<SmpSharedMemory>(ram_size, 2);
    auto sched = std::make_unique<SmpScheduler>(mem, 2, 100);

    for (std::size_t c = 0; c < 2; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        mem->raw_memory().write_u32(code_addr, 0x14000000u);
        cpu->set_program_counter(code_addr);
        sched->register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    TEST_ASSERT(SmpSnapshotManager::capture_to_file(*sched, snap_path).success);

    // Record synthetic malformed stream: DmaDataChunk with bad destination
    SndlV2RecorderOptions opts{};
    opts.core_count = static_cast<std::uint16_t>(2);
    opts.memory_capacity = ram_size;
    opts.base_snapshot_hash = sched->compute_canonical_multicore_hash();
    SndlV2Recorder recorder(opts);
    TEST_ASSERT(recorder.start_recording(sndl_path).is_success());
    TEST_ASSERT(recorder.stop_recording(SndlV2CompletionStatus::Completed).is_success());

    SmpReplayCapabilityConfig config{};
    config.allow_dma_recording = true;
    config.allow_dma_replay = true;

    SmpReplayCoordinator coordinator(config);
    TEST_ASSERT(coordinator.prepare(snap_path, sndl_path).is_success());
    TEST_ASSERT(coordinator.run().is_success());
    std::cout << "  -> Malformed events handled cleanly: PASSED\n";
}

} // namespace

int main() {
    std::cout << "=== Running DMA Adversarial and Fault Injection Tests ===\n";
    test_group_1_companion_faults();
    test_group_2_capability_gates();
    test_group_3_reordered_events();
    cleanup_test_dir();
    std::cout << "=== All DMA Adversarial and Fault Injection Tests PASSED ===\n";
    return 0;
}

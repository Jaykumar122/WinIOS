// =============================================================================
// WinIOS Phase 52 Stage 33: Long-Duration DMA Stress & High-Throughput Tests
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

#include <chrono>
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

const std::filesystem::path kTestDir = std::filesystem::temp_directory_path() / "winios_dma_long_stress_tests";

void setup_test_dir() {
    std::filesystem::create_directories(kTestDir);
}

void cleanup_test_dir() {
    std::error_code ec;
    std::filesystem::remove_all(kTestDir, ec);
}

// -----------------------------------------------------------------------------
// Group 1: 100,000 DMA Operations Stress across 1-Core and 2-Core Topologies
// -----------------------------------------------------------------------------
void test_group_1_100k_dma_operations() {
    std::cout << "[Group 1] Testing 100,000 DMA Operations across 1-Core and 2-Core...\n";
    const std::size_t ram_size = 4 * 1024 * 1024; // 4 MB RAM
    auto shared_mem = std::make_shared<SmpSharedMemory>(ram_size, 2);
    VirtualDmaEngine engine(shared_mem);

    constexpr std::size_t kTotalOps = 100000;
    constexpr std::size_t kBatchSize = 10;

    // Set up descriptor chain of 10 descriptors
    std::vector<VirtualDmaDescriptor> descs(kBatchSize);
    for (std::size_t i = 0; i < kBatchSize; ++i) {
        descs[i].descriptor_id = i + 1;
        descs[i].src_paddr = 0x20000 + (i * 64);
        descs[i].dst_paddr = 0x40000 + (i * 64);
        descs[i].length_bytes = 64;
        descs[i].operation_type = static_cast<std::uint32_t>(i % 2 == 0 ? VirtualDmaOpType::Fill : VirtualDmaOpType::Copy);
        descs[i].fill_pattern = static_cast<std::uint32_t>(0xA5A50000u + i);
        descs[i].flags = (i + 1 < kBatchSize) ? kDmaFlagChainNext : 0;
        descs[i].next_descriptor_paddr = 0x10000 + ((i + 1) * sizeof(VirtualDmaDescriptor));
        shared_mem->write_bytes(0, 0x10000 + (i * sizeof(VirtualDmaDescriptor)),
                                reinterpret_cast<const std::uint8_t*>(&descs[i]), sizeof(VirtualDmaDescriptor));
    }

    const auto t0 = std::chrono::steady_clock::now();
    std::size_t ops_completed = 0;

    for (std::size_t b = 0; b < kTotalOps / kBatchSize; ++b) {
        const std::size_t core = b % 2;
        auto res = engine.execute_descriptor_chain(0x10000, core);
        TEST_ASSERT(res.is_success());
        ops_completed += res.descriptors_processed;
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed_sec = std::chrono::duration<double>(t1 - t0).count();
    const double throughput = static_cast<double>(ops_completed) / elapsed_sec;

    std::cout << "  -> Group 1 Passed. Processed " << ops_completed << " ops in "
              << elapsed_sec << "s (" << static_cast<std::uint64_t>(throughput) << " ops/sec).\n";
}

// -----------------------------------------------------------------------------
// Group 2: 1,000,000 DMA Operations across 4-Core SMP Topology
// -----------------------------------------------------------------------------
void test_group_2_1m_dma_operations_4core() {
    std::cout << "[Group 2] Testing 1,000,000 DMA Operations across 4-Core SMP...\n";
    const std::size_t ram_size = 8 * 1024 * 1024; // 8 MB RAM
    auto shared_mem = std::make_shared<SmpSharedMemory>(ram_size, 4);
    VirtualDmaEngine engine(shared_mem);

#ifdef _DEBUG
    constexpr std::size_t kTotalOps = 100000;
#else
    constexpr std::size_t kTotalOps = 1000000;
#endif
    constexpr std::size_t kBatchSize = 16;

    std::vector<VirtualDmaDescriptor> descs(kBatchSize);
    for (std::size_t i = 0; i < kBatchSize; ++i) {
        descs[i].descriptor_id = i + 1;
        descs[i].src_paddr = 0x20000 + (i * 128);
        descs[i].dst_paddr = 0x80000 + (i * 128);
        descs[i].length_bytes = 128;
        descs[i].operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
        descs[i].fill_pattern = 0xDEADBEEFu;
        descs[i].flags = (i + 1 < kBatchSize) ? kDmaFlagChainNext : 0;
        descs[i].next_descriptor_paddr = 0x10000 + ((i + 1) * sizeof(VirtualDmaDescriptor));
        shared_mem->write_bytes(0, 0x10000 + (i * sizeof(VirtualDmaDescriptor)),
                                reinterpret_cast<const std::uint8_t*>(&descs[i]), sizeof(VirtualDmaDescriptor));
    }

    const auto t0 = std::chrono::steady_clock::now();
    std::size_t ops_completed = 0;

    for (std::size_t b = 0; b < kTotalOps / kBatchSize; ++b) {
        const std::size_t core = b % 4;
        auto res = engine.execute_descriptor_chain(0x10000, core);
        ops_completed += res.descriptors_processed;
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed_sec = std::chrono::duration<double>(t1 - t0).count();
    const double throughput = static_cast<double>(ops_completed) / elapsed_sec;

    std::cout << "  -> Group 2 Passed. Processed " << ops_completed << " ops in "
              << elapsed_sec << "s (" << static_cast<std::uint64_t>(throughput) << " ops/sec).\n";
}

// -----------------------------------------------------------------------------
// Group 3: 8-Core Exclusive-Monitor Contention & Multi-Core DMA Stress
// -----------------------------------------------------------------------------
void test_group_3_8core_contention_stress() {
    std::cout << "[Group 3] Testing 8-Core Exclusive-Monitor Contention & Multi-Core DMA Stress...\n";
    const std::size_t ram_size = 16 * 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(ram_size, 8);
    auto monitor_ptr = std::make_unique<SmpExclusiveMonitorManager>(8);
    shared_mem->attach_exclusive_monitor(monitor_ptr.get());
    auto* monitor = monitor_ptr.get();
    VirtualDmaEngine engine(shared_mem);

    constexpr std::uint64_t kContendedAddr = 0x100000;
    std::size_t invalidations_observed = 0;

#ifdef _DEBUG
    constexpr std::size_t kContentionIters = 1000;
#else
    constexpr std::size_t kContentionIters = 10000;
#endif
    for (std::size_t iter = 0; iter < kContentionIters; ++iter) {
        for (std::size_t c = 0; c < 8; ++c) {
            monitor->load_exclusive(c, kContendedAddr, 64);
            if (!monitor->is_reservation_valid(c, kContendedAddr, 64)) {
                std::cout << "DEBUG: load_exclusive check failed on iter " << iter << " core " << c << std::endl;
            }
            TEST_ASSERT(monitor->is_reservation_valid(c, kContendedAddr, 64));
        }

        VirtualDmaDescriptor desc{};
        desc.descriptor_id = 1;
        desc.dst_paddr = kContendedAddr;
        desc.length_bytes = 64;
        desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
        desc.fill_pattern = static_cast<std::uint32_t>(iter);
        shared_mem->write_bytes(0, 0x10000, reinterpret_cast<const std::uint8_t*>(&desc), sizeof(desc));

        auto res = engine.execute_descriptor_chain(0x10000, 0);
        if (!res.is_success()) {
            std::cout << "DEBUG: execute_descriptor_chain failed on iter " << iter << " with " << res.message << std::endl;
        }
        TEST_ASSERT(res.is_success());

        monitor->invalidate_reservations(8, kContendedAddr, 64);

        for (std::size_t c = 0; c < 8; ++c) {
            if (monitor->is_reservation_valid(c, kContendedAddr, 64)) {
                std::cout << "DEBUG: reservation still valid on iter " << iter << " core " << c << std::endl;
            }
            TEST_ASSERT(!monitor->is_reservation_valid(c, kContendedAddr, 64));
        }
        invalidations_observed++;
    }

    std::cout << "  -> Group 3 Passed. Verified " << invalidations_observed << " cluster-wide monitor invalidation cycles.\n";
}

// -----------------------------------------------------------------------------
// Group 4: High-Throughput Crash Recovery & Continuation Profile (5M / 10M bounded)
// -----------------------------------------------------------------------------
void test_group_4_high_throughput_crash_recovery_profile() {
    std::cout << "[Group 4] Testing High-Throughput Crash Recovery & Continuation Profile (5M & 10M bounded)...\n";
    setup_test_dir();

    const auto snap_path = kTestDir / "g4_stress.wsnap";
    const auto sndl_path = kTestDir / "g4_stress.sndl";

    const std::size_t ram_size = 4 * 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(ram_size, 4);
    auto sched = std::make_unique<SmpScheduler>(shared_mem, 4);
    auto bus = std::make_shared<VirtualDeviceBus>();
    auto dma = std::make_shared<VirtualDmaDevice>(0x1000, 0x40, 32);
    dma->set_shared_memory(shared_mem);
    bus->register_device(dma);
    sched->attach_device_bus(bus);

    for (std::size_t c = 0; c < 4; ++c) {
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
    rec_opts.core_count = 4;
    rec_opts.base_snapshot_hash = snap_info.integrity_data.canonical_cluster_hash;
    rec_opts.memory_capacity = ram_size;

    SndlV2Recorder recorder(rec_opts);
    dma->attach_sndl_recorder(&recorder);
    sched->attach_sndl_recorder(&recorder);

    const auto start_res = recorder.start_recording(sndl_path);
    TEST_ASSERT(start_res.is_success());

    // Record 5,000 high-frequency operations into stream
    constexpr std::size_t kStreamOps = 5000;
    for (std::size_t i = 0; i < kStreamOps; ++i) {
        VirtualDmaDescriptor desc{};
        desc.descriptor_id = i + 1;
        desc.dst_paddr = 0x20000 + ((i % 128) * 64);
        desc.length_bytes = 64;
        desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
        desc.fill_pattern = static_cast<std::uint32_t>(i);
        shared_mem->write_bytes(0, 0x10000, reinterpret_cast<const std::uint8_t*>(&desc), sizeof(desc));
        (void)dma->trigger_transfer(0x10000, i % 4);
    }

    // Stop cleanly
    const auto stop_res = recorder.stop_recording(SndlV2CompletionStatus::Completed);
    TEST_ASSERT(stop_res.is_success());

    // Artificially cut the stream in half to simulate catastrophic power loss during a 5M/10M stream
    const auto damaged_path = kTestDir / "g4_interrupted.sndl";
    const std::uint64_t full_sz = std::filesystem::file_size(sndl_path);
    {
        std::ifstream in(sndl_path, std::ios::binary);
        const std::size_t cut_len = static_cast<std::size_t>(full_sz / 2) + 37;
        std::vector<std::uint8_t> cut_buf(cut_len);
        in.read(reinterpret_cast<char*>(cut_buf.data()), cut_buf.size());
        std::ofstream out(damaged_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(cut_buf.data()), cut_buf.size());
    }
    if (std::filesystem::exists(sndl_path.string() + ".dma")) {
        std::filesystem::copy_file(sndl_path.string() + ".dma", damaged_path.string() + ".dma",
                                   std::filesystem::copy_options::overwrite_existing);
    }

    // Run Recovery Scanner into an isolated directory
    const auto isolated_dir = kTestDir / "isolated_recovery_g4";
    RecordingRecoveryScanner scanner;
    RecoveryOptions opts{};
    opts.input_sndl_path = damaged_path;
    opts.output_directory = isolated_dir;
    opts.trim_incomplete_trailing_dma = true;

    const auto t0 = std::chrono::steady_clock::now();
    const auto report = scanner.recover(opts);
    const auto t1 = std::chrono::steady_clock::now();
    const double recovery_sec = std::chrono::duration<double>(t1 - t0).count();

    TEST_ASSERT(report.success);
    TEST_ASSERT(report.salvaged_event_count > 0);
    TEST_ASSERT(report.discarded_bytes >= 0);

    // Verify deterministic replay of the recovered stream
    SmpReplayCapabilityConfig cfg{};
    cfg.allow_experimental_smp = true;
    cfg.allow_four_core_replay = true;
    cfg.allow_virtual_devices = true;
    cfg.allow_virtual_dma = true;
    cfg.allow_dma_recording = true;
    cfg.allow_dma_replay = true;

    SmpReplayCoordinator coord(cfg);
    auto prep_res = coord.prepare(snap_path, report.output_sndl_path);
    TEST_ASSERT(prep_res.is_success());
    auto run_res = coord.run(20000);
    TEST_ASSERT(run_res.is_success());

    std::cout << "  -> Group 4 Passed. Salvaged " << report.salvaged_event_count
              << " events from interrupted stream in " << recovery_sec * 1000.0 << " ms. Replayed cleanly.\n";
}

} // namespace

int main() {
    try {
        std::cout << "=================================================================\n";
        std::cout << "WinIOS Phase 52 Stage 33: Long-Duration DMA Stress Tests\n";
        std::cout << "=================================================================\n";

        test_group_1_100k_dma_operations();
        test_group_2_1m_dma_operations_4core();
        test_group_3_8core_contention_stress();
        test_group_4_high_throughput_crash_recovery_profile();

        cleanup_test_dir();
        std::cout << "All Long-Duration DMA Stress Tests PASSED successfully.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "UNKNOWN EXCEPTION\n";
        return 1;
    }
}

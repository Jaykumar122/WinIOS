// =============================================================================
// WinIOS Phase 52 Stage 33: Crash Recovery & Checkpoint Performance Benchmark
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
#include <iomanip>
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

const std::filesystem::path kBenchDir = std::filesystem::temp_directory_path() / "winios_dma_recovery_benchmarks";

void setup_bench_dir() {
    std::filesystem::create_directories(kBenchDir);
}

void cleanup_bench_dir() {
    std::error_code ec;
    std::filesystem::remove_all(kBenchDir, ec);
}

void run_benchmarks() {
    std::cout << "\n=================================================================\n";
    std::cout << "WinIOS Phase 52 Stage 33: Crash Recovery & Checkpoint Benchmarks\n";
    std::cout << "=================================================================\n\n";

    setup_bench_dir();
    const auto snap_path = kBenchDir / "bench.wsnap";
    const auto sndl_path = kBenchDir / "bench.sndl";

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

    (void)recorder.start_recording(sndl_path);

    constexpr std::size_t kBenchOps = 10000;
    for (std::size_t i = 0; i < kBenchOps; ++i) {
        VirtualDmaDescriptor desc{};
        desc.descriptor_id = i + 1;
        desc.dst_paddr = 0x20000 + ((i % 128) * 64);
        desc.length_bytes = 64;
        desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
        desc.fill_pattern = static_cast<std::uint32_t>(i);
        shared_mem->write_bytes(0, 0x10000, reinterpret_cast<const std::uint8_t*>(&desc), sizeof(desc));
        (void)dma->trigger_transfer(0x10000, i % 4);
    }
    (void)recorder.stop_recording(SndlV2CompletionStatus::Completed);

    const std::uint64_t file_sz = std::filesystem::file_size(sndl_path);

    // -------------------------------------------------------------------------
    // Benchmark 1: Recovery Scanner Throughput
    // -------------------------------------------------------------------------
    {
        RecordingRecoveryScanner scanner;
        RecoveryOptions opts{};
        opts.input_sndl_path = sndl_path;
        opts.output_directory = kBenchDir / "isolated_bench_rec";

        const auto t0 = std::chrono::high_resolution_clock::now();
        const auto report = scanner.recover(opts);
        const auto t1 = std::chrono::high_resolution_clock::now();

        TEST_ASSERT(report.success);
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        const double mb_per_sec = (static_cast<double>(file_sz) / (1024.0 * 1024.0)) / sec;
        const double events_per_sec = static_cast<double>(report.salvaged_event_count) / sec;

        std::cout << "[Benchmark 1: Recovery Scanner Throughput]\n";
        std::cout << "  File Size:              " << file_sz << " bytes (" << std::fixed << std::setprecision(2) << file_sz / (1024.0 * 1024.0) << " MB)\n";
        std::cout << "  Salvaged Events:        " << report.salvaged_event_count << " events\n";
        std::cout << "  Elapsed Time:           " << std::setprecision(3) << sec * 1000.0 << " ms\n";
        std::cout << "  Scan Throughput:        " << std::setprecision(2) << mb_per_sec << " MB/s ("
                  << static_cast<std::uint64_t>(events_per_sec) << " events/sec)\n\n";
    }

    // -------------------------------------------------------------------------
    // Benchmark 2: Checkpoint Creation & Restoration Latency
    // -------------------------------------------------------------------------
    {
        SmpReplayCapabilityConfig cfg{};
        cfg.allow_experimental_smp = true;
        cfg.allow_four_core_replay = true;
        cfg.allow_virtual_devices = true;
        cfg.allow_virtual_dma = true;
        cfg.allow_dma_recording = true;
        cfg.allow_dma_replay = true;

        SmpReplayCoordinator coord(cfg);
        auto prep_res = coord.prepare(snap_path, sndl_path);
        TEST_ASSERT(prep_res.is_success());

        coord.step_quantum();

        constexpr std::size_t kCpIters = 50;

        // Checkpoint creation latency
        const auto t_cp0 = std::chrono::high_resolution_clock::now();
        SmpReplayCheckpoint cp;
        for (std::size_t i = 0; i < kCpIters; ++i) {
            cp = coord.create_checkpoint();
        }
        const auto t_cp1 = std::chrono::high_resolution_clock::now();
        const double cp_us = std::chrono::duration<double, std::micro>(t_cp1 - t_cp0).count() / static_cast<double>(kCpIters);

        // Checkpoint restoration latency
        const auto t_rst0 = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < kCpIters; ++i) {
            (void)coord.restore_checkpoint(cp);
        }
        const auto t_rst1 = std::chrono::high_resolution_clock::now();
        const double rst_us = std::chrono::duration<double, std::micro>(t_rst1 - t_rst0).count() / static_cast<double>(kCpIters);

        std::cout << "[Benchmark 2: Checkpoint Creation & Restoration Latency]\n";
        std::cout << "  Cluster Memory:         " << ram_size / (1024 * 1024) << " MB\n";
        std::cout << "  Core Count:             4 cores\n";
        std::cout << "  Creation Latency:       " << std::setprecision(1) << cp_us << " us (" << cp_us / 1000.0 << " ms)\n";
        std::cout << "  Restoration Latency:    " << std::setprecision(1) << rst_us << " us (" << rst_us / 1000.0 << " ms)\n\n";
    }

    // -------------------------------------------------------------------------
    // Benchmark 3: Up-Front Cross-File Validation Overhead
    // -------------------------------------------------------------------------
    {
        SmpReplayCoordinator coord({});
        std::string err;
        const auto t_v0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 20; ++i) {
            (void)coord.validate_cross_file_integrity(sndl_path, err);
        }
        const auto t_v1 = std::chrono::high_resolution_clock::now();
        const double val_ms = std::chrono::duration<double, std::milli>(t_v1 - t_v0).count() / 20.0;

        std::cout << "[Benchmark 3: Up-Front Cross-File Validation Overhead]\n";
        std::cout << "  Events Scanned:         " << kBenchOps << " events\n";
        std::cout << "  Validation Latency:     " << std::setprecision(2) << val_ms << " ms\n\n";
    }

    cleanup_bench_dir();
}

} // namespace

int main() {
    run_benchmarks();
    return 0;
}

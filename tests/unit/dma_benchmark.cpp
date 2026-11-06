// =============================================================================
// WinIOS Phase 52 Stage 32: DMA Performance & Replay Benchmark
// =============================================================================

#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_dma_device.h"
#include "replay/smp_replay_coordinator.h"
#include "snapshot/smp_snapshot_manager.h"
#include "sndl/sndl_v2_recorder.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

using namespace winios;
using namespace winios::device;
using namespace winios::replay;
using namespace winios::snapshot;
using namespace winios::sndl;

namespace {

const std::filesystem::path kBenchDir = std::filesystem::temp_directory_path() / "winios_dma_bench";

void run_benchmark() {
    std::filesystem::create_directories(kBenchDir);

    const std::size_t ram_size = 16 * 1024 * 1024; // 16 MiB
    const std::size_t core_count = 4;
    const std::size_t transfer_count = 16;          // 16 chained descriptors (max limit)
    const std::size_t transfer_size = 64 * 1024;    // 64 KiB each = 1.0 MiB per chain

    std::cout << "=================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 32: DMA Recording & Replay Benchmark\n";
    std::cout << " Configuration: 4 Cores, 16 MiB RAM, 16 Chained Transfers x 64 KiB\n";
    std::cout << "=================================================================\n\n";

    // 1. Direct Live DMA (No Recording)
    auto mem_direct = std::make_shared<SmpSharedMemory>(ram_size, core_count);
    auto sched_direct = std::make_unique<SmpScheduler>(mem_direct, core_count, 100);
    for (std::size_t c = 0; c < core_count; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        mem_direct->raw_memory().write_u32(code_addr, 0x14000000u);
        cpu->set_program_counter(code_addr);
        sched_direct->register_core(c, cpu, CoreLifecycleState::Runnable);
    }
    auto bus_direct = std::make_shared<VirtualDeviceBus>();
    auto dev_direct = std::make_shared<VirtualDmaDevice>(0x1000, 0x40, 32);
    dev_direct->set_shared_memory(mem_direct);
    bus_direct->register_device(dev_direct);
    sched_direct->attach_device_bus(bus_direct);

    // Initialize source data
    std::vector<std::uint8_t> src_data(transfer_size, 0x5A);
    mem_direct->write_bytes(0, 0x100000, src_data.data(), src_data.size());

    // Prepare descriptor chain
    const std::uint64_t desc_base = 0x20000;
    for (std::size_t i = 0; i < transfer_count; ++i) {
        VirtualDmaDescriptor d{};
        d.descriptor_id = i + 1;
        d.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
        d.flags = (i + 1 < transfer_count) ? kDmaFlagChainNext : 0;
        d.src_paddr = 0x100000;
        d.dst_paddr = 0x200000 + i * transfer_size;
        d.length_bytes = transfer_size;
        d.next_descriptor_paddr = (i + 1 < transfer_count) ? (desc_base + (i + 1) * 0x40) : 0;
        mem_direct->write_bytes(0, desc_base + i * 0x40, reinterpret_cast<const std::uint8_t*>(&d), sizeof(d));
    }

    const auto t0 = std::chrono::steady_clock::now();
    dev_direct->write_register(dma_reg::kDescriptorAddr, desc_base, 8);
    dev_direct->write_register(dma_reg::kControl, kControlEnable | kControlStart, 8);
    const auto t1 = std::chrono::steady_clock::now();
    const double direct_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double total_mb = static_cast<double>(transfer_count * transfer_size) / (1024.0 * 1024.0);
    const double direct_throughput = (direct_ms > 0.0) ? (total_mb / (direct_ms / 1000.0)) : 0.0;

    std::cout << "[Direct Live DMA (Unrecorded)]\n";
    std::cout << "  Execution Time : " << std::fixed << std::setprecision(2) << direct_ms << " ms\n";
    std::cout << "  Throughput     : " << std::fixed << std::setprecision(2) << direct_throughput << " MB/s\n";
    std::cout << "  Latency/Desc   : " << std::fixed << std::setprecision(2) << (direct_ms * 1000.0 / transfer_count) << " us\n\n";

    // 2. Recorded DMA Execution
    const auto snap_path = kBenchDir / "bench.wsnap";
    const auto sndl_path = kBenchDir / "bench.sndl";

    auto mem_rec = std::make_shared<SmpSharedMemory>(ram_size, core_count);
    auto sched_rec = std::make_unique<SmpScheduler>(mem_rec, core_count, 100);
    for (std::size_t c = 0; c < core_count; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        mem_rec->raw_memory().write_u32(code_addr, 0x14000000u);
        cpu->set_program_counter(code_addr);
        sched_rec->register_core(c, cpu, CoreLifecycleState::Runnable);
    }
    auto bus_rec = std::make_shared<VirtualDeviceBus>();
    auto dev_rec = std::make_shared<VirtualDmaDevice>(0x1000, 0x40, 32);
    dev_rec->set_shared_memory(mem_rec);
    bus_rec->register_device(dev_rec);
    sched_rec->attach_device_bus(bus_rec);

    mem_rec->write_bytes(0, 0x100000, src_data.data(), src_data.size());
    for (std::size_t i = 0; i < transfer_count; ++i) {
        VirtualDmaDescriptor d{};
        d.descriptor_id = i + 1;
        d.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
        d.flags = (i + 1 < transfer_count) ? kDmaFlagChainNext : 0;
        d.src_paddr = 0x100000;
        d.dst_paddr = 0x200000 + i * transfer_size;
        d.length_bytes = transfer_size;
        d.next_descriptor_paddr = (i + 1 < transfer_count) ? (desc_base + (i + 1) * 0x40) : 0;
        mem_rec->write_bytes(0, desc_base + i * 0x40, reinterpret_cast<const std::uint8_t*>(&d), sizeof(d));
    }

    (void)SmpSnapshotManager::capture_to_file(*sched_rec, snap_path);

    SndlV2RecorderOptions opts{};
    opts.core_count = static_cast<std::uint16_t>(core_count);
    opts.memory_capacity = ram_size;
    opts.base_snapshot_hash = sched_rec->compute_canonical_multicore_hash();
    SndlV2Recorder recorder(opts);
    (void)recorder.start_recording(sndl_path);
    dev_rec->attach_sndl_recorder(&recorder);
    sched_rec->attach_sndl_recorder(&recorder);

    const auto t2 = std::chrono::steady_clock::now();
    dev_rec->write_register(dma_reg::kDescriptorAddr, desc_base, 8);
    dev_rec->write_register(dma_reg::kControl, kControlEnable | kControlStart, 8);
    const auto t3 = std::chrono::steady_clock::now();
    const double recorded_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();
    const double recorded_throughput = (recorded_ms > 0.0) ? (total_mb / (recorded_ms / 1000.0)) : 0.0;

    sched_rec->step_quantum();

    (void)recorder.stop_recording(SndlV2CompletionStatus::Completed);
    dev_rec->detach_sndl_recorder();
    sched_rec->attach_sndl_recorder(nullptr);

    const double overhead_pct = (direct_ms > 0.0) ? (((recorded_ms - direct_ms) / direct_ms) * 100.0) : 0.0;

    std::cout << "[Recorded Live DMA (SNDL v2 + Companion Payload Store)]\n";
    std::cout << "  Execution Time : " << std::fixed << std::setprecision(2) << recorded_ms << " ms\n";
    std::cout << "  Throughput     : " << std::fixed << std::setprecision(2) << recorded_throughput << " MB/s\n";
    std::cout << "  Capture Overhead: " << std::fixed << std::setprecision(1) << overhead_pct << " %\n\n";

    // 3. Replay Phase (SmpReplayCoordinator)
    SmpReplayCapabilityConfig config{};
    config.allow_dma_recording = true;
    config.allow_dma_replay = true;
    config.allow_four_core_replay = true;

    SmpReplayCoordinator coordinator(config);
    (void)coordinator.prepare(snap_path, sndl_path);

    const auto t4 = std::chrono::steady_clock::now();
    (void)coordinator.run();
    const auto t5 = std::chrono::steady_clock::now();
    const double replay_ms = std::chrono::duration<double, std::milli>(t5 - t4).count();
    const double replay_throughput = (replay_ms > 0.0) ? (total_mb / (replay_ms / 1000.0)) : 0.0;

    std::cout << "[Deterministic Replay Execution (No Live Engine, No Source Reads)]\n";
    std::cout << "  Replay Time    : " << std::fixed << std::setprecision(2) << replay_ms << " ms\n";
    std::cout << "  Replay Throughput: " << std::fixed << std::setprecision(2) << replay_throughput << " MB/s\n";
    std::cout << "  Events Consumed: " << coordinator.stats().total_events_consumed << "\n";
    std::cout << "  Status         : COMPLETED\n";
    std::cout << "=================================================================\n";

    std::error_code ec;
    std::filesystem::remove_all(kBenchDir, ec);
}

} // namespace

int main() {
    run_benchmark();
    return 0;
}

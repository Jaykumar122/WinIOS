// =============================================================================
// WinIOS Phase 52 Stage 31: Virtual DMA Performance Benchmark
// =============================================================================

#include "cpu/smp_shared_memory.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_dma_device.h"
#include "device/virtual_dma_types.h"
#include "replay/replay_memory_report.h"

#define TEST_ASSERT(expr) do { if (!(expr)) { std::cerr << "Assertion failed: " #expr " at " << __FILE__ << ":" << __LINE__ << std::endl; std::abort(); } } while (0)
#undef assert
#define assert(expr) TEST_ASSERT(expr)

#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

using namespace winios;
using namespace winios::device;

namespace {

struct BenchResults {
    double mmio_reads_per_sec{0};
    double mmio_writes_per_sec{0};
    double descriptor_validations_per_sec{0};
    double dma_64b_mbs{0};
    double dma_4kb_mbs{0};
    double dma_64kb_mbs{0};
    double snapshot_capture_us{0};
    double snapshot_restore_us{0};
    std::size_t peak_rss_mb{0};
};

BenchResults run_benchmarks() {
    BenchResults r;
    const std::size_t ram = 4 * 1024 * 1024; // 4 MB
    auto mem = std::make_shared<SmpSharedMemory>(ram, 8);
    auto dev = std::make_shared<VirtualDmaDevice>(0x444D4131, 0x10000000, 0x1000, mem, 48);
    VirtualDeviceBus bus;
    bus.register_device(dev);

    // 1. MMIO Register Access Benchmark
    constexpr int kMmioIters = 500000;
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < kMmioIters; ++i) {
            (void)bus.read_mmio(0x10000038, 8); // Read device ID
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        r.mmio_reads_per_sec = kMmioIters / sec;
    }
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < kMmioIters; ++i) {
            bus.write_mmio(0x10000010, 0x5000, 8); // Write descriptor addr
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        r.mmio_writes_per_sec = kMmioIters / sec;
    }

    // 2. Descriptor Validation Benchmark
    constexpr int kValIters = 500000;
    {
        VirtualDmaDescriptor desc{};
        desc.descriptor_id = 1;
        desc.src_paddr = 0x1000;
        desc.dst_paddr = 0x2000;
        desc.length_bytes = 4096;
        desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);

        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < kValIters; ++i) {
            (void)dev->engine().validate_descriptor(desc, ram);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        r.descriptor_validations_per_sec = kValIters / sec;
    }

    // 3. DMA Transfer Benchmarks (64 B, 4 KiB, 64 KiB)
    auto bench_dma = [&](std::uint64_t len, int iters) -> double {
        const std::uint64_t src = 0x10000;
        const std::uint64_t dst = 0x100000;
        const std::uint64_t desc_addr = 0x8000;

        VirtualDmaDescriptor desc{};
        desc.descriptor_id = 1;
        desc.src_paddr = src;
        desc.dst_paddr = dst;
        desc.length_bytes = len;
        desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
        mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iters; ++i) {
            (void)dev->engine().execute_descriptor_chain(desc_addr, 0);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        double total_mb = (len * iters) / (1024.0 * 1024.0);
        return total_mb / sec;
    };

    r.dma_64b_mbs = bench_dma(64, 10000);
    r.dma_4kb_mbs = bench_dma(4096, 5000);
    r.dma_64kb_mbs = bench_dma(65536, 1000);

    // 4. Device State Snapshot Capture & Restore Benchmark
    constexpr int kSnapIters = 50000;
    std::vector<std::uint8_t> snap_blob;
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < kSnapIters; ++i) {
            snap_blob = dev->serialize_state();
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        r.snapshot_capture_us = us / kSnapIters;
    }
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < kSnapIters; ++i) {
            (void)dev->deserialize_state(snap_blob.data(), snap_blob.size());
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        r.snapshot_restore_us = us / kSnapIters;
    }

    auto mem_info = replay::sample_process_memory();
    r.peak_rss_mb = static_cast<double>(mem_info.peak_working_set_bytes) / (1024.0 * 1024.0);

    return r;
}

} // namespace

int main() {
    std::cout << "===================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 31: Virtual DMA Performance Profile        \n";
    std::cout << "===================================================================\n\n";

    auto r = run_benchmarks();

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "-------------------------------------------------------------------\n";
    std::cout << " Metric                                      | Value               \n";
    std::cout << "-------------------------------------------------------------------\n";
    std::cout << " MMIO Reads / sec                            | " << std::setw(15) << r.mmio_reads_per_sec << " ops/s\n";
    std::cout << " MMIO Writes / sec                           | " << std::setw(15) << r.mmio_writes_per_sec << " ops/s\n";
    std::cout << " Descriptor Validations / sec                | " << std::setw(15) << r.descriptor_validations_per_sec << " desc/s\n";
    std::cout << " Small DMA (64 B) Throughput                 | " << std::setw(15) << r.dma_64b_mbs << " MB/s\n";
    std::cout << " Medium DMA (4 KiB) Throughput               | " << std::setw(15) << r.dma_4kb_mbs << " MB/s\n";
    std::cout << " Large DMA (64 KiB) Throughput               | " << std::setw(15) << r.dma_64kb_mbs << " MB/s\n";
    std::cout << " Device Snapshot Capture Latency             | " << std::setw(15) << r.snapshot_capture_us << " us\n";
    std::cout << " Device Snapshot Restore Latency             | " << std::setw(15) << r.snapshot_restore_us << " us\n";
    std::cout << " Peak Process RSS                            | " << std::setw(15) << r.peak_rss_mb << " MB\n";
    std::cout << "-------------------------------------------------------------------\n";

    std::cout << "\n>>> VIRTUAL DMA BENCHMARK COMPLETE <<<\n";
    return 0;
}

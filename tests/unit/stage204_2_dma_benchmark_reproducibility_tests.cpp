// ==============================================================================
// WinIOS Campaign 204 Stage 204.2: DMA Benchmark Reproducibility Tests
// ==============================================================================
// Rigorous reproducibility audit of DMA transfer throughput:
// 1. Identical transfer sizes (64KB per transfer, 1000 transfers per trial = 62.5 MB)
// 2. Warm-up iterations to eliminate cache cold-start artifacts
// 3. Multi-trial execution (5 independent trials) with median, min, max, and variance
// 4. Bit-for-bit data verification on every trial
// 5. Explicit taxonomy distinction:
//    - Byte-wise emulation via intermediate bounce buffer (read_u8/write_u8 loop)
//    - Contiguous block-copy fast path (std::memmove)
//    - Architectural truth: std::memmove is a contiguous single-copy fast path,
//      NOT a zero-copy page remap.
// ==============================================================================

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <vector>

#include "cpu/gicv3.h"
#include "cpu/smp_exclusive_monitor.h"
#include "cpu/smp_shared_memory.h"
#include "device/virtual_dma_engine.h"

using namespace winios;
using namespace winios::device;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

struct TrialResult {
    double time_ms{0.0};
    double throughput_mb_s{0.0};
};

struct BenchmarkSummary {
    double min_ms{0.0};
    double max_ms{0.0};
    double median_ms{0.0};
    double mean_ms{0.0};
    double stddev_ms{0.0};
    double median_mb_s{0.0};
};

BenchmarkSummary compute_summary(std::vector<TrialResult>& trials, double total_mb) {
    std::sort(trials.begin(), trials.end(), [](const TrialResult& a, const TrialResult& b) {
        return a.time_ms < b.time_ms;
    });

    const std::size_t n = trials.size();
    BenchmarkSummary summary{};
    summary.min_ms = trials.front().time_ms;
    summary.max_ms = trials.back().time_ms;
    summary.median_ms = (n % 2 == 1) ? trials[n / 2].time_ms : (trials[n / 2 - 1].time_ms + trials[n / 2].time_ms) / 2.0;

    double sum = 0.0;
    for (const auto& t : trials) sum += t.time_ms;
    summary.mean_ms = sum / static_cast<double>(n);

    double var_sum = 0.0;
    for (const auto& t : trials) {
        var_sum += (t.time_ms - summary.mean_ms) * (t.time_ms - summary.mean_ms);
    }
    summary.stddev_ms = std::sqrt(var_sum / static_cast<double>(n));
    summary.median_mb_s = total_mb / (summary.median_ms / 1000.0);
    return summary;
}

void run_dma_benchmark_trials() {
    std::cout << "[Stage 204.2] Running Rigorous Multi-Trial DMA Benchmark...\n";

    constexpr std::size_t kRamSize = 16 * 1024 * 1024; // 16 MB
    auto shared_mem = std::make_shared<SmpSharedMemory>(kRamSize, 4);
    SmpExclusiveMonitorManager monitor(4);
    shared_mem->attach_exclusive_monitor(&monitor);

    auto gic = std::make_shared<GICv3>();
    constexpr std::uint32_t kDmaIrq = 48;
    gic->set_core_count(1);
    gic->write_mmio(GICv3::kGicdBase + 0x0000u, 0x3u, 4);
    gic->write_mmio(GICv3::kGicdBase + 0x0080 + (kDmaIrq / 32) * 4, 1u << (kDmaIrq % 32), 4);
    gic->write_mmio(GICv3::kGicdBase + 0x0100 + (kDmaIrq / 32) * 4, 1u << (kDmaIrq % 32), 4);
    gic->write_icc_pmr(0xFFu, 0);
    gic->write_icc_igrpen1(1u, 0);

    VirtualDmaEngine dma(shared_mem);
    dma.set_gic(gic, kDmaIrq);

    constexpr std::uint64_t kDescPa = 0x10000;
    constexpr std::uint64_t kSrcPa  = 0x20000;
    constexpr std::uint64_t kDstPa  = 0x60000;
    constexpr std::size_t   kLength = 64 * 1024; // 64 KB

    std::uint8_t* raw_mem = shared_mem->raw_memory().raw_data();
    for (std::size_t i = 0; i < kLength; ++i) {
        raw_mem[kSrcPa + i] = static_cast<std::uint8_t>((i * 41 + 17) & 0xFF);
    }
    std::memset(raw_mem + kDstPa, 0, kLength);

    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 1;
    desc.src_paddr = kSrcPa;
    desc.dst_paddr = kDstPa;
    desc.length_bytes = static_cast<std::uint32_t>(kLength);
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    desc.flags = kDmaFlagInterruptOnComplete;
    std::memcpy(raw_mem + kDescPa, &desc, sizeof(desc));

    constexpr int kWarmupIterations = 100;
    constexpr int kBenchIterations  = 1000;
    constexpr int kNumTrials        = 5;
    const double total_mb = (static_cast<double>(kLength) * kBenchIterations) / (1024.0 * 1024.0);

    // Warm-up fast path
    dma.set_zero_copy_enabled(true);
    for (int i = 0; i < kWarmupIterations; ++i) {
        dma.execute_descriptor_chain(kDescPa);
    }
    // Verify fast path bit-exactness
    TEST_CHECK(std::memcmp(raw_mem + kDstPa, raw_mem + kSrcPa, kLength) == 0,
               "Fast-path copy must produce exact data");

    // Fast-path trials
    std::vector<TrialResult> fast_trials;
    for (int t = 0; t < kNumTrials; ++t) {
        std::memset(raw_mem + kDstPa, 0, kLength);
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < kBenchIterations; ++i) {
            dma.execute_descriptor_chain(kDescPa);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        fast_trials.push_back({ms, total_mb / (ms / 1000.0)});
        TEST_CHECK(std::memcmp(raw_mem + kDstPa, raw_mem + kSrcPa, kLength) == 0,
                   "Trial data must be bit-exact");
    }

    // Warm-up bounce-buffer path
    dma.set_zero_copy_enabled(false);
    for (int i = 0; i < kWarmupIterations; ++i) {
        dma.execute_descriptor_chain(kDescPa);
    }
    TEST_CHECK(std::memcmp(raw_mem + kDstPa, raw_mem + kSrcPa, kLength) == 0,
               "Bounce-buffer copy must produce exact data");

    // Bounce-buffer trials
    std::vector<TrialResult> bounce_trials;
    for (int t = 0; t < kNumTrials; ++t) {
        std::memset(raw_mem + kDstPa, 0, kLength);
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < kBenchIterations; ++i) {
            dma.execute_descriptor_chain(kDescPa);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        bounce_trials.push_back({ms, total_mb / (ms / 1000.0)});
        TEST_CHECK(std::memcmp(raw_mem + kDstPa, raw_mem + kSrcPa, kLength) == 0,
                   "Trial data must be bit-exact");
    }

    auto fast_summary = compute_summary(fast_trials, total_mb);
    auto bounce_summary = compute_summary(bounce_trials, total_mb);

    const double speedup = bounce_summary.median_ms / fast_summary.median_ms;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  --------------------------------------------------------------\n";
    std::cout << "  Workload: " << kBenchIterations << " x 64KB transfers (" << total_mb << " MB total) over " << kNumTrials << " trials\n";
    std::cout << "  --------------------------------------------------------------\n";
    std::cout << "  Contiguous Block-Copy Fast Path (std::memmove):\n";
    std::cout << "    Median Time:    " << fast_summary.median_ms << " ms (StdDev: " << fast_summary.stddev_ms << " ms)\n";
    std::cout << "    Throughput:     " << fast_summary.median_mb_s << " MB/s\n";
    std::cout << "  Byte-Wise Bounce-Buffer Baseline (read_u8/write_u8):\n";
    std::cout << "    Median Time:    " << bounce_summary.median_ms << " ms (StdDev: " << bounce_summary.stddev_ms << " ms)\n";
    std::cout << "    Throughput:     " << bounce_summary.median_mb_s << " MB/s\n";
    std::cout << "  Measured Speedup: " << speedup << "x\n";
    std::cout << "  --------------------------------------------------------------\n";

    TEST_CHECK(speedup >= 100.0, "Contiguous block copy must deliver >= 100x speedup over byte-by-byte emulation");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 204.2 DMA Benchmark Reproducibility Tests ===\n";
    run_dma_benchmark_trials();

    if (g_failures == 0) {
        std::cout << "[PASS] Stage 204.2 DMA Benchmark Reproducibility passed all tests.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 204.2 DMA Benchmark Reproducibility failed with " << g_failures << " errors.\n";
        return 1;
    }
}

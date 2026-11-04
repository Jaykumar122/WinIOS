#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "snapshot/snapshot_manager.h"
#include "snapshot/snapshot_serializer.h"
#include "snapshot/snapshot_validation.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>

namespace {

struct BenchmarkMetric {
    std::string name;
    double median_ms{0.0};
    double min_ms{0.0};
    double max_ms{0.0};
    double throughput_mb_s{0.0};
    std::size_t size_bytes{0};
};

template <typename Func>
BenchmarkMetric run_benchmark(const std::string& name, std::size_t iterations, std::size_t data_bytes, Func&& func) {
    std::vector<double> timings;
    timings.reserve(iterations);

    for (std::size_t i = 0; i < iterations; ++i) {
        const auto start = std::chrono::high_resolution_clock::now();
        func();
        const auto end = std::chrono::high_resolution_clock::now();
        const double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        timings.push_back(duration_ms);
    }

    std::sort(timings.begin(), timings.end());
    const double median = timings[timings.size() / 2];
    const double min_val = timings.front();
    const double max_val = timings.back();
    const double throughput = (data_bytes > 0 && median > 0.0)
        ? (static_cast<double>(data_bytes) / (1024.0 * 1024.0)) / (median / 1000.0)
        : 0.0;

    return BenchmarkMetric{name, median, min_val, max_val, throughput, data_bytes};
}

void print_metric(const BenchmarkMetric& m) {
    std::cout << "  " << std::left << std::setw(36) << m.name
              << " | Median: " << std::right << std::setw(8) << std::fixed << std::setprecision(3) << m.median_ms << " ms"
              << " | Range: [" << std::setw(6) << m.min_ms << " - " << std::setw(6) << m.max_ms << " ms]"
              << " | Throughput: " << std::setw(8) << std::setprecision(1) << m.throughput_mb_s << " MB/s\n";
}

} // namespace

int main() {
    std::cout << "========================================================================\n";
    std::cout << "WinIOS Phase 52 Stage 17: Architectural Snapshot Performance Benchmark\n";
    std::cout << "========================================================================\n";

    constexpr std::size_t kIterations = 20;

    // 1. CPU-Only Benchmark
    CPU cpu;
    for (std::size_t i = 0; i < 31; ++i) cpu.write_register(i, 0x12345678ULL * (i + 1));
    for (std::size_t i = 0; i < 32; ++i) cpu.write_vector_register(i, CPU::VectorRegister{0xAAAULL * i, 0x555ULL * i});

    std::vector<std::uint8_t> cpu_buf;
    auto m_cpu_ser = run_benchmark("CPU State Serialization (1.1 KB)", 500, 1128, [&]() {
        cpu_buf = winios::snapshot::serialize_cpu_state(cpu);
    });
    print_metric(m_cpu_ser);

    CPU dst_cpu;
    auto m_cpu_deser = run_benchmark("CPU State Deserialization", 500, 1128, [&]() {
        (void)winios::snapshot::deserialize_cpu_state(cpu_buf.data(), cpu_buf.size(), dst_cpu);
    });
    print_metric(m_cpu_deser);

    // 2. Memory Sparse Serialization Benchmarks
    std::cout << "\n--- Guest Memory Serialization Benchmarks ---\n";

    // 1 MB Sparse (10% dirty)
    {
        constexpr std::size_t kSize = 1024 * 1024;
        Memory mem(kSize);
        // Dirty ~100 KB
        for (std::size_t o = 0; o < 100 * 1024; o += 16) mem.write_u64(o, 0xAABBCCDDEEFF0011ULL);

        std::vector<std::uint8_t> mem_buf;
        auto m_mem1 = run_benchmark("Memory 1 MB Sparse (10% dirty)", kIterations, kSize, [&]() {
            mem_buf = winios::snapshot::serialize_memory_state(mem);
        });
        print_metric(m_mem1);
        std::cout << "    -> Serialized size on disk: " << mem_buf.size() << " bytes (Sparse ratio: "
                  << std::fixed << std::setprecision(1) << (static_cast<double>(kSize) / mem_buf.size()) << "x)\n";

        Memory restore_mem(kSize);
        auto m_mem1_res = run_benchmark("Memory 1 MB Restore", kIterations, kSize, [&]() {
            (void)winios::snapshot::deserialize_memory_state(mem_buf.data(), mem_buf.size(), restore_mem);
        });
        print_metric(m_mem1_res);
    }

    // 16 MB Sparse (25% dirty)
    {
        constexpr std::size_t kSize = 16 * 1024 * 1024;
        Memory mem(kSize);
        // Dirty ~4 MB
        for (std::size_t o = 0; o < 4 * 1024 * 1024; o += 32) mem.write_u64(o, 0x1122334455667788ULL);

        std::vector<std::uint8_t> mem_buf;
        auto m_mem16 = run_benchmark("Memory 16 MB Sparse (25% dirty)", 10, kSize, [&]() {
            mem_buf = winios::snapshot::serialize_memory_state(mem);
        });
        print_metric(m_mem16);
        std::cout << "    -> Serialized size on disk: " << mem_buf.size() << " bytes (Sparse ratio: "
                  << std::fixed << std::setprecision(1) << (static_cast<double>(kSize) / mem_buf.size()) << "x)\n";

        Memory restore_mem(kSize);
        auto m_mem16_res = run_benchmark("Memory 16 MB Restore", 10, kSize, [&]() {
            (void)winios::snapshot::deserialize_memory_state(mem_buf.data(), mem_buf.size(), restore_mem);
        });
        print_metric(m_mem16_res);
    }

    // 64 MB Sparse (Typical Boot VM footprint)
    {
        constexpr std::size_t kSize = 64 * 1024 * 1024;
        Memory mem(kSize);
        // Dirty ~8 MB
        for (std::size_t o = 0; o < 8 * 1024 * 1024; o += 64) mem.write_u64(o, 0xFEDCBA9876543210ULL);

        std::vector<std::uint8_t> mem_buf;
        auto m_mem64 = run_benchmark("Memory 64 MB Sparse (Typical Boot)", 5, kSize, [&]() {
            mem_buf = winios::snapshot::serialize_memory_state(mem);
        });
        print_metric(m_mem64);
        std::cout << "    -> Serialized size on disk: " << mem_buf.size() << " bytes (Sparse ratio: "
                  << std::fixed << std::setprecision(1) << (static_cast<double>(kSize) / mem_buf.size()) << "x)\n";

        Memory restore_mem(kSize);
        auto m_mem64_res = run_benchmark("Memory 64 MB Restore", 5, kSize, [&]() {
            (void)winios::snapshot::deserialize_memory_state(mem_buf.data(), mem_buf.size(), restore_mem);
        });
        print_metric(m_mem64_res);
    }

    // 3. Full Snapshot File End-to-End
    std::cout << "\n--- Full End-to-End System Snapshot File Benchmarks ---\n";
    {
        constexpr std::size_t kSize = 16 * 1024 * 1024;
        Memory mem(kSize);
        mem.write_u64(0x1000, 0x1234ULL);
        mem.write_u64(0x800000, 0x5678ULL);
        winios::VirtualTime vt;
        winios::GICv3 gic;
        winios::snapshot::SnapshotRestoreContext ctx{&cpu, &mem, &vt, &gic, {}};

        const std::filesystem::path bench_file = "benchmark_test.wsnap";

        auto m_file_save = run_benchmark("End-to-End Snapshot Capture & Save", 10, kSize, [&]() {
            (void)winios::snapshot::SnapshotManager::capture_to_file(ctx, bench_file);
        });
        print_metric(m_file_save);

        CPU rest_cpu;
        Memory rest_mem(kSize);
        winios::VirtualTime rest_vt;
        winios::GICv3 rest_gic;
        winios::snapshot::SnapshotRestoreContext rest_ctx{&rest_cpu, &rest_mem, &rest_vt, &rest_gic, {}};

        auto m_file_load = run_benchmark("End-to-End Snapshot Load & Restore", 10, kSize, [&]() {
            (void)winios::snapshot::SnapshotManager::restore_from_file(bench_file, rest_ctx);
        });
        print_metric(m_file_load);

        auto m_val = run_benchmark("Snapshot File Validation & CRC Check", 10, kSize, [&]() {
            (void)winios::snapshot::SnapshotManager::validate_file(bench_file);
        });
        print_metric(m_val);

        std::filesystem::remove(bench_file);
    }

    std::cout << "\n========================================================================\n";
    std::cout << "BENCHMARK SUITE COMPLETED SUCCESSFULLY\n";
    std::cout << "========================================================================\n";
    return 0;
}

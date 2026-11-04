#include "sndl/sndl_event_encoder.h"
#include "sndl/sndl_format.h"
#include "sndl/sndl_recorder.h"
#include "sndl/sndl_source_hooks.h"
#include "sndl/sndl_validation.h"
#include "sndl/sndl_writer.h"
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "cpu/virtual_time.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>

namespace {

using Clock = std::chrono::high_resolution_clock;

struct BenchmarkResult {
    std::string name;
    double median_ms{0.0};
    double min_ms{0.0};
    double max_ms{0.0};
    double throughput_events_sec{0.0};
    double throughput_mb_sec{0.0};
};

BenchmarkResult run_benchmark(const std::string& name, std::size_t iterations,
                              std::size_t events_per_iter,
                              const std::function<void()>& fn) {
    std::vector<double> timings_ms;
    timings_ms.reserve(iterations);

    // Warm-up run
    fn();

    for (std::size_t i = 0; i < iterations; ++i) {
        const auto start = Clock::now();
        fn();
        const auto end = Clock::now();
        const double ms = std::chrono::duration<double, std::milli>(end - start).count();
        timings_ms.push_back(ms);
    }

    std::sort(timings_ms.begin(), timings_ms.end());
    const double median = timings_ms[timings_ms.size() / 2];
    const double min_v = timings_ms.front();
    const double max_v = timings_ms.back();

    BenchmarkResult res{};
    res.name = name;
    res.median_ms = median;
    res.min_ms = min_v;
    res.max_ms = max_v;
    if (median > 0.0) {
        res.throughput_events_sec = (static_cast<double>(events_per_iter) / (median / 1000.0));
        res.throughput_mb_sec = (static_cast<double>(events_per_iter * winios::sndl::kWSndlRecordDiskSize) / (1024.0 * 1024.0)) / (median / 1000.0);
    }
    return res;
}

void print_result(const BenchmarkResult& r) {
    std::cout << "  " << std::setw(36) << std::left << r.name << " | "
              << "Median: " << std::setw(7) << std::fixed << std::setprecision(3) << r.median_ms << " ms | "
              << "Range: [" << std::setw(6) << r.min_ms << " - " << std::setw(6) << r.max_ms << " ms] | "
              << "Rate: " << std::setw(8) << std::fixed << std::setprecision(1) << (r.throughput_events_sec / 1e3) << " kEv/s | "
              << "Bandwidth: " << std::setw(7) << std::fixed << std::setprecision(1) << r.throughput_mb_sec << " MB/s\n";
}

} // namespace

int main() {
    std::cout << "========================================================================\n";
    std::cout << "WinIOS Phase 52 Stage 18: SNDL Recording Performance Benchmark\n";
    std::cout << "========================================================================\n\n";

    constexpr std::size_t kIter = 30;
    constexpr std::size_t kRecordBatch = 10000;

    // 1. In-Memory Record Encoding Benchmark
    {
        std::vector<winios::sndl::SndlRecord> records(kRecordBatch);
        for (std::size_t i = 0; i < kRecordBatch; ++i) {
            records[i] = winios::sndl::make_mmio_read_record(0, 0x1000, i, 0, 0x4000, i * 7, 4);
        }
        std::vector<std::uint8_t> buffer(kRecordBatch * winios::sndl::kWSndlRecordDiskSize);

        const auto res = run_benchmark("SndlRecord 64B Encoding (10k)", kIter, kRecordBatch, [&]() {
            for (std::size_t i = 0; i < kRecordBatch; ++i) {
                winios::sndl::encode_record(records[i], buffer.data() + i * winios::sndl::kWSndlRecordDiskSize);
            }
        });
        print_result(res);
    }

    // 2. In-Memory Record Decoding Benchmark
    {
        std::vector<winios::sndl::SndlRecord> records(kRecordBatch);
        for (std::size_t i = 0; i < kRecordBatch; ++i) {
            records[i] = winios::sndl::make_mmio_read_record(0, 0x1000, i, 0, 0x4000, i * 7, 4);
        }
        std::vector<std::uint8_t> buffer(kRecordBatch * winios::sndl::kWSndlRecordDiskSize);
        for (std::size_t i = 0; i < kRecordBatch; ++i) {
            winios::sndl::encode_record(records[i], buffer.data() + i * winios::sndl::kWSndlRecordDiskSize);
        }
        winios::sndl::SndlRecord out_rec{};

        const auto res = run_benchmark("SndlRecord 64B Decoding (10k)", kIter, kRecordBatch, [&]() {
            for (std::size_t i = 0; i < kRecordBatch; ++i) {
                (void)winios::sndl::decode_record(buffer.data() + i * winios::sndl::kWSndlRecordDiskSize,
                                                  winios::sndl::kWSndlRecordDiskSize, out_rec);
            }
        });
        print_result(res);
    }

    // 3. Block Writer Disk Throughput Benchmark
    {
        const std::filesystem::path bench_file = "bench_writer.sndl";
        std::vector<winios::sndl::SndlRecord> records(kRecordBatch);
        for (std::size_t i = 0; i < kRecordBatch; ++i) {
            records[i] = winios::sndl::make_mmio_read_record(0, 0x1000, i, 0, 0x4000, i * 7, 4);
        }

        const auto res = run_benchmark("Block Writer Disk Flush (10k)", 15, kRecordBatch, [&]() {
            std::filesystem::remove(bench_file);
            winios::sndl::SndlWriter writer(1024);
            winios::sndl::WSndlHeader hdr{};
            (void)writer.open(bench_file, hdr);
            for (std::size_t i = 0; i < kRecordBatch; ++i) {
                (void)writer.write_record(records[i]);
            }
            (void)writer.finalize(winios::sndl::SndlFinalizationStatus::Completed, kRecordBatch);
        });
        print_result(res);
        std::filesystem::remove(bench_file);
    }

    // 4. Live SndlRecorder Full Pipeline (Submission + Buffering + File I/O)
    {
        const std::filesystem::path bench_file = "bench_recorder.sndl";
        const auto res = run_benchmark("SndlRecorder Full Pipeline (10k)", 15, kRecordBatch, [&]() {
            std::filesystem::remove(bench_file);
            winios::sndl::SndlRecorder recorder(kRecordBatch * 2);
            (void)recorder.start_recording(bench_file, 1);
            for (std::size_t i = 0; i < kRecordBatch; ++i) {
                recorder.record_mmio_read(0, 0x1000, i, 0x4000 + (i % 64), i, 4, 1);
            }
            (void)recorder.stop_recording(kRecordBatch);
        });
        print_result(res);
        std::filesystem::remove(bench_file);
    }

    // 5. Validation and CRC32C Throughput
    {
        const std::filesystem::path bench_file = "bench_val.sndl";
        std::filesystem::remove(bench_file);
        winios::sndl::SndlRecorder recorder(kRecordBatch * 2);
        (void)recorder.start_recording(bench_file, 1);
        for (std::size_t i = 0; i < kRecordBatch; ++i) {
            recorder.record_mmio_read(0, 0x1000, i, 0x4000 + (i % 64), i, 4, 1);
        }
        (void)recorder.stop_recording(kRecordBatch);

        const auto res = run_benchmark("SNDL File Integrity Audit (10k)", kIter, kRecordBatch, [&]() {
            (void)winios::sndl::SndlValidation::validate_file(bench_file);
        });
        print_result(res);
        std::filesystem::remove(bench_file);
    }

    // 6. CPU Execution Overhead: Recorder Disabled vs Enabled
    {
        std::cout << "\n--- CPU Instruction Execution Overhead Comparison ---\n";
        CPU cpu;
        Memory mem(1024 * 1024);
        winios::VirtualTime vt;
        cpu.bind_virtual_time(&vt, 0);

        // 1,000 instructions: ADD X0, X0, #1
        for (std::size_t i = 0; i < 1000; ++i) {
            mem.write_u32(i * 4, 0x91000400u); // ADD X0, X0, #1
        }

        // Disabled
        winios::sndl::set_active_recorder(nullptr);
        const auto res_disabled = run_benchmark("CPU 1k Instructions (Disabled)", 50, 1000, [&]() {
            cpu.set_program_counter(0);
            for (int i = 0; i < 1000; ++i) {
                cpu.step(mem);
            }
        });
        print_result(res_disabled);

        // Enabled
        const std::filesystem::path bench_cpu_file = "bench_cpu.sndl";
        std::filesystem::remove(bench_cpu_file);
        winios::sndl::SndlRecorder active_rec(10000);
        (void)active_rec.start_recording(bench_cpu_file, 1);
        winios::sndl::set_active_recorder(&active_rec);

        const auto res_enabled = run_benchmark("CPU 1k Instructions (Enabled)", 50, 1000, [&]() {
            cpu.set_program_counter(0);
            for (int i = 0; i < 1000; ++i) {
                cpu.step(mem);
            }
        });
        print_result(res_enabled);

        winios::sndl::set_active_recorder(nullptr);
        (void)active_rec.stop_recording(1000);
        std::filesystem::remove(bench_cpu_file);
    }

    std::cout << "\n========================================================================\n";
    std::cout << "BENCHMARK SUITE COMPLETED SUCCESSFULLY\n";
    std::cout << "========================================================================\n";
    return 0;
}

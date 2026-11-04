#include "snapshot/smp_snapshot_manager.h"
#include "snapshot/smp_snapshot_format.h"
#include "trace/trace_serializer.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

namespace {

using Clock = std::chrono::high_resolution_clock;

struct BenchMetrics {
    double capture_ms{0.0};
    double validate_ms{0.0};
    double restore_ms{0.0};
    double hash_ms{0.0};
    std::size_t snapshot_size_bytes{0};
    double capture_throughput_mb_s{0.0};
    double restore_throughput_mb_s{0.0};
};

BenchMetrics run_benchmark(std::size_t num_cores, std::size_t mem_bytes, bool sparse) {
    auto shared_mem = std::make_shared<winios::SmpSharedMemory>(mem_bytes, num_cores);
    winios::SmpScheduler sched(shared_mem, num_cores, 5000);

    if (!sparse) {
        // Fill memory densely with pseudo-random pattern
        auto* raw = shared_mem->raw_memory().raw_data();
        for (std::size_t i = 0; i < mem_bytes; ++i) {
            raw[i] = static_cast<std::uint8_t>((i * 137 + 13) & 0xFF);
        }
    } else {
        // Write only a few chunks
        for (std::size_t i = 0; i < mem_bytes; i += 65536) {
            shared_mem->raw_memory().write_u64(i, 0x123456789ABCDEF0ULL);
        }
    }

    for (std::size_t c = 0; c < num_cores; ++c) {
        auto cpu = std::make_shared<CPU>();
        sched.register_core(c, cpu, winios::CoreLifecycleState::Runnable);
        // Write code loop: 2000 instructions of ADD X0, X0, #1 followed by branch to self (0x14000000)
        const std::uint64_t code_base = c * 0x8000;
        for (std::size_t i = 0; i < 2000; ++i) {
            shared_mem->raw_memory().write_u32(code_base + i * 4, 0x91000400u);
        }
        shared_mem->raw_memory().write_u32(code_base + 2000 * 4, 0x14000000u);
        cpu->set_program_counter(code_base);
        sched.get_slot(c).state.pc = code_base;
    }

    // Step scheduler
    sched.run_deterministic(1000);

    BenchMetrics m;

    // 1. Capture to buffer
    std::vector<std::uint8_t> buffer;
    winios::snapshot::SmpSnapshotCaptureOptions opts;
    opts.omit_zero_chunks = sparse;

    const int cap_iters = (mem_bytes >= 16 * 1024 * 1024) ? 1 : 2;
    const auto t0 = Clock::now();
    for (int it = 0; it < cap_iters; ++it) {
        buffer.clear();
        (void)winios::snapshot::SmpSnapshotManager::capture_to_buffer(sched, buffer, opts);
    }
    const auto t1 = Clock::now();
    m.capture_ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / cap_iters;
    m.snapshot_size_bytes = buffer.size();
    m.capture_throughput_mb_s = (static_cast<double>(mem_bytes) / (1024.0 * 1024.0)) / (m.capture_ms / 1000.0);

    // 2. Validate
    const int val_iters = (mem_bytes >= 16 * 1024 * 1024) ? 2 : 5;
    const auto t2 = Clock::now();
    for (int it = 0; it < val_iters; ++it) {
        (void)winios::snapshot::SmpSnapshotManager::validate_buffer(buffer.data(), buffer.size());
    }
    const auto t3 = Clock::now();
    m.validate_ms = std::chrono::duration<double, std::milli>(t3 - t2).count() / val_iters;

    // 3. Restore
    auto restore_mem = std::make_shared<winios::SmpSharedMemory>(mem_bytes, num_cores);
    winios::SmpScheduler restore_sched(restore_mem, num_cores, 5000);
    for (std::size_t c = 0; c < num_cores; ++c) {
        restore_sched.register_core(c, std::make_shared<CPU>());
    }

    const int rest_iters = (mem_bytes >= 16 * 1024 * 1024) ? 1 : 2;
    const auto t4 = Clock::now();
    for (int it = 0; it < rest_iters; ++it) {
        (void)winios::snapshot::SmpSnapshotManager::restore_from_buffer(buffer.data(), buffer.size(), restore_sched);
    }
    const auto t5 = Clock::now();
    m.restore_ms = std::chrono::duration<double, std::milli>(t5 - t4).count() / rest_iters;
    m.restore_throughput_mb_s = (static_cast<double>(mem_bytes) / (1024.0 * 1024.0)) / (m.restore_ms / 1000.0);

    // 4. Canonical Hash
    const int hash_iters = (mem_bytes >= 16 * 1024 * 1024) ? 1 : 3;
    const auto t6 = Clock::now();
    for (int it = 0; it < hash_iters; ++it) {
        (void)sched.compute_canonical_multicore_hash();
    }
    const auto t7 = Clock::now();
    m.hash_ms = std::chrono::duration<double, std::milli>(t7 - t6).count() / hash_iters;

    return m;
}

} // namespace

int main() {
    try {
    std::cout << "========================================================================================================\n" << std::flush;
    std::cout << "WinIOS Phase 52 Stage 23: Multi-Core Snapshot Scalability & Throughput Benchmark\n" << std::flush;
    std::cout << "========================================================================================================\n";
    std::cout << std::left
              << std::setw(8)  << "Cores"
              << std::setw(12) << "Memory"
              << std::setw(10) << "Mode"
              << std::setw(16) << "Snap Size (KB)"
              << std::setw(16) << "Capture (ms)"
              << std::setw(16) << "Cap MB/s"
              << std::setw(16) << "Restore (ms)"
              << std::setw(16) << "Rest MB/s"
              << std::setw(14) << "Validate (ms)"
              << std::setw(14) << "Hash (ms)"
              << "\n";
    std::cout << "--------------------------------------------------------------------------------------------------------\n";

    const std::vector<std::size_t> core_list = {2, 4, 8};
    const std::vector<std::size_t> mem_list = {1 * 1024 * 1024, 4 * 1024 * 1024, 16 * 1024 * 1024, 64 * 1024 * 1024};

    for (std::size_t cores : core_list) {
        for (std::size_t mem : mem_list) {
            // Sparse
            const auto m_sparse = run_benchmark(cores, mem, true);
            std::cout << std::left
                      << std::setw(8)  << cores
                      << std::setw(12) << (std::to_string(mem / (1024 * 1024)) + " MB")
                      << std::setw(10) << "Sparse"
                      << std::setw(16) << (m_sparse.snapshot_size_bytes / 1024)
                      << std::setw(16) << std::fixed << std::setprecision(2) << m_sparse.capture_ms
                      << std::setw(16) << std::fixed << std::setprecision(1) << m_sparse.capture_throughput_mb_s
                      << std::setw(16) << std::fixed << std::setprecision(2) << m_sparse.restore_ms
                      << std::setw(16) << std::fixed << std::setprecision(1) << m_sparse.restore_throughput_mb_s
                      << std::setw(14) << std::fixed << std::setprecision(3) << m_sparse.validate_ms
                      << std::setw(14) << std::fixed << std::setprecision(3) << m_sparse.hash_ms
                      << "\n" << std::flush;

            // Dense for small/medium sizes
            if (mem <= 16 * 1024 * 1024) {
                const auto m_dense = run_benchmark(cores, mem, false);
                std::cout << std::left
                          << std::setw(8)  << cores
                          << std::setw(12) << (std::to_string(mem / (1024 * 1024)) + " MB")
                          << std::setw(10) << "Dense"
                          << std::setw(16) << (m_dense.snapshot_size_bytes / 1024)
                          << std::setw(16) << std::fixed << std::setprecision(2) << m_dense.capture_ms
                          << std::setw(16) << std::fixed << std::setprecision(1) << m_dense.capture_throughput_mb_s
                          << std::setw(16) << std::fixed << std::setprecision(2) << m_dense.restore_ms
                          << std::setw(16) << std::fixed << std::setprecision(1) << m_dense.restore_throughput_mb_s
                          << std::setw(14) << std::fixed << std::setprecision(3) << m_dense.validate_ms
                          << std::setw(14) << std::fixed << std::setprecision(3) << m_dense.hash_ms
                          << "\n" << std::flush;
            }
        }
    }
    std::cout << "========================================================================================================\n" << std::flush;
    return 0;
    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << "\n" << std::flush;
        return 1;
    }
}

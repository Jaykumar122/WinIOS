#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "cpu/cpu.h"
#include "cpu/smp_core_state.h"
#include "cpu/smp_exclusive_monitor.h"
#include "cpu/smp_interrupt_router.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

#define CHECK(expr)                                                           \
    do {                                                                      \
        if (!(expr)) {                                                        \
            std::cerr << "CHECK failed: " #expr " at " << __FILE__ << ":"     \
                      << __LINE__ << "\n";                                    \
            std::abort();                                                     \
        }                                                                     \
    } while (false)

double elapsed_ms(std::chrono::steady_clock::time_point start,
                  std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

double elapsed_us(std::chrono::steady_clock::time_point start,
                  std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::micro>(end - start).count();
}

double elapsed_ns(std::chrono::steady_clock::time_point start,
                  std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::nano>(end - start).count();
}

// -----------------------------------------------------------------------------
// Benchmark 1: Core State Creation, Capture, Apply & Hashing Latency
// -----------------------------------------------------------------------------
void bench_core_state_latency() {
    std::cout << "--- Benchmark 1: Core State Latency ---\n";
    constexpr std::size_t kIterations = 100000;

    winios::CPU cpu;
    cpu.reset();
    for (std::size_t i = 0; i < 31; ++i) {
        cpu.write_register(i, 0x1234'5678ULL * (i + 1));
    }
    cpu.set_program_counter(0x8000);

    winios::SmpCoreState state;

    // 1. Capture Latency
    auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < kIterations; ++i) {
        state.capture_from_cpu(cpu);
    }
    auto t1 = std::chrono::steady_clock::now();
    const double capture_ns = elapsed_ns(t0, t1) / static_cast<double>(kIterations);

    // 2. Apply Latency
    t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < kIterations; ++i) {
        state.apply_to_cpu(cpu);
    }
    t1 = std::chrono::steady_clock::now();
    const double apply_ns = elapsed_ns(t0, t1) / static_cast<double>(kIterations);

    // 3. Hash Latency
    volatile std::uint64_t dummy_hash = 0;
    t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < kIterations; ++i) {
        dummy_hash = state.compute_canonical_hash();
    }
    t1 = std::chrono::steady_clock::now();
    const double hash_ns = elapsed_ns(t0, t1) / static_cast<double>(kIterations);

    std::cout << "  State Capture: " << std::fixed << std::setprecision(2) << capture_ns << " ns/op\n";
    std::cout << "  State Apply:   " << std::fixed << std::setprecision(2) << apply_ns << " ns/op\n";
    std::cout << "  State Hash:    " << std::fixed << std::setprecision(2) << hash_ns << " ns/op\n\n";
}

// -----------------------------------------------------------------------------
// Benchmark 2: Exclusive Monitor Operation Latency
// -----------------------------------------------------------------------------
void bench_exclusive_monitor_latency() {
    std::cout << "--- Benchmark 2: Exclusive Monitor Latency ---\n";
    constexpr std::size_t kIterations = 500000;
    winios::SmpExclusiveMonitorManager mon(8);

    // LDXR latency
    auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < kIterations; ++i) {
        mon.load_exclusive(0, 0x1000 + (i % 64) * 64, 8);
    }
    auto t1 = std::chrono::steady_clock::now();
    const double ldxr_ns = elapsed_ns(t0, t1) / static_cast<double>(kIterations);

    // Invalidation latency
    t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < kIterations; ++i) {
        mon.invalidate_reservations(1, 0x1000, 8);
    }
    t1 = std::chrono::steady_clock::now();
    const double inval_ns = elapsed_ns(t0, t1) / static_cast<double>(kIterations);

    // CLREX latency
    t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < kIterations; ++i) {
        mon.clear_reservation(0);
    }
    t1 = std::chrono::steady_clock::now();
    const double clrex_ns = elapsed_ns(t0, t1) / static_cast<double>(kIterations);

    std::cout << "  LDXR (Load Exclusive):       " << std::fixed << std::setprecision(2) << ldxr_ns << " ns/op\n";
    std::cout << "  Invalidate (Cross-Core):     " << std::fixed << std::setprecision(2) << inval_ns << " ns/op\n";
    std::cout << "  CLREX (Clear Exclusive):     " << std::fixed << std::setprecision(2) << clrex_ns << " ns/op\n\n";
}

// -----------------------------------------------------------------------------
// Benchmark 3: Shared Memory Access Throughput
// -----------------------------------------------------------------------------
void bench_shared_memory_throughput() {
    std::cout << "--- Benchmark 3: Shared Memory Access Throughput ---\n";
    constexpr std::size_t kIterations = 2000000;
    winios::SmpSharedMemory smem(4 * 1024 * 1024, 4);

    // 64-bit Writes
    auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < kIterations; ++i) {
        const std::uint64_t addr = (i % 32768) * 8;
        smem.write_u64(0, addr, 0x12345678ULL);
    }
    auto t1 = std::chrono::steady_clock::now();
    const double write_ms = elapsed_ms(t0, t1);
    const double write_mops = (static_cast<double>(kIterations) / (write_ms / 1000.0)) / 1e6;
    const double write_bw_mb = (static_cast<double>(kIterations * 8) / (1024.0 * 1024.0)) / (write_ms / 1000.0);

    // 64-bit Reads
    volatile std::uint64_t dummy = 0;
    t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < kIterations; ++i) {
        const std::uint64_t addr = (i % 32768) * 8;
        dummy = smem.read_u64(1, addr);
    }
    t1 = std::chrono::steady_clock::now();
    const double read_ms = elapsed_ms(t0, t1);
    const double read_mops = (static_cast<double>(kIterations) / (read_ms / 1000.0)) / 1e6;
    const double read_bw_mb = (static_cast<double>(kIterations * 8) / (1024.0 * 1024.0)) / (read_ms / 1000.0);

    std::cout << "  Write-64 Throughput: " << std::fixed << std::setprecision(2) << write_mops << " MOps/s (" << write_bw_mb << " MB/s)\n";
    std::cout << "  Read-64 Throughput:  " << std::fixed << std::setprecision(2) << read_mops << " MOps/s (" << read_bw_mb << " MB/s)\n\n";
}

// -----------------------------------------------------------------------------
// Benchmark 4: Multicore Scheduling Throughput (2, 4, 8 Cores)
// -----------------------------------------------------------------------------
void bench_multicore_scheduler_throughput() {
    std::cout << "--- Benchmark 4: Deterministic Multicore Scheduler Throughput ---\n";
    const std::vector<std::size_t> core_counts = {1, 2, 4, 8};
    constexpr std::uint64_t kInstructionsPerCore = 200000;
    constexpr std::uint64_t kQuantum = 1000;

    for (std::size_t num_cores : core_counts) {
        const std::size_t mem_sz = (num_cores + 1) * 65536;
        auto mem = std::make_shared<winios::SmpSharedMemory>(mem_sz, num_cores);
        winios::SmpScheduler sched(mem, num_cores, kQuantum);

        // Fill memory with ADD instructions for each core
        for (std::size_t c = 0; c < num_cores; ++c) {
            const std::uint64_t base = c * 0x1000;
            for (std::size_t i = 0; i < 1000; ++i) {
                mem->write_u32(c, base + i * 4, 0x91000400u); // ADD X0, X0, #1
            }
            // Loop back to base
            // B -4000 bytes = 0x17ffff00
            mem->write_u32(c, base + 1000 * 4, 0x17ffff00u);

            auto cpu = std::make_shared<winios::CPU>();
            cpu->set_program_counter(base);
            sched.register_core(c, cpu, winios::CoreLifecycleState::Runnable);
        }

        const std::uint64_t total_target = num_cores * kInstructionsPerCore;
        const auto t0 = std::chrono::steady_clock::now();
        const std::uint64_t executed = sched.run_deterministic(total_target);
        const auto t1 = std::chrono::steady_clock::now();

        const double dur_ms = elapsed_ms(t0, t1);
        const double mips = (static_cast<double>(executed) / (dur_ms / 1000.0)) / 1e6;
        const std::size_t quantums = sched.transition_history().size();
        const double us_per_quantum = (dur_ms * 1000.0) / static_cast<double>(quantums);

        std::cout << "  " << num_cores << "-Core Scheduler: "
                  << executed << " insts in " << std::fixed << std::setprecision(2) << dur_ms << " ms | "
                  << mips << " MIPS | " << quantums << " quantums (" << us_per_quantum << " us/quantum)\n";
    }
    std::cout << "\n";
}

} // namespace

int main() {
    std::cout << "========================================================\n";
    std::cout << "  WinIOS Phase 52 Stage 22: SMP Foundation Benchmarks\n";
    std::cout << "========================================================\n\n";

    bench_core_state_latency();
    bench_exclusive_monitor_latency();
    bench_shared_memory_throughput();
    bench_multicore_scheduler_throughput();

    std::cout << "========================================================\n";
    std::cout << "  SMP FOUNDATION BENCHMARKS COMPLETED SUCCESSFULLY\n";
    std::cout << "========================================================\n";
    return 0;
}

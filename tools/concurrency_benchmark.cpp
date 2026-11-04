// Phase 43: deterministic-vs-parallel throughput benchmark.
//
// The workload is deliberately register-only (ADD + branch) so the measurement
// isolates scheduler and guest-clock overhead rather than guest memory
// contention. Memory-heavy workloads are covered by the concurrency tests and
// are expected to serialize at the store lock by design.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "cpu/smp_cluster.h"

namespace {

constexpr std::uint32_t kAddX0X0One = 0x91000400u; // ADD X0, X0, #1
constexpr std::uint32_t kBranchBack4 = 0x17FFFFFFu; // B .-4

void install_loop(winios::SMPCluster& cluster, std::size_t entry) {
    Memory& memory = cluster.get_memory();
    memory.write_u32(entry + 0u, kAddX0X0One);
    memory.write_u32(entry + 4u, kBranchBack4);
}

void prepare_cores(winios::SMPCluster& cluster, std::size_t entry) {
    cluster.initialize_core(0, entry);
    for (std::size_t i = 1; i < cluster.core_count(); ++i) {
        cluster.start_secondary_core(i, entry);
    }
}

double measure_deterministic(std::size_t cores, std::size_t budget) {
    winios::SMPCluster cluster(cores, 0x20000);
    install_loop(cluster, 0x1000);
    prepare_cores(cluster, 0x1000);

    const auto start = std::chrono::steady_clock::now();
    const std::size_t retired = cluster.run_deterministic(budget);
    const auto stop = std::chrono::steady_clock::now();
    const double seconds =
        std::chrono::duration<double>(stop - start).count();
    std::printf("deterministic cores=%zu retired=%zu time=%.6f s "
                "throughput=%.0f inst/s\n",
                cores, retired, seconds,
                static_cast<double>(retired) / seconds);
    return static_cast<double>(retired) / seconds;
}

double measure_parallel(std::size_t cores, std::size_t budget) {
    winios::SMPCluster cluster(cores, 0x20000);
    install_loop(cluster, 0x1000);
    prepare_cores(cluster, 0x1000);

    // Warm the worker threads and page-in allocations before timing.
    cluster.run_parallel(cores * 1000u);

    const auto start = std::chrono::steady_clock::now();
    const std::size_t retired = cluster.run_parallel(budget);
    const auto stop = std::chrono::steady_clock::now();
    cluster.stop_parallel();

    const double seconds =
        std::chrono::duration<double>(stop - start).count();
    std::printf("parallel      cores=%zu retired=%zu time=%.6f s "
                "throughput=%.0f inst/s\n",
                cores, retired, seconds,
                static_cast<double>(retired) / seconds);
    return static_cast<double>(retired) / seconds;
}

} // namespace

int main() {
    constexpr std::size_t kBudget = 2000000;
    std::printf("Phase 43 concurrency benchmark (register-only loop)\n");
    for (std::size_t cores : {1u, 2u, 4u}) {
        const double det = measure_deterministic(cores, kBudget);
        const double par = measure_parallel(cores, kBudget);
        std::printf("  cores=%zu parallel/deterministic throughput ratio=%.3f\n",
                    cores, par / det);
    }
    return 0;
}

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/smp_cluster.h"

namespace {

constexpr std::uint32_t enc_add(unsigned rd, unsigned rn, std::uint16_t imm) {
    return 0x91000000u | ((imm & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}
constexpr std::uint32_t enc_b(std::int32_t word_offset) {
    return 0x14000000u | (static_cast<std::uint32_t>(word_offset) & 0x03FFFFFFu);
}
constexpr std::uint32_t enc_ldr(unsigned rt, unsigned rn) {
    return 0xF9400000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}
constexpr std::uint32_t enc_eret() {
    return 0xD69F03E0u;
}

struct BenchResult {
    const char* name;
    std::size_t instructions;
    double seconds;
    double throughput;
    double relative_overhead_percent;
};

} // namespace

int main() {
    std::printf("=== WinIOS Phase 51 Stage 9: Debug Architecture Performance Benchmark ===\n");
    constexpr std::size_t kIterations = 1000000;
    Memory mem(0x100000);

    // Prepare code buffer: loop of 10 ADDs + 1 B
    for (std::size_t i = 0; i < 10; ++i) {
        mem.write_u32(0x1000 + i * 4, enc_add(0, 0, 1));
    }
    mem.write_u32(0x1000 + 40, enc_b(-10)); // branch back to 0x1000

    // Also data location at 0x8000
    mem.write_u64(0x8000, 0x123456789ABCDEF0ULL);

    std::vector<BenchResult> results;
    double baseline_throughput = 0.0;

    auto run_cpu_bench = [&](const char* name, auto configure_fn) {
        CPU cpu;
        cpu.set_architectural_exceptions(true);
        cpu.set_current_el(1);
        cpu.set_vbar_el1(0x20000);
        cpu.set_program_counter(0x1000);
        configure_fn(cpu);

        const auto start = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < kIterations; ++i) {
            cpu.step(mem);
        }
        const auto stop = std::chrono::steady_clock::now();
        const double secs = std::chrono::duration<double>(stop - start).count();
        const double tput = static_cast<double>(kIterations) / secs;
        if (baseline_throughput == 0.0) {
            baseline_throughput = tput;
        }
        const double overhead = ((baseline_throughput - tput) / baseline_throughput) * 100.0;
        results.push_back({name, kIterations, secs, tput, overhead > 0 ? overhead : 0.0});
        std::printf("%-45s: %8zu inst in %.4f s (%.0f inst/s, overhead: %5.1f%%)\n",
                    name, kIterations, secs, tput, overhead > 0 ? overhead : 0.0);
    };

    // 1. Debug architecture present, all debug controls disabled (baseline)
    run_cpu_bench("1. Debug present, controls disabled (Baseline)", [](CPU& cpu) {
        cpu.get_debug_arch().reset();
    });

    // 2. MDE enabled, no active comparators
    run_cpu_bench("2. MDE enabled, no active comparators", [](CPU& cpu) {
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    });

    // 3. One execution breakpoint configured but not matched
    run_cpu_bench("3. One breakpoint active (unmatched)", [](CPU& cpu) {
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgbvr(0, 0x9000);
        cpu.get_debug_arch().set_dbgbcr(0, 0x1u); // Enabled
    });

    // 4. One watchpoint configured but not matched
    run_cpu_bench("4. One watchpoint active (unmatched)", [](CPU& cpu) {
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgwvr(0, 0x9000);
        cpu.get_debug_arch().set_dbgwcr(0, 0x1u | (0x3u << 3u) | (0xFFu << 5u)); // Enabled, Both, BAS=0xFF
    });

    // 5. Linked breakpoint configured but not matched
    run_cpu_bench("5. Linked breakpoint active (unmatched)", [](CPU& cpu) {
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgbvr(0, 0x9000);
        cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (1u << 20u) | (1u << 16u)); // BT=1, LBN=1
        cpu.get_debug_arch().set_dbgbvr(1, 0x9004);
        cpu.get_debug_arch().set_dbgbcr(1, 0x1u);
    });

    // 6. Linked watchpoint configured but not matched
    run_cpu_bench("6. Linked watchpoint active (unmatched)", [](CPU& cpu) {
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgwvr(0, 0x9000);
        cpu.get_debug_arch().set_dbgwcr(0, 0x1u | (1u << 20u) | (0u << 16u) | (0x3u << 3u) | (0xFFu << 5u)); // WT=1, LBN=0
        cpu.get_debug_arch().set_dbgbvr(0, 0x9000);
        cpu.get_debug_arch().set_dbgbcr(0, 0x1u);
    });

    // 7. Single-step enabled with quick ERET in handler
    {
        // Handler at 0x20200 (EL1h sync exception vector):
        // MSR SPSR_EL1, XZR; ERET
        mem.write_u32(0x20200, enc_eret());

        CPU cpu;
        cpu.set_architectural_exceptions(true);
        cpu.set_current_el(1);
        cpu.set_vbar_el1(0x20000);
        cpu.set_program_counter(0x1000);
        cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
        cpu.set_pstate_ss(true);

        constexpr std::size_t kStepIters = 200000;
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < kStepIters; ++i) {
            cpu.step(mem);
        }
        const auto stop = std::chrono::steady_clock::now();
        const double secs = std::chrono::duration<double>(stop - start).count();
        const double tput = static_cast<double>(kStepIters) / secs;
        const double overhead = ((baseline_throughput - tput) / baseline_throughput) * 100.0;
        std::printf("%-45s: %8zu inst in %.4f s (%.0f inst/s, overhead: %5.1f%%)\n",
                    "7. Single-step + handler recovery", kStepIters, secs, tput, overhead);
    }

    // 8. Deterministic SMP throughput
    {
        winios::SMPCluster cluster(2, 0x40000);
        cluster.get_memory().write_u32(0x1000, enc_add(0, 0, 1));
        cluster.get_memory().write_u32(0x1004, enc_b(-1));
        cluster.initialize_core(0, 0x1000);
        cluster.start_secondary_core(1, 0x1000);

        const auto start = std::chrono::steady_clock::now();
        const std::size_t ret = cluster.run_deterministic(kIterations);
        const auto stop = std::chrono::steady_clock::now();
        const double secs = std::chrono::duration<double>(stop - start).count();
        std::printf("%-45s: %8zu inst in %.4f s (%.0f inst/s)\n",
                    "8. Deterministic SMP (2 cores)", ret, secs, static_cast<double>(ret) / secs);
    }

    // 9. Parallel SMP throughput
    {
        winios::SMPCluster cluster(2, 0x40000);
        cluster.get_memory().write_u32(0x1000, enc_add(0, 0, 1));
        cluster.get_memory().write_u32(0x1004, enc_b(-1));
        cluster.initialize_core(0, 0x1000);
        cluster.start_secondary_core(1, 0x1000);

        cluster.run_parallel(2000); // Warmup
        const auto start = std::chrono::steady_clock::now();
        const std::size_t ret = cluster.run_parallel(kIterations);
        const auto stop = std::chrono::steady_clock::now();
        const double secs = std::chrono::duration<double>(stop - start).count();
        std::printf("%-45s: %8zu inst in %.4f s (%.0f inst/s)\n",
                    "9. Parallel SMP (2 cores)", ret, secs, static_cast<double>(ret) / secs);
    }

    return 0;
}

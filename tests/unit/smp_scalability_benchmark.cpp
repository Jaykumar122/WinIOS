#ifdef NDEBUG
#undef NDEBUG
#endif

#include "cpu/cpu.h"
#include "cpu/smp_core_state.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "cpu/smp_exclusive_monitor.h"
#include "cpu/smp_interrupt_router.h"
#include "cpu/virtual_time.h"
#include "snapshot/smp_snapshot_manager.h"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

using namespace winios;
using namespace winios::snapshot;

namespace {

struct TopologyBenchmarkResult {
    std::uint32_t core_count{0};
    std::uint64_t total_instructions{0};
    double execution_ms{0.0};
    double mips{0.0};
    double selections_per_sec{0.0};
    double snapshot_capture_ms{0.0};
    double snapshot_restore_ms{0.0};
    std::uint64_t final_hash{0};
};

TopologyBenchmarkResult run_topology_benchmark(std::uint32_t core_count, std::uint64_t target_instructions_per_core, std::uint64_t quantum) {
    const std::uint64_t total_target = core_count * target_instructions_per_core;
    auto mem = std::make_shared<SmpSharedMemory>(0x40000);
    SmpScheduler sched(mem, core_count, quantum);

    std::vector<std::shared_ptr<CPU>> cpus;
    for (std::uint32_t c = 0; c < core_count; ++c) {
        auto cpu = std::make_shared<CPU>();
        const std::uint64_t pc = 0x2000 + c * 0x200;
        cpu->set_program_counter(pc);

        // Instruction block:
        // MOVZ X0, #c
        // ADD X0, X0, #1
        // ADD X0, X0, #2
        // NOP ...
        const std::uint32_t movz_op = 0xD2800000u | (c << 5);
        mem->raw_memory().write_u32(pc + 0, movz_op);
        mem->raw_memory().write_u32(pc + 4, 0x91000400u);
        mem->raw_memory().write_u32(pc + 8, 0x17FFFFFFu); // B .-4 (infinite loop of ADD X0, X0, #1)

        cpus.push_back(cpu);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    const auto start = std::chrono::high_resolution_clock::now();
    std::uint64_t slices = 0;
    while (sched.global_retirement_sequence() < total_target) {
        const auto rec = sched.step_quantum();
        slices++;
        if (rec.reason == QuantumTruncationReason::AllCoresQuiescent) {
            break;
        }
    }
    const auto end = std::chrono::high_resolution_clock::now();

    const double exec_ms = std::chrono::duration<double, std::milli>(end - start).count();
    const double mips = (sched.global_retirement_sequence() / (exec_ms / 1000.0)) / 1000000.0;
    const double selections_sec = (slices / (exec_ms / 1000.0));

    // Snapshot benchmarks
    std::vector<std::uint8_t> snap_buf;
    const auto cap_start = std::chrono::high_resolution_clock::now();
    const auto cap_res = SmpSnapshotManager::capture_to_buffer(sched, snap_buf);
    (void)cap_res;
    const auto cap_end = std::chrono::high_resolution_clock::now();
    const double cap_ms = std::chrono::duration<double, std::milli>(cap_end - cap_start).count();

    auto target_mem = std::make_shared<SmpSharedMemory>(0x40000);
    SmpScheduler target_sched(target_mem, core_count, quantum);
    for (std::uint32_t c = 0; c < core_count; ++c) {
        auto cpu = std::make_shared<CPU>();
        target_sched.register_core(c, cpu, CoreLifecycleState::Halted);
    }

    const auto rest_start = std::chrono::high_resolution_clock::now();
    const auto rest_res = SmpSnapshotManager::restore_from_buffer(snap_buf.data(), snap_buf.size(), target_sched);
    (void)rest_res;
    const auto rest_end = std::chrono::high_resolution_clock::now();
    const double rest_ms = std::chrono::duration<double, std::milli>(rest_end - rest_start).count();

    TopologyBenchmarkResult res;
    res.core_count = core_count;
    res.total_instructions = sched.global_retirement_sequence();
    res.execution_ms = exec_ms;
    res.mips = mips;
    res.selections_per_sec = selections_sec;
    res.snapshot_capture_ms = cap_ms;
    res.snapshot_restore_ms = rest_ms;
    res.final_hash = sched.compute_canonical_multicore_hash();

    return res;
}

} // namespace

int main() {
    std::cout << "===================================================================" << std::endl;
    std::cout << " WinIOS Phase 52 Stage 28: SMP Topology Scalability Benchmark     " << std::endl;
    std::cout << "===================================================================" << std::endl;

    std::vector<std::uint32_t> topologies = {1, 2, 4, 8};
    const std::uint64_t target_per_core = 10000;
    const std::uint64_t quantum = 500;

    std::cout << "\nRunning benchmark across topologies (1, 2, 4, 8 Cores)...\n" << std::endl;

    std::vector<TopologyBenchmarkResult> results;
    for (std::uint32_t cores : topologies) {
        std::cout << "  Benchmarking " << cores << "-Core configuration..." << std::flush;
        auto res = run_topology_benchmark(cores, target_per_core, quantum);
        results.push_back(res);
        std::cout << " Done (" << std::fixed << std::setprecision(2) << res.mips << " MIPS, "
                  << res.execution_ms << " ms)" << std::endl;
    }

    std::cout << "\n------------------------------------------------------------------------------------------------------" << std::endl;
    std::cout << " Core Count | Retired Insns | Execution (ms) | Speed (MIPS) | Selections/s | Snap Cap(ms) | Snap Rest(ms) " << std::endl;
    std::cout << "------------------------------------------------------------------------------------------------------" << std::endl;

    for (const auto& r : results) {
        std::cout << "     " << std::setw(6) << r.core_count << " |"
                  << "        " << std::setw(6) << r.total_instructions << " |"
                  << "         " << std::setw(6) << std::fixed << std::setprecision(2) << r.execution_ms << " |"
                  << "       " << std::setw(6) << std::fixed << std::setprecision(2) << r.mips << " |"
                  << "     " << std::setw(8) << std::fixed << std::setprecision(0) << r.selections_per_sec << " |"
                  << "       " << std::setw(6) << std::fixed << std::setprecision(2) << r.snapshot_capture_ms << " |"
                  << "       " << std::setw(6) << std::fixed << std::setprecision(2) << r.snapshot_restore_ms << " " << std::endl;
    }
    std::cout << "------------------------------------------------------------------------------------------------------" << std::endl;

    std::cout << "\n>>> SMP TOPOLOGY SCALABILITY BENCHMARK COMPLETE <<<" << std::endl;
    return 0;
}

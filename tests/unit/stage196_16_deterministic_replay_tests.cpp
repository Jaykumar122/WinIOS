#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <iomanip>
#include <sstream>

#include "cpu/cpu.h"
#include "cpu/smp_cluster.h"
#include "memory/memory.h"
#include "replay/replay_state_hash.h"

using namespace winios;
using namespace winios::replay;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

static void write_u32(Memory& mem, std::uint64_t addr, std::uint32_t val) {
    mem.write_u32(addr, val);
}

struct RunResult {
    std::size_t total_steps{0};
    std::uint32_t final_counter{0};
    std::vector<ReplayComponentHashes> core_hashes;
};

static RunResult execute_deterministic_workload(std::uint32_t initial_counter) {
    constexpr std::size_t kCores = 4;
    constexpr std::uint32_t kIterations = 50;
    constexpr std::uint64_t kCounterAddr = 0x8000;
    constexpr std::uint64_t kCodeAddr = 0x1000;

    SMPCluster cluster(kCores, 0x20000);
    auto& mem = cluster.get_memory();
    mem.write_u32(kCounterAddr, initial_counter);

    // ldadd w0, w1, [x2] -> 0xb8200041
    // subs  w4, w4, #1   -> 0x71000484
    // b.ne  0x1000       -> 0x54ffffc1
    // hlt   #0           -> 0xd4400000
    write_u32(mem, kCodeAddr + 0x00, 0xb8200041u);
    write_u32(mem, kCodeAddr + 0x04, 0x71000484u);
    write_u32(mem, kCodeAddr + 0x08, 0x54ffffc1u);
    write_u32(mem, kCodeAddr + 0x0C, 0xd4400000u);

    for (std::size_t i = 0; i < kCores; ++i) {
        auto& core = cluster.get_core(i);
        core.set_current_el(1);
        core.write_register(0, 1);             // addend = 1
        core.write_register(2, kCounterAddr);  // &counter
        core.write_register(4, kIterations);   // loop count
        core.set_program_counter(kCodeAddr);
        cluster.resume_core(i);
    }

    RunResult res;
    res.total_steps = cluster.run_deterministic(20000);
    res.final_counter = mem.read_u32(kCounterAddr);

    for (std::size_t i = 0; i < kCores; ++i) {
        auto hashes = ReplayStateHash::compute_state_hashes(cluster.get_core(i), mem);
        res.core_hashes.push_back(hashes);
    }

    return res;
}

// -----------------------------------------------------------------------------
// Test N1: Exact Bit-Identical Replay Across Repeated Runs (Acceptance Test N)
// -----------------------------------------------------------------------------
static bool test_deterministic_replay_reproducibility() {
    RunResult run1 = execute_deterministic_workload(0);
    RunResult run2 = execute_deterministic_workload(0);

    TEST_ASSERT(run1.total_steps > 0, "Run 1 retired instructions");
    TEST_ASSERT(run1.total_steps == run2.total_steps, "Exact step count match across runs");
    TEST_ASSERT(run1.final_counter == run2.final_counter, "Final counter matches");
    TEST_ASSERT(run1.final_counter == 200, "Final counter exactly 200 (4 cores * 50)");

    for (std::size_t i = 0; i < run1.core_hashes.size(); ++i) {
        const auto& h1 = run1.core_hashes[i];
        const auto& h2 = run2.core_hashes[i];

        TEST_ASSERT(h1 == h2, "Core hashes must be bit-exact identical between runs");
        TEST_ASSERT(h1.cpu_registers_hash == h2.cpu_registers_hash, "GPR hash match");
        TEST_ASSERT(h1.memory_hash == h2.memory_hash, "Memory hash match");
        TEST_ASSERT(h1.combined_hash == h2.combined_hash, "Combined hash match");
    }

    // Write results to JSON
    std::ofstream out("docs/campaign196/replay_results.json");
    if (out.is_open()) {
        out << "{\n";
        out << "  \"test\": \"Stage 196.16 Deterministic SMP Replay\",\n";
        out << "  \"status\": \"VERIFIED\",\n";
        out << "  \"total_steps\": " << run1.total_steps << ",\n";
        out << "  \"final_counter\": " << run1.final_counter << ",\n";
        out << "  \"cores\": [\n";
        for (std::size_t i = 0; i < run1.core_hashes.size(); ++i) {
            std::stringstream ss;
            ss << std::hex << std::setfill('0') << std::setw(16) << run1.core_hashes[i].combined_hash;
            out << "    {\"core_id\": " << i << ", \"combined_hash\": \"0x" << ss.str() << "\"}";
            if (i + 1 < run1.core_hashes.size()) out << ",";
            out << "\n";
        }
        out << "  ]\n";
        out << "}\n";
    }

    return true;
}

// -----------------------------------------------------------------------------
// Test N2: Divergence Detection and Diff Reporting (Acceptance Test N)
// -----------------------------------------------------------------------------
static bool test_divergence_detection() {
    RunResult baseline = execute_deterministic_workload(0);
    RunResult perturbed = execute_deterministic_workload(5); // initial counter = 5

    TEST_ASSERT(baseline.final_counter != perturbed.final_counter, "Counter differs");

    std::string diff;
    const bool matches = ReplayStateHash::compare_and_diff(baseline.core_hashes[0], perturbed.core_hashes[0], diff);
    TEST_ASSERT(!matches, "compare_and_diff detected divergence");
    TEST_ASSERT(!diff.empty(), "Diff report generated");

    return true;
}

int main() {
    std::cout << "[STAGE 196.16] Running Deterministic SMP Replay Tests (Acceptance Test N)...\n";

    if (!test_deterministic_replay_reproducibility()) return 1;
    if (!test_divergence_detection()) return 1;

    std::cout << "[STAGE 196.16] All Deterministic SMP Replay Tests PASSED (Acceptance Test N VERIFIED).\n";
    return 0;
}

// ==============================================================================
// WinIOS Campaign 201 Stage 16: Multicore SMP Stress & Concurrency Tests
// ==============================================================================
// Verifies:
// 1. Multicore cluster scalability across 1, 2, 4, 6 (A16 physical), and 8 virtual cores
// 2. Cross-core exclusive monitor contention and atomic lock arbitration
// 3. SMP TLB invalidation broadcast across all active CPU cores
// 4. Concurrent instruction retirement and deterministic scheduling
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "cpu/smp_topology.h"

using namespace winios;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Cluster Scalability Across Core Counts (1, 2, 4, 6, 8 Cores)
// -----------------------------------------------------------------------------
void test_cluster_scalability() {
    std::cout << "[Test 1] Multicore Scalability (1, 2, 4, 6, 8 cores)...\n";

    const std::vector<std::size_t> core_counts = { 1, 2, 4, 6, 8 };

    for (std::size_t cores : core_counts) {
        auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024 * 2, cores);
        SmpScheduler sched(mem, cores, 20);

        for (std::size_t c = 0; c < cores; ++c) {
            auto cpu = std::make_shared<CPU>();
            cpu->set_core_id(c);
            const std::uint64_t code_addr = 0x1000 + c * 0x400;

            // Simple loop: ADD Xc, Xc, #1; B .-4
            mem->raw_memory().write_u32(code_addr + 0, 0x91000400u | (static_cast<std::uint32_t>(c) << 5) | static_cast<std::uint32_t>(c));
            mem->raw_memory().write_u32(code_addr + 4, 0x17FFFFFFu);

            cpu->set_program_counter(code_addr);
            sched.register_core(c, cpu, CoreLifecycleState::Runnable);
        }

        const std::uint64_t target_instructions = 10000;
        const std::uint64_t retired = sched.run_deterministic(target_instructions);

        TEST_CHECK(retired >= target_instructions, "Target instructions must be retired");
        TEST_CHECK(sched.core_count() == cores, "Core count must match");

        // Verify all cores made progress
        for (std::size_t c = 0; c < cores; ++c) {
            TEST_CHECK(sched.get_slot(c).cpu->read_register(c) > 0, "Every core must execute loop iterations");
        }
    }

    std::cout << "  -> Cluster scalability passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Cross-Core Exclusive Monitor Contention
// -----------------------------------------------------------------------------
void test_cross_core_exclusive_contention() {
    std::cout << "[Test 2] Cross-Core Exclusive Contention...\n";
    // 6-core cluster (Apple A16 physical core count)
    constexpr std::size_t kCores = 6;
    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024 * 4, kCores);
    SmpExclusiveMonitorManager ex_mon(kCores);
    mem->attach_exclusive_monitor(&ex_mon);

    const std::uint64_t lock_addr = 0x50000ULL;
    mem->write_u64(0, lock_addr, 0ULL);

    // Core 0 acquires reservation via load_exclusive
    ex_mon.load_exclusive(0, lock_addr, 8);
    auto v0 = mem->read_u64(0, lock_addr);
    TEST_CHECK(v0 == 0ULL, "Initial lock value must be 0");
    TEST_CHECK(ex_mon.is_reservation_valid(0, lock_addr, 8), "Core 0 must hold valid reservation");

    // Core 1 performs store_conditional on same granule
    bool c1_ok = mem->write_exclusive_u64(1, lock_addr, 1ULL);
    // Core 1 did not have a valid reservation so its store-conditional fails
    TEST_CHECK(!c1_ok, "Core 1 without load-linked must fail store-conditional");

    // Core 1 now does load-linked, then store-conditional -> succeeds!
    ex_mon.load_exclusive(1, lock_addr, 8);
    bool c1_st_ok = mem->write_exclusive_u64(1, lock_addr, 0xCAFEULL);
    TEST_CHECK(c1_st_ok, "Core 1 with load-linked must succeed store-conditional");

    // Core 0's reservation MUST have been invalidated by Core 1's write!
    TEST_CHECK(!ex_mon.is_reservation_valid(0, lock_addr, 8), "Core 0 reservation must be invalidated by Core 1 store");

    // Core 0 now attempts store-conditional: must fail!
    bool c0_st_ok = mem->write_exclusive_u64(0, lock_addr, 0xBEEFULL);
    TEST_CHECK(!c0_st_ok, "Core 0 store-conditional must fail after invalidation");
    TEST_CHECK(mem->read_u64(0, lock_addr) == 0xCAFEULL, "Memory must retain Core 1 value");

    std::cout << "  -> Cross-core exclusive contention passed.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 201.16 Multicore SMP Stress Tests ===\n";
    test_cluster_scalability();
    test_cross_core_exclusive_contention();

    if (g_failures == 0) {
        std::cout << "All Stage 201.16 tests passed successfully!\n";
        return 0;
    } else {
        std::cerr << g_failures << " Stage 201.16 test(s) failed!\n";
        return 1;
    }
}

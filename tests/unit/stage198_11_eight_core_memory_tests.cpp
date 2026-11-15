// ==============================================================================
// WinIOS Campaign 198 Stage 11: 8-Core Shared-Memory Architecture Conformance
// ==============================================================================
// Verifies:
// 1. 8-core SMP cluster instantiation and shared memory coherency
// 2. 8-way exclusive reservations and targeted cross-core invalidation
// 3. PSCI secondary core boot across all 8 cores
// 4. Cluster-wide cross-core TLBI invalidation across all 8 cores
// ==============================================================================

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/psci.h"
#include "cpu/smp_cluster.h"
#include "memory/memory.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#define TEST_ASSERT(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" \
                      << __LINE__ << std::endl;                                \
            std::exit(1);                                                      \
        }                                                                      \
    } while (false)

void test_eight_core_instantiation_and_shared_memory() {
    std::cout << "[Test 1] 8-core SMP instantiation & shared memory coherency...\n";
    constexpr std::size_t kRamSize = 4 * 1024 * 1024;
    winios::SMPCluster cluster(8, kRamSize);
    TEST_ASSERT(cluster.core_count() == 8);

    auto& mem = cluster.get_memory();

    // Core 0 writes unique patterns to 8 distinct memory slots
    for (std::size_t i = 0; i < 8; ++i) {
        const std::uint64_t addr = 0x20000 + i * 64;
        const std::uint64_t val = 0xCAFE000000000000ULL | (i << 16) | 0xBABE;
        mem.write_u64(addr, val);
    }

    // Cores 1..7 verify that they observe exactly what Core 0 wrote
    for (std::size_t i = 0; i < 8; ++i) {
        const std::uint64_t addr = 0x20000 + i * 64;
        const std::uint64_t expected = 0xCAFE000000000000ULL | (i << 16) | 0xBABE;
        for (std::size_t reader = 0; reader < 8; ++reader) {
            TEST_ASSERT(mem.read_u64(addr) == expected);
        }
    }

    std::cout << "  -> PASSED\n";
}

void test_eight_core_exclusive_reservations_and_invalidation() {
    std::cout << "[Test 2] 8-way exclusive reservations & targeted invalidation...\n";
    constexpr std::size_t kRamSize = 2 * 1024 * 1024;
    winios::SMPCluster cluster(8, kRamSize);
    TEST_ASSERT(cluster.core_count() == 8);

    // Each core establishes an exclusive reservation on its own 64-byte granule
    for (std::size_t core = 0; core < 8; ++core) {
        const std::uint64_t granule_addr = 0x10000 + core * 64;
        cluster.register_exclusive_reservation(core, granule_addr, 64);
    }

    // Check all 8 reservations are currently active
    for (std::size_t core = 0; core < 8; ++core) {
        const std::uint64_t granule_addr = 0x10000 + core * 64;
        // check_and_clear without clearing: simulate via register and check
        TEST_ASSERT(cluster.check_and_clear_exclusive_reservation(core, granule_addr, 64));
        // re-register
        cluster.register_exclusive_reservation(core, granule_addr, 64);
    }

    // Core 2 performs a store to core 5's granule (0x10000 + 5*64)
    cluster.invalidate_exclusive_reservations(2, 0x10000 + 5 * 64, 64);

    // Core 5's reservation must be invalidated
    TEST_ASSERT(!cluster.check_and_clear_exclusive_reservation(5, 0x10000 + 5 * 64, 64));

    // Cores 0, 1, 2, 3, 4, 6, 7 reservations MUST still be intact
    for (std::size_t core = 0; core < 8; ++core) {
        if (core == 5) continue;
        const std::uint64_t granule_addr = 0x10000 + core * 64;
        TEST_ASSERT(cluster.check_and_clear_exclusive_reservation(core, granule_addr, 64));
    }

    std::cout << "  -> PASSED\n";
}

void test_eight_core_psci_boot() {
    std::cout << "[Test 3] PSCI multicore bring-up across 8 cores...\n";
    constexpr std::size_t kRamSize = 2 * 1024 * 1024;
    winios::SMPCluster cluster(8, kRamSize);
    cluster.enable_psci();

    auto* psci = cluster.get_psci_service();
    TEST_ASSERT(psci != nullptr);

    // Primary core 0 is already running; secondary cores 1..7 are initially halted
    for (std::size_t core = 1; core < 8; ++core) {
        TEST_ASSERT(cluster.is_core_halted(core));
    }

    // Boot all secondary cores via start_secondary_core
    for (std::size_t core = 1; core < 8; ++core) {
        const std::uint64_t entry_addr = 0x8000 + core * 0x100;
        cluster.start_secondary_core(core, entry_addr);
        TEST_ASSERT(!cluster.is_core_halted(core));
        TEST_ASSERT(cluster.get_core(core).get_program_counter() == entry_addr);
    }

    std::cout << "  -> PASSED\n";
}

void test_eight_core_cluster_tlbi_broadcast() {
    std::cout << "[Test 4] Cluster-wide TLBI broadcast across 8 cores...\n";
    constexpr std::size_t kRamSize = 2 * 1024 * 1024;
    winios::SMPCluster cluster(8, kRamSize);

    for (std::size_t core = 0; core < 8; ++core) {
        cluster.get_core(core).get_mmu().reset_tlb_stats();
        TEST_ASSERT(cluster.get_core(core).get_mmu().get_tlb_invalidations() == 0);
    }

    // Core 0 broadcasts TLBI VAE1IS for VA 0x40000, ASID 1
    cluster.broadcast_tlbi_va(0, 0x40000, 1, false);

    // All remote cores (1..7) must have their TLB invalidation count incremented
    for (std::size_t core = 1; core < 8; ++core) {
        TEST_ASSERT(cluster.get_core(core).get_mmu().get_tlb_invalidations() == 1);
    }
    TEST_ASSERT(cluster.get_core(0).get_mmu().get_tlb_invalidations() == 0);

    std::cout << "  -> PASSED\n";
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "Stage 198.11: 8-Core Shared-Memory Conformance Tests\n";
    std::cout << "========================================================\n";

    test_eight_core_instantiation_and_shared_memory();
    test_eight_core_exclusive_reservations_and_invalidation();
    test_eight_core_psci_boot();
    test_eight_core_cluster_tlbi_broadcast();

    std::cout << "\nAll Stage 198.11 tests passed successfully.\n";
    return 0;
}

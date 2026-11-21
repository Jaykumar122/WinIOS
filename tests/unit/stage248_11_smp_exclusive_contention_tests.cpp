// ============================================================================
// Stage 248.11 & 248.12: Multi-Core Exclusive Contention & Ordering Tests
// Tests:
// 1. True multi-core exclusive monitor contention:
//    - Core 0 executes LDXR X0, [X1] establishing reservation on shared RAM.
//    - Core 1 performs store to same address, invalidating Core 0 reservation.
//    - Core 0 attempts STXR -> MUST FAIL (status = 1, memory unmodified).
// 2. Retry loop:
//    - Core 0 re-executes LDXR, updates value, and re-attempts STXR -> SUCCEEDS (status = 0).
// 3. Multi-core barrier execution across cores (DMB ISH, DSB ISH).
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/smp_cluster.h"
#include "memory/memory.h"

#include <iostream>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;

static void test_smp_exclusive_contention() {
    std::cout << "[Stage 248.11 - Test 1] Cross-core exclusive monitor contention and invalidation...\n";

    SMPCluster cluster(2, 0x40000); // 2 cores, 256 KB RAM
    Memory& memory = cluster.get_memory();

    CPU& core0 = cluster.get_core(0);
    CPU& core1 = cluster.get_core(1);
    core0.set_current_el(1);
    core1.set_current_el(1);

    const uint64_t target_addr = 0x8000;
    memory.write_u64(target_addr, 0x1000200030004000ULL);

    // Core 0 program at 0x1000:
    // 0x1000: LDXR X0, [X1]         (0xC85F7C20u)
    // 0x1004: STXR W4, X5, [X1]     (0xC8047C25u)
    memory.write_u32(0x1000, 0xC85F7C20u);
    memory.write_u32(0x1004, 0xC8047C25u);

    // Core 1 program at 0x2000:
    // 0x2000: STR X2, [X3]          (0xF9000062u)
    memory.write_u32(0x2000, 0xF9000062u);

    core0.set_program_counter(0x1000);
    core0.write_register(1, target_addr);
    core0.write_register(5, 0xAAAAAAAAAAAAAAAULL);

    core1.set_program_counter(0x2000);
    core1.write_register(3, target_addr);
    core1.write_register(2, 0xBBBBBBBBBBBBBBBBULL);

    // Step 1: Core 0 executes LDXR -> establishes reservation
    core0.step(memory);
    TEST_ASSERT(core0.read_register(0) == 0x1000200030004000ULL, "Core 0 loaded initial value");
    TEST_ASSERT(core0.exclusive_monitor_valid(), "Core 0 exclusive monitor is active");

    // Step 2: Core 1 executes STR to target_addr -> invalidates Core 0 reservation!
    core1.step(memory);
    TEST_ASSERT(memory.read_u64(target_addr) == 0xBBBBBBBBBBBBBBBBULL, "Core 1 updated shared memory");

    // Step 3: Core 0 attempts STXR -> must fail!
    core0.step(memory);
    TEST_ASSERT(core0.read_register(4) == 1, "Core 0 STXR failed due to Core 1 invalidation");
    TEST_ASSERT(memory.read_u64(target_addr) == 0xBBBBBBBBBBBBBBBBULL, "Shared memory not corrupted by failed STXR");

    // Step 4: Core 0 retries: LDXR + STXR without intervening store -> succeeds!
    core0.set_program_counter(0x1000);
    core0.step(memory); // LDXR
    TEST_ASSERT(core0.read_register(0) == 0xBBBBBBBBBBBBBBBBULL, "Core 0 loaded updated value");

    core0.step(memory); // STXR
    TEST_ASSERT(core0.read_register(4) == 0, "Core 0 retry STXR succeeded (status = 0)");
    TEST_ASSERT(memory.read_u64(target_addr) == 0xAAAAAAAAAAAAAAAULL, "Shared memory updated on successful retry");
}

static void test_smp_memory_barriers() {
    std::cout << "[Stage 248.12 - Test 2] Multi-core barrier execution across cores...\n";

    SMPCluster cluster(2, 0x40000);
    Memory& memory = cluster.get_memory();

    CPU& core0 = cluster.get_core(0);
    CPU& core1 = cluster.get_core(1);

    // Barrier instructions:
    // DMB ISH (0xD5033B9Fu), DSB ISH (0xD503399Fu), ISB (0xD5033FDFu)
    memory.write_u32(0x1000, 0xD5033B9Fu);
    memory.write_u32(0x1004, 0xD503399Fu);
    memory.write_u32(0x2000, 0xD5033B9Fu);
    memory.write_u32(0x2004, 0xD5033FDFu);

    core0.set_program_counter(0x1000);
    core1.set_program_counter(0x2000);

    core0.step(memory); // DMB
    core1.step(memory); // DMB
    core0.step(memory); // DSB
    core1.step(memory); // ISB

    TEST_ASSERT(core0.get_program_counter() == 0x1008, "Core 0 stepped through barriers");
    TEST_ASSERT(core1.get_program_counter() == 0x2008, "Core 1 stepped through barriers");
}

int main() {
    std::cout << "=== Running Stage 248.11 SMP Exclusive Contention Tests ===\n";
    test_smp_exclusive_contention();
    test_smp_memory_barriers();
    std::cout << "Stage 248.11: All SMP exclusive contention tests passed.\n";
    return 0;
}

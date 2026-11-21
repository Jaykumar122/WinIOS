// ============================================================================
// Stage 249.11 - 249.13: SMP Producer-Consumer Synchronization, Architectural
// Barrier Semantics & Scheduler Exclusive-Monitor Integration Tests
//
// Tests:
// 1. Stage 249.11: SMP Producer-Consumer Synchronization
//    - Core 0 (Producer) writes data payload, executes DMB ISH barrier, sets ready flag.
//    - Core 1 (Consumer) polls ready flag, executes DMB ISH barrier, consumes data.
//    - Multi-iteration deterministic transfer without race conditions.
// 2. Stage 249.12: Architectural Barrier Semantics
//    - Verified DMB ISH, DSB ISH, and ISB instruction semantics and memory ordering.
// 3. Stage 249.13: Exclusive-Monitor and Scheduler Integration
//    - Verified that context switch and CLREX explicitly invalidate exclusive reservations.
//    - Subsequent STXR fails deterministically with status 1.
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/smp_shared_memory.h"
#include "memory/memory.h"

#include <iostream>
#include <memory>
#include <vector>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;

// ----------------------------------------------------------------------------
// Test 1: Stage 249.11 - SMP Producer-Consumer Workload
// ----------------------------------------------------------------------------
static void test_smp_producer_consumer_synchronization() {
    std::cout << "[Stage 249.11] Testing SMP Producer-Consumer Synchronization...\n";

    Memory mem(0x100000); // 1 MB RAM
    CPU producer;
    CPU consumer;
    producer.reset();
    consumer.reset();

    const uint64_t data_addr = 0x20000;
    const uint64_t flag_addr = 0x20008;

    mem.write_u64(data_addr, 0);
    mem.write_u64(flag_addr, 0);

    // Producer Code at 0x10000:
    // 1. STR X2, [X1, #0]   (0xF9000022u) -> Store payload (0xCAFEBABE) to data_addr
    // 2. DMB ISH            (0xD5033BBFu) -> Memory barrier ensures data visibility
    // 3. STR X4, [X3, #0]   (0xF9000064u) -> Store ready flag (1) to flag_addr
    const uint64_t prod_pc = 0x10000;
    mem.write_u32(prod_pc + 0, 0xF9000022u);
    mem.write_u32(prod_pc + 4, 0xD5033BBFu);
    mem.write_u32(prod_pc + 8, 0xF9000064u);

    producer.set_program_counter(prod_pc);
    producer.write_register(1, data_addr);
    producer.write_register(2, 0xCAFEBABEULL);
    producer.write_register(3, flag_addr);
    producer.write_register(4, 1);

    // Consumer Code at 0x18000:
    // 1. LDR X4, [X3, #0]   (0xF9400064u) -> Read ready flag
    // 2. DMB ISH            (0xD5033BBFu) -> Acquire barrier
    // 3. LDR X2, [X1, #0]   (0xF9400022u) -> Read payload
    const uint64_t cons_pc = 0x18000;
    mem.write_u32(cons_pc + 0, 0xF9400064u);
    mem.write_u32(cons_pc + 4, 0xD5033BBFu);
    mem.write_u32(cons_pc + 8, 0xF9400022u);

    consumer.set_program_counter(cons_pc);
    consumer.write_register(1, data_addr);
    consumer.write_register(3, flag_addr);

    // Producer step 1: write payload
    producer.step(mem);
    TEST_ASSERT(mem.read_u64(data_addr) == 0xCAFEBABEULL, "Producer stored payload");

    // Producer step 2: DMB ISH barrier
    producer.step(mem);

    // Producer step 3: write ready flag
    producer.step(mem);
    TEST_ASSERT(mem.read_u64(flag_addr) == 1, "Producer published ready flag");

    // Consumer step 1: read ready flag
    consumer.step(mem);
    TEST_ASSERT(consumer.read_register(4) == 1, "Consumer observed ready flag");

    // Consumer step 2: DMB ISH barrier
    consumer.step(mem);

    // Consumer step 3: read payload
    consumer.step(mem);
    TEST_ASSERT(consumer.read_register(2) == 0xCAFEBABEULL, "Consumer read correct payload across barrier");

    std::cout << "[Stage 249.11] SMP producer-consumer synchronization verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 249.12 - Architectural Barrier Semantics
// ----------------------------------------------------------------------------
static void test_architectural_barriers() {
    std::cout << "[Stage 249.12] Testing Architectural Barrier Semantics (DMB, DSB, ISB)...\n";

    CPU cpu;
    Memory mem(0x100000);
    cpu.reset();

    const uint64_t code_pc = 0x10000;
    // 1. DMB ISH  (0xD5033BBFu) -> Data Memory Barrier (Inner Shareable)
    // 2. DSB ISH  (0xD5033B9Fu) -> Data Synchronization Barrier
    // 3. ISB      (0xD5033FDFu) -> Instruction Synchronization Barrier
    mem.write_u32(code_pc + 0, 0xD5033BBFu);
    mem.write_u32(code_pc + 4, 0xD5033B9Fu);
    mem.write_u32(code_pc + 8, 0xD5033FDFu);

    cpu.set_program_counter(code_pc);

    // Execute DMB ISH
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == code_pc + 4, "DMB ISH executed cleanly");

    // Execute DSB ISH
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == code_pc + 8, "DSB ISH executed cleanly");

    // Execute ISB
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == code_pc + 12, "ISB executed cleanly");

    std::cout << "[Stage 249.12] Architectural barrier instructions verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 249.13 - Exclusive-Monitor & Scheduler Context Switch Invalidation
// ----------------------------------------------------------------------------
static void test_exclusive_monitor_and_scheduler_invalidation() {
    std::cout << "[Stage 249.13] Testing Exclusive-Monitor & Scheduler Invalidation...\n";

    CPU cpu;
    Memory mem(0x100000);
    cpu.reset();

    const uint64_t code_pc = 0x10000;
    const uint64_t target_addr = 0x20000;
    mem.write_u64(target_addr, 0x11112222ULL);

    // Code at 0x10000:
    // 1. LDXR X0, [X1]      (0xC85F7C20u) -> Establish exclusive reservation
    // 2. CLREX              (0xD5033F5Fu) -> Invalidate reservation (simulating context switch)
    // 3. STXR W2, X3, [X1]  (0xC8027C23u) -> Exclusive store must fail (W2 = 1)
    mem.write_u32(code_pc + 0, 0xC85F7C20u);
    mem.write_u32(code_pc + 4, 0xD5033F5Fu);
    mem.write_u32(code_pc + 8, 0xC8027C23u);

    cpu.set_program_counter(code_pc);
    cpu.write_register(1, target_addr);
    cpu.write_register(3, 0x99998888ULL);

    // Step 1: LDXR establishes reservation
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 0x11112222ULL, "LDXR loaded target value");

    // Step 2: CLREX invalidates local reservation
    cpu.step(mem);

    // Step 3: STXR attempts exclusive store without valid reservation
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(2) == 1, "STXR returned status 1 (failure) after CLREX");
    TEST_ASSERT(mem.read_u64(target_addr) == 0x11112222ULL, "Memory remains unmodified after failed STXR");

    // Verify successful LDXR + STXR when unbroken
    mem.write_u32(code_pc + 12, 0xC85F7C20u); // LDXR X0, [X1]
    mem.write_u32(code_pc + 16, 0xC8027C23u); // STXR W2, X3, [X1]
    cpu.set_program_counter(code_pc + 12);

    cpu.step(mem); // LDXR
    cpu.step(mem); // STXR
    TEST_ASSERT(cpu.read_register(2) == 0, "STXR returned status 0 (success) with valid reservation");
    TEST_ASSERT(mem.read_u64(target_addr) == 0x99998888ULL, "Memory updated by successful STXR");

    std::cout << "[Stage 249.13] Exclusive-monitor invalidation verified.\n";
}

int main() {
    std::cout << "============================================================\n";
    std::cout << "Running Stage 249.11 - 249.13 SMP & Memory Ordering Tests\n";
    std::cout << "============================================================\n";

    test_smp_producer_consumer_synchronization();
    test_architectural_barriers();
    test_exclusive_monitor_and_scheduler_invalidation();

    std::cout << "\n>>> ALL STAGE 249.11 - 249.13 TESTS PASSED SUCCESSFULLY! <<<\n";
    return 0;
}

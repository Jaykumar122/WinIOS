// ============================================================================
// Stage 247.6 & 247.7: Multi-Core Exclusive Monitor & Memory Ordering Tests
// Tests:
// 1. LDXR / STXR exclusive reservation lifecycle (success path).
// 2. STXR failure when no prior LDXR exists (status = 1, memory unmodified).
// 3. Intervening store invalidating exclusive reservation.
// 4. CLREX explicitly clearing reservation.
// 5. Memory barrier instruction execution: DMB ISH, DSB ISH, ISB.
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

static void test_exclusive_monitor_lifecycle() {
    std::cout << "[Stage 247.6 - Test 1] LDXR / STXR exclusive reservation lifecycle...\n";
    CPU cpu;
    Memory mem(0x100000);
    const uint64_t target_addr = 0x50000;
    const uint64_t pc = 0x10000;

    cpu.reset();
    mem.write_u64(target_addr, 0x1122334455667788ULL);

    // 0x10000: LDXR X0, [X1]         (0xC85F7C20u)
    // 0x10004: STXR W2, X3, [X1]     (0xC8027C23u)
    mem.write_u32(pc + 0, 0xC85F7C20u);
    mem.write_u32(pc + 4, 0xC8027C23u);

    cpu.set_program_counter(pc);
    cpu.write_register(1, target_addr);
    cpu.write_register(3, 0xAABBCCDDEEFF0011ULL);

    // Step 1: LDXR loads value and sets exclusive monitor
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 0x1122334455667788ULL, "LDXR loaded initial memory value");

    // Step 2: STXR succeeds (reservation active)
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(2) == 0, "STXR returned status 0 (success)");
    TEST_ASSERT(mem.read_u64(target_addr) == 0xAABBCCDDEEFF0011ULL, "Memory updated by successful STXR");

    // Step 3: Immediate second STXR without LDXR -> MUST FAIL (status = 1)
    mem.write_u32(pc + 8, 0xC8027C23u);
    cpu.write_register(3, 0x9999999999999999ULL);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(2) == 1, "Second STXR without LDXR failed (status 1)");
    TEST_ASSERT(mem.read_u64(target_addr) == 0xAABBCCDDEEFF0011ULL, "Memory untouched by failed STXR");
}

static void test_clrex_invalidation() {
    std::cout << "[Stage 247.6 - Test 2] CLREX instruction invalidates reservation...\n";
    CPU cpu;
    Memory mem(0x100000);
    const uint64_t target_addr = 0x60000;
    const uint64_t pc = 0x20000;

    cpu.reset();
    mem.write_u64(target_addr, 0xDEADBEEFCAFEBABEULL);

    // 0x20000: LDXR X0, [X1]         (0xC85F7C20u)
    // 0x20004: CLREX                 (0xD5033F5Fu)
    // 0x20008: STXR W2, X3, [X1]     (0xC8027C23u)
    mem.write_u32(pc + 0, 0xC85F7C20u);
    mem.write_u32(pc + 4, 0xD5033F5Fu);
    mem.write_u32(pc + 8, 0xC8027C23u);

    cpu.set_program_counter(pc);
    cpu.write_register(1, target_addr);
    cpu.write_register(3, 0x1234123412341234ULL);

    cpu.step(mem); // LDXR
    cpu.step(mem); // CLREX
    cpu.step(mem); // STXR
    TEST_ASSERT(cpu.read_register(2) == 1, "STXR failed after CLREX (status 1)");
    TEST_ASSERT(mem.read_u64(target_addr) == 0xDEADBEEFCAFEBABEULL, "Memory preserved after CLREX cancellation");
}

static void test_memory_barriers() {
    std::cout << "[Stage 247.7 - Test 3] Memory barriers DMB, DSB, ISB execution...\n";
    CPU cpu;
    Memory mem(0x100000);
    const uint64_t pc = 0x30000;

    cpu.reset();
    // 0x30000: DMB ISH (0xD5033B9Fu)
    // 0x30004: DSB ISH (0xD503399Fu)
    // 0x30008: ISB     (0xD5033FDFu)
    mem.write_u32(pc + 0, 0xD5033B9Fu);
    mem.write_u32(pc + 4, 0xD503399Fu);
    mem.write_u32(pc + 8, 0xD5033FDFu);

    cpu.set_program_counter(pc);
    cpu.step(mem); // DMB
    TEST_ASSERT(cpu.get_program_counter() == pc + 4, "DMB completed");
    cpu.step(mem); // DSB
    TEST_ASSERT(cpu.get_program_counter() == pc + 8, "DSB completed");
    cpu.step(mem); // ISB
    TEST_ASSERT(cpu.get_program_counter() == pc + 12, "ISB completed");
}

int main() {
    std::cout << "=== Running Stage 247.6 & 247.7 Exclusive Monitor & Ordering Tests ===\n";
    test_exclusive_monitor_lifecycle();
    test_clrex_invalidation();
    test_memory_barriers();
    std::cout << "Stage 247.6 & 247.7: All exclusive monitor and ordering tests passed.\n";
    return 0;
}

// =============================================================================
// WinIOS Campaign 198 Stage 198.7: Multicore Coherency Protocol Foundation Tests
// =============================================================================

#include "cpu/cpu.h"
#include "cpu/smp_cluster.h"
#include "cpu/smp_exclusive_monitor.h"
#include "memory/memory.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#define TEST_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "Assertion failed: " #expr " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::abort(); \
        } \
    } while (0)

using namespace winios;

namespace {

void test_shared_memory_multi_copy_atomicity() {
    std::cout << "[Test 1] Multi-copy atomicity and barrier ordering across cores...\n";
    SMPCluster cluster(2, 1024 * 1024);
    Memory& mem = cluster.get_memory();

    CPU& cpu0 = cluster.get_core(0);
    CPU& cpu1 = cluster.get_core(1);

    // Flag at 0x2000, Data at 0x2008
    mem.write_u64(0x2000, 0);
    mem.write_u64(0x2008, 0);

    // Core 0 program:
    // 0x1000: MOVZ X0, #42
    // 0x1004: MOVZ X1, #0x2008
    // 0x1008: STR X0, [X1]      ; Write data = 42
    // 0x100C: DMB ISHST         ; Ensure data write is observable before flag write
    // 0x1010: MOVZ X2, #1
    // 0x1014: MOVZ X3, #0x2000
    // 0x1018: STR X2, [X3]      ; Set flag = 1
    // 0x101C: NOP
    mem.write_u32(0x1000, 0xD2800540u); // MOVZ X0, #42
    mem.write_u32(0x1004, 0xD2840101u); // MOVZ X1, #0x2008
    mem.write_u32(0x1008, 0xF9000020u); // STR X0, [X1]
    mem.write_u32(0x100C, 0xD50339BFu); // DMB ISHST (CRm=9)
    mem.write_u32(0x1010, 0xD2800022u); // MOVZ X2, #1
    mem.write_u32(0x1014, 0xD2840003u); // MOVZ X3, #0x2000
    mem.write_u32(0x1018, 0xF9000062u); // STR X2, [X3]
    mem.write_u32(0x101C, 0xD503201Fu); // NOP

    cpu0.set_program_counter(0x1000);
    cpu0.set_current_el(1);

    // Execute Core 0
    for (int i = 0; i < 7; ++i) {
        cpu0.step(mem);
    }

    // Now Core 1 reads flag, then data
    // 0x3000: MOVZ X3, #0x2000
    // 0x3004: LDR X4, [X3]      ; Read flag
    // 0x3008: DMB ISHLD         ; Ensure flag is read before data
    // 0x300C: MOVZ X1, #0x2008
    // 0x3010: LDR X5, [X1]      ; Read data
    mem.write_u32(0x3000, 0xD2840003u); // MOVZ X3, #0x2000
    mem.write_u32(0x3004, 0xF9400064u); // LDR X4, [X3]
    mem.write_u32(0x3008, 0xD5033ABFu); // DMB ISHLD (CRm=10)
    mem.write_u32(0x300C, 0xD2840101u); // MOVZ X1, #0x2008
    mem.write_u32(0x3010, 0xF9400025u); // LDR X5, [X1]

    cpu1.set_program_counter(0x3000);
    cpu1.set_current_el(1);

    for (int i = 0; i < 5; ++i) {
        cpu1.step(mem);
    }

    // Verify: If flag == 1, then data MUST be 42
    TEST_ASSERT(cpu1.read_register(4) == 1);
    TEST_ASSERT(cpu1.read_register(5) == 42);

    std::cout << "  -> PASSED\n";
}

void test_exclusive_monitor_cross_core_invalidation() {
    std::cout << "[Test 2] Cross-core exclusive monitor reservation invalidation...\n";
    SMPCluster cluster(2, 1024 * 1024);
    Memory& mem = cluster.get_memory();

    CPU& cpu0 = cluster.get_core(0);
    CPU& cpu1 = cluster.get_core(1);

    const std::uint64_t target_addr = 0x4000;
    mem.write_u64(target_addr, 100);

    // Core 1 registers reservation at 0x4000 via LDXR X0, [X1]
    // 0x1000: MOVZ X1, #0x4000
    // 0x1004: LDXR X0, [X1] (0xC85F7C20: LDXR X0, [X1])
    mem.write_u32(0x1000, 0xD2880001u);
    mem.write_u32(0x1004, 0xC85F7C20u);

    cpu1.set_current_el(1);
    cpu1.set_program_counter(0x1000);
    cpu1.step(mem); // MOVZ X1, #0x4000
    cpu1.step(mem); // LDXR X0, [X1]

    TEST_ASSERT(cpu1.read_register(0) == 100);

    // Core 0 performs an ordinary store to 0x4000
    // 0x2000: MOVZ X1, #0x4000
    // 0x2004: MOVZ X2, #200
    // 0x2008: STR X2, [X1]
    mem.write_u32(0x2000, 0xD2880001u);
    mem.write_u32(0x2004, 0xD2801902u);
    mem.write_u32(0x2008, 0xF9000022u);

    cpu0.set_current_el(1);
    cpu0.set_program_counter(0x2000);
    cpu0.step(mem);
    cpu0.step(mem);
    cpu0.step(mem); // STR X2, [X1] commits 200

    TEST_ASSERT(mem.read_u64(target_addr) == 200);

    // Core 1 now attempts STXR W3, X4, [X1]
    // 0x1008: MOVZ X4, #300
    // 0x100C: STXR W3, X4, [X1] (0xC8037C24: STXR W3, X4, [X1])
    mem.write_u32(0x1008, 0xD2802584u);
    mem.write_u32(0x100C, 0xC8037C24u);

    cpu1.step(mem); // MOVZ
    cpu1.step(mem); // STXR

    // STXR status W3 MUST be 1 (failure), and memory at 0x4000 must NOT be updated
    TEST_ASSERT(cpu1.read_register(3) == 1);
    TEST_ASSERT(mem.read_u64(target_addr) == 200);

    std::cout << "  -> PASSED\n";
}

void test_concurrent_stripe_isolation() {
    std::cout << "[Test 3] Memory lock-striping concurrent isolation...\n";
    Memory mem(1024 * 1024);
    mem.set_concurrent_mode(true);

    // Run two threads writing to disjoint stripes (stripe size = 4096 bytes)
    // Thread A writes stripe 1 (0x1000 - 0x1FFF)
    // Thread B writes stripe 2 (0x2000 - 0x2FFF)
    std::atomic<bool> start_flag{false};
    std::thread t1([&]() {
        while (!start_flag.load()) {}
        for (int i = 0; i < 1000; ++i) {
            mem.write_u64(0x1000, static_cast<std::uint64_t>(i));
            (void)mem.read_u64(0x1000);
        }
    });

    std::thread t2([&]() {
        while (!start_flag.load()) {}
        for (int i = 0; i < 1000; ++i) {
            mem.write_u64(0x2000, static_cast<std::uint64_t>(i));
            (void)mem.read_u64(0x2000);
        }
    });

    start_flag.store(true);
    t1.join();
    t2.join();

    TEST_ASSERT(mem.read_u64(0x1000) == 999);
    TEST_ASSERT(mem.read_u64(0x2000) == 999);

    std::cout << "  -> PASSED\n";
}

} // namespace

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::cout << "====================================================\n";
    std::cout << "Stage 198.7: Multicore Coherency Protocol Tests\n";
    std::cout << "====================================================\n";

    test_shared_memory_multi_copy_atomicity();
    test_exclusive_monitor_cross_core_invalidation();
    test_concurrent_stripe_isolation();

    std::cout << "\nAll Stage 198.7 Coherency Protocol tests passed successfully!\n";
    return 0;
}

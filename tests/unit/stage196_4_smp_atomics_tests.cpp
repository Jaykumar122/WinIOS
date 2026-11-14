#include <iostream>
#include <vector>
#include <cstdint>
#include "cpu/cpu.h"
#include "cpu/smp_cluster.h"
#include "memory/memory.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;

static void write_u32(Memory& mem, std::uint64_t addr, std::uint32_t val) {
    mem.write_u32(addr, val);
}

// -----------------------------------------------------------------------------
// Test 1: 4-Core Contended LDADD Shared Counter
// -----------------------------------------------------------------------------
static bool test_4core_ldadd_contention() {
    constexpr std::size_t kCores = 4;
    constexpr std::uint32_t kIterationsPerCore = 100;
    constexpr std::uint64_t kCounterAddr = 0x8000;
    constexpr std::uint64_t kCodeAddr = 0x1000;

    SMPCluster cluster(kCores, 0x20000);
    auto& mem = cluster.get_memory();
    mem.write_u32(kCounterAddr, 0); // initial counter = 0

    // ldadd_loop:
    // 0x1000: ldadd w0, w1, [x2] -> 0xb8200041
    // 0x1004: subs  w4, w4, #1   -> 0x71000484
    // 0x1008: b.ne  0x1000       -> 0x54ffffc1
    // 0x100C: hlt   #0           -> 0xd4400000
    write_u32(mem, kCodeAddr + 0x00, 0xb8200041u);
    write_u32(mem, kCodeAddr + 0x04, 0x71000484u);
    write_u32(mem, kCodeAddr + 0x08, 0x54ffffc1u);
    write_u32(mem, kCodeAddr + 0x0C, 0xd4400000u);

    for (std::size_t i = 0; i < kCores; ++i) {
        auto& core = cluster.get_core(i);
        core.set_current_el(1);
        core.write_register(0, 1);                  // addend = 1
        core.write_register(2, kCounterAddr);       // &counter
        core.write_register(4, kIterationsPerCore); // loop count
        core.set_program_counter(kCodeAddr);
        cluster.resume_core(i);
    }

    std::size_t steps = cluster.run_deterministic(50000);
    TEST_ASSERT(steps > 0, "Guest instructions retired");

    std::uint32_t final_val = mem.read_u32(kCounterAddr);
    TEST_ASSERT(final_val == kCores * kIterationsPerCore, "LDADD shared counter exactly equal to 400 (no lost updates)");
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: 4-Core Contended LDXR / STXR Exclusive Retry Loop
// -----------------------------------------------------------------------------
static bool test_4core_ldxr_stxr_contention() {
    constexpr std::size_t kCores = 4;
    constexpr std::uint32_t kIterationsPerCore = 50;
    constexpr std::uint64_t kCounterAddr = 0x8000;
    constexpr std::uint64_t kCodeAddr = 0x2000;

    SMPCluster cluster(kCores, 0x20000);
    auto& mem = cluster.get_memory();
    mem.write_u32(kCounterAddr, 0);

    // ldxr_loop:
    // 0x2000: ldxr w0, [x2]      -> 0x885f7c40
    // 0x2004: add  w0, w0, #1    -> 0x11000400
    // 0x2008: stxr w3, w0, [x2]  -> 0x88037c40
    // 0x200C: cbnz w3, 0x2000    -> 0x35ffffa3
    // 0x2010: subs w4, w4, #1    -> 0x71000484
    // 0x2014: b.ne 0x2000        -> 0x54ffff61
    // 0x2018: hlt  #0            -> 0xd4400000
    write_u32(mem, kCodeAddr + 0x00, 0x885f7c40u);
    write_u32(mem, kCodeAddr + 0x04, 0x11000400u);
    write_u32(mem, kCodeAddr + 0x08, 0x88037c40u);
    write_u32(mem, kCodeAddr + 0x0C, 0x35ffffa3u);
    write_u32(mem, kCodeAddr + 0x10, 0x71000484u);
    write_u32(mem, kCodeAddr + 0x14, 0x54ffff61u);
    write_u32(mem, kCodeAddr + 0x18, 0xd4400000u);

    for (std::size_t i = 0; i < kCores; ++i) {
        auto& core = cluster.get_core(i);
        core.set_current_el(1);
        core.write_register(2, kCounterAddr);       // &counter
        core.write_register(4, kIterationsPerCore); // loop count
        core.set_program_counter(kCodeAddr);
        cluster.resume_core(i);
    }

    std::size_t steps = cluster.run_deterministic(100000);
    TEST_ASSERT(steps > 0, "Guest instructions retired");

    std::uint32_t final_val = mem.read_u32(kCounterAddr);
    TEST_ASSERT(final_val == kCores * kIterationsPerCore, "LDXR/STXR shared counter exactly equal to 200 (no lost updates)");
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: 4-Core Contended CAS Retry Loop
// -----------------------------------------------------------------------------
static bool test_4core_cas_contention() {
    constexpr std::size_t kCores = 4;
    constexpr std::uint32_t kIterationsPerCore = 50;
    constexpr std::uint64_t kCounterAddr = 0x8000;
    constexpr std::uint64_t kCodeAddr = 0x3000;

    SMPCluster cluster(kCores, 0x20000);
    auto& mem = cluster.get_memory();
    mem.write_u32(kCounterAddr, 0);

    // cas_loop:
    // 0x3000: ldr  w0, [x2]      -> 0xb9400040
    // 0x3004: add  w1, w0, #1    -> 0x11000401
    // 0x3008: mov  w5, w0        -> 0x2a0003e5
    // 0x300C: cas  w0, w1, [x2]  -> 0x88a07c41
    // 0x3010: cmp  w0, w5        -> 0x6b05001f
    // 0x3014: b.ne 0x3004        -> 0x54ffff81 (retry)
    // 0x3018: subs w4, w4, #1    -> 0x71000484
    // 0x301C: b.ne 0x3000        -> 0x54ffff21 (next increment)
    // 0x3020: hlt  #0            -> 0xd4400000
    write_u32(mem, kCodeAddr + 0x00, 0xb9400040u);
    write_u32(mem, kCodeAddr + 0x04, 0x11000401u);
    write_u32(mem, kCodeAddr + 0x08, 0x2a0003e5u);
    write_u32(mem, kCodeAddr + 0x0C, 0x88a07c41u);
    write_u32(mem, kCodeAddr + 0x10, 0x6b05001fu);
    write_u32(mem, kCodeAddr + 0x14, 0x54ffff81u);
    write_u32(mem, kCodeAddr + 0x18, 0x71000484u);
    write_u32(mem, kCodeAddr + 0x1C, 0x54ffff21u);
    write_u32(mem, kCodeAddr + 0x20, 0xd4400000u);

    for (std::size_t i = 0; i < kCores; ++i) {
        auto& core = cluster.get_core(i);
        core.set_current_el(1);
        core.write_register(2, kCounterAddr);       // &counter
        core.write_register(4, kIterationsPerCore); // loop count
        core.set_program_counter(kCodeAddr);
        cluster.resume_core(i);
    }

    std::size_t steps = cluster.run_deterministic(100000);
    TEST_ASSERT(steps > 0, "Guest instructions retired");

    std::uint32_t final_val = mem.read_u32(kCounterAddr);
    TEST_ASSERT(final_val == kCores * kIterationsPerCore, "CAS shared counter exactly equal to 200 (no lost updates)");
    return true;
}

int main() {
    std::cout << "[STAGE 196.4] Running Atomicity Under SMP Execution Tests (Acceptance Test B)..." << std::endl;

    if (!test_4core_ldadd_contention()) {
        std::cerr << "test_4core_ldadd_contention FAILED" << std::endl;
        return 1;
    }
    if (!test_4core_ldxr_stxr_contention()) {
        std::cerr << "test_4core_ldxr_stxr_contention FAILED" << std::endl;
        return 1;
    }
    if (!test_4core_cas_contention()) {
        std::cerr << "test_4core_cas_contention FAILED" << std::endl;
        return 1;
    }

    std::cout << "[STAGE 196.4] All SMP Atomicity Tests PASSED (Acceptance Test B VERIFIED)." << std::endl;
    return 0;
}

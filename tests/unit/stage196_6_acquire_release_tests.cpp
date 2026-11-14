#include <iostream>
#include <vector>
#include <cstdint>
#include <exception>
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
// Litmus Test 1: Message Passing (MP) with Acquire / Release
// Producer: STR [data], STLR [flag]
// Consumer: LDAR [flag], LDR [data]
// Invariant: If consumer observes flag == 1, it MUST observe data == 42.
// -----------------------------------------------------------------------------
static bool test_litmus_message_passing_acquire_release() {
    constexpr std::uint64_t kDataAddr = 0x8000;
    constexpr std::uint64_t kFlagAddr = 0x8004;

    SMPCluster cluster(2, 0x10000);
    auto& mem = cluster.get_memory();
    mem.write_u32(kDataAddr, 0);
    mem.write_u32(kFlagAddr, 0);

    // Producer (Core 0):
    // 0x1000: str  w0, [x1]  -> 0xb9000020
    // 0x1004: stlr w2, [x3]  -> 0x889ffc62
    // 0x1008: hlt  #0        -> 0xd4400000
    write_u32(mem, 0x1000, 0xb9000020u);
    write_u32(mem, 0x1004, 0x889ffc62u);
    write_u32(mem, 0x1008, 0xd4400000u);

    // Consumer (Core 1):
    // 0x2000: ldar w0, [x3]  -> 0x88dffc60
    // 0x2004: cbz  w0, 0x2000-> 0x34ffffe0
    // 0x2008: ldr  w1, [x1]  -> 0xb9400021
    // 0x200C: hlt  #0        -> 0xd4400000
    write_u32(mem, 0x2000, 0x88dffc60u);
    write_u32(mem, 0x2004, 0x34ffffe0u);
    write_u32(mem, 0x2008, 0xb9400021u);
    write_u32(mem, 0x200C, 0xd4400000u);

    auto& core0 = cluster.get_core(0);
    core0.set_current_el(1);
    core0.write_register(0, 42);
    core0.write_register(1, kDataAddr);
    core0.write_register(2, 1);
    core0.write_register(3, kFlagAddr);
    core0.set_program_counter(0x1000);

    auto& core1 = cluster.get_core(1);
    core1.set_current_el(1);
    core1.write_register(1, kDataAddr);
    core1.write_register(3, kFlagAddr);
    core1.set_program_counter(0x2000);
    cluster.resume_core(1);

    std::size_t steps = cluster.run_deterministic(10000);
    TEST_ASSERT(steps > 0, "Guest instructions stepped");

    const auto flag_val = static_cast<std::uint32_t>(core1.read_register(0));
    const auto data_val = static_cast<std::uint32_t>(core1.read_register(1));

    TEST_ASSERT(flag_val == 1, "Consumer observed published flag == 1");
    TEST_ASSERT(data_val == 42, "Consumer observed data == 42 under acquire/release (forbidden outcome prevented)");
    return true;
}

// -----------------------------------------------------------------------------
// Litmus Test 2: Store Buffering (SB) with DMB SY
// Core 0: STR [X], DMB SY, LDR [Y]
// Core 1: STR [Y], DMB SY, LDR [X]
// Outcome (r0 == 0 && r1 == 0) is ARCHITECTURALLY FORBIDDEN with DMB SY.
// -----------------------------------------------------------------------------
static bool test_litmus_store_buffering_dmb_sy() {
    constexpr std::uint64_t kAddrX = 0x8000;
    constexpr std::uint64_t kAddrY = 0x8004;

    SMPCluster cluster(2, 0x10000);
    auto& mem = cluster.get_memory();
    mem.write_u32(kAddrX, 0);
    mem.write_u32(kAddrY, 0);

    // Core 0:
    // 0x1000: str w0, [x1] -> 0xb9000020 (write 1 to X)
    // 0x1004: dmb sy       -> 0xd5033fbf
    // 0x1008: ldr w2, [x3] -> 0xb9400062 (read Y into W2)
    // 0x100C: hlt #0       -> 0xd4400000
    write_u32(mem, 0x1000, 0xb9000020u);
    write_u32(mem, 0x1004, 0xd5033fbfu);
    write_u32(mem, 0x1008, 0xb9400062u);
    write_u32(mem, 0x100C, 0xd4400000u);

    // Core 1:
    // 0x2000: str w0, [x3] -> 0xb9000060 (write 1 to Y)
    // 0x2004: dmb sy       -> 0xd5033fbf
    // 0x2008: ldr w2, [x1] -> 0xb9400022 (read X into W2)
    // 0x200C: hlt #0       -> 0xd4400000
    write_u32(mem, 0x2000, 0xb9000060u);
    write_u32(mem, 0x2004, 0xd5033fbfu);
    write_u32(mem, 0x2008, 0xb9400022u);
    write_u32(mem, 0x200C, 0xd4400000u);

    auto& core0 = cluster.get_core(0);
    core0.set_current_el(1);
    core0.write_register(0, 1);
    core0.write_register(1, kAddrX);
    core0.write_register(3, kAddrY);
    core0.set_program_counter(0x1000);

    auto& core1 = cluster.get_core(1);
    core1.set_current_el(1);
    core1.write_register(0, 1);
    core1.write_register(1, kAddrX);
    core1.write_register(3, kAddrY);
    core1.set_program_counter(0x2000);
    cluster.resume_core(1);

    cluster.run_deterministic(10000);

    const auto c0_y = static_cast<std::uint32_t>(core0.read_register(2));
    const auto c1_x = static_cast<std::uint32_t>(core1.read_register(2));

    // In SB with DMB SY, at least one core must see the other's store.
    // Both seeing 0 is forbidden.
    TEST_ASSERT(!(c0_y == 0 && c1_x == 0), "Store buffering forbidden outcome (0,0) prevented by DMB SY");
    return true;
}

// -----------------------------------------------------------------------------
// Litmus Test 3: Lock-based Publication and Data Visibility
// Producer writes payload under spinlock, releases spinlock with STLR.
// Consumer acquires spinlock with LDAXR/STLXR, verifies all payload fields.
// -----------------------------------------------------------------------------
static bool test_litmus_lock_publication() {
    constexpr std::uint64_t kLockAddr = 0x8000;
    constexpr std::uint64_t kField1Addr = 0x8008;
    constexpr std::uint64_t kField2Addr = 0x8010;

    SMPCluster cluster(2, 0x10000);
    auto& mem = cluster.get_memory();
    mem.write_u64(kLockAddr, 0); // unlocked = 0
    mem.write_u64(kField1Addr, 0);
    mem.write_u64(kField2Addr, 0);

    // Producer (Core 0):
    // 0x1000: ldadd  w0, w1, [x2]  -> 0xb8200041
    // 0x1004: str    x3, [x4]      -> 0xf9000083
    // 0x1008: str    x5, [x6]      -> 0xf90000c5
    // 0x100C: stlr   xzr, [x2]     -> 0xc89ffc5f
    // 0x1010: hlt    #0            -> 0xd4400000
    write_u32(mem, 0x1000, 0xb8200041u);
    write_u32(mem, 0x1004, 0xf9000083u);
    write_u32(mem, 0x1008, 0xf90000c5u);
    write_u32(mem, 0x100C, 0xc89ffc5fu);
    write_u32(mem, 0x1010, 0xd4400000u);

    // Consumer (Core 1):
    // 0x2000: ldaxr  w0, [x2]      -> 0x885ffc40
    // 0x2004: cbnz   w0, 0x2000    -> 0x35ffffe0
    // 0x2008: stlxr  w1, w7, [x2]  -> 0x8801fc47
    // 0x200C: cbnz   w1, 0x2000    -> 0x35ffffa1
    // 0x2010: ldr    x8, [x4]      -> 0xf9400088
    // 0x2014: ldr    x9, [x6]      -> 0xf94000c9
    // 0x2018: stlr   xzr, [x2]     -> 0xc89ffc5f
    // 0x201C: hlt    #0            -> 0xd4400000
    write_u32(mem, 0x2000, 0x885ffc40u);
    write_u32(mem, 0x2004, 0x35ffffe0u);
    write_u32(mem, 0x2008, 0x8801fc47u);
    write_u32(mem, 0x200C, 0x35ffffa1u);
    write_u32(mem, 0x2010, 0xf9400088u);
    write_u32(mem, 0x2014, 0xf94000c9u);
    write_u32(mem, 0x2018, 0xc89ffc5fu);
    write_u32(mem, 0x201C, 0xd4400000u);

    auto& core0 = cluster.get_core(0);
    core0.set_current_el(1);
    core0.write_register(0, 1);
    core0.write_register(2, kLockAddr);
    core0.write_register(3, 0x1111);
    core0.write_register(4, kField1Addr);
    core0.write_register(5, 0x2222);
    core0.write_register(6, kField2Addr);
    core0.set_program_counter(0x1000);

    auto& core1 = cluster.get_core(1);
    core1.set_current_el(1);
    core1.write_register(2, kLockAddr);
    core1.write_register(4, kField1Addr);
    core1.write_register(6, kField2Addr);
    core1.write_register(7, 1);
    core1.set_program_counter(0x2000);
    cluster.resume_core(1);

    cluster.run_deterministic(20000);

    std::uint64_t field1 = core1.read_register(8);
    std::uint64_t field2 = core1.read_register(9);

    TEST_ASSERT(field1 == 0x1111, "Field1 published and observed correctly");
    TEST_ASSERT(field2 == 0x2222, "Field2 published and observed correctly");
    return true;
}

int main() {
    try {
        std::cout << "[STAGE 196.6] Running Acquire/Release Litmus Tests (Acceptance Test C)..." << std::endl;

        if (!test_litmus_message_passing_acquire_release()) {
            std::cerr << "test_litmus_message_passing_acquire_release FAILED" << std::endl;
            return 1;
        }
        if (!test_litmus_store_buffering_dmb_sy()) {
            std::cerr << "test_litmus_store_buffering_dmb_sy FAILED" << std::endl;
            return 1;
        }
        if (!test_litmus_lock_publication()) {
            std::cerr << "test_litmus_lock_publication FAILED" << std::endl;
            return 1;
        }

        std::cout << "[STAGE 196.6] All Acquire/Release Litmus Tests PASSED (Acceptance Test C VERIFIED)." << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "STAGE 196.6 Exception caught: " << e.what() << std::endl;
        return 1;
    }
}

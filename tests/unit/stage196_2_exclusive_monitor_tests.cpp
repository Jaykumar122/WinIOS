#include <iostream>
#include <vector>
#include <cstdint>
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "cpu/smp_cluster.h"
#include "cpu/smp_exclusive_monitor.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;

// Helper to assemble instructions into memory
static void write_u32(Memory& mem, std::uint64_t addr, std::uint32_t val) {
    mem.write_u32(addr, val);
}

// LDXR X0, [X1]  -> 0xC85F7C20 (size=11, op=2, Rs=31, Rt2=31, Rn=1, Rt=0)
// STXR W2, X3, [X1] -> 0xC8027C23 (size=11, op=0, Rs=2, Rt2=31, Rn=1, Rt=3)
// CLREX          -> 0xD5033F5F (CRn=3, op1=3, CRm=15, op2=2, Rt=31)

static bool test_exclusive_monitor_granule() {
    SmpExclusiveMonitorManager mgr(4);
    // Core 0 reserves 0x1008
    mgr.load_exclusive(0, 0x1008, 8);
    TEST_ASSERT(mgr.is_reservation_valid(0, 0x1008, 8), "Core 0 reservation valid");
    TEST_ASSERT(mgr.is_reservation_valid(0, 0x1038, 8), "Same 64B granule valid");

    // Core 1 writes to 0x1020 (same 64B granule 0x1000..0x103F)
    mgr.invalidate_reservations(1, 0x1020, 4);
    TEST_ASSERT(!mgr.is_reservation_valid(0, 0x1008, 8), "Core 0 reservation must be invalidated by write in same granule");

    // Core 0 re-reserves 0x1000
    mgr.load_exclusive(0, 0x1000, 8);
    // Core 1 writes to 0x2000 (different 64B granule)
    mgr.invalidate_reservations(1, 0x2000, 8);
    TEST_ASSERT(mgr.is_reservation_valid(0, 0x1000, 8), "Core 0 reservation must remain valid for distant write");

    // Core 0 CLREX
    mgr.clear_reservation(0);
    TEST_ASSERT(!mgr.is_reservation_valid(0, 0x1000, 8), "CLREX must invalidate reservation");
    return true;
}

static bool test_ldxr_stxr_success_and_failure() {
    Memory mem(0x10000);
    CPU cpu;
    cpu.set_current_el(1);

    // Initial data at 0x8000: 0x1122334455667788
    mem.write_u64(0x8000, 0x1122334455667788ULL);

    // Program:
    // 0x1000: LDXR X0, [X1]        0xC85F7C20
    // 0x1004: STXR W2, X3, [X1]    0xC8027C23 (write X3 to [X1], status in W2)
    // 0x1008: STXR W4, X3, [X1]    0xC8047C23 (second STXR without LDXR -> should fail)
    write_u32(mem, 0x1000, 0xC85F7C20u);
    write_u32(mem, 0x1004, 0xC8027C23u);
    write_u32(mem, 0x1008, 0xC8047C23u);

    cpu.write_register(1, 0x8000); // base address
    cpu.write_register(3, 0xAABBCCDDEEFF0011ULL); // new value
    cpu.set_program_counter(0x1000);

    // Step 1: LDXR X0, [X1]
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 0x1122334455667788ULL, "LDXR loaded correct value");

    // Step 2: STXR W2, X3, [X1] (should succeed, W2=0)
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(2) == 0, "STXR succeeded (status = 0)");
    TEST_ASSERT(mem.read_u64(0x8000) == 0xAABBCCDDEEFF0011ULL, "Memory updated by STXR");

    // Step 3: STXR W4, X3, [X1] (second STXR must fail, W4=1)
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(4) == 1, "Second STXR failed without LDXR (status = 1)");
    return true;
}

static bool test_clrex_clears_reservation() {
    Memory mem(0x10000);
    CPU cpu;
    cpu.set_current_el(1);
    mem.write_u64(0x8000, 0x42);

    // 0x1000: LDXR X0, [X1]
    // 0x1004: CLREX
    // 0x1008: STXR W2, X3, [X1]
    write_u32(mem, 0x1000, 0xC85F7C20u);
    write_u32(mem, 0x1004, 0xD5033F5Fu);
    write_u32(mem, 0x1008, 0xC8027C23u);

    cpu.write_register(1, 0x8000);
    cpu.write_register(3, 0x99);
    cpu.set_program_counter(0x1000);

    cpu.step(mem); // LDXR
    TEST_ASSERT(cpu.read_register(0) == 0x42, "LDXR read value");

    cpu.step(mem); // CLREX

    cpu.step(mem); // STXR -> must fail
    TEST_ASSERT(cpu.read_register(2) == 1, "STXR failed after CLREX (status = 1)");
    TEST_ASSERT(mem.read_u64(0x8000) == 0x42, "Memory unchanged after failed STXR");
    return true;
}

static bool test_smp_cross_core_invalidation() {
    SMPCluster cluster(2, 0x10000);
    auto& mem = cluster.get_memory();
    mem.write_u64(0x8000, 0x100);

    // Program:
    // 0x1000: LDXR X0, [X1]     0xC85F7C20
    // 0x1004: STXR W2, X3, [X1] 0xC8027C23
    write_u32(mem, 0x1000, 0xC85F7C20u);
    write_u32(mem, 0x1004, 0xC8027C23u);

    auto& core0 = cluster.get_core(0);
    auto& core1 = cluster.get_core(1);

    core0.write_register(1, 0x8000);
    core0.write_register(3, 0x200);
    core0.set_program_counter(0x1000);

    // Core 0 executes LDXR at 0x8000
    core0.step(mem);
    TEST_ASSERT(core0.read_register(0) == 0x100, "Core 0 LDXR succeeded");

    // Core 1 writes to 0x8000 (intervening cross-core store)
    core1.note_normal_store(0x8000, 8);

    // Core 0 executes STXR -> must fail because Core 1's store invalidated the reservation
    core0.step(mem);
    TEST_ASSERT(core0.read_register(2) == 1, "Core 0 STXR failed due to cross-core invalidation");
    return true;
}

static bool test_eret_clears_reservation() {
    Memory mem(0x10000);
    CPU cpu;
    cpu.set_current_el(1);
    mem.write_u64(0x8000, 0x55);

    // Set up ELR_EL1 and SPSR_EL1 for ERET to 0x1008
    cpu.set_elr_el1(0x1008);
    cpu.set_spsr_el1(0x00000005); // SPSR_EL1: EL1h

    // 0x1000: LDXR X0, [X1]     0xC85F7C20
    // 0x1004: ERET              0xD69F03E0
    // 0x1008: STXR W2, X3, [X1] 0xC8027C23
    write_u32(mem, 0x1000, 0xC85F7C20u);
    write_u32(mem, 0x1004, 0xD69F03E0u);
    write_u32(mem, 0x1008, 0xC8027C23u);

    cpu.write_register(1, 0x8000);
    cpu.write_register(3, 0x77);
    cpu.set_program_counter(0x1000);

    cpu.step(mem); // LDXR
    TEST_ASSERT(cpu.read_register(0) == 0x55, "LDXR succeeded");

    cpu.step(mem); // ERET -> jumps to 0x1008 and clears exclusive monitor
    TEST_ASSERT(cpu.get_program_counter() == 0x1008, "ERET jumped to 0x1008");

    cpu.step(mem); // STXR -> must fail because ERET cleared reservation
    TEST_ASSERT(cpu.read_register(2) == 1, "STXR failed after ERET (status = 1)");
    return true;
}

int main() {
    std::cout << "[STAGE 196.2] Running Exclusive Monitor Tests (Acceptance Test A)..." << std::endl;

    if (!test_exclusive_monitor_granule()) {
        std::cerr << "test_exclusive_monitor_granule FAILED" << std::endl;
        return 1;
    }
    if (!test_ldxr_stxr_success_and_failure()) {
        std::cerr << "test_ldxr_stxr_success_and_failure FAILED" << std::endl;
        return 1;
    }
    if (!test_clrex_clears_reservation()) {
        std::cerr << "test_clrex_clears_reservation FAILED" << std::endl;
        return 1;
    }
    if (!test_smp_cross_core_invalidation()) {
        std::cerr << "test_smp_cross_core_invalidation FAILED" << std::endl;
        return 1;
    }
    if (!test_eret_clears_reservation()) {
        std::cerr << "test_eret_clears_reservation FAILED" << std::endl;
        return 1;
    }

    std::cout << "[STAGE 196.2] All Exclusive Monitor Tests PASSED (Acceptance Test A VERIFIED)." << std::endl;
    return 0;
}

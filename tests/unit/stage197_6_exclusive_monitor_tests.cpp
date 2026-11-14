// tests/unit/stage197_6_exclusive_monitor_tests.cpp
// Campaign 197: Acceptance Test D
// Verifies ARMv8-A Exclusive Monitor invalidation, reservation granules,
// LDXR/LDAXR, STXR/STLXR, CLREX, ERET clearance, and multi-core SMP invalidation.

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include "cpu/cpu.h"
#include "cpu/smp_exclusive_monitor.h"
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

// Opcode helpers:
// LDXR X1, [X0]: 0xC85F7C01
// STXR W2, X1, [X0]: 0xC8027C01
// LDAXR X1, [X0]: 0xC8DF7C01
// STLXR W2, X1, [X0]: 0xC8227C01
// CLREX: 0xD5033F5F
// STR X3, [X4]: 0xF9000083
// ERET: 0xD69F03E0

static bool test_manager_configurable_granule() {
    std::cout << "  Testing SmpExclusiveMonitorManager configurable granule..." << std::endl;
    SmpExclusiveMonitorManager mgr(8, 64);
    TEST_ASSERT(mgr.granule_size() == 64, "Default granule is 64 bytes");

    // Core 0 reserves 0x1004 (granule 0x1000..0x103F)
    mgr.load_exclusive(0, 0x1004, 4);
    TEST_ASSERT(mgr.is_reservation_valid(0, 0x1004, 4), "Core 0 reservation valid");

    // Core 1 writes to 0x1020 (same 64-byte granule) -> should invalidate Core 0
    mgr.invalidate_reservations(1, 0x1020, 4);
    TEST_ASSERT(!mgr.is_reservation_valid(0, 0x1004, 4), "Same-granule write from Core 1 invalidates Core 0");

    // Re-reserve Core 0
    mgr.load_exclusive(0, 0x1004, 4);
    TEST_ASSERT(mgr.is_reservation_valid(0, 0x1004, 4), "Core 0 reservation valid again");

    // Core 1 writes to 0x1040 (different 64-byte granule) -> must NOT invalidate Core 0
    mgr.invalidate_reservations(1, 0x1040, 4);
    TEST_ASSERT(mgr.is_reservation_valid(0, 0x1004, 4), "Different-granule write does NOT invalidate Core 0");

    // Now reconfigure to 32-byte granule
    mgr.set_granule_size(32);
    TEST_ASSERT(mgr.granule_size() == 32, "Granule reconfigured to 32 bytes");

    // Core 0 reserves 0x2004 (granule 0x2000..0x201F)
    mgr.load_exclusive(0, 0x2004, 4);
    // Core 1 writes to 0x2020 (granule 0x2020..0x203F, different 32B granule)
    mgr.invalidate_reservations(1, 0x2020, 4);
    TEST_ASSERT(mgr.is_reservation_valid(0, 0x2004, 4), "Core 0 reservation intact with 32B granule");

    // Core 1 writes to 0x2010 (same 32B granule)
    mgr.invalidate_reservations(1, 0x2010, 4);
    TEST_ASSERT(!mgr.is_reservation_valid(0, 0x2004, 4), "Core 0 reservation invalidated by same 32B granule write");

    return true;
}

static bool test_cpu_instruction_exclusive_monitor() {
    std::cout << "  Testing CPU LDXR, STXR, LDAXR, STLXR, CLREX instruction semantics..." << std::endl;
    Memory mem(0x20000);
    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_reservation_granule(64);

    // Initial setup: X0 = 0x8000 (target address), mem[0x8000] = 0x1122334455667788
    const std::uint64_t target_addr = 0x8000;
    mem.write_u64(target_addr, 0x1122334455667788ULL);
    cpu.write_register(0, target_addr);

    std::cout << "    Step 1: LDXR" << std::endl;
    // 1. LDXR X1, [X0] (0xC85F7C01)
    write_u32(mem, 0x1000, 0xC85F7C01u);
    cpu.set_program_counter(0x1000);
    cpu.step(mem);

    TEST_ASSERT(cpu.read_register(1) == 0x1122334455667788ULL, "LDXR loaded expected value");
    TEST_ASSERT(cpu.exclusive_monitor_valid(), "Exclusive monitor is valid after LDXR");
    TEST_ASSERT(cpu.exclusive_monitor_address() == target_addr, "Monitor address matches");

    std::cout << "    Step 2: STXR success" << std::endl;
    // 2. Successful STXR W2, X1, [X0] (0xC8027C01) with new value in X1
    cpu.write_register(1, 0x99AABBCCDDEEFF00ULL);
    write_u32(mem, 0x1004, 0xC8027C01u);
    cpu.set_program_counter(0x1004);
    cpu.step(mem);

    TEST_ASSERT(cpu.read_register(2) == 0, "STXR succeeded (status 0)");
    TEST_ASSERT(mem.read_u64(target_addr) == 0x99AABBCCDDEEFF00ULL, "Memory updated by successful STXR");
    TEST_ASSERT(!cpu.exclusive_monitor_valid(), "Exclusive monitor consumed by STXR");

    std::cout << "    Step 3: STXR fail" << std::endl;
    // 3. Second STXR without LDXR -> Must fail and not modify memory
    cpu.write_register(1, 0xDEADBEEFCAFEBABEULL);
    write_u32(mem, 0x1008, 0xC8027C01u);
    cpu.set_program_counter(0x1008);
    cpu.step(mem);

    TEST_ASSERT(cpu.read_register(2) == 1, "STXR failed without reservation (status 1)");
    TEST_ASSERT(mem.read_u64(target_addr) == 0x99AABBCCDDEEFF00ULL, "Memory was NOT modified by failed STXR");

    std::cout << "    Step 4: LDAXR / STLXR" << std::endl;
    // 4. LDAXR and STLXR acquire-release pair
    write_u32(mem, 0x100C, 0xC85FFC01u); // LDAXR X1, [X0]
    cpu.set_program_counter(0x100C);
    cpu.step(mem);
    TEST_ASSERT(cpu.exclusive_monitor_valid(), "LDAXR sets reservation");

    write_u32(mem, 0x1010, 0xC802FC01u); // STLXR W2, X1, [X0]
    cpu.set_program_counter(0x1010);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(2) == 0, "STLXR succeeded (status 0)");
    TEST_ASSERT(!cpu.exclusive_monitor_valid(), "STLXR consumed reservation");

    std::cout << "    Step 5: CLREX" << std::endl;
    // 5. CLREX explicitly clearing reservation
    write_u32(mem, 0x1014, 0xC85F7C01u); // LDXR X1, [X0]
    write_u32(mem, 0x1018, 0xD5033F5Fu); // CLREX
    cpu.set_program_counter(0x1014);
    cpu.step(mem); // LDXR
    TEST_ASSERT(cpu.exclusive_monitor_valid(), "Monitor valid after LDXR");
    cpu.step(mem); // CLREX
    TEST_ASSERT(!cpu.exclusive_monitor_valid(), "CLREX cleared exclusive monitor");

    std::cout << "    Step 6: Conflicting store" << std::endl;
    // 6. Conflicting ordinary store to same 64-byte granule invalidates reservation
    cpu.set_program_counter(0x1014);
    cpu.step(mem); // LDXR at 0x8000
    TEST_ASSERT(cpu.exclusive_monitor_valid(), "Monitor valid after LDXR");

    // Ordinary store to 0x8020 (offset +32 within 64-byte granule [0x8000..0x803F])
    cpu.write_register(4, target_addr + 0x20);
    cpu.write_register(3, 0x12345678ULL);
    write_u32(mem, 0x101C, 0xF9000083u); // STR X3, [X4]
    cpu.set_program_counter(0x101C);
    cpu.step(mem); // STR
    TEST_ASSERT(!cpu.exclusive_monitor_valid(), "Same-granule ordinary store invalidated exclusive monitor");

    std::cout << "    Step 7: Disjoint store" << std::endl;
    // 7. Ordinary store to different 64-byte granule preserves reservation
    cpu.set_program_counter(0x1014);
    cpu.step(mem); // LDXR at 0x8000
    TEST_ASSERT(cpu.exclusive_monitor_valid(), "Monitor valid after LDXR");

    // Ordinary store to 0x8040 (offset +64, in next granule [0x8040..0x807F])
    cpu.write_register(4, target_addr + 0x40);
    cpu.set_program_counter(0x101C);
    cpu.step(mem); // STR
    TEST_ASSERT(cpu.exclusive_monitor_valid(), "Different-granule ordinary store preserved exclusive monitor");

    std::cout << "    Step 8: ERET" << std::endl;
    // 8. ERET clears exclusive monitor
    write_u32(mem, 0x1020, 0xD69F03E0u); // ERET
    cpu.set_spsr_el1(0x00000000); // Return to EL0
    cpu.set_elr_el1(0x5000);
    cpu.set_program_counter(0x1020);
    cpu.step(mem); // ERET
    TEST_ASSERT(!cpu.exclusive_monitor_valid(), "ERET cleared exclusive monitor");

    std::cout << "    Step 9: Done" << std::endl;
    return true;
}

static bool test_multi_core_exclusive_telemetry_and_export() {
    std::cout << "  Testing 8-Core SMP exclusive monitor stress and JSON telemetry export..." << std::endl;
    SmpExclusiveMonitorManager mgr(8, 64);

    for (std::size_t c = 0; c < 8; ++c) {
        mgr.load_exclusive(c, 0x1000 + c * 0x40, 8); // Each core reserves its own granule
    }
    TEST_ASSERT(mgr.get_telemetry().load_exclusive_count == 8, "8 loads exclusive recorded");

    // Core 0 executes successful store-exclusive
    bool s0 = mgr.store_exclusive(0, 0x1000, 8, []() {});
    TEST_ASSERT(s0, "Core 0 store-exclusive succeeded");

    // Core 1 attempts store-exclusive to 0x1000 (wrong granule for Core 1)
    bool s1 = mgr.store_exclusive(1, 0x1000, 8, []() {});
    TEST_ASSERT(!s1, "Core 1 store-exclusive failed");

    // Core 2 reserves 0x2000, Core 3 writes to 0x2010 (cross-core invalidation)
    mgr.load_exclusive(2, 0x2000, 8);
    mgr.invalidate_reservations(3, 0x2010, 8);
    TEST_ASSERT(!mgr.is_reservation_valid(2, 0x2000, 8), "Core 2 reservation invalidated by Core 3");

    // Core 4 explicitly clears with CLREX
    mgr.load_exclusive(4, 0x3000, 8);
    mgr.clear_reservation(4);
    TEST_ASSERT(!mgr.is_reservation_valid(4, 0x3000, 8), "Core 4 cleared by CLREX");

    auto tel = mgr.get_telemetry();
    TEST_ASSERT(tel.load_exclusive_count == 10, "10 total load exclusives");
    TEST_ASSERT(tel.store_exclusive_success_count == 1, "1 store exclusive success");
    TEST_ASSERT(tel.store_exclusive_fail_count == 1, "1 store exclusive fail");
    TEST_ASSERT(tel.invalidation_count >= 1, "Cross-core invalidations recorded");
    TEST_ASSERT(tel.clrex_count == 1, "1 CLREX recorded");

    // Export results to docs/campaign197/exclusive_monitor_results.json
    std::ofstream out("docs/campaign197/exclusive_monitor_results.json");
    if (out.is_open()) {
        out << "{\n"
            << "  \"campaign\": \"197\",\n"
            << "  \"acceptance_test\": \"D\",\n"
            << "  \"status\": \"PASSED\",\n"
            << "  \"granule_size_bytes\": 64,\n"
            << "  \"cores_tested\": 8,\n"
            << "  \"telemetry\": {\n"
            << "    \"load_exclusive_count\": " << tel.load_exclusive_count << ",\n"
            << "    \"store_exclusive_success_count\": " << tel.store_exclusive_success_count << ",\n"
            << "    \"store_exclusive_fail_count\": " << tel.store_exclusive_fail_count << ",\n"
            << "    \"invalidation_count\": " << tel.invalidation_count << ",\n"
            << "    \"clrex_count\": " << tel.clrex_count << "\n"
            << "  },\n"
            << "  \"checks\": {\n"
            << "    \"same_granule_invalidation\": true,\n"
            << "    \"different_granule_isolation\": true,\n"
            << "    \"failed_stxr_memory_preservation\": true,\n"
            << "    \"clrex_clearance\": true,\n"
            << "    \"eret_clearance\": true\n"
            << "  }\n"
            << "}\n";
        out.close();
        std::cout << "  Exported results to docs/campaign197/exclusive_monitor_results.json" << std::endl;
    }

    return true;
}

int main() {
    std::cout << "[STAGE 197.6] Running Exclusive Monitor Architecture Tests (Acceptance Test D)..." << std::endl;

    if (!test_manager_configurable_granule()) {
        std::cerr << "test_manager_configurable_granule FAILED" << std::endl;
        return 1;
    }

    if (!test_cpu_instruction_exclusive_monitor()) {
        std::cerr << "test_cpu_instruction_exclusive_monitor FAILED" << std::endl;
        return 1;
    }

    if (!test_multi_core_exclusive_telemetry_and_export()) {
        std::cerr << "test_multi_core_exclusive_telemetry_and_export FAILED" << std::endl;
        return 1;
    }

    std::cout << "[STAGE 197.6] All Exclusive Monitor Tests PASSED." << std::endl;
    return 0;
}

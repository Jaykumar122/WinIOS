// WinIOS Phase 49: AArch64 Performance Monitoring Unit (PMU) Unit Tests
#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/pmu.h"
#include "cpu/psci.h"
#include "cpu/smp_cluster.h"
#include "cpu/synchronous_exception.h"
#include "memory/memory.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cout << "Assertion failed at " << __FILE__ << ":" << __LINE__ \
                      << ": " #cond << std::endl;                              \
            std::exit(1);                                                      \
        }                                                                      \
    } while (false)

// Instruction encodings
static constexpr std::uint32_t enc_nop() {
    return 0xD503201Fu;
}

static constexpr std::uint32_t enc_movz(unsigned rd, std::uint16_t imm, unsigned shift = 0) {
    const unsigned hw = shift / 16;
    return 0xD2800000u | (static_cast<std::uint32_t>(hw) << 21u) |
           (static_cast<std::uint32_t>(imm) << 5u) | rd;
}

static constexpr std::uint32_t enc_mrs(unsigned rt, unsigned op1, unsigned crn, unsigned crm, unsigned op2) {
    return 0xD5300000u | (1u << 20u) | ((op1 & 7u) << 16u) | ((crn & 0xFu) << 12u) |
           ((crm & 0xFu) << 8u) | ((op2 & 7u) << 5u) | (rt & 0x1Fu);
}

static constexpr std::uint32_t enc_msr(unsigned op1, unsigned crn, unsigned crm, unsigned op2, unsigned rt) {
    return 0xD5100000u | (1u << 20u) | ((op1 & 7u) << 16u) | ((crn & 0xFu) << 12u) |
           ((crm & 0xFu) << 8u) | ((op2 & 7u) << 5u) | (rt & 0x1Fu);
}

// Well-known sysreg encodings: op1, crn, crm, op2
// PMCR_EL0: 3, 9, 12, 0
// PMCNTENSET_EL0: 3, 9, 12, 1
// PMCNTENCLR_EL0: 3, 9, 12, 2
// PMOVSCLR_EL0: 3, 9, 12, 3
// PMSWINC_EL0: 3, 9, 12, 4
// PMSELR_EL0: 3, 9, 12, 5
// PMCEID0_EL0: 3, 9, 12, 6
// PMCEID1_EL0: 3, 9, 12, 7
// PMCCNTR_EL0: 3, 9, 13, 0
// PMXEVTYPER_EL0: 3, 9, 13, 1
// PMXEVCNTR_EL0: 3, 9, 13, 2
// PMUSERENR_EL0: 3, 9, 14, 0
// PMINTENSET_EL1: 0, 9, 14, 1
// PMINTENCLR_EL1: 0, 9, 14, 2
// PMOVSSET_EL0: 3, 9, 14, 3
// PMCCFILTR_EL0: 3, 14, 15, 7

// Helper to write instructions to memory and initialize CPU PC
static void load_code(Memory& mem, CPU& cpu, const std::vector<std::uint32_t>& code, std::uint64_t start_pc = 0x1000) {
    cpu.set_program_counter(start_pc);
    for (std::size_t i = 0; i < code.size(); ++i) {
        mem.write_u32(static_cast<std::size_t>(start_pc + i * 4), code[i]);
    }
}

// Test 1: PMCR_EL0 control, reset bits (P, C), and counter count (N=6)
static void test_pmcr_control_and_reset() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);

    // Initial read: N should be 6 (bits [15:11])
    std::uint32_t pmcr = cpu.get_pmu().read_pmcr();
    CHECK(((pmcr >> 11u) & 0x1Fu) == 6u);
    CHECK((pmcr & 0x6u) == 0u); // Bits 1 and 2 read as 0

    // Test writing via MSR PMCR_EL0, X0
    // X0 = E (bit 0) | D (bit 3) | LC (bit 6)
    std::vector<std::uint32_t> code = {
        enc_movz(0, 0x0049),               // X0 = 0x49 (E=1, D=1, LC=1)
        enc_msr(3, 9, 12, 0, 0),           // MSR PMCR_EL0, X0
        enc_mrs(1, 3, 9, 12, 0)            // MRS X1, PMCR_EL0
    };
    load_code(mem, cpu, code);
    cpu.step(mem);
    cpu.step(mem);
    cpu.step(mem);

    std::uint64_t val = cpu.read_register(1);
    CHECK((val & 0x1u) == 0x1u);   // E set
    CHECK((val & 0x8u) == 0x8u);   // D set
    CHECK((val & 0x40u) == 0x40u); // LC set
    CHECK(((val >> 11u) & 0x1Fu) == 6u); // N remains 6

    // Test reset bits P and C
    cpu.get_pmu().write_pmccntr(12345);
    cpu.get_pmu().write_pmevcntr(0, 54321);
    CHECK(cpu.get_pmu().read_pmccntr() == 12345);
    CHECK(cpu.get_pmu().read_pmevcntr(0) == 54321);

    // Write P=1 (bit 1) and C=1 (bit 2)
    cpu.get_pmu().write_pmcr(cpu.get_pmu().read_pmcr() | winios::kPMCREventReset | winios::kPMCRCycleReset);
    CHECK(cpu.get_pmu().read_pmccntr() == 0);
    CHECK(cpu.get_pmu().read_pmevcntr(0) == 0);
    // Bits P and C must read as 0
    CHECK((cpu.get_pmu().read_pmcr() & 0x6u) == 0u);
}

// Test 2: PMCNTENSET_EL0 / PMCNTENCLR_EL0
static void test_pmcnten_set_and_clear() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);

    // Set cycle counter (bit 31) and event counters 0, 1, 2 (bits 0, 1, 2)
    std::vector<std::uint32_t> code = {
        enc_movz(0, 0x0007),               // X0 = bits 0..2
        enc_movz(1, 0x8000, 16),           // X1 = bit 31
        0xAA010000u,                       // ORR X0, X0, X1 (X0 = (1<<31) | 0x7)
        enc_msr(3, 9, 12, 1, 0),           // MSR PMCNTENSET_EL0, X0
        enc_mrs(2, 3, 9, 12, 1),           // MRS X2, PMCNTENSET_EL0
        enc_movz(3, 0x0003),               // X3 = bits 0..1 to clear
        enc_msr(3, 9, 12, 2, 3),           // MSR PMCNTENCLR_EL0, X3
        enc_mrs(4, 3, 9, 12, 2)            // MRS X4, PMCNTENCLR_EL0
    };
    load_code(mem, cpu, code);
    for (std::size_t i = 0; i < code.size(); ++i) {
        cpu.step(mem);
    }

    CHECK(cpu.read_register(2) == ((1ULL << 31u) | 0x7ULL));
    CHECK(cpu.read_register(4) == ((1ULL << 31u) | 0x4ULL)); // bits 0, 1 cleared, bit 2 and 31 remain
}

// Test 3: PMCCNTR_EL0 cycle counting
static void test_pmccntr_cycle_counting() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);

    // Enable PMCR.E and PMCNTENSET.C
    cpu.get_pmu().write_pmcr(winios::kPMCREnable);
    cpu.get_pmu().write_pmcntenset(1u << 31u);
    cpu.get_pmu().write_pmccntr(0);

    // Execute 10 NOPs
    std::vector<std::uint32_t> code(10, enc_nop());
    load_code(mem, cpu, code);
    for (std::size_t i = 0; i < 10; ++i) {
        cpu.step(mem);
    }

    CHECK(cpu.get_pmu().read_pmccntr() == 10);

    // Disable PMCR.E
    cpu.get_pmu().write_pmcr(0);
    for (std::size_t i = 0; i < 5; ++i) {
        cpu.step(mem);
    }
    // Should NOT have incremented
    CHECK(cpu.get_pmu().read_pmccntr() == 10);

    // Re-enable PMCR.E, but disable PMCNTENSET.C
    cpu.get_pmu().write_pmcr(winios::kPMCREnable);
    cpu.get_pmu().write_pmcntenclr(1u << 31u);
    for (std::size_t i = 0; i < 5; ++i) {
        cpu.step(mem);
    }
    CHECK(cpu.get_pmu().read_pmccntr() == 10);
}

// Test 4: PMCCNTR divider by 64 (PMCR.D = 1)
static void test_pmccntr_divider_by_64() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);

    // Enable PMCR.E and PMCR.D, and PMCNTENSET.C
    cpu.get_pmu().write_pmcr(winios::kPMCREnable | winios::kPMCRClockDivider);
    cpu.get_pmu().write_pmcntenset(1u << 31u);
    cpu.get_pmu().write_pmccntr(0);

    std::vector<std::uint32_t> code(128, enc_nop());
    load_code(mem, cpu, code);

    // Step 63 instructions
    for (std::size_t i = 0; i < 63; ++i) {
        cpu.step(mem);
    }
    CHECK(cpu.get_pmu().read_pmccntr() == 0); // Not yet 64 cycles

    // Step 1 more instruction (64th)
    cpu.step(mem);
    CHECK(cpu.get_pmu().read_pmccntr() == 1);

    // Step 64 more instructions (128 total)
    for (std::size_t i = 0; i < 64; ++i) {
        cpu.step(mem);
    }
    CHECK(cpu.get_pmu().read_pmccntr() == 2);
}

// Test 5: PMSELR_EL0 and PMXEVTYPER_EL0 / PMXEVCNTR_EL0
static void test_pmselr_and_pmxev() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);

    // Select counter 3 via PMSELR_EL0
    std::vector<std::uint32_t> code = {
        enc_movz(0, 3),                    // X0 = 3
        enc_msr(3, 9, 12, 5, 0),           // MSR PMSELR_EL0, X0
        enc_movz(1, 0x0008),               // X1 = INST_RETIRED (0x08)
        enc_msr(3, 9, 13, 1, 1),           // MSR PMXEVTYPER_EL0, X1
        enc_movz(2, 42),                   // X2 = 42
        enc_msr(3, 9, 13, 2, 2),           // MSR PMXEVCNTR_EL0, X2
        enc_mrs(3, 3, 9, 13, 1),           // MRS X3, PMXEVTYPER_EL0
        enc_mrs(4, 3, 9, 13, 2)            // MRS X4, PMXEVCNTR_EL0
    };
    load_code(mem, cpu, code);
    for (std::size_t i = 0; i < code.size(); ++i) {
        cpu.step(mem);
    }

    CHECK(cpu.read_register(3) == 0x0008);
    CHECK(cpu.read_register(4) == 42);
    // Direct counter 3 must also match
    CHECK(cpu.get_pmu().read_pmevcntr(3) == 42);
    CHECK((cpu.get_pmu().read_pmevtyper(3) & 0xFFFFu) == 0x0008);
}

// Test 6: Direct PMEVCNTR<n>_EL0 and PMEVTYPER<n>_EL0
static void test_direct_pmevcntr_and_pmevtyper() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);

    // PMEVCNTR0_EL0: op1=3, CRn=14, CRm=8, op2=0
    // PMEVTYPER0_EL0: op1=3, CRn=14, CRm=12, op2=0
    std::vector<std::uint32_t> code = {
        enc_movz(0, 100),                  // X0 = 100
        enc_msr(3, 14, 8, 0, 0),           // MSR PMEVCNTR0_EL0, X0
        enc_movz(1, 0x0011),               // X1 = CPU_CYCLES
        enc_msr(3, 14, 12, 0, 1),          // MSR PMEVTYPER0_EL0, X1
        enc_mrs(2, 3, 14, 8, 0),              // MRS X2, PMEVCNTR0_EL0
        enc_mrs(3, 3, 14, 12, 0)              // MRS X3, PMEVTYPER0_EL0
    };
    load_code(mem, cpu, code);
    for (std::size_t i = 0; i < code.size(); ++i) {
        cpu.step(mem);
    }

    CHECK(cpu.read_register(2) == 100);
    CHECK(cpu.read_register(3) == 0x0011);
}

// Test 7: Event counting INST_RETIRED (0x0008)
static void test_event_counting_inst_retired() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);

    // Configure counter 0 for INST_RETIRED
    cpu.get_pmu().write_pmcr(winios::kPMCREnable);
    cpu.get_pmu().write_pmcntenset(1u << 0u);
    cpu.get_pmu().write_pmevtyper(0, winios::ARMV8_EVENT_INST_RETIRED);
    cpu.get_pmu().write_pmevcntr(0, 0);

    // Run 15 NOPs
    std::vector<std::uint32_t> code(15, enc_nop());
    load_code(mem, cpu, code);
    for (std::size_t i = 0; i < 15; ++i) {
        cpu.step(mem);
    }

    CHECK(cpu.get_pmu().read_pmevcntr(0) == 15);
}

// Test 8: Event counting SW_INCR (0x0000) via PMSWINC_EL0
static void test_event_counting_sw_incr() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);

    // Configure counter 1 for SW_INCR
    cpu.get_pmu().write_pmcr(winios::kPMCREnable);
    cpu.get_pmu().write_pmcntenset(1u << 1u);
    cpu.get_pmu().write_pmevtyper(1, winios::ARMV8_EVENT_SW_INCR);
    cpu.get_pmu().write_pmevcntr(1, 0);

    // Write PMSWINC_EL0 with bit 1 set -> increments counter 1
    std::vector<std::uint32_t> code = {
        enc_movz(0, 0x0002),               // X0 = bit 1
        enc_msr(3, 9, 12, 4, 0),           // MSR PMSWINC_EL0, X0
        enc_msr(3, 9, 12, 4, 0),           // MSR PMSWINC_EL0, X0
        enc_movz(1, 0x0001),               // X1 = bit 0 (not configured for SW_INCR)
        enc_msr(3, 9, 12, 4, 1),           // MSR PMSWINC_EL0, X1
        enc_mrs(2, 3, 9, 12, 4)            // MRS X2, PMSWINC_EL0 (should return 0)
    };
    load_code(mem, cpu, code);
    for (std::size_t i = 0; i < code.size(); ++i) {
        cpu.step(mem);
    }

    CHECK(cpu.get_pmu().read_pmevcntr(1) == 2);
    CHECK(cpu.read_register(2) == 0); // PMSWINC reads as 0
}

// Test 9: Event counting MEM_ACCESS (0x0013)
static void test_event_counting_mem_access() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);

    // Configure counter 2 for MEM_ACCESS (0x0013)
    cpu.get_pmu().write_pmcr(winios::kPMCREnable);
    cpu.get_pmu().write_pmcntenset(1u << 2u);
    cpu.get_pmu().write_pmevtyper(2, winios::ARMV8_EVENT_MEM_ACCESS);
    cpu.get_pmu().write_pmevcntr(2, 0);

    // STR X0, [X1] and LDR X2, [X1]
    // X1 = 0x5000 (RAM address)
    std::vector<std::uint32_t> code = {
        enc_movz(0, 0x1234),
        enc_movz(1, 0x5000),
        0xF9000020u,                       // STR X0, [X1]
        0xF9400022u                        // LDR X2, [X1]
    };
    load_code(mem, cpu, code);
    for (std::size_t i = 0; i < code.size(); ++i) {
        cpu.step(mem);
    }

    CHECK(cpu.read_register(2) == 0x1234);
    // 2 memory accesses committed (1 store + 1 load)
    CHECK(cpu.get_pmu().read_pmevcntr(2) == 2);
}

// Test 10: Event counting EXC_TAKEN (0x0009) and EXC_RETURN (0x000A)
static void test_event_counting_exc_taken_and_return() {
    CPU cpu;
    Memory mem(0x20000);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);

    // Set VBAR_EL1 to 0x10000
    cpu.set_vbar_el1(0x10000);

    // Configure counter 0 for EXC_TAKEN and counter 1 for EXC_RETURN
    cpu.get_pmu().write_pmcr(winios::kPMCREnable);
    cpu.get_pmu().write_pmcntenset((1u << 0u) | (1u << 1u));
    cpu.get_pmu().write_pmevtyper(0, winios::ARMV8_EVENT_EXC_TAKEN);
    cpu.get_pmu().write_pmevtyper(1, winios::ARMV8_EVENT_EXC_RETURN);
    cpu.get_pmu().write_pmevcntr(0, 0);
    cpu.get_pmu().write_pmevcntr(1, 0);

    // At EL1 synchronous vector offset (0x200): put ERET (0xD69F03E0)
    mem.write_u32(0x10200, 0xD69F03E0u);

    // Code at 0x2000: SVC #0 (0xD4000001)
    std::vector<std::uint32_t> code = {
        0xD4000001u, // SVC #0
        enc_nop()
    };
    load_code(mem, cpu, code, 0x2000);

    // Step 1: execute SVC #0 -> takes exception to 0x10200
    cpu.step(mem);
    CHECK(cpu.get_pmu().read_pmevcntr(0) == 1); // EXC_TAKEN incremented
    CHECK(cpu.get_pmu().read_pmevcntr(1) == 0); // Not yet returned

    // Step 2: execute ERET at 0x10200 -> returns to 0x2004
    cpu.step(mem);
    CHECK(cpu.get_pmu().read_pmevcntr(1) == 1); // EXC_RETURN incremented
    CHECK(cpu.get_program_counter() == 0x2004);
}

// Test 11: Privilege filtering (P, U, NSH, M bits)
static void test_privilege_filtering_el0_el1() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_architectural_exceptions(true);

    // Configure counter 0 for INST_RETIRED with Filter P (bit 31 = inhibit EL1)
    cpu.get_pmu().write_pmcr(winios::kPMCREnable);
    cpu.get_pmu().write_pmcntenset(1u << 0u);
    cpu.get_pmu().write_pmevtyper(0, winios::ARMV8_EVENT_INST_RETIRED | winios::kFilterP);
    cpu.get_pmu().write_pmevcntr(0, 0);

    // Configure cycle counter with Filter P
    cpu.get_pmu().write_pmcntenset(1u << 31u);
    cpu.get_pmu().write_pmccfiltr(winios::kFilterP);
    cpu.get_pmu().write_pmccntr(0);

    // Execute at EL1: should NOT count because P bit is set
    cpu.set_current_el(1);
    std::vector<std::uint32_t> code(5, enc_nop());
    load_code(mem, cpu, code);
    for (std::size_t i = 0; i < 5; ++i) {
        cpu.step(mem);
    }
    CHECK(cpu.get_pmu().read_pmevcntr(0) == 0);
    CHECK(cpu.get_pmu().read_pmccntr() == 0);

    // Now change filter to Filter U (bit 30 = inhibit EL0, allow EL1)
    cpu.get_pmu().write_pmevtyper(0, winios::ARMV8_EVENT_INST_RETIRED | winios::kFilterU);
    cpu.get_pmu().write_pmccfiltr(winios::kFilterU);
    cpu.set_program_counter(0x1000);

    for (std::size_t i = 0; i < 5; ++i) {
        cpu.step(mem);
    }
    // Now both must count!
    CHECK(cpu.get_pmu().read_pmevcntr(0) == 5);
    CHECK(cpu.get_pmu().read_pmccntr() == 5);
}

// Test 12: Cycle counter overflow (32-bit vs 64-bit LC mode)
static void test_cycle_counter_overflow_32bit_and_64bit() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);

    // Case A: 32-bit mode (LC = 0)
    cpu.get_pmu().write_pmcr(winios::kPMCREnable);
    cpu.get_pmu().write_pmcntenset(1u << 31u);
    cpu.get_pmu().write_pmccntr(0xFFFFFFFFULL);
    cpu.get_pmu().write_pmovsclr(1u << 31u);

    std::vector<std::uint32_t> code(2, enc_nop());
    load_code(mem, cpu, code);
    cpu.step(mem);

    // Overflows bit 31
    CHECK((cpu.get_pmu().read_pmovsset() & (1u << 31u)) != 0u);

    // Clear overflow flag
    cpu.get_pmu().write_pmovsclr(1u << 31u);
    CHECK((cpu.get_pmu().read_pmovsset() & (1u << 31u)) == 0u);

    // Case B: 64-bit mode (LC = 1)
    cpu.set_program_counter(0x1000);
    cpu.get_pmu().write_pmcr(winios::kPMCREnable | winios::kPMCRLongCycles);
    cpu.get_pmu().write_pmccntr(0xFFFFFFFFFFFFFFFFULL);
    cpu.step(mem);

    CHECK((cpu.get_pmu().read_pmovsset() & (1u << 31u)) != 0u);
}

// Test 13: Event counter overflow and PMOVSSET / PMOVSCLR
static void test_event_counter_overflow() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);

    // Counter 0: preset to 0xFFFFFFFF, event INST_RETIRED
    cpu.get_pmu().write_pmcr(winios::kPMCREnable);
    cpu.get_pmu().write_pmcntenset(1u << 0u);
    cpu.get_pmu().write_pmevtyper(0, winios::ARMV8_EVENT_INST_RETIRED);
    cpu.get_pmu().write_pmevcntr(0, 0xFFFFFFFFu);
    cpu.get_pmu().write_pmovsclr(0x7Fu);

    std::vector<std::uint32_t> code(2, enc_nop());
    load_code(mem, cpu, code);
    cpu.step(mem);

    // Counter 0 wraps to 0 and asserts overflow bit 0
    CHECK(cpu.get_pmu().read_pmevcntr(0) == 0);
    CHECK((cpu.get_pmu().read_pmovsset() & (1u << 0u)) != 0u);

    // Clear overflow bit 0 via PMOVSCLR_EL0
    cpu.get_pmu().write_pmovsclr(1u << 0u);
    CHECK((cpu.get_pmu().read_pmovsset() & (1u << 0u)) == 0u);

    // Software injection via PMOVSSET_EL0
    cpu.get_pmu().write_pmovsset(1u << 0u);
    CHECK((cpu.get_pmu().read_pmovsset() & (1u << 0u)) != 0u);
}

// Test 14: Overflow interrupt generation and clearing
static void test_interrupt_generation_and_clearing() {
    CPU cpu;
    cpu.get_pmu().reset();

    // Enable interrupt for counter 1 via PMINTENSET_EL1
    cpu.get_pmu().write_pmintenset(1u << 1u);
    CHECK((cpu.get_pmu().read_pmintenset() & (1u << 1u)) != 0u);
    CHECK(!cpu.get_pmu().has_pending_interrupt()); // No overflow yet

    // Trigger overflow on counter 1
    cpu.get_pmu().write_pmovsset(1u << 1u);
    CHECK(cpu.get_pmu().has_pending_interrupt()); // Interrupt active!

    // Clear overflow via PMOVSCLR_EL0
    cpu.get_pmu().write_pmovsclr(1u << 1u);
    CHECK(!cpu.get_pmu().has_pending_interrupt()); // Deasserted!

    // Re-trigger overflow, but disable interrupt via PMINTENCLR_EL1
    cpu.get_pmu().write_pmovsset(1u << 1u);
    CHECK(cpu.get_pmu().has_pending_interrupt());
    cpu.get_pmu().write_pmintenclr(1u << 1u);
    CHECK(!cpu.get_pmu().has_pending_interrupt()); // Deasserted by masking!
}

// Test 15: GICv3 PPI 23 integration
static void test_gicv3_ppi23_integration() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_daif(0); // unmask IRQ

    // Configure GICv3: enable GICD, GICR, Group 1, PMR=0xF0
    cpu.get_gic().write_mmio(winios::GICv3::kGicdBase + 0x0000, 0x3u, 4);
    cpu.get_gic().write_icc_sre_el1(1, 0);
    cpu.get_gic().write_icc_igrpen1(1, 0);
    cpu.get_gic().write_icc_pmr(0xF0, 0);
    cpu.get_gic().write_mmio(0x080B0080, 1u << 23, 4);  // Group 1
    cpu.get_gic().write_mmio(0x080B0400 + 23, 0x30, 1); // Priority 0x30
    cpu.get_gic().write_mmio(0x080B0100, 1u << 23, 4);  // Enable PPI 23

    // Assert PMU overflow + interrupt enable
    cpu.get_pmu().write_pmintenset(1u << 31u); // Cycle counter
    cpu.get_pmu().write_pmovsset(1u << 31u);
    CHECK(cpu.get_pmu().has_pending_interrupt());

    // Evaluate interrupts via check_pending_interrupts
    // check_pending_interrupts routes pmu_irq -> set_ppi_level(23, true, core_id_)
    cpu.check_pending_interrupts();

    // Verify GICv3 evaluates pending interrupt
    auto sig = cpu.get_gic().evaluate(1, 0, 0);
    CHECK(sig.has_interrupt);

    // Deassert PMU interrupt
    cpu.get_pmu().write_pmovsclr(1u << 31u);
    cpu.check_pending_interrupts();
    sig = cpu.get_gic().evaluate(1, 0, 0);
    CHECK(!sig.has_interrupt);
}

// Test 16: EL0 access control via PMUSERENR_EL0
static void test_el0_access_pmuserenr() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_architectural_exceptions(true);

    // When at EL0 and PMUSERENR_EL0 == 0: MRS PMCCNTR_EL0 must raise undefined instruction
    cpu.set_current_el(0);
    cpu.get_pmu().write_pmuserenr(0);

    std::vector<std::uint32_t> code = {
        enc_mrs(0, 3, 9, 13, 0), // MRS X0, PMCCNTR_EL0
        enc_nop()
    };
    load_code(mem, cpu, code);
    cpu.step(mem);
    // Should have raised an exception to EL1
    CHECK(cpu.get_current_el() == 1);

    // Now test PMUSERENR.CR = 1 (cycle counter read enabled at EL0)
    cpu.set_current_el(0);
    cpu.get_pmu().write_pmuserenr(winios::kPMUserCycleRead);
    cpu.get_pmu().write_pmccntr(777);
    load_code(mem, cpu, code);
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 0); // Succeeded at EL0!
    CHECK(cpu.read_register(0) == 777);

    // But event counter read must still fail if ER = 0
    std::vector<std::uint32_t> code2 = {
        enc_mrs(1, 3, 14, 8, 0), // MRS X1, PMEVCNTR0_EL0
        enc_nop()
    };
    load_code(mem, cpu, code2);
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1); // Trapped!

    // Enable ER bit
    cpu.set_current_el(0);
    cpu.get_pmu().write_pmuserenr(winios::kPMUserCycleRead | winios::kPMUserEventRead);
    cpu.get_pmu().write_pmevcntr(0, 999);
    load_code(mem, cpu, code2);
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 0);
    CHECK(cpu.read_register(1) == 999);
}

// Test 17: EL2 and EL3 traps (MDCR_EL2.TPM / MDCR_EL3.TPM)
static void test_el2_and_el3_traps() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_architectural_exceptions(true);

    // At EL1, configure MDCR_EL2.TPM (bit 6 = 1) -> traps PMU access to EL2
    cpu.set_current_el(1);
    cpu.set_mdcr_el2(1u << 6u);

    std::vector<std::uint32_t> code = {
        enc_mrs(0, 3, 9, 12, 0), // MRS X0, PMCR_EL0
        enc_nop()
    };
    load_code(mem, cpu, code);
    cpu.step(mem);

    // Trapped to EL2!
    CHECK(cpu.get_current_el() == 2);
    CHECK(((cpu.get_esr_el2() >> 26u) & 0x3Fu) == 0x18u); // EC = 0x18 (Trapped MSR/MRS)

    // At EL1, configure MDCR_EL3.TPM (bit 6 = 1) -> traps PMU access to EL3
    cpu.set_mdcr_el2(0);
    cpu.set_current_el(1);
    cpu.set_mdcr_el3(1u << 6u);

    load_code(mem, cpu, code);
    cpu.step(mem);

    // Trapped to EL3!
    CHECK(cpu.get_current_el() == 3);
    CHECK(((cpu.get_esr_el3() >> 26u) & 0x3Fu) == 0x18u);
}

// Test 18: Identification registers PMCEID0_EL0 and PMCEID1_EL0
static void test_identification_registers() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);

    std::vector<std::uint32_t> code = {
        enc_mrs(0, 3, 9, 12, 6), // MRS X0, PMCEID0_EL0
        enc_mrs(1, 3, 9, 12, 7)  // MRS X1, PMCEID1_EL0
    };
    load_code(mem, cpu, code);
    cpu.step(mem);
    cpu.step(mem);

    CHECK(cpu.read_register(0) == winios::kPMCEID0Value);
    CHECK(cpu.read_register(1) == winios::kPMCEID1Value);
}

// Test 19: Per-core PMU state isolation in SMPCluster
static void test_smp_per_core_pmu_isolation() {
    winios::SMPCluster cluster(2);
    cluster.get_core(0).set_architectural_exceptions(true);
    cluster.get_core(1).set_architectural_exceptions(true);
    cluster.get_core(0).set_current_el(1);
    cluster.get_core(1).set_current_el(1);

    // Core 0: enable cycle counter
    cluster.get_core(0).get_pmu().write_pmcr(winios::kPMCREnable);
    cluster.get_core(0).get_pmu().write_pmcntenset(1u << 31u);
    cluster.get_core(0).get_pmu().write_pmccntr(0);

    // Core 1: enable cycle counter with different initial value
    cluster.get_core(1).get_pmu().write_pmcr(winios::kPMCREnable);
    cluster.get_core(1).get_pmu().write_pmcntenset(1u << 31u);
    cluster.get_core(1).get_pmu().write_pmccntr(1000);

    // Step Core 0 with 5 NOPs
    Memory& mem = cluster.get_memory();
    std::vector<std::uint32_t> code(5, enc_nop());
    load_code(mem, cluster.get_core(0), code, 0x1000);
    for (std::size_t i = 0; i < 5; ++i) {
        cluster.get_core(0).step(mem);
    }

    // Core 0 must be 5, Core 1 must remain 1000!
    CHECK(cluster.get_core(0).get_pmu().read_pmccntr() == 5);
    CHECK(cluster.get_core(1).get_pmu().read_pmccntr() == 1000);
}

// Test 20: PSCI lifecycle and PMU state reset
static void test_psci_lifecycle_pmu_state() {
    winios::SMPCluster cluster(2);
    cluster.enable_psci();

    // Dirty Core 1 PMU before start
    cluster.get_core(1).get_pmu().write_pmcr(winios::kPMCREnable);
    cluster.get_core(1).get_pmu().write_pmcntenset(0x7F);
    cluster.get_core(1).get_pmu().write_pmccntr(99999);

    // Start Core 1 via PSCI CPU_ON
    CPU& cpu0 = cluster.get_core(0);
    cpu0.write_register(0, winios::PSCIService::kCpuOn64);
    cpu0.write_register(1, 1);
    cpu0.write_register(2, 0x4000);
    cpu0.write_register(3, 0);
    bool handled = cluster.get_psci_service()->handle_smc(cpu0, 0);
    CHECK(handled);
    CHECK(static_cast<std::int64_t>(cpu0.read_register(0)) == winios::PSCIService::kSuccess);

    // Core 1 PMU must start in architectural reset state!
    CHECK(cluster.get_core(1).get_pmu().read_pmccntr() == 0);
    CHECK(cluster.get_core(1).get_pmu().read_pmcntenset() == 0);
}

// Test 21: Deterministic scheduler reproducibility
static void test_deterministic_scheduler_pmu_reproducibility() {
    winios::SMPCluster cluster1(2);
    cluster1.get_core(0).set_architectural_exceptions(true);
    cluster1.get_core(0).set_current_el(1);
    cluster1.get_core(0).get_pmu().write_pmcr(winios::kPMCREnable);
    cluster1.get_core(0).get_pmu().write_pmcntenset((1u << 31u) | (1u << 0u));
    cluster1.get_core(0).get_pmu().write_pmevtyper(0, winios::ARMV8_EVENT_INST_RETIRED);

    Memory& mem1 = cluster1.get_memory();
    std::vector<std::uint32_t> code(50, enc_nop());
    load_code(mem1, cluster1.get_core(0), code, 0x1000);
    for (std::size_t i = 0; i < 50; ++i) {
        cluster1.get_core(0).step(mem1);
    }
    const std::uint64_t cycles1 = cluster1.get_core(0).get_pmu().read_pmccntr();
    const std::uint32_t insts1 = cluster1.get_core(0).get_pmu().read_pmevcntr(0);

    // Run 2: exact same sequence
    winios::SMPCluster cluster2(2);
    cluster2.get_core(0).set_architectural_exceptions(true);
    cluster2.get_core(0).set_current_el(1);
    cluster2.get_core(0).get_pmu().write_pmcr(winios::kPMCREnable);
    cluster2.get_core(0).get_pmu().write_pmcntenset((1u << 31u) | (1u << 0u));
    cluster2.get_core(0).get_pmu().write_pmevtyper(0, winios::ARMV8_EVENT_INST_RETIRED);

    Memory& mem2 = cluster2.get_memory();
    load_code(mem2, cluster2.get_core(0), code, 0x1000);
    for (std::size_t i = 0; i < 50; ++i) {
        cluster2.get_core(0).step(mem2);
    }
    const std::uint64_t cycles2 = cluster2.get_core(0).get_pmu().read_pmccntr();
    const std::uint32_t insts2 = cluster2.get_core(0).get_pmu().read_pmevcntr(0);

    CHECK(cycles1 == cycles2);
    CHECK(insts1 == insts2);
    CHECK(cycles1 == 50);
    CHECK(insts1 == 50);
}

int main() {
    std::cout << "Running WinIOS Phase 49 PMU unit tests...\n";

    test_pmcr_control_and_reset();
    std::cout << "  [PASS] test_pmcr_control_and_reset\n";

    test_pmcnten_set_and_clear();
    std::cout << "  [PASS] test_pmcnten_set_and_clear\n";

    test_pmccntr_cycle_counting();
    std::cout << "  [PASS] test_pmccntr_cycle_counting\n";

    test_pmccntr_divider_by_64();
    std::cout << "  [PASS] test_pmccntr_divider_by_64\n";

    test_pmselr_and_pmxev();
    std::cout << "  [PASS] test_pmselr_and_pmxev\n";

    test_direct_pmevcntr_and_pmevtyper();
    std::cout << "  [PASS] test_direct_pmevcntr_and_pmevtyper\n";

    test_event_counting_inst_retired();
    std::cout << "  [PASS] test_event_counting_inst_retired\n";

    test_event_counting_sw_incr();
    std::cout << "  [PASS] test_event_counting_sw_incr\n";

    test_event_counting_mem_access();
    std::cout << "  [PASS] test_event_counting_mem_access\n";

    test_event_counting_exc_taken_and_return();
    std::cout << "  [PASS] test_event_counting_exc_taken_and_return\n";

    test_privilege_filtering_el0_el1();
    std::cout << "  [PASS] test_privilege_filtering_el0_el1\n";

    test_cycle_counter_overflow_32bit_and_64bit();
    std::cout << "  [PASS] test_cycle_counter_overflow_32bit_and_64bit\n";

    test_event_counter_overflow();
    std::cout << "  [PASS] test_event_counter_overflow\n";

    test_interrupt_generation_and_clearing();
    std::cout << "  [PASS] test_interrupt_generation_and_clearing\n";

    test_gicv3_ppi23_integration();
    std::cout << "  [PASS] test_gicv3_ppi23_integration\n";

    test_el0_access_pmuserenr();
    std::cout << "  [PASS] test_el0_access_pmuserenr\n";

    test_el2_and_el3_traps();
    std::cout << "  [PASS] test_el2_and_el3_traps\n";

    test_identification_registers();
    std::cout << "  [PASS] test_identification_registers\n";

    test_smp_per_core_pmu_isolation();
    std::cout << "  [PASS] test_smp_per_core_pmu_isolation\n";

    test_psci_lifecycle_pmu_state();
    std::cout << "  [PASS] test_psci_lifecycle_pmu_state\n";

    test_deterministic_scheduler_pmu_reproducibility();
    std::cout << "  [PASS] test_deterministic_scheduler_pmu_reproducibility\n";

    std::cout << "\nAll 21 WinIOS Phase 49 PMU unit tests PASSED successfully!\n";
    return 0;
}

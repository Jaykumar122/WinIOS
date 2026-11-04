#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/smp_cluster.h"
#include "cpu/synchronous_exception.h"
#include "memory/memory.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__  \
                      << ": " #cond << std::endl;                              \
            std::exit(1);                                                      \
        }                                                                      \
    } while (false)

static void test_el3_execution_state() {
    CPU cpu;
    // Default boot state is EL1 with SPSel=1
    CHECK(cpu.get_current_el() == 1);
    CHECK(cpu.get_sp_sel() == true);

    // Switch to EL3
    cpu.set_current_el(3);
    cpu.set_sp_sel(true);
    CHECK(cpu.get_current_el() == 3);
    CHECK(cpu.get_sp_sel() == true);
    CHECK(cpu.is_secure_state() == true);

    // Banked stack pointers
    cpu.set_sp_el0(0x1000);
    cpu.set_sp_el1(0x2000);
    cpu.set_sp_el2(0x3000);
    cpu.set_sp_el3(0x4000);

    // In EL3 with SPSel=1, get_stack_pointer() returns SP_EL3
    CHECK(cpu.get_stack_pointer() == 0x4000);
    cpu.set_stack_pointer(0x4500);
    CHECK(cpu.get_sp_el3() == 0x4500);
    CHECK(cpu.get_stack_pointer() == 0x4500);

    // In EL3 with SPSel=0, get_stack_pointer() returns SP_EL0
    cpu.set_sp_sel(false);
    CHECK(cpu.get_stack_pointer() == 0x1000);
    cpu.set_stack_pointer(0x1500);
    CHECK(cpu.get_sp_el0() == 0x1500);
    CHECK(cpu.get_stack_pointer() == 0x1500);
    CHECK(cpu.get_sp_el3() == 0x4500);

    // Reset back
    cpu.set_sp_sel(true);
    cpu.set_current_el(1);
}

static void test_el3_system_registers_rw_and_traps() {
    CPU cpu;
    Memory memory(0x10000);
    cpu.set_architectural_exceptions(true);

    // Helper: assemble MRS X0, <reg> or MSR <reg>, X0
    // SCR_EL3:   0x7088 -> MRS: 0xD53E1100, MSR: 0xD51E1100
    // CPTR_EL3:  0x708A -> MRS: 0xD53E1140, MSR: 0xD51E1140
    // MDCR_EL3:  0x7099 -> MRS: 0xD53E1320, MSR: 0xD51E1320
    // SPSR_EL3:  0x7200 -> MRS: 0xD53E4000, MSR: 0xD51E4000
    // ELR_EL3:   0x7201 -> MRS: 0xD53E4020, MSR: 0xD51E4020
    // ESR_EL3:   0x7290 -> MRS: 0xD53E5200, MSR: 0xD51E5200
    // FAR_EL3:   0x7300 -> MRS: 0xD53E6000, MSR: 0xD51E6000
    // VBAR_EL3:  0x7600 -> MRS: 0xD53EC000, MSR: 0xD51EC000

    // Test 2a: Trapping from EL1 to Undefined Instruction
    cpu.set_current_el(1);
    cpu.set_vbar_el1(0x1000);
    cpu.set_program_counter(0x100);
    // Execute MRS X0, SCR_EL3 at EL1
    memory.write_u32(0x100, 0xD53E1100u);
    cpu.step(memory);

    // Should have taken Undefined Instruction to EL1 vector table (+0x200 for EL1h)
    CHECK(cpu.get_program_counter() == 0x1200);
    CHECK((cpu.get_esr_el1() >> 26u) == 0x00u); // EC 0x00 = Unknown / Undefined

    // Test 2b: Trapping from EL2 to Undefined Instruction
    cpu.set_current_el(2);
    cpu.set_vbar_el2(0x2000);
    cpu.set_program_counter(0x200);
    // Execute MSR SCR_EL3, X0 at EL2
    memory.write_u32(0x200, 0xD51E1100u);
    cpu.step(memory);

    // Should have taken Undefined Instruction to EL2 vector table (+0x200 for EL2h)
    CHECK(cpu.get_program_counter() == 0x2200);
    CHECK((cpu.get_esr_el2() >> 26u) == 0x00u); // EC 0x00 = Unknown / Undefined

    // Test 2c: Valid read/write at EL3
    cpu.set_current_el(3);
    cpu.set_program_counter(0x300);

    // Write to SCR_EL3
    cpu.write_register(0, 0x501ULL);
    memory.write_u32(0x300, 0xD51E1100u); // MSR SCR_EL3, X0
    cpu.step(memory);
    CHECK(cpu.get_scr_el3() == 0x501ULL);

    // Read back from SCR_EL3 into X1
    memory.write_u32(0x304, 0xD53E1101u); // MRS X1, SCR_EL3
    cpu.step(memory);
    CHECK(cpu.read_register(1) == 0x501ULL);

    // Test SPSR_EL3, ELR_EL3, ESR_EL3, FAR_EL3, VBAR_EL3
    cpu.write_register(0, 0x60000000ULL);
    memory.write_u32(0x308, 0xD51E4000u); // MSR SPSR_EL3, X0
    cpu.step(memory);
    CHECK(cpu.get_spsr_el3() == 0x60000000u);

    cpu.write_register(0, 0xDEADBEEFULL);
    memory.write_u32(0x30C, 0xD51E4020u); // MSR ELR_EL3, X0
    cpu.step(memory);
    CHECK(cpu.get_elr_el3() == 0xDEADBEEFULL);

    cpu.write_register(0, 0x17000000ULL);
    memory.write_u32(0x310, 0xD51E5200u); // MSR ESR_EL3, X0
    cpu.step(memory);
    CHECK(cpu.get_esr_el3() == 0x17000000u);

    cpu.write_register(0, 0x80004000ULL);
    memory.write_u32(0x314, 0xD51E6000u); // MSR FAR_EL3, X0
    cpu.step(memory);
    CHECK(cpu.get_far_el3() == 0x80004000ULL);

    cpu.write_register(0, 0x40000000ULL);
    memory.write_u32(0x318, 0xD51EC000u); // MSR VBAR_EL3, X0
    cpu.step(memory);
    CHECK(cpu.get_vbar_el3() == 0x40000000ULL);
}

static void test_scr_el3_controls() {
    CPU cpu;
    Memory memory(0x10000);
    cpu.set_architectural_exceptions(true);

    // 3a. SCR_EL3.NS bit control
    cpu.set_current_el(3);
    cpu.set_scr_el3(0x500ULL); // NS = 0 (Secure state)
    CHECK(cpu.is_secure_state() == true);
    cpu.set_current_el(1);
    CHECK(cpu.is_secure_state() == true); // EL1 in Secure state when NS=0

    cpu.set_current_el(3);
    cpu.set_scr_el3(0x501ULL); // NS = 1 (Non-secure state)
    cpu.set_current_el(1);
    CHECK(cpu.is_secure_state() == false); // EL1 in Non-secure state when NS=1

    // 3b. SCR_EL3.SMD bit: SMC disable
    cpu.set_current_el(3);
    cpu.set_scr_el3(0x501ULL | (1ULL << 7u)); // SMD = 1 (Disable SMC)
    cpu.set_current_el(1);
    cpu.set_vbar_el1(0x1000);
    cpu.set_program_counter(0x500);

    // SMC #0: 0xD4000003u
    memory.write_u32(0x500, 0xD4000003u);
    cpu.step(memory);

    // Should generate Undefined Instruction exception at EL1
    CHECK(cpu.get_current_el() == 1);
    CHECK(cpu.get_program_counter() == 0x1200); // EL1h vector
    CHECK((cpu.get_esr_el1() >> 26u) == 0x00u); // EC 0x00 = Unknown

    // 3c. SCR_EL3.HCE bit: HVC disable at EL1
    cpu.set_current_el(3);
    cpu.set_scr_el3(0x501ULL & ~(1ULL << 8u)); // HCE = 0 (Disable HVC)
    cpu.set_current_el(1);
    cpu.set_vbar_el1(0x1000);
    cpu.set_program_counter(0x600);

    // HVC #0: 0xD4000002u
    memory.write_u32(0x600, 0xD4000002u);
    cpu.step(memory);

    // Should generate Undefined Instruction exception at EL1
    CHECK(cpu.get_current_el() == 1);
    CHECK(cpu.get_program_counter() == 0x1200);
    CHECK((cpu.get_esr_el1() >> 26u) == 0x00u);
}

static void test_smc_instruction_execution_and_syndrome() {
    CPU cpu;
    Memory memory(0x10000);
    cpu.set_architectural_exceptions(true);

    // 4a. SMC from EL0 is always undefined
    cpu.set_current_el(0);
    cpu.set_vbar_el1(0x1000);
    cpu.set_program_counter(0x100);
    memory.write_u32(0x100, 0xD4000003u); // SMC #0
    cpu.step(memory);

    CHECK(cpu.get_current_el() == 1);
    CHECK(cpu.get_program_counter() == 0x1400); // Lower EL AArch64 vector offset +0x400
    CHECK((cpu.get_esr_el1() >> 26u) == 0x00u);

    // 4b. SMC from EL1 to EL3 with imm16 = 0x42
    cpu.set_current_el(1);
    cpu.set_scr_el3(0x501ULL); // SMD = 0, HCE = 1, RW = 1, NS = 1
    cpu.set_vbar_el3(0x8000);
    cpu.set_program_counter(0x200);

    // SMC #0x42: bits[20:5] = 0x42 -> (0x42 << 5) | 3 | 0xD4000000 = 0xD4000843u
    const std::uint32_t smc_imm = 0xD4000003u | (0x42u << 5u);
    memory.write_u32(0x200, smc_imm);
    cpu.step(memory);

    // Exception taken to EL3
    CHECK(cpu.get_current_el() == 3);
    CHECK(cpu.get_program_counter() == 0x8400); // Lower EL vector at VBAR_EL3 + 0x400
    CHECK(cpu.get_elr_el3() == 0x204);          // Preferred return address = PC + 4
    CHECK((cpu.get_esr_el3() >> 26u) == 0x17u); // EC 0x17 = SmcAArch64
    CHECK((cpu.get_esr_el3() & 0xFFFFu) == 0x42u); // imm16 = 0x42
    CHECK((cpu.get_esr_el3() & (1u << 25u)) != 0u); // IL = 1 (32-bit instruction)

    // 4c. HCR_EL2.TSC trap: SMC at EL1 traps to EL2
    cpu.set_current_el(1);
    cpu.set_hcr_el2(1ULL << 19u); // TSC bit = 1
    cpu.set_vbar_el2(0x4000);
    cpu.set_program_counter(0x300);
    memory.write_u32(0x300, 0xD4000003u); // SMC #0
    cpu.step(memory);

    // Trapped to EL2!
    CHECK(cpu.get_current_el() == 2);
    CHECK(cpu.get_program_counter() == 0x4400); // Lower EL vector at VBAR_EL2 + 0x400
    CHECK(cpu.get_elr_el2() == 0x300);          // Trapped preferred return = PC
    CHECK((cpu.get_esr_el2() >> 26u) == 0x17u); // EC 0x17 = SmcAArch64
}

static void test_secure_monitor_dispatch_and_roundtrip() {
    CPU cpu;
    Memory memory(0x20000);
    cpu.set_architectural_exceptions(true);

    // Setup guest at EL1
    cpu.set_current_el(1);
    cpu.set_scr_el3(0x501ULL);
    cpu.set_vbar_el3(0x10000);

    // Guest code at 0x1000:
    // MOV X0, #0x8400, LSL #16 (0x84000000: PSCI_VERSION function ID)
    // SMC #0
    // ADD X1, X0, #1
    memory.write_u32(0x1000, 0xD2800000u | (1u << 21u) | (0x8400u << 5u)); // MOV X0, 0x84000000
    memory.write_u32(0x1004, 0xD4000003u);                   // SMC #0
    memory.write_u32(0x1008, 0x91000401u);                   // ADD X1, X0, #1

    // EL3 Secure Monitor handler at VBAR_EL3 + 0x400 (0x10400):
    // If X0 == 0x84000000, set X0 = 0x00010002 (PSCI v1.2)
    // ERET (0xD69F03E0u)
    // We assemble:
    // MOV X0, #0x0002
    // MOVK X0, #0x0001, LSL #16
    // ERET
    memory.write_u32(0x10400, 0xD2800040u); // MOV X0, #2
    memory.write_u32(0x10404, 0xF2A00020u); // MOVK X0, #1, LSL #16 -> 0x00010002
    memory.write_u32(0x10408, 0xD69F03E0u); // ERET

    // Run guest to SMC
    cpu.set_program_counter(0x1000);
    cpu.step(memory); // MOV X0
    CHECK(cpu.read_register(0) == 0x84000000ULL);
    CHECK(cpu.get_current_el() == 1);

    cpu.step(memory); // SMC #0 -> vectors to EL3 at 0x10400
    CHECK(cpu.get_current_el() == 3);
    CHECK(cpu.get_program_counter() == 0x10400);
    CHECK(cpu.get_elr_el3() == 0x1008);

    // Execute Monitor handler
    cpu.step(memory); // MOV X0, #2
    cpu.step(memory); // MOVK X0, #1, LSL #16
    CHECK(cpu.read_register(0) == 0x00010002ULL);

    cpu.step(memory); // ERET -> returns to EL1 at 0x1008
    CHECK(cpu.get_current_el() == 1);
    CHECK(cpu.get_program_counter() == 0x1008);

    // Execute next guest instruction
    cpu.step(memory); // ADD X1, X0, #1
    CHECK(cpu.read_register(1) == 0x00010003ULL);
}

static void test_eret_from_el3() {
    CPU cpu;
    Memory memory(0x10000);
    cpu.set_architectural_exceptions(true);

    // 6a. Valid ERET from EL3 to EL2h (M = 0x09)
    cpu.set_current_el(3);
    cpu.set_elr_el3(0x2000);
    cpu.set_spsr_el3(0x09u); // M = EL2h
    memory.write_u32(0x100, 0xD69F03E0u); // ERET
    cpu.set_program_counter(0x100);
    cpu.step(memory);

    CHECK(cpu.get_current_el() == 2);
    CHECK(cpu.get_sp_sel() == true);
    CHECK(cpu.get_program_counter() == 0x2000);
    CHECK(cpu.get_il() == false);

    // 6b. Valid ERET from EL3 to EL1h (M = 0x05)
    cpu.set_current_el(3);
    cpu.set_elr_el3(0x3000);
    cpu.set_spsr_el3(0x05u); // M = EL1h
    memory.write_u32(0x200, 0xD69F03E0u); // ERET
    cpu.set_program_counter(0x200);
    cpu.step(memory);

    CHECK(cpu.get_current_el() == 1);
    CHECK(cpu.get_sp_sel() == true);
    CHECK(cpu.get_program_counter() == 0x3000);
    CHECK(cpu.get_il() == false);

    // 6c. Valid ERET from EL3 to EL0t (M = 0x00)
    cpu.set_current_el(3);
    cpu.set_elr_el3(0x4000);
    cpu.set_spsr_el3(0x00u); // M = EL0t
    memory.write_u32(0x300, 0xD69F03E0u); // ERET
    cpu.set_program_counter(0x300);
    cpu.step(memory);

    CHECK(cpu.get_current_el() == 0);
    CHECK(cpu.get_sp_sel() == false);
    CHECK(cpu.get_program_counter() == 0x4000);
    CHECK(cpu.get_il() == false);

    // 6d. Invalid ERET: reserved M field (0x0F) -> triggers PSTATE.IL
    cpu.set_current_el(3);
    cpu.set_elr_el3(0x5000);
    cpu.set_spsr_el3(0x0Fu); // Invalid M
    memory.write_u32(0x400, 0xD69F03E0u); // ERET
    cpu.set_program_counter(0x400);
    cpu.step(memory);

    CHECK(cpu.get_il() == true);
    CHECK(cpu.get_program_counter() == 0x5000);
}

static void test_interrupt_routing_to_el3() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el3(0x8000);

    // 7a. Non-GIC timer interrupt with SCR_EL3.IRQ = 1
    cpu.set_current_el(1);
    cpu.set_daif(0x0u); // Unmask all interrupts
    cpu.set_scr_el3(0x501ULL | (1ULL << 1u)); // SCR_EL3.IRQ = 1

    // Program physical timer to fire immediately
    cpu.set_cntfrq_el0(1000000);
    cpu.set_guest_counter(100);
    CPU::TimerState timer;
    timer.enable = true;
    timer.imask = false;
    timer.cval = 50; // counter >= cval -> istatus = true
    cpu.set_phys_timer(timer);

    bool pending = cpu.check_pending_interrupts();
    CHECK(pending == true);
    CHECK(cpu.get_current_el() == 3);
    // Lower EL IRQ vector at VBAR_EL3 + 0x480
    CHECK(cpu.get_program_counter() == 0x8480);

    // 7b. GICv3 routing with SCR_EL3.FIQ = 1 (Group 0 routing)
    CPU cpu2;
    cpu2.set_architectural_exceptions(true);
    cpu2.set_vbar_el3(0x9000);
    cpu2.set_current_el(1);
    cpu2.set_daif(0x0u);
    cpu2.set_scr_el3(0x501ULL | (1ULL << 2u)); // SCR_EL3.FIQ = 1

    // Enable GICv3 CPU interface and distributor
    cpu2.get_gic().write_mmio(winios::GICv3::kGicdBase + 0x0000, 0x3u, 4); // Enable Grp0 and Grp1
    cpu2.get_gic().write_icc_sre_el1(1, 0);
    cpu2.get_gic().write_icc_igrpen0(1, 0);
    cpu2.get_gic().write_icc_igrpen1(1, 0);
    cpu2.get_gic().write_icc_pmr(0xF0, 0);

    // INTID 1 in Group 0 (clear bit 1 in GICR_IGROUPR0 at 0x080B0080)
    cpu2.get_gic().write_mmio(0x080B0080, 0, 4);
    cpu2.get_gic().write_mmio(0x080B0400 + 1, 0x30, 1); // Priority 0x30
    cpu2.get_gic().write_mmio(0x080B0100, 1u << 1, 4);   // Enable INTID 1

    // Assert INTID 1 via GICR_ISPENDR0 (0x080B0200)
    cpu2.get_gic().write_mmio(0x080B0200, 1u << 1, 4);

    bool gic_pending = cpu2.check_pending_interrupts();
    CHECK(gic_pending == true);
    CHECK(cpu2.get_current_el() == 3);
    // Lower EL FIQ vector at VBAR_EL3 + 0x500
    CHECK(cpu2.get_program_counter() == 0x9500);
}

static void test_multicore_el3_state_isolation() {
    winios::SMPCluster cluster(4, 0x100000);

    for (std::size_t i = 0; i < 4; ++i) {
        CPU& core = cluster.get_core(i);
        core.set_architectural_exceptions(true);
        core.set_current_el(3);
        core.set_scr_el3(0x500ULL | (i * 0x10ULL));
        core.set_sp_el3(0x10000ULL + i * 0x1000ULL);
        core.set_elr_el3(0x20000ULL + i * 0x1000ULL);
        core.set_spsr_el3(static_cast<std::uint32_t>(0x09u + i));
    }

    // Verify complete cross-core isolation
    for (std::size_t i = 0; i < 4; ++i) {
        const CPU& core = cluster.get_core(i);
        CHECK(core.get_current_el() == 3);
        CHECK(core.get_scr_el3() == (0x500ULL | (i * 0x10ULL)));
        CHECK(core.get_sp_el3() == (0x10000ULL + i * 0x1000ULL));
        CHECK(core.get_elr_el3() == (0x20000ULL + i * 0x1000ULL));
        CHECK(core.get_spsr_el3() == static_cast<std::uint32_t>(0x09u + i));
    }
}

static void test_smc_concurrency_and_stress() {
    // Stress test: 1,000 SMC entries and ERETs across multiple cores
    winios::SMPCluster cluster(4, 0x100000);

    for (std::size_t i = 0; i < 4; ++i) {
        CPU& core = cluster.get_core(i);
        core.set_architectural_exceptions(true);
        core.set_vbar_el3(0x80000);
        core.set_scr_el3(0x501ULL);
    }

    Memory& mem = cluster.get_memory();
    // Handler at 0x80400: ERET
    mem.write_u32(0x80400, 0xD69F03E0u);

    // Call site at 0x10000: SMC #0, followed by NOP
    mem.write_u32(0x10000, 0xD4000003u);
    mem.write_u32(0x10004, 0xD503201Fu); // NOP

    for (std::size_t round = 0; round < 250; ++round) {
        for (std::size_t i = 0; i < 4; ++i) {
            CPU& core = cluster.get_core(i);
            core.set_current_el(1);
            core.set_program_counter(0x10000);

            // Step 1: SMC
            core.step(mem);
            CHECK(core.get_current_el() == 3);
            CHECK(core.get_program_counter() == 0x80400);

            // Step 2: ERET
            core.step(mem);
            CHECK(core.get_current_el() == 1);
            CHECK(core.get_program_counter() == 0x10004);
        }
    }
}

int main() {
    std::cout << "Running WinIOS Phase 47 EL3 and Secure Monitor test suite..." << std::endl;

    test_el3_execution_state();
    std::cout << "  [PASS] Test 1: EL3 execution state and banked stack pointers" << std::endl;

    test_el3_system_registers_rw_and_traps();
    std::cout << "  [PASS] Test 2: EL3 system register access, read/write, and lower EL traps" << std::endl;

    test_scr_el3_controls();
    std::cout << "  [PASS] Test 3: SCR_EL3 control behavior (NS, SMD, HCE, RW)" << std::endl;

    test_smc_instruction_execution_and_syndrome();
    std::cout << "  [PASS] Test 4: SMC instruction execution, syndromes, and HCR_EL2 trap" << std::endl;

    test_secure_monitor_dispatch_and_roundtrip();
    std::cout << "  [PASS] Test 5: Secure Monitor SMC dispatch and roundtrip return" << std::endl;

    test_eret_from_el3();
    std::cout << "  [PASS] Test 6: ERET from EL3 to EL2, EL1, EL0 and PSTATE.IL handling" << std::endl;

    test_interrupt_routing_to_el3();
    std::cout << "  [PASS] Test 7: Interrupt routing to EL3 via SCR_EL3.FIQ and SCR_EL3.IRQ" << std::endl;

    test_multicore_el3_state_isolation();
    std::cout << "  [PASS] Test 8: Multicore EL3 state isolation across SMP cluster" << std::endl;

    test_smc_concurrency_and_stress();
    std::cout << "  [PASS] Test 9: SMC concurrency and stress validation (1,000 roundtrips)" << std::endl;

    std::cout << "All EL3 test suites passed successfully!" << std::endl;
    return 0;
}

// ============================================================================
// Stage 247.3: Guest-Driven Timer Interrupt Delivery & Exception Return Tests
// Exercises the complete CPU exception journey:
// 1. EL1 execution with generic virtual timer programmed (CNTV_CVAL_EL0).
// 2. Hardware interrupt generation when CNTVCT_EL0 >= CNTV_CVAL_EL0.
// 3. GICv3 PPI 27 interrupt evaluation and signaling to CPU.
// 4. Asynchronous exception entry to VBAR_EL1 + 0x280 (IRQ vector).
// 5. Guest handler reading ICC_IAR1_EL1 to acknowledge INTID 27.
// 6. Guest handler writing ICC_EOIR1_EL1 to signal completion.
// 7. Incrementing interrupt service counter and returning via ERET.
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/virtual_time.h"
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

static void test_guest_timer_interrupt_dispatch_and_eret() {
    std::cout << "[Stage 247.3 - Test 1] Guest timer interrupt delivery, IAR1/EOIR1, and ERET...\n";

    CPU cpu;
    Memory mem(0x200000); // 2 MB guest RAM
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);

    const uint64_t vbar = 0x80000;
    cpu.set_vbar_el1(vbar);

    // Setup GICv3 Distributor & Core 0 Redistributor
    GICv3::DistributorState dist{};
    dist.gicd_ctlr = 0x3u; // Enable Group 0 and Group 1
    cpu.get_gic().set_distributor_state(dist);

    auto& r = cpu.get_gic().get_redistributor(0);
    r.isenabler0 |= (1u << 27); // Enable Virtual Timer PPI 27
    r.igroupr0   |= (1u << 27); // Group 1
    r.ipriorityr[27] = 0x80u;   // Priority 0x80

    cpu.get_gic().write_icc_pmr(0xF0, 0); // PMR unmask (< 0xF0)
    cpu.get_gic().write_icc_igrpen1(1, 0); // Group 1 CPU interface enable

    // Setup Exception Vector at VBAR_EL1 + 0x280 (Current EL with SP_ELx IRQ)
    const uint64_t irq_vector = vbar + 0x280;
    // Handler:
    // 1. MRS X0, ICC_IAR1_EL1   (0xD538CC00u) -> acknowledge interrupt, X0 = 27
    // 2. MSR ICC_EOIR1_EL1, X0  (0xD518CC20u) -> end of interrupt
    // 3. ADD X2, X2, #1         (0x91000442u) -> increment counter in X2
    // 4. ERET                   (0xD69F03E0u) -> return from exception
    mem.write_u32(irq_vector + 0, 0xD538CC00u);
    mem.write_u32(irq_vector + 4, 0xD518CC20u);
    mem.write_u32(irq_vector + 8, 0x91000442u);
    mem.write_u32(irq_vector + 12, 0xD69F03E0u);

    // Setup Main Program at 0x10000:
    // 1. ADD X1, X1, #1  (0x91000421u)
    // 2. B -4 (loop)     (0x17FFFFFFu)
    const uint64_t code_pc = 0x10000;
    mem.write_u32(code_pc + 0, 0x91000421u); // ADD X1, X1, #1
    mem.write_u32(code_pc + 4, 0x17FFFFFFu); // B .-4 (loop back to 0x10000)

    cpu.set_program_counter(code_pc);
    cpu.write_register(1, 0); // X1 = loop counter
    cpu.write_register(2, 0); // X2 = IRQ serviced counter

    // Configure Virtual Timer:
    // Virtual counter starts at 100
    cpu.get_virtual_time().set_physical_counter(100);
    TimerState vt{};
    vt.enable = true;
    vt.imask = false;
    vt.cval = 105; // Expire when counter reaches 105
    cpu.get_virtual_time().set_virt_timer(0, vt);

    // Unmask DAIF.I (clear bit 1 of DAIF)
    cpu.set_daif(0x0);

    // Step 1: Counter = 100 (< 105), execute ADD X1, X1, #1
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(1) == 1, "Main loop executed instruction 1");
    TEST_ASSERT(cpu.read_register(2) == 0, "No interrupt serviced yet");

    // Advance virtual counter to 105 (timer comparator match!)
    cpu.get_virtual_time().set_physical_counter(105);

    // Step 2: CPU checks pending interrupts, takes IRQ exception to irq_vector!
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == irq_vector, "CPU branched to IRQ vector 0x80280");

    // Step 3: Vector instruction 0: MRS X0, ICC_IAR1_EL1
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 27, "ICC_IAR1_EL1 acknowledged INTID 27");

    // Step 4: Vector instruction 1: MSR ICC_EOIR1_EL1, X0
    // Before EOI, interrupt is active
    cpu.step(mem);

    // Step 5: Vector instruction 2: ADD X2, X2, #1
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(2) == 1, "Interrupt service counter incremented to 1");

    // Clear timer assertion so it does not immediately re-trigger after ERET
    vt.enable = false;
    cpu.get_virtual_time().set_virt_timer(0, vt);
    cpu.get_virtual_time().evaluate_core_timers(0);

    // Step 6: Vector instruction 3: ERET
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == code_pc + 4, "ERET successfully returned to interrupted PC (0x10004)");

    // Step 7: Execute branch instruction at 0x10004 (B .-4)
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == code_pc, "Loop branched back to 0x10000");

    // Step 8: Main loop resumes: execute ADD X1, X1, #1
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(1) == 2, "Main loop resumed and incremented loop counter to 2");
    TEST_ASSERT(cpu.read_register(2) == 1, "IRQ counter preserved across ERET");
}

int main() {
    std::cout << "=== Running Stage 247.3 Guest Timer & Interrupt Tests ===\n";
    test_guest_timer_interrupt_dispatch_and_eret();
    std::cout << "Stage 247.3: All guest timer and interrupt tests passed.\n";
    return 0;
}

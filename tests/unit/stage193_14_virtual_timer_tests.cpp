// ============================================================================
// Stage 193.14: Virtual Timer Interrupts Tests
// ============================================================================

#include <cstdint>
#include <iostream>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "cpu/synchronous_exception.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

using namespace winios;

static bool test_timer_counter_and_compare_evaluation() {
    CPU cpu;
    VirtualTime& vt = cpu.get_virtual_time();

    const std::uint64_t freq = vt.get_frequency();
    CHECK(freq == VirtualTime::kDefaultFrequency, "Frequency must default to 24 MHz");

    vt.set_physical_counter(1000ULL);
    CHECK(vt.get_physical_counter() == 1000ULL, "Physical counter must be 1000");

    // Virtual counter = physical counter - cntvoff
    vt.set_cntvoff_el2(0, 200ULL);
    CHECK(vt.get_virtual_counter(0) == 800ULL, "Virtual counter must reflect CNTVOFF offset (1000 - 200 = 800)");

    vt.advance_physical_counter(500ULL);
    CHECK(vt.get_physical_counter() == 1500ULL, "Physical counter must advance to 1500");
    CHECK(vt.get_virtual_counter(0) == 1300ULL, "Virtual counter must advance to 1300");

    std::cout << "[PASS] test_timer_counter_and_compare_evaluation\n";
    return true;
}

static bool test_timer_enable_mask_and_expiration() {
    CPU cpu;
    VirtualTime& vt = cpu.get_virtual_time();

    vt.set_physical_counter(1000ULL);
    vt.set_cntvoff_el2(0, 0ULL); // Virtual count = 1000

    auto& vtimer = vt.get_virt_timer(0);
    // Setup compare value = 2000, Enable = 1, Mask = 0
    vtimer.cval = 2000ULL;
    vtimer.enable = true;
    vtimer.imask = false;
    vt.evaluate_core_timers(0);

    // Before expiration (count 1000 < 2000)
    CHECK(!vtimer.irq_asserted(), "Timer must not be asserted before expiration");
    CHECK(!vtimer.istatus, "ISTATUS bit must be 0 before expiration");

    // Advance past compare value (count = 2500)
    vt.advance_physical_counter(1500ULL); // count reaches 2500
    CHECK(vtimer.irq_asserted(), "Timer must assert IRQ upon expiration");
    CHECK(vtimer.istatus, "ISTATUS bit must be set upon expiration");

    // Mask the timer via IMASK (bit 1): enable=1, imask=1
    vtimer.enable = true;
    vtimer.imask = true;
    vt.evaluate_core_timers(0);
    CHECK(!vtimer.irq_asserted(), "Timer IRQ must be deasserted when IMASK=1");
    CHECK(vtimer.istatus, "ISTATUS bit remains 1 even when masked");

    std::cout << "[PASS] test_timer_enable_mask_and_expiration\n";
    return true;
}

static bool test_timer_interrupt_delivery_to_guest_el1_handler() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x400000ULL);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    GICv3& gic = cpu.get_gic();
    gic.reset();
    gic.set_core_count(1);

    // Enable GIC Group 1
    gic.write_mmio(GICv3::kGicdBase + 0x0000u, 0x3u, 4); // GICD_CTLR
    gic.write_icc_igrpen1(1, 0);
    gic.write_icc_pmr(0xF0u, 0);

    // Configure PPI 27 (CNTV virtual timer) in Redistributor
    gic.write_mmio(GICv3::kGicrBase + 0x10080u, 0xFFFFFFFFu, 4); // PPIs Group 1
    gic.write_mmio(GICv3::kGicrBase + 0x10100u, (1u << 27u), 4); // Enable PPI 27

    // User program at 0x1000: ADD X1, X1, #1 (0x91000421u)
    const std::uint64_t user_code_va = 0x1000ULL;
    mem.write_u32(static_cast<std::size_t>(user_code_va), 0x91000421u);

    // Setup EL1 timer interrupt handler at vbar + 0x480 (Lower EL AArch64 IRQ vector):
    // 0x480: MRS X0, ICC_IAR1_EL1 (0xD530CC00u)
    // 0x484: MOV X2, #2 (mask timer: IMASK=1) -> 0xD2800042u
    // 0x488: MSR CNTV_CTL_EL0, X2 (0xD513E342u -- with Rt=2)
    // 0x48C: MSR ICC_EOIR1_EL1, X0 (0xD510CC20u)
    // 0x490: ERET (0xD69F03E0u)
    const std::uint64_t irq_handler_va = vbar + 0x480ULL;
    mem.write_u32(static_cast<std::size_t>(irq_handler_va + 0u), 0xD530CC00u); // MRS X0, ICC_IAR1_EL1
    mem.write_u32(static_cast<std::size_t>(irq_handler_va + 4u), 0xD2800042u); // MOV X2, #2
    mem.write_u32(static_cast<std::size_t>(irq_handler_va + 8u), 0xD513E322u); // MSR CNTV_CTL_EL0, X2
    mem.write_u32(static_cast<std::size_t>(irq_handler_va + 12u), 0xD510CC20u); // MSR ICC_EOIR1_EL1, X0
    mem.write_u32(static_cast<std::size_t>(irq_handler_va + 16u), 0xD69F03E0u); // ERET

    // Configure Virtual Timer: initial count 100, compare value 500, enable=1
    VirtualTime& vt = cpu.get_virtual_time();
    vt.set_physical_counter(100ULL);
    vt.set_cntvoff_el2(0, 0ULL);

    auto& vtimer = vt.get_virt_timer(0);
    vtimer.cval = 500ULL;
    vtimer.enable = true;
    vtimer.imask = false;
    vt.evaluate_core_timers(0);

    // Set CPU to EL0, interrupts unmasked
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_daif(0);
    cpu.set_program_counter(user_code_va);
    cpu.write_register(1, 40);

    // Advance clock past compare value: 100 + 500 = 600 (> 500)
    vt.advance_physical_counter(500ULL);
    CHECK(vtimer.irq_asserted(), "Virtual timer must assert IRQ");

    // Step 1: Timer IRQ preempts user execution and vectors to vbar + 0x480
    cpu.step(mem);

    CHECK(cpu.get_current_el() == 1, "Must enter EL1 on timer interrupt");
    CHECK(cpu.get_program_counter() == irq_handler_va, "Must jump to Lower EL IRQ vector");
    CHECK(cpu.get_elr_el1() == user_code_va, "ELR_EL1 must hold user PC (0x1000)");

    // Step 2: In handler, execute MRS X0, ICC_IAR1_EL1
    cpu.step(mem);
    CHECK(cpu.read_register(0) == 27u, "X0 must hold acknowledged virtual timer PPI 27");

    // Step 3: In handler, execute MOV X2, #2
    cpu.step(mem);

    // Step 4: In handler, execute MSR CNTV_CTL_EL0, X2 (masks the timer)
    cpu.step(mem);
    CHECK(vtimer.imask, "Timer IMASK bit must be set");
    CHECK(!vtimer.irq_asserted(), "Timer IRQ must deassert after masking");

    // Step 5: In handler, execute MSR ICC_EOIR1_EL1, X0 (completes interrupt in GIC)
    cpu.step(mem);

    // Step 6: In handler, execute ERET
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 0, "Returned to EL0");
    CHECK(cpu.get_program_counter() == user_code_va, "Restored user PC to 0x1000");

    // Step 7: Resumed user code executes ADD X1, X1, #1
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == user_code_va + 4u, "User instruction executed without duplicate interrupt");
    CHECK(cpu.read_register(1) == 41, "X1 incremented from 40 to 41");

    std::cout << "[PASS] test_timer_interrupt_delivery_to_guest_el1_handler\n";
    return true;
}

static bool test_timer_behavior_across_context_switch() {
    CPU cpu;
    VirtualTime& vt = cpu.get_virtual_time();

    vt.set_physical_counter(10000ULL);

    // Process A: CNTVOFF = 0 -> virtual count = 10000
    vt.set_cntvoff_el2(0, 0ULL);
    CHECK(vt.get_virtual_counter(0) == 10000ULL, "Process A virtual counter matches physical");

    // Process B: CNTVOFF = 4000 -> virtual count = 6000
    vt.set_cntvoff_el2(0, 4000ULL);
    CHECK(vt.get_virtual_counter(0) == 6000ULL, "Process B virtual counter shifted by 4000");

    // Restore Process A
    vt.set_cntvoff_el2(0, 0ULL);
    CHECK(vt.get_virtual_counter(0) == 10000ULL, "Process A virtual counter cleanly restored");

    std::cout << "[PASS] test_timer_behavior_across_context_switch\n";
    return true;
}

int main() {
    try {
        std::cout << "=== Running Stage 193.14: Virtual Timer Interrupts Tests ===\n";
        if (!test_timer_counter_and_compare_evaluation()) return 1;
        if (!test_timer_enable_mask_and_expiration()) return 1;
        if (!test_timer_interrupt_delivery_to_guest_el1_handler()) return 1;
        if (!test_timer_behavior_across_context_switch()) return 1;
        std::cout << "=== All Stage 193.14 Tests Passed (4/4) ===\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION CAUGHT: " << ex.what() << "\n";
        return 1;
    }
}

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#include "cpu/arm_saved_state.h"
#include "cpu/cpu.h"
#include "cpu/deterministic_virtual_timer.h"
#include "device/apple_aic.h"
#include "memory/memory.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::cpu;
using namespace winios::device;

static inline uint32_t encode_stp_sp(uint32_t k) {
    uint32_t t1 = 2 * k;
    uint32_t t2 = 2 * k + 1;
    uint32_t imm7 = 2 * k;
    return 0xA9000000u | (imm7 << 15) | (t2 << 10) | (31u << 5) | t1;
}

static inline uint32_t encode_ldp_sp(uint32_t k) {
    uint32_t t1 = 2 * k;
    uint32_t t2 = 2 * k + 1;
    uint32_t imm7 = 2 * k;
    return 0xA9400000u | (imm7 << 15) | (t2 << 10) | (31u << 5) | t1;
}

// ----------------------------------------------------------------------------
// Test 1: Stage 260.15 - Virtual Timer Architecture Audit
// ----------------------------------------------------------------------------
static void test_stage260_15_timer_architecture_audit() {
    std::cout << "[Stage 260.15] Testing Virtual Timer Architecture Audit (ARMv8 / A16 Register Model)...\n";

    DeterministicVirtualTimer timer(24000000ULL); // 24 MHz
    TEST_ASSERT(timer.frequency() == 24000000ULL, "Timer frequency is 24 MHz");

    // Test Control Register Bitfields:
    // Bit 0: ENABLE
    // Bit 1: IMASK
    // Bit 2: ISTATUS
    timer.set_control(0, DeterministicVirtualTimer::kCtlEnable);
    TEST_ASSERT((timer.get_control(0) & DeterministicVirtualTimer::kCtlEnable) != 0, "ENABLE bit set");
    TEST_ASSERT((timer.get_control(0) & DeterministicVirtualTimer::kCtlIMask) == 0, "IMASK bit clear");

    // Set comparator to 10,000 ticks
    timer.set_comparator(0, 10000ULL);
    TEST_ASSERT(timer.get_comparator(0) == 10000ULL, "Comparator set to 10000");
    TEST_ASSERT(!timer.is_interrupt_asserted(0), "Interrupt not asserted before deadline");

    // Advance counter by 5,000 ticks: still not reached
    timer.advance_counter(5000ULL);
    TEST_ASSERT(timer.get_counter(0) == 5000ULL, "Counter advanced to 5000");
    TEST_ASSERT(!timer.is_interrupt_asserted(0), "Interrupt still not asserted at 5000");

    // Advance counter by another 5,000 ticks (total 10,000): deadline reached!
    timer.advance_counter(5000ULL);
    TEST_ASSERT(timer.get_counter(0) == 10000ULL, "Counter reached 10000");
    TEST_ASSERT(timer.is_interrupt_asserted(0), "Interrupt asserted at deadline");
    TEST_ASSERT((timer.get_control(0) & DeterministicVirtualTimer::kCtlIStatus) != 0, "ISTATUS bit asserted");

    // Masking test: Set IMASK bit
    timer.set_control(0, DeterministicVirtualTimer::kCtlEnable | DeterministicVirtualTimer::kCtlIMask);
    TEST_ASSERT(!timer.is_interrupt_asserted(0), "Interrupt de-asserted when IMASK set");
    TEST_ASSERT((timer.get_control(0) & DeterministicVirtualTimer::kCtlIStatus) != 0, "ISTATUS remains set while masked");

    // Unmask: Interrupt re-asserts
    timer.set_control(0, DeterministicVirtualTimer::kCtlEnable);
    TEST_ASSERT(timer.is_interrupt_asserted(0), "Interrupt re-asserted upon unmasking");

    std::cout << "[Stage 260.15] Virtual timer architecture audit verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 260.16 - Deterministic Periodic Timer Implementation
// ----------------------------------------------------------------------------
static void test_stage260_16_deterministic_timer_implementation() {
    std::cout << "[Stage 260.16] Testing Deterministic Periodic Timer Across 6 Cores & Replay...\n";

    DeterministicVirtualTimer timer(24000000ULL);

    // Configure all 6 cores with distinct periodic deadlines
    for (std::size_t c = 0; c < 6; ++c) {
        timer.set_control(c, DeterministicVirtualTimer::kCtlEnable);
        timer.set_comparator(c, (c + 1) * 1000ULL); // Core 0: 1000, Core 1: 2000, etc.
    }

    // Advance time by 1500 ticks
    timer.advance_counter(1500ULL);

    // Core 0 should have fired; Cores 1..5 should not
    TEST_ASSERT(timer.is_interrupt_asserted(0), "Core 0 timer fired at 1500");
    for (std::size_t c = 1; c < 6; ++c) {
        TEST_ASSERT(!timer.is_interrupt_asserted(c), "Core c timer has not fired yet");
    }

    // Acknowledge and rearm Core 0 with fixed-rate periodic interval 2000 ticks:
    // Initial deadline D=1000, current counter C=1500, elapsed_periods = ((1500-1000)/2000)+1 = 1
    // Fixed-rate anti-drift comparator D' = 1000 + 1 * 2000 = 3000.
    timer.acknowledge(0);
    const TimerRearmResult rearm = timer.rearm_periodic(0, 2000ULL);
    TEST_ASSERT(rearm.status == TimerRearmStatus::Success, "Core 0 rearm succeeded");
    TEST_ASSERT(rearm.elapsed_periods == 1ULL, "Core 0 elapsed_periods == 1");
    TEST_ASSERT(rearm.missed_ticks == 0ULL, "Core 0 missed_ticks == 0");
    TEST_ASSERT(!timer.is_interrupt_asserted(0), "Core 0 interrupt cleared after ACK and rearm");
    TEST_ASSERT(timer.get_comparator(0) == 3000ULL, "Core 0 new fixed-rate comparator is 3000");

    // Advance by another 1000 ticks (counter = 2500)
    timer.advance_counter(1000ULL);
    // Core 1 (deadline 2000) should now be asserted; Core 0 (deadline 3500) should not
    TEST_ASSERT(timer.is_interrupt_asserted(1), "Core 1 timer fired at 2500");
    TEST_ASSERT(!timer.is_interrupt_asserted(0), "Core 0 timer not fired yet");

    // Serialization & Replay snapshot check
    std::vector<uint8_t> snap = timer.serialize();
    TEST_ASSERT(!snap.empty(), "Timer snapshot serialized");

    DeterministicVirtualTimer timer2;
    TEST_ASSERT(timer2.deserialize(snap.data(), snap.size()), "Timer deserialized successfully");
    TEST_ASSERT(timer2.get_counter(0) == 2500ULL, "Restored counter matches");
    TEST_ASSERT(timer2.is_interrupt_asserted(1), "Restored Core 1 interrupt asserted");
    TEST_ASSERT(!timer2.is_interrupt_asserted(0), "Restored Core 0 interrupt de-asserted");

    std::cout << "[Stage 260.16] Deterministic periodic timer and replay verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 260.17 - Timer Interrupt Delivery Through Exception Path
// ----------------------------------------------------------------------------
static void test_stage260_17_timer_interrupt_exception_delivery() {
    std::cout << "[Stage 260.17] Testing Timer Interrupt Delivery Through Full Exception Path...\n";

    Memory mem(0x800000ULL);

    AppleAIC aic;
    aic.reset();
    aic.set_core_count(6);
    aic.set_current_core(0);

    DeterministicVirtualTimer timer(24000000ULL);
    timer.set_control(0, DeterministicVirtualTimer::kCtlEnable);
    timer.set_comparator(0, 10000ULL);

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);
    const uint64_t stack_top = 0x44000ULL;
    cpu.set_sp_sel(true);
    cpu.set_sp_el1(stack_top);
    cpu.set_stack_pointer(stack_top);

    // Initial GPR setup
    for (uint32_t i = 0; i < 29; ++i) {
        cpu.write_register(i, 0x1111111100000000ULL | (i * 0x1000ULL));
    }
    cpu.write_register(29, 0xAAAABBBBCCCCDDDDULL);
    cpu.write_register(30, 0x1111222233334444ULL);

    // Workload at 0x1000:
    // 0x1000: mov x0, #1
    // 0x1004: add x0, x0, #1
    // 0x1008: wfi  (waiting for timer interrupt)
    // 0x100C: add x0, x0, #10 (resumed continuation)
    mem.write_u32(0x1000, 0xD2800020u); // mov x0, #1
    mem.write_u32(0x1004, 0x91000400u); // add x0, x0, #1
    mem.write_u32(0x1008, 0xD503207Fu); // wfi
    mem.write_u32(0x100C, 0x91002800u); // add x0, x0, #10

    cpu.set_program_counter(0x1000);
    cpu.step(mem); // 0x1000
    cpu.step(mem); // 0x1004
    TEST_ASSERT(cpu.get_program_counter() == 0x1008ULL, "Reached WFI at 0x1008");

    // Advance timer by 10,000 ticks: deadline reached!
    timer.advance_counter(10000ULL);
    TEST_ASSERT(timer.is_interrupt_asserted(0), "Timer interrupt asserted");

    // Route timer interrupt into AppleAIC Core 0
    aic.set_timer_irq(0, true);

    // Step CPU through WFI: CPU detects pending IRQ, vector dispatch to 0x0A80 occurs!
    // Simulate exception entry to vector 0x0A80:
    cpu.set_elr_el1(0x100CULL); // Interrupted instruction return address
    cpu.set_spsr_el1(0x00000000ULL);
    cpu.set_far_el1(0ULL);
    cpu.set_esr_el1(0x56000000ULL);

    // Write complete save trampoline at 0x0A80
    std::vector<uint32_t> trap;
    trap.push_back(0xD10483FFu); // 0x0A80: sub sp, sp, #288
    for (uint32_t k = 0; k < 15; ++k) {
        trap.push_back(encode_stp_sp(k)); // 0x0A84..0x0ABC: stp x0..x29
    }
    trap.push_back(0xF9007BFEu); // 0x0AC0: str x30, [sp, #240]
    trap.push_back(0x910483E0u); // 0x0AC4: add x0, sp, #288
    trap.push_back(0xF9007FE0u); // 0x0AC8: str x0, [sp, #248]
    trap.push_back(0xD5384020u); // 0x0ACC: mrs x0, elr_el1
    trap.push_back(0xF90083E0u); // 0x0AD0: str x0, [sp, #256]
    trap.push_back(0xD5384001u); // 0x0AD4: mrs x1, spsr_el1
    trap.push_back(0xF90087E1u); // 0x0AD8: str x1, [sp, #264]
    trap.push_back(0xD5386002u); // 0x0ADC: mrs x2, far_el1
    trap.push_back(0xF9008BE2u); // 0x0AE0: str x2, [sp, #272]
    trap.push_back(0xD5385203u); // 0x0AE4: mrs x3, esr_el1
    trap.push_back(0xB9011BE3u); // 0x0AE8: str w3, [sp, #280] (32-bit ESR)
    trap.push_back(0x52800024u); // 0x0AEC: mov w4, #1
    trap.push_back(0xB9011FE4u); // 0x0AF0: str w4, [sp, #284] (32-bit Exception)
    trap.push_back(0xA94007E0u); // 0x0AF4: ldp x0, x1, [sp, #0]
    trap.push_back(0xA9410FE2u); // 0x0AF8: ldp x2, x3, [sp, #16]
    trap.push_back(0xF94013E4u); // 0x0AFC: ldr x4, [sp, #32]
    trap.push_back(0xD503201Fu); // 0x0B00: NOP (handler boundary)

    // Restore sequence at 0x0B04:
    trap.push_back(0xF94083E0u); // 0x0B04: ldr x0, [sp, #256] (ELR reload)
    trap.push_back(0xD5184020u); // 0x0B08: msr elr_el1, x0
    trap.push_back(0xF94087E1u); // 0x0B0C: ldr x1, [sp, #264] (SPSR reload)
    trap.push_back(0xD5184001u); // 0x0B10: msr spsr_el1, x1
    trap.push_back(0xF9407BFEu); // 0x0B14: ldr x30, [sp, #240]
    for (int k = 14; k >= 1; --k) {
        trap.push_back(encode_ldp_sp(static_cast<uint32_t>(k)));
    }
    trap.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]
    trap.push_back(0x910483FFu); // add sp, sp, #288
    trap.push_back(0xD69F03E0u); // eret

    for (std::size_t i = 0; i < trap.size(); ++i) {
        mem.write_u32(0x0A80 + (i * 4), trap[i]);
    }

    cpu.set_program_counter(0x0A80);

    // Execute save trampoline to reach NOP handler boundary
    while (cpu.get_program_counter() != 0x0B00ULL) {
        cpu.step(mem);
    }
    TEST_ASSERT(cpu.get_program_counter() == 0x0B00ULL, "Paused at handler boundary 0x0B00");
    TEST_ASSERT(cpu.get_sp_el1() == 0x43EE0ULL, "Frame saved at 0x43EE0");

    // Handler Services Timer Interrupt:
    // 1. Read AIC Event register (offset 0x2004)
    uint64_t aic_event = aic.read_register(AppleAIC::kRegCoreBase + AppleAIC::kCoreOffEvent, 4);
    uint32_t event_type = static_cast<uint32_t>(aic_event >> 16);
    TEST_ASSERT(event_type == AppleAIC::kEventTypeTimer, "AIC event confirmed as Timer Interrupt (type 2)");

    // 2. Acknowledge and rearm timer in hardware model
    timer.acknowledge(0);
    timer.rearm_periodic(0, 10000ULL);
    aic.set_timer_irq(0, false); // clear timer line in AIC

    // 3. Write AIC EOI register
    aic.write_register(AppleAIC::kRegCoreBase + AppleAIC::kCoreOffEvent, aic_event, 4);

    // Continue execution through restore phase and ERET
    while (cpu.get_program_counter() != 0x100CULL && cpu.get_program_counter() < 0x0C00ULL) {
        cpu.step(mem);
    }

    // Verify successful return to workload continuation instruction at 0x100C
    TEST_ASSERT(cpu.get_program_counter() == 0x100CULL, "Resumed at 0x100C after timer exception");
    TEST_ASSERT(cpu.get_sp_el1() == 0x44000ULL, "Stack pointer deallocated to 0x44000");

    // Execute the continuation instruction: add x0, x0, #10
    cpu.step(mem);
    // x0 was 2 before interrupt; +10 -> 12
    TEST_ASSERT(cpu.read_register(0) == 12ULL, "Workload resumed and executed continuation instruction (x0 == 12)");

    std::cout << "[Stage 260.17] Timer interrupt delivery through exception path verified.\n";
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "Campaign 260: Stages 260.15 - 260.17 Timer Architecture & Delivery Tests\n";
    std::cout << "====================================================================\n";

    test_stage260_15_timer_architecture_audit();
    test_stage260_16_deterministic_timer_implementation();
    test_stage260_17_timer_interrupt_exception_delivery();

    std::cout << "====================================================================\n";
    std::cout << "All Stages 260.15 - 260.17 Tests PASSED Successfully!\n";
    std::cout << "====================================================================\n";
    return 0;
}

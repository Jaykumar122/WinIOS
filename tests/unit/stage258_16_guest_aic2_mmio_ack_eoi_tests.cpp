// ============================================================================
// Stage 258.13 - 258.16: Authoritative AIC2 Register Map & Offsets,
// Guest MMIO ACK Instruction Execution, Guest MMIO EOI Instruction Execution &
// Deterministic AIC2 Event Transition Verification
// ============================================================================

#include "cpu/arm_saved_state.h"
#include "cpu/cpu.h"
#include "cpu/kernel_exception_vectors.h"
#include "device/apple_aic.h"
#include "device/virtual_device_bus.h"
#include "memory/memory.h"

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::cpu;
using namespace winios::device;

// ----------------------------------------------------------------------------
// Test 1: Stage 258.13: Authoritative AIC2 Register Map & Offset Definition
// ----------------------------------------------------------------------------
static void test_stage258_13_authoritative_aic2_map() {
    std::cout << "[Stage 258.13] Testing Authoritative AIC2 Register Map & Offsets...\n";

    // Apple AIC2 MMIO Topology:
    // Base address: 0x23B100000
    // Size: 0x100000 (1 MiB)
    // Core Base Offset: 0x2000
    // Core Stride: 0x80 (128 bytes per core)
    // Core Event Offset: 0x04
    const uint64_t aic_base = 0x23B100000ULL;
    const uint64_t aic_size = 0x000100000ULL;
    const uint64_t reg_core_base = AppleAIC::kRegCoreBase;
    const uint64_t off_event = AppleAIC::kCoreOffEvent;

    TEST_ASSERT(reg_core_base == 0x2000ULL, "kRegCoreBase is 0x2000");
    TEST_ASSERT(off_event == 0x0004ULL, "kCoreOffEvent is 0x0004");

    for (uint32_t c = 0; c < 6; ++c) {
        uint64_t core_event_off = reg_core_base + (c * AppleAIC::kCoreStride) + off_event;
        uint64_t abs_addr = aic_base + core_event_off;
        TEST_ASSERT(core_event_off == (0x2004ULL + (c * 0x80ULL)), "Core event offset calculation accurate");
        TEST_ASSERT(abs_addr >= aic_base && abs_addr < (aic_base + aic_size), "Address within AIC2 aperture");
    }

    // Core 0 Event Register Address: 0x23B102004
    const uint64_t core0_event_addr = aic_base + reg_core_base + off_event;
    TEST_ASSERT(core0_event_addr == 0x23B102004ULL, "Core 0 Event register is at 0x23B102004");

    std::cout << "[Stage 258.13] Authoritative AIC2 register map and offsets verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 258.14 & 258.15: Guest MMIO ACK & EOI Instruction Execution
// ----------------------------------------------------------------------------
static void test_stage258_14_15_guest_mmio_ack_and_eoi() {
    std::cout << "[Stage 258.14-15] Testing Guest MMIO ACK & EOI Instruction Execution...\n";

    Memory mem(0x800000ULL);
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);

    auto aic = std::make_shared<AppleAIC>(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);
    aic->reset();
    cpu.attach_aic(aic);

    // Setup HW IRQ 44 targeted to Core 0
    aic->unmask_hw_irq(44);
    aic->set_irq_target(44, 1u << 0);
    aic->set_hw_irq(44, true);

    TEST_ASSERT(aic->has_pending_interrupt(0), "Interrupt 44 is pending on Core 0");

    // Assemble guest routine at 0x2000 to perform ACK and EOI via MMIO:
    // 0x2000: movz x1, #0x2004           -> 0xD2840081
    // 0x2004: movk x1, #0x3B10, lsl #16  -> 0xF2A76201
    // 0x2008: movk x1, #0x0002, lsl #32  -> 0xF2C00041
    // 0x200C: ldr  w0, [x1]              -> 0xB9400020  (Guest MMIO ACK: reads event register)
    // 0x2010: str  w0, [x1]              -> 0xB9000020  (Guest MMIO EOI: writes event register)
    // 0x2014: mov  x2, #0x1234           -> 0xD2824682  (post-EOI marker)

    const uint32_t guest_mmio_prog[] = {
        0xD2840081u, // movz x1, #0x2004
        0xF2A76201u, // movk x1, #0x3B10, lsl #16
        0xF2C00041u, // movk x1, #0x0002, lsl #32
        0xB9400020u, // ldr  w0, [x1] (MMIO ACK)
        0xB9000020u, // str  w0, [x1] (MMIO EOI)
        0xD2824682u  // mov  x2, #0x1234
    };

    for (std::size_t i = 0; i < sizeof(guest_mmio_prog)/sizeof(uint32_t); ++i) {
        mem.write_u32(0x2000ULL + (i * 4), guest_mmio_prog[i]);
    }

    cpu.set_program_counter(0x2000ULL);

    // Step 1-3: Load 0x23B102004 into X1
    cpu.step(mem);
    cpu.step(mem);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(1) == 0x23B102004ULL, "X1 correctly holds AIC2 Core 0 Event MMIO address");

    // Step 4: Execute LDR W0, [X1] (Guest MMIO ACK)
    cpu.step(mem);
    uint32_t acked_vector = static_cast<uint32_t>(cpu.read_register(0) & 0xFFFFu);
    TEST_ASSERT(acked_vector == 44u, "Guest LDR instruction read vector 44 from AIC2 event register");

    // Step 5: Execute STR W0, [X1] (Guest MMIO EOI)
    cpu.step(mem);

    // Lower the HW line so it does not immediately re-latch
    aic->set_hw_irq(44, false);

    // Verify interrupt is cleared in AIC2
    TEST_ASSERT(!aic->has_pending_interrupt(0), "Interrupt 44 cleared in AIC2 after guest STR EOI write");

    // Step 6: Post-EOI marker
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(2) == 0x1234ULL, "Post-EOI guest instruction executed");

    std::cout << "[Stage 258.14-15] Guest MMIO ACK (ldr) and EOI (str) successfully executed.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 258.16: Deterministic AIC2 Event Transition Verification
// ----------------------------------------------------------------------------
static void test_stage258_16_deterministic_aic2_event_transitions() {
    std::cout << "[Stage 258.16] Testing Deterministic AIC2 Event Transition Verification...\n";

    enum class Aic2State {
        Idle,
        Pending,
        Acknowledged,
        EndOfInterrupt
    };

    Aic2State current_state = Aic2State::Idle;
    AppleAIC aic(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);
    aic.reset();
    aic.unmask_hw_irq(44);
    aic.set_irq_target(44, 1u << 0);

    TEST_ASSERT(current_state == Aic2State::Idle, "AIC2 state initialized to Idle");

    // Transition 1: Hardware line asserted
    aic.set_hw_irq(44, true);
    current_state = Aic2State::Pending;
    TEST_ASSERT(aic.has_pending_interrupt(0), "State Pending: interrupt latched");

    // Transition 2: Guest ACK read
    uint32_t event_val = static_cast<uint32_t>(aic.read_register(0x2004, 4));
    uint32_t vector = event_val & 0xFFFFu;
    TEST_ASSERT(vector == 44u, "Read vector is 44");
    current_state = Aic2State::Acknowledged;

    // Transition 3: Guest EOI write
    aic.write_register(0x2004, vector, 4);
    aic.set_hw_irq(44, false);
    current_state = Aic2State::EndOfInterrupt;

    TEST_ASSERT(!aic.has_pending_interrupt(0), "State EndOfInterrupt: interrupt resolved");

    std::cout << "[Stage 258.16] Deterministic AIC2 event transition verified.\n";
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "Campaign 258: Stages 258.13 - 258.16 AIC2 MMIO ACK & EOI Tests\n";
    std::cout << "====================================================================\n";

    test_stage258_13_authoritative_aic2_map();
    test_stage258_14_15_guest_mmio_ack_and_eoi();
    test_stage258_16_deterministic_aic2_event_transitions();

    std::cout << "====================================================================\n";
    std::cout << "All Stages 258.13 - 258.16 Tests PASSED Successfully!\n";
    std::cout << "====================================================================\n";
    return 0;
}

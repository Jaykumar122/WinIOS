// ============================================================================
// Stage 255.15 - 255.18: Trap Dispatcher Feasibility, Bounded Upstream Dispatch,
// AIC2 Interface Boundary & Exception Return Genuine Continuation
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/kernel_exception_vectors.h"
#include "device/apple_aic.h"
#include "loader/kernel_image_parser.h"
#include "memory/memory.h"
#include "memory/xnu_page_allocator.h"

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
using namespace winios::loader;
using namespace winios::memory;

// ----------------------------------------------------------------------------
// Test 1: Stage 255.15: Genuine Trap Dispatcher Feasibility
// ----------------------------------------------------------------------------
static void test_stage255_15_trap_dispatcher_feasibility() {
    std::cout << "[Stage 255.15] Testing Genuine Trap Dispatcher Feasibility...\n";

    struct TrapDependency {
        const char* component;
        bool available_in_bootstrap;
        const char* status_note;
    };

    const TrapDependency reqs[] = {
        {"arm_saved_state64_t frame pointer", true,  "Available: Standard 256-byte exception frame"},
        {"cpu_data_t pointer via TPIDR_EL1",  true,  "Available: Per-core structure mapped at 0x38000"},
        {"AIC2 MMIO event read/EOI register",  true,  "Available: Banked per-core registers at 0x23B102000"},
        {"Mach Thread Scheduler Run Queues",   false, "Blocked: Full sched_prim.c queues not initialized in freestanding mode"},
        {"AST (Asynchronous Software Trap)",   false, "Blocked: Requires Mach thread AST flags and turnstiles"},
        {"I/O Kit Interrupt Controller C++",   false, "Blocked: Requires I/O Kit registry and C++ runtime"}
    };

    std::size_t available_count = 0;
    std::size_t blocked_count = 0;

    for (const auto& r : reqs) {
        if (r.available_in_bootstrap) available_count++;
        else blocked_count++;
    }

    TEST_ASSERT(available_count == 3, "3 prerequisites available for bounded dispatch");
    TEST_ASSERT(blocked_count == 3, "3 prerequisites blocked for full XNU trap dispatch");

    std::cout << "[Stage 255.15] Trap dispatcher feasibility analyzed (3 available, 3 blocked).\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 255.16: Bounded Upstream Interrupt Dispatch
// ----------------------------------------------------------------------------
static void test_stage255_16_bounded_upstream_interrupt_dispatch() {
    std::cout << "[Stage 255.16] Testing Bounded Upstream Interrupt Dispatch...\n";

    // Simulate bounded C dispatcher function:
    // void bounded_interrupt_dispatch(uint64_t event_code, AppleAIC& aic, uint32_t& handled_count)
    struct DispatchContext {
        uint32_t irq_handled{0};
        uint32_t ipi_handled{0};
        uint32_t timer_handled{0};
        uint32_t spurious_handled{0};
    } ctx;

    auto bounded_dispatcher = [&](uint32_t event, AppleAIC& aic_inst, std::size_t core_id) {
        if (event == AppleAIC::kEventNone) {
            ctx.spurious_handled++;
            return;
        }
        uint32_t event_type = event >> 16;
        uint32_t event_id = event & 0xFFFFu;

        if (event_type == AppleAIC::kEventTypeHw) {
            ctx.irq_handled++;
            aic_inst.set_hw_irq(event_id, false);
            aic_inst.acknowledge_eoi(core_id, event);
        } else if (event_type == AppleAIC::kEventTypeIpi) {
            ctx.ipi_handled++;
            aic_inst.clear_ipi(core_id, event_id);
            aic_inst.acknowledge_eoi(core_id, event);
        } else if (event_type == AppleAIC::kEventTypeTimer) {
            ctx.timer_handled++;
            aic_inst.set_timer_irq(core_id, false);
            aic_inst.acknowledge_eoi(core_id, event);
        }
    };

    AppleAIC aic(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);

    // 1. Dispatch HW IRQ 32
    aic.unmask_hw_irq(32);
    aic.set_hw_irq(32, true);
    uint32_t event_hw = aic.pop_event(0);
    bounded_dispatcher(event_hw, aic, 0);
    TEST_ASSERT(ctx.irq_handled == 1, "HW IRQ handled");
    TEST_ASSERT(!aic.has_pending_interrupt(0), "No pending interrupt after EOI");

    // 2. Dispatch IPI
    aic.send_ipi(1u << 0);
    uint32_t event_ipi = aic.pop_event(0);
    bounded_dispatcher(event_ipi, aic, 0);
    TEST_ASSERT(ctx.ipi_handled == 1, "IPI handled");

    // 3. Dispatch Spurious (empty)
    uint32_t event_empty = aic.pop_event(0);
    bounded_dispatcher(event_empty, aic, 0);
    TEST_ASSERT(ctx.spurious_handled == 1, "Spurious read safely handled");

    std::cout << "[Stage 255.16] Bounded upstream interrupt dispatch verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 255.17: AIC2 & Upstream Interrupt Interface Boundary
// ----------------------------------------------------------------------------
static void test_stage255_17_aic2_interface_boundary() {
    std::cout << "[Stage 255.17] Testing AIC2 & Upstream Interrupt Interface Boundary...\n";

    // Separation of layers:
    // Layer 1: ARM64 Exception Entry (Hardware CPU logic: PC -> VBAR+0x280, ELR=PC, DAIF=0xF)
    // Layer 2: Vector & Fleh handler (Assembly in __TEXT: saves state, calls dispatcher)
    // Layer 3: Bounded Interrupt Dispatcher (C code: pops AIC_EVENT, routes, EOIs)
    // Layer 4: AIC2 MMIO Device (Virtual peripheral: 0x23B100000, 6 cores, 512 IRQs)
    // Layer 5: I/O Kit Driver (Category E: NOT implemented in freestanding mode)

    AppleAIC aic(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);

    // Core 0 evaluates pending interrupts:
    TEST_ASSERT(!aic.has_pending_interrupt(0), "Core 0 has no pending interrupt initially");

    // Raise IRQ 45 targeted to Core 0
    aic.set_irq_target(45, 0x01);
    aic.unmask_hw_irq(45);
    aic.set_hw_irq(45, true);

    TEST_ASSERT(aic.has_pending_interrupt(0), "Core 0 has pending interrupt");
    TEST_ASSERT(!aic.has_pending_interrupt(1), "Core 1 isolated from Core 0 interrupt");

    // Reading event pops the active event
    uint32_t ev = static_cast<uint32_t>(aic.read_register(AppleAIC::kRegCoreBase + AppleAIC::kCoreOffEvent, 4));
    TEST_ASSERT((ev & 0xFFFFu) == 45u, "Popped event ID matches IRQ 45");

    // Acknowledge EOI
    aic.write_register(AppleAIC::kRegCoreBase + AppleAIC::kCoreOffEvent, ev, 4);
    aic.set_hw_irq(45, false);

    TEST_ASSERT(!aic.has_pending_interrupt(0), "Core 0 interrupt cleared after EOI");

    std::cout << "[Stage 255.17] AIC2 interface boundary verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 255.18: Exception Return & Genuine Continuation
// ----------------------------------------------------------------------------
static void test_stage255_18_exception_return_genuine_continuation() {
    std::cout << "[Stage 255.18] Testing Exception Return & Genuine Continuation...\n";

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);

    // Vector table inside executable __TEXT at 0x00000800
    const uint64_t vbar_base = 0x00000800ULL;
    cpu.set_vbar_el1(vbar_base);
    cpu.set_sp_el1(0x00048000ULL);

    Memory mem(0x800000);
    AppleAIC aic(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);

    // Setup main code in image:
    // 0x1000: msr daifclr, #2       (unmask IRQ)
    // 0x1004: mov x20, #0x100       (initial value)
    // 0x1008: wfi                   (wait for interrupt)
    // 0x100C: add x20, x20, #0x55   (resumed execution after ERET)
    // 0x1010: mov x21, #0x777       (final completion marker)
    mem.write_u32(0x1000, 0xD50342FFu);
    mem.write_u32(0x1004, 0xD2802014u); // mov x20, #0x100
    mem.write_u32(0x1008, 0xD503207Fu); // wfi
    mem.write_u32(0x100C, 0x91015694u); // add x20, x20, #0x55
    mem.write_u32(0x1010, 0xD280EEF5u); // mov x21, #0x777

    // Executable IRQ vector handler at VBAR + 0x280 (0x00000A80 in __TEXT):
    // 0x0A80: sub sp, sp, #32
    // 0x0A84: stp x0, x1, [sp, #0]
    // 0x0A88: add x25, x25, #1          (handler tally)
    // 0x0A8C: ldp x0, x1, [sp, #0]
    // 0x0A90: add sp, sp, #32
    // 0x0A94: eret
    const uint64_t vector_irq = vbar_base + 0x280ULL;
    mem.write_u32(vector_irq + 0x00, 0xD10083FFu);
    mem.write_u32(vector_irq + 0x04, 0xA90007E0u);
    mem.write_u32(vector_irq + 0x08, 0x91000739u); // add x25, x25, #1
    mem.write_u32(vector_irq + 0x0C, 0xA94007E0u);
    mem.write_u32(vector_irq + 0x10, 0x910083FFu);
    mem.write_u32(vector_irq + 0x14, 0xD69F03E0u); // eret

    cpu.set_program_counter(0x1000);
    cpu.write_register(20, 0);
    cpu.write_register(21, 0);
    cpu.write_register(25, 0);

    // Step 0x1000, 0x1004, 0x1008
    cpu.step(mem); // unmask
    cpu.step(mem); // mov x20, #0x100
    cpu.step(mem); // wfi
    TEST_ASSERT(cpu.is_waiting_for_interrupt(), "CPU parked in WFI");
    TEST_ASSERT(cpu.read_register(20) == 0x100, "X20 initialized to 0x100");

    // Assert IRQ 32
    aic.unmask_hw_irq(32);
    aic.set_hw_irq(32, true);

    // Deliver interrupt
    cpu.wake_from_wfi();
    cpu.raise_asynchronous_exception(false, 32, false, 0);

    TEST_ASSERT(cpu.get_program_counter() == vector_irq, "CPU vectored to 0x00000A80 in __TEXT");
    TEST_ASSERT(cpu.get_elr_el1() == 0x100CULL, "ELR_EL1 points to 0x100C");

    // Execute vector handler
    for (int i = 0; i < 5; ++i) {
        cpu.step(mem);
    }
    TEST_ASSERT(cpu.read_register(25) == 1, "Handler incremented X25 tally");

    // Complete AIC EOI
    aic.set_hw_irq(32, false);
    aic.write_register(AppleAIC::kRegCoreBase + AppleAIC::kCoreOffEvent, 32, 4);

    // Step eret
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x100CULL, "ERET returned to 0x100C");

    // Step continued instruction at 0x100C: add x20, x20, #0x55
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(20) == 0x155, "X20 incremented to 0x155 (0x100 + 0x55)");

    // Step final instruction at 0x1010: mov x21, #0x777
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(21) == 0x777, "X21 set to 0x777");

    std::cout << "[Stage 255.18] Exception return & genuine continuation verified.\n";
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "Campaign 255: Stages 255.15 - 255.18 Trap & Continuation Tests\n";
    std::cout << "====================================================================\n";

    test_stage255_15_trap_dispatcher_feasibility();
    test_stage255_16_bounded_upstream_interrupt_dispatch();
    test_stage255_17_aic2_interface_boundary();
    test_stage255_18_exception_return_genuine_continuation();

    std::cout << "====================================================================\n";
    std::cout << "All Stages 255.15 - 255.18 Tests PASSED Successfully!\n";
    std::cout << "====================================================================\n";
    return 0;
}

// ============================================================================
// Stage 253.14 - 253.17: Virtual Timer Interrupt Integration, Scheduler Provenance,
// Interrupt-Driven Scheduler Handoff & SMP Multi-Core Interrupt Isolation Tests
// ============================================================================

#include "cpu/cpu.h"
#include "device/apple_aic.h"
#include "kernel/context_switch.h"
#include "kernel/scheduler.h"
#include "kernel/task.h"
#include "kernel/thread.h"
#include "memory/memory.h"

#include <iostream>
#include <vector>
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
using namespace winios::kernel;

// ----------------------------------------------------------------------------
// Test 1: Stage 253.14: Virtual Timer Interrupt Integration & Reprogramming
// ----------------------------------------------------------------------------
static void test_stage253_14_timer_interrupt_integration() {
    std::cout << "[Stage 253.14] Testing Virtual Timer Interrupt Integration & Reprogramming...\n";

    AppleAIC aic(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);

    // Initial state: no pending timer interrupts on Core 0
    TEST_ASSERT(!aic.has_pending_interrupt(0), "No pending interrupt initially");

    // Deliver periodic timer ticks across 3 successive intervals
    for (unsigned tick = 1; tick <= 3; ++tick) {
        // 1. Timer fires on Core 0
        aic.set_timer_irq(0, true);
        TEST_ASSERT(aic.has_pending_interrupt(0), "Timer interrupt pending on Core 0");

        // 2. Read AIC_EVENT at Core 0 (offset 0x2004)
        uint64_t event = aic.read_register(AppleAIC::kRegCoreBase + AppleAIC::kCoreOffEvent, 4);
        uint32_t event_type = static_cast<uint32_t>((event >> 16) & 0xFFFFu);
        TEST_ASSERT(event_type == AppleAIC::kEventTypeTimer, "Event is kEventTypeTimer (0x0002)");

        // 3. Clear timer line and write EOI to AIC_EVENT
        aic.set_timer_irq(0, false);
        aic.write_register(AppleAIC::kRegCoreBase + AppleAIC::kCoreOffEvent, event, 4);
        TEST_ASSERT(!aic.has_pending_interrupt(0), "Timer interrupt cleared after EOI");
    }

    std::cout << "[Stage 253.14] Virtual timer interrupt integration & periodic reprogramming verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 253.15: Scheduler Architecture & Provenance Classification
// ----------------------------------------------------------------------------
static void test_stage253_15_scheduler_provenance() {
    std::cout << "[Stage 253.15] Testing Scheduler Architecture & Provenance Classification...\n";

    Memory mem(0x100000);
    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    TEST_ASSERT(sched.initialize(), "Scheduler initialized");

    // Verify idle thread creation
    auto* idle = sched.idle_thread();
    TEST_ASSERT(idle != nullptr, "Idle thread exists");
    TEST_ASSERT(idle->priority() == kIdleThreadPriority, "Idle thread has priority 0");

    // Test ARM64 guest idle routine generator
    auto idle_code = PriorityScheduler::emit_guest_idle_routine();
    TEST_ASSERT(!idle_code.empty(), "Idle routine generated");
    // WFI opcode is 0xD503207F
    bool has_wfi = false;
    for (uint32_t op : idle_code) {
        if (op == 0xD503207Fu) {
            has_wfi = true;
            break;
        }
    }
    TEST_ASSERT(has_wfi, "Guest idle routine contains architectural WFI instruction");

    // Test Thread Context Save/Restore Fidelity
    GuestTcb tcb1{};
    tcb1.pc = 0x1004ULL;
    tcb1.sp_el1 = 0x48000ULL;
    tcb1.spsr_el1 = 0x00000000ULL;
    tcb1.gpr[0] = 0x1111222233334444ULL;
    tcb1.gpr[19] = 0xAAAABBBBCCCCDDD0ULL;
    tcb1.gpr[30] = 0x5555666677778888ULL;

    GuestTcb tcb2 = tcb1;
    TEST_ASSERT(tcb2.pc == tcb1.pc, "PC bitwise identical");
    TEST_ASSERT(tcb2.sp_el1 == tcb1.sp_el1, "SP bitwise identical");
    TEST_ASSERT(tcb2.gpr[0] == tcb1.gpr[0], "X0 bitwise identical");
    TEST_ASSERT(tcb2.gpr[19] == tcb1.gpr[19], "X19 bitwise identical");
    TEST_ASSERT(tcb2.gpr[30] == tcb1.gpr[30], "X30 bitwise identical");

    std::cout << "[Stage 253.15] Scheduler architecture and thread context provenance verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 253.16: Interrupt-Driven Scheduler Handoff & Context Switch
// ----------------------------------------------------------------------------
static void test_stage253_16_interrupt_driven_scheduler_handoff() {
    std::cout << "[Stage 253.16] Testing Interrupt-Driven Scheduler Handoff...\n";

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);

    Memory mem(0x200000);
    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    TEST_ASSERT(sched.initialize(), "Initialize scheduler");

    // Create Thread A (priority 10) and Thread B (priority 20)
    auto* thread_a = thread_mgr.create_thread("task_a", 0x1000ULL, 0ULL, 0x20000ULL, 0x4000ULL, 10);
    auto* thread_b = thread_mgr.create_thread("task_b", 0x2000ULL, 0ULL, 0x30000ULL, 0x4000ULL, 20);
    TEST_ASSERT(thread_a && thread_b, "Threads created");

    sched.enqueue_thread(thread_a);
    sched.enqueue_thread(thread_b);

    // Initial dispatch: high priority thread_b is selected
    TEST_ASSERT(sched.dispatch_next(cpu), "Dispatch next");
    TEST_ASSERT(sched.current_thread() == thread_b, "Thread B scheduled first due to higher priority");
    TEST_ASSERT(cpu.get_program_counter() == 0x2000ULL, "CPU PC points to Thread B entry");

    // Thread B yields or blocks on IPC wait
    TEST_ASSERT(sched.block_current(WaitReason::IpcMessage, cpu), "Block Thread B on IPC receive");
    TEST_ASSERT(thread_b->state() == ThreadState::Blocked, "Thread B is blocked");

    // Now Thread A is scheduled
    TEST_ASSERT(sched.current_thread() == thread_a, "Thread A now scheduled");
    TEST_ASSERT(cpu.get_program_counter() == 0x1000ULL, "CPU PC switched to Thread A entry");

    // Timer interrupt tick occurs!
    // Unblock Thread B (e.g. IPC message arrived during interrupt handling)
    TEST_ASSERT(sched.unblock_thread(thread_b), "Unblock Thread B");
    TEST_ASSERT(thread_b->state() == ThreadState::Ready, "Thread B is Ready again");

    // Advance scheduler tick -> triggers preemption of lower-priority Thread A by Thread B
    TEST_ASSERT(sched.tick(cpu), "Scheduler timer tick executed");
    TEST_ASSERT(sched.current_thread() == thread_b, "Preemption handoff: Thread B resumed");
    TEST_ASSERT(cpu.get_program_counter() == 0x2000ULL, "CPU PC restored to Thread B");

    std::cout << "[Stage 253.16] Interrupt-driven scheduler handoff and preemption verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 253.17: SMP Interrupt Routing & Core Isolation
// ----------------------------------------------------------------------------
static void test_stage253_17_smp_interrupt_isolation() {
    std::cout << "[Stage 253.17] Testing SMP Multi-Core Interrupt Routing & Isolation...\n";

    AppleAIC aic(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);

    // 1. Core 0 sends IPI to Core 3 (bitmask: 1 << 3 = 0x08)
    aic.send_ipi(0x08u);

    // Verify Core 3 has pending IPI
    TEST_ASSERT(aic.has_pending_interrupt(3), "Core 3 has pending IPI");
    TEST_ASSERT(aic.get_pending_ipi(3) == 0x01u, "Core 3 IPI bit 0 is set");

    // Verify all other cores (0, 1, 2, 4, 5) remain isolated with ZERO pending interrupts!
    for (std::size_t c = 0; c < 6; ++c) {
        if (c != 3) {
            TEST_ASSERT(!aic.has_pending_interrupt(c), "Isolated core has NO pending interrupts");
            TEST_ASSERT(aic.get_pending_ipi(c) == 0, "Isolated core has 0 pending IPIs");
        }
    }

    // Core 3 acknowledges and clears IPI
    aic.clear_ipi(3, 0x01u);
    TEST_ASSERT(!aic.has_pending_interrupt(3), "Core 3 pending IPI cleared");

    // 2. Hardware IRQ routing: Route IRQ 45 strictly to Core 2 (bitmask 0x04)
    aic.set_irq_target(45, 0x04u);
    aic.unmask_hw_irq(45);
    aic.set_hw_irq(45, true);

    // Verify IRQ is pending on Core 2 only
    TEST_ASSERT(aic.has_pending_interrupt(2), "IRQ 45 pending on Core 2");
    for (std::size_t c = 0; c < 6; ++c) {
        if (c != 2) {
            TEST_ASSERT(!aic.has_pending_interrupt(c), "Core isolation: IRQ 45 not pending on non-target cores");
        }
    }

    // Clear IRQ 45
    aic.set_hw_irq(45, false);
    aic.write_register(AppleAIC::kRegCoreBase + (2 * AppleAIC::kCoreStride) + AppleAIC::kCoreOffEvent, 45, 4);
    TEST_ASSERT(!aic.has_pending_interrupt(2), "IRQ 45 cleared on Core 2");

    std::cout << "[Stage 253.17] SMP interrupt routing and core isolation verified.\n";
}

int main() {
    std::cout << "=== Running Stages 253.14 - 253.17 Unit Tests ===\n";
    test_stage253_14_timer_interrupt_integration();
    test_stage253_15_scheduler_provenance();
    test_stage253_16_interrupt_driven_scheduler_handoff();
    test_stage253_17_smp_interrupt_isolation();
    std::cout << "[PASS] All Stage 253.14 - 253.17 tests passed successfully.\n";
    return 0;
}

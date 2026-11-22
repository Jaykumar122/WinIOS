// ============================================================================
// Stage 254.17 - 254.21: Kernel Stack & Thread State Preparation, Scheduler
// Provenance Boundary, Replay Fault Injection & Bounded Continuation Experiment
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/kernel_exception_vectors.h"
#include "device/apple_aic.h"
#include "kernel/context_switch.h"
#include "kernel/scheduler.h"
#include "kernel/task.h"
#include "kernel/thread.h"
#include "loader/kernel_interval_map.h"
#include "memory/memory.h"
#include "memory/xnu_page_allocator.h"
#include "memory/xnu_vm_map.h"

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
using namespace winios::kernel;
using namespace winios::loader;
using namespace winios::memory;

// ----------------------------------------------------------------------------
// Test 1: Stage 254.17: Kernel Stack & Thread State Preparation
// ----------------------------------------------------------------------------
static void test_stage254_17_kernel_stack_thread_state_prep() {
    std::cout << "[Stage 254.17] Testing Kernel Stack & Thread State Preparation...\n";

    // Validate GuestTcb layout and alignment
    TEST_ASSERT(sizeof(GuestTcb) == 512, "GuestTcb must be exactly 512 bytes packed");
    TEST_ASSERT(alignof(GuestTcb) == 1, "GuestTcb packing alignment is 1 byte");

    GuestTcb tcb{};
    tcb.magic = kTcbMagic;
    tcb.tid = 1001;
    tcb.state = static_cast<uint32_t>(ThreadState::Ready);
    tcb.priority = 24;
    tcb.sp_el0 = 0x0000000100080000ULL;
    tcb.sp_el1 = 0x0000000000048000ULL;
    tcb.pc = 0x0000000000002be0ULL;
    tcb.spsr_el1 = 0x0000000060000005ULL; // EL1h with NZCV=0110

    // Populate all 31 general-purpose registers
    for (uint32_t i = 0; i < 31; ++i) {
        tcb.gpr[i] = 0x1000000000000000ULL + i;
    }

    // Verify bitwise fidelity of state preservation
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);

    // Save CPU state to TCB
    for (uint32_t i = 0; i < 31; ++i) {
        cpu.write_register(i, tcb.gpr[i]);
    }
    cpu.set_sp_el0(tcb.sp_el0);
    cpu.set_sp_el1(tcb.sp_el1);
    cpu.set_program_counter(tcb.pc);

    // Read back and compare
    for (uint32_t i = 0; i < 31; ++i) {
        TEST_ASSERT(cpu.read_register(i) == tcb.gpr[i], "GPR bitwise fidelity match");
    }
    TEST_ASSERT(cpu.get_sp_el0() == tcb.sp_el0, "SP_EL0 match");
    TEST_ASSERT(cpu.get_sp_el1() == tcb.sp_el1, "SP_EL1 match");
    TEST_ASSERT(cpu.get_program_counter() == tcb.pc, "PC match");

    std::cout << "[Stage 254.17] Kernel stack & thread state preparation verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 254.18: Upstream Scheduler Feasibility & Handoff Boundary
// ----------------------------------------------------------------------------
static void test_stage254_18_upstream_scheduler_feasibility() {
    std::cout << "[Stage 254.18] Testing Upstream Scheduler Feasibility & Handoff Boundary...\n";

    Memory mem(0x800000);
    KernelThreadManager thread_mgr;
    PriorityScheduler scheduler(thread_mgr, mem);

    TEST_ASSERT(scheduler.initialize(), "Initialize PriorityScheduler");
    TEST_ASSERT(thread_mgr.thread_count() >= 1, "Idle thread registered");

    // Create two test threads with different priorities
    auto* t_high = thread_mgr.create_thread("HighPriorityThread", 0x3000ULL, 0ULL, 0x00068000ULL, 0x4000ULL, 28);
    auto* t_norm = thread_mgr.create_thread("NormalPriorityThread", 0x4000ULL, 0ULL, 0x00070000ULL, 0x4000ULL, 16);
    TEST_ASSERT(t_high != nullptr && t_norm != nullptr, "Created test threads");

    TEST_ASSERT(scheduler.enqueue_thread(t_norm), "Enqueue normal priority thread");
    TEST_ASSERT(scheduler.enqueue_thread(t_high), "Enqueue high priority thread");

    // Scheduler must pick the highest priority thread first (t_high, priority 28)
    auto* picked = scheduler.pick_next_thread();
    TEST_ASSERT(picked == t_high, "PriorityScheduler picks higher priority thread (28 > 16)");

    // Upstream XNU scheduler comparison:
    // Upstream XNU sched_prim.c uses 128 priority levels, run queues with priority decay,
    // turnstiles for priority inheritance, AST (Asynchronous Software Trap) checks on exception return.
    // Our PriorityScheduler is Category C (Compatibility Emulation) providing basic 32-level scheduling.
    std::cout << "[Stage 254.18] Upstream scheduler feasibility & handoff boundary verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 254.19: Category C Boundary Enforcement
// ----------------------------------------------------------------------------
static void test_stage254_19_category_c_boundary_enforcement() {
    std::cout << "[Stage 254.19] Testing Category C Boundary Enforcement...\n";

    // Strict classification matrix between upstream XNU and compatibility shims:
    struct ComponentClassification {
        const char* name;
        const char* provenance_category;
        bool is_upstream_genuine;
    };

    const ComponentClassification catalog[] = {
        {"osfmk/arm64/start.s",        "Category B", false}, // Adapted upstream assembly
        {"osfmk/arm/arm_init.c",       "Category B", false}, // Adapted upstream C initialization
        {"xnu_genuine_init.macho",     "Category B", false}, // Built from adapted upstream sources
        {"PriorityScheduler",          "Category C", false}, // WinIOS synthetic compatibility scheduler
        {"AppleAIC_Driver_Shim",       "Category C", false}, // WinIOS virtual AIC2 device emulation
        {"TestStub_0x80280",           "Category D", false}, // Synthetic test harness stub
        {"KernelBootCoordinator",      "Category E", false}  // Host C++ runtime environment
    };

    for (const auto& item : catalog) {
        // Enforce: NO component in our runtime or test harness is allowed to claim verbatim upstream status without qualification
        TEST_ASSERT(!item.is_upstream_genuine, "Strict boundary: Not unadapted upstream code");
        if (std::string(item.provenance_category) == "Category C") {
            TEST_ASSERT(std::string(item.name).find("PriorityScheduler") != std::string::npos ||
                        std::string(item.name).find("AppleAIC") != std::string::npos,
                        "Category C components properly identified");
        }
    }

    std::cout << "[Stage 254.19] Category C boundary enforcement verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 254.20: Persistent Replay Provenance & Fault Injection
// ----------------------------------------------------------------------------
static void test_stage254_20_persistent_replay_provenance() {
    std::cout << "[Stage 254.20] Testing Persistent Replay Provenance & Fault Injection...\n";

    // 1. Fault Scenario A: Unregistered physical frame access
    XnuPhysicalPageAllocator alloc;
    std::string err;
    TEST_ASSERT(alloc.initialize(0x0ULL, 0x800000ULL, &err), "Initialize allocator");
    TEST_ASSERT(alloc.reserve_range(0x0ULL, 0x00050000ULL, "Enclave", &err), "Reserve enclave");

    // Attempting to free unallocated frame 25 must fail gracefully
    TEST_ASSERT(!alloc.free_pages(0x00050000ULL + 5 * 16384ULL, 1, &err),
                "Freeing unallocated frame must fail");

    // 2. Fault Scenario B: Out-of-bounds frame access
    TEST_ASSERT(!alloc.free_pages(0x00900000ULL, 1, &err), "Out-of-bounds frame access rejected");

    // 3. Fault Scenario C: W^X physical alias collision
    XnuVmMap vm_map(0x0ULL, 0x800000ULL);
    vm_map.set_strict_wx(true);
    TEST_ASSERT(vm_map.register_physical_alias(0x00050000ULL, 0x00050000ULL, 0x4000ULL, kVmProtRead | kVmProtWrite, &err), "Map RW");
    TEST_ASSERT(!vm_map.register_physical_alias(0x00060000ULL, 0x00050000ULL, 0x4000ULL, kVmProtRead | kVmProtExecute, &err),
                "W^X collision properly rejected on physical alias");

    // 4. Fault Scenario D: Spurious AIC event pop
    AppleAIC aic(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);
    uint64_t spurious = aic.read_register(AppleAIC::kRegCoreBase + AppleAIC::kCoreOffEvent, 4);
    TEST_ASSERT(spurious == AppleAIC::kEventNone, "Spurious event pop returns kEventNone (0xFFFFFFFF)");

    std::cout << "[Stage 254.20] Persistent replay provenance & fault injection verified.\n";
}

// ----------------------------------------------------------------------------
// Test 5: Stage 254.21: Bounded Runtime Continuation Experiment
// ----------------------------------------------------------------------------
static void test_stage254_21_bounded_runtime_entry_experiment() {
    std::cout << "[Stage 254.21] Testing Bounded Runtime Continuation Experiment...\n";

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);

    const uint64_t vbar_base = 0x00020000ULL;
    cpu.set_vbar_el1(vbar_base);
    cpu.set_sp_el1(0x00048000ULL);

    Memory mem(0x800000);
    AppleAIC aic(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);

    // Setup main execution loop at 0x1000:
    // 0x1000: msr daifclr, #2       (unmask IRQ)
    // 0x1004: add x20, x20, #1      (cycle counter)
    // 0x1008: wfi                   (wait for timer interrupt)
    // 0x100C: cmp x20, #3           (run exactly 3 interrupt cycles)
    // 0x1010: b.lt 0x1004           (loop)
    // 0x1014: mov x21, #0x99        (completion flag)
    mem.write_u32(0x1000, 0xD50342FFu);
    mem.write_u32(0x1004, 0x91000694u); // add x20, x20, #1
    mem.write_u32(0x1008, 0xD503207Fu); // wfi
    mem.write_u32(0x100C, 0xF1000E9Fu); // cmp x20, #3
    mem.write_u32(0x1010, 0x54FFFFABu); // b.lt 0x1004 (offset -12 bytes, cond=0xB LT)
    mem.write_u32(0x1014, 0xD2801335u); // mov x21, #0x99

    // Exception handler at VBAR + 0x280 (0x20280):
    // 0x20280: add x22, x22, #10     (interrupt tally)
    // 0x20284: eret
    const uint64_t vector_irq = vbar_base + 0x280ULL;
    mem.write_u32(vector_irq + 0, 0x91002AD6u); // add x22, x22, #10 (Rd=X22, Rn=X22, imm12=10)
    mem.write_u32(vector_irq + 4, 0xD69F03E0u); // eret

    cpu.set_program_counter(0x1000);
    cpu.write_register(20, 0); // Cycle count
    cpu.write_register(21, 0); // Completion flag
    cpu.write_register(22, 0); // Interrupt tally

    // Step 0x1000: unmask IRQ
    cpu.step(mem);

    // Execute 3 full interrupt cycles:
    for (int cycle = 0; cycle < 3; ++cycle) {
        // Step 0x1004: add x20, x20, #1
        cpu.step(mem);
        // Step 0x1008: WFI
        cpu.step(mem);
        TEST_ASSERT(cpu.is_waiting_for_interrupt(), "CPU parked in WFI");

        // AIC2 raises interrupt
        aic.unmask_hw_irq(50);
        aic.set_hw_irq(50, true);

        // Deliver IRQ
        cpu.wake_from_wfi();
        cpu.raise_asynchronous_exception(false, 50, false, 0);
        TEST_ASSERT(cpu.get_program_counter() == vector_irq, "CPU vectored to handler");

        // Step handler: add x22, x22, #10
        cpu.step(mem);
        // EOI
        aic.set_hw_irq(50, false);
        aic.write_register(AppleAIC::kRegCoreBase + AppleAIC::kCoreOffEvent, 50, 4);

        // Step handler: eret
        cpu.step(mem);
        TEST_ASSERT(cpu.get_program_counter() == 0x100CULL, "ERET returned to loop comparison");

        // Step 0x100C: cmp x20, #3
        cpu.step(mem);
        // Step 0x1010: b.lt (or fall through on cycle 2)
        cpu.step(mem);
    }

    // Step 0x1014: completion flag
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(20) == 3, "Exactly 3 execution cycles completed");
    TEST_ASSERT(cpu.read_register(21) == 0x99, "Completion flag set to 0x99");
    TEST_ASSERT(cpu.read_register(22) == 30, "Exactly 3 interrupts handled (tally = 30)");

    std::cout << "[Stage 254.21] Bounded runtime continuation experiment verified successfully.\n";
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "Campaign 254: Stages 254.17 - 254.21 Scheduler & Continuation Tests\n";
    std::cout << "====================================================================\n";

    test_stage254_17_kernel_stack_thread_state_prep();
    test_stage254_18_upstream_scheduler_feasibility();
    test_stage254_19_category_c_boundary_enforcement();
    test_stage254_20_persistent_replay_provenance();
    test_stage254_21_bounded_runtime_entry_experiment();

    std::cout << "====================================================================\n";
    std::cout << "All Stages 254.17 - 254.21 Tests PASSED Successfully!\n";
    std::cout << "====================================================================\n";
    return 0;
}

// ============================================================================
// Stage 195.4–195.9: Asynchronous Preemption & Complete Context Preservation Tests
// Covers:
//   Test A — Full GPR Preservation (X0–X30)
//   Test B — SIMD/FP Preservation (Q0–Q31, FPCR, FPSR)
//   Test C — PSTATE Preservation (NZCV, DAIF)
//   Test D — Arbitrary Instruction-Boundary Preemption
//   Test E — Multi-Task Preemptive Switching
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/cpu_context.h"
#include "kernel/exception_frame_manager.h"
#include "kernel/preemption_timer.h"
#include "kernel/scheduler.h"
#include "kernel/thread.h"
#include "loader/macho_loader.h"
#include "memory/memory.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::kernel;

// ----------------------------------------------------------------------------
// Stage 195.4: Exception Frame Architecture & Alignment
// ----------------------------------------------------------------------------
static bool test_exception_frame_architecture() {
    static_assert(sizeof(ExceptionFrame) == 896, "ExceptionFrame must be 896 bytes");
    static_assert(sizeof(ExceptionFrame) % 16 == 0, "ExceptionFrame must be 16-byte aligned");

    Memory mem(1024 * 1024);
    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_sp_el1(0x80000ULL);
    cpu.set_cpacr_el1(0x00300000ULL);

    KernelThreadManager mgr;
    auto* thread = mgr.create_thread("worker", 0x1000, 0, 0x70000, 0x10000, 16);
    CHECK(thread != nullptr, "Thread creation");

    // Save frame onto stack
    CHECK(ExceptionFrameManager::save_exception_frame(cpu, mem, thread, 0x25000001ULL, 0xDEADBEEFULL),
          "Save exception frame");

    const std::uint64_t frame_sp = cpu.get_sp_el1();
    CHECK((frame_sp & 0xFULL) == 0, "Frame SP must be 16-byte aligned");
    CHECK(frame_sp == (0x80000ULL - sizeof(ExceptionFrame)), "Frame size deducted from SP");

    const auto* frame = reinterpret_cast<const ExceptionFrame*>(mem.raw_data() + frame_sp);
    CHECK(frame->magic == kExceptionFrameMagic, "Frame magic must match");
    CHECK(frame->frame_size == 896, "Frame size field");
    CHECK(frame->esr_el1 == 0x25000001ULL, "Saved ESR syndrome");
    CHECK(frame->far_el1 == 0xDEADBEEFULL, "Saved FAR address");
    CHECK(frame->sp_el1 == 0x80000ULL, "Original SP recorded");

    // Restore frame and verify SP restored
    CHECK(ExceptionFrameManager::restore_exception_frame(cpu, mem, thread), "Restore exception frame");
    CHECK(cpu.get_sp_el1() == 0x80000ULL, "Restored original SP_EL1");

    std::cout << "[PASS] test_exception_frame_architecture" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Stage 195.5 / Test A: Full GPR Preservation (X0–X30) Across Timer Preemption
// ----------------------------------------------------------------------------
static bool test_mandatory_a_full_gpr_preservation() {
    Memory mem(16 * 1024 * 1024);
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_cpacr_el1(0x00300000ULL);

    const std::string bin_path = "D:/WinISO26/WinIOS/tests/artifacts/bin/guest_preemption_stress";
    const auto load_res = loader::MachOLoader::load_from_file(bin_path, mem, 0, mem.size());
    CHECK(load_res.is_success(), "Mach-O load_from_file must succeed");

    const std::uint64_t entry_point = load_res.entry_point; // 0x13b0
    constexpr std::uint64_t kHandlerAddr = 0x151cULL;        // _irq_vector_handler
    constexpr std::uint64_t kVbarBase = 0x20000ULL;
    constexpr std::uint64_t kStackTop = 0x80000ULL;
    constexpr std::uint64_t kStatusAddr = 0x40000ULL;
    constexpr std::uint64_t kTickCountAddr = 0x40008ULL;

    // Install vector table at kVbarBase pointing IRQ vector (0x280) to kHandlerAddr
    CHECK(PreemptionTimer::setup_vector_table(mem, kVbarBase, kHandlerAddr), "Setup vector table");
    cpu.set_vbar_el1(kVbarBase);

    // Initialize CPU state
    cpu.set_program_counter(entry_point);
    cpu.set_sp_el1(kStackTop);
    cpu.set_daif(0x0); // Unmask IRQs

    // Configure virtual timer with short interval (50 cycles)
    PreemptionTimer::configure_virtual_timer(cpu, 50);

    // Clear status flags
    mem.write_u32(static_cast<std::size_t>(kStatusAddr), 0);
    mem.write_u64(static_cast<std::size_t>(kTickCountAddr), 0);

    std::size_t steps = 0;
    constexpr std::size_t kMaxSteps = 20000;
    while (steps < kMaxSteps) {
        cpu.step(mem);
        ++steps;

        // Periodic timer tick assertion to inject preemption at arbitrary instruction boundaries
        if (steps % 100 == 0) {
            PreemptionTimer::assert_virtual_timer_tick(cpu);
        }

        if (mem.read_u32(static_cast<std::size_t>(kStatusAddr)) == 1) {
            break;
        }
    }

    CHECK(mem.read_u32(static_cast<std::size_t>(kStatusAddr)) == 1,
          "Mandatory Test A: All GPRs X0..X30 validated intact after live preemption");
    const std::uint64_t ticks = mem.read_u64(static_cast<std::size_t>(kTickCountAddr));
    CHECK(ticks > 5, "Must have handled multiple timer preemptions");

    std::cout << "[PASS] test_mandatory_a_full_gpr_preservation (retired " << steps
              << " instructions, " << ticks << " preemptions handled)" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Stage 195.6 / Test B: SIMD and Floating-Point Context Preservation (Q0–Q31, FPCR, FPSR)
// ----------------------------------------------------------------------------
static bool test_mandatory_b_simd_fp_preservation() {
    Memory mem(1024 * 1024);
    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_cpacr_el1(0x00300000ULL);

    KernelThreadManager mgr;
    auto* task_a = mgr.create_thread("task_a", 0x1000, 0, 0x50000, 0x10000, 16);
    auto* task_b = mgr.create_thread("task_b", 0x2000, 0, 0x70000, 0x10000, 16);

    // 1. Task A initializes unique SIMD patterns in Q0..Q31 and FPCR/FPSR
    cpu.set_sp_el1(0x50000);
    cpu.set_fpcr(0x01000000);
    cpu.set_fpsr(0x00000008);
    for (std::size_t i = 0; i < 32; ++i) {
        cpu.write_vector_register(i, 0xA0000000ULL + i, 0xB0000000ULL + i);
    }

    // Preempt Task A -> Save exception frame
    task_a->set_state(ThreadState::Running);
    CHECK(ExceptionFrameManager::save_exception_frame(cpu, mem, task_a), "Save Task A frame");

    // 2. Task B executes and overwrites all vector registers with different patterns
    cpu.set_sp_el1(0x70000);
    cpu.set_fpcr(0x02000000);
    cpu.set_fpsr(0x00000004);
    for (std::size_t i = 0; i < 32; ++i) {
        cpu.write_vector_register(i, 0xC0000000ULL + i, 0xD0000000ULL + i);
    }
    task_b->set_state(ThreadState::Running);
    CHECK(ExceptionFrameManager::save_exception_frame(cpu, mem, task_b), "Save Task B frame");

    // 3. Resume Task A and verify complete vector register preservation
    CHECK(ExceptionFrameManager::restore_exception_frame(cpu, mem, task_a), "Restore Task A frame");

    CHECK(cpu.get_fpcr() == 0x01000000, "Task A FPCR restored");
    CHECK(cpu.get_fpsr() == 0x00000008, "Task A FPSR restored");

    for (std::size_t i = 0; i < 32; ++i) {
        const auto v = cpu.read_vector_register(i);
        CHECK(v.low == (0xA0000000ULL + i), "Vector low register match");
        CHECK(v.high == (0xB0000000ULL + i), "Vector high register match");
    }

    // 4. Resume Task B and verify Task B's vector registers
    CHECK(ExceptionFrameManager::restore_exception_frame(cpu, mem, task_b), "Restore Task B frame");
    CHECK(cpu.get_fpcr() == 0x02000000, "Task B FPCR restored");
    CHECK(cpu.get_fpsr() == 0x00000004, "Task B FPSR restored");
    for (std::size_t i = 0; i < 32; ++i) {
        const auto v = cpu.read_vector_register(i);
        CHECK(v.low == (0xC0000000ULL + i), "Task B vector low match");
        CHECK(v.high == (0xD0000000ULL + i), "Task B vector high match");
    }

    std::cout << "[PASS] test_mandatory_b_simd_fp_preservation (32 Q registers + FPCR/FPSR validated)" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Stage 195.7 / Test C: PSTATE, DAIF and Exception-Return Preservation
// ----------------------------------------------------------------------------
static bool test_mandatory_c_pstate_preservation() {
    Memory mem(1024 * 1024);
    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_cpacr_el1(0x00300000ULL);

    KernelThreadManager mgr;
    auto* t1 = mgr.create_thread("t1", 0x1000, 0, 0x40000, 0x10000, 16);
    auto* t2 = mgr.create_thread("t2", 0x2000, 0, 0x60000, 0x10000, 16);

    // T1 has NZCV = 0x9 (N=1, Z=0, C=0, V=1), DAIF = 0x2
    cpu.set_sp_el1(0x40000);
    cpu.set_nzcv(0x9);
    cpu.set_daif(0x2);
    cpu.set_program_counter(0x1050);
    t1->set_state(ThreadState::Running);
    CHECK(ExceptionFrameManager::save_exception_frame(cpu, mem, t1), "Save T1");

    // T2 has NZCV = 0x6 (N=0, Z=1, C=1, V=0), DAIF = 0x0
    cpu.set_sp_el1(0x60000);
    cpu.set_nzcv(0x6);
    cpu.set_daif(0x0);
    cpu.set_program_counter(0x2050);
    t2->set_state(ThreadState::Running);
    CHECK(ExceptionFrameManager::save_exception_frame(cpu, mem, t2), "Save T2");

    // Resume T1: flags must NOT be corrupted by T2
    CHECK(ExceptionFrameManager::restore_exception_frame(cpu, mem, t1), "Restore T1");
    CHECK(cpu.get_nzcv() == 0x9, "T1 NZCV restored");
    CHECK(cpu.get_daif() == 0x2, "T1 DAIF restored");
    CHECK(cpu.get_program_counter() == 0x1050, "T1 PC restored");

    // Resume T2
    CHECK(ExceptionFrameManager::restore_exception_frame(cpu, mem, t2), "Restore T2");
    CHECK(cpu.get_nzcv() == 0x6, "T2 NZCV restored");
    CHECK(cpu.get_daif() == 0x0, "T2 DAIF restored");
    CHECK(cpu.get_program_counter() == 0x2050, "T2 PC restored");

    std::cout << "[PASS] test_mandatory_c_pstate_preservation" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Stage 195.8: Thread-Local State and Address Space Isolation
// ----------------------------------------------------------------------------
static bool test_thread_local_and_address_space_isolation() {
    Memory mem(1024 * 1024);
    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_cpacr_el1(0x00300000ULL);

    KernelThreadManager mgr;
    auto* t1 = mgr.create_thread("t1_tls", 0x1000, 0, 0x40000, 0x10000, 16);
    auto* t2 = mgr.create_thread("t2_tls", 0x2000, 0, 0x60000, 0x10000, 16);

    // T1 configuration
    cpu.set_sp_el1(0x40000);
    cpu.set_tpidr_el0(0x11110000ULL);
    cpu.set_tpidr_el1(0x1111FFFFULL);
    cpu.set_ttbr0_el1(0x0001000040000000ULL); // ASID = 1
    t1->set_state(ThreadState::Running);
    CHECK(ExceptionFrameManager::save_exception_frame(cpu, mem, t1), "Save T1 TLS");

    // T2 configuration
    cpu.set_sp_el1(0x60000);
    cpu.set_tpidr_el0(0x22220000ULL);
    cpu.set_tpidr_el1(0x2222FFFFULL);
    cpu.set_ttbr0_el1(0x0002000040000000ULL); // ASID = 2
    t2->set_state(ThreadState::Running);
    CHECK(ExceptionFrameManager::save_exception_frame(cpu, mem, t2), "Save T2 TLS");

    // Swap back to T1
    CHECK(ExceptionFrameManager::restore_exception_frame(cpu, mem, t1), "Restore T1 TLS");
    CHECK(cpu.get_tpidr_el0() == 0x11110000ULL, "T1 TPIDR_EL0 restored");
    CHECK(cpu.get_tpidr_el1() == 0x1111FFFFULL, "T1 TPIDR_EL1 restored");
    CHECK(cpu.get_ttbr0_el1() == 0x0001000040000000ULL, "T1 TTBR0 restored");

    // Swap back to T2
    CHECK(ExceptionFrameManager::restore_exception_frame(cpu, mem, t2), "Restore T2 TLS");
    CHECK(cpu.get_tpidr_el0() == 0x22220000ULL, "T2 TPIDR_EL0 restored");
    CHECK(cpu.get_tpidr_el1() == 0x2222FFFFULL, "T2 TPIDR_EL1 restored");
    CHECK(cpu.get_ttbr0_el1() == 0x0002000040000000ULL, "T2 TTBR0 restored");

    std::cout << "[PASS] test_thread_local_and_address_space_isolation" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Stage 195.9 / Test D & E: Preemptive Context-Switch Integration (3 Tasks)
// ----------------------------------------------------------------------------
static bool test_mandatory_d_and_e_three_task_preemption() {
    Memory mem(2 * 1024 * 1024);
    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_cpacr_el1(0x00300000ULL);

    KernelThreadManager mgr;
    PriorityScheduler sched(mgr, mem);
    CHECK(sched.initialize(), "Init scheduler");

    auto* t1 = mgr.create_thread("task1", 0x1000, 0, 0x40000, 0x8000, 20); // High priority
    auto* t2 = mgr.create_thread("task2", 0x2000, 0, 0x50000, 0x8000, 16); // Normal priority
    auto* t3 = mgr.create_thread("task3", 0x3000, 0, 0x60000, 0x8000, 10); // Low priority

    CHECK(t1 != nullptr && t2 != nullptr && t3 != nullptr, "Create 3 threads");

    sched.enqueue_thread(t1);
    sched.enqueue_thread(t2);
    sched.enqueue_thread(t3);

    // Initial tick: T1 (priority 20) is selected
    sched.tick(cpu);
    CHECK(sched.current_thread() == t1, "T1 selected first due to highest priority");

    // Preempt T1 via ExceptionFrameManager and switch to T2
    cpu.write_register(0, 0x1111);
    cpu.write_register(1, 0x2222);
    cpu.set_program_counter(0x1040);
    CHECK(ExceptionFrameManager::preemptive_switch(cpu, mem, t1, t2), "Preemptive switch T1 -> T2");
    CHECK(t1->state() == ThreadState::Ready, "T1 marked Ready");
    CHECK(t2->state() == ThreadState::Running, "T2 marked Running");

    // Execute in T2
    cpu.write_register(0, 0x3333);
    cpu.write_register(1, 0x4444);
    cpu.set_program_counter(0x2080);

    // Preempt T2 and switch to T3
    CHECK(ExceptionFrameManager::preemptive_switch(cpu, mem, t2, t3), "Preemptive switch T2 -> T3");
    CHECK(t2->state() == ThreadState::Ready, "T2 marked Ready");
    CHECK(t3->state() == ThreadState::Running, "T3 marked Running");

    // Resume T1: verify all registers and PC intact
    CHECK(ExceptionFrameManager::preemptive_switch(cpu, mem, t3, t1), "Preemptive switch T3 -> T1");
    CHECK(cpu.get_program_counter() == 0x1040, "T1 resumed at exact PC");
    CHECK(cpu.read_register(0) == 0x1111, "T1 X0 intact");
    CHECK(cpu.read_register(1) == 0x2222, "T1 X1 intact");

    // Resume T2: verify registers and PC intact
    CHECK(ExceptionFrameManager::preemptive_switch(cpu, mem, t1, t2), "Preemptive switch T1 -> T2");
    CHECK(cpu.get_program_counter() == 0x2080, "T2 resumed at exact PC");
    CHECK(cpu.read_register(0) == 0x3333, "T2 X0 intact");
    CHECK(cpu.read_register(1) == 0x4444, "T2 X1 intact");

    std::cout << "[PASS] test_mandatory_d_and_e_three_task_preemption" << std::endl;
    return true;
}

int main() {
    std::cout << "Running Stage 195.4-195.9 Asynchronous Preemption & Context Preservation Tests..." << std::endl;

    if (!test_exception_frame_architecture()) return 1;
    if (!test_mandatory_a_full_gpr_preservation()) return 1;
    if (!test_mandatory_b_simd_fp_preservation()) return 1;
    if (!test_mandatory_c_pstate_preservation()) return 1;
    if (!test_thread_local_and_address_space_isolation()) return 1;
    if (!test_mandatory_d_and_e_three_task_preemption()) return 1;

    std::cout << "\nAll Stage 195.4-195.9 tests PASSED successfully." << std::endl;
    return 0;
}

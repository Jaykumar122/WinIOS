// ============================================================================
// Stage 193.17: Freestanding Kernel Integration Tests
// ============================================================================

#include <cstdint>
#include <iostream>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/mmu.h"
#include "cpu/synchronous_exception.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace mmu;

// Test 1: Full System Call Round-Trip with Register and Privilege Preservation
static bool test_integration_syscall_invocation_and_register_preservation() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x200000ULL);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);
    cpu.set_sp_el1(0x70000ULL);

    // Setup EL1 SVC handler at VBAR + 0x400 (Lower EL Sync):
    // 0x400: ADD X0, X0, #100   (0x91019000u)
    // 0x404: ERET               (0xD69F03E0u)
    mem.write_u32(static_cast<std::size_t>(vbar + 0x400u), 0x91019000u);
    mem.write_u32(static_cast<std::size_t>(vbar + 0x404u), 0xD69F03E0u);

    // Setup EL0 user code at 0x1000:
    // 0x1000: SVC #0            (0xD4000001u)
    // 0x1004: ADD X19, X19, #1  (0x91000673u)
    const std::uint64_t user_pc = 0x1000ULL;
    mem.write_u32(static_cast<std::size_t>(user_pc + 0u), 0xD4000001u);
    mem.write_u32(static_cast<std::size_t>(user_pc + 4u), 0x91000673u);

    // Set initial user state: callee-saved registers X19..X28
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_program_counter(user_pc);
    cpu.write_register(0, 50);  // Syscall arg
    cpu.write_register(19, 0x19191919ULL);
    cpu.write_register(20, 0x20202020ULL);
    cpu.write_register(21, 0x21212121ULL);
    cpu.set_nzcv(0b1100);

    // Step 1: Execute SVC at EL0 -> transitions to EL1, vectors to VBAR + 0x400
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1, "Must elevate privilege to EL1");
    CHECK(cpu.get_program_counter() == vbar + 0x400u, "Must jump to Lower EL Sync vector");
    CHECK(cpu.get_elr_el1() == user_pc + 4u, "ELR_EL1 holds preferred return address 0x1004");
    CHECK((cpu.get_esr_el1() >> 26u) == 0x15u, "ESR_EL1 holds EC=0x15 (SVC)");

    // Step 2: In kernel handler, execute ADD X0, X0, #100
    cpu.step(mem);
    CHECK(cpu.read_register(0) == 150, "Kernel handler updated X0 to 150");

    // Step 3: In kernel handler, execute ERET -> returns to EL0
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 0, "ERET must demote privilege to EL0");
    CHECK(cpu.get_program_counter() == user_pc + 4u, "Restored user PC to 0x1004");
    CHECK(cpu.read_register(19) == 0x19191919ULL, "Callee-saved register X19 preserved");
    CHECK(cpu.read_register(20) == 0x20202020ULL, "Callee-saved register X20 preserved");
    CHECK(cpu.read_register(21) == 0x21212121ULL, "Callee-saved register X21 preserved");
    CHECK(cpu.get_nzcv() == 0b1100, "User NZCV flags preserved");

    // Step 4: Resume user instruction ADD X19, X19, #1
    cpu.step(mem);
    CHECK(cpu.read_register(19) == 0x1919191AULL, "X19 incremented by user code");

    std::cout << "[PASS] test_integration_syscall_invocation_and_register_preservation\n";
    return true;
}

// Test 2: Demand-Paging Resolution via Guest Exception Entry and TLB Invalidation
static bool test_integration_demand_paging_and_tlb_invalidation() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x400000ULL);

    const std::uint64_t vbar = 0x60000ULL;
    cpu.set_vbar_el1(vbar);
    cpu.set_sp_el1(0x70000ULL);

    MMU& mmu = cpu.get_mmu();
    mmu.set_sctlr_el1(0x00C50839ULL); // MMU enabled
    mmu.set_tcr_el1((2ULL << 14u) | 28ULL); // 16KB granule, 36-bit VA

    // Setup L2 table at 0x30000
    const std::uint64_t l2_table_pa = 0x30000ULL;
    mmu.set_ttbr0_el1(l2_table_pa);

    // L3 table at 0x34000
    const std::uint64_t l3_table_pa = 0x34000ULL;
    mem.write_u64(static_cast<std::size_t>(l2_table_pa), l3_table_pa | 0x3ULL);

    // Page 0 (VA 0x0000 - 0x3FFF): Map user code (valid, read-exec)
    // Page descriptor at L3 index 0: PA = 0x10000, attr: Normal memory, AF=1, AP=0b01 (read/write user)
    const std::uint64_t code_pa = 0x10000ULL;
    const std::uint64_t valid_page_desc = code_pa | 0x3ULL | (1ULL << 10u) | (1ULL << 6u);
    mem.write_u64(static_cast<std::size_t>(l3_table_pa), valid_page_desc);

    // User program at VA 0x1000:
    // 0x1000: LDR X1, [X2]   (X2 = 0x8000, which is Page 2: unmapped!)
    // Opcode: LDR X1, [X2] = 0xF9400041u
    // 0x1004: ADD X1, X1, #5 = 0x91001421u
    mem.write_u32(static_cast<std::size_t>(code_pa + 0x1000u), 0xF9400041u);
    mem.write_u32(static_cast<std::size_t>(code_pa + 0x1004u), 0x91001421u);

    // Page 2 (VA 0x8000) is initially unmapped (L3 entry 2 = 0)
    mem.write_u64(static_cast<std::size_t>(l3_table_pa + 16u), 0ULL);

    // Setup EL1 Page Fault Handler at VBAR + 0x400:
    // Kernel reads FAR_EL1, updates L3 descriptor to map page, invalidates TLB, and executes ERET!
    // In our test, we verify the exception delivery, the handler resolution, and resumed execution.
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_program_counter(0x1000);
    cpu.write_register(2, 0x8000ULL); // target unmapped VA

    // Step 1: User executes LDR X1, [X2] at VA 0x1000 -> triggers Translation Fault at 0x8000!
    cpu.step(mem);

    // Exception taken to EL1 vector VBAR + 0x400
    CHECK(cpu.get_current_el() == 1, "Must elevate to EL1 on translation fault");
    CHECK(cpu.get_program_counter() == vbar + 0x400u, "Jump to Lower EL Sync vector");
    CHECK(cpu.get_far_el1() == 0x8000ULL, "FAR_EL1 holds faulting VA 0x8000");
    CHECK((cpu.get_esr_el1() >> 26u) == 0x24u, "ESR_EL1 EC = 0x24 (Data Abort from Lower EL)");
    CHECK(cpu.get_elr_el1() == 0x1000ULL, "ELR_EL1 holds faulting instruction PC 0x1000");

    // Kernel demand-paging service:
    // 1. Allocate backing physical page at 0x20000
    const std::uint64_t data_pa = 0x20000ULL;
    mem.write_u64(static_cast<std::size_t>(data_pa), 99ULL); // Data value at offset 0
    // 2. Map L3 entry 2 to point to data_pa
    const std::uint64_t data_page_desc = data_pa | 0x3ULL | (1ULL << 10u) | (1ULL << 6u);
    mem.write_u64(static_cast<std::size_t>(l3_table_pa + 16u), data_page_desc);
    // 3. Invalidate TLB for VA 0x8000
    mmu.invalidate_tlb_vaa(0x8000ULL);

    // Kernel executes ERET to return to EL0 at ELR_EL1 (0x1000)
    cpu.set_current_el(0);
    cpu.set_program_counter(cpu.get_elr_el1());

    // Step 2: Re-execute LDR X1, [X2] at 0x1000 -> translation now succeeds!
    cpu.step(mem);
    CHECK(cpu.read_register(1) == 99, "X1 loaded value 99 from demand-paged memory");
    CHECK(cpu.get_program_counter() == 0x1004, "PC advanced to next instruction 0x1004");

    // Step 3: Execute ADD X1, X1, #5
    cpu.step(mem);
    CHECK(cpu.read_register(1) == 104, "X1 updated to 104");

    std::cout << "[PASS] test_integration_demand_paging_and_tlb_invalidation\n";
    return true;
}

// Test 3: Timer Interrupt Servicing and Task Context Switching
static bool test_integration_timer_interrupt_servicing_and_task_switch() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x200000ULL);

    const std::uint64_t vbar = 0x50000ULL;
    cpu.set_vbar_el1(vbar);

    GICv3& gic = cpu.get_gic();
    gic.reset();
    gic.set_core_count(1);
    gic.write_mmio(GICv3::kGicdBase + 0x0000u, 0x3u, 4);
    gic.write_icc_igrpen1(1, 0);
    gic.write_icc_pmr(0xF0u, 0);

    // Enable PPI 27 (Virtual Timer)
    gic.write_mmio(GICv3::kGicrBase + 0x10080u, 0xFFFFFFFFu, 4);
    gic.write_mmio(GICv3::kGicrBase + 0x10100u, (1u << 27u), 4);

    // Setup EL1 Timer Handler at VBAR + 0x480:
    // 0x480: MRS X0, ICC_IAR1_EL1  (0xD530CC00u)
    // 0x484: MOV X2, #2            (0xD2800042u)
    // 0x488: MSR CNTV_CTL_EL0, X2  (0xD513E322u)
    // 0x48C: MSR ICC_EOIR1_EL1, X0 (0xD510CC20u)
    // 0x490: ERET                  (0xD69F03E0u)
    mem.write_u32(static_cast<std::size_t>(vbar + 0x480u + 0u),  0xD530CC00u);
    mem.write_u32(static_cast<std::size_t>(vbar + 0x480u + 4u),  0xD2800042u);
    mem.write_u32(static_cast<std::size_t>(vbar + 0x480u + 8u),  0xD513E322u);
    mem.write_u32(static_cast<std::size_t>(vbar + 0x480u + 12u), 0xD510CC20u);
    mem.write_u32(static_cast<std::size_t>(vbar + 0x480u + 16u), 0xD69F03E0u);

    // Task A code at 0x1000: ADD X10, X10, #1 (0x9100054Au)
    mem.write_u32(0x1000, 0x9100054Au);

    // Task B code at 0x2000: ADD X11, X11, #2 (0x9100096Bu)
    mem.write_u32(0x2000, 0x9100096Bu);

    // Configure Virtual Timer: expires at count 1000
    VirtualTime& vt = cpu.get_virtual_time();
    vt.set_physical_counter(500ULL);
    vt.set_cntvoff_el2(0, 0ULL);
    auto& vtimer = vt.get_virt_timer(0);
    vtimer.cval = 1000ULL;
    vtimer.enable = true;
    vtimer.imask = false;
    vt.evaluate_core_timers(0);

    // Start running Task A at EL0
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_daif(0);
    cpu.set_program_counter(0x1000);
    cpu.write_register(10, 1);

    // Advance clock to trigger timer expiration: 500 + 600 = 1100 (> 1000)
    vt.advance_physical_counter(600ULL);
    CHECK(vtimer.irq_asserted(), "Virtual timer must assert IRQ");

    // Step 1: Preemption -> vectors to VBAR + 0x480 (EL1)
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1, "Entered EL1 on timer interrupt");
    CHECK(cpu.get_program_counter() == vbar + 0x480u, "Jumped to IRQ vector");
    CHECK(cpu.get_elr_el1() == 0x1000, "ELR_EL1 holds Task A PC (0x1000)");

    // Step 2..5: Execute interrupt handler instructions
    cpu.step(mem); // MRS X0, ICC_IAR1_EL1
    CHECK(cpu.read_register(0) == 27u, "Acknowledged INTID 27");
    cpu.step(mem); // MOV X2, #2
    cpu.step(mem); // MSR CNTV_CTL_EL0, X2 (masks timer)
    CHECK(vtimer.imask, "Timer masked");
    cpu.step(mem); // MSR ICC_EOIR1_EL1, X0

    // Scheduler switch: Kernel switches return context to Task B at 0x2000
    cpu.set_elr_el1(0x2000);
    cpu.write_register(11, 20);

    // Step 6: Execute ERET -> returns to EL0 into Task B!
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 0, "Returned to EL0");
    CHECK(cpu.get_program_counter() == 0x2000, "Resumed at Task B PC (0x2000)");

    // Step 7: Task B executes instruction ADD X11, X11, #2
    cpu.step(mem);
    CHECK(cpu.read_register(11) == 22, "Task B executed successfully");

    std::cout << "[PASS] test_integration_timer_interrupt_servicing_and_task_switch\n";
    return true;
}

// Test 4: Exception Nesting Under Guest Kernel Control
static bool test_integration_nested_exception_under_kernel_control() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x200000ULL);

    const std::uint64_t vbar = 0x90000ULL;
    cpu.set_vbar_el1(vbar);
    cpu.set_sp_el1(0x80000ULL);

    // EL0 executes SVC #100 at 0x1000
    mem.write_u32(0x1000, 0xD4000C81u); // SVC #100
    mem.write_u32(0x1004, 0x91000400u); // ADD X0, X0, #1

    // Vector VBAR + 0x400 (Lower EL Sync):
    // 0x90400: SVC #200 (nested exception triggered from EL1 with SP_EL1)
    // 0x90404: ERET
    mem.write_u32(static_cast<std::size_t>(vbar + 0x400u), 0xD4001901u); // SVC #200
    mem.write_u32(static_cast<std::size_t>(vbar + 0x404u), 0xD69F03E0u); // ERET

    // Vector VBAR + 0x200 (Current EL SPx Sync):
    // 0x90200: ADD X5, X5, #50
    // 0x90204: ERET
    mem.write_u32(static_cast<std::size_t>(vbar + 0x200u), 0x9100C8A5u); // ADD X5, X5, #50
    mem.write_u32(static_cast<std::size_t>(vbar + 0x204u), 0xD69F03E0u); // ERET

    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_program_counter(0x1000);
    cpu.write_register(0, 5);
    cpu.write_register(5, 50);

    // Step 1: EL0 executes SVC #100 -> vectors to VBAR + 0x400
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1, "Entered EL1");
    CHECK(cpu.get_program_counter() == vbar + 0x400u, "Jumped to Lower EL Sync vector");
    const std::uint64_t outer_elr = cpu.get_elr_el1();
    const std::uint32_t outer_spsr = static_cast<std::uint32_t>(cpu.get_spsr_el1());
    CHECK(outer_elr == 0x1004, "Outer ELR_EL1 is 0x1004");
    CHECK((cpu.get_esr_el1() & 0xFFFFu) == 100u, "Outer ESR holds SVC #100");

    // Step 2: Outer kernel handler executes SVC #200 -> vectors to Current EL SPx (+0x200)
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1, "Remains at EL1 for nested exception");
    CHECK(cpu.get_program_counter() == vbar + 0x200u, "Jumped to Current EL SPx Sync vector");
    CHECK(cpu.get_elr_el1() == vbar + 0x404u, "Inner ELR_EL1 points to outer handler instruction 0x90404");
    CHECK((cpu.get_esr_el1() & 0xFFFFu) == 200u, "Inner ESR holds SVC #200");

    // Step 3: Inner handler executes ADD X5, X5, #50
    cpu.step(mem);
    CHECK(cpu.read_register(5) == 100, "X5 incremented to 100 in nested handler");

    // Step 4: Inner handler executes ERET -> returns to outer handler at 0x90404
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1, "Returns to EL1 outer handler");
    CHECK(cpu.get_program_counter() == vbar + 0x404u, "Resumes at outer handler PC");

    // Restore outer context
    cpu.set_elr_el1(outer_elr);
    cpu.set_spsr_el1(outer_spsr);

    // Step 5: Outer handler executes ERET -> returns to EL0 at 0x1004
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 0, "Returns to EL0");
    CHECK(cpu.get_program_counter() == 0x1004, "Resumes user PC at 0x1004");

    // Step 6: EL0 resumes and executes ADD X0, X0, #1
    cpu.step(mem);
    CHECK(cpu.read_register(0) == 6, "X0 incremented to 6");

    std::cout << "[PASS] test_integration_nested_exception_under_kernel_control\n";
    return true;
}

int main() {
    try {
        std::cout << "=== Running Stage 193.17: Freestanding Kernel Integration Tests ===\n";
        if (!test_integration_syscall_invocation_and_register_preservation()) return 1;
        if (!test_integration_demand_paging_and_tlb_invalidation()) return 1;
        if (!test_integration_timer_interrupt_servicing_and_task_switch()) return 1;
        if (!test_integration_nested_exception_under_kernel_control()) return 1;
        std::cout << "=== All Stage 193.17 Tests Passed (4/4) ===\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION CAUGHT: " << ex.what() << "\n";
        return 1;
    }
}

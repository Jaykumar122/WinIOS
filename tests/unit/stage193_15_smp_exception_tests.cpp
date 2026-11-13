// ============================================================================
// Stage 193.15: Exception Nesting and SMP Delivery Tests
// ============================================================================

#include <cstdint>
#include <iostream>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/smp_cluster.h"
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

// Test 1: Verify per-core exception state isolation (VBAR_EL1, ESR_EL1, ELR_EL1, SPSR_EL1)
static bool test_per_core_exception_state_isolation() {
    SMPCluster cluster(2, 0x100000ULL);
    CPU& core0 = cluster.get_core(0);
    CPU& core1 = cluster.get_core(1);
    Memory& mem = cluster.get_memory();

    core0.set_architectural_exceptions(true);
    core1.set_architectural_exceptions(true);

    const std::uint64_t vbar0 = 0x20000ULL;
    const std::uint64_t vbar1 = 0x30000ULL;
    core0.set_vbar_el1(vbar0);
    core1.set_vbar_el1(vbar1);

    // Core 0 code at 0x1000: SVC #10 (0xD4000141u)
    const std::uint64_t core0_code_va = 0x1000ULL;
    mem.write_u32(static_cast<std::size_t>(core0_code_va), 0xD4000141u);

    // Core 1 code at 0x2000: SVC #20 (0xD4000281u)
    const std::uint64_t core1_code_va = 0x2000ULL;
    mem.write_u32(static_cast<std::size_t>(core1_code_va), 0xD4000281u);

    // Configure Core 0 at EL0
    core0.set_current_el(0);
    core0.set_sp_sel(false);
    core0.set_program_counter(core0_code_va);

    // Configure Core 1 at EL0
    core1.set_current_el(0);
    core1.set_sp_sel(false);
    core1.set_program_counter(core1_code_va);

    // Step Core 0 -> takes SVC #10
    core0.step(mem);

    CHECK(core0.get_current_el() == 1, "Core 0 must enter EL1");
    CHECK(core0.get_program_counter() == vbar0 + 0x400ULL, "Core 0 must vector to vbar0 + 0x400");
    CHECK(core0.get_elr_el1() == core0_code_va + 4u, "Core 0 ELR_EL1 must point to instruction after SVC");
    CHECK((core0.get_esr_el1() >> 26u) == 0x15u, "Core 0 ESR_EL1 must have EC = 0x15 (SVC)");
    CHECK((core0.get_esr_el1() & 0xFFFFu) == 10u, "Core 0 ESR_EL1 must hold immediate #10");

    // Verify Core 1 was completely unaffected by Core 0's exception
    CHECK(core1.get_current_el() == 0, "Core 1 must remain at EL0");
    CHECK(core1.get_program_counter() == core1_code_va, "Core 1 PC must remain at 0x2000");
    CHECK(core1.get_esr_el1() == 0, "Core 1 ESR_EL1 must remain 0");
    CHECK(core1.get_elr_el1() == 0, "Core 1 ELR_EL1 must remain 0");

    // Step Core 1 -> takes SVC #20
    core1.step(mem);

    CHECK(core1.get_current_el() == 1, "Core 1 must enter EL1");
    CHECK(core1.get_program_counter() == vbar1 + 0x400ULL, "Core 1 must vector to vbar1 + 0x400");
    CHECK(core1.get_elr_el1() == core1_code_va + 4u, "Core 1 ELR_EL1 must point to instruction after SVC");
    CHECK((core1.get_esr_el1() >> 26u) == 0x15u, "Core 1 ESR_EL1 must have EC = 0x15 (SVC)");
    CHECK((core1.get_esr_el1() & 0xFFFFu) == 20u, "Core 1 ESR_EL1 must hold immediate #20");

    // Verify Core 0's exception registers were not overwritten by Core 1
    CHECK((core0.get_esr_el1() & 0xFFFFu) == 10u, "Core 0 ESR_EL1 must still hold immediate #10");
    CHECK(core0.get_elr_el1() == core0_code_va + 4u, "Core 0 ELR_EL1 still holds 0x1004");

    std::cout << "[PASS] test_per_core_exception_state_isolation\n";
    return true;
}

// Test 2: Verify per-core exception stack isolation
static bool test_per_core_smp_exception_stack_isolation() {
    SMPCluster cluster(2, 0x100000ULL);
    CPU& core0 = cluster.get_core(0);
    CPU& core1 = cluster.get_core(1);
    Memory& mem = cluster.get_memory();

    core0.set_architectural_exceptions(true);
    core1.set_architectural_exceptions(true);

    const std::uint64_t sp_el1_core0 = 0x80000ULL;
    const std::uint64_t sp_el1_core1 = 0x90000ULL;

    core0.set_sp_el1(sp_el1_core0);
    core1.set_sp_el1(sp_el1_core1);

    core0.set_current_el(1);
    core0.set_sp_sel(true);
    core1.set_current_el(1);
    core1.set_sp_sel(true);

    CHECK(core0.get_stack_pointer() == sp_el1_core0, "Core 0 stack pointer must be sp_el1_core0");
    CHECK(core1.get_stack_pointer() == sp_el1_core1, "Core 1 stack pointer must be sp_el1_core1");

    // Core 0 pushes canary 0x1111222233334444ULL
    const std::uint64_t canary0 = 0x1111222233334444ULL;
    core0.set_stack_pointer(sp_el1_core0 - 16);
    mem.write_u64(static_cast<std::size_t>(core0.get_stack_pointer()), canary0);

    // Core 1 pushes canary 0x5555666677778888ULL
    const std::uint64_t canary1 = 0x5555666677778888ULL;
    core1.set_stack_pointer(sp_el1_core1 - 16);
    mem.write_u64(static_cast<std::size_t>(core1.get_stack_pointer()), canary1);

    // Check memory isolation between stacks
    CHECK(mem.read_u64(static_cast<std::size_t>(sp_el1_core0 - 16)) == canary0, "Core 0 stack has canary0");
    CHECK(mem.read_u64(static_cast<std::size_t>(sp_el1_core1 - 16)) == canary1, "Core 1 stack has canary1");
    CHECK(core0.get_stack_pointer() != core1.get_stack_pointer(), "Stack pointers must remain distinct");

    std::cout << "[PASS] test_per_core_smp_exception_stack_isolation\n";
    return true;
}

// Test 3: Bounded nested exception handling (EL0 -> EL1 outer -> EL1 inner -> EL1 outer -> EL0)
static bool test_nested_exception_handling_el1_to_el1() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x200000ULL);

    const std::uint64_t vbar = 0x50000ULL;
    cpu.set_vbar_el1(vbar);
    cpu.set_sp_el1(0x80000ULL);

    // EL0 user code at 0x1000:
    // 0x1000: SVC #1
    // 0x1004: ADD X1, X1, #1
    mem.write_u32(0x1000, 0xD4000021u); // SVC #1
    mem.write_u32(0x1004, 0x91000421u); // ADD X1, X1, #1

    // Vector VBAR + 0x400 (Lower EL AArch64 Sync):
    // 0x50400: SVC #2 (triggers nested exception from EL1 with SP_EL1)
    // 0x50404: ERET
    mem.write_u32(static_cast<std::size_t>(vbar + 0x400u), 0xD4000041u); // SVC #2
    mem.write_u32(static_cast<std::size_t>(vbar + 0x404u), 0xD69F03E0u); // ERET

    // Vector VBAR + 0x200 (Current EL with SPx Sync):
    // 0x50200: ADD X3, X3, #1
    // 0x50204: ERET
    mem.write_u32(static_cast<std::size_t>(vbar + 0x200u), 0x91000463u); // ADD X3, X3, #1
    mem.write_u32(static_cast<std::size_t>(vbar + 0x204u), 0xD69F03E0u); // ERET

    // Setup initial EL0 state
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_program_counter(0x1000);
    cpu.write_register(1, 10);
    cpu.write_register(3, 100);

    // Step 1: Execute SVC #1 at EL0 -> vectors to VBAR + 0x400 (Lower EL Sync)
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1, "Must enter EL1");
    CHECK(cpu.get_program_counter() == vbar + 0x400u, "Must jump to Lower EL Sync vector");
    const std::uint64_t outer_elr = cpu.get_elr_el1();
    const std::uint64_t outer_spsr = cpu.get_spsr_el1();
    CHECK(outer_elr == 0x1004, "Outer ELR_EL1 must point to user instruction 0x1004");

    // In a real kernel, software saves outer_elr and outer_spsr to stack.
    // Step 2: EL1 handler executes SVC #2 -> vectors to VBAR + 0x200 (Current EL SPx Sync)
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1, "Must remain at EL1 for nested exception");
    CHECK(cpu.get_program_counter() == vbar + 0x200u, "Must jump to Current EL SPx Sync vector (+0x200)");
    CHECK(cpu.get_elr_el1() == vbar + 0x404u, "Inner ELR_EL1 points to outer handler instruction 0x50404");
    CHECK((cpu.get_esr_el1() & 0xFFFFu) == 2u, "Inner ESR_EL1 holds SVC #2");

    // Step 3: Inner handler executes ADD X3, X3, #1
    cpu.step(mem);
    CHECK(cpu.read_register(3) == 101, "X3 incremented to 101 in nested handler");

    // Step 4: Inner handler executes ERET -> returns to outer handler at 0x50404
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1, "Returns to EL1 outer handler");
    CHECK(cpu.get_program_counter() == vbar + 0x404u, "Resumes at outer handler PC");

    // Restore outer context (simulating kernel epilogue)
    cpu.set_elr_el1(outer_elr);
    cpu.set_spsr_el1(static_cast<std::uint32_t>(outer_spsr));

    // Step 5: Outer handler executes ERET -> returns to EL0 at 0x1004
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 0, "Returns to EL0");
    CHECK(cpu.get_program_counter() == 0x1004, "Resumes user PC at 0x1004");

    // Step 6: EL0 executes ADD X1, X1, #1
    cpu.step(mem);
    CHECK(cpu.read_register(1) == 11, "X1 incremented to 11");

    std::cout << "[PASS] test_nested_exception_handling_el1_to_el1\n";
    return true;
}

// Test 4: Inter-core interrupt delivery (SGI via GICv3)
static bool test_inter_core_sgi_interrupt_delivery() {
    SMPCluster cluster(2, 0x100000ULL);
    CPU& core0 = cluster.get_core(0);
    CPU& core1 = cluster.get_core(1);
    Memory& mem = cluster.get_memory();
    GICv3& gic = cluster.get_gic();

    core0.set_architectural_exceptions(true);
    core1.set_architectural_exceptions(true);

    const std::uint64_t vbar1 = 0x60000ULL;
    core1.set_vbar_el1(vbar1);

    // Initialize GIC Group 1
    gic.reset();
    gic.set_core_count(2);
    gic.write_mmio(GICv3::kGicdBase + 0x0000u, 0x3u, 4); // GICD_CTLR: EnableGrp0 and EnableGrp1

    // Enable CPU interface on both cores
    gic.write_icc_igrpen1(1, 0);
    gic.write_icc_pmr(0xF0u, 0);
    gic.write_icc_igrpen1(1, 1);
    gic.write_icc_pmr(0xF0u, 1);

    // Configure SGI 5 as Group 1 on Core 1 Redistributor
    // GICR Core 1 base = kGicrBase + 0x20000
    // SGI/PPI frame = GICR base + 0x10000 -> 0x20000 + 0x10000 = 0x30000
    const std::uint64_t gicr1_sgi_base = GICv3::kGicrBase + 0x20000ULL + 0x10000ULL;
    gic.write_mmio(gicr1_sgi_base + 0x0080u, 0xFFFFFFFFu, 4); // IGROUPR0: all SGIs/PPIs Group 1
    gic.write_mmio(gicr1_sgi_base + 0x0100u, (1u << 5u), 4);   // ISENABLER0: enable SGI 5

    // Core 1 user program at 0x2000: ADD X1, X1, #1 (0x91000421u)
    const std::uint64_t user_pc1 = 0x2000ULL;
    mem.write_u32(static_cast<std::size_t>(user_pc1), 0x91000421u);

    // Core 1 IRQ handler at vbar1 + 0x480:
    // 0x480: MRS X0, ICC_IAR1_EL1 (0xD530CC00u)
    // 0x484: MSR ICC_EOIR1_EL1, X0 (0xD510CC20u)
    // 0x488: ERET (0xD69F03E0u)
    const std::uint64_t irq_handler1 = vbar1 + 0x480ULL;
    mem.write_u32(static_cast<std::size_t>(irq_handler1 + 0u), 0xD530CC00u);
    mem.write_u32(static_cast<std::size_t>(irq_handler1 + 4u), 0xD510CC20u);
    mem.write_u32(static_cast<std::size_t>(irq_handler1 + 8u), 0xD69F03E0u);

    // Set Core 1 at EL0, unmasked DAIF
    core1.set_current_el(0);
    core1.set_sp_sel(false);
    core1.set_daif(0);
    core1.set_program_counter(user_pc1);
    core1.write_register(1, 50);

    // Core 0 sends SGI 5 targeted to Core 1:
    // sgi1r encoding:
    // TargetList = (1 << 1) = 2 (bit 1 for Core 1)
    // INTID = 5 (bits [27:24] = 5) -> (5ULL << 24)
    // IRM = 0 (bits [40] = 0)
    const std::uint64_t sgi1r = (5ULL << 24u) | (1ULL << 1u);
    gic.send_sgi(0, sgi1r);

    // Verify Core 0 has no pending interrupt
    const auto signal0 = gic.evaluate(core0.get_current_el(), 0, 0);
    CHECK(!signal0.has_interrupt, "Core 0 must NOT have a pending interrupt");

    // Verify Core 1 has pending IRQ
    const auto signal1 = gic.evaluate(core1.get_current_el(), 0, 1);
    CHECK(signal1.has_interrupt, "Core 1 must have a pending interrupt");

    // Step Core 1 -> interrupt preempts and vectors to vbar1 + 0x480
    core1.step(mem);
    CHECK(core1.get_current_el() == 1, "Core 1 entered EL1 on SGI");
    CHECK(core1.get_program_counter() == irq_handler1, "Core 1 jumped to Lower EL IRQ vector");
    CHECK(core1.get_elr_el1() == user_pc1, "Core 1 ELR_EL1 holds user PC 0x2000");

    // Step Core 1: MRS X0, ICC_IAR1_EL1
    core1.step(mem);
    CHECK(core1.read_register(0) == 5u, "Core 1 acknowledged SGI INTID 5");

    // Step Core 1: MSR ICC_EOIR1_EL1, X0
    core1.step(mem);

    // Step Core 1: ERET -> returns to EL0
    core1.step(mem);
    CHECK(core1.get_current_el() == 0, "Core 1 returned to EL0");
    CHECK(core1.get_program_counter() == user_pc1, "Core 1 restored user PC");

    // Step Core 1: executes user instruction ADD X1, X1, #1
    core1.step(mem);
    CHECK(core1.read_register(1) == 51, "Core 1 completed user instruction after interrupt");

    std::cout << "[PASS] test_inter_core_sgi_interrupt_delivery\n";
    return true;
}

int main() {
    try {
        std::cout << "=== Running Stage 193.15: Exception Nesting and SMP Delivery Tests ===\n";
        if (!test_per_core_exception_state_isolation()) return 1;
        if (!test_per_core_smp_exception_stack_isolation()) return 1;
        if (!test_nested_exception_handling_el1_to_el1()) return 1;
        if (!test_inter_core_sgi_interrupt_delivery()) return 1;
        std::cout << "=== All Stage 193.15 Tests Passed (4/4) ===\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION CAUGHT: " << ex.what() << "\n";
        return 1;
    }
}

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "cpu/synchronous_exception.h"
#include "memory/memory.h"

#define STAGE108_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] FAILED: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

namespace {

constexpr std::uint64_t kVbarBase = 0xFFFFFE0008000000ULL; // 2 KB aligned
constexpr std::uint64_t kUserPc   = 0x0000000010001000ULL;
constexpr std::uint64_t kKernelPc = 0xFFFFFE0007004000ULL;

void setup_cpu(CPU& cpu, unsigned el, bool sp_sel, std::uint64_t pc, std::uint64_t vbar) {
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(el);
    cpu.set_sp_sel(sp_sel);
    cpu.set_program_counter(pc);
    cpu.set_vbar_el1(vbar);
    cpu.set_daif(0x0u);
}

// -----------------------------------------------------------------------------
// Test 1: VBAR_EL1 2 KB Alignment Enforcement
// -----------------------------------------------------------------------------
bool test_vbar_alignment() {
    std::cout << "[RUN] Test 1: VBAR_EL1 2 KB alignment enforcement ... ";
    CPU cpu;
    cpu.reset();

    // Valid 2048-byte aligned address
    cpu.set_vbar_el1(0x80000000ULL);
    STAGE108_ASSERT(cpu.get_vbar_el1() == 0x80000000ULL, "2048-byte aligned VBAR accepted");

    // Address with bits [10:0] set: low 11 bits masked to 0
    cpu.set_vbar_el1(0x800007FFULL);
    STAGE108_ASSERT((cpu.get_vbar_el1() & 0x7FFULL) == 0ULL, "VBAR bits [10:0] must be masked to zero");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Lower EL (EL0) Synchronous Exception via SVC (VBAR + 0x400)
// -----------------------------------------------------------------------------
bool test_lower_el_svc_sync_exception() {
    std::cout << "[RUN] Test 2: Lower EL (EL0) SVC sync exception (VBAR + 0x400) ... ";
    Memory mem(0x200000);
    CPU cpu;
    setup_cpu(cpu, 0, false, 0x1000ULL, kVbarBase);

    // SVC #0x12: 0xD4000241
    mem.write_u32(0x1000, 0xD4000241u);
    cpu.step(mem);

    STAGE108_ASSERT(cpu.get_current_el() == 1, "Target EL must be EL1");
    STAGE108_ASSERT(cpu.get_program_counter() == kVbarBase + 0x400ULL, "PC must be VBAR + 0x400");
    STAGE108_ASSERT(cpu.get_elr_el1() == 0x1004ULL, "ELR_EL1 must be PC + 4");
    STAGE108_ASSERT((cpu.get_esr_el1() >> 26u) == 0x15u, "ESR_EL1 EC must be 0x15 (SVC)");
    STAGE108_ASSERT((cpu.get_esr_el1() & 0xFFFFu) == 0x12u, "ESR_EL1 imm16 must be 0x12");
    STAGE108_ASSERT(cpu.get_daif() == 0xFu, "DAIF must be masked (0xF)");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Lower EL (EL0) IRQ Interrupt Dispatch (VBAR + 0x480)
// -----------------------------------------------------------------------------
bool test_lower_el_irq_dispatch() {
    std::cout << "[RUN] Test 3: Lower EL (EL0) IRQ dispatch (VBAR + 0x480) ... ";
    CPU cpu;
    setup_cpu(cpu, 0, false, 0x1000ULL, kVbarBase);

    // Deliver asynchronous IRQ exception
    cpu.raise_asynchronous_exception(false, 30, false, 0x80);

    STAGE108_ASSERT(cpu.get_current_el() == 1, "Target EL must be EL1");
    STAGE108_ASSERT(cpu.get_program_counter() == kVbarBase + 0x480ULL, "PC must be VBAR + 0x480 (Lower EL AArch64 IRQ)");
    STAGE108_ASSERT(cpu.get_elr_el1() == 0x1000ULL, "ELR_EL1 must be preferred return PC");
    STAGE108_ASSERT(cpu.get_daif() == 0xFu, "DAIF must be masked");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Lower EL (EL0) FIQ Interrupt Dispatch (VBAR + 0x500)
// -----------------------------------------------------------------------------
bool test_lower_el_fiq_dispatch() {
    std::cout << "[RUN] Test 4: Lower EL (EL0) FIQ dispatch (VBAR + 0x500) ... ";
    CPU cpu;
    setup_cpu(cpu, 0, false, 0x1000ULL, kVbarBase);

    cpu.raise_asynchronous_exception(true, 27, false, 0x80);

    STAGE108_ASSERT(cpu.get_current_el() == 1, "Target EL must be EL1");
    STAGE108_ASSERT(cpu.get_program_counter() == kVbarBase + 0x500ULL, "PC must be VBAR + 0x500 (Lower EL AArch64 FIQ)");
    STAGE108_ASSERT(cpu.get_elr_el1() == 0x1000ULL, "ELR_EL1 must be 0x1000");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Current EL with SP_ELx Synchronous Exception (VBAR + 0x200)
// -----------------------------------------------------------------------------
bool test_current_el_spx_sync_exception() {
    std::cout << "[RUN] Test 5: Current EL with SP_ELx sync exception (VBAR + 0x200) ... ";
    Memory mem(0x200000);
    CPU cpu;
    setup_cpu(cpu, 1, true, 0x2000ULL, kVbarBase); // EL1, SPSel=1 (SP_EL1)

    // Unallocated / illegal opcode to trigger Synchronous Exception at EL1
    mem.write_u32(0x2000, 0x00000000u);
    cpu.step(mem);

    STAGE108_ASSERT(cpu.get_current_el() == 1, "Stay at EL1");
    STAGE108_ASSERT(cpu.get_program_counter() == kVbarBase + 0x200ULL, "PC must be VBAR + 0x200 (Current EL SPx Sync)");
    STAGE108_ASSERT(cpu.get_elr_el1() == 0x2000ULL, "ELR_EL1 must hold faulting instruction PC");
    STAGE108_ASSERT((cpu.get_esr_el1() >> 26u) == 0x00u, "ESR_EL1 EC must be 0x00 (Unknown/Unallocated)");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Current EL with SP_ELx IRQ Dispatch (VBAR + 0x280)
// -----------------------------------------------------------------------------
bool test_current_el_spx_irq_dispatch() {
    std::cout << "[RUN] Test 6: Current EL with SP_ELx IRQ dispatch (VBAR + 0x280) ... ";
    CPU cpu;
    setup_cpu(cpu, 1, true, 0x2000ULL, kVbarBase);

    cpu.raise_asynchronous_exception(false, 30, false, 0x80);

    STAGE108_ASSERT(cpu.get_current_el() == 1, "Stay at EL1");
    STAGE108_ASSERT(cpu.get_program_counter() == kVbarBase + 0x280ULL, "PC must be VBAR + 0x280 (Current EL SPx IRQ)");
    STAGE108_ASSERT(cpu.get_elr_el1() == 0x2000ULL, "ELR_EL1 must be 0x2000");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Current EL with SP_ELx FIQ Dispatch (VBAR + 0x300)
// -----------------------------------------------------------------------------
bool test_current_el_spx_fiq_dispatch() {
    std::cout << "[RUN] Test 7: Current EL with SP_ELx FIQ dispatch (VBAR + 0x300) ... ";
    CPU cpu;
    setup_cpu(cpu, 1, true, 0x2000ULL, kVbarBase);

    cpu.raise_asynchronous_exception(true, 27, false, 0x80);

    STAGE108_ASSERT(cpu.get_current_el() == 1, "Stay at EL1");
    STAGE108_ASSERT(cpu.get_program_counter() == kVbarBase + 0x300ULL, "PC must be VBAR + 0x300 (Current EL SPx FIQ)");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Current EL with SP_EL0 Synchronous Exception (VBAR + 0x000)
// -----------------------------------------------------------------------------
bool test_current_el_sp0_sync_exception() {
    std::cout << "[RUN] Test 8: Current EL with SP_EL0 sync exception (VBAR + 0x000) ... ";
    Memory mem(0x200000);
    CPU cpu;
    setup_cpu(cpu, 1, false, 0x2000ULL, kVbarBase); // EL1, SPSel=0 (SP_EL0)

    mem.write_u32(0x2000, 0x00000000u);
    cpu.step(mem);

    STAGE108_ASSERT(cpu.get_current_el() == 1, "Stay at EL1");
    STAGE108_ASSERT(cpu.get_program_counter() == kVbarBase + 0x000ULL, "PC must be VBAR + 0x000 (Current EL SP0 Sync)");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 9: Current EL with SP_EL0 IRQ Dispatch (VBAR + 0x080)
// -----------------------------------------------------------------------------
bool test_current_el_sp0_irq_dispatch() {
    std::cout << "[RUN] Test 9: Current EL with SP_EL0 IRQ dispatch (VBAR + 0x080) ... ";
    CPU cpu;
    setup_cpu(cpu, 1, false, 0x2000ULL, kVbarBase);

    cpu.raise_asynchronous_exception(false, 30, false, 0x80);

    STAGE108_ASSERT(cpu.get_current_el() == 1, "Stay at EL1");
    STAGE108_ASSERT(cpu.get_program_counter() == kVbarBase + 0x080ULL, "PC must be VBAR + 0x080 (Current EL SP0 IRQ)");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 10: Current EL with SP_EL0 FIQ Dispatch (VBAR + 0x100)
// -----------------------------------------------------------------------------
bool test_current_el_sp0_fiq_dispatch() {
    std::cout << "[RUN] Test 10: Current EL with SP_EL0 FIQ dispatch (VBAR + 0x100) ... ";
    CPU cpu;
    setup_cpu(cpu, 1, false, 0x2000ULL, kVbarBase);

    cpu.raise_asynchronous_exception(true, 27, false, 0x80);

    STAGE108_ASSERT(cpu.get_current_el() == 1, "Stay at EL1");
    STAGE108_ASSERT(cpu.get_program_counter() == kVbarBase + 0x100ULL, "PC must be VBAR + 0x100 (Current EL SP0 FIQ)");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 11: DAIF Masking Behavior
// -----------------------------------------------------------------------------
bool test_daif_masking_behavior() {
    std::cout << "[RUN] Test 11: DAIF masking behavior ... ";
    CPU cpu;
    setup_cpu(cpu, 1, true, 0x2000ULL, kVbarBase);

    // Initial state: DAIF masked (0xF)
    cpu.set_daif(0xFu);
    STAGE108_ASSERT(cpu.get_daif() == 0xFu, "DAIF is 0xF");

    // Unmask all interrupts: DAIF = 0
    cpu.set_daif(0x0u);
    STAGE108_ASSERT(cpu.get_daif() == 0x0u, "DAIF unmasked (0)");

    // Mask IRQ only (I bit is bit 1: value 0x2)
    cpu.set_daif(0x2u);
    STAGE108_ASSERT((cpu.get_daif() & 0x2u) != 0, "I bit masked");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 12: ERET Roundtrip State Restoration
// -----------------------------------------------------------------------------
bool test_eret_state_restoration() {
    std::cout << "[RUN] Test 12: ERET state restoration ... ";
    Memory mem(0x200000);
    CPU cpu;
    setup_cpu(cpu, 1, true, 0x3000ULL, kVbarBase);

    const std::uint64_t return_pc = 0x0000000040001000ULL;
    // SPSR_EL1: Mode=EL0t (0b0000), NZCV=0b1000 (N=1)
    const std::uint32_t return_spsr = (8u << 28u);

    cpu.set_elr_el1(return_pc);
    cpu.set_spsr_el1(return_spsr);

    // ERET instruction: 0xD69F03E0
    mem.write_u32(0x3000, 0xD69F03E0u);
    cpu.step(mem);

    STAGE108_ASSERT(cpu.get_program_counter() == return_pc, "PC restored from ELR_EL1");
    STAGE108_ASSERT(cpu.get_current_el() == 0, "Current EL restored to EL0");
    STAGE108_ASSERT((cpu.get_nzcv() & 0x8u) != 0, "NZCV N bit restored");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 13: Illegal ERET Syndrome & Trap
// -----------------------------------------------------------------------------
bool test_illegal_eret_protection() {
    std::cout << "[RUN] Test 13: Illegal ERET protection ... ";
    Memory mem(0x200000);
    CPU cpu;
    setup_cpu(cpu, 1, true, 0x3000ULL, kVbarBase);

    // SPSR_EL1 with illegal mode (e.g. EL3 mode 0b1100 when EL3 not implemented)
    cpu.set_elr_el1(0x4000ULL);
    cpu.set_spsr_el1(0x0000000Cu); // EL3t illegal

    // ERET instruction: 0xD69F03E0
    mem.write_u32(0x3000, 0xD69F03E0u);
    cpu.step(mem);

    // Illegal ERET sets PSTATE.IL
    STAGE108_ASSERT(cpu.get_il(), "Illegal ERET must set PSTATE.IL");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 14: FAR_EL1 Fault Address Recording on Data Abort
// -----------------------------------------------------------------------------
bool test_far_el1_fault_address_recording() {
    std::cout << "[RUN] Test 14: FAR_EL1 fault address recording ... ";
    Memory mem(0x200000);
    CPU cpu;
    setup_cpu(cpu, 1, true, 0x2000ULL, kVbarBase);

    const std::uint64_t fault_addr = 0x50004000ULL;
    cpu.set_far_el1(fault_addr);
    STAGE108_ASSERT(cpu.get_far_el1() == fault_addr, "FAR_EL1 holds exact fault address");

    std::cout << "PASS\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 108: Exception Vector Dispatch & Sysreg Test Suite       \n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*test_fn)()) {
        if (test_fn()) {
            passed++;
        } else {
            failed++;
        }
    };

    run_test(test_vbar_alignment);
    run_test(test_lower_el_svc_sync_exception);
    run_test(test_lower_el_irq_dispatch);
    run_test(test_lower_el_fiq_dispatch);
    run_test(test_current_el_spx_sync_exception);
    run_test(test_current_el_spx_irq_dispatch);
    run_test(test_current_el_spx_fiq_dispatch);
    run_test(test_current_el_sp0_sync_exception);
    run_test(test_current_el_sp0_irq_dispatch);
    run_test(test_current_el_sp0_fiq_dispatch);
    run_test(test_daif_masking_behavior);
    run_test(test_eret_state_restoration);
    run_test(test_illegal_eret_protection);
    run_test(test_far_el1_fault_address_recording);

    std::cout << "=================================================================\n";
    std::cout << " Summary: " << passed << " / " << (passed + failed) << " tests passed.\n";
    std::cout << "=================================================================\n";

    return (failed == 0) ? 0 : 1;
}

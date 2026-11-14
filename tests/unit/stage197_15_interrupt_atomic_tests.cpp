// tests/unit/stage197_15_interrupt_atomic_tests.cpp
// Campaign 197: Acceptance Test L
// Verifies Preemption & Interrupt Boundaries on Memory Operations:
// Validates that asynchronous interrupts and preemption preserve atomic invariants,
// properly invalidate exclusive reservations, and prevent memory corruption.

#include <iostream>
#include <vector>
#include <cstdint>
#include <memory>
#include "cpu/cpu.h"
#include "cpu/smp_cluster.h"
#include "memory/memory.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;

static bool test_interrupt_between_ldxr_and_stxr() {
    std::cout << "  Testing interrupt delivery between LDXR and STXR..." << std::endl;
    CPU cpu;
    Memory mem(0x100000ULL);

    cpu.reset();
    cpu.set_architectural_exceptions(true);
    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_daif(0x0); // unmask IRQ

    const std::uint64_t target_addr = 0x2000ULL;
    const std::uint64_t initial_data = 0x1111222233334444ULL;
    const std::uint64_t updated_data = 0x9999888877776666ULL;
    mem.write_u64(static_cast<std::size_t>(target_addr), initial_data);

    // Setup code at 0x1000:
    // 0x1000: LDXR X1, [X0]    -> 0xC85F7C01
    // 0x1004: STXR W2, X3, [X0] -> 0xC8027C03
    mem.write_u32(0x1000, 0xC85F7C01u);
    mem.write_u32(0x1004, 0xC8027C03u);

    // Setup IRQ handler at VBAR_EL1 + 0x280 (Current EL with SPx IRQ vector):
    // 0x80280: NOP              -> 0xD503201Fu
    // 0x80284: ERET             -> 0xD69F03E0u
    mem.write_u32(static_cast<std::size_t>(vbar + 0x280), 0xD503201Fu);
    mem.write_u32(static_cast<std::size_t>(vbar + 0x284), 0xD69F03E0u);

    cpu.write_register(0, target_addr);
    cpu.write_register(3, updated_data);
    cpu.set_program_counter(0x1000);

    // 1. Execute LDXR
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(1) == initial_data, "LDXR loaded expected initial data");
    TEST_ASSERT(cpu.exclusive_monitor_valid(), "Exclusive monitor must be valid after LDXR");
    TEST_ASSERT(cpu.get_program_counter() == 0x1004, "PC advanced to STXR");

    // 2. Fire asynchronous IRQ before STXR executes
    cpu.raise_asynchronous_exception(false, 30, false, 0x80);

    // Verify exception delivery state
    TEST_ASSERT(!cpu.exclusive_monitor_valid(), "Interrupt delivery MUST clear exclusive monitor");
    TEST_ASSERT(cpu.get_program_counter() == vbar + 0x280, "PC vectored to IRQ handler");
    TEST_ASSERT(cpu.get_elr_el1() == 0x1004, "ELR_EL1 saved interrupted PC (STXR)");

    // 3. Execute IRQ handler (NOP, then ERET)
    cpu.step(mem); // NOP
    TEST_ASSERT(cpu.get_program_counter() == vbar + 0x284, "PC at ERET");
    cpu.step(mem); // ERET
    TEST_ASSERT(cpu.get_program_counter() == 0x1004, "ERET restored PC to interrupted STXR");
    TEST_ASSERT(!cpu.exclusive_monitor_valid(), "ERET also guarantees cleared exclusive monitor");

    // 4. Execute interrupted STXR
    cpu.step(mem); // STXR
    TEST_ASSERT(cpu.read_register(2) == 1, "STXR must fail with status 1 due to cleared monitor");
    TEST_ASSERT(mem.read_u64(static_cast<std::size_t>(target_addr)) == initial_data,
                "Memory must NOT be modified by failed STXR");

    // 5. Retry full atomic sequence (LDXR then STXR without interrupt)
    cpu.set_program_counter(0x1000);
    cpu.step(mem); // LDXR
    TEST_ASSERT(cpu.exclusive_monitor_valid(), "Exclusive monitor valid again after retry LDXR");
    cpu.step(mem); // STXR
    TEST_ASSERT(cpu.read_register(2) == 0, "STXR must succeed with status 0 on uninterupted retry");
    TEST_ASSERT(mem.read_u64(static_cast<std::size_t>(target_addr)) == updated_data,
                "Memory successfully updated on retry");

    return true;
}

static bool test_interrupt_preserves_lse_atomic_correctness() {
    std::cout << "  Testing interrupt before LSE atomic instruction..." << std::endl;
    CPU cpu;
    Memory mem(0x100000ULL);

    cpu.reset();
    cpu.set_architectural_exceptions(true);
    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_daif(0x0);

    const std::uint64_t target_addr = 0x3000ULL;
    mem.write_u64(static_cast<std::size_t>(target_addr), 100ULL);

    // Setup code at 0x1000:
    // 0x1000: LDADD X1, X2, [X0] -> 0xB8E10002u (LDADD 64-bit: opc=11, V=0, size=11)
    // 0xB8610002u: size=11, 000111, 0, 0, 00000, 0000, 00, Rn=0, Rt=2, Rs=1
    // LDADD X1, X2, [X0] = 0xF8610002u
    mem.write_u32(0x1000, 0xF8610002u);

    // IRQ handler: NOP, ERET
    mem.write_u32(static_cast<std::size_t>(vbar + 0x280), 0xD503201Fu);
    mem.write_u32(static_cast<std::size_t>(vbar + 0x284), 0xD69F03E0u);

    cpu.write_register(0, target_addr);
    cpu.write_register(1, 25ULL); // value to add
    cpu.set_program_counter(0x1000);

    // Raise IRQ before step
    cpu.raise_asynchronous_exception(false, 30, false, 0x80);
    TEST_ASSERT(cpu.get_program_counter() == vbar + 0x280, "Vectored to IRQ handler before atomic execution");
    TEST_ASSERT(mem.read_u64(static_cast<std::size_t>(target_addr)) == 100ULL, "Memory unmodified before execution");

    // Execute IRQ handler
    cpu.step(mem); // NOP
    cpu.step(mem); // ERET
    TEST_ASSERT(cpu.get_program_counter() == 0x1000, "Returned to LDADD instruction");

    // Execute LDADD
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(2) == 100ULL, "LDADD returned previous value (100)");
    TEST_ASSERT(mem.read_u64(static_cast<std::size_t>(target_addr)) == 125ULL, "Memory atomically incremented to 125");

    return true;
}

int main() {
    std::cout << "[STAGE 197.15] Running Interrupt and Atomic State Integrity Tests (Acceptance Test L)..." << std::endl;

    if (!test_interrupt_between_ldxr_and_stxr()) {
        std::cerr << "test_interrupt_between_ldxr_and_stxr FAILED" << std::endl;
        return 1;
    }

    if (!test_interrupt_preserves_lse_atomic_correctness()) {
        std::cerr << "test_interrupt_preserves_lse_atomic_correctness FAILED" << std::endl;
        return 1;
    }

    std::cout << "[STAGE 197.15] All Interrupt and Atomic Integrity Tests PASSED." << std::endl;
    return 0;
}

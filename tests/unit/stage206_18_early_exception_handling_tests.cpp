#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/kernel_exception_vectors.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::cpu;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_synchronous_exception_and_eret() {
    std::cout << "[Test 1] Synchronous Exception Handling, Syndrome Decoding & ERET Return...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);

    Memory memory(0x100000); // 1 MB

    constexpr std::uint64_t kVbar = 0x4000ULL; // 2KB-aligned vector table
    constexpr std::uint64_t kHandlerOffset = 0x200ULL; // Current EL with SP_ELx Synchronous
    constexpr std::uint64_t kHandlerAddr = kVbar + kHandlerOffset;

    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(0x1000ULL);
    cpu.set_stack_pointer(0x80000ULL);
    cpu.set_vbar_el1(kVbar);

    // Write guest program at 0x1000:
    // Insn 0 (0x1000): MOVZ X0, #42 (0xD2800540)
    // Insn 1 (0x1004): Trigger Data Abort by accessing out-of-RAM unmapped PA 0x200000000ULL
    //                  MOVZ X1, #2, LSL #32 (0xD2800001 | (2 << 21) -> 0xD2C00041)
    //                  LDR X2, [X1] (0xF9400022) -> Traps to kVbar + 0x200!
    // Insn 2 (0x100C): MOVZ X3, #0xBEEF (0xD297DDE3) -> Resumed after ERET
    // Insn 3 (0x1010): WFI (0xD503207F)
    memory.write_u32(0x1000, 0xD2800540U);
    memory.write_u32(0x1004, 0xD2C00041U);
    memory.write_u32(0x1008, 0xF9400022U);
    memory.write_u32(0x100C, 0xD297DDE3U);
    memory.write_u32(0x1010, 0xD503207FU);

    // Write exception handler at kVbar + 0x200:
    // Insn 0: MRS X28, ESR_EL1 (0xD538521C)
    // Insn 1: MRS X29, FAR_EL1 (0xD538601D)
    // Insn 2: MRS X30, ELR_EL1 (0xD538403E)
    // Insn 3: ADD X30, X30, #4 (0x910013DE) -> Advance past faulting LDR
    // Insn 4: MSR ELR_EL1, X30 (0xD518403E)
    // Insn 5: ERET (0xD69F03E0)
    memory.write_u32(kHandlerAddr + 0,  0xD538521CU);
    memory.write_u32(kHandlerAddr + 4,  0xD538601DU);
    memory.write_u32(kHandlerAddr + 8,  0xD538403EU);
    memory.write_u32(kHandlerAddr + 12, 0x910013DEU);
    memory.write_u32(kHandlerAddr + 16, 0xD518403EU);
    memory.write_u32(kHandlerAddr + 20, 0xD69F03E0U);

    // Step 1: MOVZ X0, #42
    cpu.step(memory);
    TEST_CHECK(cpu.read_register(0) == 42, "X0 = 42");

    // Step 2: MOVZ X1, #2, LSL #32 (0x200000000)
    cpu.step(memory);
    TEST_CHECK(cpu.read_register(1) == 0x200000000ULL, "X1 = 0x200000000");

    // Step 3: LDR X2, [X1] -> Faults!
    cpu.step(memory);
    TEST_CHECK(cpu.get_program_counter() == kHandlerAddr, "Exception taken to VBAR + 0x200");
    TEST_CHECK(cpu.get_current_el() == 1, "Stayed in EL1");

    // Step through handler:
    cpu.step(memory); // MRS X28, ESR_EL1
    cpu.step(memory); // MRS X29, FAR_EL1
    cpu.step(memory); // MRS X30, ELR_EL1
    cpu.step(memory); // ADD X30, X30, #4
    cpu.step(memory); // MSR ELR_EL1, X30
    cpu.step(memory); // ERET

    // Verify handler decoded fault correctly
    TEST_CHECK(cpu.read_register(29) == 0x200000000ULL, "FAR_EL1 captured 0x200000000");
    auto esr_info = KernelExceptionVectors::decode_esr(static_cast<std::uint32_t>(cpu.read_register(28)));
    TEST_CHECK(esr_info.ec == 0x25, "ESR EC indicates Data Abort (0x25)");

    // After ERET, PC must return to 0x100C!
    TEST_CHECK(cpu.get_program_counter() == 0x100CULL, "Returned to instruction following fault (0x100C)");

    // Step next instruction: MOVZ X3, #0xBEEF
    cpu.step(memory);
    TEST_CHECK(cpu.read_register(3) == 0xBEEF, "X3 = 0xBEEF executed cleanly after exception return");

    // Step WFI
    cpu.step(memory);
    TEST_CHECK(cpu.is_waiting_for_interrupt(), "Reached WFI");

    std::cout << "  -> Exception entry, syndrome decoding, and ERET return verified.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 206.18 Early Exception & Interrupt Handling Tests ===\n";
    test_synchronous_exception_and_eret();

    if (g_failures == 0) {
        std::cout << "=== All Stage 206.18 Tests PASSED ===\n";
        return 0;
    } else {
        std::cerr << "=== " << g_failures << " Stage 206.18 Tests FAILED ===\n";
        return 1;
    }
}

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

void test_synchronous_data_abort_and_recovery() {
    std::cout << "[Test 1] Kernel Data Abort, FAR_EL1, ESR_EL1, and Resume Past Fault..." << std::endl;

    CPU cpu;
    cpu.set_architectural_exceptions(true);

    Memory mem(0x20000); // 128 KB RAM

    // Initialize CPU at EL1h
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_stack_pointer(0x10000);

    constexpr std::uint64_t kVbarBase = 0x2000ULL; // 2048-byte aligned
    cpu.set_vbar_el1(kVbarBase);

    // Faulting address that exceeds physical RAM limit (e.g. 0x80000ULL > mem.size())
    constexpr std::uint64_t kFaultingAddr = 0x80000ULL;
    cpu.write_register(11, kFaultingAddr);
    cpu.write_register(23, 0); // handler executed flag

    // 1. Exception Handler at VBAR_EL1 + 0x200 (CurrentEL_SPx Synchronous)
    // Address: 0x2200
    // Insn 0: MRS X20, FAR_EL1 (0xD5386014)
    // Insn 1: MRS X21, ESR_EL1 (0xD5385215)
    // Insn 2: MRS X22, ELR_EL1 (0xD5384036)
    // Insn 3: ADD X22, X22, #4 (0x910012D6) -> advance PC past faulting LDR
    // Insn 4: MSR ELR_EL1, X22 (0xD5184036)
    // Insn 5: MOV X23, #1      (0xD2800037) -> set handler executed flag
    // Insn 6: ERET             (0xD69F03E0)
    const std::vector<std::uint32_t> handler_code = {
        0xD5386014U,
        0xD5385215U,
        0xD5384036U,
        0x910012D6U,
        0xD5184036U,
        0xD2800037U,
        0xD69F03E0U
    };

    const std::size_t handler_addr = static_cast<std::size_t>(kVbarBase + 0x200);
    for (std::size_t i = 0; i < handler_code.size(); ++i) {
        mem.write_u32(handler_addr + (i * 4), handler_code[i]);
    }

    // 2. Main Kernel Code at 0x1000:
    // Insn 0: LDR X10, [X11] (0xF940016A) -> triggers Synchronous Data Abort!
    // Insn 1: MOV X24, #0xCAFE (0xD2995FD8: MOVZ X24, #0xCAFE) -> resumed instruction!
    // Insn 2: NOP (0xD503201F)
    const std::vector<std::uint32_t> main_code = {
        0xF940016AU,
        0xD2995FD8U,
        0xD503201FU
    };

    for (std::size_t i = 0; i < main_code.size(); ++i) {
        mem.write_u32(0x1000 + (i * 4), main_code[i]);
    }

    cpu.set_program_counter(0x1000);

    // Step 1: Execute LDR X10, [X11] at 0x1000 -> Abort taken!
    cpu.step(mem);

    TEST_CHECK(cpu.get_program_counter() == handler_addr, "PC redirected to CurrentEL_SPx Synchronous handler (0x2200)");
    TEST_CHECK(cpu.get_far_el1() == kFaultingAddr, "FAR_EL1 captures exact faulting address");

    // Decode ESR_EL1
    auto esr_info = KernelExceptionVectors::decode_esr(static_cast<std::uint32_t>(cpu.get_esr_el1()));
    TEST_CHECK(esr_info.ec == 0x25, "ESR EC indicates Data Abort taken without a change in Exception level (0x25)");

    // Step through the handler (7 instructions)
    for (std::size_t i = 0; i < handler_code.size(); ++i) {
        cpu.step(mem);
    }

    TEST_CHECK(cpu.read_register(20) == kFaultingAddr, "Handler read FAR_EL1 into X20");
    TEST_CHECK(cpu.read_register(23) == 1, "Handler executed flag set");
    TEST_CHECK(cpu.get_program_counter() == 0x1004, "ERET successfully resumed execution at 0x1004 (past fault)");

    // Step the resumed instruction (MOV X24, #0xCAFE)
    cpu.step(mem);
    TEST_CHECK(cpu.read_register(24) == 0xCAFE, "Resumed instruction executed successfully");

    std::cout << "  -> Kernel fault recovery and resume verified." << std::endl;
}

} // namespace

int main() {
    std::cout << "=== Stage 205.16 Kernel Exception & Fault Recovery Tests ===" << std::endl;

    test_synchronous_data_abort_and_recovery();

    if (g_failures > 0) {
        std::cerr << "FAILED: " << g_failures << " tests failed.\n";
        return 1;
    }

    std::cout << "All Stage 205.16 tests PASSED." << std::endl;
    return 0;
}

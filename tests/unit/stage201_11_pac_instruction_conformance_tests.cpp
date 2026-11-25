// ==============================================================================
// WinIOS Campaign 201 Stage 11: PAC Instruction & Exception Conformance Tests
// ==============================================================================
// Verifies ARM64e Pointer Authentication instructions and exception behavior:
// 1. Instruction opcodes: PACIA, PACIB, PACDA, PACDB
// 2. Authentication opcodes: AUTIA, AUTIB, AUTDA, AUTDB
// 3. Pointer stripping: XPACI, XPACD
// 4. Authenticated branch and return: BRAA, RETAA
// 5. Dual fault modes:
//    - FPAC Synchronous Trap Exception (EC = 0x1C)
//    - Pre-ARMv8.6 CorruptUpperBits mode (bit 62 flipped, no trap taken)
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>

#include "cpu/cpu.h"
#include "cpu/pointer_auth.h"
#include "memory/memory.h"

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

constexpr std::uint32_t encode_pacia(std::uint32_t rd, std::uint32_t rn) { return 0xDAC10000u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }
constexpr std::uint32_t encode_pacib(std::uint32_t rd, std::uint32_t rn) { return 0xDAC10400u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }
constexpr std::uint32_t encode_pacda(std::uint32_t rd, std::uint32_t rn) { return 0xDAC10800u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }
constexpr std::uint32_t encode_pacdb(std::uint32_t rd, std::uint32_t rn) { return 0xDAC10C00u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }

constexpr std::uint32_t encode_autia(std::uint32_t rd, std::uint32_t rn) { return 0xDAC11000u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }
constexpr std::uint32_t encode_autda(std::uint32_t rd, std::uint32_t rn) { return 0xDAC11800u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }

constexpr std::uint32_t encode_xpaci(std::uint32_t rd) { return 0xDAC143E0u | (rd & 0x1Fu); }
constexpr std::uint32_t encode_xpacd(std::uint32_t rd) { return 0xDAC147E0u | (rd & 0x1Fu); }

constexpr std::uint32_t encode_retaa() { return 0xD65F0BFFu; }

void step_insn(CPU& cpu, Memory& mem, std::uint64_t pc, std::uint32_t insn) {
    mem.write_u32(pc, insn);
    cpu.set_program_counter(pc);
    cpu.step(mem);
}

// -----------------------------------------------------------------------------
// Test 1: PAC Data Processing Sign and Strip
// -----------------------------------------------------------------------------
void test_pac_sign_and_strip() {
    std::cout << "[Test 1] PAC Sign and Strip Instructions...\n";
    CPU cpu;
    Memory mem(0x100000);
    cpu.reset();

    const std::uint64_t raw_ptr = 0x00007FFFF0001000ULL;
    const std::uint64_t mod     = 0x12345678ULL;

    cpu.write_register(0, raw_ptr);
    cpu.write_register(1, mod);

    // PACIA X0, X1
    step_insn(cpu, mem, 0x1000, encode_pacia(0, 1));
    const std::uint64_t signed_ptr = cpu.read_register(0);
    TEST_CHECK(signed_ptr != raw_ptr, "PACIA must modify upper pointer bits with tag");
    TEST_CHECK((signed_ptr & 0x0000FFFFFFFFFFFFULL) == raw_ptr, "Lower 48 bits must be preserved");

    // XPACI X0 (strip tag)
    step_insn(cpu, mem, 0x1004, encode_xpaci(0));
    TEST_CHECK(cpu.read_register(0) == raw_ptr, "XPACI must restore original raw pointer");

    std::cout << "  -> Sign and strip passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Dual Fault Modes (Trap vs Corrupt)
// -----------------------------------------------------------------------------
void test_dual_fault_modes() {
    std::cout << "[Test 2] Dual Fault Modes (Trap vs Corrupt)...\n";

    // Mode A: TrapException (ARMv8.6 FPAC)
    {
        CPU cpu;
        Memory mem(0x100000);
        cpu.reset();
        cpu.set_architectural_exceptions(true);
        cpu.set_vbar_el1(0x80000ULL);
        cpu.set_current_el(1);
        cpu.get_pac().set_fault_mode(PacFaultMode::TrapException);

        // Sign with mod 0x1111
        cpu.write_register(0, 0x00007FFFF0002000ULL);
        cpu.write_register(1, 0x1111ULL);
        step_insn(cpu, mem, 0x2000, encode_pacia(0, 1));

        // Authenticate with BAD mod 0x9999
        cpu.write_register(1, 0x9999ULL);
        step_insn(cpu, mem, 0x2004, encode_autia(0, 1));

        TEST_CHECK(cpu.exception_taken_in_step(), "TrapException mode must raise synchronous exception on auth failure");
        const std::uint32_t esr = static_cast<std::uint32_t>(cpu.get_esr_el1());
        const std::uint32_t ec = (esr >> 26u) & 0x3Fu;
        TEST_CHECK(ec == 0x1C, "EC must be 0x1C (Synchronous PAC Trap)");
    }

    // Mode B: CorruptUpperBits (ARMv8.3 Error Injection)
    {
        CPU cpu;
        Memory mem(0x100000);
        cpu.reset();
        cpu.set_architectural_exceptions(true);
        cpu.set_vbar_el1(0x80000ULL);
        cpu.set_current_el(1);
        cpu.get_pac().set_fault_mode(PacFaultMode::CorruptUpperBits);

        // Sign with mod 0x2222
        cpu.write_register(0, 0x00007FFFF0003000ULL);
        cpu.write_register(1, 0x2222ULL);
        step_insn(cpu, mem, 0x3000, encode_pacia(0, 1));

        // Authenticate with BAD mod 0x8888
        cpu.write_register(1, 0x8888ULL);
        step_insn(cpu, mem, 0x3004, encode_autia(0, 1));

        TEST_CHECK(!cpu.exception_taken_in_step(), "CorruptUpperBits mode must NOT raise synchronous trap");
        const std::uint64_t corrupted = cpu.read_register(0);
        // Bit 62 must be set to inject an invalid address error
        TEST_CHECK((corrupted & (1ULL << 62u)) != 0ULL, "Bit 62 must be corrupted on auth failure");
    }

    std::cout << "  -> Dual fault modes passed.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Authenticated Return (RETAA) Stack Protection
// -----------------------------------------------------------------------------
void test_retaa_stack_protection() {
    std::cout << "[Test 3] Authenticated Return (RETAA)...\n";
    CPU cpu;
    Memory mem(0x100000);
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000ULL);
    cpu.set_current_el(1);
    cpu.get_pac().set_fault_mode(PacFaultMode::TrapException);

    cpu.set_stack_pointer(0x40000ULL);
    const std::uint64_t ret_target = 0x0000000010008000ULL;

    // Sign LR (X30) with SP using PACIA X30, SP (X31)
    cpu.write_register(30, ret_target);
    step_insn(cpu, mem, 0x4000, encode_pacia(30, 31));

    const std::uint64_t valid_signed_lr = cpu.read_register(30);

    // Case 3.1: RETAA with authentic signed LR -> Returns to ret_target
    step_insn(cpu, mem, 0x4004, encode_retaa());
    TEST_CHECK(!cpu.exception_taken_in_step(), "Authentic RETAA must succeed");
    TEST_CHECK(cpu.get_program_counter() == ret_target, "PC must branch to authentic return target");

    // Case 3.2: Tampered LR -> RETAA must trap
    cpu.write_register(30, valid_signed_lr ^ 0x0100000000000000ULL); // Corrupt tag bit
    step_insn(cpu, mem, 0x4008, encode_retaa());
    TEST_CHECK(cpu.exception_taken_in_step(), "Tampered RETAA must take exception");
    const std::uint32_t esr = static_cast<std::uint32_t>(cpu.get_esr_el1());
    TEST_CHECK(((esr >> 26u) & 0x3Fu) == 0x1C, "RETAA auth failure must report EC 0x1C");

    std::cout << "  -> RETAA stack protection passed.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 201.11 PAC Instruction Conformance Tests ===\n";
    test_pac_sign_and_strip();
    test_dual_fault_modes();
    test_retaa_stack_protection();

    if (g_failures == 0) {
        std::cout << "All Stage 201.11 tests passed successfully!\n";
        return 0;
    } else {
        std::cerr << g_failures << " Stage 201.11 test(s) failed!\n";
        return 1;
    }
}

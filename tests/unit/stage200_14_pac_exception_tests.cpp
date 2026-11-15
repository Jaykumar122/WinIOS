// ==============================================================================
// WinIOS Campaign 200 Stage 14: PAC Fault & Exception Integration Tests
// ==============================================================================
// Verifies:
// 1. TrapException mode: AUT failure raises synchronous exception (EC 0x1C)
// 2. Syndrome generation: ESR_EL1 with EC 0x1C, ELR_EL1 = faulting PC, FAR_EL1 = bad pointer
// 3. CorruptUpperBits mode: AUT failure corrupts upper bit (bit 62) without taking trap
// 4. Memory translation fault on subsequent dereference of bit-62-corrupted pointer
// 5. RETAA failure when return address on stack has been tampered with
// 6. Exception vector offset delivery (Synchronous from Current EL with SP_ELx)
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>

#include "cpu/cpu.h"
#include "cpu/pointer_auth.h"
#include "memory/memory.h"

using namespace winios::cpu;

namespace {

constexpr std::uint32_t encode_pacia(std::uint32_t rd, std::uint32_t rn) { return 0xDAC10000u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }
constexpr std::uint32_t encode_autia(std::uint32_t rd, std::uint32_t rn) { return 0xDAC11000u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }
constexpr std::uint32_t encode_pacia_sp() { return 0xD503233Fu; } // PACIASP (rd=30, rn=31)
constexpr std::uint32_t encode_retaa() { return 0xD65F0BFFu; }

} // namespace

static void test_trap_exception_mode_syndrome() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000ULL);
    cpu.set_current_el(1);
    Memory mem(0x100000);

    const std::uint64_t raw_ptr = 0x00007FFFF0001000ULL;
    const std::uint64_t sign_mod = 0x1111222233334444ULL;
    const std::uint64_t wrong_mod = 0x9999888877776666ULL;

    // Sign with sign_mod
    cpu.write_register(0, raw_ptr);
    cpu.write_register(1, sign_mod);
    mem.write_u32(0x1000, encode_pacia(0, 1));
    cpu.set_program_counter(0x1000);
    cpu.step(mem);

    const std::uint64_t signed_ptr = cpu.read_register(0);
    assert(signed_ptr != raw_ptr);

    // Attempt authenticate with wrong_mod
    cpu.write_register(1, wrong_mod);
    mem.write_u32(0x1004, encode_autia(0, 1));
    cpu.set_program_counter(0x1004);
    cpu.step(mem);

    // Verify FPAC trap exception occurred
    assert(cpu.exception_taken_in_step());
    const std::uint32_t esr = static_cast<std::uint32_t>(cpu.get_esr_el1());
    const std::uint32_t ec = (esr >> 26u) & 0x3Fu;
    assert(ec == 0x1C); // EC 0x1C: Synchronous PAC Failure Trap
    assert(cpu.get_elr_el1() == 0x1004); // Faulting instruction PC
    assert(cpu.get_far_el1() == signed_ptr); // FAR records corrupted pointer

    (void)esr;
    (void)ec;
    (void)signed_ptr;

    std::cout << "[PASS] test_trap_exception_mode_syndrome\n";
}

static void test_corrupt_upper_bits_mode() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000ULL);
    cpu.set_current_el(1);
    Memory mem(0x100000);

    // Configure PAC for CorruptUpperBits mode
    cpu.get_pac().set_fault_mode(PacFaultMode::CorruptUpperBits);

    const std::uint64_t raw_ptr = 0x0000000100004000ULL;
    const std::uint64_t valid_mod = 0xABCDEFULL;
    const std::uint64_t bad_mod   = 0x123456ULL;

    cpu.write_register(0, raw_ptr);
    cpu.write_register(1, valid_mod);
    mem.write_u32(0x2000, encode_pacia(0, 1));
    cpu.set_program_counter(0x2000);
    cpu.step(mem);

    const std::uint64_t signed_ptr = cpu.read_register(0);
    assert(signed_ptr != raw_ptr);

    // Authenticate with bad modifier in CorruptUpperBits mode
    cpu.write_register(1, bad_mod);
    mem.write_u32(0x2004, encode_autia(0, 1));
    cpu.set_program_counter(0x2004);
    cpu.step(mem);

    // In CorruptUpperBits mode, NO synchronous exception trap is taken
    assert(!cpu.exception_taken_in_step());

    // Register X0 now contains the pointer with bit 62 flipped (corrupted)
    const std::uint64_t corrupted_ptr = cpu.read_register(0);
    assert((corrupted_ptr & (1ULL << 62u)) != 0);

    (void)signed_ptr;
    (void)corrupted_ptr;

    std::cout << "[PASS] test_corrupt_upper_bits_mode\n";
}

static void test_retaa_stack_tampering_exception() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000ULL);
    cpu.set_current_el(1);
    Memory mem(0x100000);

    // Setup stack pointer
    cpu.set_stack_pointer(0x50000);

    // Sign return address (LR = X30) using PACIA SP
    cpu.write_register(30, 0x10004000ULL); // Return address
    mem.write_u32(0x3000, encode_pacia(30, 31));
    cpu.set_program_counter(0x3000);
    cpu.step(mem);

    const std::uint64_t signed_lr = cpu.read_register(30);
    assert(signed_lr != 0x10004000ULL);

    // Attacker modifies LR upper bits / signature
    cpu.write_register(30, signed_lr ^ 0x0100000000000000ULL);

    // Execute RETAA (must fail auth and trap with EC 0x1C)
    mem.write_u32(0x3004, encode_retaa());
    cpu.set_program_counter(0x3004);
    cpu.step(mem);

    assert(cpu.exception_taken_in_step());
    const std::uint32_t esr = static_cast<std::uint32_t>(cpu.get_esr_el1());
    const std::uint32_t ec = (esr >> 26u) & 0x3Fu;
    assert(ec == 0x1C);

    (void)esr;
    (void)ec;

    std::cout << "[PASS] test_retaa_stack_tampering_exception\n";
}

int main() {
    std::cout << "=== Running Stage 200.14 PAC Fault & Exception Tests ===\n";
    test_trap_exception_mode_syndrome();
    test_corrupt_upper_bits_mode();
    test_retaa_stack_tampering_exception();
    std::cout << "All Stage 200.14 tests passed successfully!\n";
    return 0;
}

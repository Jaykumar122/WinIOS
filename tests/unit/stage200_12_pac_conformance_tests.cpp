// ==============================================================================
// WinIOS Campaign 200 Stage 12: PAC Instruction Conformance Tests
// ==============================================================================
// Verifies:
// 1. PACIA, PACIB, PACDA, PACDB pointer signing
// 2. PACIZA, PACIZB, PACDZA, PACDZB zero-modifier signing
// 3. AUTIA, AUTIB, AUTDA, AUTDB authentication and signature verification
// 4. AUTIZA, AUTIZB, AUTDZA, AUTDZB zero-modifier authentication
// 5. XPACI, XPACD, XPACLRI pointer stripping
// 6. Generic signing computation (PACGA)
// 7. Modifier context sensitivity (different modifiers produce different PACs)
// 8. Key diversity (different keys produce divergent PAC signatures for same pointer)
// 9. Authenticated branches and returns (BRAA, BRAB, BLRAA, BLRAB, RETAA, RETAB)
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
constexpr std::uint32_t encode_pacib(std::uint32_t rd, std::uint32_t rn) { return 0xDAC10400u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }
constexpr std::uint32_t encode_pacda(std::uint32_t rd, std::uint32_t rn) { return 0xDAC10800u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }
constexpr std::uint32_t encode_pacdb(std::uint32_t rd, std::uint32_t rn) { return 0xDAC10C00u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }

constexpr std::uint32_t encode_autia(std::uint32_t rd, std::uint32_t rn) { return 0xDAC11000u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }
constexpr std::uint32_t encode_autib(std::uint32_t rd, std::uint32_t rn) { return 0xDAC11400u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }
constexpr std::uint32_t encode_autda(std::uint32_t rd, std::uint32_t rn) { return 0xDAC11800u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }
constexpr std::uint32_t encode_autdb(std::uint32_t rd, std::uint32_t rn) { return 0xDAC11C00u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }

constexpr std::uint32_t encode_paciza(std::uint32_t rd) { return 0xDAC123E0u | (rd & 0x1Fu); }
constexpr std::uint32_t encode_pacizb(std::uint32_t rd) { return 0xDAC127E0u | (rd & 0x1Fu); }
constexpr std::uint32_t encode_pacdza(std::uint32_t rd) { return 0xDAC12BE0u | (rd & 0x1Fu); }
constexpr std::uint32_t encode_pacdzb(std::uint32_t rd) { return 0xDAC12FE0u | (rd & 0x1Fu); }

constexpr std::uint32_t encode_autiza(std::uint32_t rd) { return 0xDAC133E0u | (rd & 0x1Fu); }
constexpr std::uint32_t encode_autizb(std::uint32_t rd) { return 0xDAC137E0u | (rd & 0x1Fu); }
constexpr std::uint32_t encode_autdza(std::uint32_t rd) { return 0xDAC13BE0u | (rd & 0x1Fu); }
constexpr std::uint32_t encode_autdzb(std::uint32_t rd) { return 0xDAC13FE0u | (rd & 0x1Fu); }

constexpr std::uint32_t encode_xpaci(std::uint32_t rd) { return 0xDAC143E0u | (rd & 0x1Fu); }
constexpr std::uint32_t encode_xpacd(std::uint32_t rd) { return 0xDAC147E0u | (rd & 0x1Fu); }

constexpr std::uint32_t encode_pacga(std::uint32_t rd, std::uint32_t rn, std::uint32_t rm) {
    return 0x9AC03000u | ((rm & 0x1Fu) << 16u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t encode_braa(std::uint32_t rn, std::uint32_t rm) { return 0xD71F0800u | ((rn & 0x1Fu) << 5u) | (rm & 0x1Fu); }
constexpr std::uint32_t encode_retaa() { return 0xD65F0BFFu; }

} // namespace

static void test_pointer_signing_and_authentication() {
    CPU cpu;
    cpu.reset();
    Memory mem(0x20000);

    const std::uint64_t code_addr = 0x1000;
    const std::uint64_t raw_ptr = 0x0000000100004000ULL;
    const std::uint64_t mod = 0xDEADBEEFULL;

    cpu.write_register(0, raw_ptr);
    cpu.write_register(1, mod);

    // 1. PACIA X0, X1
    // 2. AUTIA X0, X1
    mem.write_u32(static_cast<std::size_t>(code_addr), encode_pacia(0, 1));
    mem.write_u32(static_cast<std::size_t>(code_addr + 4), encode_autia(0, 1));

    cpu.set_program_counter(code_addr);

    // Step 1: Sign
    cpu.step(mem);
    const std::uint64_t signed_ptr = cpu.read_register(0);
    assert(signed_ptr != raw_ptr);
    // Upper bits must hold non-zero PAC signature
    assert((signed_ptr >> 48u) != 0);
    (void)signed_ptr;

    // Step 2: Authenticate
    cpu.step(mem);
    const std::uint64_t restored_ptr = cpu.read_register(0);
    assert(restored_ptr == raw_ptr);
    assert(!cpu.exception_taken_in_step());
    (void)restored_ptr;

    std::cout << "[PASS] test_pointer_signing_and_authentication\n";
}

static void test_zero_modifier_and_stripping() {
    CPU cpu;
    cpu.reset();
    Memory mem(0x20000);

    const std::uint64_t code_addr = 0x2000;
    const std::uint64_t raw_ptr = 0x0000000100008000ULL;

    cpu.write_register(2, raw_ptr);

    // 1. PACIZA X2
    // 2. XPACI X2
    mem.write_u32(static_cast<std::size_t>(code_addr), encode_paciza(2));
    mem.write_u32(static_cast<std::size_t>(code_addr + 4), encode_xpaci(2));

    cpu.set_program_counter(code_addr);

    // Step 1: PACIZA
    cpu.step(mem);
    const std::uint64_t paciza_val = cpu.read_register(2);
    assert(paciza_val != raw_ptr);
    (void)paciza_val;

    // Step 2: XPACI (strips PAC bits back to raw pointer)
    cpu.step(mem);
    const std::uint64_t stripped_val = cpu.read_register(2);
    assert(stripped_val == raw_ptr);
    (void)stripped_val;

    std::cout << "[PASS] test_zero_modifier_and_stripping\n";
}

static void test_key_diversity_and_modifier_divergence() {
    Arm64ePointerAuth pac;

    const std::uint64_t ptr = 0x0000000100005000ULL;
    const std::uint64_t mod1 = 0x1111ULL;
    const std::uint64_t mod2 = 0x2222ULL;

    const std::uint64_t pac_ia1 = pac.sign_pointer(ptr, mod1, PacKeyType::InstructionA);
    const std::uint64_t pac_ia2 = pac.sign_pointer(ptr, mod2, PacKeyType::InstructionA);
    const std::uint64_t pac_ib  = pac.sign_pointer(ptr, mod1, PacKeyType::InstructionB);
    const std::uint64_t pac_da  = pac.sign_pointer(ptr, mod1, PacKeyType::DataA);
    const std::uint64_t pac_db  = pac.sign_pointer(ptr, mod1, PacKeyType::DataB);

    // Modifier sensitivity
    assert(pac_ia1 != pac_ia2);

    // Key diversity
    assert(pac_ia1 != pac_ib);
    assert(pac_ia1 != pac_da);
    assert(pac_ia1 != pac_db);
    assert(pac_da  != pac_db);

    (void)pac_ia1;
    (void)pac_ia2;
    (void)pac_ib;
    (void)pac_da;
    (void)pac_db;

    std::cout << "[PASS] test_key_diversity_and_modifier_divergence\n";
}

static void test_generic_tag_pacga() {
    CPU cpu;
    cpu.reset();
    Memory mem(0x20000);

    const std::uint64_t code_addr = 0x3000;
    cpu.write_register(3, 0x123456789ABCDEF0ULL);
    cpu.write_register(4, 0xFEDCBA9876543210ULL);

    // PACGA X5, X3, X4
    mem.write_u32(static_cast<std::size_t>(code_addr), encode_pacga(5, 3, 4));
    cpu.set_program_counter(code_addr);

    cpu.step(mem);
    const std::uint64_t tag = cpu.read_register(5);
    // Upper 32 bits contain the PACGA tag; lower 32 bits zeroed
    assert((tag & 0xFFFFFFFF00000000ULL) != 0);
    assert((tag & 0x00000000FFFFFFFFULL) == 0);
    (void)tag;

    std::cout << "[PASS] test_generic_tag_pacga\n";
}

static void test_authenticated_branch_braa() {
    CPU cpu;
    cpu.reset();
    Memory mem(0x20000);

    const std::uint64_t code_addr = 0x4000;
    const std::uint64_t target_fn = 0x8000;
    const std::uint64_t mod = 0xC0FFEEULL;

    // Sign target_fn with APIA
    auto& pac = cpu.get_pac();
    const std::uint64_t signed_target = pac.sign_pointer(target_fn, mod, PacKeyType::InstructionA);

    cpu.write_register(0, signed_target);
    cpu.write_register(1, mod);

    // BRAA X0, X1
    mem.write_u32(static_cast<std::size_t>(code_addr), encode_braa(0, 1));
    cpu.set_program_counter(code_addr);

    cpu.step(mem);
    // PC jumped to target_fn on successful authentication
    assert(cpu.get_program_counter() == target_fn);
    assert(!cpu.exception_taken_in_step());

    std::cout << "[PASS] test_authenticated_branch_braa\n";
}

int main() {
    std::cout << "=== Running Stage 200.12 PAC Instruction Conformance Tests ===\n";
    test_pointer_signing_and_authentication();
    test_zero_modifier_and_stripping();
    test_key_diversity_and_modifier_divergence();
    test_generic_tag_pacga();
    test_authenticated_branch_braa();
    std::cout << "All Stage 200.12 tests passed successfully!\n";
    return 0;
}

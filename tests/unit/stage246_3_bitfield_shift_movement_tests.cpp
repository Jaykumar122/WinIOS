// ============================================================================
// Stage 246.3: ARM64 Bitfield, Shift & Extract Conformance Tests
// Verifies UBFM (unsigned bitfield extract), SBFM (signed bitfield extract
// with sign-extension), and EXTR (extract from register pair) with boundaries.
// ============================================================================

#include "cpu/cpu.h"
#include "memory/memory.h"

#include <iostream>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

static void test_ubfm_unsigned_extract() {
    std::cout << "[Stage 246.3 - Test 1] UBFM / UBFX unsigned bitfield extraction...\n";
    CPU cpu;
    Memory mem(0x100000);
    const uint64_t pc = 0x20000;

    // UBFX X0, X1, #8, #16 -> extracts bits [23:8]
    // Opcode: 0xD3485C20u
    cpu.reset();
    cpu.write_register(1, 0x1234567890ABCDEFULL);
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0xD3485C20u);
    cpu.step(mem);

    // Bits [23:8] of 0x...ABCDEF is 0xABCD
    TEST_ASSERT(cpu.read_register(0) == 0xABCDULL, "UBFX extracts bits [23:8] cleanly");
}

static void test_sbfm_signed_extract() {
    std::cout << "[Stage 246.3 - Test 2] SBFM / SBFX sign-extending bitfield extraction...\n";
    CPU cpu;
    Memory mem(0x100000);
    const uint64_t pc = 0x20000;

    // SBFX X0, X1, #0, #8 -> extracts lower 8 bits and sign-extends to 64-bit
    // Opcode: 0x93401C20u
    // Case A: Negative sign bit (0x80 -> -128)
    cpu.reset();
    cpu.write_register(1, 0x80);
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0x93401C20u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 0xFFFFFFFFFFFFFF80ULL, "SBFX sign-extends 0x80 to negative 64-bit");

    // Case B: Positive sign bit (0x7F -> +127)
    cpu.reset();
    cpu.write_register(1, 0x7F);
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0x93401C20u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 0x7FULL, "SBFX sign-extends 0x7F to positive 64-bit");
}

static void test_extr_register_pair_extract() {
    std::cout << "[Stage 246.3 - Test 3] EXTR concatenated register pair extraction...\n";
    CPU cpu;
    Memory mem(0x100000);
    const uint64_t pc = 0x20000;

    // EXTR X0, X1, X2, #16
    // Opcode: 0x93C24020u (sf=1, N=1 for 64-bit EXTR)
    cpu.reset();
    cpu.write_register(1, 0x1111111111111111ULL);
    cpu.write_register(2, 0x2222222222222222ULL);
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0x93C24020u);
    cpu.step(mem);

    uint64_t expected = (0x2222222222222222ULL >> 16u) | (0x1111111111111111ULL << 48u);
    TEST_ASSERT(cpu.read_register(0) == expected, "EXTR extracts correctly across concatenated register pair");
}

int main() {
    std::cout << "=== Running Stage 246.3 Bitfield & Shift Movement Tests ===\n";
    test_ubfm_unsigned_extract();
    test_sbfm_signed_extract();
    test_extr_register_pair_extract();
    std::cout << "Stage 246.3: All bitfield and shift tests passed.\n";
    return 0;
}

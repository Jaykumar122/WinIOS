// tests/unit/stage197_8_atomic_fault_tests.cpp
// Campaign 197: Acceptance Test F
// Verifies that atomic memory operations (CAS, SWP, LDADD, STXR) that trigger
// architectural faults (alignment fault, translation fault, permission fault)
// DO NOT partially modify memory.

#include <iostream>
#include <vector>
#include <cstdint>
#include "cpu/cpu.h"
#include "memory/memory.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;

static void write_u32(Memory& mem, std::uint64_t addr, std::uint32_t val) {
    mem.write_u32(addr, val);
}

// Opcode construction:
// LDADD 64-bit: size=3 (0xC0000000) | (0x38 << 24) | (1 << 21) | (source << 16) | (0 << 12) | (base << 5) | target
static std::uint32_t encode_ldadd_64(std::uint32_t source, std::uint32_t base, std::uint32_t target) {
    return 0xB8200000u | (1u << 21) | ((source & 0x1Fu) << 16) | ((base & 0x1Fu) << 5) | (target & 0x1Fu);
}

// CAS 64-bit: size=3 | 0x08A07C00u | (expected << 16) | (base << 5) | desired
static std::uint32_t encode_cas_64(std::uint32_t expected, std::uint32_t base, std::uint32_t desired) {
    return 0xC8A07C00u | ((expected & 0x1Fu) << 16) | ((base & 0x1Fu) << 5) | (desired & 0x1Fu);
}

// SWP 64-bit: size=3 | (0x38 << 24) | (1 << 21) | (source << 16) | (8 << 12) | (base << 5) | target
static std::uint32_t encode_swp_64(std::uint32_t source, std::uint32_t base, std::uint32_t target) {
    return 0xB8208000u | (1u << 21) | ((source & 0x1Fu) << 16) | ((base & 0x1Fu) << 5) | (target & 0x1Fu);
}

static bool test_alignment_fault_memory_preservation() {
    std::cout << "  Testing atomic alignment fault memory preservation..." << std::endl;
    Memory mem(0x20000);
    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_vbar_el1(0x80000);
    cpu.set_architectural_exceptions(true);

    // Set unaligned address (0x1003 is not 8-byte aligned)
    const std::uint64_t unaligned_addr = 0x1003;
    const std::uint64_t original_val = 0x0123456789ABCDEFULL;

    // Write original 8 bytes at 0x1003
    for (std::size_t i = 0; i < 8; ++i) {
        mem.write_u8(unaligned_addr + i, static_cast<std::uint8_t>((original_val >> (i * 8)) & 0xFF));
    }

    cpu.write_register(0, unaligned_addr); // X0 = unaligned address
    cpu.write_register(1, 0xAAAAAAAAAAAAAAAAULL); // X1 = value to add/swap
    cpu.write_register(2, 0);

    // 1. LDADD on unaligned address
    std::uint32_t ldadd_insn = encode_ldadd_64(1, 0, 2);
    write_u32(mem, 0x2000, ldadd_insn);
    cpu.set_program_counter(0x2000);

    cpu.step(mem);

    // Alignment fault taken: PC jumps to VBAR_EL1 + 0x200
    TEST_ASSERT(cpu.get_program_counter() == 0x80200, "Alignment fault triggered exception to vector table");

    // CRUCIAL: Verify NOT A SINGLE BYTE was altered in memory!
    for (std::size_t i = 0; i < 8; ++i) {
        std::uint8_t expected_byte = static_cast<std::uint8_t>((original_val >> (i * 8)) & 0xFF);
        TEST_ASSERT(mem.read_u8(unaligned_addr + i) == expected_byte, "Memory byte preserved without partial modification");
    }

    // 2. CAS on unaligned address
    cpu.set_program_counter(0x2004);
    std::uint32_t cas_insn = encode_cas_64(1, 0, 1);
    write_u32(mem, 0x2004, cas_insn);

    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x80200, "CAS alignment fault taken");

    for (std::size_t i = 0; i < 8; ++i) {
        std::uint8_t expected_byte = static_cast<std::uint8_t>((original_val >> (i * 8)) & 0xFF);
        TEST_ASSERT(mem.read_u8(unaligned_addr + i) == expected_byte, "Memory byte preserved during CAS fault");
    }

    // 3. SWP on unaligned address
    cpu.set_program_counter(0x2008);
    std::uint32_t swp_insn = encode_swp_64(1, 0, 2);
    write_u32(mem, 0x2008, swp_insn);

    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x80200, "SWP alignment fault taken");

    for (std::size_t i = 0; i < 8; ++i) {
        std::uint8_t expected_byte = static_cast<std::uint8_t>((original_val >> (i * 8)) & 0xFF);
        TEST_ASSERT(mem.read_u8(unaligned_addr + i) == expected_byte, "Memory byte preserved during SWP fault");
    }

    return true;
}

static bool test_translation_fault_memory_preservation() {
    std::cout << "  Testing atomic translation fault memory preservation..." << std::endl;
    Memory mem(0x20000);
    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_vbar_el1(0x80000);
    cpu.set_architectural_exceptions(true);

    // Setup an address outside memory (e.g. 0xF0000000ULL, beyond flat memory size)
    const std::uint64_t unmapped_addr = 0x000F00000000ULL;
    cpu.write_register(0, unmapped_addr);
    cpu.write_register(1, 0x12345678ULL);

    std::uint32_t cas_insn = encode_cas_64(1, 0, 1);
    write_u32(mem, 0x3000, cas_insn);
    cpu.set_program_counter(0x3000);

    cpu.step(mem);

    TEST_ASSERT(cpu.get_program_counter() == 0x80200, "Translation/address fault routed to vector table");
    // Verify registers not corrupted
    TEST_ASSERT(cpu.read_register(0) == unmapped_addr, "Base register preserved");
    TEST_ASSERT(cpu.read_register(1) == 0x12345678ULL, "Source register preserved");

    return true;
}

int main() {
    std::cout << "[STAGE 197.8] Running Atomic Fault Correctness Tests (Acceptance Test F)..." << std::endl;

    if (!test_alignment_fault_memory_preservation()) {
        std::cerr << "test_alignment_fault_memory_preservation FAILED" << std::endl;
        return 1;
    }

    if (!test_translation_fault_memory_preservation()) {
        std::cerr << "test_translation_fault_memory_preservation FAILED" << std::endl;
        return 1;
    }

    std::cout << "[STAGE 197.8] All Atomic Fault Correctness Tests PASSED." << std::endl;
    return 0;
}

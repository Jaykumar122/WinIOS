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

// Opcode constants:
// cas  w0, w1, [x2]  -> 0x88a07c41 (Rs=0, Rn=2, Rt=1)
// casa w0, w1, [x2]  -> 0x88e07c41
// casl w0, w1, [x2]  -> 0x88a0fc41
// casal w0, w1, [x2] -> 0x88e0fc41
// cas  x0, x1, [x2]  -> 0xc8a07c41
// swp  w0, w1, [x2]  -> 0xb8208041 (Rs=0 is source value, Rt=1 receives old value)
// ldadd w0, w1, [x2] -> 0xb8200041 (Rs=0 is value to add, Rt=1 receives old value)
// ldclr w0, w1, [x2] -> 0xb8201041 (Rs=0 is mask to clear, Rt=1 receives old value)
// ldeor w0, w1, [x2] -> 0xb8202041 (Rs=0 is mask to xor, Rt=1 receives old value)
// ldset w0, w1, [x2] -> 0xb8203041 (Rs=0 is mask to set, Rt=1 receives old value)

static bool test_cas_success_and_failure() {
    Memory mem(0x10000);
    CPU cpu;
    cpu.set_current_el(1);

    // Initial memory at 0x8000: 0x12345678
    mem.write_u32(0x8000, 0x12345678u);

    // 0x1000: cas w0, w1, [x2]
    write_u32(mem, 0x1000, 0x88a07c41u);

    cpu.write_register(0, 0x12345678u); // expected (Rs): matches memory
    cpu.write_register(1, 0x99887766u); // desired (Rt): new value
    cpu.write_register(2, 0x8000);       // base pointer (Rn)
    cpu.set_program_counter(0x1000);

    cpu.step(mem); // CAS should succeed
    TEST_ASSERT(mem.read_u32(0x8000) == 0x99887766u, "Memory updated to desired value");
    TEST_ASSERT(cpu.read_register(0) == 0x12345678u, "Expected register received old memory value");

    // Second CAS: expected doesn't match current memory
    write_u32(mem, 0x1004, 0x88a07c41u);
    cpu.write_register(0, 0x11111111u); // wrong expected value
    cpu.write_register(1, 0x22222222u); // desired
    cpu.set_program_counter(0x1004);

    cpu.step(mem); // CAS should fail
    TEST_ASSERT(mem.read_u32(0x8000) == 0x99887766u, "Memory unchanged after failed CAS");
    TEST_ASSERT(cpu.read_register(0) == 0x99887766u, "Expected register loaded with current memory value");
    return true;
}

static bool test_casa_casl_casal() {
    Memory mem(0x10000);
    CPU cpu;
    cpu.set_current_el(1);

    mem.write_u32(0x8000, 100);
    mem.write_u32(0x8004, 200);
    mem.write_u32(0x8008, 300);

    // 0x1000: casa  w0, w1, [x2] -> 0x88e07c41
    // 0x1004: casl  w0, w1, [x2] -> 0x88a0fc41
    // 0x1008: casal w0, w1, [x2] -> 0x88e0fc41
    write_u32(mem, 0x1000, 0x88e07c41u);
    write_u32(mem, 0x1004, 0x88a0fc41u);
    write_u32(mem, 0x1008, 0x88e0fc41u);

    // Test CASA
    cpu.write_register(0, 100);
    cpu.write_register(1, 101);
    cpu.write_register(2, 0x8000);
    cpu.set_program_counter(0x1000);
    cpu.step(mem);
    TEST_ASSERT(mem.read_u32(0x8000) == 101, "CASA succeeded");

    // Test CASL
    cpu.write_register(0, 200);
    cpu.write_register(1, 201);
    cpu.write_register(2, 0x8004);
    cpu.set_program_counter(0x1004);
    cpu.step(mem);
    TEST_ASSERT(mem.read_u32(0x8004) == 201, "CASL succeeded");

    // Test CASAL
    cpu.write_register(0, 300);
    cpu.write_register(1, 301);
    cpu.write_register(2, 0x8008);
    cpu.set_program_counter(0x1008);
    cpu.step(mem);
    TEST_ASSERT(mem.read_u32(0x8008) == 301, "CASAL succeeded");
    return true;
}

static bool test_cas_64bit() {
    Memory mem(0x10000);
    CPU cpu;
    cpu.set_current_el(1);

    mem.write_u64(0x8000, 0x1122334455667788ULL);
    // 0x1000: cas x0, x1, [x2] -> 0xc8a07c41
    write_u32(mem, 0x1000, 0xc8a07c41u);

    cpu.write_register(0, 0x1122334455667788ULL);
    cpu.write_register(1, 0x9988776655443322ULL);
    cpu.write_register(2, 0x8000);
    cpu.set_program_counter(0x1000);
    cpu.step(mem);

    TEST_ASSERT(mem.read_u64(0x8000) == 0x9988776655443322ULL, "64-bit CAS updated memory");
    TEST_ASSERT(cpu.read_register(0) == 0x1122334455667788ULL, "64-bit CAS returned old value in X0");
    return true;
}

static bool test_swp() {
    Memory mem(0x10000);
    CPU cpu;
    cpu.set_current_el(1);

    mem.write_u32(0x8000, 0xCAFEBABE);
    // swp w0, w1, [x2] -> 0xb8208041 (Rs=0, Rt=1, Rn=2)
    write_u32(mem, 0x1000, 0xb8208041u);

    cpu.write_register(0, 0xDEADBEEF); // new value to write to memory (Rs)
    cpu.write_register(1, 0x1111);     // target register for old value (Rt)
    cpu.write_register(2, 0x8000);     // base pointer (Rn)
    cpu.set_program_counter(0x1000);

    cpu.step(mem);
    TEST_ASSERT(mem.read_u32(0x8000) == 0xDEADBEEF, "SWP stored new value in memory");
    TEST_ASSERT(cpu.read_register(1) == 0xCAFEBABE, "SWP returned old memory value in target register");
    return true;
}

static bool test_ldadd_ldclr_ldeor_ldset() {
    Memory mem(0x10000);
    CPU cpu;
    cpu.set_current_el(1);

    // 1. LDADD: memory has 50, add 25 -> 75
    mem.write_u32(0x8000, 50);
    // ldadd w0, w1, [x2] -> 0xb8200041 (Rs=0, Rt=1, Rn=2)
    write_u32(mem, 0x1000, 0xb8200041u);
    cpu.write_register(0, 25);     // Rs: value to add
    cpu.write_register(1, 0);      // Rt: target for old value
    cpu.write_register(2, 0x8000); // Rn
    cpu.set_program_counter(0x1000);
    cpu.step(mem);
    TEST_ASSERT(mem.read_u32(0x8000) == 75, "LDADD incremented memory");
    TEST_ASSERT(cpu.read_register(1) == 50, "LDADD returned old value in target register");

    // 2. LDCLR: memory has 0xFF, clear 0x0F -> 0xF0
    mem.write_u32(0x8000, 0xFF);
    // ldclr w0, w1, [x2] -> 0xb8201041
    write_u32(mem, 0x1004, 0xb8201041u);
    cpu.write_register(0, 0x0F);
    cpu.write_register(1, 0);
    cpu.set_program_counter(0x1004);
    cpu.step(mem);
    TEST_ASSERT(mem.read_u32(0x8000) == 0xF0, "LDCLR cleared bits");
    TEST_ASSERT(cpu.read_register(1) == 0xFF, "LDCLR returned old value");

    // 3. LDEOR: memory has 0xAA, XOR 0x55 -> 0xFF
    mem.write_u32(0x8000, 0xAA);
    // ldeor w0, w1, [x2] -> 0xb8202041
    write_u32(mem, 0x1008, 0xb8202041u);
    cpu.write_register(0, 0x55);
    cpu.write_register(1, 0);
    cpu.set_program_counter(0x1008);
    cpu.step(mem);
    TEST_ASSERT(mem.read_u32(0x8000) == 0xFF, "LDEOR inverted bits");
    TEST_ASSERT(cpu.read_register(1) == 0xAA, "LDEOR returned old value");

    // 4. LDSET: memory has 0x10, SET 0x01 -> 0x11
    mem.write_u32(0x8000, 0x10);
    // ldset w0, w1, [x2] -> 0xb8203041
    write_u32(mem, 0x100C, 0xb8203041u);
    cpu.write_register(0, 0x01);
    cpu.write_register(1, 0);
    cpu.set_program_counter(0x100C);
    cpu.step(mem);
    TEST_ASSERT(mem.read_u32(0x8000) == 0x11, "LDSET set bits");
    TEST_ASSERT(cpu.read_register(1) == 0x10, "LDSET returned old value");
    return true;
}

int main() {
    std::cout << "[STAGE 196.3] Running Atomic Instruction Expansion Tests (Acceptance Test B)..." << std::endl;

    if (!test_cas_success_and_failure()) {
        std::cerr << "test_cas_success_and_failure FAILED" << std::endl;
        return 1;
    }
    if (!test_casa_casl_casal()) {
        std::cerr << "test_casa_casl_casal FAILED" << std::endl;
        return 1;
    }
    if (!test_cas_64bit()) {
        std::cerr << "test_cas_64bit FAILED" << std::endl;
        return 1;
    }
    if (!test_swp()) {
        std::cerr << "test_swp FAILED" << std::endl;
        return 1;
    }
    if (!test_ldadd_ldclr_ldeor_ldset()) {
        std::cerr << "test_ldadd_ldclr_ldeor_ldset FAILED" << std::endl;
        return 1;
    }

    std::cout << "[STAGE 196.3] All Atomic Instruction Tests PASSED (Acceptance Test B VERIFIED)." << std::endl;
    return 0;
}

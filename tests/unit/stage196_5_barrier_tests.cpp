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

// Helper to write instruction
static void write_u32(Memory& mem, std::uint64_t addr, std::uint32_t val) {
    mem.write_u32(addr, val);
}

// Opcode encodings:
// DMB ISH   -> 0xD5033BBF (op2=5, crm=11, rt=31)
// DMB ISHST -> 0xD5033ABF (op2=5, crm=10, rt=31)
// DMB ISHLD -> 0xD50339BF (op2=5, crm=9,  rt=31)
// DMB SY    -> 0xD5033FBF (op2=5, crm=15, rt=31)
// DSB ISH   -> 0xD5033B9F (op2=4, crm=11, rt=31)
// DSB SY    -> 0xD5033F9F (op2=4, crm=15, rt=31)
// ISB       -> 0xD5033FDF (op2=6, crm=15, rt=31)

static bool test_barrier_execution() {
    Memory mem(0x10000);
    CPU cpu;
    cpu.set_current_el(1);

    const std::vector<std::pair<std::uint32_t, const char*>> barriers = {
        {0xD5033BBFu, "DMB ISH"},
        {0xD5033ABFu, "DMB ISHST"},
        {0xD50339BFu, "DMB ISHLD"},
        {0xD5033FBFu, "DMB SY"},
        {0xD5033B9Fu, "DSB ISH"},
        {0xD5033F9Fu, "DSB SY"},
        {0xD5033FDFu, "ISB"}
    };

    std::uint64_t pc = 0x1000;
    for (const auto& [opcode, name] : barriers) {
        write_u32(mem, pc, opcode);
        cpu.set_program_counter(pc);
        cpu.step(mem);
        TEST_ASSERT(cpu.get_program_counter() == pc + 4, std::string(name) + " advanced PC by 4");
        pc += 4;
    }
    return true;
}

int main() {
    std::cout << "[STAGE 196.5] Running DMB, DSB & ISB Barrier Tests..." << std::endl;

    if (!test_barrier_execution()) {
        std::cerr << "test_barrier_execution FAILED" << std::endl;
        return 1;
    }

    std::cout << "[STAGE 196.5] All Barrier Tests PASSED." << std::endl;
    return 0;
}

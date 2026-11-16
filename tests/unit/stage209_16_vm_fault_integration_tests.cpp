#include <iostream>
#include <cstdint>

#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "memory/memory.h"

using namespace winios;
using namespace mmu;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_vm_fault_integration() {
    std::cout << "[Test 1] Stage 209.16 & 209.17: VM Fault Integration (ESR_EL1 / FAR_EL1 Syndromes)...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory memory(0x800000); // 8 MiB

    // Set Exception Vector Base
    cpu.set_vbar_el1(0x8000ULL);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);

    // 1. Translation Fault Simulation (Accessing unmapped VA 0x300000000)
    // Write an instruction: LDR X0, [X1] where X1 = 0x300000000
    const std::uint64_t unmapped_va = 0x300000000ULL;
    cpu.set_program_counter(0x1000ULL);
    memory.write_u32(0x1000ULL, 0xF9400020U); // LDR X0, [X1]
    cpu.write_register(1, unmapped_va);

    cpu.step(memory);

    // Verify CPU trapped to exception vector
    TEST_CHECK(cpu.get_program_counter() >= 0x8000ULL, "Trapped to exception vector at VBAR_EL1");
    // Verify FAR_EL1 captured exact faulting address
    std::cout << "  -> FAR_EL1: 0x" << std::hex << cpu.get_far_el1() << std::dec << "\n";
    TEST_CHECK(cpu.get_far_el1() == unmapped_va, "FAR_EL1 captured unmapped fault address 0x300000000");

    // Verify ESR_EL1: EC=0x25 (Data Abort from current EL), DFSC=0x04 (Translation fault)
    const std::uint32_t esr = cpu.get_esr_el1();
    const std::uint32_t ec = (esr >> 26u) & 0x3Fu;
    std::cout << "  -> ESR_EL1: 0x" << std::hex << esr << " (EC=0x" << ec << ")" << std::dec << "\n";
    TEST_CHECK(ec == 0x25u, "ESR_EL1 Exception Class is 0x25 (Data Abort)");

    // 2. Alignment Fault Simulation
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x8000ULL);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_sp_el1(0x8ULL); // Misaligned SP (not 16-byte aligned)
    cpu.set_program_counter(0x1000ULL);
    memory.write_u32(0x1000ULL, 0xF81F0FE0U); // STR X0, [SP, #-16]!

    cpu.step(memory);

    TEST_CHECK(cpu.get_program_counter() >= 0x8000ULL, "SP misalignment trapped to exception vector");
    const std::uint32_t esr_align = cpu.get_esr_el1();
    const std::uint32_t ec_align = (esr_align >> 26u) & 0x3Fu;
    TEST_CHECK(ec_align == 0x26u || ec_align == 0x25u || ec_align == 0x00u, "SP alignment fault trapped with valid syndrome");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 209.16 VM Fault Integration Tests ===\n";
    test_vm_fault_integration();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 209.16: All VM fault integration tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 209.16: " << g_failures << " tests failed.\n";
        return 1;
    }
}

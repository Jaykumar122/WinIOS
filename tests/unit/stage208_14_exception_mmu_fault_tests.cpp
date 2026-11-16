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

void test_mmu_fault_exception_integration() {
    std::cout << "[Test 1] Stage 208.14: MMU Fault & Exception System Integration...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x400000);

    // Set exception vector base address: VBAR_EL1 = 0x8000
    cpu.set_vbar_el1(0x8000ULL);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true); // EL1h

    // Set up MMU with page table at 0x30000
    cpu.get_mmu().set_mair_el1(0x00000000000004FFULL);
    cpu.get_mmu().set_tcr_el1((0b10ULL << 14u) | (28ULL));
    cpu.get_mmu().set_ttbr0_el1(0x30000ULL);

    // Entry 0: 32MB identity block, READ-ONLY (AP=2, 0x781)
    mem.write_u64(0x30000ULL, 0x0000000000000781ULL);

    // Enable MMU
    cpu.get_mmu().set_sctlr_el1(0x00C50839ULL);

    // Set PC to 0x1000
    cpu.set_program_counter(0x1000ULL);

    // Opcode at 0x1000: STR X0, [X1] (0xF9000020)
    mem.write_u32(0x1000ULL, 0xF9000020U);
    cpu.write_register(0, 0xDEADBEEFULL);
    cpu.write_register(1, 0x2000ULL); // Inside Entry 0 (which is Read-Only!)

    // Execute instruction: Must trigger Permission Fault Data Abort!
    cpu.step(mem);

    // 1. Check FAR_EL1 recorded the fault address 0x2000
    const std::uint64_t far = cpu.get_far_el1();
    std::cout << "  -> FAR_EL1: 0x" << std::hex << far << std::dec << "\n";
    TEST_CHECK(far == 0x2000ULL, "FAR_EL1 recorded exact fault virtual address (0x2000)");

    // 2. Check ESR_EL1 reported Data Abort (EC = 0x25 for current EL)
    const std::uint64_t esr = cpu.get_esr_el1();
    const std::uint32_t ec = static_cast<std::uint32_t>((esr >> 26u) & 0x3Fu);
    const std::uint32_t fsc = static_cast<std::uint32_t>(esr & 0x3Fu);
    std::cout << "  -> ESR_EL1: 0x" << std::hex << esr << " (EC=0x" << ec << ", FSC=0x" << fsc << ")" << std::dec << "\n";
    TEST_CHECK(ec == 0x25, "ESR_EL1 Exception Class is Data Abort taken from current EL (0x25)");
    TEST_CHECK((fsc & 0x3Cu) == 0x0C, "ESR_EL1 FSC is Permission Fault (0x0C - 0x0F)");

    // 3. Check PC routed to architectural exception vector at VBAR_EL1 + offset
    TEST_CHECK(cpu.get_program_counter() >= 0x8000ULL, "PC routed to VBAR_EL1 exception vector table");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 208.14 Exception & MMU Fault Integration Tests ===\n";
    test_mmu_fault_exception_integration();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 208.14: All exception MMU fault integration tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 208.14: " << g_failures << " tests failed.\n";
        return 1;
    }
}

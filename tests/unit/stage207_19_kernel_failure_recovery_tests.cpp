#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"

using namespace winios;
using namespace winios::cpu;
using namespace winios::loader;
using namespace winios::runtime;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_kernel_fault_diagnostic_telemetry() {
    std::cout << "[Test 1] Stage 207.19: Kernel Fault Diagnostic Telemetry...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);

    Memory memory(0x100000); // 1 MB DRAM

    // Configure VBAR_EL1
    cpu.set_vbar_el1(0x8000ULL);
    cpu.set_program_counter(0x1000ULL);

    // Place an unmapped memory access instruction at 0x1000:
    // LDR X0, [X1] where X1 points to unmapped address 0xDEADBEEF000ULL
    // 0xF9400020 (LDR X0, [X1])
    memory.write_u32(0x1000ULL, 0xF9400020U);
    cpu.write_register(1, 0xDEADBEEF000ULL);

    // Step CPU to trigger synchronous Data Abort
    cpu.step(memory);

    // Verify exception state was captured deterministically
    const std::uint64_t far = cpu.get_far_el1();
    const std::uint32_t esr = cpu.get_esr_el1();
    const std::uint64_t pc_after = cpu.get_program_counter();

    std::cout << "  -> Fault FAR_EL1: 0x" << std::hex << far << "\n";
    std::cout << "  -> Fault ESR_EL1: 0x" << esr << "\n";
    std::cout << "  -> Exception Handler PC: 0x" << pc_after << std::dec << "\n";

    TEST_CHECK(far == 0xDEADBEEF000ULL, "FAR_EL1 captures exact faulting address");
    TEST_CHECK((esr >> 26) == 0x25, "ESR_EL1.EC is 0x25 (Data Abort from same EL)");
    TEST_CHECK(pc_after >= 0x8000ULL, "PC vectors into VBAR_EL1 exception table");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 207.19 Kernel Failure and Recovery Tests ===\n";
    test_kernel_fault_diagnostic_telemetry();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 207.19: All kernel failure recovery tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 207.19: " << g_failures << " tests failed.\n";
        return 1;
    }
}

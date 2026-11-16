#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/independent_kernel_fixture.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"

using namespace winios;
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

void test_independent_fixture_execution() {
    std::cout << "[Test 1] Independent ARM64 Kernel Entry & Exception Cycle..." << std::endl;

    CPU cpu;
    cpu.set_architectural_exceptions(true);

    Memory mem(0x200000); // 2 MB RAM

    IndependentKernelFixtureConfig fcfg;
    fcfg.text_vmaddr = 0x00000000ULL;
    fcfg.entry_vaddr = 0x00001000ULL;
    fcfg.el0_code_vaddr = 0x00002000ULL;
    fcfg.vbar_vaddr = 0x00002800ULL; // aligned to 2048 (0x2800 % 2048 == 0)
    fcfg.status_target_addr = 0x00003800ULL;
    fcfg.status_magic = 0xCAFEFEEDC001CAFEULL;

    auto fixture_bytes = IndependentKernelFixtureBuilder::build_fixture(fcfg);
    TEST_CHECK(!fixture_bytes.empty(), "Fixture binary generated");

    KernelBootConfig bcfg;
    bcfg.raw_kernel_image = fixture_bytes;
    bcfg.dram_base = 0x0ULL;
    bcfg.dram_size = 0x200000ULL;
    bcfg.num_cores = 1;
    bcfg.vbar_el1 = fcfg.vbar_vaddr;

    auto bres = KernelBootCoordinator::prepare_and_boot(cpu, mem, bcfg);
    TEST_CHECK(bres.success, "Handoff to CPU must succeed: " + bres.error_message);
    TEST_CHECK(cpu.get_current_el() == 1, "Initial EL is EL1");
    TEST_CHECK(cpu.get_program_counter() == fcfg.entry_vaddr, "Initial PC is entry_vaddr");

    // Execute steps:
    // 1. EL1 reads boot_args (physBase, memSize) and SP
    // 2. EL1 programs VBAR_EL1, ELR_EL1, SPSR_EL1
    // 3. EL1 executes ERET -> drops to EL0 at el0_code_vaddr (0x2000)
    // 4. EL0 executes MOV and SVC #0 -> vectors to VBAR_EL1 + 0x400 (0x2C00) at EL1
    // 5. EL1 handler executes at 0x2C00, writes status_magic to status_target_addr, and hits WFI
    std::size_t steps = 0;
    constexpr std::size_t kMaxSteps = 50;

    bool observed_el0 = false;
    bool returned_to_el1 = false;

    while (steps < kMaxSteps) {
        if (cpu.get_current_el() == 0) {
            observed_el0 = true;
        } else if (observed_el0 && cpu.get_current_el() == 1) {
            returned_to_el1 = true;
        }

        cpu.step(mem);
        ++steps;

        // Check if status magic has been written to status_target_addr
        const std::uint64_t written_magic = mem.read_u64(static_cast<std::size_t>(fcfg.status_target_addr));
        if (written_magic == fcfg.status_magic) {
            break;
        }
    }

    std::cout << "  Executed " << steps << " instructions.\n";

    TEST_CHECK(observed_el0, "CPU transitioned to EL0 via ERET");
    TEST_CHECK(returned_to_el1, "CPU transitioned back to EL1 via SVC exception");

    const std::uint64_t final_magic = mem.read_u64(static_cast<std::size_t>(fcfg.status_target_addr));
    TEST_CHECK(final_magic == fcfg.status_magic, "Completion magic correctly written to target memory");
    TEST_CHECK(cpu.get_vbar_el1() == fcfg.vbar_vaddr, "VBAR_EL1 is set to fixture vector table");

    std::cout << "  -> Independent fixture execution verified." << std::endl;
}

} // namespace

int main() {
    std::cout << "=== Stage 205.14 Independent Kernel Fixture Tests ===" << std::endl;

    test_independent_fixture_execution();

    if (g_failures > 0) {
        std::cerr << "FAILED: " << g_failures << " tests failed.\n";
        return 1;
    }

    std::cout << "All Stage 205.14 tests PASSED." << std::endl;
    return 0;
}

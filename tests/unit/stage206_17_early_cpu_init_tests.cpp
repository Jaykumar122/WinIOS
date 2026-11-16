#include <cstdint>
#include <iostream>
#include <string>

#include "cpu/arm64_cpu_boot_state.h"
#include "cpu/cpu.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::cpu;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_early_cpu_initialization() {
    std::cout << "[Test 1] Early CPU Architectural State & Registers Initialization...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);

    CpuBootConfig cfg{};
    cfg.initial_pc = 0x1000ULL;
    cfg.initial_sp = 0x80000ULL;
    cfg.boot_args_phys = 0x20000ULL;
    cfg.exception_level = KernelBootExceptionLevel::EL1;
    cfg.vbar_el1 = 0x4000ULL;
    cfg.sctlr_el1 = 0x30D00800ULL; // Standard XNU SCTLR_EL1
    cfg.tcr_el1 = (1ULL << 30) | (2ULL << 14) | (25ULL << 16) | 25ULL; // 16KB Granule
    cfg.ttbr0_el1 = 0x20000ULL;
    cfg.ttbr1_el1 = 0x24000ULL;
    cfg.mair_el1 = 0x0000000444FF0400ULL;
    cfg.spsr_el1 = 0x3C5; // EL1h, DAIF masked
    cfg.set_x19_boot_args = true;

    std::string err;
    bool init_ok = Arm64CpuBootInitializer::initialize_cpu(cpu, cfg, &err);
    TEST_CHECK(init_ok, "Arm64CpuBootInitializer must succeed: " + err);

    // 1. Verify Privilege Level & Stack Pointer Selection
    TEST_CHECK(cpu.get_current_el() == 1, "CPU must be at EL1");
    TEST_CHECK(cpu.get_sp_sel() == true, "CPU must be using SP_EL1 (EL1h mode)");
    TEST_CHECK(cpu.get_program_counter() == cfg.initial_pc, "PC matches initial_pc");
    TEST_CHECK(cpu.get_stack_pointer() == cfg.initial_sp, "SP matches initial_sp");

    // 2. Verify Register Arguments
    TEST_CHECK(cpu.read_register(0) == cfg.boot_args_phys, "X0 points to boot_args");
    TEST_CHECK(cpu.read_register(19) == cfg.boot_args_phys, "X19 points to boot_args");

    // 3. Verify System Registers
    TEST_CHECK(cpu.get_vbar_el1() == cfg.vbar_el1, "VBAR_EL1 matches");
    TEST_CHECK(cpu.get_tcr_el1() == cfg.tcr_el1, "TCR_EL1 matches 16KB granule");
    TEST_CHECK(cpu.get_ttbr0_el1() == cfg.ttbr0_el1, "TTBR0_EL1 matches");
    TEST_CHECK(cpu.get_ttbr1_el1() == cfg.ttbr1_el1, "TTBR1_EL1 matches");

    std::cout << "  -> Early CPU initialization verified successfully.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 206.17 Early CPU Initialization Tests ===\n";
    test_early_cpu_initialization();

    if (g_failures == 0) {
        std::cout << "=== All Stage 206.17 Tests PASSED ===\n";
        return 0;
    } else {
        std::cerr << "=== " << g_failures << " Stage 206.17 Tests FAILED ===\n";
        return 1;
    }
}

#include <cstdint>
#include <iostream>
#include <string>

#include "cpu/arm64_cpu_boot_state.h"
#include "cpu/cpu.h"

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

void test_valid_el1_boot_state() {
    std::cout << "[Test 1] Valid EL1 Boot State Initialization...\n";
    CPU cpu;

    CpuBootConfig cfg;
    cfg.initial_pc = 0xFFFFFE0007004000ULL;
    cfg.initial_sp = 0xFFFF800020010000ULL;
    cfg.boot_args_phys = 0x804000000ULL;
    cfg.exception_level = KernelBootExceptionLevel::EL1;
    cfg.vbar_el1 = 0xFFFFFE0007002000ULL; // 2KB aligned (0x2000 % 2048 == 0)
    cfg.sctlr_el1 = 0x30D00800ULL;
    cfg.tcr_el1 = 0x2B5593519ULL; // 16KB granule, 48-bit VA
    cfg.ttbr0_el1 = 0x804010000ULL;
    cfg.ttbr1_el1 = 0x804020000ULL;
    cfg.mair_el1 = 0x0000000444FF0400ULL;
    cfg.spsr_el1 = 0x3C5;
    cfg.set_x19_boot_args = true;

    std::string err;
    bool ok = Arm64CpuBootInitializer::initialize_cpu(cpu, cfg, &err);
    TEST_CHECK(ok, "CPU initialization must succeed: " + err);

    // Verify CPU registers
    TEST_CHECK(cpu.get_current_el() == 1, "Current EL must be 1 (EL1)");
    TEST_CHECK(cpu.get_sp_sel() == true, "SPSel must be true (EL1h)");
    TEST_CHECK(cpu.get_sp_el1() == cfg.initial_sp, "SP_EL1 must match initial SP");
    TEST_CHECK(cpu.get_stack_pointer() == cfg.initial_sp, "Active SP must match initial SP");
    TEST_CHECK(cpu.get_program_counter() == cfg.initial_pc, "PC must match initial PC");
    TEST_CHECK(cpu.read_register(0) == cfg.boot_args_phys, "X0 must hold boot_args pointer");
    TEST_CHECK(cpu.read_register(19) == cfg.boot_args_phys, "X19 must hold boot_args pointer");
    TEST_CHECK(cpu.get_vbar_el1() == cfg.vbar_el1, "VBAR_EL1 must match config");
    TEST_CHECK(cpu.get_sctlr_el1() == cfg.sctlr_el1, "SCTLR_EL1 must match config");
    TEST_CHECK(cpu.get_tcr_el1() == cfg.tcr_el1, "TCR_EL1 must match config");
    TEST_CHECK(cpu.get_ttbr0_el1() == cfg.ttbr0_el1, "TTBR0_EL1 must match config");
    TEST_CHECK(cpu.get_ttbr1_el1() == cfg.ttbr1_el1, "TTBR1_EL1 must match config");
    TEST_CHECK(cpu.get_mair_el1() == cfg.mair_el1, "MAIR_EL1 must match config");
    TEST_CHECK(cpu.get_spsr_el1() == cfg.spsr_el1, "SPSR_EL1 must match config");

    std::string json = Arm64CpuBootInitializer::export_boot_state_json(cfg);
    TEST_CHECK(json.find("\"exception_level\": \"EL1\"") != std::string::npos, "JSON must contain EL1");
    TEST_CHECK(json.find("initial_pc") != std::string::npos, "JSON must contain initial_pc");

    std::cout << "  -> Valid EL1 boot state passed.\n";
}

void test_alignment_validation() {
    std::cout << "[Test 2] Alignment Validation...\n";
    CpuBootConfig bad_sp;
    bad_sp.initial_pc = 0x1000;
    bad_sp.initial_sp = 0x2001; // Not 16-byte aligned!
    std::string err;
    TEST_CHECK(!Arm64CpuBootInitializer::validate_config(bad_sp, &err), "Must reject misaligned SP");
    TEST_CHECK(err.find("16-byte aligned") != std::string::npos, "Error must mention 16-byte aligned SP");

    CpuBootConfig bad_pc;
    bad_pc.initial_pc = 0x1002; // Not 4-byte aligned!
    bad_pc.initial_sp = 0x2000;
    TEST_CHECK(!Arm64CpuBootInitializer::validate_config(bad_pc, &err), "Must reject misaligned PC");
    TEST_CHECK(err.find("4-byte aligned") != std::string::npos, "Error must mention 4-byte aligned PC");

    CpuBootConfig bad_vbar;
    bad_vbar.initial_pc = 0x1000;
    bad_vbar.initial_sp = 0x2000;
    bad_vbar.vbar_el1 = 0x3000; // 0x3000 % 2048 == 2048 - not 2KB aligned? Wait, 0x3000 = 12288, 12288 % 2048 == 0!
    bad_vbar.vbar_el1 = 0x3100; // 0x3100 = 12544 % 2048 = 256 != 0
    TEST_CHECK(!Arm64CpuBootInitializer::validate_config(bad_vbar, &err), "Must reject misaligned VBAR");
    TEST_CHECK(err.find("2048-byte aligned") != std::string::npos, "Error must mention 2048-byte aligned VBAR");

    std::cout << "  -> Alignment validation passed.\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Running Stage 205.8 ARM64 CPU Boot State Tests ===\n";
    test_valid_el1_boot_state();
    test_alignment_validation();

    if (g_failures == 0) {
        std::cout << "ALL STAGE 205.8 ARM64 CPU BOOT STATE TESTS PASSED!\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED!\n";
        return 1;
    }
}

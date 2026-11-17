#include <iostream>
#include <vector>
#include <cstdint>

#include "cpu/cpu.h"
#include "kernel/arm_trap_frame.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::kernel;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_trap_frame_layout_and_size() {
    std::cout << "[Test 1] Stage 211.2: ArmTrapFrame Binary Layout & Alignment...\n";

    TEST_CHECK(sizeof(ArmTrapFrame) == 304, "ArmTrapFrame is exactly 304 bytes");
    TEST_CHECK((sizeof(ArmTrapFrame) % 16) == 0, "ArmTrapFrame is 16-byte aligned");

    ArmTrapFrame frame{};
    TEST_CHECK(frame.magic == kTrapFrameMagic, "Magic identifies TRAPFRME");
}

void test_adversarial_register_preservation() {
    std::cout << "[Test 2] Stage 211.2: Adversarial Full-GPR & Special Register Preservation...\n";

    CPU cpu;

    // Fill all 31 general-purpose registers with distinct bit patterns
    for (int i = 0; i < 31; ++i) {
        const std::uint64_t val = (0xCAFE000000000000ULL) | (static_cast<std::uint64_t>(i) << 32u) | (i * 0x1010101u);
        cpu.write_register(i, val);
    }

    cpu.set_sp_el0(0x000000007FFFF000ULL);
    cpu.set_elr_el1(0x0000000000010040ULL);
    cpu.set_spsr_el1(0x0000000000000000ULL); // EL0t
    cpu.set_esr_el1(0x56000000u);            // EC 0x15 (SVC)
    cpu.set_far_el1(0x0000000000002000ULL);

    // Capture state
    ArmTrapFrame frame{};
    TrapFrameManager::capture_from_cpu(cpu, frame);

    // Scramble CPU registers with hostile noise
    for (int i = 0; i < 31; ++i) {
        cpu.write_register(i, 0xDEADBEEFDEADBEEFULL);
    }
    cpu.set_sp_el0(0xBAD0BAD0BAD0BAD0ULL);
    cpu.set_elr_el1(0xBAD0BAD0BAD0BAD0ULL);
    cpu.set_spsr_el1(0xBAD0BAD0u);
    cpu.set_esr_el1(0xBAD0BAD0u);
    cpu.set_far_el1(0xBAD0BAD0BAD0BAD0ULL);

    // Restore state
    TrapFrameManager::restore_to_cpu(cpu, frame);

    // Verify all 31 registers are bit-exact
    for (int i = 0; i < 31; ++i) {
        const std::uint64_t expected = (0xCAFE000000000000ULL) | (static_cast<std::uint64_t>(i) << 32u) | (i * 0x1010101u);
        TEST_CHECK(cpu.read_register(i) == expected, "GPR X" + std::to_string(i) + " preserved exactly");
    }

    TEST_CHECK(cpu.get_sp_el0() == 0x000000007FFFF000ULL, "SP_EL0 preserved exactly");
    TEST_CHECK(cpu.get_elr_el1() == 0x0000000000010040ULL, "ELR_EL1 preserved exactly");
    TEST_CHECK(cpu.get_spsr_el1() == 0x0000000000000000ULL, "SPSR_EL1 preserved exactly");
    TEST_CHECK(cpu.get_esr_el1() == 0x56000000u, "ESR_EL1 preserved exactly");
    TEST_CHECK(cpu.get_far_el1() == 0x0000000000002000ULL, "FAR_EL1 preserved exactly");
}

void test_memory_serialization_and_routine_emission() {
    std::cout << "[Test 3] Stage 211.2: Guest Memory Serialization & Machine Code Routines...\n";

    Memory mem(0x10000);
    ArmTrapFrame orig{};
    orig.magic = kTrapFrameMagic;
    orig.gpr[0] = 0x1234567890ABCDEFULL;
    orig.sp_el0 = 0x70000000ULL;
    orig.elr_el1 = 0x1000ULL;

    const std::uint64_t tf_addr = 0x2000ULL;
    TEST_CHECK(TrapFrameManager::write_to_memory(mem, tf_addr, orig), "Write trap frame to memory");

    ArmTrapFrame loaded{};
    TEST_CHECK(TrapFrameManager::read_from_memory(mem, tf_addr, loaded), "Read trap frame from memory");
    TEST_CHECK(loaded.magic == kTrapFrameMagic, "Loaded magic matches");
    TEST_CHECK(loaded.gpr[0] == 0x1234567890ABCDEFULL, "Loaded GPR X0 matches");
    TEST_CHECK(loaded.sp_el0 == 0x70000000ULL, "Loaded SP_EL0 matches");
    TEST_CHECK(loaded.elr_el1 == 0x1000ULL, "Loaded ELR_EL1 matches");

    // Machine code routines
    const auto entry_insns = TrapFrameManager::emit_el0_trap_entry();
    TEST_CHECK(!entry_insns.empty(), "Entry routine emitted");
    TEST_CHECK(entry_insns.back() == 0xD65F03C0u, "Entry routine ends with RET");

    const auto exit_insns = TrapFrameManager::emit_el0_trap_exit();
    TEST_CHECK(!exit_insns.empty(), "Exit routine emitted");
    TEST_CHECK(exit_insns.back() == 0xD69F03E0u, "Exit routine ends with ERET");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 211.2 Context Preservation Tests ===\n";
    test_trap_frame_layout_and_size();
    test_adversarial_register_preservation();
    test_memory_serialization_and_routine_emission();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 211.2: All context preservation tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 211.2: " << g_failures << " tests failed.\n";
        return 1;
    }
}

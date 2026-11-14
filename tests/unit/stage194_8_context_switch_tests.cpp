// ============================================================================
// Stage 194.8: Guest Context Switching Tests
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/context_switch.h"
#include "kernel/thread.h"
#include "memory/memory.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::kernel;

static bool test_switch_routine_machine_code_validity() {
    const auto routine = ContextSwitch::emit_guest_switch_routine();
    CHECK(routine.size() == 22, "Routine must consist of exactly 22 ARM64 instructions (88 bytes)");

    // Check first instruction: cbz x0, +40 bytes (0xB4000140u)
    CHECK(routine[0] == 0xB4000140u, "First instruction must be cbz x0, +40");
    // Check final instruction: br x16 (0xD61F0200u)
    CHECK(routine[21] == 0xD61F0200u, "Final instruction must be br x16");

    std::cout << "[PASS] test_switch_routine_machine_code_validity" << std::endl;
    return true;
}

static bool test_host_thread_switch() {
    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);

    KernelThread t1(1, "thread_1", 0x1000, 0, 0x50000, 0x4000, 16);
    KernelThread t2(2, "thread_2", 0x2000, 0, 0x60000, 0x4000, 20);

    // Initial launch of T1
    CHECK(ContextSwitch::switch_threads(nullptr, &t1, cpu), "Initial switch to T1 must succeed");
    CHECK(t1.state() == ThreadState::Running, "T1 is Running");
    CHECK(cpu.get_program_counter() == 0x1000, "PC is 0x1000");

    // Populate CPU state while running T1
    for (std::uint32_t i = 0; i < 31; ++i) {
        cpu.write_register(i, 0x11110000ULL + i);
    }
    cpu.set_program_counter(0x1040); // T1 progressed to 0x1040

    // Switch from T1 to T2
    CHECK(ContextSwitch::switch_threads(&t1, &t2, cpu), "Switch from T1 to T2 must succeed");
    CHECK(t1.state() == ThreadState::Ready, "T1 returned to Ready");
    CHECK(t2.state() == ThreadState::Running, "T2 is now Running");
    CHECK(cpu.get_program_counter() == 0x2000, "CPU is now at T2 entry point (0x2000)");

    // T1's saved context must have its progressed state
    CHECK(t1.tcb().pc == 0x1040, "T1 saved PC must be 0x1040");
    CHECK(t1.tcb().gpr[19] == 0x11110000ULL + 19, "T1 saved X19 must match");

    // Populate CPU state while running T2
    for (std::uint32_t i = 0; i < 31; ++i) {
        cpu.write_register(i, 0x22220000ULL + i);
    }
    cpu.set_program_counter(0x2080); // T2 progressed to 0x2080

    // Switch back from T2 to T1
    CHECK(ContextSwitch::switch_threads(&t2, &t1, cpu), "Switch back from T2 to T1");
    CHECK(t2.state() == ThreadState::Ready, "T2 is Ready");
    CHECK(t1.state() == ThreadState::Running, "T1 is Running");
    CHECK(cpu.get_program_counter() == 0x1040, "CPU resumed T1 at 0x1040");
    CHECK(cpu.read_register(19) == 0x11110000ULL + 19, "CPU restored T1's X19");
    CHECK(cpu.read_register(29) == 0x11110000ULL + 29, "CPU restored T1's FP");

    std::cout << "[PASS] test_host_thread_switch" << std::endl;
    return true;
}

static bool test_guest_assembly_context_switch_round_trip() {
    Memory mem(4 * 1024 * 1024); // 4MB RAM

    const std::uint64_t routine_addr = 0x2000ULL;
    const std::uint64_t t1_addr = 0x40000ULL;
    const std::uint64_t t2_addr = 0x41000ULL;
    const std::uint64_t t2_target_pc = 0x80003000ULL;
    const std::uint64_t t1_return_pc = 0x80001044ULL;

    // 1. Write switch routine into guest memory
    const auto routine = ContextSwitch::emit_guest_switch_routine();
    for (std::size_t i = 0; i < routine.size(); ++i) {
        mem.write_u32(static_cast<std::size_t>(routine_addr + i * 4), routine[i]);
    }

    // 2. Initialize T1 TCB in guest RAM
    KernelThread t1(1, "t1_worker", 0x80001000ULL, 0, 0x70000ULL, 0x4000ULL);
    t1.set_state(ThreadState::Running);
    CHECK(t1.write_to_guest_memory(mem, t1_addr), "Write T1 TCB");

    // 3. Initialize T2 TCB in guest RAM
    KernelThread t2(2, "t2_worker", t2_target_pc, 0, 0x80000ULL, 0x4000ULL);
    // Preset T2's callee-saved registers in TCB
    for (std::uint32_t i = 19; i <= 30; ++i) {
        t2.tcb().gpr[i] = 0x22220000ULL + i;
    }
    CHECK(t2.write_to_guest_memory(mem, t2_addr), "Write T2 TCB");

    // 4. Configure CPU simulating active execution of T1
    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_sp_el1(0x74000ULL); // T1 stack pointer
    for (std::uint32_t i = 19; i <= 30; ++i) {
        cpu.write_register(i, 0x11110000ULL + i); // T1 callee-saved registers
    }

    // 5. Execute guest switch routine
    const bool ok = ContextSwitch::execute_guest_switch(
        mem, cpu, routine_addr, t1_addr, t2_addr, t1_return_pc, 50);

    CHECK(ok, "execute_guest_switch must succeed and reach target PC");
    CHECK(cpu.get_program_counter() == t2_target_pc, "CPU must be at T2 target PC");
    CHECK(cpu.get_stack_pointer() == 0x84000ULL, "CPU SP must be restored to T2 SP (0x84000)");

    // Check CPU restored T2 registers
    for (std::uint32_t i = 19; i <= 30; ++i) {
        CHECK(cpu.read_register(i) == 0x22220000ULL + i, "CPU must hold T2's register value");
    }

    // Check T1 state saved in RAM
    CHECK(mem.read_u64(static_cast<std::size_t>(t1_addr + 0x28)) == 0x74000ULL, "T1 SP saved to RAM");
    CHECK(mem.read_u64(static_cast<std::size_t>(t1_addr + 0x30)) == t1_return_pc, "T1 return PC saved to RAM");
    for (std::uint32_t i = 19; i <= 29; ++i) {
        const std::uint64_t reg_val = mem.read_u64(static_cast<std::size_t>(t1_addr + 0x40 + i * 8));
        CHECK(reg_val == 0x11110000ULL + i, "T1 callee-saved register must be saved to RAM");
    }
    const std::uint64_t lr_val = mem.read_u64(static_cast<std::size_t>(t1_addr + 0x40 + 30 * 8));
    CHECK(lr_val == t1_return_pc, "T1 X30 (LR) must hold caller return address");

    // Check T2 state updated to Running in RAM
    const std::uint32_t t2_state = mem.read_u32(static_cast<std::size_t>(t2_addr + 0x10));
    CHECK(t2_state == static_cast<std::uint32_t>(ThreadState::Running), "T2 state in RAM must be Running (1)");

    std::cout << "[PASS] test_guest_assembly_context_switch_round_trip" << std::endl;
    return true;
}

static bool test_guest_bootstrap_initial_thread_switch() {
    Memory mem(4 * 1024 * 1024);

    const std::uint64_t routine_addr = 0x2000ULL;
    const std::uint64_t t2_addr = 0x41000ULL;
    const std::uint64_t t2_target_pc = 0x80005000ULL;

    const auto routine = ContextSwitch::emit_guest_switch_routine();
    for (std::size_t i = 0; i < routine.size(); ++i) {
        mem.write_u32(static_cast<std::size_t>(routine_addr + i * 4), routine[i]);
    }

    KernelThread t2(2, "init_thread", t2_target_pc, 0, 0x80000ULL, 0x4000ULL);
    CHECK(t2.write_to_guest_memory(mem, t2_addr), "Write T2");

    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);

    // Initial switch with old_tcb = 0 (null)
    const bool ok = ContextSwitch::execute_guest_switch(
        mem, cpu, routine_addr, 0, t2_addr, 0, 50);

    CHECK(ok, "execute_guest_switch with null old_tcb must succeed without fault");
    CHECK(cpu.get_program_counter() == t2_target_pc, "CPU reached init thread PC");
    CHECK(cpu.get_stack_pointer() == 0x84000ULL, "CPU SP is init thread stack");

    std::cout << "[PASS] test_guest_bootstrap_initial_thread_switch" << std::endl;
    return true;
}

static bool test_compiled_macho_binary_artifact() {
    const std::string bin_path = "D:/WinISO26/WinIOS/tests/artifacts/bin/guest_context_switch";
    std::ifstream file(bin_path, std::ios::binary | std::ios::ate);
    CHECK(file.is_open(), "Compiled guest_context_switch binary artifact must exist");

    const auto size = file.tellg();
    CHECK(size == 50088, "Binary artifact size must be exactly 50088 bytes");

    std::cout << "[PASS] test_compiled_macho_binary_artifact" << std::endl;
    return true;
}

int main() {
    std::cout << "Running Stage 194.8 Guest Context Switching Tests..." << std::endl;

    if (!test_switch_routine_machine_code_validity()) return 1;
    if (!test_host_thread_switch()) return 1;
    if (!test_guest_assembly_context_switch_round_trip()) return 1;
    if (!test_guest_bootstrap_initial_thread_switch()) return 1;
    if (!test_compiled_macho_binary_artifact()) return 1;

    std::cout << "\nAll Stage 194.8 Guest Context Switching tests PASSED successfully." << std::endl;
    return 0;
}

#include <iostream>
#include <vector>
#include <cstdint>

#include "cpu/cpu.h"
#include "kernel/context_switch.h"
#include "kernel/thread.h"
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

void test_guest_context_switch_routine_emission() {
    std::cout << "[Test 1] Stage 210.15: Authentic ARM64 Context-Switch Machine Code Routine...\n";

    const auto insns = ContextSwitch::emit_guest_switch_routine();
    TEST_CHECK(!insns.empty(), "Context switch routine emitted");
    TEST_CHECK(insns.size() == 22, "Routine consists of exactly 22 ARM64 instructions");

    // Last instruction must be BR X16 (0xD61F0200)
    TEST_CHECK(insns.back() == 0xD61F0200u, "Last instruction is BR X16 (0xD61F0200)");
}

void test_guest_context_switch_execution() {
    std::cout << "[Test 2] Stage 210.15: Execution of Context-Switch on Virtual CPU...\n";

    CPU cpu;
    Memory mem(0x100000);
    KernelThreadManager mgr;

    auto* t1 = mgr.create_thread("thread_old", 0x3000ULL, 0x111ULL, 0x10000ULL, 0x4000ULL, 16);
    auto* t2 = mgr.create_thread("thread_new", 0x5000ULL, 0x222ULL, 0x14000ULL, 0x4000ULL, 16);

    const std::uint64_t routine_addr = 0x1000ULL;
    const std::uint64_t tcb_old_addr = 0x2000ULL;
    const std::uint64_t tcb_new_addr = 0x2400ULL;

    // Write context switch instructions into guest memory
    const auto routine = ContextSwitch::emit_guest_switch_routine();
    for (std::size_t i = 0; i < routine.size(); ++i) {
        mem.write_u32(static_cast<std::size_t>(routine_addr + (i * 4)), routine[i]);
    }

    // Set distinctive register values on CPU representing outgoing thread
    cpu.write_register(19, 0xAAAA1919ULL);
    cpu.write_register(20, 0xAAAA2020ULL);
    cpu.set_sp_el1(0x13FF0ULL);

    // Set incoming thread values in t2's TCB
    t2->tcb().gpr[19] = 0xBBBB1919ULL;
    t2->tcb().gpr[20] = 0xBBBB2020ULL;
    t2->tcb().sp_el1 = 0x17FF0ULL;
    t2->tcb().pc = 0x5000ULL;

    t1->write_to_guest_memory(mem, tcb_old_addr);
    t2->write_to_guest_memory(mem, tcb_new_addr);

    // Execute context switch routine on virtual CPU
    const bool success = ContextSwitch::execute_guest_switch(
        mem, cpu, routine_addr, tcb_old_addr, tcb_new_addr, 0x3040ULL, 50);

    TEST_CHECK(success, "Guest context switch executed cleanly");
    TEST_CHECK(cpu.read_register(19) == 0xBBBB1919ULL, "X19 restored from incoming thread");
    TEST_CHECK(cpu.read_register(20) == 0xBBBB2020ULL, "X20 restored from incoming thread");
    TEST_CHECK(cpu.get_sp_el1() == 0x17FF0ULL, "SP_EL1 switched to incoming thread stack");
    TEST_CHECK(cpu.get_program_counter() == 0x5000ULL, "PC switched to incoming thread entry");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 210.15 Context Switch Tests ===\n";
    test_guest_context_switch_routine_emission();
    test_guest_context_switch_execution();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 210.15: All context switch tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 210.15: " << g_failures << " tests failed.\n";
        return 1;
    }
}

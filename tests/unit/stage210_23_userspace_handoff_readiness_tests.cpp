#include <iostream>
#include <vector>
#include <cstdint>

#include "cpu/cpu.h"
#include "kernel/thread.h"
#include "memory/memory.h"
#include "memory/virtual_page_table.h"

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_el0_thread_context_and_spsr() {
    std::cout << "[Test 1] Stage 210.23: EL0 User Thread Context & SPSR_EL1 Mode Verification...\n";

    KernelThread user_thread(50, "minimal_el0_app", 0x10000ULL, 0x1234ULL, 0x20000ULL, 0x4000ULL, 16, true);

    TEST_CHECK(user_thread.tid() == 50, "User thread TID is 50");
    TEST_CHECK(user_thread.tcb().sp_el0 == 0x24000ULL, "User stack pointer SP_EL0 initialized to stack top");
    // Mode bits [3:0] in SPSR_EL1: 0b0000 = EL0t
    TEST_CHECK((user_thread.tcb().spsr_el1 & 0x0F) == 0x00, "SPSR_EL1 target mode is EL0t (0x0)");
}

void test_el0_execution_and_page_protection() {
    std::cout << "[Test 2] Stage 210.23: Minimal EL0 Instruction Execution & Privilege Boundary...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x100000);

    // Setup minimal EL0 code at 0x10000:
    // 1. mov x0, #42       (0xD2800540)
    // 2. add x0, x0, #8    (0x91002000)
    // 3. wfi               (0xD503207F)
    mem.write_u32(0x10000ULL, 0xD2800540u); // mov x0, #42
    mem.write_u32(0x10004ULL, 0x91002000u); // add x0, x0, #8
    mem.write_u32(0x10008ULL, 0xD503207Fu); // wfi

    cpu.set_program_counter(0x10000ULL);
    cpu.set_sp_el0(0x20000ULL);

    // Execute 3 instructions
    cpu.step(mem);
    TEST_CHECK(cpu.read_register(0) == 42, "Instruction 1: X0 = 42");

    cpu.step(mem);
    TEST_CHECK(cpu.read_register(0) == 50, "Instruction 2: X0 = 50");

    cpu.step(mem);
    TEST_CHECK(cpu.is_waiting_for_interrupt(), "Instruction 3: WFI reached cleanly");

    // Privilege boundary test: Verify PagePermissions for user vs kernel
    const auto u_desc = VirtualPageTable::make_page_descriptor(0x10000ULL, PagePermissions::UserExecute);
    const auto k_desc = VirtualPageTable::make_page_descriptor(0x10000ULL, PagePermissions::KernelExecute);

    const auto u_fields = VirtualPageTable::parse_descriptor(u_desc, 3);
    const auto k_fields = VirtualPageTable::parse_descriptor(k_desc, 3);

    TEST_CHECK(u_fields.el0_accessible, "UserExecute page is EL0 accessible");
    TEST_CHECK(u_fields.user_execute, "UserExecute page allows user execution");

    TEST_CHECK(!k_fields.el0_accessible, "KernelExecute page is forbidden to EL0");
    TEST_CHECK(k_fields.privileged_execute, "KernelExecute allows EL1 execution only");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 210.23 Userspace Handoff Readiness Tests ===\n";
    test_el0_thread_context_and_spsr();
    test_el0_execution_and_page_protection();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 210.23: All userspace handoff readiness tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 210.23: " << g_failures << " tests failed.\n";
        return 1;
    }
}

#include <iostream>
#include <vector>
#include <cstdint>

#include "cpu/cpu.h"
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

void test_guest_tcb_structure_layout() {
    std::cout << "[Test 1] Stage 210.12: GuestTcb Structure Offsets & 512-byte Packing...\n";

    TEST_CHECK(sizeof(GuestTcb) == 512, "sizeof(GuestTcb) must be exactly 512 bytes");

    GuestTcb tcb{};
    const auto* base = reinterpret_cast<const std::uint8_t*>(&tcb);

    auto offset_of = [base](const void* member) -> std::size_t {
        return static_cast<std::size_t>(reinterpret_cast<const std::uint8_t*>(member) - base);
    };

    TEST_CHECK(offset_of(&tcb.magic) == 0x00, "magic offset is 0x00");
    TEST_CHECK(offset_of(&tcb.tid) == 0x08, "tid offset is 0x08");
    TEST_CHECK(offset_of(&tcb.state) == 0x10, "state offset is 0x10");
    TEST_CHECK(offset_of(&tcb.priority) == 0x14, "priority offset is 0x14");
    TEST_CHECK(offset_of(&tcb.time_slice) == 0x18, "time_slice offset is 0x18");
    TEST_CHECK(offset_of(&tcb.wait_reason) == 0x1C, "wait_reason offset is 0x1C");
    TEST_CHECK(offset_of(&tcb.sp_el0) == 0x20, "sp_el0 offset is 0x20");
    TEST_CHECK(offset_of(&tcb.sp_el1) == 0x28, "sp_el1 offset is 0x28");
    TEST_CHECK(offset_of(&tcb.pc) == 0x30, "pc offset is 0x30");
    TEST_CHECK(offset_of(&tcb.spsr_el1) == 0x38, "spsr_el1 offset is 0x38");
    TEST_CHECK(offset_of(&tcb.gpr[0]) == 0x40, "gpr[0] offset is 0x40");
    TEST_CHECK(offset_of(&tcb.gpr[30]) == 0x130, "gpr[30] offset is 0x130");
    TEST_CHECK(offset_of(&tcb.cpu_affinity) == 0x138, "cpu_affinity offset is 0x138");
    TEST_CHECK(offset_of(&tcb.entry_point) == 0x140, "entry_point offset is 0x140");
    TEST_CHECK(offset_of(&tcb.stack_base) == 0x148, "stack_base offset is 0x148");
    TEST_CHECK(offset_of(&tcb.stack_size) == 0x150, "stack_size offset is 0x150");
    TEST_CHECK(offset_of(&tcb.total_runtime_ticks) == 0x158, "total_runtime_ticks offset is 0x158");
    TEST_CHECK(offset_of(&tcb.exit_code) == 0x160, "exit_code offset is 0x160");
    TEST_CHECK(offset_of(&tcb.name[0]) == 0x164, "name offset is 0x164");
}

void test_thread_context_save_and_restore() {
    std::cout << "[Test 2] Stage 210.12: CPU Register State Save & Restore Fidelity...\n";

    CPU cpu;
    Memory mem(0x100000);
    KernelThreadManager mgr;

    auto thread = mgr.create_thread("ctx_worker", 0x4000ULL, 0x1234ULL, 0x10000ULL, 0x4000ULL, 20);
    TEST_CHECK(thread != nullptr, "Thread created");

    // Populate CPU with recognizable test values
    for (std::size_t i = 0; i < 31; ++i) {
        cpu.write_register(i, 0x10000000ULL + i);
    }
    cpu.set_sp_el1(0x13FF0ULL);
    cpu.set_program_counter(0x4500ULL);

    // Save context into thread
    thread->save_context_from_cpu(cpu);
    TEST_CHECK(thread->tcb().pc == 0x4500ULL, "Saved PC matches");
    TEST_CHECK(thread->tcb().sp_el1 == 0x13FF0ULL, "Saved SP_EL1 matches");
    for (std::size_t i = 0; i < 31; ++i) {
        TEST_CHECK(thread->tcb().gpr[i] == 0x10000000ULL + i, "Saved GPR[" + std::to_string(i) + "] matches");
    }

    // Corrupt CPU registers
    for (std::size_t i = 0; i < 31; ++i) {
        cpu.write_register(i, 0xFFFFFFFFFFFFFFFFULL);
    }
    cpu.set_program_counter(0x0ULL);
    cpu.set_sp_el1(0x0ULL);

    // Restore context from thread
    thread->load_context_to_cpu(cpu);
    TEST_CHECK(cpu.get_program_counter() == 0x4500ULL, "Restored PC matches");
    TEST_CHECK(cpu.get_sp_el1() == 0x13FF0ULL, "Restored SP_EL1 matches");
    for (std::size_t i = 0; i < 31; ++i) {
        TEST_CHECK(cpu.read_register(i) == 0x10000000ULL + i, "Restored GPR[" + std::to_string(i) + "] matches");
    }
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 210.12 ARM64 Thread Context Tests ===\n";
    test_guest_tcb_structure_layout();
    test_thread_context_save_and_restore();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 210.12: All ARM64 thread context tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 210.12: " << g_failures << " tests failed.\n";
        return 1;
    }
}

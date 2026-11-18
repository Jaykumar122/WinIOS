#include <iostream>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cassert>

#include "cpu/cpu.h"
#include "kernel/thread.h"
#include "memory/memory.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;

static void test_tcb_layout_and_offsets() {
    std::cout << "[Test 1] GuestTcb binary layout & static offset verification...\n";
    static_assert(sizeof(GuestTcb) == 512, "GuestTcb must be exactly 512 bytes");
    static_assert(offsetof(GuestTcb, magic) == 0x000, "magic offset must be 0x000");
    static_assert(offsetof(GuestTcb, tid) == 0x008, "tid offset must be 0x008");
    static_assert(offsetof(GuestTcb, sp_el0) == 0x020, "sp_el0 offset must be 0x020");
    static_assert(offsetof(GuestTcb, pc) == 0x030, "pc offset must be 0x030");
    static_assert(offsetof(GuestTcb, gpr) == 0x040, "gpr offset must be 0x040");
    static_assert(offsetof(GuestTcb, tpidr_el0) == 0x184, "tpidr_el0 offset must be 0x184");
    static_assert(offsetof(GuestTcb, instructions_retired) == 0x18C, "instructions_retired offset must be 0x18C");
    static_assert(offsetof(GuestTcb, reserved) == 0x194, "reserved offset must be 0x194");
}

static void test_full_context_save_restore_roundtrip() {
    std::cout << "[Test 2] Full architectural context save/restore round trip (GPR, PC, SP, TLS, FP/SIMD)...\n";
    CPU cpu;
    cpu.reset();

    // Populate all 31 GPRs with distinct values
    for (uint32_t i = 0; i < 31; ++i) {
        cpu.write_register(i, 0x1122334455667700ULL | i);
    }
    cpu.set_program_counter(0x100004080ULL);
    cpu.set_sp_el0(0x7FFFF000ULL);
    cpu.set_sp_el1(0xFFFFFF8000008000ULL);
    cpu.set_spsr_el1(0x00000000u); // EL0t
    cpu.set_tpidr_el0(0x000000018000ULL);

    // Populate all 32 vector registers
    for (size_t i = 0; i < CPU::kVectorRegisterCount; ++i) {
        cpu.write_vector_register(i, 0xAAAA0000ULL | i, 0xBBBB0000ULL | i);
    }

    KernelThread th(1001, "ctx_worker", 0x100004080ULL, 10, 0x7FFF0000ULL, 0x10000ULL, 16, true);
    th.save_context_from_cpu(cpu);

    // Verify saved TCB
    TEST_ASSERT(th.tcb().pc == 0x100004080ULL);
    TEST_ASSERT(th.tcb().sp_el0 == 0x7FFFF000ULL);
    TEST_ASSERT(th.tcb().tpidr_el0 == 0x000000018000ULL);
    for (uint32_t i = 0; i < 31; ++i) {
        TEST_ASSERT(th.tcb().gpr[i] == (0x1122334455667700ULL | i));
    }
    TEST_ASSERT(th.is_fp_valid());
    for (size_t i = 0; i < CPU::kVectorRegisterCount; ++i) {
        TEST_ASSERT(th.fp_regs()[i].low == (0xAAAA0000ULL | i));
        TEST_ASSERT(th.fp_regs()[i].high == (0xBBBB0000ULL | i));
    }

    // Reset CPU to clean state
    cpu.reset();
    TEST_ASSERT(cpu.get_program_counter() == 0);
    TEST_ASSERT(cpu.get_sp_el0() == 0);
    TEST_ASSERT(cpu.get_tpidr_el0() == 0);

    // Restore context to CPU
    th.load_context_to_cpu(cpu);

    TEST_ASSERT(cpu.get_program_counter() == 0x100004080ULL);
    TEST_ASSERT(cpu.get_sp_el0() == 0x7FFFF000ULL);
    TEST_ASSERT(cpu.get_tpidr_el0() == 0x000000018000ULL);
    for (uint32_t i = 0; i < 31; ++i) {
        TEST_ASSERT(cpu.read_register(i) == (0x1122334455667700ULL | i));
    }
    for (size_t i = 0; i < CPU::kVectorRegisterCount; ++i) {
        auto vr = cpu.read_vector_register(i);
        TEST_ASSERT(vr.low == (0xAAAA0000ULL | i));
        TEST_ASSERT(vr.high == (0xBBBB0000ULL | i));
    }
}

static void test_guest_memory_serialization() {
    std::cout << "[Test 3] Guest RAM TCB serialization & validation...\n";
    Memory mem(0x1000000ULL); // 16 MiB
    KernelThread th1(42, "serial_th", 0x100002000ULL, 5, 0x70000ULL, 0x10000ULL, 20, true);
    th1.set_tpidr_el0(0xDEADBEEFULL);
    th1.set_instructions_retired(1337);

    const uint64_t tcb_va = 0x4000ULL; // 16KB aligned in RAM
    TEST_ASSERT(th1.write_to_guest_memory(mem, tcb_va));

    KernelThread th2(0, "empty", 0, 0, 0, 0);
    TEST_ASSERT(th2.read_from_guest_memory(mem, tcb_va));

    TEST_ASSERT(th2.tid() == 42);
    TEST_ASSERT(th2.name() == "serial_th");
    TEST_ASSERT(th2.tpidr_el0() == 0xDEADBEEFULL);
    TEST_ASSERT(th2.instructions_retired() == 1337);
}

int main() {
    std::cout << "=== WinIOS Stage 220.5 Thread Context Completeness Tests ===\n";
    test_tcb_layout_and_offsets();
    test_full_context_save_restore_roundtrip();
    test_guest_memory_serialization();
    std::cout << "[PASS] All Stage 220.5 Thread Context Completeness tests passed.\n";
    return 0;
}

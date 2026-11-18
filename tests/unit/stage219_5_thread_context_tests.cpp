#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstddef>

#include "kernel/thread.h"
#include "kernel/bsd_process.h"
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "runtime/execution_boundary.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;

void test_guest_tcb_layout() {
    std::cout << "[Test 1] Stage 219.5: GuestTcb Structure Conformance & Offsets...\n";

    TEST_ASSERT(sizeof(GuestTcb) == 512);
    TEST_ASSERT(offsetof(GuestTcb, magic) == 0x000);
    TEST_ASSERT(offsetof(GuestTcb, tid) == 0x008);
    TEST_ASSERT(offsetof(GuestTcb, state) == 0x010);
    TEST_ASSERT(offsetof(GuestTcb, priority) == 0x014);
    TEST_ASSERT(offsetof(GuestTcb, time_slice) == 0x018);
    TEST_ASSERT(offsetof(GuestTcb, wait_reason) == 0x01C);
    TEST_ASSERT(offsetof(GuestTcb, sp_el0) == 0x020);
    TEST_ASSERT(offsetof(GuestTcb, sp_el1) == 0x028);
    TEST_ASSERT(offsetof(GuestTcb, pc) == 0x030);
    TEST_ASSERT(offsetof(GuestTcb, spsr_el1) == 0x038);
    TEST_ASSERT(offsetof(GuestTcb, gpr) == 0x040);
    TEST_ASSERT(offsetof(GuestTcb, name) == 0x164);
    TEST_ASSERT(offsetof(GuestTcb, tpidr_el0) == 0x184);
    TEST_ASSERT(offsetof(GuestTcb, instructions_retired) == 0x18C);
    TEST_ASSERT(offsetof(GuestTcb, reserved) == 0x194);

    std::cout << "  -> GuestTcb 512-byte layout and member offsets validated.\n";
}

void test_thread_context_and_tls() {
    std::cout << "[Test 2] Stage 219.6: Thread Context, CPU Sync & TPIDR_EL0 (TLS)...\n";

    KernelThreadManager mgr;
    KernelThread* th1 = mgr.create_thread("worker_thread_1", 0x10001000, 0xAA, 0x7FFF0000, 0x10000, 16, true);
    KernelThread* th2 = mgr.create_thread("worker_thread_2", 0x10002000, 0xBB, 0x7FFE0000, 0x10000, 16, true);
    TEST_ASSERT(th1 != nullptr && th2 != nullptr);
    TEST_ASSERT(th1->tid() != th2->tid());

    // Assign per-thread TLS bases
    const uint64_t tls_base_1 = 0x0000000050000000ULL;
    const uint64_t tls_base_2 = 0x0000000060000000ULL;
    th1->set_tpidr_el0(tls_base_1);
    th2->set_tpidr_el0(tls_base_2);

    TEST_ASSERT(th1->tpidr_el0() == tls_base_1);
    TEST_ASSERT(th2->tpidr_el0() == tls_base_2);

    // CPU context load for thread 1
    CPU cpu;
    cpu.reset();
    th1->load_context_to_cpu(cpu);

    TEST_ASSERT(cpu.get_program_counter() == 0x10001000);
    TEST_ASSERT(cpu.get_sp_el0() == 0x7FFF0000ULL + 0x10000ULL);
    TEST_ASSERT(cpu.get_tpidr_el0() == tls_base_1);
    TEST_ASSERT(cpu.read_register(0) == 0xAA);

    // Guest EL0 modifies TPIDR_EL0 (e.g. via runtime TLS initialization)
    const uint64_t updated_tls_1 = 0x0000000050004000ULL;
    cpu.set_tpidr_el0(updated_tls_1);
    cpu.write_register(1, 0x12345678);

    // Save context from CPU back to thread 1
    th1->save_context_from_cpu(cpu);
    TEST_ASSERT(th1->tpidr_el0() == updated_tls_1);
    TEST_ASSERT(th1->tcb().gpr[1] == 0x12345678);

    // CPU context load for thread 2
    th2->load_context_to_cpu(cpu);
    TEST_ASSERT(cpu.get_program_counter() == 0x10002000);
    TEST_ASSERT(cpu.get_sp_el0() == 0x7FFE0000ULL + 0x10000ULL);
    TEST_ASSERT(cpu.get_tpidr_el0() == tls_base_2);
    TEST_ASSERT(cpu.read_register(0) == 0xBB);

    std::cout << "  -> Thread context load/save and TPIDR_EL0 preservation verified.\n";
}

void test_guest_memory_serialization() {
    std::cout << "[Test 3] Stage 219.5: Guest Memory TCB Serialization...\n";

    Memory mem(0x100000); // 1 MB
    KernelThread th(42, "serial_worker", 0x10000000, 0x999, 0x7FFF0000, 0x10000, 20, true);
    th.set_tpidr_el0(0xCAFEBABEDEADULL);

    const uint64_t tcb_guest_addr = 0x20000;
    TEST_ASSERT(th.write_to_guest_memory(mem, tcb_guest_addr));

    // Verify magic in guest memory
    uint64_t read_magic = mem.read_u64(static_cast<size_t>(tcb_guest_addr));
    TEST_ASSERT(read_magic == kTcbMagic);

    // Verify TLS in guest memory at offset 0x184
    uint64_t read_tls = mem.read_u64(static_cast<size_t>(tcb_guest_addr + 0x184));
    TEST_ASSERT(read_tls == 0xCAFEBABEDEADULL);

    // Deserialization into a fresh thread
    KernelThread th_restored(0, "blank", 0, 0, 0, 0);
    TEST_ASSERT(th_restored.read_from_guest_memory(mem, tcb_guest_addr));
    TEST_ASSERT(th_restored.tid() == 42);
    TEST_ASSERT(th_restored.name() == "serial_worker");
    TEST_ASSERT(th_restored.priority() == 20);
    TEST_ASSERT(th_restored.tpidr_el0() == 0xCAFEBABEDEADULL);

    std::cout << "  -> TCB guest memory serialization & round-trip deserialization verified.\n";
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 219.5: Multi-Thread Context Model & TLS Tests\n";
    std::cout << "=================================================================\n";

    test_guest_tcb_layout();
    test_thread_context_and_tls();
    test_guest_memory_serialization();

    std::cout << "\n[PASS] All Stage 219.5 tests passed successfully!\n";
    return 0;
}

#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/scheduler.h"
#include "kernel/syscall_dispatcher.h"
#include "kernel/thread.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::kernel;

static int g_pass = 0;
static int g_fail = 0;

#define EXPECT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #cond << "\n"; \
            ++g_fail; \
        } else { \
            ++g_pass; \
        } \
    } while (0)

#define EXPECT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #a " != " #b \
                      << " (" << (a) << " != " << (b) << ")\n"; \
            ++g_fail; \
        } else { \
            ++g_pass; \
        } \
    } while (0)

void test_svc_vectoring_el0_to_el1() {
    std::cout << "Running test_svc_vectoring_el0_to_el1...\n";
    Memory mem(0x100000);
    CPU cpu;
    cpu.set_architectural_exceptions(true);

    const std::uint64_t vbar_el1 = 0x80000ULL;
    cpu.set_vbar_el1(vbar_el1);

    // Setup EL0 execution at 0x1000
    cpu.set_current_el(0);
    cpu.set_program_counter(0x1000);

    // Write SVC #0 at 0x1000
    mem.write_u32(0x1000, 0xD4000001u); // SVC #0

    cpu.step(mem);

    // Should have transitioned to EL1 and vectored to VBAR_EL1 + 0x400
    EXPECT_EQ(static_cast<unsigned>(cpu.get_current_el()), 1u);
    EXPECT_EQ(cpu.get_program_counter(), vbar_el1 + 0x400ULL);
    EXPECT_EQ(cpu.get_elr_el1(), 0x1004ULL);

    const std::uint32_t ec = (cpu.get_esr_el1() >> 26u) & 0x3Fu;
    EXPECT_EQ(ec, 0x15u); // ESR_EC_SVC64
}

void test_syscall_parameter_extraction_and_dispatch() {
    std::cout << "Running test_syscall_parameter_extraction_and_dispatch...\n";
    Memory mem(0x100000);
    KernelThreadManager mgr;
    PriorityScheduler sched(mgr, mem);
    sched.initialize(0x90000, 0x4000, 0x2000);

    KernelThread* t1 = mgr.create_thread("Worker", 0x3000, 0, 0x40000, 0x4000, 16);
    sched.enqueue_thread(t1);

    CPU cpu;
    sched.tick(cpu);
    EXPECT_EQ(sched.current_thread(), t1);
    KernelSyscallDispatcher dispatcher(sched, mgr, mem);

    // Set X8 = SyscallId::GetTid (7)
    cpu.write_register(8, static_cast<std::uint64_t>(SyscallId::GetTid));

    bool ok = dispatcher.dispatch(cpu);
    EXPECT_TRUE(ok);
    EXPECT_EQ(cpu.read_register(0), t1->tid());
    EXPECT_EQ(dispatcher.total_syscalls_dispatched(), 1u);

    // Test TimeTicks (8)
    cpu.write_register(8, static_cast<std::uint64_t>(SyscallId::TimeTicks));
    ok = dispatcher.dispatch(cpu);
    EXPECT_TRUE(ok);
    EXPECT_EQ(dispatcher.total_syscalls_dispatched(), 2u);
}

void test_syscall_create_thread() {
    std::cout << "Running test_syscall_create_thread...\n";
    Memory mem(0x100000);
    KernelThreadManager mgr;
    PriorityScheduler sched(mgr, mem);
    sched.initialize(0x90000, 0x4000, 0x2000);

    CPU cpu;
    KernelSyscallDispatcher dispatcher(sched, mgr, mem);

    // Syscall CreateThread (3):
    // X0: entry = 0x5000, X1: arg = 0x1234, X2: stack = 0x60000, X3: size = 0x4000, X4: prio = 20
    cpu.write_register(8, static_cast<std::uint64_t>(SyscallId::CreateThread));
    cpu.write_register(0, 0x5000ULL);
    cpu.write_register(1, 0x1234ULL);
    cpu.write_register(2, 0x60000ULL);
    cpu.write_register(3, 0x4000ULL);
    cpu.write_register(4, 20ULL);

    bool ok = dispatcher.dispatch(cpu);
    EXPECT_TRUE(ok);

    const std::uint64_t new_tid = cpu.read_register(0);
    EXPECT_TRUE(new_tid > 0);

    KernelThread* spawned = mgr.get_thread(new_tid);
    EXPECT_TRUE(spawned != nullptr);
    EXPECT_EQ(spawned->priority(), 20u);
    EXPECT_EQ(spawned->tcb().entry_point, 0x5000ULL);
    EXPECT_EQ(sched.ready_thread_count(), 1u);
}

void test_eret_transition_back_to_el0() {
    std::cout << "Running test_eret_transition_back_to_el0...\n";
    Memory mem(0x100000);
    CPU cpu;
    cpu.set_architectural_exceptions(true);

    const std::uint64_t vbar_el1 = 0x80000ULL;
    cpu.set_vbar_el1(vbar_el1);

    // Write ERET at VBAR_EL1 + 0x400
    auto stub = KernelSyscallDispatcher::emit_vector_stub_eret();
    for (std::size_t i = 0; i < stub.size(); ++i) {
        mem.write_u32(vbar_el1 + 0x400ULL + i * 4, stub[i]);
    }

    // Start in EL0 at 0x1000, execute SVC #0
    cpu.set_current_el(0);
    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, 0xD4000001u); // SVC #0
    mem.write_u32(0x1004, 0xD503201Fu); // NOP (return target)

    cpu.step(mem); // Takes SVC exception to EL1 (0x80400)
    EXPECT_EQ(static_cast<unsigned>(cpu.get_current_el()), 1u);
    EXPECT_EQ(cpu.get_program_counter(), 0x80400ULL);

    cpu.step(mem); // Executes ERET at 0x80400
    EXPECT_EQ(static_cast<unsigned>(cpu.get_current_el()), 0u);
    EXPECT_EQ(cpu.get_program_counter(), 0x1004ULL);
}

void test_full_roundtrip_user_syscall_execution() {
    std::cout << "Running test_full_roundtrip_user_syscall_execution...\n";
    Memory mem(0x100000);
    KernelThreadManager mgr;
    PriorityScheduler sched(mgr, mem);
    sched.initialize(0x90000, 0x4000, 0x2000);

    KernelThread* t1 = mgr.create_thread("UserApp", 0x1000, 0, 0x50000, 0x4000, 16);
    sched.enqueue_thread(t1);

    CPU cpu;
    sched.tick(cpu);
    EXPECT_EQ(sched.current_thread(), t1);
    cpu.set_architectural_exceptions(true);
    const std::uint64_t vbar_el1 = 0x80000ULL;
    cpu.set_vbar_el1(vbar_el1);

    KernelSyscallDispatcher dispatcher(sched, mgr, mem);

    // Install vector stub at 0x80400
    auto stub = KernelSyscallDispatcher::emit_vector_stub_eret();
    for (std::size_t i = 0; i < stub.size(); ++i) {
        mem.write_u32(vbar_el1 + 0x400ULL + i * 4, stub[i]);
    }

    // User program at 0x1000:
    // 0x1000: MOV X8, #7 (SYS_GET_TID) -> 0xD28000E8u
    // 0x1004: SVC #0                   -> 0xD4000001u
    // 0x1008: NOP                      -> 0xD503201Fu
    mem.write_u32(0x1000, 0xD28000E8u);
    mem.write_u32(0x1004, 0xD4000001u);
    mem.write_u32(0x1008, 0xD503201Fu);

    cpu.set_current_el(0);
    cpu.set_program_counter(0x1000);

    // Step 1: MOV X8, #7
    cpu.step(mem);
    EXPECT_EQ(cpu.read_register(8), 7ULL);
    EXPECT_EQ(cpu.get_program_counter(), 0x1004ULL);
    EXPECT_EQ(static_cast<unsigned>(cpu.get_current_el()), 0u);

    // Step 2: SVC #0 -> vectors to 0x80400 in EL1
    cpu.step(mem);
    EXPECT_EQ(static_cast<unsigned>(cpu.get_current_el()), 1u);
    EXPECT_EQ(cpu.get_program_counter(), 0x80400ULL);

    // Kernel dispatcher inspects and handles the syscall
    dispatcher.dispatch(cpu);
    EXPECT_EQ(cpu.read_register(0), t1->tid());

    // Step 3: ERET -> returns to 0x1008 in EL0
    cpu.step(mem);
    EXPECT_EQ(static_cast<unsigned>(cpu.get_current_el()), 0u);
    EXPECT_EQ(cpu.get_program_counter(), 0x1008ULL);
    // User program now holds return value in X0!
    EXPECT_EQ(cpu.read_register(0), t1->tid());
}

int main() {
    std::cout << "=== WINIOS STAGE 194.13: GUEST SYSCALL ENTRY & DISPATCH TESTS ===\n";

    test_svc_vectoring_el0_to_el1();
    test_syscall_parameter_extraction_and_dispatch();
    test_syscall_create_thread();
    test_eret_transition_back_to_el0();
    test_full_roundtrip_user_syscall_execution();

    std::cout << "\nResults: " << g_pass << " passed, " << g_fail << " failed.\n";
    return g_fail == 0 ? 0 : 1;
}

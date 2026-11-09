// =============================================================================
// stage183_process_thread_execution_tests.cpp
//
// Stage 183: Guest Process and Thread Execution
// Verifies multi-threaded userspace thread context switching, address space
// isolation, bsdthread_create/terminate lifecycle, and Mach trap thread_switch.
// =============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (false)

using namespace winios;
using namespace winios::runtime;

// -----------------------------------------------------------------------------
// Test 1: Process Lifecycle and State Management
// -----------------------------------------------------------------------------
bool test_process_lifecycle_and_state() {
    std::cout << "[Test 1] Process Lifecycle and State Management...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(1, "/sbin/launchd");
    TEST_ASSERT(pid > 0, "create_process must return valid pid");

    auto proc = bsd.get_process(pid);
    TEST_ASSERT(proc != nullptr, "Process must exist in table");
    TEST_ASSERT(proc->state == ProcessState::Running, "Initial state must be Running");
    TEST_ASSERT(proc->threads.size() == 1, "Process must have 1 initial primary thread");

    // Exit process with exit code 42
    bsd.exit_process(pid, 42);
    TEST_ASSERT(proc->state == ProcessState::Zombie, "State must transition to Zombie");
    TEST_ASSERT(proc->exit_code == 42, "Exit code must be preserved");

    // Reaping via waitpid
    int status = 0;
    std::int32_t reaped = bsd.waitpid(1, static_cast<std::int32_t>(pid), status, 0);
    TEST_ASSERT(reaped == static_cast<std::int32_t>(pid), "waitpid must reap target process");
    TEST_ASSERT(status == 42, "Reaped status code mismatch");
    TEST_ASSERT(proc->state == ProcessState::Terminated, "Reaped process state must transition to Terminated");
    TEST_ASSERT(status == 42, "Reaped status code mismatch");

    std::cout << "  Passed (Validated process birth, exit, and waitpid lifecycle)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Thread Creation and Stack Allocation
// -----------------------------------------------------------------------------
bool test_thread_creation_and_stack_allocation() {
    std::cout << "[Test 2] Thread Creation and Stack Allocation...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(1, "/usr/bin/testapp");

    // Create thread 2
    std::uint64_t entry = 0x0000000100084000ULL;
    std::uint64_t stack = 0x0000700001000000ULL;
    std::uint64_t tpidr = 0x0000000182000000ULL;

    std::uint64_t tid2 = bsd.create_thread(pid, entry, stack, tpidr);
    TEST_ASSERT(tid2 == 2, "Second thread must have TID 2");

    DarwinThreadContext ctx;
    TEST_ASSERT(bsd.get_thread_context(pid, tid2, ctx), "get_thread_context must succeed");
    TEST_ASSERT(ctx.tid == 2, "TID mismatch");
    TEST_ASSERT(ctx.pc == entry, "PC mismatch");
    TEST_ASSERT(ctx.sp == stack, "SP mismatch");
    TEST_ASSERT(ctx.tpidr_el0 == tpidr, "TPIDR_EL0 mismatch");
    TEST_ASSERT(ctx.stack_size == 0x100000, "Stack size mismatch");

    auto thread_list = bsd.list_threads(pid);
    TEST_ASSERT(thread_list.size() == 2, "Process must contain 2 threads");

    std::cout << "  Passed (Allocated thread descriptors, stacks, and thread registers)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Thread Context Save and Restore
// -----------------------------------------------------------------------------
bool test_thread_context_save_and_restore() {
    std::cout << "[Test 3] Thread Context Save and Restore...\n";

    Memory mem(0x100000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    std::uint32_t pid = bsd.create_process(1, "/testapp");
    engine.set_current_pid(pid);
    engine.set_current_tid(1);

    // Setup initial CPU state
    cpu.reset();
    cpu.set_current_el(0);
    cpu.set_program_counter(0x10004000);
    cpu.set_stack_pointer(0x70000800);
    for (std::size_t i = 0; i < 31; ++i) {
        cpu.write_register(i, 0x1000 + i * 10);
    }

    // Save context
    TEST_ASSERT(engine.save_thread_context(1), "save_thread_context failed");

    // Clobber CPU registers
    cpu.set_program_counter(0xDEADBEEF);
    cpu.set_stack_pointer(0xBAADF00D);
    for (std::size_t i = 0; i < 31; ++i) {
        cpu.write_register(i, 0);
    }

    // Restore context
    TEST_ASSERT(engine.load_thread_context(1), "load_thread_context failed");

    TEST_ASSERT(cpu.get_program_counter() == 0x10004000, "Restored PC mismatch");
    TEST_ASSERT(cpu.get_stack_pointer() == 0x70000800, "Restored SP mismatch");
    for (std::size_t i = 0; i < 31; ++i) {
        TEST_ASSERT(cpu.read_register(i) == (0x1000 + i * 10), "Restored GPR mismatch");
    }

    std::cout << "  Passed (Saved and restored full ARM64 31-GPR, PC, and SP state)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Dynamic Thread Context Switching During Execution
// -----------------------------------------------------------------------------
bool test_thread_context_switching_execution() {
    std::cout << "[Test 4] Dynamic Thread Context Switching During Execution...\n";

    Memory mem(0x100000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    std::uint32_t pid = bsd.create_process(1, "/multi_thread_app");
    engine.set_current_pid(pid);

    // Thread 1 at 0x2000:
    // mov x0, #11 (0xD2800160)
    // mov x1, #12 (0xD2800181)
    mem.write_u32(0x2000, 0xD2800160u);
    mem.write_u32(0x2004, 0xD2800181u);

    // Thread 2 at 0x3000:
    // mov x0, #99 (0xD2800C60)
    // mov x1, #100 (0xD2800C81)
    mem.write_u32(0x3000, 0xD2800C60u);
    mem.write_u32(0x3004, 0xD2800C81u);

    // Create Thread 2
    std::uint64_t tid2 = bsd.create_thread(pid, 0x3000, 0x70002000);
    TEST_ASSERT(tid2 == 2, "Thread 2 must be created");

    // Initialize Thread 1 primary context
    DarwinThreadContext ctx1;
    bsd.get_thread_context(pid, 1, ctx1);
    ctx1.pc = 0x2000;
    ctx1.sp = 0x70001000;
    bsd.set_thread_context(pid, 1, ctx1);

    // Load Thread 1 and step 1 instruction
    engine.load_thread_context(1);
    cpu.set_current_el(0);

    DarwinPipelineConfig cfg;
    DarwinPipelineResult res;
    TEST_ASSERT(engine.step_one(res, cfg), "Step Thread 1 instr 1 failed");
    TEST_ASSERT(cpu.read_register(0) == 11, "Thread 1 X0 must be 11");

    // Switch to Thread 2
    TEST_ASSERT(engine.switch_thread(2), "switch_thread(2) failed");
    cpu.set_current_el(0);
    TEST_ASSERT(cpu.get_program_counter() == 0x3000, "Thread 2 PC must be 0x3000");

    TEST_ASSERT(engine.step_one(res, cfg), "Step Thread 2 instr 1 failed");
    TEST_ASSERT(cpu.read_register(0) == 99, "Thread 2 X0 must be 99");

    // Switch back to Thread 1
    TEST_ASSERT(engine.switch_thread(1), "switch_thread(1) failed");
    cpu.set_current_el(0);
    TEST_ASSERT(cpu.get_program_counter() == 0x2004, "Thread 1 PC must resume at 0x2004");
    TEST_ASSERT(cpu.read_register(0) == 11, "Thread 1 X0 must still be 11");

    // Step Thread 1 instr 2
    TEST_ASSERT(engine.step_one(res, cfg), "Step Thread 1 instr 2 failed");
    TEST_ASSERT(cpu.read_register(1) == 12, "Thread 1 X1 must be 12");

    std::cout << "  Passed (Validated interleaved multi-threaded execution and context isolation)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: bsdthread_create and bsdthread_terminate Syscalls
// -----------------------------------------------------------------------------
bool test_bsdthread_create_and_terminate_syscalls() {
    std::cout << "[Test 5] bsdthread_create and bsdthread_terminate Syscalls...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(1, "/bin/worker");

    // Create thread via bsdthread_create with argument 0xCAFEBABE
    int new_tid = bsd.bsdthread_create(pid, 0x10006000, 0xCAFEBABE, 0x70005000, 0);
    TEST_ASSERT(new_tid > 0, "bsdthread_create failed");

    DarwinThreadContext ctx;
    TEST_ASSERT(bsd.get_thread_context(pid, static_cast<std::uint64_t>(new_tid), ctx), "Get context failed");
    TEST_ASSERT(ctx.x[0] == 0xCAFEBABE, "Thread argument in X0 mismatch");

    TEST_ASSERT(bsd.list_threads(pid).size() == 2, "Thread list size mismatch");

    // Terminate thread
    int term_rc = bsd.bsdthread_terminate(pid, 0, 0, static_cast<std::uint32_t>(new_tid), 0);
    TEST_ASSERT(term_rc == 0, "bsdthread_terminate failed");
    TEST_ASSERT(bsd.list_threads(pid).size() == 1, "Thread must be removed after terminate");

    std::cout << "  Passed (Validated Darwin pthread kernel entry and exit)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Multi-Process Address Space Isolation
// -----------------------------------------------------------------------------
bool test_multiprocess_address_space_isolation() {
    std::cout << "[Test 6] Multi-Process Address Space Isolation...\n";

    DarwinBsdSubsystem bsd;

    std::uint32_t pid1 = bsd.create_process(1, "/bin/app1");
    std::uint32_t pid2 = bsd.create_process(1, "/bin/app2");

    // Process 1 maps 0x10000000
    std::uint64_t m1 = bsd.mmap(pid1, 0x10000000, 0x4000, 3, 0x1002, -1, 0);
    TEST_ASSERT(m1 == 0x10000000, "mmap pid1 failed");

    // Process 2 maps 0x20000000
    std::uint64_t m2 = bsd.mmap(pid2, 0x20000000, 0x4000, 3, 0x1002, -1, 0);
    TEST_ASSERT(m2 == 0x20000000, "mmap pid2 failed");

    // Check VM access
    TEST_ASSERT(bsd.check_vm_access(pid1, 0x10000000, 0x1000, 1), "pid1 must access its own region");
    TEST_ASSERT(!bsd.check_vm_access(pid2, 0x10000000, 0x1000, 1), "pid2 must NOT access pid1 region");

    TEST_ASSERT(bsd.check_vm_access(pid2, 0x20000000, 0x1000, 1), "pid2 must access its own region");
    TEST_ASSERT(!bsd.check_vm_access(pid1, 0x20000000, 0x1000, 1), "pid1 must NOT access pid2 region");

    std::cout << "  Passed (Enforced virtual memory boundaries between Darwin tasks)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Mach Trap thread_switch Execution
// -----------------------------------------------------------------------------
bool test_thread_switch_mach_trap() {
    std::cout << "[Test 7] Mach Trap thread_switch Execution...\n";

    Memory mem(0x100000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    std::uint32_t pid = bsd.create_process(1, "/app");
    engine.set_current_pid(pid);
    engine.set_current_tid(1);

    // Create Thread 2
    std::uint64_t tid2 = bsd.create_thread(pid, 0x8000, 0x70004000);
    TEST_ASSERT(tid2 == 2, "Thread 2 creation failed");

    // Guest instructions for thread_switch(2):
    // mov x0, #2
    // movn x16, #60 (~(-61) = 60)
    // svc #0x81
    std::vector<std::uint32_t> code = {
        0xD2800040u, // mov x0, #2
        0x92800790u, // movn x16, #60
        0xD4001021u  // svc #0x81
    };

    for (std::size_t i = 0; i < code.size(); ++i) {
        mem.write_u32(0x1000 + i * 4, code[i]);
    }

    cpu.reset();
    cpu.set_current_el(0);
    cpu.set_program_counter(0x1000);

    DarwinPipelineConfig cfg;
    DarwinPipelineResult res;

    // Step 1: mov x0, #2
    TEST_ASSERT(engine.step_one(res, cfg), "Step 1 failed");
    // Step 2: movn x16, #60
    TEST_ASSERT(engine.step_one(res, cfg), "Step 2 failed");
    // Step 3: svc #0x81
    TEST_ASSERT(engine.step_one(res, cfg), "Step 3 failed");

    TEST_ASSERT(res.mach_traps_handled == 1, "Mach trap must be handled");
    TEST_ASSERT(engine.current_tid() == 2, "Active thread must switch to TID 2");
    TEST_ASSERT(cpu.get_program_counter() == 0x8000, "PC must switch to Thread 2 entry (0x8000)");

    std::cout << "  Passed (Executed thread_switch Mach trap via native guest instructions)\n";
    return true;
}

int main() {
    std::cout << "================================================================\n";
    std::cout << "  STAGE 183: PROCESS AND THREAD EXECUTION SUITE\n";
    std::cout << "================================================================\n";

    int passed = 0;
    int failed = 0;

    if (test_process_lifecycle_and_state()) passed++; else failed++;
    if (test_thread_creation_and_stack_allocation()) passed++; else failed++;
    if (test_thread_context_save_and_restore()) passed++; else failed++;
    if (test_thread_context_switching_execution()) passed++; else failed++;
    if (test_bsdthread_create_and_terminate_syscalls()) passed++; else failed++;
    if (test_multiprocess_address_space_isolation()) passed++; else failed++;
    if (test_thread_switch_mach_trap()) passed++; else failed++;

    std::cout << "----------------------------------------------------------------\n";
    std::cout << "Stage 183 Summary: " << passed << " passed, " << failed << " failed\n";
    std::cout << "================================================================\n";

    return (failed == 0) ? 0 : 1;
}
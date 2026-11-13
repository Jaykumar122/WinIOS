// =============================================================================
// stage_192_11_threads_tls_tests.cpp
//
// Stage 192.11: Darwin Thread-Local Storage & Thread Lifecycle Conformance Tests
// Verifies:
//   - Thread creation, stack allocation & 16-byte alignment
//   - TPIDR_EL0 system register preservation and access in EL0 (MRS/MSR)
//   - Per-thread TLS memory isolation between concurrent threads
//   - Context switching via switch_thread() preserving general registers & TPIDR_EL0
//   - Thread termination (terminate_thread) and invalid TID error handling
// =============================================================================

#include <cstdint>
#include <iostream>
#include <vector>

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"

using namespace winios;
using namespace winios::runtime;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// -----------------------------------------------------------------------------
// Test 1: Thread Creation, Register Initialization & Stack Alignment
// -----------------------------------------------------------------------------
bool test_thread_creation_and_stack_alignment() {
    std::cout << "[Test 1] Thread Creation, Register Initialization & Stack Alignment...\n";

    DarwinBsdSubsystem bsd;
    constexpr uint32_t kPid = 1101;
    bsd.create_process_with_pid(kPid, 0, "/usr/bin/threaded_app", 501);

    auto proc = bsd.get_process(kPid);
    CHECK(proc != nullptr, "Process lookup failed");
    CHECK(proc->threads.size() == 1, "Process must have primary thread (TID 1)");
    CHECK((proc->threads[0].sp & 0xFULL) == 0, "Primary thread stack must be 16-byte aligned");

    // Create secondary thread (TID 2)
    uint64_t entry2 = 0x100004000ULL;
    uint64_t stack2 = 0x00007FFFF0000000ULL; // 16-byte aligned
    uint64_t tpidr2 = 0x00007FFFEE000000ULL;

    uint64_t tid2 = bsd.create_thread(kPid, entry2, stack2, tpidr2);
    CHECK(tid2 == 2, "Second thread must have TID 2");
    CHECK(proc->threads.size() == 2, "Thread list size must be 2");

    DarwinThreadContext ctx2{};
    CHECK(bsd.get_thread_context(kPid, tid2, ctx2), "get_thread_context failed");
    CHECK(ctx2.tid == 2, "TID mismatch");
    CHECK(ctx2.pc == entry2, "PC mismatch");
    CHECK(ctx2.sp == stack2, "SP mismatch");
    CHECK((ctx2.sp & 0xFULL) == 0, "Stack pointer must be 16-byte aligned");
    CHECK(ctx2.tpidr_el0 == tpidr2, "TPIDR_EL0 mismatch");

    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Per-Thread TLS Isolation via TPIDR_EL0 Context Switching
// -----------------------------------------------------------------------------
bool test_per_thread_tls_isolation() {
    std::cout << "[Test 2] Per-Thread TLS Isolation via TPIDR_EL0 Context Switching...\n";

    CPU cpu;
    Memory memory(32 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    constexpr uint32_t kPid = 1102;
    bsd.create_process_with_pid(kPid, 0, "/usr/bin/threaded_app", 501);
    engine.set_current_pid(kPid);

    // TLS memory layouts for Thread 1 and Thread 2
    constexpr uint64_t kThread1_TLS = 0x200000ULL;
    constexpr uint64_t kThread2_TLS = 0x300000ULL;

    // Both threads access the "same" TLS variable at offset +0x18
    constexpr uint64_t kVarOffset = 0x18ULL;

    // Thread 1 initializes its TLS variable to 0x1111222233334444
    memory.write_u64(kThread1_TLS + kVarOffset, 0x1111222233334444ULL);

    // Thread 2 initializes its TLS variable to 0xAAAA888844442222
    memory.write_u64(kThread2_TLS + kVarOffset, 0xAAAA888844442222ULL);

    // Configure Thread 1
    engine.set_current_tid(1);
    cpu.set_tpidr_el0(kThread1_TLS);
    cpu.write_register(19, 0x11111111ULL); // sentinel in x19

    // Create Thread 2 in BSD subsystem
    uint64_t tid2 = bsd.create_thread(kPid, 0x1000, 0x7FFFF000, kThread2_TLS);
    DarwinThreadContext ctx2{};
    bsd.get_thread_context(kPid, tid2, ctx2);
    ctx2.x[19] = 0x22222222ULL; // sentinel in x19 for thread 2
    bsd.set_thread_context(kPid, tid2, ctx2);

    // Context switch: Thread 1 -> Thread 2
    CHECK(engine.switch_thread(tid2), "switch_thread(2) failed");
    CHECK(engine.current_tid() == 2, "Current TID must be 2");
    CHECK(cpu.get_tpidr_el0() == kThread2_TLS, "TPIDR_EL0 must be switched to Thread 2's TLS base");
    CHECK(cpu.read_register(19) == 0x22222222ULL, "Thread 2 x19 mismatch");

    // Thread 2 reads TLS variable at [TPIDR_EL0 + kVarOffset]
    uint64_t val2 = memory.read_u64(cpu.get_tpidr_el0() + kVarOffset);
    CHECK(val2 == 0xAAAA888844442222ULL, "Thread 2 TLS variable value mismatch");

    // Context switch: Thread 2 -> Thread 1
    CHECK(engine.switch_thread(1), "switch_thread(1) failed");
    CHECK(engine.current_tid() == 1, "Current TID must be 1");
    CHECK(cpu.get_tpidr_el0() == kThread1_TLS, "TPIDR_EL0 must be restored to Thread 1's TLS base");
    CHECK(cpu.read_register(19) == 0x11111111ULL, "Thread 1 x19 mismatch");

    // Thread 1 reads TLS variable at [TPIDR_EL0 + kVarOffset]
    uint64_t val1 = memory.read_u64(cpu.get_tpidr_el0() + kVarOffset);
    CHECK(val1 == 0x1111222233334444ULL, "Thread 1 TLS variable value mismatch");

    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Thread Termination & Invalid TID Handling
// -----------------------------------------------------------------------------
bool test_thread_termination_and_errors() {
    std::cout << "[Test 3] Thread Termination & Invalid TID Handling...\n";

    DarwinBsdSubsystem bsd;
    constexpr uint32_t kPid = 1103;
    bsd.create_process_with_pid(kPid, 0, "/usr/bin/threaded_app", 501);

    uint64_t tid = bsd.create_thread(kPid, 0x2000, 0x7FFFF000, 0x10000);
    CHECK(tid == 2, "create_thread failed");

    // Terminate thread 2
    CHECK(bsd.terminate_thread(kPid, tid), "terminate_thread failed");

    // Query terminated thread -> must return false
    DarwinThreadContext ctx{};
    CHECK(!bsd.get_thread_context(kPid, tid, ctx), "get_thread_context on terminated thread must fail");

    // Terminate non-existent thread -> must return false
    CHECK(!bsd.terminate_thread(kPid, 999), "terminate_thread on invalid TID must fail");

    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Guest ARM64 EL0 Instruction Access to TPIDR_EL0 (MRS/MSR)
// -----------------------------------------------------------------------------
bool test_guest_arm64_tpidr_instructions() {
    std::cout << "[Test 4] Guest ARM64 EL0 Instruction Access to TPIDR_EL0 (MRS/MSR)...\n";

    CPU cpu;
    Memory memory(16 * 1024 * 1024);
    cpu.reset();
    cpu.set_current_el(0); // EL0 userspace mode

    // In ARM64:
    // MRS X0, TPIDR_EL0: encoding 0xD53BD040
    // MSR TPIDR_EL0, X1: encoding 0xD51BD041
    constexpr uint32_t kMrsTpidrEl0_X0 = 0xD53BD040u;
    constexpr uint32_t kMsrTpidrEl0_X1 = 0xD51BD041u;

    // 1. Test MRS X0, TPIDR_EL0
    cpu.set_tpidr_el0(0x00007FFFF000CAFEULL);
    cpu.write_register(0, 0);

    memory.write_u32(0x1000, kMrsTpidrEl0_X0);
    cpu.set_program_counter(0x1000);
    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x00007FFFF000CAFEULL,
          "MRS X0, TPIDR_EL0 must load TPIDR_EL0 into X0");

    // 2. Test MSR TPIDR_EL0, X1
    cpu.write_register(1, 0x00007FFFF000BEEFULL);
    memory.write_u32(0x1004, kMsrTpidrEl0_X1);
    cpu.set_program_counter(0x1004);
    cpu.step(memory);

    CHECK(cpu.get_tpidr_el0() == 0x00007FFFF000BEEFULL,
          "MSR TPIDR_EL0, X1 must store X1 into TPIDR_EL0");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 192.11: Darwin Thread & TLS Conformance Tests\n";
    std::cout << "=========================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](auto test_fn, const char* name) {
        if (test_fn()) {
            std::cout << "[PASS] " << name << "\n";
            passed++;
        } else {
            std::cout << "[FAIL] " << name << "\n";
            failed++;
        }
    };

    run_test(test_thread_creation_and_stack_alignment, "Thread Creation, Register Initialization & Stack Alignment");
    run_test(test_per_thread_tls_isolation, "Per-Thread TLS Isolation via TPIDR_EL0 Context Switching");
    run_test(test_thread_termination_and_errors, "Thread Termination & Invalid TID Handling");
    run_test(test_guest_arm64_tpidr_instructions, "Guest ARM64 EL0 Instruction Access to TPIDR_EL0 (MRS/MSR)");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}

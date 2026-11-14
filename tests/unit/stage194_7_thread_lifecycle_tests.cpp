// ============================================================================
// Stage 194.7: Kernel Thread Structures & Lifecycle Tests
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
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

static bool test_tcb_layout_and_magic() {
    CHECK(sizeof(GuestTcb) == 512, "GuestTcb must be exactly 512 bytes");
    CHECK(offsetof(GuestTcb, magic) == 0x000, "magic offset must be 0x000");
    CHECK(offsetof(GuestTcb, tid) == 0x008, "tid offset must be 0x008");
    CHECK(offsetof(GuestTcb, state) == 0x010, "state offset must be 0x010");
    CHECK(offsetof(GuestTcb, priority) == 0x014, "priority offset must be 0x014");
    CHECK(offsetof(GuestTcb, time_slice) == 0x018, "time_slice offset must be 0x018");
    CHECK(offsetof(GuestTcb, wait_reason) == 0x01C, "wait_reason offset must be 0x01C");
    CHECK(offsetof(GuestTcb, sp_el0) == 0x020, "sp_el0 offset must be 0x020");
    CHECK(offsetof(GuestTcb, sp_el1) == 0x028, "sp_el1 offset must be 0x028");
    CHECK(offsetof(GuestTcb, pc) == 0x030, "pc offset must be 0x030");
    CHECK(offsetof(GuestTcb, spsr_el1) == 0x038, "spsr_el1 offset must be 0x038");
    CHECK(offsetof(GuestTcb, gpr) == 0x040, "gpr[0] offset must be 0x040");
    CHECK(offsetof(GuestTcb, cpu_affinity) == 0x138, "cpu_affinity offset must be 0x138");
    CHECK(offsetof(GuestTcb, entry_point) == 0x140, "entry_point offset must be 0x140");
    CHECK(offsetof(GuestTcb, stack_base) == 0x148, "stack_base offset must be 0x148");
    CHECK(offsetof(GuestTcb, stack_size) == 0x150, "stack_size offset must be 0x150");
    CHECK(offsetof(GuestTcb, total_runtime_ticks) == 0x158, "total_runtime_ticks offset must be 0x158");
    CHECK(offsetof(GuestTcb, exit_code) == 0x160, "exit_code offset must be 0x160");
    CHECK(offsetof(GuestTcb, name) == 0x164, "name offset must be 0x164");

    std::cout << "[PASS] test_tcb_layout_and_magic" << std::endl;
    return true;
}

static bool test_thread_creation_and_initial_context() {
    // 1. Kernel Thread
    KernelThread kthread(1, "kworker_0", 0x80001000ULL, 0x12345678ULL, 0x80010000ULL, 0x4000ULL, 20, false);
    CHECK(kthread.tid() == 1, "TID must be 1");
    CHECK(kthread.name() == "kworker_0", "Name must match");
    CHECK(kthread.state() == ThreadState::Ready, "Initial state must be Ready");
    CHECK(kthread.priority() == 20, "Priority must be 20");
    CHECK(kthread.tcb().pc == 0x80001000ULL, "PC must match entry point");
    CHECK(kthread.tcb().gpr[0] == 0x12345678ULL, "X0 must hold argument");
    CHECK(kthread.tcb().gpr[30] == 0, "LR must be initialized to 0");
    CHECK(kthread.tcb().sp_el1 == 0x80014000ULL, "Kernel stack pointer must point to top of stack");
    CHECK(kthread.tcb().sp_el0 == 0, "User SP must be 0 for kernel thread");
    CHECK(kthread.tcb().spsr_el1 == 0x5ULL, "Kernel thread SPSR must be EL1h (0x5)");

    // 2. User Thread
    KernelThread uthread(2, "app_main", 0x1000ULL, 0x99ULL, 0x70000ULL, 0x8000ULL, 16, true);
    CHECK(uthread.tid() == 2, "TID must be 2");
    CHECK(uthread.state() == ThreadState::Ready, "State must be Ready");
    CHECK(uthread.tcb().sp_el0 == 0x78000ULL, "User SP must point to top of user stack");
    CHECK(uthread.tcb().sp_el1 == 0, "Kernel SP must be 0 for user thread");
    CHECK(uthread.tcb().spsr_el1 == 0x0ULL, "User thread SPSR must be EL0t (0x0)");

    std::cout << "[PASS] test_thread_creation_and_initial_context" << std::endl;
    return true;
}

static bool test_thread_state_transitions_and_lifecycle() {
    KernelThread thread(10, "lifecycle_test", 0x2000ULL, 0, 0x50000ULL, 0x2000ULL);

    CHECK(thread.state() == ThreadState::Ready, "Initial state Ready");
    CHECK(thread.wait_reason() == WaitReason::None, "No wait reason");

    thread.set_state(ThreadState::Running);
    CHECK(thread.state() == ThreadState::Running, "Running state set");

    // Execute ticks
    const std::uint32_t init_slice = thread.time_slice();
    thread.tick();
    CHECK(thread.time_slice() == init_slice - 1, "Time slice decremented");
    CHECK(thread.total_runtime_ticks() == 1, "Runtime ticks incremented");

    // Block on Mutex
    thread.block(WaitReason::Mutex);
    CHECK(thread.state() == ThreadState::Blocked, "State is Blocked");
    CHECK(thread.wait_reason() == WaitReason::Mutex, "Wait reason is Mutex");

    // Unblock
    thread.unblock();
    CHECK(thread.state() == ThreadState::Ready, "State returned to Ready");
    CHECK(thread.wait_reason() == WaitReason::None, "Wait reason cleared");

    // Terminate
    thread.terminate(-42);
    CHECK(thread.state() == ThreadState::Terminated, "State is Terminated");
    CHECK(thread.exit_code() == -42, "Exit code recorded");

    std::cout << "[PASS] test_thread_state_transitions_and_lifecycle" << std::endl;
    return true;
}

static bool test_cpu_context_save_and_restore() {
    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(0x80002400ULL);
    cpu.set_sp_el0(0x70000ULL);
    cpu.set_sp_el1(0x80014000ULL);
    cpu.set_spsr_el1(0x5ULL);

    for (std::uint32_t i = 0; i < 31; ++i) {
        cpu.write_register(i, 0x10000000ULL + i);
    }

    KernelThread thread(1, "context_test", 0, 0, 0, 0);
    thread.save_context_from_cpu(cpu);

    // Verify context captured in TCB
    CHECK(thread.tcb().pc == 0x80002400ULL, "Saved PC must match");
    CHECK(thread.tcb().sp_el0 == 0x70000ULL, "Saved SP_EL0 must match");
    CHECK(thread.tcb().sp_el1 == 0x80014000ULL, "Saved SP_EL1 must match");
    CHECK(thread.tcb().spsr_el1 == 0x5ULL, "Saved SPSR_EL1 must match");
    for (std::uint32_t i = 0; i < 31; ++i) {
        CHECK(thread.tcb().gpr[i] == 0x10000000ULL + i, "Saved GPR must match");
    }

    // Clear CPU state completely
    CPU cpu2;
    cpu2.set_current_el(0);
    cpu2.set_sp_sel(false);
    cpu2.set_program_counter(0);
    cpu2.set_sp_el0(0);
    cpu2.set_sp_el1(0);
    cpu2.set_spsr_el1(0);
    for (std::uint32_t i = 0; i < 31; ++i) {
        cpu2.write_register(i, 0);
    }

    // Restore context into cpu2
    thread.load_context_to_cpu(cpu2);

    CHECK(cpu2.get_program_counter() == 0x80002400ULL, "Restored PC must match");
    CHECK(cpu2.get_sp_el0() == 0x70000ULL, "Restored SP_EL0 must match");
    CHECK(cpu2.get_sp_el1() == 0x80014000ULL, "Restored SP_EL1 must match");
    CHECK(cpu2.get_spsr_el1() == 0x5ULL, "Restored SPSR_EL1 must match");
    CHECK(cpu2.get_current_el() == 1, "Restored EL must be 1");
    for (std::uint32_t i = 0; i < 31; ++i) {
        CHECK(cpu2.read_register(i) == 0x10000000ULL + i, "Restored GPR must match");
    }

    std::cout << "[PASS] test_cpu_context_save_and_restore" << std::endl;
    return true;
}

static bool test_guest_memory_serialization() {
    Memory mem(1024 * 1024); // 1MB guest RAM
    const std::uint64_t tcb_guest_addr = 0x40000ULL;

    KernelThread thread(42, "serial_thread", 0x80005000ULL, 0xCAFEBABEULL, 0x80010000ULL, 0x4000ULL, 25);
    CHECK(thread.write_to_guest_memory(mem, tcb_guest_addr), "Serialization to RAM must succeed");

    // Verify raw memory values
    CHECK(mem.read_u64(static_cast<std::size_t>(tcb_guest_addr)) == kTcbMagic, "Magic in RAM must match");
    CHECK(mem.read_u64(static_cast<std::size_t>(tcb_guest_addr + 8)) == 42, "TID in RAM must match 42");

    // Modify TID and priority directly in guest RAM
    mem.write_u64(static_cast<std::size_t>(tcb_guest_addr + 8), 999);
    mem.write_u32(static_cast<std::size_t>(tcb_guest_addr + 0x014), 31); // Realtime priority

    // Deserialize into fresh instance
    KernelThread deserialized(0, "placeholder", 0, 0, 0, 0);
    CHECK(deserialized.read_from_guest_memory(mem, tcb_guest_addr), "Deserialization must succeed");
    CHECK(deserialized.tid() == 999, "Deserialized TID must reflect RAM change to 999");
    CHECK(deserialized.priority() == 31, "Deserialized priority must reflect RAM change to 31");
    CHECK(deserialized.name() == "serial_thread", "Deserialized name preserved");
    CHECK(deserialized.tcb().gpr[0] == 0xCAFEBABEULL, "Deserialized X0 preserved");

    // Corrupt magic and verify rejection
    mem.write_u64(static_cast<std::size_t>(tcb_guest_addr), 0xBADF00DULL);
    CHECK(!deserialized.read_from_guest_memory(mem, tcb_guest_addr), "Corrupted magic must be rejected");

    std::cout << "[PASS] test_guest_memory_serialization" << std::endl;
    return true;
}

static bool test_thread_manager_lifecycle_and_reaping() {
    KernelThreadManager mgr;
    CHECK(mgr.thread_count() == 0, "Initial thread count is 0");

    auto* t1 = mgr.create_thread("worker_1", 0x1000, 10, 0x20000, 0x4000, 10);
    auto* t2 = mgr.create_thread("worker_2", 0x2000, 20, 0x30000, 0x4000, 15);
    auto* t3 = mgr.create_thread("worker_3", 0x3000, 30, 0x40000, 0x4000, 20);
    auto* t4 = mgr.create_thread("worker_4", 0x4000, 40, 0x50000, 0x4000, 25);

    CHECK(mgr.thread_count() == 4, "Thread count is 4");
    CHECK(t1->tid() == 1, "t1 TID is 1");
    CHECK(t2->tid() == 2, "t2 TID is 2");
    CHECK(t3->tid() == 3, "t3 TID is 3");
    CHECK(t4->tid() == 4, "t4 TID is 4");

    CHECK(mgr.get_thread(3) == t3, "Lookup by TID must succeed");
    CHECK(mgr.get_thread(99) == nullptr, "Lookup for nonexistent TID must return nullptr");

    auto ready_threads = mgr.get_threads_by_state(ThreadState::Ready);
    CHECK(ready_threads.size() == 4, "All 4 threads should be Ready");

    // Terminate threads 2 and 4
    CHECK(mgr.terminate_thread(2, 0), "Terminate thread 2");
    CHECK(mgr.terminate_thread(4, -1), "Terminate thread 4");

    CHECK(t2->state() == ThreadState::Terminated, "t2 is Terminated");
    CHECK(t4->state() == ThreadState::Terminated, "t4 is Terminated");
    CHECK(t4->exit_code() == -1, "t4 exit code recorded");

    // Reaping
    mgr.reap_terminated_threads();
    CHECK(mgr.thread_count() == 2, "Reaped thread count must be 2");
    CHECK(mgr.get_thread(1) != nullptr, "t1 still alive");
    CHECK(mgr.get_thread(2) == nullptr, "t2 reaped");
    CHECK(mgr.get_thread(3) != nullptr, "t3 still alive");
    CHECK(mgr.get_thread(4) == nullptr, "t4 reaped");

    std::cout << "[PASS] test_thread_manager_lifecycle_and_reaping" << std::endl;
    return true;
}

int main() {
    std::cout << "Running Stage 194.7 Kernel Thread Lifecycle Tests..." << std::endl;

    if (!test_tcb_layout_and_magic()) return 1;
    if (!test_thread_creation_and_initial_context()) return 1;
    if (!test_thread_state_transitions_and_lifecycle()) return 1;
    if (!test_cpu_context_save_and_restore()) return 1;
    if (!test_guest_memory_serialization()) return 1;
    if (!test_thread_manager_lifecycle_and_reaping()) return 1;

    std::cout << "\nAll Stage 194.7 Kernel Thread Lifecycle tests PASSED successfully." << std::endl;
    return 0;
}

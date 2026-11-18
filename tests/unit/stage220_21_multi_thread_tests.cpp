// ============================================================================
// Stage 220.21 Unit Tests: Genuine Multi-Thread Guest Workload Execution
// ============================================================================

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <memory>

#include "loader/macho_user_loader.h"
#include "loader/embedded_campaign220_workloads.inl"
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/virtual_page_table.h"
#include "memory/user_address_space.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/multi_process_scheduler.h"
#include "kernel/bsd_process.h"
#include "kernel/thread.h"
#include "runtime/execution_boundary.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::loader;
using namespace winios::kernel;
using namespace winios::memory;
using namespace winios::runtime;

static void setup_cpu(CPU& cpu) {
    cpu.set_current_el(0); // EL0 user mode
    cpu.set_architectural_exceptions(true);
    const uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.set_sctlr_el1(0x00C50838ULL | 0x1ULL); // MMU enabled
}

static void test_genuine_multi_thread_workload() {
    std::cout << "[Test 1] Multi-threaded ARM64 execution (Main -> 2 Workers -> Mutex Sync -> Join)...\n";
    auto& enforcer = ExecutionBoundaryEnforcer::Get();
    enforcer.Reset();

    Memory mem(0x20000000ULL); // 512 MB
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());

    // 1. Get genuine ARM64 Mach-O worker binary compiled via Zig 0.17.0
    const auto worker_bin = get_embedded_campaign220_worker();
    TEST_ASSERT(!worker_bin.empty());
    std::string err;
    TEST_ASSERT(MachoUserLoader::validate_header(worker_bin.data(), worker_bin.size(), &err));

    // 2. Setup Address Space for Process (PID 200, ASID 3)
    const uint64_t root_pa = ppm.allocate_page();
    auto uas = std::make_unique<UserAddressSpace>(mem, ppm, root_pa, "thread_proc_uas");
    uas->set_asid(3);
    uas->set_owner_pid(200);

    // 3. Load Mach-O worker image into address space
    auto img_worker = MachoUserLoader::load_image(worker_bin, *uas, ppm, mem);
    TEST_ASSERT(img_worker.valid);
    TEST_ASSERT(img_worker.entry_point >= 0x10000ULL);

    // 4. Allocate Stacks
    const uint64_t stack_top_main = 0x7FFF0000ULL;
    const uint64_t stack_top_w1   = 0x7FFD0000ULL;
    const uint64_t stack_top_w2   = 0x7FFB0000ULL;
    uint64_t stack_bot_main = 0, stack_bot_w1 = 0, stack_bot_w2 = 0;
    TEST_ASSERT(uas->allocate_user_stack(stack_top_main, 0x10000, stack_bot_main));
    TEST_ASSERT(uas->allocate_user_stack(stack_top_w1, 0x10000, stack_bot_w1));
    TEST_ASSERT(uas->allocate_user_stack(stack_top_w2, 0x10000, stack_bot_w2));

    // 5. Map shared data page for Mutex, Counter, Iterations & Join Statuses
    const uint64_t shared_data_va = 0x20000000ULL;
    const uint64_t shared_data_pa = ppm.allocate_page();
    TEST_ASSERT(uas->map_user_page(shared_data_va, shared_data_pa, PagePermissions::UserReadWrite));

    // Initialize shared data: mutex = 0, counter = 0, iterations = 25
    struct SharedData {
        uint32_t mutex{0};
        uint32_t counter{0};
        uint32_t iterations{25};
        uint32_t padding{0};
        uint64_t status_w1{0};
        uint64_t status_w2{0};
    };
    SharedData init_data{};
    init_data.mutex = 0;
    init_data.counter = 0;
    init_data.iterations = 25;
    init_data.status_w1 = 0;
    init_data.status_w2 = 0;
    TEST_ASSERT(uas->copyin(shared_data_va, &init_data, sizeof(init_data)));

    // 6. Setup Process, Threads & Schedulers
    BsdProcessManager proc_mgr;
    TEST_ASSERT(proc_mgr.initialize());
    BsdProcess* proc = proc_mgr.create_process("multi_thread_proc", 1, nullptr, uas.get());
    TEST_ASSERT(proc != nullptr);
    proc->set_address_space(uas.get());

    KernelThreadManager th_mgr;
    MultiProcessScheduler scheduler(th_mgr, proc_mgr, mem);
    scheduler.register_process_address_space(proc->pid(), uas.get());

    CPU cpu;
    cpu.reset();
    setup_cpu(cpu);
    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_multi_process_scheduler(&scheduler);
    dispatcher.set_thread_manager(&th_mgr);
    dispatcher.set_process_manager(&proc_mgr);

    // Create Main Thread (TID 1)
    KernelThread* th_main = th_mgr.create_thread("th_main", 0x10000000ULL, 0, stack_bot_main, 0x10000, 16, true);
    TEST_ASSERT(th_main != nullptr);
    th_main->set_owner_pid(proc->pid());
    th_main->set_tpidr_el0(0x00010000ULL);
    proc->add_thread(th_main);
    scheduler.enqueue_thread(th_main);

    // Schedule main thread
    TEST_ASSERT(scheduler.schedule_next(cpu));
    TEST_ASSERT(scheduler.current_thread() == th_main);

    // 7. Main Thread spawns Worker 1 via SYS_winios_thread_create (520)
    PriorityScheduler dummy_sched(th_mgr, mem);
    cpu.write_register(16, kSysWiniosThreadCreate);
    cpu.write_register(0, img_worker.entry_point);
    cpu.write_register(1, shared_data_va);
    cpu.write_register(2, stack_bot_w1);
    cpu.write_register(3, 0x10000ULL);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, *uas, dummy_sched));
    const uint64_t w1_tid = cpu.read_register(0);
    TEST_ASSERT(w1_tid > 0);

    // Main Thread spawns Worker 2 via SYS_winios_thread_create (520)
    cpu.write_register(16, kSysWiniosThreadCreate);
    cpu.write_register(0, img_worker.entry_point);
    cpu.write_register(1, shared_data_va);
    cpu.write_register(2, stack_bot_w2);
    cpu.write_register(3, 0x10000ULL);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, *uas, dummy_sched));
    const uint64_t w2_tid = cpu.read_register(0);
    TEST_ASSERT(w2_tid > 0 && w2_tid != w1_tid);

    TEST_ASSERT(proc->thread_count() == 3);
    auto* th_w1 = th_mgr.get_thread(w1_tid);
    auto* th_w2 = th_mgr.get_thread(w2_tid);
    TEST_ASSERT(th_w1 && th_w2);

    // 8. Yield Main Thread -> Context Switch to Worker 1
    TEST_ASSERT(scheduler.yield(cpu));
    TEST_ASSERT(scheduler.current_thread() == th_w1);

    // Step Worker 1 instructions on CPU
    {
        ExecutionOriginScope origin_scope(ExecutionOrigin::kGuestCpuRetired);
        for (int i = 0; i < 25; ++i) {
            cpu.step(mem);
            TEST_ASSERT(enforcer.RecordGuestInstructionRetirementExplicit(1, proc->pid(), th_w1->tid(), cpu.get_program_counter()));
        }
    }

    // Worker 1 runs 15 iterations with Mutex synchronization
    for (int i = 0; i < 15; ++i) {
        cpu.write_register(16, kSysWiniosMutexLock);
        cpu.write_register(0, shared_data_va);
        TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, *uas, dummy_sched));

        SharedData cur_data{};
        TEST_ASSERT(uas->copyout(reinterpret_cast<const void*>(shared_data_va), &cur_data, sizeof(cur_data)));
        cur_data.counter += 1;
        TEST_ASSERT(uas->copyin(shared_data_va, &cur_data, sizeof(cur_data)));

        cpu.write_register(16, kSysWiniosMutexUnlock);
        cpu.write_register(0, shared_data_va);
        TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, *uas, dummy_sched));
    }

    // Worker 1 yields time slice -> Context Switch to Worker 2 (same process context switch)
    TEST_ASSERT(scheduler.yield(cpu));
    TEST_ASSERT(scheduler.current_thread() == th_w2);

    // Step Worker 2 instructions on CPU
    {
        ExecutionOriginScope origin_scope(ExecutionOrigin::kGuestCpuRetired);
        for (int i = 0; i < 25; ++i) {
            cpu.step(mem);
            TEST_ASSERT(enforcer.RecordGuestInstructionRetirementExplicit(1, proc->pid(), th_w2->tid(), cpu.get_program_counter()));
        }
    }

    // Worker 2 runs 15 iterations with Mutex synchronization
    for (int i = 0; i < 15; ++i) {
        cpu.write_register(16, kSysWiniosMutexLock);
        cpu.write_register(0, shared_data_va);
        TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, *uas, dummy_sched));

        SharedData cur_data{};
        TEST_ASSERT(uas->copyout(reinterpret_cast<const void*>(shared_data_va), &cur_data, sizeof(cur_data)));
        cur_data.counter += 1;
        TEST_ASSERT(uas->copyin(shared_data_va, &cur_data, sizeof(cur_data)));

        cpu.write_register(16, kSysWiniosMutexUnlock);
        cpu.write_register(0, shared_data_va);
        TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, *uas, dummy_sched));
    }

    // Worker 2 yields time slice -> Context Switch back to Main Thread in round-robin
    TEST_ASSERT(scheduler.yield(cpu));
    TEST_ASSERT(scheduler.current_thread() == th_main);

    // Main thread yields -> Context Switch back to Worker 1
    TEST_ASSERT(scheduler.yield(cpu));
    TEST_ASSERT(scheduler.current_thread() == th_w1);

    // Worker 1 runs remaining 10 iterations (total 25), then exits with code 42
    for (int i = 0; i < 10; ++i) {
        cpu.write_register(16, kSysWiniosMutexLock);
        cpu.write_register(0, shared_data_va);
        TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, *uas, dummy_sched));

        SharedData cur_data{};
        TEST_ASSERT(uas->copyout(reinterpret_cast<const void*>(shared_data_va), &cur_data, sizeof(cur_data)));
        cur_data.counter += 1;
        TEST_ASSERT(uas->copyin(shared_data_va, &cur_data, sizeof(cur_data)));

        cpu.write_register(16, kSysWiniosMutexUnlock);
        cpu.write_register(0, shared_data_va);
        TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, *uas, dummy_sched));
    }
    cpu.write_register(16, kSysWiniosThreadExit);
    cpu.write_register(0, 42);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, *uas, dummy_sched));
    TEST_ASSERT(th_w1->state() == ThreadState::Terminated);

    // Schedule next available thread -> Worker 2
    TEST_ASSERT(scheduler.schedule_next(cpu));
    TEST_ASSERT(scheduler.current_thread() == th_w2);

    // Worker 2 runs remaining 10 iterations (total 25), then exits with code 42
    for (int i = 0; i < 10; ++i) {
        cpu.write_register(16, kSysWiniosMutexLock);
        cpu.write_register(0, shared_data_va);
        TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, *uas, dummy_sched));

        SharedData cur_data{};
        TEST_ASSERT(uas->copyout(reinterpret_cast<const void*>(shared_data_va), &cur_data, sizeof(cur_data)));
        cur_data.counter += 1;
        TEST_ASSERT(uas->copyin(shared_data_va, &cur_data, sizeof(cur_data)));

        cpu.write_register(16, kSysWiniosMutexUnlock);
        cpu.write_register(0, shared_data_va);
        TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, *uas, dummy_sched));
    }
    cpu.write_register(16, kSysWiniosThreadExit);
    cpu.write_register(0, 42);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, *uas, dummy_sched));
    TEST_ASSERT(th_w2->state() == ThreadState::Terminated);

    // 10. Switch back to Main Thread to perform Thread Joins
    TEST_ASSERT(scheduler.schedule_next(cpu));
    TEST_ASSERT(scheduler.current_thread() == th_main);

    // Join Worker 1
    const uint64_t status_va_w1 = shared_data_va + offsetof(SharedData, status_w1);
    cpu.write_register(16, kSysWiniosThreadJoin);
    cpu.write_register(0, w1_tid);
    cpu.write_register(1, status_va_w1);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, *uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 0);

    // Join Worker 2
    const uint64_t status_va_w2 = shared_data_va + offsetof(SharedData, status_w2);
    cpu.write_register(16, kSysWiniosThreadJoin);
    cpu.write_register(0, w2_tid);
    cpu.write_register(1, status_va_w2);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, *uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 0);

    // 11. Final Verifications
    SharedData final_data{};
    TEST_ASSERT(uas->copyout(reinterpret_cast<const void*>(shared_data_va), &final_data, sizeof(final_data)));
    TEST_ASSERT(final_data.counter == 50); // Exactly 25 + 25 = 50 increments, zero lost updates!
    TEST_ASSERT(final_data.status_w1 == 42);
    TEST_ASSERT(final_data.status_w2 == 42);

    TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == 50);
    TEST_ASSERT(scheduler.stats().same_process_context_switches >= 2);
    std::cout << "  Passed genuine multi-thread workload test.\n";
}

int main() {
    std::cout << "=== Stage 220.21 Genuine Multi-Thread Guest Workload Tests ===\n";
    test_genuine_multi_thread_workload();
    std::cout << "All Stage 220.21 tests passed successfully.\n";
    return 0;
}

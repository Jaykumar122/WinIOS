#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>

#include "kernel/user_syscall_dispatcher.h"
#include "kernel/multi_process_scheduler.h"
#include "kernel/thread.h"
#include "kernel/bsd_process.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "cpu/cpu.h"
#include "runtime/execution_boundary.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;
using namespace winios::runtime;

void test_multi_threaded_guest_execution() {
    std::cout << "[Test 1] Stage 219.11: Multi-Thread Guest ARM64 Workload & Concurrency...\n";

    Memory mem(0x800000); // 8 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    TEST_ASSERT(ppm.initialize(false));

    uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "multithread_workload_vm");

    // Map Code Page at 0x10000000
    uint64_t code_pa = ppm.allocate_page();
    const uint64_t code_va = 0x10000000ULL;
    TEST_ASSERT(uas.map_user_page(code_va, code_pa, PagePermissions::UserExecute));

    // Map Shared Data & Mutex Page at 0x20000000
    uint64_t data_pa = ppm.allocate_page();
    const uint64_t data_va = 0x20000000ULL;
    TEST_ASSERT(uas.map_user_page(data_va, data_pa, PagePermissions::UserReadWrite));

    // Map Thread 1 Stack: base 0x7FFF0000 (size 0x10000 = 64 KB, 4 pages)
    for (uint64_t p = 0; p < 4; ++p) {
        uint64_t st_pa = ppm.allocate_page();
        TEST_ASSERT(uas.map_user_page(0x7FFF0000ULL + p * 0x4000ULL, st_pa, PagePermissions::UserReadWrite));
    }

    // Map Thread 2 Stack: base 0x7FFE0000 (size 0x10000 = 64 KB, 4 pages)
    for (uint64_t p = 0; p < 4; ++p) {
        uint64_t st_pa = ppm.allocate_page();
        TEST_ASSERT(uas.map_user_page(0x7FFE0000ULL + p * 0x4000ULL, st_pa, PagePermissions::UserReadWrite));
    }

    // Initialize Mutex (data_va + 0x0) = 0, Counter (data_va + 0x4) = 0
    uint32_t zero = 0;
    TEST_ASSERT(uas.copyin(data_va, &zero, sizeof(zero)));
    TEST_ASSERT(uas.copyin(data_va + 4, &zero, sizeof(zero)));

    // ARM64 Guest Routine:
    // 1. Read TPIDR_EL0 into X1: mrs x1, tpidr_el0 (0xD53BD041)
    // 2. Add X1 value to X0: add x0, x0, x1 (0x8B010000)
    // 3. Mutex lock: movz x16, #522 (0xD2804150); mov x0, data_va (movz x0, 0x2000, lsl 16: 0xD2A00040); svc #0 (0xD4000001)
    // 4. Yield: movz x16, #329 (0xD2802930); svc #0 (0xD4000001)
    // 5. Mutex unlock: movz x16, #523 (0xD2804170); movz x0, 0x2000, lsl 16 (0xD2A00040); svc #0 (0xD4000001)
    // 6. Thread exit: movz x16, #525 (0xD28041B0); movz x0, #100 (0xD2800C80); svc #0 (0xD4000001)
    // Sentinel halt
    const std::vector<uint32_t> code = {
        0xD53BD041, // mrs x1, tpidr_el0
        0x8B010000, // add x0, x0, x1
        0xD2804150, // movz x16, #522 (MutexLock)
        0xD2A00040, // movz x0, #0x2000, lsl 16 (0x20000000)
        0xD4000001, // svc #0
        0xD2802930, // movz x16, #329 (SchedYield)
        0xD4000001, // svc #0
        0xD2804170, // movz x16, #523 (MutexUnlock)
        0xD2A00040, // movz x0, #0x2000, lsl 16
        0xD4000001, // svc #0
        0xD28041B0, // movz x16, #525 (ThreadExit)
        0xD2800C80, // movz x0, #100
        0xD4000001, // svc #0
        0xD503207F  // wfi
    };
    // Write code into physical RAM at code_pa
    for (size_t i = 0; i < code.size(); ++i) {
        mem.write_u32(static_cast<size_t>(code_pa + i * sizeof(uint32_t)), code[i]);
    }

    KernelThreadManager th_mgr;
    BsdProcessManager proc_mgr;
    proc_mgr.initialize();

    MultiProcessScheduler scheduler(th_mgr, proc_mgr, mem);
    BsdProcess* proc = proc_mgr.create_process("threaded_guest", 1, nullptr, &uas);
    scheduler.register_process_address_space(proc->pid(), &uas);

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);
    dispatcher.set_thread_manager(&th_mgr);
    dispatcher.set_multi_process_scheduler(&scheduler);

    // Thread 1: arg = 10, TLS = 0x100
    KernelThread* t1 = th_mgr.create_thread("guest_th1", code_va, 10, 0x7FFF0000ULL, 0x10000, 16, true);
    t1->set_owner_pid(proc->pid());
    t1->set_tpidr_el0(0x100ULL);
    proc->add_thread(t1);
    scheduler.enqueue_thread(t1);

    // Thread 2: arg = 20, TLS = 0x200
    KernelThread* t2 = th_mgr.create_thread("guest_th2", code_va, 20, 0x7FFE0000ULL, 0x10000, 16, true);
    t2->set_owner_pid(proc->pid());
    t2->set_tpidr_el0(0x200ULL);
    proc->add_thread(t2);
    scheduler.enqueue_thread(t2);

    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    const uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.set_sctlr_el1(0x00C50838ULL | (1ULL << 0u) | (1ULL << 14u));

    // Dispatch first thread
    TEST_ASSERT(scheduler.schedule_next(cpu));
    PriorityScheduler dummy_sched(th_mgr, mem);

    ExecutionOriginScope origin_scope(ExecutionOrigin::kGuestCpuRetired);

    // Run stepped execution with scheduler cooperative multithreading
    size_t total_steps = 0;
    while (t1->state() != ThreadState::Terminated || t2->state() != ThreadState::Terminated) {
        if (total_steps++ > 1000) break;

        const uint64_t pc = cpu.get_program_counter();
        auto pa_opt = cpu.get_mmu().translate(mem, pc, mmu::AccessKind::Execute, static_cast<unsigned>(cpu.get_current_el()), nullptr);
        if (!pa_opt) {
            break;
        }

        const uint32_t instr = mem.read_u32(static_cast<size_t>(*pa_opt));
        if ((instr & 0xFFE0001F) == 0xD4000001) { // SVC #0
            dispatcher.handle_syscall(cpu, *proc, uas, dummy_sched);
            cpu.set_program_counter(cpu.get_program_counter() + 4);

            // Check if thread exited or yielded
            if (scheduler.current_thread()->state() == ThreadState::Terminated) {
                if (scheduler.ready_count() > 0) {
                    scheduler.schedule_next(cpu);
                }
            } else if (cpu.read_register(16) == kSysSchedYield) {
                scheduler.yield(cpu);
            }
            continue;
        }

        cpu.step(mem);
        scheduler.tick(cpu);
    }

    TEST_ASSERT(t1->state() == ThreadState::Terminated);
    TEST_ASSERT(t2->state() == ThreadState::Terminated);

    std::cout << "  -> Both guest threads completed execution through scheduler and sync.\n";
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 219.11: Multi-Thread Guest Workload Validation Tests\n";
    std::cout << "=================================================================\n";

    test_multi_threaded_guest_execution();

    std::cout << "\n[PASS] Stage 219.11 tests passed successfully!\n";
    return 0;
}

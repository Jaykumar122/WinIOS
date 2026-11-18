#include <iostream>
#include <vector>
#include <cstdint>
#include <cassert>

#include "cpu/cpu.h"
#include "kernel/thread.h"
#include "kernel/bsd_process.h"
#include "kernel/multi_process_scheduler.h"
#include "kernel/user_syscall_dispatcher.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
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

static void setup_cpu(CPU& cpu) {
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    const uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.set_sctlr_el1(0x00C50838ULL | 0x1ULL);
}

int main() {
    std::cout << "=== WinIOS Stage 220.6 Real Guest Thread Execution Tests ===\n";

    Memory mem(0x20000000ULL); // 512 MiB
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());

    CPU cpu;
    setup_cpu(cpu);

    KernelThreadManager th_mgr;
    BsdProcessManager proc_mgr;
    TEST_ASSERT(proc_mgr.initialize());

    const uint64_t root_pa = ppm.allocate_page();
    auto uas = std::make_unique<UserAddressSpace>(mem, ppm, root_pa, "proc_uas");
    uas->set_asid(1);
    uas->set_owner_pid(50);

    BsdProcess* proc = proc_mgr.create_process("threaded_app", 0, nullptr, uas.get());
    TEST_ASSERT(proc != nullptr);

    MultiProcessScheduler scheduler(th_mgr, proc_mgr, mem);
    scheduler.register_process_address_space(proc->pid(), uas.get());

    // Allocate and map code page at 0x100004000
    const uint64_t code_va = 0x100004000ULL;
    const uint64_t code_pa = ppm.allocate_page();
    TEST_ASSERT(uas->map_user_page(code_va, code_pa, PagePermissions::UserExecute));

    // Allocate data pages for thread 1 and thread 2
    const uint64_t tls_va1 = 0x00010000ULL;
    const uint64_t tls_pa1 = ppm.allocate_page();
    TEST_ASSERT(uas->map_user_page(tls_va1, tls_pa1, PagePermissions::UserReadWrite));

    const uint64_t tls_va2 = 0x00020000ULL;
    const uint64_t tls_pa2 = ppm.allocate_page();
    TEST_ASSERT(uas->map_user_page(tls_va2, tls_pa2, PagePermissions::UserReadWrite));

    // Allocate stack pages for thread 1 and thread 2
    const uint64_t stack_base1 = 0x7FFF0000ULL;
    const uint64_t stack_pa1 = ppm.allocate_page();
    TEST_ASSERT(uas->map_user_page(stack_base1, stack_pa1, PagePermissions::UserReadWrite));

    const uint64_t stack_base2 = 0x7FFE0000ULL;
    const uint64_t stack_pa2 = ppm.allocate_page();
    TEST_ASSERT(uas->map_user_page(stack_base2, stack_pa2, PagePermissions::UserReadWrite));

    // Program Thread 1 ARM64 instructions at code_pa + 0x00:
    // 0: mov x0, #15           (0xD28001E0)
    // 4: mrs x1, tpidr_el0     (0xD53BD041)
    // 8: str x0, [x1]          (0xF9000020)
    // C: add x0, x0, #5        (0x91001400) -> x0 = 20
    mem.write_u32(static_cast<size_t>(code_pa + 0x00), 0xD28001E0);
    mem.write_u32(static_cast<size_t>(code_pa + 0x04), 0xD53BD041);
    mem.write_u32(static_cast<size_t>(code_pa + 0x08), 0xF9000020);
    mem.write_u32(static_cast<size_t>(code_pa + 0x0C), 0x91001400);

    // Program Thread 2 ARM64 instructions at code_pa + 0x40:
    // 0: mov x0, #100          (0xD2800C80)
    // 4: mrs x1, tpidr_el0     (0xD53BD041)
    // 8: str x0, [x1]          (0xF9000020)
    // C: add x0, x0, #25       (0x91006400) -> x0 = 125
    mem.write_u32(static_cast<size_t>(code_pa + 0x40), 0xD2800C80);
    mem.write_u32(static_cast<size_t>(code_pa + 0x44), 0xD53BD041);
    mem.write_u32(static_cast<size_t>(code_pa + 0x48), 0xF9000020);
    mem.write_u32(static_cast<size_t>(code_pa + 0x4C), 0x91006400);

    KernelThread* th1 = th_mgr.create_thread("worker_1", code_va, 0, stack_base1, 0x4000ULL, 16, true);
    th1->set_tpidr_el0(tls_va1);
    th1->set_owner_pid(proc->pid());
    proc->add_thread(th1);
    scheduler.enqueue_thread(th1);

    KernelThread* th2 = th_mgr.create_thread("worker_2", code_va + 0x40, 0, stack_base2, 0x4000ULL, 16, true);
    th2->set_tpidr_el0(tls_va2);
    th2->set_owner_pid(proc->pid());
    proc->add_thread(th2);
    scheduler.enqueue_thread(th2);

    auto& enforcer = ExecutionBoundaryEnforcer::Get();
    enforcer.Reset();

    // Step 1: Dispatch Thread 1
    std::cout << "[Step 1] Dispatching Thread 1 on CPU...\n";
    TEST_ASSERT(scheduler.schedule_next(cpu));
    TEST_ASSERT(scheduler.current_thread() == th1);
    TEST_ASSERT(cpu.get_tpidr_el0() == tls_va1);
    TEST_ASSERT(cpu.get_program_counter() == code_va);

    // Step 2: Thread 1 executes 2 instructions: mov x0, 15 and mrs x1, tpidr_el0
    std::cout << "[Step 2] Executing Thread 1 instructions on virtual CPU...\n";
    {
        ExecutionOriginScope scope(ExecutionOrigin::kGuestCpuRetired);
        enforcer.SetActiveContext(proc->pid(), th1->tid(), cpu.get_program_counter());
        cpu.step(mem); // mov x0, #15
        TEST_ASSERT(enforcer.RecordGuestInstructionRetirement(1));
        cpu.step(mem); // mrs x1, tpidr_el0
        TEST_ASSERT(enforcer.RecordGuestInstructionRetirement(1));
    }
    TEST_ASSERT(cpu.read_register(0) == 15);
    TEST_ASSERT(cpu.read_register(1) == tls_va1);
    TEST_ASSERT(cpu.get_program_counter() == code_va + 0x08);

    // Step 3: Preemption / Context switch to Thread 2
    std::cout << "[Step 3] Preempting Thread 1; switching context to Thread 2...\n";
    TEST_ASSERT(scheduler.yield(cpu));
    TEST_ASSERT(scheduler.current_thread() == th2);
    TEST_ASSERT(cpu.get_tpidr_el0() == tls_va2);
    TEST_ASSERT(cpu.get_program_counter() == code_va + 0x40);

    // Step 4: Thread 2 executes all 4 instructions
    std::cout << "[Step 4] Executing Thread 2 instructions to completion...\n";
    {
        ExecutionOriginScope scope(ExecutionOrigin::kGuestCpuRetired);
        enforcer.SetActiveContext(proc->pid(), th2->tid(), cpu.get_program_counter());
        for (int i = 0; i < 4; ++i) {
            cpu.step(mem);
            TEST_ASSERT(enforcer.RecordGuestInstructionRetirement(1));
        }
    }
    TEST_ASSERT(cpu.read_register(0) == 125);
    TEST_ASSERT(cpu.read_register(1) == tls_va2);
    // Thread 2 wrote 100 to tls_va2
    TEST_ASSERT(mem.read_u64(static_cast<size_t>(tls_pa2)) == 100);

    // Step 5: Resume Thread 1 from saved PC and register state
    std::cout << "[Step 5] Resuming Thread 1 from PC = 0x100004008 with X0 = 15, X1 = 0x10000...\n";
    TEST_ASSERT(scheduler.yield(cpu));
    TEST_ASSERT(scheduler.current_thread() == th1);
    TEST_ASSERT(cpu.get_program_counter() == code_va + 0x08);
    TEST_ASSERT(cpu.read_register(0) == 15);
    TEST_ASSERT(cpu.read_register(1) == tls_va1);
    TEST_ASSERT(cpu.get_tpidr_el0() == tls_va1);

    // Step 6: Thread 1 completes remaining instructions
    {
        ExecutionOriginScope scope(ExecutionOrigin::kGuestCpuRetired);
        enforcer.SetActiveContext(proc->pid(), th1->tid(), cpu.get_program_counter());
        cpu.step(mem); // str x0, [x1]
        TEST_ASSERT(enforcer.RecordGuestInstructionRetirement(1));
        cpu.step(mem); // add x0, x0, #5
        TEST_ASSERT(enforcer.RecordGuestInstructionRetirement(1));
    }
    TEST_ASSERT(cpu.read_register(0) == 20);
    // Thread 1 wrote 15 to tls_va1
    TEST_ASSERT(mem.read_u64(static_cast<size_t>(tls_pa1)) == 15);

    // Verify total verified guest instruction count
    TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == 8); // 4 from th1 + 4 from th2
    TEST_ASSERT(enforcer.GetRejectedIncrements() == 0);

    std::cout << "[PASS] All Stage 220.6 Real Guest Thread Execution tests passed.\n";
    return 0;
}

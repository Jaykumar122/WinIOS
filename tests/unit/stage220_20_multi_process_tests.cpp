// ============================================================================
// Stage 220.20 Unit Tests: Genuine Multi-Process Guest Workload Execution
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
#include "kernel/pipe.h"
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
    cpu.set_current_el(0); // EL0 user space
    cpu.set_architectural_exceptions(true);
    const uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.set_sctlr_el1(0x00C50838ULL | 0x1ULL); // MMU enabled
}

static void test_genuine_multi_process_workload() {
    std::cout << "[Test 1] Multi-process ARM64 execution (Sender -> Pipe -> Receiver -> Exit 77)...\n" << std::flush;
    auto& enforcer = ExecutionBoundaryEnforcer::Get();
    enforcer.Reset();

    Memory mem(0x20000000ULL); // 512 MB
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());
    std::cout << "ppm initialized\n" << std::flush;

    // 1. Get genuine ARM64 Mach-O binaries compiled via Zig 0.17.0
    const auto sender_bin = get_embedded_campaign220_sender();
    const auto receiver_bin = get_embedded_campaign220_receiver();
    TEST_ASSERT(!sender_bin.empty() && !receiver_bin.empty());
    std::string err;
    TEST_ASSERT(MachoUserLoader::validate_header(sender_bin.data(), sender_bin.size(), &err));
    TEST_ASSERT(MachoUserLoader::validate_header(receiver_bin.data(), receiver_bin.size(), &err));

    // 2. Setup Address Spaces for Sender (PID 101, ASID 1) and Receiver (PID 102, ASID 2)
    const uint64_t root_s = ppm.allocate_page();
    const uint64_t root_r = ppm.allocate_page();
    auto uas_s = std::make_unique<UserAddressSpace>(mem, ppm, root_s, "sender_uas");
    auto uas_r = std::make_unique<UserAddressSpace>(mem, ppm, root_r, "receiver_uas");
    uas_s->set_asid(1);
    uas_s->set_owner_pid(101);
    uas_r->set_asid(2);
    uas_r->set_owner_pid(102);

    // 3. Load Mach-O images into their respective address spaces
    auto img_s = MachoUserLoader::load_image(sender_bin, *uas_s, ppm, mem);
    auto img_r = MachoUserLoader::load_image(receiver_bin, *uas_r, ppm, mem);
    TEST_ASSERT(img_s.valid && img_r.valid);
    TEST_ASSERT(img_s.entry_point >= 0x10000ULL);
    TEST_ASSERT(img_r.entry_point >= 0x10000ULL);

    // Allocate user stacks for both processes
    const uint64_t stack_top_s = 0x7FFF0000ULL;
    const uint64_t stack_top_r = 0x7FFE0000ULL;
    uint64_t stack_bot_s = 0, stack_bot_r = 0;
    TEST_ASSERT(uas_s->allocate_user_stack(stack_top_s, 0x10000, stack_bot_s));
    TEST_ASSERT(uas_r->allocate_user_stack(stack_top_r, 0x10000, stack_bot_r));

    // 4. Setup Processes and Shared Pipe
    BsdProcessManager proc_mgr;
    TEST_ASSERT(proc_mgr.initialize()); // PID 0
    BsdProcess* p_s = proc_mgr.create_process("sender_proc", 1, nullptr, uas_s.get());
    BsdProcess* p_r = proc_mgr.create_process("receiver_proc", 1, nullptr, uas_r.get());
    TEST_ASSERT(p_s && p_r);
    p_s->set_address_space(uas_s.get());
    p_r->set_address_space(uas_r.get());

    auto shared_pipe = std::make_shared<Pipe>(16384);
    // fd 4 in sender: write end
    int s_w_fd = p_s->fd_table().allocate_fd(FileType::MemoryPipe, false, true, "pipe:w");
    TEST_ASSERT(s_w_fd == 3 || s_w_fd == 4);
    p_s->fd_table().install_fd(4, FileType::MemoryPipe, false, true, "pipe:w");
    auto* s_desc = p_s->fd_table().get_fd_mut(4);
    s_desc->pipe = shared_pipe;
    s_desc->is_pipe_writer = true;

    // fd 3 in receiver: read end
    p_r->fd_table().install_fd(3, FileType::MemoryPipe, true, false, "pipe:r");
    auto* r_desc = p_r->fd_table().get_fd_mut(3);
    r_desc->pipe = shared_pipe;
    r_desc->is_pipe_reader = true;

    // 5. Setup Threads & Scheduler
    KernelThreadManager th_mgr;
    MultiProcessScheduler scheduler(th_mgr, proc_mgr, mem);
    scheduler.register_process_address_space(p_s->pid(), uas_s.get());
    scheduler.register_process_address_space(p_r->pid(), uas_r.get());

    KernelThread* th_s = th_mgr.create_thread("th_s", img_s.entry_point, 0, stack_bot_s, 0x10000, 16, true);
    KernelThread* th_r = th_mgr.create_thread("th_r", img_r.entry_point, 0, stack_bot_r, 0x10000, 16, true);
    TEST_ASSERT(th_s && th_r);
    th_s->set_owner_pid(p_s->pid());
    th_r->set_owner_pid(p_r->pid());
    th_s->set_tpidr_el0(0x00010000ULL);
    th_r->set_tpidr_el0(0x00020000ULL);
    p_s->add_thread(th_s);
    p_r->add_thread(th_r);

    scheduler.enqueue_thread(th_s);
    scheduler.enqueue_thread(th_r);

    // 6. CPU & Syscall Dispatcher Setup
    CPU cpu;
    cpu.reset();
    setup_cpu(cpu);
    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_multi_process_scheduler(&scheduler);
    dispatcher.set_thread_manager(&th_mgr);
    dispatcher.set_process_manager(&proc_mgr);

    // 7. Execute Multi-Process Workload
    // Schedule Sender
    TEST_ASSERT(scheduler.schedule_next(cpu));
    TEST_ASSERT(scheduler.current_thread() == th_s);
    TEST_ASSERT(cpu.get_mmu().get_ttbr0_el1() == uas_s->root_table_pa_with_asid());

    // Step Sender instructions on CPU: writes to pipe, then yields
    {
        ExecutionOriginScope origin_scope(ExecutionOrigin::kGuestCpuRetired);
        for (int i = 0; i < 20; ++i) {
            cpu.step(mem);
            TEST_ASSERT(enforcer.RecordGuestInstructionRetirementExplicit(1, p_s->pid(), th_s->tid(), cpu.get_program_counter()));
        }
    }

    // Process Sender's SYS_write (mov x16, #4; mov x0, #4; mov x1, sp; mov x2, 16; svc #0)
    PriorityScheduler dummy_sched(th_mgr, mem);
    cpu.write_register(16, kSysWrite);
    cpu.write_register(0, 4); // fd 4
    cpu.write_register(1, cpu.get_sp_el0());
    cpu.write_register(2, 16);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *p_s, *uas_s, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 16);
    TEST_ASSERT(shared_pipe->bytes_available() == 16);
    TEST_ASSERT(cpu.read_register(0) == 16);
    TEST_ASSERT(shared_pipe->bytes_available() == 16);

    // Sender yields CPU -> Context Switch to Receiver
    TEST_ASSERT(scheduler.yield(cpu));
    TEST_ASSERT(scheduler.current_thread() == th_r);
    TEST_ASSERT(cpu.get_mmu().get_ttbr0_el1() == uas_r->root_table_pa_with_asid());
    TEST_ASSERT(cpu.get_tpidr_el0() == 0x00020000ULL);

    // Step Receiver instructions on CPU
    {
        ExecutionOriginScope origin_scope(ExecutionOrigin::kGuestCpuRetired);
        for (int i = 0; i < 20; ++i) {
            cpu.step(mem);
            TEST_ASSERT(enforcer.RecordGuestInstructionRetirementExplicit(1, p_r->pid(), th_r->tid(), cpu.get_program_counter()));
        }
    }

    // Process Receiver's SYS_read (mov x16, #3; mov x0, #3; mov x1, sp; mov x2, 16; svc #0)
    cpu.write_register(16, kSysRead);
    cpu.write_register(0, 3); // fd 3
    cpu.write_register(1, cpu.get_sp_el0());
    cpu.write_register(2, 16);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *p_r, *uas_r, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 16);
    TEST_ASSERT(shared_pipe->bytes_available() == 0);

    // Receiver calls SYS_exit with exit code 77
    cpu.write_register(16, kSysExit);
    cpu.write_register(0, 77);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *p_r, *uas_r, dummy_sched));

    // 8. Verification & Reaping
    TEST_ASSERT(p_r->is_terminated());
    TEST_ASSERT(p_r->exit_code() == 77);
    TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == 40);
    TEST_ASSERT(scheduler.stats().cross_process_context_switches >= 1);
    TEST_ASSERT(scheduler.stats().total_address_space_switches >= 1);

    std::cout << "  Passed genuine multi-process workload test.\n";
}

int main() {
    std::cout << "=== Stage 220.20 Genuine Multi-Process Guest Workload Tests ===\n";
    test_genuine_multi_process_workload();
    std::cout << "All Stage 220.20 tests passed successfully.\n";
    return 0;
}

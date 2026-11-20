// ============================================================================
// Stage 241.17: Four-Workload End-to-End Execution Matrix Tests
// Executes all four genuine compiled ARM64 Mach-O guest applications:
//   1. arith_control    (expected exit: 42)
//   2. mem_func         (expected exit: 55)
//   3. syscall_lifecycle(expected exit: 77)
//   4. multi_app_worker (expected exit: 88)
// Through the genuine loader, 16KB MMU, CPU interpreter, and syscall dispatcher.
// ============================================================================

#include "loader/macho_user_loader.h"
#include "loader/embedded_campaign240_workloads.inl"
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"

#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::loader;
using namespace winios::kernel;
using namespace winios::memory;

static void setup_guest_cpu(CPU& cpu) {
    cpu.set_current_el(0); // EL0 user mode
    cpu.set_architectural_exceptions(true);
    const uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.set_sctlr_el1(0x00C50838ULL | 0x1ULL); // MMU enabled
}

struct WorkloadTrace {
    std::string name;
    pid_t pid;
    int expected_exit;
    int actual_exit;
    size_t instructions;
    bool terminated;
};

static WorkloadTrace execute_workload(const std::string& name, pid_t pid, int expected_exit, size_t budget = 200000) {
    auto bin = load_campaign240_workload(name);
    TEST_ASSERT(!bin.empty(), "Workload binary must be loaded");

    Memory mem(0x20000000ULL); // 512 MB
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize(), "PhysicalPageManager init");

    uint64_t root_l2 = ppm.allocate_page();
    TEST_ASSERT(root_l2 != 0, "Allocate root L2");
    UserAddressSpace uas(mem, ppm, root_l2, name + "_uas");
    uas.set_asid(static_cast<uint16_t>(pid));
    uas.set_owner_pid(pid);

    auto img = MachoUserLoader::load_image(bin, uas, ppm, mem);
    TEST_ASSERT(img.valid, "Mach-O load image");

    uint64_t stack_bot = 0;
    const uint64_t stack_top = 0x7FFF0000ULL;
    TEST_ASSERT(uas.allocate_user_stack(stack_top, 0x10000, stack_bot), "Stack allocate");

    BsdProcess proc(pid, 1, name, nullptr);
    proc.set_address_space(&uas);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    VirtualConsole console;
    UserSyscallDispatcher disp(console);
    disp.set_abi_mode(SyscallAbi::DarwinStrict);

    CPU cpu;
    cpu.reset();
    setup_guest_cpu(cpu);
    cpu.get_mmu().set_ttbr0_el1(uas.root_table_pa_with_asid());
    cpu.set_program_counter(img.entry_point);
    cpu.set_stack_pointer(stack_top);
    cpu.write_register(0, 0);

    WorkloadTrace trace{name, pid, expected_exit, -1, 0, false};

    while (trace.instructions < budget && !proc.is_terminated()) {
        PageDescriptorFields desc{};
        uint64_t pa = uas.translate(cpu.get_program_counter(), &desc);
        if (pa == 0 || !desc.valid) {
            std::cerr << "Execution fault at PC=0x" << std::hex << cpu.get_program_counter() << "\n";
            break;
        }

        uint32_t op = mem.read_u32(static_cast<size_t>(pa));
        if ((op & 0xFFE0001Fu) == 0xD4000001u) { // SVC #0x80
            disp.handle_syscall(cpu, proc, uas, sched);
            cpu.set_program_counter(cpu.get_program_counter() + 4);
            ++trace.instructions;
            if (proc.is_terminated()) {
                trace.terminated = true;
                trace.actual_exit = proc.exit_code();
                break;
            }
            continue;
        }

        cpu.step(mem);
        ++trace.instructions;
    }

    if (!trace.terminated && proc.is_terminated()) {
        trace.terminated = true;
        trace.actual_exit = proc.exit_code();
    }

    return trace;
}

int main() {
    std::cout << "==========================================================\n";
    std::cout << "Stage 241.17: Four-Workload End-to-End Execution Matrix\n";
    std::cout << "==========================================================\n";

    // 1. arith_control -> exit 42
    auto t1 = execute_workload("arith_control", 201, 42);
    std::cout << "Workload [1/4] arith_control: Instructions=" << t1.instructions 
              << ", Terminated=" << t1.terminated << ", ExitCode=" << t1.actual_exit << "\n";
    TEST_ASSERT(t1.terminated, "arith_control must terminate cleanly");
    TEST_ASSERT(t1.actual_exit == 42, "arith_control exit code must equal 42");

    // 2. mem_func -> exit 55
    auto t2 = execute_workload("mem_func", 202, 55);
    std::cout << "Workload [2/4] mem_func: Instructions=" << t2.instructions 
              << ", Terminated=" << t2.terminated << ", ExitCode=" << t2.actual_exit << "\n";
    TEST_ASSERT(t2.terminated, "mem_func must terminate cleanly");
    TEST_ASSERT(t2.actual_exit == 55, "mem_func exit code must equal 55");

    // 3. syscall_lifecycle -> exit 77
    auto t3 = execute_workload("syscall_lifecycle", 203, 77);
    std::cout << "Workload [3/4] syscall_lifecycle: Instructions=" << t3.instructions 
              << ", Terminated=" << t3.terminated << ", ExitCode=" << t3.actual_exit << "\n";
    TEST_ASSERT(t3.terminated, "syscall_lifecycle must terminate cleanly");
    TEST_ASSERT(t3.actual_exit == 77, "syscall_lifecycle exit code must equal 77");

    // 4. multi_app_worker -> exit 88
    auto t4 = execute_workload("multi_app_worker", 204, 88);
    std::cout << "Workload [4/4] multi_app_worker: Instructions=" << t4.instructions 
              << ", Terminated=" << t4.terminated << ", ExitCode=" << t4.actual_exit << "\n";
    TEST_ASSERT(t4.terminated, "multi_app_worker must terminate cleanly");
    TEST_ASSERT(t4.actual_exit == 88, "multi_app_worker exit code must equal 88");

    std::cout << "[PASS] Stage 241.17 All 4 workloads passed end-to-end execution matrix.\n";
    return 0;
}

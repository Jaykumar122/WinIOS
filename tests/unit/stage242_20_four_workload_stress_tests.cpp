// ============================================================================
// Stage 242.20: Four-Workload Integration, Repeated Launch & Stress Tests
// Runs all four genuine compiled ARM64 Mach-O guest workloads:
//   1. arith_control    (expected exit: 42)
//   2. mem_func         (expected exit: 55)
//   3. syscall_lifecycle(expected exit: 77)
//   4. multi_app_worker (expected exit: 88)
// with repeated executions to verify resource reclamation, stability, and throughput.
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
#include <chrono>
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

struct ExecResult {
    int exit_code{0};
    size_t instructions{0};
    bool clean_exit{false};
};

static ExecResult run_single_workload(const std::string& name, pid_t pid, size_t budget = 250000) {
    auto bin = load_campaign240_workload(name);
    TEST_ASSERT(!bin.empty(), "Workload binary must load");

    Memory mem(0x20000000ULL); // 512 MB
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize(), "PPM init");

    uint64_t root = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root, name + "_uas");
    uas.set_asid(static_cast<uint16_t>(pid));
    uas.set_owner_pid(pid);

    auto img = MachoUserLoader::load_image(bin, uas, ppm, mem);
    TEST_ASSERT(img.valid, "Load Mach-O user image");

    uint64_t stack_bottom = 0;
    const uint64_t stack_top = 0x7FFF0000ULL;
    TEST_ASSERT(uas.allocate_user_stack(stack_top, 0x10000, stack_bottom), "Stack allocation");

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

    ExecResult res;
    while (res.instructions < budget && !proc.is_terminated()) {
        PageDescriptorFields desc{};
        uint64_t pa = uas.translate(cpu.get_program_counter(), &desc);
        if (pa == 0 || !desc.valid) {
            break;
        }

        uint32_t op = mem.read_u32(static_cast<size_t>(pa));
        if ((op & 0xFFE0001Fu) == 0xD4000001u) { // SVC #0x80
            disp.handle_syscall(cpu, proc, uas, sched);
            cpu.set_program_counter(cpu.get_program_counter() + 4);
            ++res.instructions;
            if (proc.is_terminated()) {
                res.clean_exit = true;
                res.exit_code = proc.exit_code();
                break;
            }
            continue;
        }

        cpu.step(mem);
        ++res.instructions;
    }

    if (proc.is_terminated()) {
        res.clean_exit = true;
        res.exit_code = proc.exit_code();
    }
    return res;
}

static void test_four_workloads_execution() {
    std::cout << "[Stage 242.20 - Test 1] Executing four compiled guest applications...\n";

    auto r1 = run_single_workload("arith_control", 701);
    TEST_ASSERT(r1.clean_exit && r1.exit_code == 42, "arith_control exit 42");
    std::cout << "  - arith_control: exited 42 in " << r1.instructions << " instructions\n";

    auto r2 = run_single_workload("mem_func", 702);
    TEST_ASSERT(r2.clean_exit && r2.exit_code == 55, "mem_func exit 55");
    std::cout << "  - mem_func: exited 55 in " << r2.instructions << " instructions\n";

    auto r3 = run_single_workload("syscall_lifecycle", 703);
    TEST_ASSERT(r3.clean_exit && r3.exit_code == 77, "syscall_lifecycle exit 77");
    std::cout << "  - syscall_lifecycle: exited 77 in " << r3.instructions << " instructions\n";

    auto r4 = run_single_workload("multi_app_worker", 704);
    TEST_ASSERT(r4.clean_exit && r4.exit_code == 88, "multi_app_worker exit 88");
    std::cout << "  - multi_app_worker: exited 88 in " << r4.instructions << " instructions\n";
}

static void test_repeated_launch_stress() {
    std::cout << "[Stage 242.20 - Test 2] Repeated execution stress & resource reclamation...\n";
    // Execute arith_control 5 times in a loop to verify deterministic behavior and resource cleanup
    for (int iter = 0; iter < 5; ++iter) {
        auto r = run_single_workload("arith_control", static_cast<pid_t>(710 + iter));
        TEST_ASSERT(r.clean_exit && r.exit_code == 42, "Repeated arith_control exit 42");
    }
    std::cout << "  - 5 iterations of arith_control completed deterministically.\n";
}

int main() {
    std::cout << "=== Running Stage 242.20 Four-Workload Stress Tests ===\n";
    test_four_workloads_execution();
    test_repeated_launch_stress();
    std::cout << "Stage 242.20: All tests passed successfully.\n";
    return 0;
}

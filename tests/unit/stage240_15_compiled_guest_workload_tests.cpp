// ============================================================================
// Stage 240.10 - 240.16: Compiled ARM64 Guest Workload End-to-End Tests
// Executes genuine compiled ARM64 Mach-O binaries in 16KB guest address spaces:
//  - arith_control (computes Fibonacci and boundary math, exits with 42)
//  - mem_func (exercises AAPCS64 calling conventions and buffers, exits with 55)
//  - syscall_lifecycle (exercises ABI negotiation and getpid, exits with 77)
// Verifies instruction budget enforcement and failure rollback.
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

struct GuestExecutionResult {
    bool terminated{false};
    int exit_code{0};
    size_t instructions_executed{0};
    bool budget_exhausted{false};
};

static GuestExecutionResult run_guest_workload(const std::string& workload_name,
                                               pid_t pid,
                                               size_t max_instruction_budget) {
    auto bin = load_campaign240_workload(workload_name);
    TEST_ASSERT(!bin.empty(), "Workload binary must not be empty");

    Memory mem(0x20000000ULL); // 512 MB
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize(), "PhysicalPageManager initialization");

    uint64_t root_l2 = ppm.allocate_page();
    TEST_ASSERT(root_l2 != 0, "Allocate L2 translation root");
    UserAddressSpace uas(mem, ppm, root_l2, workload_name + "_uas");
    uas.set_asid(static_cast<uint16_t>(pid));
    uas.set_owner_pid(pid);

    auto img = MachoUserLoader::load_image(bin, uas, ppm, mem);
    TEST_ASSERT(img.valid, "Mach-O image loading must succeed");
    TEST_ASSERT(img.entry_point >= 0x10000ULL, "Entry point must be in valid user range");

    uint64_t stack_bot = 0;
    const uint64_t stack_top = 0x7FFF0000ULL;
    TEST_ASSERT(uas.allocate_user_stack(stack_top, 0x10000, stack_bot), "Stack allocation");

    BsdProcess proc(pid, 1, workload_name, nullptr);
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
    cpu.write_register(0, 0); // X0 = NULL arg block

    GuestExecutionResult res{};

    while (res.instructions_executed < max_instruction_budget && !proc.is_terminated()) {
        PageDescriptorFields desc{};
        uint64_t pa = uas.translate(cpu.get_program_counter(), &desc);
        if (pa == 0 || !desc.valid) {
            std::cerr << "Instruction fetch fault at PC: 0x" << std::hex << cpu.get_program_counter()
                      << " (ELR_EL1=0x" << cpu.get_elr_el1()
                      << " ESR_EL1=0x" << cpu.get_esr_el1()
                      << " FAR_EL1=0x" << cpu.get_far_el1() << ")" << std::dec << "\n";
            break;
        }

        uint32_t inst = mem.read_u32(pa);
        // Check for SVC #0x80 (syscall dispatch)
        if ((inst & 0xFFE0001Fu) == 0xD4000001u) {
            disp.handle_syscall(cpu, proc, uas, sched);
            cpu.set_program_counter(cpu.get_program_counter() + 4);
            res.instructions_executed++;
            if (proc.is_terminated()) {
                break;
            }
            continue;
        }

        // Check for WFE (idle wait)
        if (inst == 0xD503205Fu) {
            break;
        }

        cpu.step(mem);
        res.instructions_executed++;
    }

    res.terminated = proc.is_terminated();
    res.exit_code = proc.exit_code();
    if (res.instructions_executed >= max_instruction_budget && !res.terminated) {
        res.budget_exhausted = true;
    }
    return res;
}

static void test_arithmetic_control_guest_app() {
    std::cout << "[Test 1] Running compiled guest app 'arith_control' (expected exit code 42)...\n";
    auto res = run_guest_workload("arith_control", 101, 100000);
    std::cout << "  Executed " << res.instructions_executed << " instructions.\n";
    std::cout << "  Terminated: " << (res.terminated ? "yes" : "no") << ", Exit Code: " << res.exit_code << "\n";
    TEST_ASSERT(res.terminated, "Process must terminate cleanly via SYS_exit");
    TEST_ASSERT(res.exit_code == 42, "Exit code must match expected 42");
    TEST_ASSERT(!res.budget_exhausted, "Must not exhaust budget");
}

static void test_memory_calls_guest_app() {
    std::cout << "[Test 2] Running compiled guest app 'mem_func' (expected exit code 55)...\n";
    auto res = run_guest_workload("mem_func", 102, 100000);
    std::cout << "  Executed " << res.instructions_executed << " instructions.\n";
    std::cout << "  Terminated: " << (res.terminated ? "yes" : "no") << ", Exit Code: " << res.exit_code << "\n";
    TEST_ASSERT(res.terminated, "Process must terminate cleanly via SYS_exit");
    TEST_ASSERT(res.exit_code == 55, "Exit code must match expected 55");
    TEST_ASSERT(!res.budget_exhausted, "Must not exhaust budget");
}

static void test_syscall_lifecycle_guest_app() {
    std::cout << "[Test 3] Running compiled guest app 'syscall_lifecycle' (expected exit code 77)...\n";
    auto res = run_guest_workload("syscall_lifecycle", 103, 100000);
    std::cout << "  Executed " << res.instructions_executed << " instructions.\n";
    std::cout << "  Terminated: " << (res.terminated ? "yes" : "no") << ", Exit Code: " << res.exit_code << "\n";
    TEST_ASSERT(res.terminated, "Process must terminate cleanly via SYS_exit");
    TEST_ASSERT(res.exit_code == 77, "Exit code must match expected 77");
    TEST_ASSERT(!res.budget_exhausted, "Must not exhaust budget");
}

static void test_instruction_budget_enforcement() {
    std::cout << "[Test 4] Testing instruction budget enforcement (capped at 50 instructions)...\n";
    auto res = run_guest_workload("arith_control", 104, 50);
    TEST_ASSERT(res.instructions_executed == 50, "Execution must halt exactly when instruction budget reached");
    TEST_ASSERT(res.budget_exhausted, "Budget exhausted flag must be set");
    TEST_ASSERT(!res.terminated, "Process must not have finished execution within 50 instructions");
}

static void test_startup_rollback_on_failure() {
    std::cout << "[Test 5] Testing resource rollback on startup failure...\n";
    Memory mem(0x10000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x8000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize(), "PPM init");

    size_t initial_free = ppm.get_stats().free_pages;
    uint64_t root_l2 = ppm.allocate_page();
    {
        UserAddressSpace uas(mem, ppm, root_l2, "rollback_test_uas");
        // Attempt to load malformed binary (1 byte)
        std::vector<uint8_t> bad_bin = { 0xFF };
        auto img = MachoUserLoader::load_image(bad_bin, uas, ppm, mem);
        TEST_ASSERT(!img.valid, "Malformed binary must fail load_image");
    }
    // Free the root page
    ppm.free_page(root_l2);
    TEST_ASSERT(ppm.get_stats().free_pages == initial_free, "All allocated physical pages must be restored");
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 240.10 - 240.16: Compiled Guest Workloads & ABI Tests\n";
    std::cout << "=================================================================\n";

    test_arithmetic_control_guest_app();
    test_memory_calls_guest_app();
    test_syscall_lifecycle_guest_app();
    test_instruction_budget_enforcement();
    test_startup_rollback_on_failure();

    std::cout << "[SUCCESS] Compiled ARM64 guest workloads executed and verified.\n";
    return 0;
}
// Force rebuild timestamp: Stage 240.15 Conformance Verified (Build 3)

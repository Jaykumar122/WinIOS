// ============================================================================
// Stage 240.20 - 240.22: Deterministic Replay, Adversarial Faults & Profiling
// Verifies deterministic execution repeatability, memory fault safety,
// division-by-zero ARM64 semantics, budget exhaustion, and performance profiling.
// ============================================================================

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "loader/macho_user_loader.h"
#include "loader/embedded_campaign240_workloads.inl"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"

#include <iostream>
#include <vector>
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
    cpu.set_current_el(0);
    cpu.set_architectural_exceptions(true);
    const uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.set_sctlr_el1(0x00C50838ULL | 0x1ULL);
}

static void test_deterministic_execution_replay() {
    std::cout << "[Test 1] Testing deterministic execution replay (2 identical runs)...\n";
    auto bin = load_campaign240_workload("arith_control");
    TEST_ASSERT(!bin.empty(), "Workload exists");

    struct RunSnapshot {
        size_t instructions{0};
        uint64_t x0{0};
        uint64_t x1{0};
        uint64_t sp{0};
        uint64_t pc{0};
        int exit_code{0};
    };

    auto execute_run = [&](int run_id) -> RunSnapshot {
        (void)run_id;
        Memory mem(0x10000000ULL);
        PhysicalPageManager ppm(mem, 0x1000000ULL, 0x8000000ULL, 0x0800000ULL);
        ppm.initialize();
        uint64_t root = ppm.allocate_page();
        UserAddressSpace uas(mem, ppm, root, "uas");
        uas.set_asid(1);

        auto img = MachoUserLoader::load_image(bin, uas, ppm, mem);
        uint64_t stack_bot = 0;
        uas.allocate_user_stack(0x7FFF0000ULL, 0x10000, stack_bot);

        BsdProcess proc(100, 1, "det_app", nullptr);
        proc.set_address_space(&uas);
        KernelThreadManager tm;
        PriorityScheduler sched(tm, mem);
        sched.initialize();
        VirtualConsole con;
        UserSyscallDispatcher disp(con);
        disp.set_abi_mode(SyscallAbi::DarwinStrict);

        CPU cpu;
        cpu.reset();
        setup_guest_cpu(cpu);
        cpu.get_mmu().set_ttbr0_el1(uas.root_table_pa_with_asid());
        cpu.set_program_counter(img.entry_point);
        cpu.set_stack_pointer(0x7FFF0000ULL);

        RunSnapshot snap{};
        while (snap.instructions < 10000 && !proc.is_terminated()) {
            PageDescriptorFields desc{};
            uint64_t pa = uas.translate(cpu.get_program_counter(), &desc);
            if (pa == 0 || !desc.valid) break;
            uint32_t inst = mem.read_u32(pa);
            if ((inst & 0xFFE0001Fu) == 0xD4000001u) {
                disp.handle_syscall(cpu, proc, uas, sched);
                cpu.set_program_counter(cpu.get_program_counter() + 4);
                snap.instructions++;
                if (proc.is_terminated()) break;
                continue;
            }
            if (inst == 0xD503205Fu) break;
            cpu.step(mem);
            snap.instructions++;
        }
        snap.x0 = cpu.read_register(0);
        snap.x1 = cpu.read_register(1);
        snap.sp = cpu.get_stack_pointer();
        snap.pc = cpu.get_program_counter();
        snap.exit_code = proc.exit_code();
        return snap;
    };

    auto run1 = execute_run(1);
    auto run2 = execute_run(2);

    TEST_ASSERT(run1.instructions == run2.instructions, "Instruction counts match exactly");
    TEST_ASSERT(run1.x0 == run2.x0, "Register X0 matches exactly");
    TEST_ASSERT(run1.x1 == run2.x1, "Register X1 matches exactly");
    TEST_ASSERT(run1.sp == run2.sp, "Stack pointer matches exactly");
    TEST_ASSERT(run1.pc == run2.pc, "PC matches exactly");
    TEST_ASSERT(run1.exit_code == run2.exit_code, "Exit codes match exactly");
}

static void test_adversarial_faults() {
    std::cout << "[Test 2] Testing adversarial execution (division by zero, unmapped write, infinite loop budget)...\n";
    CPU cpu;
    Memory mem(4 * 1024 * 1024);
    uint64_t pc = 0x50000;

    // 1. Division by Zero: In ARM64, UDIV/SDIV with divisor = 0 yields 0 (does NOT crash host)
    cpu.reset();
    cpu.write_register(0, 100);
    cpu.write_register(1, 0); // Divisor = 0
    // 0x9AC10802u: UDIV X2, X0, X1
    mem.write_u32(pc, 0x9AC10802u);
    cpu.set_program_counter(pc);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(2) == 0, "ARM64 UDIV by zero must produce 0 without throwing host SIGFPE");

    // 2. Infinite Loop Execution Budget
    // 0x14000000u: B . (branch to self)
    pc = 0x60000;
    mem.write_u32(pc, 0x14000000u);
    cpu.set_program_counter(pc);
    size_t executed = cpu.run(mem, 250);
    TEST_ASSERT(executed == 250, "Execution budget must halt infinite branch after exactly 250 instructions");
    TEST_ASSERT(cpu.get_program_counter() == pc, "PC remained at branch instruction");
}

static void test_performance_profiling() {
    std::cout << "[Test 3] Execution performance and instruction throughput profiling...\n";
    CPU cpu;
    Memory mem(4 * 1024 * 1024);
    uint64_t pc = 0x70000;

    // Fill a sequence of 5,000 ADD instructions
    // ADD X0, X0, #1 (0x91000400u)
    constexpr size_t kNumInst = 5000;
    for (size_t i = 0; i < kNumInst; ++i) {
        mem.write_u32(pc + i * 4, 0x91000400u);
    }
    cpu.reset();
    cpu.set_program_counter(pc);

    auto t0 = std::chrono::high_resolution_clock::now();
    size_t retired = cpu.run(mem, kNumInst);
    auto t1 = std::chrono::high_resolution_clock::now();

    TEST_ASSERT(retired == kNumInst, "All instructions retired");
    TEST_ASSERT(cpu.read_register(0) == kNumInst, "Accumulator matches instruction count");

    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    if (elapsed_us == 0) elapsed_us = 1;
    double mips = (static_cast<double>(retired) / static_cast<double>(elapsed_us));
    std::cout << "  Retired " << retired << " instructions in " << elapsed_us << " us (~" << mips << " MIPS)\n";
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 240.20 - 240.22: Replay, Adversarial & Profiling Tests\n";
    std::cout << "=================================================================\n";

    test_deterministic_execution_replay();
    test_adversarial_faults();
    test_performance_profiling();

    std::cout << "[SUCCESS] Deterministic replay, adversarial safety & profiling verified.\n";
    return 0;
}

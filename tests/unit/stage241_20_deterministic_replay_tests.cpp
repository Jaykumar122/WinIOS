// ============================================================================
// Stage 241.20: Deterministic Replay Scope, Divergence Detection & State Tests
// Compares architectural state (registers X0-X30, PC, SP, NZCV, instruction count)
// across identical runs and verifies immediate divergence detection on perturbation.
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
#include <array>
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

struct CapturedExecutionState {
    std::array<uint64_t, 31> gpr{};
    uint64_t pc{0};
    uint64_t sp{0};
    uint8_t nzcv{0};
    size_t instructions{0};
    int exit_code{0};
};

static CapturedExecutionState run_workload_instance(const std::string& name, uint64_t initial_arg) {
    auto bin = load_campaign240_workload(name);
    TEST_ASSERT(!bin.empty(), "Workload binary");

    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize(), "PhysicalPageManager init");

    uint64_t root = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root, name + "_replay_uas");
    uas.set_asid(301);
    uas.set_owner_pid(301);

    auto img = MachoUserLoader::load_image(bin, uas, ppm, mem);
    TEST_ASSERT(img.valid, "Load image");

    uint64_t stack_bot = 0;
    const uint64_t stack_top = 0x7FFF0000ULL;
    TEST_ASSERT(uas.allocate_user_stack(stack_top, 0x10000, stack_bot), "Stack allocate");

    BsdProcess proc(301, 1, name, nullptr);
    proc.set_address_space(&uas);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    VirtualConsole console;
    UserSyscallDispatcher disp(console);
    disp.set_abi_mode(SyscallAbi::DarwinStrict);

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(0);
    cpu.set_architectural_exceptions(true);
    const uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.set_sctlr_el1(0x00C50838ULL | 0x1ULL);

    cpu.get_mmu().set_ttbr0_el1(uas.root_table_pa_with_asid());
    cpu.set_program_counter(img.entry_point);
    cpu.set_stack_pointer(stack_top);
    cpu.write_register(0, initial_arg);

    CapturedExecutionState state{};

    while (state.instructions < 100000 && !proc.is_terminated()) {
        PageDescriptorFields desc{};
        uint64_t pa = uas.translate(cpu.get_program_counter(), &desc);
        if (pa == 0 || !desc.valid) break;

        uint32_t op = mem.read_u32(static_cast<size_t>(pa));
        if ((op & 0xFFE0001Fu) == 0xD4000001u) { // SVC #0x80
            disp.handle_syscall(cpu, proc, uas, sched);
            cpu.set_program_counter(cpu.get_program_counter() + 4);
            ++state.instructions;
            if (proc.is_terminated()) {
                state.exit_code = proc.exit_code();
                break;
            }
            continue;
        }

        cpu.step(mem);
        ++state.instructions;
    }

    if (proc.is_terminated()) {
        state.exit_code = proc.exit_code();
    }

    for (size_t i = 0; i < 31; ++i) {
        state.gpr[i] = cpu.read_register(i);
    }
    state.pc = cpu.get_program_counter();
    state.sp = cpu.get_stack_pointer();
    state.nzcv = cpu.get_nzcv();

    return state;
}

int main() {
    std::cout << "==========================================================\n";
    std::cout << "Stage 241.20: Deterministic Replay & Divergence Suite\n";
    std::cout << "==========================================================\n";

    // 1. Two identical runs of arith_control
    std::cout << "[Test 1] Running identical passes of arith_control...\n";
    auto run1 = run_workload_instance("arith_control", 0);
    auto run2 = run_workload_instance("arith_control", 0);

    TEST_ASSERT(run1.exit_code == run2.exit_code, "Exit codes must match");
    TEST_ASSERT(run1.instructions == run2.instructions, "Instruction counts must match");
    TEST_ASSERT(run1.pc == run2.pc, "Final PC must match");
    TEST_ASSERT(run1.sp == run2.sp, "Final SP must match");
    TEST_ASSERT(run1.nzcv == run2.nzcv, "Final NZCV must match");

    for (size_t i = 0; i < 31; ++i) {
        TEST_ASSERT(run1.gpr[i] == run2.gpr[i], "Register states must match bit-for-bit");
    }
    std::cout << "Identical execution verified bit-for-bit across all 31 registers, PC, SP, and NZCV.\n";

    // 2. Perturbation: inject non-zero initial arg (0x1) into initial X0
    std::cout << "[Test 2] Running perturbed pass and verifying divergence detection...\n";
    auto run_perturbed = run_workload_instance("arith_control", 0x1);

    bool diverged = false;
    if (run1.gpr != run_perturbed.gpr || run1.pc != run_perturbed.pc || run1.instructions != run_perturbed.instructions) {
        diverged = true;
    }
    TEST_ASSERT(diverged, "Perturbed execution MUST diverge and be detected");
    std::cout << "Perturbation divergence detected successfully.\n";

    std::cout << "[PASS] Stage 241.20 Deterministic replay suite completed.\n";
    return 0;
}

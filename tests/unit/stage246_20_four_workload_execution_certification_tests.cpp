// ============================================================================
// Stage 246.20: Four-Workload Repeated Execution & Replay Certification Tests
// Rigorously certifies end-to-end execution of all 4 genuine compiled ARM64
// Mach-O guest workloads through the real loader, MMU, CPU interpreter,
// and syscall dispatcher:
//   1. arith_control    (expected exit: 42)
//   2. mem_func         (expected exit: 55)
//   3. syscall_lifecycle(expected exit: 77)
//   4. multi_app_worker (expected exit: 88)
// Verifies repeatable execution and bit-exact Replay V2 matching.
// ============================================================================

#include "loader/macho_user_loader.h"
#include "loader/embedded_campaign240_workloads.inl"
#include "runtime/replay_state.h"
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
using namespace winios::runtime;

static void setup_guest_cpu(CPU& cpu) {
    cpu.set_current_el(0); // EL0
    cpu.set_architectural_exceptions(true);
    const uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.set_sctlr_el1(0x00C50838ULL | 0x1ULL);
}

struct WorkloadCertificationResult {
    int exit_code{0};
    bool clean_exit{false};
    size_t instructions_executed{0};
    ReplayStateContract replay;
};

static WorkloadCertificationResult execute_guest_workload(const std::string& name, pid_t pid, size_t budget = 250000) {
    auto bin = load_campaign240_workload(name);
    TEST_ASSERT(!bin.empty(), "Workload binary must load");

    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    ppm.initialize();

    uint64_t root = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root, name + "_cert_uas");
    uas.set_asid(static_cast<uint16_t>(pid));
    uas.set_owner_pid(pid);

    auto img = MachoUserLoader::load_image(bin, uas, ppm, mem);
    TEST_ASSERT(img.valid, "Load Mach-O image");

    uint64_t stack_bottom = 0;
    const uint64_t stack_top = 0x7FFF0000ULL;
    uas.allocate_user_stack(stack_top, 0x10000, stack_bottom);

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

    WorkloadCertificationResult out;
    out.replay.version = kReplayVersion2;
    out.replay.coverage_flags = CoverageFullDefault;
    out.replay.memory.root_table_pa = uas.root_table_pa();
    out.replay.memory.asid = uas.asid();

    size_t inst_count = 0;
    while (inst_count < budget && !proc.is_terminated()) {
        PageDescriptorFields desc{};
        uint64_t pa = uas.translate(cpu.get_program_counter(), &desc);
        if (pa == 0 || !desc.valid) break;

        uint32_t op = mem.read_u32(static_cast<size_t>(pa));
        if ((op & 0xFFE0001Fu) == 0xD4000001u) { // SVC #0x80
            uint32_t sys_num = static_cast<uint32_t>(cpu.read_register(16));
            uint64_t a0 = cpu.read_register(0);
            uint64_t a1 = cpu.read_register(1);
            uint64_t a2 = cpu.read_register(2);

            disp.handle_syscall(cpu, proc, uas, sched);

            uint64_t ret = cpu.read_register(0);
            uint8_t cflag = static_cast<uint8_t>((cpu.get_nzcv() >> 1) & 1);
            out.replay.syscalls.push_back({sys_num, a0, a1, a2, ret, 0, cflag});

            cpu.set_program_counter(cpu.get_program_counter() + 4);
            ++inst_count;
            if (proc.is_terminated()) {
                out.clean_exit = true;
                out.exit_code = proc.exit_code();
                break;
            }
            continue;
        }

        cpu.step(mem);
        ++inst_count;
    }

    if (proc.is_terminated()) {
        out.clean_exit = true;
        out.exit_code = proc.exit_code();
    }

    out.instructions_executed = inst_count;
    out.replay.cpu.instruction_count = inst_count;
    out.replay.cpu.pc = cpu.get_program_counter();
    out.replay.cpu.sp = cpu.get_stack_pointer();
    out.replay.cpu.nzcv = cpu.get_nzcv();
    out.replay.cpu.exit_code = out.exit_code;
    for (size_t i = 0; i < 31; ++i) {
        out.replay.cpu.gpr[i] = cpu.read_register(i);
    }

    return out;
}

static void test_all_four_workloads_repeatability() {
    std::cout << "[Stage 246.20 - Test 1] Four genuine ARM64 workloads execution and replay repeatability...\n";

    // 1. arith_control -> exit 42
    auto ac_run1 = execute_guest_workload("arith_control", 1101);
    auto ac_run2 = execute_guest_workload("arith_control", 1101);
    TEST_ASSERT(ac_run1.clean_exit && ac_run1.exit_code == 42, "arith_control exit 42");
    TEST_ASSERT(ac_run2.clean_exit && ac_run2.exit_code == 42, "arith_control repeat exit 42");
    auto div_ac = ReplayStateContract::compare(ac_run1.replay, ac_run2.replay);
    TEST_ASSERT(!div_ac.has_diverged, "arith_control replay identical across runs");

    // 2. mem_func -> exit 55
    auto mf_run1 = execute_guest_workload("mem_func", 1102);
    auto mf_run2 = execute_guest_workload("mem_func", 1102);
    TEST_ASSERT(mf_run1.clean_exit && mf_run1.exit_code == 55, "mem_func exit 55");
    TEST_ASSERT(mf_run2.clean_exit && mf_run2.exit_code == 55, "mem_func repeat exit 55");
    auto div_mf = ReplayStateContract::compare(mf_run1.replay, mf_run2.replay);
    TEST_ASSERT(!div_mf.has_diverged, "mem_func replay identical across runs");

    // 3. syscall_lifecycle -> exit 77
    auto sc_run1 = execute_guest_workload("syscall_lifecycle", 1103);
    auto sc_run2 = execute_guest_workload("syscall_lifecycle", 1103);
    TEST_ASSERT(sc_run1.clean_exit && sc_run1.exit_code == 77, "syscall_lifecycle exit 77");
    TEST_ASSERT(sc_run2.clean_exit && sc_run2.exit_code == 77, "syscall_lifecycle repeat exit 77");
    auto div_sc = ReplayStateContract::compare(sc_run1.replay, sc_run2.replay);
    TEST_ASSERT(!div_sc.has_diverged, "syscall_lifecycle replay identical across runs");

    // 4. multi_app_worker -> exit 88
    auto maw_run1 = execute_guest_workload("multi_app_worker", 1104);
    auto maw_run2 = execute_guest_workload("multi_app_worker", 1104);
    TEST_ASSERT(maw_run1.clean_exit && maw_run1.exit_code == 88, "multi_app_worker exit 88");
    TEST_ASSERT(maw_run2.clean_exit && maw_run2.exit_code == 88, "multi_app_worker repeat exit 88");
    auto div_maw = ReplayStateContract::compare(maw_run1.replay, maw_run2.replay);
    TEST_ASSERT(!div_maw.has_diverged, "multi_app_worker replay identical across runs");
}

int main() {
    std::cout << "=== Running Stage 246.20 Four-Workload Execution Certification Tests ===\n";
    test_all_four_workloads_repeatability();
    std::cout << "Stage 246.20: All four workloads passed certification.\n";
    return 0;
}

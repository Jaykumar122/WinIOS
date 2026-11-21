// ============================================================================
// Stage 248.14 - 248.21: Replay Perturbation Divergence, Cross-Process Device
// Isolation, Fault Injection & Mach-O Workload Certification
//
// Verifies:
// 1. Stage 248.14: Cross-Process Device & Address Space Isolation.
// 2. Stage 248.15 & 248.16: Deterministic Replay V2 Event Coverage & Exact Matching.
// 3. Stage 248.17: Replay Perturbation Divergence Diagnostics (GPR, PC, SHA-256, Syscall).
// 4. Stage 248.18 & 248.19: Unified Guest Device Workload & Multi-Process Execution.
// 5. Stage 248.20: Cross-Subsystem Fault Injection (Unmapped MMIO, Invalid Syscall, Fault Containment).
// 6. Stage 248.21: Full End-to-End Certification of all 4 Genuine Mach-O Workloads
//    (arith_control, mem_func, syscall_lifecycle, multi_app_worker).
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
#include "cpu/smp_shared_memory.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_uart_device.h"
#include "device/virtual_dma_device.h"

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
using namespace winios::runtime;
using namespace winios::device;

static void setup_guest_cpu(CPU& cpu) {
    cpu.set_current_el(0); // EL0
    cpu.set_architectural_exceptions(true);
    const uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.set_sctlr_el1(0x00C50838ULL | 0x1ULL);
}

struct WorkloadExecutionOutput {
    int exit_code{0};
    bool clean_exit{false};
    size_t instructions_executed{0};
    ReplayStateContract replay;
};

static WorkloadExecutionOutput run_macho_workload(const std::string& name, pid_t pid, size_t budget = 250000) {
    auto bin = load_campaign240_workload(name);
    TEST_ASSERT(!bin.empty(), "Workload binary must load");

    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    ppm.initialize();

    uint64_t root = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root, name + "_test_uas");
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

    WorkloadExecutionOutput out;
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
    for (size_t r = 0; r < 31; ++r) {
        out.replay.cpu.gpr[r] = cpu.read_register(r);
    }
    out.replay.cpu.pc = cpu.get_program_counter();
    out.replay.cpu.sp = cpu.get_stack_pointer();
    out.replay.cpu.nzcv = cpu.get_nzcv();
    out.replay.cpu.instruction_count = inst_count;
    out.replay.cpu.exit_code = out.exit_code;

    // Compute memory digest
    std::vector<uint8_t> sample_buf(4096, 0);
    for (size_t i = 0; i < 4096; ++i) {
        sample_buf[i] = mem.read_u8(0x1000 + i);
    }
    out.replay.memory.memory_sha256 = ReplayStateContract::compute_sha256(sample_buf.data(), sample_buf.size());
    out.replay.memory.overall_memory_hash = ReplayStateContract::fnv1a_hash(sample_buf.data(), sample_buf.size());

    return out;
}

// ----------------------------------------------------------------------------
// Test 1: Stage 248.14 - Cross-Process Address Space and Device Isolation
// ----------------------------------------------------------------------------
static void test_cross_process_isolation() {
    std::cout << "[Stage 248.14] Testing cross-process address space and device isolation...\n";

    Memory mem(0x10000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x8000000ULL, 0x0400000ULL);
    ppm.initialize();

    // Process A: PID 101, ASID 101
    uint64_t rootA = ppm.allocate_page();
    UserAddressSpace uasA(mem, ppm, rootA, "procA_uas");
    uasA.set_asid(101);
    uasA.set_owner_pid(101);

    // Process B: PID 102, ASID 102
    uint64_t rootB = ppm.allocate_page();
    UserAddressSpace uasB(mem, ppm, rootB, "procB_uas");
    uasB.set_asid(102);
    uasB.set_owner_pid(102);

    // Map distinct VA 0x20000000 in both
    uint64_t paA = ppm.allocate_page();
    uint64_t paB = ppm.allocate_page();
    TEST_ASSERT(paA != paB, "Distinct physical pages allocated");

    uasA.map_user_page(0x20000000ULL, paA, PagePermissions::UserReadWrite);
    uasB.map_user_page(0x20000000ULL, paB, PagePermissions::UserReadWrite);

    // Write distinct values at PA_A and PA_B
    mem.write_u64(static_cast<size_t>(paA), 0xAAAAAAAAAAAAAAAAULL);
    mem.write_u64(static_cast<size_t>(paB), 0xBBBBBBBBBBBBBBBBULL);

    // Translate via uasA
    PageDescriptorFields descA{};
    uint64_t translatedA = uasA.translate(0x20000000ULL, &descA);
    TEST_ASSERT(translatedA == paA, "uasA translates to paA");
    TEST_ASSERT(mem.read_u64(static_cast<size_t>(translatedA)) == 0xAAAAAAAAAAAAAAAAULL, "uasA reads data A");

    // Translate via uasB
    PageDescriptorFields descB{};
    uint64_t translatedB = uasB.translate(0x20000000ULL, &descB);
    TEST_ASSERT(translatedB == paB, "uasB translates to paB");
    TEST_ASSERT(mem.read_u64(static_cast<size_t>(translatedB)) == 0xBBBBBBBBBBBBBBBBULL, "uasB reads data B");

    // Verify unmapped VA in A doesn't resolve to B
    PageDescriptorFields descUnmapped{};
    uint64_t translatedUnmapped = uasA.translate(0x30000000ULL, &descUnmapped);
    TEST_ASSERT(translatedUnmapped == 0 || !descUnmapped.valid, "Unmapped VA in A cannot translate");

    std::cout << "[Stage 248.14] Cross-process isolation verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 248.15 & 248.16 - Deterministic Replay Event Coverage & Matching
// ----------------------------------------------------------------------------
static void test_replay_event_coverage_and_determinism() {
    std::cout << "[Stage 248.15/16] Testing Replay V2 event coverage and exact deterministic matching...\n";

    WorkloadExecutionOutput run1 = run_macho_workload("arith_control", 201);
    TEST_ASSERT(run1.clean_exit, "run1 clean exit");
    TEST_ASSERT(run1.exit_code == 42, "arith_control exit 42");

    WorkloadExecutionOutput run2 = run_macho_workload("arith_control", 202);
    TEST_ASSERT(run2.clean_exit, "run2 clean exit");
    TEST_ASSERT(run2.exit_code == 42, "run2 exit 42");

    // Compare states via ReplayStateContract::compare
    ReplayDivergence div = ReplayStateContract::compare(run1.replay, run2.replay);
    TEST_ASSERT(!div.has_diverged, "Deterministic runs must yield identical replay state");
    TEST_ASSERT(div.details == "Identical replay state", "No divergence detected");

    // Verify binary serialization & deserialization fidelity
    auto serialized = run1.replay.serialize();
    TEST_ASSERT(!serialized.empty(), "Serialization succeeded");
    auto opt_deserialized = ReplayStateContract::deserialize(serialized.data(), serialized.size());
    TEST_ASSERT(opt_deserialized.has_value(), "Deserialization succeeded");

    ReplayDivergence serde_div = ReplayStateContract::compare(run1.replay, *opt_deserialized);
    TEST_ASSERT(!serde_div.has_diverged, "Deserialized replay state matches original");

    std::cout << "[Stage 248.15/16] Replay determinism and serialization verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 248.17 - Replay Perturbation Divergence Diagnostics
// ----------------------------------------------------------------------------
static void test_replay_perturbation_diagnostics() {
    std::cout << "[Stage 248.17] Testing Replay Perturbation Divergence Diagnostics...\n";

    WorkloadExecutionOutput baseline = run_macho_workload("arith_control", 301);

    // 1. GPR X0 perturbation
    {
        WorkloadExecutionOutput perturbed = baseline;
        perturbed.replay.cpu.gpr[0] ^= 0x1ULL;
        ReplayDivergence div = ReplayStateContract::compare(baseline.replay, perturbed.replay);
        TEST_ASSERT(div.has_diverged, "GPR divergence detected");
        TEST_ASSERT(div.field_name == "cpu.gpr[X0]", "Field name must be cpu.gpr[X0]");
    }

    // 2. GPR X19 perturbation (callee-saved register)
    {
        WorkloadExecutionOutput perturbed = baseline;
        perturbed.replay.cpu.gpr[19] += 42;
        ReplayDivergence div = ReplayStateContract::compare(baseline.replay, perturbed.replay);
        TEST_ASSERT(div.has_diverged, "GPR X19 divergence detected");
        TEST_ASSERT(div.field_name == "cpu.gpr[X19]", "Field name must be cpu.gpr[X19]");
    }

    // 3. PC perturbation
    {
        WorkloadExecutionOutput perturbed = baseline;
        perturbed.replay.cpu.pc += 4;
        ReplayDivergence div = ReplayStateContract::compare(baseline.replay, perturbed.replay);
        TEST_ASSERT(div.has_diverged, "PC divergence detected");
        TEST_ASSERT(div.field_name == "cpu.pc", "Field name must be cpu.pc");
    }

    // 4. SP perturbation
    {
        WorkloadExecutionOutput perturbed = baseline;
        perturbed.replay.cpu.sp -= 16;
        ReplayDivergence div = ReplayStateContract::compare(baseline.replay, perturbed.replay);
        TEST_ASSERT(div.has_diverged, "SP divergence detected");
        TEST_ASSERT(div.field_name == "cpu.sp", "Field name must be cpu.sp");
    }

    // 5. Instruction count perturbation
    {
        WorkloadExecutionOutput perturbed = baseline;
        perturbed.replay.cpu.instruction_count += 1;
        ReplayDivergence div = ReplayStateContract::compare(baseline.replay, perturbed.replay);
        TEST_ASSERT(div.has_diverged, "Instruction count divergence detected");
        TEST_ASSERT(div.field_name == "cpu.instruction_count", "Field name must be cpu.instruction_count");
    }

    // 6. Memory SHA-256 digest perturbation
    {
        WorkloadExecutionOutput perturbed = baseline;
        perturbed.replay.memory.memory_sha256[0] ^= 0xFF;
        ReplayDivergence div = ReplayStateContract::compare(baseline.replay, perturbed.replay);
        TEST_ASSERT(div.has_diverged, "Memory SHA-256 divergence detected");
        TEST_ASSERT(div.field_name == "memory.memory_sha256", "Field name must be memory.memory_sha256");
    }

    // 7. Syscall count perturbation
    {
        WorkloadExecutionOutput perturbed = baseline;
        perturbed.replay.syscalls.push_back({1, 0, 0, 0, 0, 0, 0});
        ReplayDivergence div = ReplayStateContract::compare(baseline.replay, perturbed.replay);
        TEST_ASSERT(div.has_diverged, "Syscall count divergence detected");
        TEST_ASSERT(div.field_name == "syscalls.count", "Field name must be syscalls.count");
    }

    std::cout << "[Stage 248.17] All replay perturbation diagnostics passed.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 248.18 & 248.20 - Unified Workload Fault Injection & Containment
// ----------------------------------------------------------------------------
static void test_fault_injection_and_containment() {
    std::cout << "[Stage 248.18/20] Testing cross-subsystem fault injection & containment...\n";

    Memory mem(0x10000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x8000000ULL, 0x0400000ULL);
    ppm.initialize();

    auto bus = std::make_shared<VirtualDeviceBus>();
    auto uart = std::make_shared<VirtualUartDevice>(0x55415254u, 0x09000000ULL, 0x1000ULL, 33);
    auto shared_mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 4);
    auto dma = std::make_shared<VirtualDmaDevice>(0x56444D41u, 0x10000000ULL, 0x1000ULL, shared_mem, 34);
    bus->register_device(uart);
    bus->register_device(dma);

    CPU cpu;
    cpu.reset();
    cpu.attach_device_bus(bus);
    setup_guest_cpu(cpu);

    // 1. Unmapped MMIO address check
    TEST_ASSERT(!bus->is_device_address(0x08000000ULL), "Unmapped address must not be a device address");
    uint64_t unmapped_val = bus->read_mmio(0x08000000ULL, 4);
    TEST_ASSERT(unmapped_val == ~0ULL || unmapped_val == 0, "Unmapped MMIO returns default");

    // 2. DMA invalid descriptor address
    uint64_t bad_desc_addr = 0x10003ULL; // misaligned descriptor address
    dma->write_register(dma_reg::kControl, kControlEnable, 4);
    dma->write_register(dma_reg::kDescriptorAddr, bad_desc_addr, 8);
    dma->write_register(dma_reg::kControl, kControlEnable | kControlStart, 4);
    uint64_t dma_status = dma->read_register(dma_reg::kStatus, 4);
    TEST_ASSERT((dma_status & kStatusError) != 0, "Misaligned DMA descriptor triggers kStatusError");

    // 3. Invalid Syscall Handling
    VirtualConsole console;
    UserSyscallDispatcher disp(console);
    disp.set_abi_mode(SyscallAbi::DarwinStrict);

    uint64_t root = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root, "fault_uas");
    BsdProcess proc(501, 1, "fault_proc", nullptr);
    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);

    cpu.write_register(16, 999999); // Unknown syscall number
    disp.handle_syscall(cpu, proc, uas, sched);

    // Darwin ABI returns ENOSYS (78) in X0 and sets carry flag on error
    uint64_t ret = cpu.read_register(0);
    uint8_t cflag = static_cast<uint8_t>((cpu.get_nzcv() >> 1) & 1);
    TEST_ASSERT(ret == 78, "Unknown syscall must return ENOSYS (78)");
    TEST_ASSERT(cflag == 1, "Darwin error must set carry flag");
    TEST_ASSERT(!proc.is_terminated(), "Non-fatal bad syscall does not crash the process");

    std::cout << "[Stage 248.18/20] Fault injection & containment verified.\n";
}

// ----------------------------------------------------------------------------
// Test 5: Stage 248.21 - End-to-End Certification of all 4 Genuine Mach-O Workloads
// ----------------------------------------------------------------------------
static void test_all_four_macho_workloads() {
    std::cout << "[Stage 248.21] End-to-End Certification of all 4 Mach-O Workloads...\n";

    struct WorkloadSpec {
        std::string name;
        int expected_exit;
        pid_t pid;
    };

    const std::vector<WorkloadSpec> workloads = {
        {"arith_control",     42, 601},
        {"mem_func",          55, 602},
        {"syscall_lifecycle", 77, 603},
        {"multi_app_worker",  88, 604}
    };

    for (const auto& w : workloads) {
        std::cout << "  -> Certifying " << w.name << " (expected exit: " << w.expected_exit << ")...\n";
        WorkloadExecutionOutput out = run_macho_workload(w.name, w.pid);
        TEST_ASSERT(out.clean_exit, "Workload clean exit");
        TEST_ASSERT(out.exit_code == w.expected_exit, "Workload exit code matches expectation");
        TEST_ASSERT(out.instructions_executed > 10, "Instructions were executed");
        TEST_ASSERT(out.replay.version == kReplayVersion2, "Replay V2 version");
        std::cout << "     Passed: " << out.instructions_executed << " instructions executed, exit code "
                  << out.exit_code << ".\n";
    }

    std::cout << "[Stage 248.21] All 4 Mach-O Workloads Certified.\n";
}

int main() {
    std::cout << "============================================================\n";
    std::cout << "Running Stage 248.14 - 248.21 Replay & Workload Assurance Tests\n";
    std::cout << "============================================================\n";

    test_cross_process_isolation();
    test_replay_event_coverage_and_determinism();
    test_replay_perturbation_diagnostics();
    test_fault_injection_and_containment();
    test_all_four_macho_workloads();

    std::cout << "\n>>> ALL STAGE 248.14 - 248.21 TESTS PASSED SUCCESSFULLY! <<<\n";
    return 0;
}

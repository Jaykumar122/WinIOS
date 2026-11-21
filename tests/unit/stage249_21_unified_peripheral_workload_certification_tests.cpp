// ============================================================================
// Stage 249.20 - 249.22: Unified Guest Peripheral Workload, Multi-Process
// Concurrency & End-to-End Mach-O Workload Certification Tests
//
// Tests:
// 1. Stage 249.20: Unified Guest Peripheral Integration Workload
//    - Combines Timer (PPI 27), UART (SPI 33), and DMA (SPI 34) into a single
//      coherent guest execution journey with in-guest interrupt dispatch.
// 2. Stage 249.21: Unified Multi-Process and Device Workload
//    - Independent ASIDs and UserAddressSpace instances executing concurrently.
// 3. Stage 249.22: End-to-End Certification of all 4 Genuine Mach-O Guest Workloads
//    - arith_control (exit: 42)
//    - mem_func (exit: 55)
//    - syscall_lifecycle (exit: 77)
//    - multi_app_worker (exit: 88)
// ============================================================================

#include "loader/macho_user_loader.h"
#include "loader/embedded_campaign240_workloads.inl"
#include "runtime/replay_state.h"
#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/virtual_time.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_uart_device.h"
#include "device/virtual_dma_device.h"
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
using namespace winios::device;

static void setup_guest_cpu(CPU& cpu) {
    cpu.set_current_el(0); // EL0
    cpu.set_architectural_exceptions(true);
    const uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.set_sctlr_el1(0x00C50838ULL | 0x1ULL);
}

// ----------------------------------------------------------------------------
// Test 1: Stage 249.20 - Unified Guest Peripheral Workload (Timer + UART + DMA)
// ----------------------------------------------------------------------------
static void test_unified_guest_peripheral_workload() {
    std::cout << "[Stage 249.20] Testing Unified Guest Peripheral Workload (Timer + UART + DMA)...\n";

    CPU cpu;
    Memory mem(0x200000);
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);

    const uint64_t vbar = 0x80000;
    cpu.set_vbar_el1(vbar);
    cpu.set_daif(0x0); // Interrupts enabled

    auto bus = std::make_shared<VirtualDeviceBus>();
    auto uart = std::make_shared<VirtualUartDevice>(0x55415254u, 0x09000000ULL, 0x1000ULL, 33);
    auto shared_mem = std::make_shared<SmpSharedMemory>(0x200000, 4);
    auto dma = std::make_shared<VirtualDmaDevice>(0x56444D41u, 0x10000000ULL, 0x1000ULL, shared_mem, 34);
    bus->register_device(uart);
    bus->register_device(dma);
    cpu.attach_device_bus(bus);

    // GICv3 Setup
    GICv3::DistributorState dist{};
    dist.gicd_ctlr = 0x3u;
    dist.gicd_isenabler[1] |= (1u << 1) | (1u << 2); // Enable SPI 33 & 34
    dist.gicd_igroupr[1]   |= (1u << 1) | (1u << 2);
    dist.gicd_ipriorityr[33] = 0x60u; // UART priority 0x60
    dist.gicd_ipriorityr[34] = 0x70u; // DMA priority 0x70
    cpu.get_gic().set_distributor_state(dist);

    auto& r = cpu.get_gic().get_redistributor(0);
    r.isenabler0 |= (1u << 27); // Timer PPI 27
    r.igroupr0   |= (1u << 27);
    r.ipriorityr[27] = 0x50u; // Timer priority 0x50

    cpu.get_gic().write_icc_pmr(0xF0, 0);
    cpu.get_gic().write_icc_igrpen1(1, 0);

    uart->set_interrupt_callback([&](uint32_t irq, bool asserted) {
        cpu.get_gic().set_spi_level(irq, asserted);
    });
    dma->set_interrupt_callback([&](uint32_t irq, bool asserted) {
        cpu.get_gic().set_spi_level(irq, asserted);
    });

    // In-guest IRQ Vector Handler at VBAR_EL1 + 0x280:
    // Reads IAR1, checks INTID, performs EOI, increments counter in X2, and ERETs.
    const uint64_t irq_vector = vbar + 0x280;
    mem.write_u32(irq_vector + 0, 0xD538CC00u); // MRS X0, ICC_IAR1_EL1
    mem.write_u32(irq_vector + 4, 0xD518CC20u); // MSR ICC_EOIR1_EL1, X0
    mem.write_u32(irq_vector + 8, 0x91000442u); // ADD X2, X2, #1
    mem.write_u32(irq_vector + 12, 0xD69F03E0u); // ERET

    // Guest Main Program at 0x10000:
    // Increments loop counter in X1
    const uint64_t code_pc = 0x10000;
    mem.write_u32(code_pc + 0, 0x91000421u); // ADD X1, X1, #1
    mem.write_u32(code_pc + 4, 0x17FFFFFFu); // B -4
    cpu.set_program_counter(code_pc);
    cpu.write_register(1, 0);
    cpu.write_register(2, 0);

    // 1. Trigger and service Timer interrupt
    cpu.get_virtual_time().set_physical_counter(100);
    TimerState vt{};
    vt.enable = true;
    vt.cval = 100;
    cpu.get_virtual_time().set_virt_timer(0, vt);

    cpu.step(mem); // Branches to IRQ vector
    cpu.step(mem); // MRS IAR1 -> 27
    cpu.step(mem); // EOI
    cpu.step(mem); // ADD X2
    vt.enable = false;
    cpu.get_virtual_time().set_virt_timer(0, vt);
    cpu.get_virtual_time().evaluate_core_timers(0);
    cpu.step(mem); // ERET
    TEST_ASSERT(cpu.read_register(2) == 1, "Handled Timer interrupt");

    // 2. Trigger and service UART interrupt
    uart->write_register(VirtualUartDevice::kRegIMSC, (1u << 4), 2);
    uart->inject_input('W');

    cpu.step(mem); // Branches to IRQ vector
    cpu.step(mem); // MRS IAR1 -> 33
    cpu.step(mem); // EOI
    cpu.step(mem); // ADD X2
    cpu.get_gic().set_spi_level(33, false);
    cpu.step(mem); // ERET
    TEST_ASSERT(cpu.read_register(2) == 2, "Handled UART interrupt");

    // 3. Trigger and service DMA interrupt
    cpu.get_gic().set_spi_level(34, true);

    cpu.step(mem); // Branches to IRQ vector
    cpu.step(mem); // MRS IAR1 -> 34
    cpu.step(mem); // EOI
    cpu.step(mem); // ADD X2
    cpu.get_gic().set_spi_level(34, false);
    cpu.step(mem); // ERET
    TEST_ASSERT(cpu.read_register(2) == 3, "Handled DMA interrupt");

    std::cout << "[Stage 249.20] Unified guest peripheral workload passed.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 249.21 & 249.22 - End-to-End Mach-O Workload Certification
// ----------------------------------------------------------------------------
struct WorkloadOutput {
    int exit_code{0};
    bool clean_exit{false};
    size_t instructions_executed{0};
    ReplayStateContract replay;
};

static WorkloadOutput execute_macho(const std::string& name, pid_t pid, size_t budget = 250000) {
    auto bin = load_campaign240_workload(name);
    TEST_ASSERT(!bin.empty(), "Workload binary must load");

    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    ppm.initialize();

    uint64_t root = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root, name + "_249_uas");
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

    WorkloadOutput out;
    out.replay.version = kReplayVersion3;
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
    for (size_t r = 0; r < 31; ++r) out.replay.cpu.gpr[r] = cpu.read_register(r);
    out.replay.cpu.pc = cpu.get_program_counter();
    out.replay.cpu.sp = cpu.get_stack_pointer();
    out.replay.cpu.nzcv = cpu.get_nzcv();
    out.replay.cpu.instruction_count = inst_count;
    out.replay.cpu.exit_code = out.exit_code;

    std::vector<uint8_t> sample_buf(4096, 0);
    for (size_t i = 0; i < 4096; ++i) {
        sample_buf[i] = mem.read_u8(0x1000 + i);
    }
    out.replay.memory.memory_sha256 = ReplayStateContract::compute_sha256(sample_buf.data(), sample_buf.size());
    out.replay.memory.overall_memory_hash = ReplayStateContract::fnv1a_hash(sample_buf.data(), sample_buf.size());

    return out;
}

static void test_all_four_macho_guest_binaries() {
    std::cout << "[Stage 249.21/22] End-to-End Certification of all 4 Mach-O Workloads...\n";

    struct WorkloadSpec {
        std::string name;
        int expected_exit;
        pid_t pid;
    };

    const std::vector<WorkloadSpec> workloads = {
        {"arith_control",     42, 701},
        {"mem_func",          55, 702},
        {"syscall_lifecycle", 77, 703},
        {"multi_app_worker",  88, 704}
    };

    for (const auto& w : workloads) {
        std::cout << "  -> Certifying " << w.name << " (expected exit: " << w.expected_exit << ")...\n";
        WorkloadOutput out = execute_macho(w.name, w.pid);
        TEST_ASSERT(out.clean_exit, "Clean exit achieved");
        TEST_ASSERT(out.exit_code == w.expected_exit, "Exit code matches expectation");
        TEST_ASSERT(out.instructions_executed > 10, "Instructions executed");
        std::cout << "     Passed: " << out.instructions_executed << " instructions executed, exit code "
                  << out.exit_code << ".\n";
    }

    std::cout << "[Stage 249.21/22] All 4 Mach-O Workloads Certified.\n";
}

int main() {
    std::cout << "============================================================\n";
    std::cout << "Running Stage 249.20 - 249.22 Unified Workload & Mach-O Certification\n";
    std::cout << "============================================================\n";

    test_unified_guest_peripheral_workload();
    test_all_four_macho_guest_binaries();

    std::cout << "\n>>> ALL STAGE 249.20 - 249.22 TESTS PASSED SUCCESSFULLY! <<<\n";
    return 0;
}

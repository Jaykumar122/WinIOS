// ============================================================================
// Stage 194.10: Preemptive Scheduling with Timer Interrupts Tests
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/preemption_timer.h"
#include "kernel/scheduler.h"
#include "kernel/thread.h"
#include "memory/memory.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::kernel;

static bool test_irq_handler_machine_code() {
    const auto routine = PreemptionTimer::emit_guest_irq_handler();
    CHECK(routine.size() == 15, "Routine must consist of exactly 15 ARM64 instructions (60 bytes)");

    CHECK(routine[0] == 0xA9BF07E0u, "First instruction must be stp x0, x1, [sp, #-16]!");
    CHECK(routine[3] == 0xD538CC00u, "Instruction 3 must be mrs x0, ICC_IAR1_EL1");
    CHECK(routine[4] == 0xD518CC20u, "Instruction 4 must be msr ICC_EOIR1_EL1, x0");
    CHECK(routine[6] == 0xD51BE301u, "Instruction 6 must be msr CNTV_TVAL_EL0, x1");
    CHECK(routine[14] == 0xD69F03E0u, "Instruction 14 must be eret");

    std::cout << "[PASS] test_irq_handler_machine_code" << std::endl;
    return true;
}

static bool test_vector_table_setup() {
    Memory mem(4 * 1024 * 1024);
    const std::uint64_t vbar_pa = 0x80000ULL;
    const std::uint64_t handler_pa = 0x82000ULL;

    CHECK(PreemptionTimer::setup_vector_table(mem, vbar_pa, handler_pa), "Setup vector table");

    // Offset 0x280 (diff = 0x82000 - 0x80280 = 0x1D80 = 7552 bytes = 1888 instructions = 0x760)
    // imm26 = 0x760 -> B opcode = 0x14000760u
    const std::uint32_t b1 = mem.read_u32(static_cast<std::size_t>(vbar_pa + 0x280));
    CHECK(b1 == 0x14000760u, "VBAR + 0x280 must encode branch to handler");

    // Offset 0x480 (diff = 0x82000 - 0x80480 = 0x1B80 = 7040 bytes = 1760 instructions = 0x6E0)
    // imm26 = 0x6E0 -> B opcode = 0x140006E0u
    const std::uint32_t b2 = mem.read_u32(static_cast<std::size_t>(vbar_pa + 0x480));
    CHECK(b2 == 0x140006E0u, "VBAR + 0x480 must encode branch to handler");

    std::cout << "[PASS] test_vector_table_setup" << std::endl;
    return true;
}

static bool test_timer_irq_delivery_and_execution_el1() {
    Memory mem(4 * 1024 * 1024);
    const std::uint64_t vbar_pa = 0x80000ULL;
    const std::uint64_t handler_pa = 0x82000ULL;
    const std::uint64_t code_pa = 0x1000ULL;
    const std::uint64_t tick_counter_addr = 0x40000ULL;

    // 1. Write IRQ handler into guest RAM
    const auto handler = PreemptionTimer::emit_guest_irq_handler();
    for (std::size_t i = 0; i < handler.size(); ++i) {
        mem.write_u32(static_cast<std::size_t>(handler_pa + i * 4), handler[i]);
    }

    // 2. Setup vector table
    PreemptionTimer::setup_vector_table(mem, vbar_pa, handler_pa);

    // 3. User code at 0x1000:
    // [0x1000] add x20, x20, #1 (0x91000694u)
    // [0x1004] b 0x1000         (0x17FFFFFFu)
    mem.write_u32(static_cast<std::size_t>(code_pa + 0), 0x91000694u);
    mem.write_u32(static_cast<std::size_t>(code_pa + 4), 0x17FFFFFFu);

    // Clear tick counter
    mem.write_u64(static_cast<std::size_t>(tick_counter_addr), 0);

    // 4. Configure CPU
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(vbar_pa);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_sp_el1(0x70000ULL);
    cpu.set_program_counter(code_pa);
    cpu.set_daif(0); // Unmask IRQ (I bit = 0)
    cpu.write_register(20, 0);

    PreemptionTimer::configure_virtual_timer(cpu);

    // Run 5 loop steps
    for (int i = 0; i < 5; ++i) {
        cpu.step(mem);
    }
    CHECK(cpu.read_register(20) > 0, "Loop must be progressing");
    const std::uint64_t initial_loops = cpu.read_register(20);

    // 5. Fire virtual timer interrupt
    PreemptionTimer::assert_virtual_timer_tick(cpu);

    // Next step triggers architectural IRQ exception to VBAR + 0x280
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == vbar_pa + 0x280, "CPU must vector to VBAR + 0x280 upon IRQ");

    // Next step executes branch to handler (0x82000)
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == handler_pa, "CPU must branch to handler_pa (0x82000)");

    // Step through the entire 15-instruction handler until ERET
    std::size_t handler_steps = 0;
    while (cpu.get_program_counter() != code_pa && handler_steps < 30) {
        cpu.step(mem);
        ++handler_steps;
    }

    CHECK(cpu.get_program_counter() == code_pa, "CPU must return to code_pa via ERET");
    CHECK(cpu.get_current_el() == 1, "CPU remains at EL1");

    // Verify tick counter at 0x40000 was incremented to 1
    const std::uint64_t ticks = mem.read_u64(static_cast<std::size_t>(tick_counter_addr));
    CHECK(ticks == 1, "Tick counter in guest RAM must equal 1 after IRQ");

    // Resume loop execution
    cpu.step(mem);
    CHECK(cpu.read_register(20) > initial_loops, "Loop must resume seamlessly after ERET");

    std::cout << "[PASS] test_timer_irq_delivery_and_execution_el1" << std::endl;
    return true;
}

static bool test_timer_irq_delivery_and_execution_el0() {
    Memory mem(4 * 1024 * 1024);
    const std::uint64_t vbar_pa = 0x80000ULL;
    const std::uint64_t handler_pa = 0x82000ULL;
    const std::uint64_t code_pa = 0x1000ULL;
    const std::uint64_t tick_counter_addr = 0x40000ULL;

    const auto handler = PreemptionTimer::emit_guest_irq_handler();
    for (std::size_t i = 0; i < handler.size(); ++i) {
        mem.write_u32(static_cast<std::size_t>(handler_pa + i * 4), handler[i]);
    }
    PreemptionTimer::setup_vector_table(mem, vbar_pa, handler_pa);

    mem.write_u32(static_cast<std::size_t>(code_pa + 0), 0x91000694u); // add x20, x20, #1
    mem.write_u32(static_cast<std::size_t>(code_pa + 4), 0x17FFFFFFu); // b 0x1000
    mem.write_u64(static_cast<std::size_t>(tick_counter_addr), 10);

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(vbar_pa);
    cpu.set_current_el(0); // USER MODE EL0
    cpu.set_sp_sel(false);
    cpu.set_sp_el0(0x60000ULL);
    cpu.set_sp_el1(0x70000ULL); // Handler runs on SP_EL1
    cpu.set_program_counter(code_pa);
    cpu.set_daif(0);
    cpu.write_register(20, 100);

    PreemptionTimer::configure_virtual_timer(cpu);

    // Fire virtual timer interrupt
    PreemptionTimer::assert_virtual_timer_tick(cpu);

    // Vector to Lower EL IRQ slot at VBAR + 0x480
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == vbar_pa + 0x480, "CPU must vector to VBAR + 0x480 for Lower EL IRQ");
    CHECK(cpu.get_current_el() == 1, "IRQ transitions CPU to EL1 for handling");

    // Execute handler until ERET back to EL0
    std::size_t steps = 0;
    while ((cpu.get_current_el() != 0 || cpu.get_program_counter() != code_pa) && steps < 30) {
        cpu.step(mem);
        ++steps;
    }

    CHECK(cpu.get_current_el() == 0, "CPU must return to EL0 via ERET");
    CHECK(cpu.get_program_counter() == code_pa, "CPU returns to interrupted user instruction");

    const std::uint64_t ticks = mem.read_u64(static_cast<std::size_t>(tick_counter_addr));
    CHECK(ticks == 11, "Tick counter must increment to 11");

    std::cout << "[PASS] test_timer_irq_delivery_and_execution_el0" << std::endl;
    return true;
}

static bool test_timer_irq_scheduler_preemption() {
    Memory mem(4 * 1024 * 1024);
    KernelThreadManager thread_mgr;
    PriorityScheduler scheduler(thread_mgr, mem);
    CHECK(scheduler.initialize(), "Init scheduler");

    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);

    auto* t1 = thread_mgr.create_thread("t1", 0x1000, 0, 0x20000, 0x4000, 16);
    auto* t2 = thread_mgr.create_thread("t2", 0x2000, 0, 0x30000, 0x4000, 16);

    t1->set_time_slice(2); // 2 ticks quantum
    scheduler.enqueue_thread(t1);
    scheduler.enqueue_thread(t2);

    // Tick 0: T1 starts
    scheduler.tick(cpu);
    CHECK(scheduler.current_thread() == t1, "t1 active");

    // Simulated Timer Interrupt 1
    scheduler.tick(cpu);
    CHECK(scheduler.current_thread() == t1, "t1 continues running (quantum = 1)");

    // Simulated Timer Interrupt 2: quantum hits 0 -> preemption!
    scheduler.tick(cpu);
    CHECK(scheduler.current_thread() == t2, "t2 preempts t1 on timer tick");
    CHECK(scheduler.stats().total_preemptions >= 1, "Preemption counted");

    std::cout << "[PASS] test_timer_irq_scheduler_preemption" << std::endl;
    return true;
}

static bool test_compiled_macho_binary_artifact() {
    const std::string bin_path = "D:/WinISO26/WinIOS/tests/artifacts/bin/guest_preemption_irq";
    std::ifstream file(bin_path, std::ios::binary | std::ios::ate);
    CHECK(file.is_open(), "Compiled guest_preemption_irq binary artifact must exist");

    const auto size = file.tellg();
    CHECK(size == 50008, "Binary artifact size must be exactly 50008 bytes");

    std::cout << "[PASS] test_compiled_macho_binary_artifact" << std::endl;
    return true;
}

int main() {
    std::cout << "Running Stage 194.10 Preemptive Timer Tests..." << std::endl;

    if (!test_irq_handler_machine_code()) return 1;
    if (!test_vector_table_setup()) return 1;
    if (!test_timer_irq_delivery_and_execution_el1()) return 1;
    if (!test_timer_irq_delivery_and_execution_el0()) return 1;
    if (!test_timer_irq_scheduler_preemption()) return 1;
    if (!test_compiled_macho_binary_artifact()) return 1;

    std::cout << "\nAll Stage 194.10 Preemptive Timer tests PASSED successfully." << std::endl;
    return 0;
}

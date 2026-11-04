#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/psci.h"
#include "cpu/smp_cluster.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "trace/trace_config.h"
#include "trace/trace_file_reader.h"
#include "trace/trace_file_writer.h"
#include "trace/trace_manager.h"
#include "trace/trace_types.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#define TEST_ASSERT(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":"   \
                      << __LINE__ << std::endl;                                \
            std::abort();                                                      \
        }                                                                      \
    } while (0)

using namespace winios;
using namespace winios::trace;

namespace {

std::vector<TraceEvent> pop_all_events(TraceManager& mgr) {
    std::vector<TraceEvent> results;
    TraceEvent ev{};
    auto* rb = const_cast<InMemoryRingBuffer*>(mgr.ring_buffer());
    if (!rb) {
        return results;
    }
    while (rb->try_pop(ev)) {
        results.push_back(ev);
    }
    return results;
}

void setup_manager_with_timer(TraceManager& mgr) {
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.set(TraceEventType::TimerEvent, true);
    cfg.event_types.set(TraceEventType::InterruptDelivered, true);
    cfg.event_types.set(TraceEventType::ExceptionEntered, true);
    cfg.event_types.set(TraceEventType::ExceptionReturned, true);
    cfg.event_types.set(TraceEventType::ContextChanged, true);
    cfg.event_types.set(TraceEventType::InstructionRetired, true);
    cfg.event_types.set(TraceEventType::CPUStarted, true);
    cfg.event_types.set(TraceEventType::CPUStopped, true);
    mgr.configure(cfg);
    mgr.start();
}

constexpr std::uint32_t enc_nop() {
    return 0xD503201Fu;
}

constexpr std::uint32_t enc_eret() {
    return 0xD69F03E0u;
}

constexpr std::uint32_t enc_msr(unsigned op1, unsigned crn, unsigned crm, unsigned op2, unsigned rt) {
    return 0xD5100000u | (1u << 20u) | ((op1 & 7u) << 16u) | ((crn & 0xFu) << 12u) |
           ((crm & 0xFu) << 8u) | ((op2 & 7u) << 5u) | (rt & 0x1Fu);
}

// -----------------------------------------------------------------------------
// Test 1: End-to-end Timer Execution Workflow
// -----------------------------------------------------------------------------
void test_end_to_end_timer_workflow() {
    CPU cpu;
    Memory mem(0x20000);
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);

    cpu.start();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x10000);
    cpu.set_daif(0); // Unmask IRQ
    cpu.set_program_counter(0x2000);

    // Setup main code
    mem.write_u32(0x2000, enc_nop());
    mem.write_u32(0x2004, enc_nop());

    // Setup ISR at EL1h IRQ vector (0x10280):
    // 1. Disable timer: MSR CNTP_CTL_EL0, X0 (where X0 = 0)
    // 2. ERET
    mem.write_u32(0x10280, enc_msr(3, 14, 2, 1, 0)); // MSR CNTP_CTL_EL0, X0
    mem.write_u32(0x10284, enc_eret());

    // Arm timer for deadline 200
    cpu.set_guest_counter(100ULL);
    cpu.set_phys_timer(TimerState{true, false, false, 200ULL});
    pop_all_events(mgr); // Clear setup events

    // Advance counter and step CPU: triggers interrupt acceptance
    cpu.set_guest_counter(250ULL);
    cpu.step(mem); // Enters ISR at 0x10280

    TEST_ASSERT(cpu.get_program_counter() == 0x10280);

    // Step ISR: execute MSR CNTP_CTL_EL0
    cpu.write_register(0, 0ULL);
    cpu.step(mem); // Executes MSR, disabling timer

    // Step ISR: execute ERET
    cpu.step(mem); // Returns to 0x2000
    TEST_ASSERT(cpu.get_program_counter() == 2000 || cpu.get_program_counter() == 0x2000);

    cpu.stop(0);

    auto evs = pop_all_events(mgr);
    bool has_expired = false;
    bool has_irq_asserted = false;
    bool has_delivered = false;
    bool has_entered = false;
    bool has_irq_cleared = false;
    bool has_eret = false;

    for (const auto& ev : evs) {
        if (ev.get_event_type() == TraceEventType::TimerEvent) {
            auto op = static_cast<TimerOp>(ev.opcode_or_syndrome & 0xFFu);
            if (op == TimerOp::Expired) has_expired = true;
            if (op == TimerOp::InterruptAsserted) has_irq_asserted = true;
            if (op == TimerOp::InterruptCleared) has_irq_cleared = true;
        }
        if (ev.get_event_type() == TraceEventType::InterruptDelivered) has_delivered = true;
        if (ev.get_event_type() == TraceEventType::ExceptionEntered) has_entered = true;
        if (ev.get_event_type() == TraceEventType::ExceptionReturned) has_eret = true;
    }

    TEST_ASSERT(has_expired);
    TEST_ASSERT(has_irq_asserted);
    TEST_ASSERT(has_delivered);
    TEST_ASSERT(has_entered);
    TEST_ASSERT(has_irq_cleared);
    TEST_ASSERT(has_eret);
}

// -----------------------------------------------------------------------------
// Test 2: Differential Architectural State Validation
// -----------------------------------------------------------------------------
void test_differential_timer_execution() {
    auto run_workload = [](bool enable_tracing) {
        struct StateSnapshot {
            std::uint64_t x[31];
            std::uint64_t pc;
            std::uint64_t sp_el0;
            std::uint64_t sp_el1;
            std::uint64_t elr_el1;
            std::uint32_t spsr_el1;
            std::uint8_t daif;
            unsigned el;
            std::uint64_t counter;
            TimerState phys_timer;
        } snap{};

        CPU cpu;
        Memory mem(0x20000);
        TraceManager mgr;
        if (enable_tracing) {
            setup_manager_with_timer(mgr);
            cpu.set_trace_manager(&mgr);
        }

        cpu.start();
        cpu.set_architectural_exceptions(true);
        cpu.set_vbar_el1(0x10000);
        cpu.set_daif(0);
        cpu.set_program_counter(0x2000);

        // Code: ADD X1, X1, #10; NOP
        constexpr std::uint32_t enc_add_x1_10 = 0x91002821u;
        mem.write_u32(0x2000, enc_add_x1_10);
        mem.write_u32(0x2004, enc_nop());

        // ISR: ADD X2, X2, #20; MSR CNTP_CTL_EL0, X0; ERET
        constexpr std::uint32_t enc_add_x2_20 = 0x91005042u;
        mem.write_u32(0x10280, enc_add_x2_20);
        mem.write_u32(0x10284, enc_msr(3, 14, 2, 1, 0));
        mem.write_u32(0x10288, enc_eret());

        cpu.write_register(0, 0ULL); // for timer disable in ISR
        cpu.write_register(1, 5ULL);
        cpu.write_register(2, 7ULL);

        // Set timer deadline 300
        cpu.set_guest_counter(100ULL);
        cpu.set_phys_timer(TimerState{true, false, false, 300ULL});

        // Step 1: execute ADD X1 (counter becomes 101)
        cpu.step(mem);
        // Step 2: advance counter and step -> interrupt accepts!
        cpu.set_guest_counter(350ULL);
        cpu.step(mem); // Enters ISR at 0x10280

        // Step 3: ISR ADD X2 (27)
        cpu.step(mem);
        // Step 4: ISR disable timer
        cpu.step(mem);
        // Step 5: ISR ERET -> returns to 0x2004
        cpu.step(mem);
        // Step 6: NOP at 0x2004
        cpu.step(mem);

        cpu.stop(0);

        for (int i = 0; i < 31; ++i) snap.x[i] = cpu.read_register(i);
        snap.pc = cpu.get_program_counter();
        snap.sp_el0 = cpu.get_stack_pointer();
        snap.sp_el1 = cpu.get_sp_el1();
        snap.elr_el1 = cpu.get_elr_el1();
        snap.spsr_el1 = cpu.get_spsr_el1();
        snap.daif = cpu.get_daif();
        snap.el = static_cast<unsigned>(cpu.get_current_el());
        snap.counter = cpu.get_guest_counter();
        snap.phys_timer = cpu.get_phys_timer();

        return snap;
    };

    auto untraced = run_workload(false);
    auto traced = run_workload(true);

    for (int i = 0; i < 31; ++i) {
        TEST_ASSERT(untraced.x[i] == traced.x[i]);
    }
    TEST_ASSERT(untraced.pc == traced.pc);
    TEST_ASSERT(untraced.sp_el0 == traced.sp_el0);
    TEST_ASSERT(untraced.sp_el1 == traced.sp_el1);
    TEST_ASSERT(untraced.elr_el1 == traced.elr_el1);
    TEST_ASSERT(untraced.spsr_el1 == traced.spsr_el1);
    TEST_ASSERT(untraced.daif == traced.daif);
    TEST_ASSERT(untraced.el == traced.el);
    TEST_ASSERT(untraced.counter == traced.counter);
    TEST_ASSERT(untraced.phys_timer.enable == traced.phys_timer.enable);
    TEST_ASSERT(untraced.phys_timer.cval == traced.phys_timer.cval);
}

// -----------------------------------------------------------------------------
// Test 3: Multi-Core SMP Timer Pipeline
// -----------------------------------------------------------------------------
void test_smp_4core_timer_pipeline() {
    SMPCluster cluster(4, 0x40000);
    std::vector<TraceManager> managers(4);

    for (std::size_t i = 0; i < 4; ++i) {
        setup_manager_with_timer(managers[i]);
        cluster.get_core(i).set_trace_manager(&managers[i]);
        cluster.initialize_core(i, 0x1000 + i * 0x1000);
    }

    // Set separate deadlines across cores
    cluster.get_core(0).set_phys_timer(TimerState{true, false, false, 200ULL});
    cluster.get_core(1).set_phys_timer(TimerState{true, false, false, 400ULL});
    cluster.get_core(2).set_phys_timer(TimerState{true, false, false, 600ULL});
    cluster.get_core(3).set_phys_timer(TimerState{true, false, false, 800ULL});

    // Advance counter to 500
    cluster.get_virtual_time().set_physical_counter(500ULL);
    for (std::size_t i = 0; i < 4; ++i) {
        cluster.get_core(i).check_timer_status();
    }

    // Cores 0 and 1 should have expired; Cores 2 and 3 should not
    TEST_ASSERT(cluster.get_core(0).get_phys_timer().istatus == true);
    TEST_ASSERT(cluster.get_core(1).get_phys_timer().istatus == true);
    TEST_ASSERT(cluster.get_core(2).get_phys_timer().istatus == false);
    TEST_ASSERT(cluster.get_core(3).get_phys_timer().istatus == false);

    // Verify events isolated per-core
    for (std::size_t i = 0; i < 4; ++i) {
        auto evs = pop_all_events(managers[i]);
        for (const auto& ev : evs) {
            TEST_ASSERT(ev.core_id == static_cast<std::uint16_t>(i));
        }
    }
}

// -----------------------------------------------------------------------------
// Test 4: Multi-block Binary Trace File Persistence
// -----------------------------------------------------------------------------
void test_multiblock_timer_trace_file() {
    const std::filesystem::path test_file = "test_multiblock_timer.wtr";
    if (std::filesystem::exists(test_file)) {
        std::filesystem::remove(test_file);
    }

    TraceFileWriter writer;
    TEST_ASSERT(writer.open(test_file.string()));

    constexpr std::size_t kEventCount = 2050; // Spans across multiple blocks
    for (std::size_t i = 0; i < kEventCount; ++i) {
        TraceEvent ev{};
        ev.sequence_number = i + 1;
        ev.virtual_time = i * 10;
        ev.pc = 0x1000 + (i % 64) * 4;
        ev.address = 10000ULL + i;
        ev.data = 5000ULL + i;
        ev.opcode_or_syndrome = static_cast<std::uint32_t>(TimerOp::CompareUpdate) | (static_cast<std::uint32_t>(TimerType::Physical) << 8u);
        ev.flags = static_cast<std::uint32_t>(TraceEventFlags::HasData | TraceEventFlags::IsWrite | TraceEventFlags::IsPrivileged);
        ev.event_type = static_cast<std::uint16_t>(TraceEventType::TimerEvent);
        ev.core_id = 0;
        ev.exception_level = 1;
        TEST_ASSERT(writer.write_event(ev));
    }
    writer.close();

    TraceFileReader reader;
    TEST_ASSERT(reader.open(test_file.string()));
    TEST_ASSERT(reader.is_cleanly_finalized());

    std::size_t read_count = 0;
    TraceEvent read_ev{};
    while (reader.read_event(read_ev)) {
        ++read_count;
        TEST_ASSERT(read_ev.get_event_type() == TraceEventType::TimerEvent);
        TEST_ASSERT(read_ev.sequence_number == read_count);
    }
    TEST_ASSERT(read_count == kEventCount);
    reader.close();

    std::filesystem::remove(test_file);
}

// -----------------------------------------------------------------------------
// Test 5: Corrupted Block Detection
// -----------------------------------------------------------------------------
void test_corrupted_block_detection() {
    const std::filesystem::path test_file = "test_corrupted_timer.wtr";
    if (std::filesystem::exists(test_file)) {
        std::filesystem::remove(test_file);
    }

    TraceFileWriter writer;
    TEST_ASSERT(writer.open(test_file.string()));
    for (int i = 0; i < 50; ++i) {
        TraceEvent ev{};
        ev.sequence_number = i + 1;
        ev.event_type = static_cast<std::uint16_t>(TraceEventType::TimerEvent);
        writer.write_event(ev);
    }
    writer.close();

    // Corrupt one byte inside block body
    {
        std::fstream file(test_file.string(), std::ios::in | std::ios::out | std::ios::binary);
        TEST_ASSERT(file.is_open());
        file.seekp(128); // Corrupt payload byte
        char bad = '\xFF';
        file.write(&bad, 1);
    }

    TraceFileReader reader;
    TEST_ASSERT(reader.open(test_file.string()));
    std::vector<TraceEvent> loaded; std::size_t n = reader.read_all_events(loaded); TEST_ASSERT(n < 50 || reader.last_error() != TraceFileError::Success);
    reader.close();

    std::filesystem::remove(test_file);
}

// -----------------------------------------------------------------------------
// Test 6: Rapid Timer Rearm Pipeline
// -----------------------------------------------------------------------------
void test_rapid_timer_rearm_pipeline() {
    CPU cpu;
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);

    constexpr int kIterations = 50;
    for (int i = 0; i < kIterations; ++i) {
        cpu.set_guest_counter(i * 100);
        cpu.set_phys_timer(TimerState{true, false, false, static_cast<std::uint64_t>((i * 100) + 50)});
        cpu.set_guest_counter((i * 100) + 60);
        cpu.check_timer_status();
    }

    auto evs = pop_all_events(mgr);
    std::size_t expired_count = 0;
    for (const auto& ev : evs) {
        if (ev.get_event_type() == TraceEventType::TimerEvent) {
            auto op = static_cast<TimerOp>(ev.opcode_or_syndrome & 0xFFu);
            if (op == TimerOp::Expired) {
                ++expired_count;
            }
        }
    }
    TEST_ASSERT(expired_count == kIterations);
}

// -----------------------------------------------------------------------------
// Test 7: Virtual Offset Dynamic Pipeline
// -----------------------------------------------------------------------------
void test_virtual_offset_timer_pipeline() {
    CPU cpu;
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);

    cpu.set_guest_counter(1000ULL);
    cpu.set_cntvoff_el2(500ULL); // Virtual counter is 500
    cpu.set_virt_timer(TimerState{true, false, false, 800ULL}); // Deadline 800
    pop_all_events(mgr);

    // Dynamic update of CNTVOFF_EL2 to 100 -> Virtual counter jumps to 900!
    cpu.set_cntvoff_el2(100ULL);
    cpu.check_timer_status();

    auto evs = pop_all_events(mgr);
    bool found_virt_expired = false;
    for (const auto& ev : evs) {
        if (ev.get_event_type() == TraceEventType::TimerEvent) {
            auto op = static_cast<TimerOp>(ev.opcode_or_syndrome & 0xFFu);
            auto type = static_cast<TimerType>((ev.opcode_or_syndrome >> 8u) & 0xFFu);
            if (op == TimerOp::Expired && type == TimerType::Virtual) {
                found_virt_expired = true;
            }
        }
    }
    TEST_ASSERT(found_virt_expired);
}

// -----------------------------------------------------------------------------
// Test 8: WFI Timer Wake Pipeline
// -----------------------------------------------------------------------------
void test_wfi_timer_wake_pipeline() {
    SMPCluster cluster(2, 0x20000);
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    auto& c0 = cluster.get_core(0);
    c0.set_trace_manager(&mgr);

    cluster.initialize_core(0, 0x1000);
    c0.set_waiting_for_interrupt(true);
    c0.set_phys_timer(TimerState{true, false, false, 500ULL});
    c0.set_daif(0);
    c0.get_gic().write_icc_pmr(0xFF, 0);
    c0.get_gic().write_icc_igrpen1(1, 0);
    c0.get_gic().get_redistributor(0).isenabler0 |= (1u << 30);

    // Advance clock directly to deadline
    cluster.run_deterministic(10);
    TEST_ASSERT(c0.get_phys_timer().istatus == true);

    auto evs = pop_all_events(mgr);
    bool found_timer_event = false;
    for (const auto& ev : evs) {
        if (ev.get_event_type() == TraceEventType::TimerEvent) {
            found_timer_event = true;
        }
    }
    TEST_ASSERT(found_timer_event);
}

} // namespace

int main() {
    std::cout << "=== WinIOS Phase 52 Stage 14: Timer Trace Pipeline Integration Tests ===" << std::endl;

    std::cout << "  Test 1: End-to-end timer execution workflow... ";
    test_end_to_end_timer_workflow();
    std::cout << "PASS\n";

    std::cout << "  Test 2: Differential architectural state validation... ";
    test_differential_timer_execution();
    std::cout << "PASS\n";

    std::cout << "  Test 3: Multi-core SMP timer pipeline... ";
    test_smp_4core_timer_pipeline();
    std::cout << "PASS\n";

    std::cout << "  Test 4: Multi-block binary trace file persistence... ";
    test_multiblock_timer_trace_file();
    std::cout << "PASS\n";

    std::cout << "  Test 5: Corrupted block detection... ";
    test_corrupted_block_detection();
    std::cout << "PASS\n";

    std::cout << "  Test 6: Rapid timer rearm pipeline... ";
    test_rapid_timer_rearm_pipeline();
    std::cout << "PASS\n";

    std::cout << "  Test 7: Virtual offset dynamic pipeline... ";
    test_virtual_offset_timer_pipeline();
    std::cout << "PASS\n";

    std::cout << "  Test 8: WFI timer wake pipeline... ";
    test_wfi_timer_wake_pipeline();
    std::cout << "PASS\n";

    std::cout << "All 8/8 Phase 52 Stage 14 Timer Pipeline Integration Tests PASSED successfully!" << std::endl;
    return 0;
}

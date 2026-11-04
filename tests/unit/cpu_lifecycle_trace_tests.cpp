#include "cpu/cpu.h"
#include "cpu/psci.h"
#include "cpu/smp_cluster.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "trace/trace_config.h"
#include "trace/trace_manager.h"
#include "trace/trace_types.h"

#include <cassert>
#include <cstdint>
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

constexpr std::uint32_t enc_nop() {
    return 0xD503201Fu;
}

constexpr std::uint32_t enc_wfi() {
    return 0xD503207Fu;
}

constexpr std::uint32_t enc_wfe() {
    return 0xD503205Fu;
}

void setup_test_cpu(CPU& cpu, Memory& mem, unsigned el = 1, std::uint64_t vbar = 0x10000) {
    (void)mem;
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(el);
    cpu.set_vbar_el1(vbar);
    cpu.set_vbar_el2(vbar + 0x10000);
    cpu.set_vbar_el3(vbar + 0x20000);
    cpu.set_daif(0);
}

// -----------------------------------------------------------------------------
// Test 1: CPU::start() emits CPUStarted with sequence 1 and entry PC
// -----------------------------------------------------------------------------
void test_cpu_start_emits_event() {
    std::cout << "  Test 1: CPU::start() emits CPUStarted event... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();
    cpu.set_program_counter(0x8000);
    cpu.set_current_el(1);

    TEST_ASSERT(!cpu.is_started());
    TEST_ASSERT(!cpu.is_stopped());

    cpu.start();

    TEST_ASSERT(cpu.is_started());
    TEST_ASSERT(!cpu.is_stopped());

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 1);

    const auto& ev = events[0];
    TEST_ASSERT(ev.event_type == static_cast<std::uint16_t>(TraceEventType::CPUStarted));
    TEST_ASSERT(ev.sequence_number == 1);
    TEST_ASSERT(ev.pc == 0x8000);
    TEST_ASSERT(ev.address == 0x8000);
    TEST_ASSERT(ev.core_id == 0);
    TEST_ASSERT(ev.exception_level == 1);
    TEST_ASSERT((ev.flags & static_cast<std::uint32_t>(TraceEventFlags::HasVirtualAddress)) != 0);
    TEST_ASSERT((ev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsPrivileged)) != 0);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 2: CPU::start() is idempotent
// -----------------------------------------------------------------------------
void test_cpu_start_idempotency() {
    std::cout << "  Test 2: CPU::start() idempotency... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();
    cpu.set_program_counter(0x8000);

    cpu.start();
    cpu.start();
    cpu.start();

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 1);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::CPUStarted));
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 3: CPU::stop() emits CPUStopped with shutdown PC and stop reason
// -----------------------------------------------------------------------------
void test_cpu_stop_emits_event() {
    std::cout << "  Test 3: CPU::stop() emits CPUStopped event... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();
    cpu.set_program_counter(0x8000);

    cpu.start();
    auto start_events = pop_all_events(mgr);
    TEST_ASSERT(start_events.size() == 1);

    cpu.set_program_counter(0x9000);
    cpu.stop(42);

    TEST_ASSERT(cpu.is_stopped());

    auto stop_events = pop_all_events(mgr);
    TEST_ASSERT(stop_events.size() == 1);

    const auto& ev = stop_events[0];
    TEST_ASSERT(ev.event_type == static_cast<std::uint16_t>(TraceEventType::CPUStopped));
    TEST_ASSERT(ev.sequence_number == 2);
    TEST_ASSERT(ev.pc == 0x9000);
    TEST_ASSERT(ev.address == 0);
    TEST_ASSERT(ev.data == 42);
    TEST_ASSERT(ev.opcode_or_syndrome == 42);
    TEST_ASSERT(ev.core_id == 0);
    TEST_ASSERT((ev.flags & static_cast<std::uint32_t>(TraceEventFlags::HasData)) != 0);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 4: CPU::stop() is idempotent
// -----------------------------------------------------------------------------
void test_cpu_stop_idempotency() {
    std::cout << "  Test 4: CPU::stop() idempotency... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.start();
    pop_all_events(mgr);

    cpu.stop(1);
    cpu.stop(2);
    cpu.stop(3);

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 1);
    TEST_ASSERT(events[0].data == 1); // original stop reason retained
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 5: CPU::reset() lifecycle reset
// -----------------------------------------------------------------------------
void test_cpu_reset_lifecycle() {
    std::cout << "  Test 5: CPU::reset() lifecycle state restoration... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.start();
    cpu.stop(1);

    TEST_ASSERT(!cpu.is_started());
    TEST_ASSERT(cpu.is_stopped());
    pop_all_events(mgr);

    cpu.reset();
    TEST_ASSERT(!cpu.is_started());
    TEST_ASSERT(!cpu.is_stopped());

    // Should be able to start again after reset
    cpu.set_program_counter(0x5000);
    cpu.start();
    TEST_ASSERT(cpu.is_started());

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 1);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::CPUStarted));
    TEST_ASSERT(events[0].pc == 0x5000);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 6: Sequence Monotonicity between Start, Step, and Stop
// -----------------------------------------------------------------------------
void test_sequence_monotonicity() {
    std::cout << "  Test 6: Monotonic sequence ordering across lifecycle... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    Memory mem(0x10000);
    mem.write_u32(0x2000, enc_nop());
    mem.write_u32(0x2004, enc_nop());
    mem.write_u32(0x2008, enc_nop());
    CPU cpu;
    setup_test_cpu(cpu, mem, 1, 0x1000);
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();
    cpu.set_program_counter(0x2000);

    // 1. CPU Started (seq 1)
    cpu.start();

    // 2. Step 3 NOP instructions (seq 2, 3, 4)
    cpu.step(mem);
    cpu.step(mem);
    cpu.step(mem);

    // 3. CPU Stopped (seq 5)
    cpu.stop(0);

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 5);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::CPUStarted));
    TEST_ASSERT(events[0].sequence_number == 1);

    TEST_ASSERT(events[1].event_type == static_cast<std::uint16_t>(TraceEventType::InstructionRetired));
    TEST_ASSERT(events[1].sequence_number == 2);

    TEST_ASSERT(events[2].event_type == static_cast<std::uint16_t>(TraceEventType::InstructionRetired));
    TEST_ASSERT(events[2].sequence_number == 3);

    TEST_ASSERT(events[3].event_type == static_cast<std::uint16_t>(TraceEventType::InstructionRetired));
    TEST_ASSERT(events[3].sequence_number == 4);

    TEST_ASSERT(events[4].event_type == static_cast<std::uint16_t>(TraceEventType::CPUStopped));
    TEST_ASSERT(events[4].sequence_number == 5);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 7: SMPCluster::initialize_core(0, ...) emits CPUStarted
// -----------------------------------------------------------------------------
void test_smp_cluster_init_core_0() {
    std::cout << "  Test 7: SMPCluster::initialize_core(0) emits CPUStarted... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    SMPCluster cluster(2);
    cluster.get_core(0).bind_trace_manager(&mgr);
    cluster.get_core(0).reset_instruction_trace_sequence();

    cluster.initialize_core(0, 0x4000);

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 1);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::CPUStarted));
    TEST_ASSERT(events[0].pc == 0x4000);
    TEST_ASSERT(events[0].core_id == 0);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 8: SMPCluster::start_secondary_core(1, ...) emits CPUStarted
// -----------------------------------------------------------------------------
void test_smp_cluster_start_secondary() {
    std::cout << "  Test 8: SMPCluster::start_secondary_core(1) emits CPUStarted... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    SMPCluster cluster(2);
    cluster.get_core(1).bind_trace_manager(&mgr);
    cluster.get_core(1).reset_instruction_trace_sequence();

    cluster.start_secondary_core(1, 0x8000);

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 1);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::CPUStarted));
    TEST_ASSERT(events[0].pc == 0x8000);
    TEST_ASSERT(events[0].core_id == 1);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 9: PSCIService::do_cpu_off(1) emits CPUStopped(1)
// -----------------------------------------------------------------------------
void test_psci_cpu_off() {
    std::cout << "  Test 9: PSCIService::do_cpu_off(1) emits CPUStopped... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    SMPCluster cluster(2);
    cluster.enable_psci();
    cluster.get_core(1).bind_trace_manager(&mgr);
    cluster.start_secondary_core(1, 0x8000);
    pop_all_events(mgr);

    cluster.get_core(1).write_register(0, winios::PSCIService::kCpuOff);
    bool handled_off = cluster.get_psci_service()->handle_smc(cluster.get_core(1), 1);
    TEST_ASSERT(handled_off);

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 1);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::CPUStopped));
    TEST_ASSERT(events[0].core_id == 1);
    TEST_ASSERT(events[0].data == 1); // PSCI_CPU_OFF
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 10: PSCIService::do_system_off() emits CPUStopped on all cores
// -----------------------------------------------------------------------------
void test_psci_system_off() {
    std::cout << "  Test 10: PSCIService::do_system_off() emits CPUStopped on all cores... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    SMPCluster cluster(2);
    cluster.enable_psci();
    cluster.get_core(0).bind_trace_manager(&mgr);
    cluster.get_core(1).bind_trace_manager(&mgr);

    cluster.initialize_core(0, 0x4000);
    cluster.start_secondary_core(1, 0x8000);
    pop_all_events(mgr);

    cluster.get_core(0).write_register(0, winios::PSCIService::kSystemOff);
    bool handled_sysoff = cluster.get_psci_service()->handle_smc(cluster.get_core(0), 0);
    TEST_ASSERT(handled_sysoff);

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::CPUStopped));
    TEST_ASSERT(events[0].data == 2); // PSCI_SYSTEM_OFF
    TEST_ASSERT(events[1].event_type == static_cast<std::uint16_t>(TraceEventType::CPUStopped));
    TEST_ASSERT(events[1].data == 2); // PSCI_SYSTEM_OFF
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 11: PSCIService::do_system_reset() emits CPUStopped on all cores
// -----------------------------------------------------------------------------
void test_psci_system_reset() {
    std::cout << "  Test 11: PSCIService::do_system_reset() emits CPUStopped on all cores... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    SMPCluster cluster(2);
    cluster.enable_psci();
    cluster.get_core(0).bind_trace_manager(&mgr);
    cluster.get_core(1).bind_trace_manager(&mgr);

    cluster.initialize_core(0, 0x4000);
    cluster.start_secondary_core(1, 0x8000);
    pop_all_events(mgr);

    cluster.get_core(0).write_register(0, winios::PSCIService::kSystemReset);
    bool handled_reset = cluster.get_psci_service()->handle_smc(cluster.get_core(0), 0);
    TEST_ASSERT(handled_reset);

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::CPUStopped));
    TEST_ASSERT(events[0].data == 3); // PSCI_SYSTEM_RESET
    TEST_ASSERT(events[1].event_type == static_cast<std::uint16_t>(TraceEventType::CPUStopped));
    TEST_ASSERT(events[1].data == 3); // PSCI_SYSTEM_RESET
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 12: Temporary halt via set_halted does NOT emit CPUStopped
// -----------------------------------------------------------------------------
void test_temporary_halt_no_event() {
    std::cout << "  Test 12: Temporary halt does not emit CPUStopped... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.start();
    pop_all_events(mgr);

    cpu.set_halted(true);
    TEST_ASSERT(pop_all_events(mgr).empty());

    cpu.set_halted(false);
    TEST_ASSERT(pop_all_events(mgr).empty());
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 13: WFI/WFE execution does NOT emit CPUStopped
// -----------------------------------------------------------------------------
void test_wfi_wfe_execution_no_stop_event() {
    std::cout << "  Test 13: WFI/WFE execution does not emit CPUStopped... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    Memory mem(0x10000);
    mem.write_u32(0x1000, enc_wfi());
    mem.write_u32(0x1004, enc_wfe());

    CPU cpu;
    setup_test_cpu(cpu, mem, 1, 0x20000);
    cpu.bind_trace_manager(&mgr);
    cpu.set_program_counter(0x1000);
    cpu.start();
    pop_all_events(mgr);

    // Step WFI
    cpu.step(mem);
    auto wfi_events = pop_all_events(mgr);
    for (const auto& ev : wfi_events) {
        TEST_ASSERT(ev.event_type != static_cast<std::uint16_t>(TraceEventType::CPUStopped));
    }

    // Clear wait and step WFE
    cpu.wake_from_wfi();
    cpu.set_program_counter(0x1004);
    cpu.step(mem);
    auto wfe_events = pop_all_events(mgr);
    for (const auto& ev : wfe_events) {
        TEST_ASSERT(ev.event_type != static_cast<std::uint16_t>(TraceEventType::CPUStopped));
    }
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 14: Wakeup from WFI does NOT emit CPUStarted
// -----------------------------------------------------------------------------
void test_wfi_wakeup_no_start_event() {
    std::cout << "  Test 14: Wakeup from WFI does not emit CPUStarted... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.start();
    pop_all_events(mgr);

    cpu.wake_from_wfi();
    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.empty());
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 15: Event filtering for CPUStarted and CPUStopped
// -----------------------------------------------------------------------------
void test_event_filtering() {
    std::cout << "  Test 15: Event filtering for lifecycle events... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();

    // Disable CPUStarted
    cfg.event_types.set(TraceEventType::CPUStarted, false);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.start();
    TEST_ASSERT(pop_all_events(mgr).empty());

    // CPUStopped is still enabled
    cpu.stop(99);
    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 1);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::CPUStopped));

    mgr.stop();

    // Now disable CPUStopped and enable CPUStarted
    cfg.event_types.set(TraceEventType::CPUStarted, true);
    cfg.event_types.set(TraceEventType::CPUStopped, false);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    cpu.reset();
    cpu.start();
    events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 1);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::CPUStarted));

    cpu.stop(99);
    TEST_ASSERT(pop_all_events(mgr).empty());
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 16: Ring buffer boundary handling under multi-core start/stop cycles
// -----------------------------------------------------------------------------
void test_ring_buffer_boundary() {
    std::cout << "  Test 16: Ring buffer boundary under lifecycle churn... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 64; // Small ring buffer
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    SMPCluster cluster(4);
    for (std::size_t i = 0; i < 4; ++i) {
        cluster.get_core(i).bind_trace_manager(&mgr);
    }

    std::size_t total_events = 0;
    for (int cycle = 0; cycle < 10; ++cycle) {
        for (std::size_t i = 0; i < 4; ++i) {
            cluster.initialize_core(i, 0x1000 + i * 0x100);
            cluster.get_core(i).stop(cycle);
            cluster.get_core(i).reset();
        }
        auto evs = pop_all_events(mgr);
        total_events += evs.size();
    }

    TEST_ASSERT(total_events == 80); // 10 cycles * 4 cores * 2 events
    std::cout << "PASS\n";
}

} // namespace

int main() {
    std::cout << "=== WinIOS Phase 52 Stage 13: CPU Lifecycle Trace Unit Tests ===\n\n";

    test_cpu_start_emits_event();
    test_cpu_start_idempotency();
    test_cpu_stop_emits_event();
    test_cpu_stop_idempotency();
    test_cpu_reset_lifecycle();
    test_sequence_monotonicity();
    test_smp_cluster_init_core_0();
    test_smp_cluster_start_secondary();
    test_psci_cpu_off();
    test_psci_system_off();
    test_psci_system_reset();
    test_temporary_halt_no_event();
    test_wfi_wfe_execution_no_stop_event();
    test_wfi_wakeup_no_start_event();
    test_event_filtering();
    test_ring_buffer_boundary();

    std::cout << "\nAll 16 CPU lifecycle trace unit tests PASSED.\n";
    return 0;
}

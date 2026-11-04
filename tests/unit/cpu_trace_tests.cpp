#include "cpu/cpu.h"
#include "memory/memory.h"
#include "trace/trace_manager.h"
#include "trace/trace_event.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace winios::trace;

namespace {

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at line " << __LINE__ << "\n"; \
        std::abort(); \
    } \
} while(0)

// Helper: writes 32-bit instruction at address
void write_instruction(Memory& memory, std::uint64_t address, std::uint32_t inst) {
    memory.write_u32(static_cast<std::size_t>(address), inst);
}

void test_1_single_instruction_retirement() {
    std::cout << "  Test 1: Successful instruction emits exactly one retirement event... ";
    Memory memory(0x10000);
    CPU cpu;
    // MOV X0, #10: 0xD2800140
    write_instruction(memory, 0x1000, 0xD2800140u);
    cpu.set_program_counter(0x1000);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.reset();
    cfg.event_types.set(TraceEventType::InstructionRetired, true);
    cfg.ring_capacity = 64;
    cfg.file_output_enabled = false;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    cpu.bind_trace_manager(&mgr);
    cpu.step(memory);

    TEST_ASSERT(cpu.read_register(0) == 10);
    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_submitted == 1);
    TEST_ASSERT(stats.events_accepted == 1);
    TEST_ASSERT(stats.events_filtered == 0);
    TEST_ASSERT(stats.events_dropped == 0);

    TraceEvent ev;
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev));
    TEST_ASSERT(ev.sequence_number == 1);
    TEST_ASSERT(ev.pc == 0x1000);
    TEST_ASSERT(ev.opcode_or_syndrome == 0xD2800140u);
    TEST_ASSERT(ev.get_event_type() == TraceEventType::InstructionRetired);
    TEST_ASSERT(ev.core_id == 0);

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

void test_2_multiple_instructions_count() {
    std::cout << "  Test 2: Multiple successful instructions emit correct event count... ";
    Memory memory(0x10000);
    CPU cpu;
    // MOV X0, #10; MOV X1, #20; ADD X2, X0, X1
    write_instruction(memory, 0x1000, 0xD2800140u);
    write_instruction(memory, 0x1004, 0xD2800281u);
    write_instruction(memory, 0x1008, 0x8B010002u);
    cpu.set_program_counter(0x1000);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.reset();
    cfg.event_types.set(TraceEventType::InstructionRetired, true);
    cfg.ring_capacity = 64;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    cpu.bind_trace_manager(&mgr);
    cpu.step(memory);
    cpu.step(memory);
    cpu.step(memory);

    TEST_ASSERT(cpu.read_register(2) == 30);
    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_accepted == 3);

    for (std::uint64_t i = 1; i <= 3; ++i) {
        TraceEvent ev;
        TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev));
        TEST_ASSERT(ev.sequence_number == i);
    }

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

void test_3_and_4_branch_instruction_pc_semantics() {
    std::cout << "  Tests 3 & 4: Branch instruction emits one event with original PC... ";
    Memory memory(0x10000);
    CPU cpu;
    // At 0x1000: B #8 (jumps to 0x1008, skipping 0x1004) -> opcode 0x14000002
    write_instruction(memory, 0x1000, 0x14000002u);
    write_instruction(memory, 0x1004, 0xD503201Fu); // NOP (skipped)
    write_instruction(memory, 0x1008, 0xD2800140u); // MOV X0, #10
    cpu.set_program_counter(0x1000);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.reset();
    cfg.event_types.set(TraceEventType::InstructionRetired, true);
    cfg.ring_capacity = 64;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    cpu.bind_trace_manager(&mgr);
    // Execute branch at 0x1000
    cpu.step(memory);
    TEST_ASSERT(cpu.get_program_counter() == 0x1008);

    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_accepted == 1);

    TraceEvent ev;
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev));
    // CRITICAL: pc must be the original execution address (0x1000), NOT next PC (0x1008)
    TEST_ASSERT(ev.pc == 0x1000);
    TEST_ASSERT(ev.opcode_or_syndrome == 0x14000002u);
    TEST_ASSERT(ev.sequence_number == 1);

    // Now execute target at 0x1008
    cpu.step(memory);
    TEST_ASSERT(cpu.read_register(0) == 10);
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev));
    TEST_ASSERT(ev.pc == 0x1008);
    TEST_ASSERT(ev.sequence_number == 2);

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

void test_5_sequential_execution_addresses() {
    std::cout << "  Test 5: Sequential execution records correct instruction addresses... ";
    Memory memory(0x10000);
    CPU cpu;
    write_instruction(memory, 0x2000, 0xD503201Fu); // NOP
    write_instruction(memory, 0x2004, 0xD503201Fu); // NOP
    write_instruction(memory, 0x2008, 0xD503201Fu); // NOP
    cpu.set_program_counter(0x2000);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.reset();
    cfg.event_types.set(TraceEventType::InstructionRetired, true);
    cfg.ring_capacity = 64;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    cpu.bind_trace_manager(&mgr);
    cpu.step(memory);
    cpu.step(memory);
    cpu.step(memory);

    TraceEvent ev1, ev2, ev3;
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev1));
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev2));
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev3));

    TEST_ASSERT(ev1.pc == 0x2000);
    TEST_ASSERT(ev2.pc == 0x2004);
    TEST_ASSERT(ev3.pc == 0x2008);

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

void test_6_w_and_x_register_opcodes() {
    std::cout << "  Test 6: W-register and X-register instructions preserve correct opcodes... ";
    Memory memory(0x10000);
    CPU cpu;
    // MOV W0, #42: 0x52800540u
    // MOV X1, #42: 0xD2800541u
    write_instruction(memory, 0x1000, 0x52800540u);
    write_instruction(memory, 0x1004, 0xD2800541u);
    cpu.set_program_counter(0x1000);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.reset();
    cfg.event_types.set(TraceEventType::InstructionRetired, true);
    cfg.ring_capacity = 64;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    cpu.bind_trace_manager(&mgr);
    cpu.step(memory);
    cpu.step(memory);

    TraceEvent ev_w, ev_x;
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev_w));
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev_x));

    TEST_ASSERT(ev_w.opcode_or_syndrome == 0x52800540u);
    TEST_ASSERT(ev_x.opcode_or_syndrome == 0xD2800541u);

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

void test_7_and_8_core_id_and_exception_level() {
    std::cout << "  Tests 7 & 8: Correct core ID and Exception Level recorded... ";
    Memory memory(0x10000);
    CPU cpu;
    write_instruction(memory, 0x1000, 0xD503201Fu);
    cpu.set_program_counter(0x1000);
    cpu.set_core_id(3);
    cpu.set_current_el(1); // EL1 = Supervisor / privileged

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.reset();
    cfg.event_types.set(TraceEventType::InstructionRetired, true);
    cfg.ring_capacity = 64;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    cpu.bind_trace_manager(&mgr);
    cpu.step(memory);

    TraceEvent ev;
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev));
    TEST_ASSERT(ev.core_id == 3);
    TEST_ASSERT(ev.exception_level == 1);
    TEST_ASSERT(ev.has_flag(TraceEventFlags::IsPrivileged));

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

void test_9_fetch_failure_emits_no_event() {
    std::cout << "  Test 9: Failed instruction fetch emits no retirement event... ";
    Memory memory(0x1000); // 4 KiB RAM
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    // Set PC way beyond RAM size -> fetch abort
    cpu.set_program_counter(0x80000);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.reset();
    cfg.event_types.set(TraceEventType::InstructionRetired, true);
    cfg.ring_capacity = 64;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    cpu.bind_trace_manager(&mgr);
    cpu.step(memory);

    // Exception taken, no instruction retired
    TEST_ASSERT(cpu.exception_taken_in_step());
    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_accepted == 0);
    TEST_ASSERT(mgr.ring_buffer()->empty());

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

void test_10_decode_failure_emits_no_event() {
    std::cout << "  Test 10: Decode failure (unallocated encoding) emits no retirement event... ";
    Memory memory(0x10000);
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    // 0x00000000 is unallocated in AArch64 (UDF #0)
    write_instruction(memory, 0x1000, 0x00000000u);
    cpu.set_program_counter(0x1000);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.reset();
    cfg.event_types.set(TraceEventType::InstructionRetired, true);
    cfg.ring_capacity = 64;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    cpu.bind_trace_manager(&mgr);
    cpu.step(memory);

    TEST_ASSERT(cpu.exception_taken_in_step());
    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_accepted == 0);
    TEST_ASSERT(mgr.ring_buffer()->empty());

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

void test_11_data_abort_emits_no_event() {
    std::cout << "  Test 11: Faulting data access emits no retirement event... ";
    Memory memory(0x2000); // 8 KiB RAM
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    // LDR X0, [X1] where X1 points to 0x50000 (out of bounds -> data abort)
    // LDR X0, [X1]: 0xF9400020u
    write_instruction(memory, 0x1000, 0xF9400020u);
    cpu.write_register(1, 0x50000);
    cpu.set_program_counter(0x1000);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.reset();
    cfg.event_types.set(TraceEventType::InstructionRetired, true);
    cfg.ring_capacity = 64;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    cpu.bind_trace_manager(&mgr);
    cpu.step(memory);

    TEST_ASSERT(cpu.exception_taken_in_step());
    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_accepted == 0);
    TEST_ASSERT(mgr.ring_buffer()->empty());

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

void test_12_halted_cpu_emits_no_event() {
    std::cout << "  Test 12: Halted CPU emits no retirement event... ";
    Memory memory(0x10000);
    CPU cpu;
    write_instruction(memory, 0x1000, 0xD503201Fu);
    cpu.set_program_counter(0x1000);
    cpu.set_halted(true);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.reset();
    cfg.event_types.set(TraceEventType::InstructionRetired, true);
    cfg.ring_capacity = 64;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    cpu.bind_trace_manager(&mgr);
    cpu.step(memory);

    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_accepted == 0);
    TEST_ASSERT(mgr.ring_buffer()->empty());
    // PC must not have moved
    TEST_ASSERT(cpu.get_program_counter() == 0x1000);

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

void test_13_disabled_tracing_emits_no_event() {
    std::cout << "  Test 13: Disabled tracing emits no events... ";
    Memory memory(0x10000);
    CPU cpu;
    write_instruction(memory, 0x1000, 0xD2800140u);
    cpu.set_program_counter(0x1000);

    // Unconfigured / unstarted manager
    TraceManager mgr;
    cpu.bind_trace_manager(&mgr);
    cpu.step(memory);

    TEST_ASSERT(cpu.read_register(0) == 10);
    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_accepted == 0);

    // Null trace manager
    cpu.bind_trace_manager(nullptr);
    cpu.set_program_counter(0x1000);
    cpu.step(memory);
    TEST_ASSERT(cpu.read_register(0) == 10);
    std::cout << "PASS\n";
}

void test_14_event_type_filtering_suppresses_retirement() {
    std::cout << "  Test 14: Event type filtering suppresses retirement events... ";
    Memory memory(0x10000);
    CPU cpu;
    write_instruction(memory, 0x1000, 0xD2800140u);
    cpu.set_program_counter(0x1000);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 64;
    // Disable InstructionRetired type
    cfg.event_types.set(TraceEventType::InstructionRetired, false);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    cpu.bind_trace_manager(&mgr);
    cpu.step(memory);

    TEST_ASSERT(cpu.read_register(0) == 10);
    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_accepted == 0);
    TEST_ASSERT(stats.events_filtered == 0); // Fast hot-path check skipped submission entirely!

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Phase 52 Stage 6: CPU Trace Unit Tests ===\n\n";

    test_1_single_instruction_retirement();
    test_2_multiple_instructions_count();
    test_3_and_4_branch_instruction_pc_semantics();
    test_5_sequential_execution_addresses();
    test_6_w_and_x_register_opcodes();
    test_7_and_8_core_id_and_exception_level();
    test_9_fetch_failure_emits_no_event();
    test_10_decode_failure_emits_no_event();
    test_11_data_abort_emits_no_event();
    test_12_halted_cpu_emits_no_event();
    test_13_disabled_tracing_emits_no_event();
    test_14_event_type_filtering_suppresses_retirement();

    std::cout << "\n=== All CPU Trace Unit Tests PASSED Successfully! ===\n";
    return 0;
}

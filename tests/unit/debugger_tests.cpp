#include "debugger/debugger.h"
#include "test_harness.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

void tracing_reports_instruction_without_changing_state() {
    CPU cpu;
    Memory memory(4);
    memory.write_u32(0, 0xD280055Fu); // MOVZ XZR, #42
    std::vector<CPU::TraceRecord> records;
    Debugger debugger(cpu, memory);

    debugger.set_tracing(true, [&](const CPU::TraceRecord& record) {
        records.push_back(record);
    });
    debugger.step();

    CHECK(records.size() == 1);
    CHECK(records[0].instruction_address == 0);
    CHECK(records[0].instruction_word == 0xD280055Fu);
    CHECK(records[0].disassembly == "MOVZ XZR, #42");
    CHECK(records[0].program_counter_after == 4);
    CHECK(records[0].changed_registers.empty());
    CHECK(cpu.get_program_counter() == 4);
}

void tracing_can_be_disabled() {
    CPU cpu;
    Memory memory(4);
    memory.write_u32(0, 0xD280055Fu); // MOVZ XZR, #42
    std::vector<CPU::TraceRecord> records;
    Debugger debugger(cpu, memory);

    debugger.set_tracing(true, [&](const CPU::TraceRecord& record) {
        records.push_back(record);
    });
    debugger.set_tracing(false);
    debugger.step();

    CHECK(records.empty());
}

void tracing_reports_changed_register() {
    CPU cpu;
    Memory memory(4);
    memory.write_u32(0, 0xD2800A21u); // MOVZ X1, #0x51
    std::vector<CPU::TraceRecord> records;
    Debugger debugger(cpu, memory);

    debugger.set_tracing(true, [&](const CPU::TraceRecord& record) {
        records.push_back(record);
    });
    debugger.step();

    CHECK(records.size() == 1);
    CHECK(records[0].changed_registers.size() == 1);
    CHECK(records[0].changed_registers[0] == "X1");
}

void tracing_decodes_expanded_instructions() {
    CPU cpu;
    Memory memory(24);
    memory.write_u32(0, 0x92A24680u); // MOVN X0, #0x1234, LSL #16
    memory.write_u32(4, 0xAA021020u); // ORR X0, X1, X2, LSL #4
    memory.write_u32(8, 0xF9400422u); // LDR X2, [X1, #8]
    std::vector<CPU::TraceRecord> records;
    Debugger debugger(cpu, memory);

    debugger.set_tracing(true, [&](const CPU::TraceRecord& record) {
        records.push_back(record);
    });
    cpu.write_register(1, 8);
    cpu.write_register(2, 0x10);
    memory.write_u64(16, 0x20);
    debugger.step();
    debugger.step();
    debugger.step();

    CHECK(records.size() == 3);
    CHECK(records[0].disassembly == "MOVN X0, #4660, LSL #16");
    CHECK(records[1].disassembly == "ORR X0, X1, X2, LSL #4");
    CHECK(records[2].disassembly == "LDR X2, [X1, #8]");
    CHECK(records[2].instruction_address == 8);
    CHECK(records[2].instruction_word == 0xF9400422u);
}

void debugger_inspects_cpu_and_memory() {
    CPU cpu;
    Memory memory(8);
    cpu.write_register(0, 42);
    cpu.set_stack_pointer(0x1000);
    memory.write_u32(0, 0x11223344u);
    Debugger debugger(cpu, memory);

    const std::string cpu_state = debugger.inspect_cpu();
    CHECK(cpu_state.find("PC=0x0000000000000000") != std::string::npos);
    CHECK(cpu_state.find("SP=0x0000000000001000") != std::string::npos);
    CHECK(cpu_state.find("X0=0x000000000000002A") != std::string::npos);
    CHECK(debugger.inspect_memory(0, 4) == "0x44 0x33 0x22 0x11");
}

void debugger_rejects_invalid_memory_ranges() {
    CPU cpu;
    Memory memory(8);
    Debugger debugger(cpu, memory);

    CHECK_OUT_OF_RANGE(debugger.inspect_memory(7, 2));
    CHECK_OUT_OF_RANGE(debugger.inspect_memory(9, 0));
}

void debugger_step_and_continue_are_bounded() {
    CPU cpu;
    Memory memory(8);
    memory.write_u32(0, 0xD280055Fu); // MOVZ XZR, #42
    memory.write_u32(4, 0x14000000u); // B .
    Debugger debugger(cpu, memory);

    debugger.step();
    CHECK(cpu.get_program_counter() == 4);
    CHECK(debugger.continue_execution(3) == 3);
    CHECK(cpu.get_program_counter() == 4);
}

void debugger_requires_sink_when_enabling_tracing() {
    CPU cpu;
    Memory memory(4);
    Debugger debugger(cpu, memory);

    CHECK_INVALID_ARGUMENT(debugger.set_tracing(true));
}

} // namespace

int main() {
    tracing_reports_instruction_without_changing_state();
    tracing_can_be_disabled();
    tracing_reports_changed_register();
    tracing_decodes_expanded_instructions();
    debugger_inspects_cpu_and_memory();
    debugger_rejects_invalid_memory_ranges();
    debugger_step_and_continue_are_bounded();
    debugger_requires_sink_when_enabling_tracing();
    return test::summarize("debugger_tests");
}

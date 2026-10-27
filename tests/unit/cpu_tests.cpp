#include "cpu/cpu.h"
#include "memory/memory.h"
#include "test_harness.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace {

void constructor_starts_zeroed() {
    CPU cpu;

    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
        CHECK(cpu.read_register(i) == 0);
    }
    CHECK(cpu.get_program_counter() == 0);
}

void reset_clears_all_registers() {
    CPU cpu;
    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
        cpu.write_register(i, static_cast<std::uint64_t>(i) + 100);
    }

    cpu.reset();

    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
        CHECK(cpu.read_register(i) == 0);
    }
}

void reset_clears_program_counter() {
    CPU cpu;
    cpu.set_program_counter(0xFFFF0000ull);

    cpu.reset();

    CHECK(cpu.get_program_counter() == 0);
}

void reset_clears_stack_pointer() {
    CPU cpu;
    cpu.set_stack_pointer(0x1234);
    cpu.reset();
    CHECK(cpu.get_stack_pointer() == 0);
}

void register_read_write() {
    CPU cpu;

    cpu.write_register(0, 0x0123456789ABCDEFull);
    cpu.write_register(7, 0xDEADBEEFull);
    cpu.write_register(30, 42);

    CHECK(cpu.read_register(0) == 0x0123456789ABCDEFull);
    CHECK(cpu.read_register(7) == 0xDEADBEEFull);
    CHECK(cpu.read_register(30) == 42);
    CHECK(cpu.read_register(1) == 0);
}

void register_30_is_valid() {
    CPU cpu;

    cpu.write_register(30, std::numeric_limits<std::uint64_t>::max());

    CHECK(cpu.read_register(30) == std::numeric_limits<std::uint64_t>::max());
    CHECK(CPU::kRegisterCount == 31);
}

void register_31_is_rejected() {
    CPU cpu;

    CHECK_OUT_OF_RANGE(cpu.read_register(31));
    CHECK_OUT_OF_RANGE(cpu.write_register(31, 1));
}

void huge_register_index_is_rejected() {
    CPU cpu;
    constexpr std::size_t huge = std::numeric_limits<std::size_t>::max();

    CHECK_OUT_OF_RANGE(cpu.read_register(huge));
    CHECK_OUT_OF_RANGE(cpu.write_register(huge, 1));
}

void program_counter_read_write() {
    CPU cpu;

    cpu.set_program_counter(0x40000000ull);
    CHECK(cpu.get_program_counter() == 0x40000000ull);

    cpu.set_program_counter(0);
    CHECK(cpu.get_program_counter() == 0);

    cpu.set_program_counter(std::numeric_limits<std::uint64_t>::max());
    CHECK(cpu.get_program_counter() == std::numeric_limits<std::uint64_t>::max());
}

void demo_preserves_expected_result() {
    CPU cpu;

    cpu.run_demo();

    CHECK(cpu.read_register(0) == 10);
    CHECK(cpu.read_register(1) == 20);
    CHECK(cpu.read_register(2) == 30);
    CHECK(cpu.get_program_counter() == 0);
}

void movz_writes_immediate() {
    CPU cpu;
    Memory memory(4);
    memory.write_u32(0, 0xD2800A21u); // MOVZ X1, #0x51 (Arm ARM encoding)

    cpu.step(memory);

    CHECK(cpu.read_register(1) == 0x51);
    CHECK(cpu.get_program_counter() == 4);
}

void movz_supports_halfword_shift() {
    CPU cpu;
    Memory memory(4);
    memory.write_u32(0, 0xD2A24682u); // MOVZ X2, #0x1234, LSL #16

    cpu.step(memory);

    CHECK(cpu.read_register(2) == 0x12340000ull);
}

void movz_destination_31_is_zero_register() {
    CPU cpu;
    Memory memory(4);
    memory.write_u32(0, 0xD2800540u); // MOVZ XZR, #42 (destination discarded)

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 4);
    CHECK(cpu.get_stack_pointer() == 0);
}

void add_immediate_writes_result() {
    CPU cpu;
    Memory memory(4);
    cpu.write_register(3, 10);
    memory.write_u32(0, 0x91002860u); // ADD X0, X3, #10

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 20);
    CHECK(cpu.get_program_counter() == 4);
}

void add_immediate_supports_shifted_immediate() {
    CPU cpu;
    Memory memory(4);
    cpu.write_register(1, 7);
    memory.write_u32(0, 0x91401420u); // ADD X0, X1, #5, LSL #12

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 7u + (5u << 12u));
    CHECK(cpu.get_program_counter() == 4);
}

void add_immediate_uses_stack_pointer_for_register_31() {
    CPU cpu;
    Memory memory(8);
    cpu.set_stack_pointer(100);
    memory.write_u32(0, 0x9100281Fu); // ADD SP, X0, #10
    memory.write_u32(4, 0x91002BE1u); // ADD X1, SP, #10

    cpu.step(memory);
    cpu.step(memory);

    CHECK(cpu.get_stack_pointer() == 10);
    CHECK(cpu.read_register(1) == 20);
}

void branch_changes_pc_relative_to_instruction() {
    CPU cpu;
    Memory memory(12);
    memory.write_u32(0, 0x14000002u); // B +8

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 8);
}

void branch_supports_negative_offset() {
    CPU cpu;
    Memory memory(8);
    cpu.set_program_counter(4);
    memory.write_u32(4, 0x17FFFFFFu); // B -4

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0);
}

void unsupported_instruction_fails_explicitly() {
    CPU cpu;
    Memory memory(4);
    memory.write_u32(0, 0);

    CHECK_RUNTIME_ERROR(cpu.step(memory));
}

void unsupported_movz_width_fails_explicitly() {
    CPU cpu;
    Memory memory(4);
    memory.write_u32(0, 0x52800540u); // MOVZ W0, #42 (32-bit form unsupported)

    CHECK_RUNTIME_ERROR(cpu.step(memory));
}

void instruction_fetch_beyond_memory_fails() {
    CPU cpu;
    Memory memory(4);
    cpu.set_program_counter(4);

    CHECK_OUT_OF_RANGE(cpu.step(memory));
}

void execution_limit_stops_self_branch() {
    CPU cpu;
    Memory memory(4);
    memory.write_u32(0, 0x14000000u); // B . (self branch)

    CHECK(cpu.run(memory, 5) == 5);
    CHECK(cpu.get_program_counter() == 0);
}

void zero_instruction_limit_executes_nothing() {
    CPU cpu;
    Memory memory(4);
    memory.write_u32(0, 0x14000000u); // B . (would branch to itself)

    CHECK(cpu.run(memory, 0) == 0);
    CHECK(cpu.get_program_counter() == 0);
}

} // namespace

int main() {
    constructor_starts_zeroed();
    reset_clears_all_registers();
    reset_clears_program_counter();
    reset_clears_stack_pointer();
    register_read_write();
    register_30_is_valid();
    register_31_is_rejected();
    huge_register_index_is_rejected();
    program_counter_read_write();
    demo_preserves_expected_result();
    movz_writes_immediate();
    movz_supports_halfword_shift();
    movz_destination_31_is_zero_register();
    add_immediate_writes_result();
    add_immediate_supports_shifted_immediate();
    add_immediate_uses_stack_pointer_for_register_31();
    branch_changes_pc_relative_to_instruction();
    branch_supports_negative_offset();
    unsupported_instruction_fails_explicitly();
    unsupported_movz_width_fails_explicitly();
    instruction_fetch_beyond_memory_fails();
    execution_limit_stops_self_branch();
    zero_instruction_limit_executes_nothing();

    return test::summarize("cpu_tests");
}

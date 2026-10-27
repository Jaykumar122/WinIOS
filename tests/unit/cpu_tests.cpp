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
    cpu.write_register(0, 7);
    memory.write_u32(0, 0xD280055Fu); // MOVZ XZR, #42 (destination discarded)

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 7);
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

void movn_inverts_shifted_immediate() {
    CPU cpu;
    Memory memory(4);
    memory.write_u32(0, 0x92A24680u); // MOVN X0, #0x1234, LSL #16

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFFEDCBFFFFull);
    CHECK(cpu.get_program_counter() == 4);
}

void movk_preserves_unselected_halfwords() {
    CPU cpu;
    Memory memory(4);
    cpu.write_register(0, 0x0123456789ABCDEFull);
    memory.write_u32(0, 0xF2AACF00u); // MOVK X0, #0x5678, LSL #16

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x012345675678CDEFu);
    CHECK(cpu.get_program_counter() == 4);
}

void movn_and_movk_discard_register_31_writes() {
    CPU cpu;
    Memory memory(4);
    cpu.set_stack_pointer(0x1234);
    memory.write_u32(0, 0xF280055Fu); // MOVK XZR, #42

    cpu.step(memory);

    CHECK(cpu.get_stack_pointer() == 0x1234);
    CHECK(cpu.get_program_counter() == 4);
}

void orr_supports_shifted_register() {
    CPU cpu;
    Memory memory(4);
    cpu.write_register(1, 1);
    cpu.write_register(2, 0x10);
    memory.write_u32(0, 0xAA021020u); // ORR X0, X1, X2, LSL #4

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x101);
    CHECK(cpu.get_program_counter() == 4);
}

void orr_supports_lsr_and_asr() {
    CPU cpu;
    Memory memory(8);
    cpu.write_register(1, 0x8000000000000010ull);
    cpu.write_register(2, 0x40);
    memory.write_u32(0, 0xAA421020u); // ORR X0, X1, X2, LSR #4
    memory.write_u32(4, 0xAA821020u); // ORR X0, X1, X2, ASR #4

    cpu.step(memory);
    CHECK(cpu.read_register(0) == 0x8000000000000014ull);
    cpu.write_register(2, 0x8000000000000000ull);
    cpu.step(memory);
    CHECK(cpu.read_register(0) == 0xF800000000000010ull);
}

void orr_rejects_reserved_shift_type() {
    CPU cpu;
    Memory memory(4);
    memory.write_u32(0, 0xAAC21020u); // ORR X0, X1, X2, reserved shift type

    CHECK_RUNTIME_ERROR(cpu.step(memory));
    CHECK(cpu.get_program_counter() == 0);
}

void sub_immediate_supports_shift_and_sp() {
    CPU cpu;
    Memory memory(8);
    cpu.write_register(1, 0x6000);
    memory.write_u32(0, 0xD1001420u); // SUB X0, X1, #5
    memory.write_u32(4, 0xD14023FFu); // SUB SP, SP, #8, LSL #12
    cpu.set_stack_pointer(0x20000);

    cpu.step(memory);
    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x5FFB);
    CHECK(cpu.get_stack_pointer() == 0x18000);
    CHECK(cpu.get_program_counter() == 8);
}

void ldr_and_str_use_scaled_offsets() {
    CPU cpu;
    Memory memory(32);
    cpu.write_register(1, 8);
    cpu.write_register(0, 0x0123456789ABCDEFull);
    memory.write_u64(16, 0);
    memory.write_u32(0, 0xF9000420u); // STR X0, [X1, #8]
    memory.write_u32(4, 0xF9400422u); // LDR X2, [X1, #8]

    cpu.step(memory);
    cpu.step(memory);

    CHECK(memory.read_u64(16) == 0x0123456789ABCDEFull);
    CHECK(cpu.read_register(2) == 0x0123456789ABCDEFull);
    CHECK(cpu.get_program_counter() == 8);
}

void ldr_and_str_support_sp_base() {
    CPU cpu;
    Memory memory(24);
    cpu.set_stack_pointer(8);
    cpu.write_register(0, 0xAABBCCDDEEFF0011ull);
    memory.write_u32(0, 0xF90007FFu); // STR XZR, [SP, #0x?] (zero store)
    memory.write_u32(4, 0xF94007E1u); // LDR X1, [SP, #0x?]
    memory.write_u64(16, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);
    cpu.step(memory);

    CHECK(memory.read_u64(8) == 0);
    CHECK(cpu.read_register(1) == 0);
}

void invalid_load_leaves_state_unchanged() {
    CPU cpu;
    Memory memory(8);
    cpu.write_register(0, 0x1234);
    cpu.write_register(1, 8);
    memory.write_u32(0, 0xF9400420u); // LDR X0, [X1, #8] (out of bounds)

    CHECK_OUT_OF_RANGE(cpu.step(memory));
    CHECK(cpu.read_register(0) == 0x1234);
    CHECK(cpu.get_program_counter() == 0);
}

void invalid_store_leaves_memory_and_pc_unchanged() {
    CPU cpu;
    Memory memory(8);
    cpu.write_register(0, 0x1234);
    cpu.write_register(1, 8);
    memory.write_u32(0, 0xF9000420u); // STR X0, [X1, #8] (out of bounds)

    CHECK_OUT_OF_RANGE(cpu.step(memory));
    CHECK(cpu.get_program_counter() == 0);
    CHECK(memory.read_u32(0) == 0xF9000420u);
}

void overflowing_effective_address_fails_without_state_change() {
    CPU cpu;
    Memory memory(16);
    cpu.write_register(0, 0x1234);
    cpu.write_register(1, std::numeric_limits<std::uint64_t>::max() - 3u);
    memory.write_u32(0, 0xF9400420u); // LDR X0, [X1, #8] (address overflow)

    CHECK_OUT_OF_RANGE(cpu.step(memory));
    CHECK(cpu.read_register(0) == 0x1234);
    CHECK(cpu.get_program_counter() == 0);
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

void branch_underflow_fails_without_changing_pc() {
    CPU cpu;
    Memory memory(4);
    memory.write_u32(0, 0x17FFFFFFu); // B -4 (target below guest address zero)

    CHECK_OUT_OF_RANGE(cpu.step(memory));
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

    cpu.step(memory);
    CHECK(cpu.read_register(0) == 42);
    CHECK(cpu.get_program_counter() == 4);
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

void register_add_sub_support_shifted_and_32_bit_forms() {
    CPU cpu;
    Memory memory(20);
    cpu.write_register(1, 10);
    cpu.write_register(2, 3);
    memory.write_u32(0, 0x8B020020u); // ADD X0, X1, X2
    memory.write_u32(4, 0xCB020020u); // SUB X0, X1, X2
    memory.write_u32(8, 0x8B021020u); // ADD X0, X1, X2, LSL #4
    cpu.step(memory);
    CHECK(cpu.read_register(0) == 13);
    cpu.step(memory);
    CHECK(cpu.read_register(0) == 7);
    cpu.step(memory);
    CHECK(cpu.read_register(0) == 58);

    cpu.write_register(1, 0xFFFFFFFF00000001ull);
    cpu.write_register(2, 2);
    memory.write_u32(12, 0x0B020020u); // ADD W0, W1, W2
    cpu.step(memory);
    CHECK(cpu.read_register(0) == 3);
}

void flag_setting_arithmetic_updates_nzcv() {
    CPU cpu;
    Memory memory(16);
    cpu.write_register(1, 0xFFFFFFFFFFFFFFFFull);
    memory.write_u32(0, 0xAB010020u); // ADDS X0, X1, X1
    cpu.step(memory);
    CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFFFFFEull);
    CHECK(cpu.get_nzcv() == 0xAu); // N and C

    cpu.write_register(1, 0);
    memory.write_u32(4, 0xF100043Fu); // SUBS XZR, X1, #1 (CMP X1, #1)
    cpu.step(memory);
    CHECK((cpu.get_nzcv() & 0xFu) == 0x8u); // N=1, Z=0, C=0, V=0

    cpu.write_register(1, 0x7FFFFFFFFFFFFFFFull);
    memory.write_u32(8, 0xB1000420u); // ADDS X0, X1, #1
    cpu.step(memory);
    CHECK(cpu.read_register(0) == 0x8000000000000000ull);
    CHECK(cpu.get_nzcv() == 0x9u); // N and V
}

void conditional_branches_and_compare_aliases_work() {
    CPU cpu;
    Memory memory(32);
    cpu.write_register(0, 1);
    memory.write_u32(0, 0xF100041Fu); // CMP X0, #1
    memory.write_u32(4, 0x54000040u); // B.EQ +8
    memory.write_u32(8, 0xD2800001u); // MOVZ X1, #0 (skipped)
    memory.write_u32(12, 0xD2800021u); // MOVZ X1, #1
    cpu.step(memory);
    cpu.step(memory);
    CHECK(cpu.get_program_counter() == 12);
    cpu.step(memory);
    CHECK(cpu.read_register(1) == 1);

    cpu.reset();
    memory.write_u32(0, 0xB4000040u); // CBZ X0, +8
    memory.write_u32(4, 0xD2800001u); // MOVZ X1, #0 (skipped)
    cpu.step(memory);
    CHECK(cpu.get_program_counter() == 8);

    cpu.reset();
    cpu.write_register(0, 1);
    memory.write_u32(0, 0xB5000040u); // CBNZ X0, +8
    cpu.step(memory);
    CHECK(cpu.get_program_counter() == 8);
}

void tst_alias_sets_zero_and_negative_flags() {
    CPU cpu;
    Memory memory(8);
    cpu.write_register(0, 0x8000000000000000ull);
    cpu.write_register(1, 0xFFFFFFFFFFFFFFFFull);
    memory.write_u32(0, 0xEA01001Fu); // TST X0, X1 (ANDS XZR, X0, X1)
    cpu.step(memory);
    CHECK((cpu.get_nzcv() & 0xCu) == 0x8u);
    CHECK(cpu.get_program_counter() == 4);
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
    movn_inverts_shifted_immediate();
    movk_preserves_unselected_halfwords();
    movn_and_movk_discard_register_31_writes();
    orr_supports_shifted_register();
    orr_supports_lsr_and_asr();
    orr_rejects_reserved_shift_type();
    sub_immediate_supports_shift_and_sp();
    ldr_and_str_use_scaled_offsets();
    ldr_and_str_support_sp_base();
    invalid_load_leaves_state_unchanged();
    invalid_store_leaves_memory_and_pc_unchanged();
    overflowing_effective_address_fails_without_state_change();
    branch_changes_pc_relative_to_instruction();
    branch_supports_negative_offset();
    branch_underflow_fails_without_changing_pc();
    unsupported_instruction_fails_explicitly();
    unsupported_movz_width_fails_explicitly();
    instruction_fetch_beyond_memory_fails();
    execution_limit_stops_self_branch();
    zero_instruction_limit_executes_nothing();
    register_add_sub_support_shifted_and_32_bit_forms();
    flag_setting_arithmetic_updates_nzcv();
    conditional_branches_and_compare_aliases_work();
    tst_alias_sets_zero_and_negative_flags();

    return test::summarize("cpu_tests");
}

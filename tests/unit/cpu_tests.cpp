#include "cpu/cpu.h"

#include "memory/memory.h"

#include "test_harness.h"



#include <bit>

#include <cmath>

#include <cstddef>

#include <cstdint>

#include <limits>

#include <stdexcept>

#include <string>

#include <vector>



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

    memory.write_u32(0, 0x2A028020u); // ORR W0, W1, W2, LSL #32 (reserved in 32-bit)



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





void unallocated_register_offset_option_is_rejected() {

    CPU cpu;

    Memory memory(0x40);

    cpu.write_register(1, 0x20);

    cpu.write_register(2, 0x4);

    const std::uint32_t words[] = {

        0xB8620820u, // option=000

        0xB8622820u, // option=001

        0xF8628820u, // option=100

        0xF862A820u, // option=101

    };

    for (const std::uint32_t word : words) {

        memory.write_u32(0, word);

        cpu.set_program_counter(0);

        CHECK_RUNTIME_ERROR(cpu.step(memory));

        CHECK(cpu.get_program_counter() == 0);

        CHECK(cpu.read_register(0) == 0);

    }

}



void load_store_pair_offset_pre_post_indexing() {

    CPU cpu;

    Memory memory(0x200);



    // 1. 32-bit STP / LDP signed scaled offset

    // STP W0, W1, [X2, #0] -> writes 8 bytes at 0x100

    memory.write_u32(0, 0x29000440u);

    cpu.write_register(0, 0xAABBCCDDu);

    cpu.write_register(1, 0x11223344u);

    cpu.write_register(2, 0x100);

    cpu.step(memory);

    CHECK(memory.read_u32(0x100) == 0xAABBCCDDu);

    CHECK(memory.read_u32(0x104) == 0x11223344u);

    CHECK(cpu.read_register(2) == 0x100); // no writeback

    CHECK(cpu.get_program_counter() == 4);



    // LDP W3, W4, [X2, #0]

    memory.write_u32(4, 0x29401043u);

    cpu.write_register(3, 0);

    cpu.write_register(4, 0);

    cpu.step(memory);

    CHECK(cpu.read_register(3) == 0xAABBCCDDull);

    CHECK(cpu.read_register(4) == 0x11223344ull);

    CHECK(cpu.get_program_counter() == 8);



    // 2. 32-bit STP / LDP pre-index: imm7 = 2 (offset = +8)

    // STP W0, W1, [X2, #8]! -> EA = 0x108, X2 becomes 0x108

    memory.write_u32(8, 0x29810440u);

    cpu.step(memory);

    CHECK(memory.read_u32(0x108) == 0xAABBCCDDu);

    CHECK(memory.read_u32(0x10C) == 0x11223344u);

    CHECK(cpu.read_register(2) == 0x108); // writeback



    // LDP W5, W6, [X2, #8]! -> EA = 0x110, X2 becomes 0x110

    memory.write_u32(0x110, 0x55667788u);

    memory.write_u32(0x114, 0x99AABBCCu);

    memory.write_u32(12, 0x29C11845u);

    cpu.step(memory);

    CHECK(cpu.read_register(5) == 0x55667788ull);

    CHECK(cpu.read_register(6) == 0x99AABBCCull);

    CHECK(cpu.read_register(2) == 0x110); // writeback



    // 3. 32-bit STP / LDP post-index: imm7 = 2 (offset = +8)

    // STP W0, W1, [X2], #8 -> EA = 0x110, X2 becomes 0x118

    memory.write_u32(16, 0x28810440u);

    cpu.step(memory);

    CHECK(memory.read_u32(0x110) == 0xAABBCCDDu);

    CHECK(memory.read_u32(0x114) == 0x11223344u);

    CHECK(cpu.read_register(2) == 0x118); // writeback



    // LDP W7, W8, [X2], #8 -> EA = 0x118, X2 becomes 0x120

    memory.write_u32(0x118, 0x12345678u);

    memory.write_u32(0x11C, 0x87654321u);

    memory.write_u32(20, 0x28C12047u);

    cpu.step(memory);

    CHECK(cpu.read_register(7) == 0x12345678ull);

    CHECK(cpu.read_register(8) == 0x87654321ull);

    CHECK(cpu.read_register(2) == 0x120);



    // 4. 64-bit STP / LDP signed scaled offset

    // STP X0, X1, [X2, #0] -> writes 16 bytes at 0x120

    memory.write_u32(24, 0xA9000440u);

    cpu.write_register(0, 0x0123456789ABCDEFull);

    cpu.write_register(1, 0xFEDCBA9876543210ull);

    cpu.step(memory);

    CHECK(memory.read_u64(0x120) == 0x0123456789ABCDEFull);

    CHECK(memory.read_u64(0x128) == 0xFEDCBA9876543210ull);

    CHECK(cpu.read_register(2) == 0x120); // no writeback



    // LDP X9, X10, [X2, #0]

    memory.write_u32(28, 0xA9402849u);

    cpu.step(memory);

    CHECK(cpu.read_register(9) == 0x0123456789ABCDEFull);

    CHECK(cpu.read_register(10) == 0xFEDCBA9876543210ull);



    // 5. 64-bit STP / LDP pre-index: imm7 = 2 (offset = +16)

    // STP X0, X1, [X2, #16]! -> EA = 0x130, X2 becomes 0x130

    memory.write_u32(32, 0xA9810440u);

    cpu.step(memory);

    CHECK(memory.read_u64(0x130) == 0x0123456789ABCDEFull);

    CHECK(memory.read_u64(0x138) == 0xFEDCBA9876543210ull);

    CHECK(cpu.read_register(2) == 0x130);



    // LDP X11, X12, [X2, #16]! -> EA = 0x140, X2 becomes 0x140

    memory.write_u64(0x140, 0x1111222233334444ull);

    memory.write_u64(0x148, 0x5555666677778888ull);

    memory.write_u32(36, 0xA9C1304Bu);

    cpu.step(memory);

    CHECK(cpu.read_register(11) == 0x1111222233334444ull);

    CHECK(cpu.read_register(12) == 0x5555666677778888ull);

    CHECK(cpu.read_register(2) == 0x140);



    // 6. 64-bit STP / LDP post-index: imm7 = 2 (offset = +16)

    // STP X0, X1, [X2], #16 -> EA = 0x140, X2 becomes 0x150

    memory.write_u32(40, 0xA8810440u);

    cpu.step(memory);

    CHECK(memory.read_u64(0x140) == 0x0123456789ABCDEFull);

    CHECK(memory.read_u64(0x148) == 0xFEDCBA9876543210ull);

    CHECK(cpu.read_register(2) == 0x150);



    // LDP X13, X14, [X2], #16 -> EA = 0x150, X2 becomes 0x160

    memory.write_u64(0x150, 0xAAAABBBBCCCCDDDDull);

    memory.write_u64(0x158, 0xEEEEFFFF00001111ull);

    memory.write_u32(44, 0xA8C1384Du);

    cpu.step(memory);

    CHECK(cpu.read_register(13) == 0xAAAABBBBCCCCDDDDull);

    CHECK(cpu.read_register(14) == 0xEEEEFFFF00001111ull);

    CHECK(cpu.read_register(2) == 0x160);

}



void load_store_pair_signed_imm7_scaling_and_boundaries() {

    CPU cpu;

    Memory memory(0x1000);



    // 32-bit max positive imm7 = +63 -> offset = +252 bytes

    // LDP W0, W1, [X2, #252] (0x295F8440u)

    memory.write_u32(0, 0x295F8440u);

    cpu.write_register(2, 0x100);

    memory.write_u32(0x1FC, 0x11111111u);

    memory.write_u32(0x200, 0x22222222u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x11111111ull);

    CHECK(cpu.read_register(1) == 0x22222222ull);

    CHECK(cpu.read_register(2) == 0x100);



    // 32-bit min negative imm7 = -64 -> offset = -256 bytes

    // LDP W0, W1, [X2, #-256] (0x29600440u)

    memory.write_u32(4, 0x29600440u);

    cpu.write_register(2, 0x300);

    memory.write_u32(0x200, 0x33333333u);

    memory.write_u32(0x204, 0x44444444u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x33333333ull);

    CHECK(cpu.read_register(1) == 0x44444444ull);



    // 64-bit max positive imm7 = +63 -> offset = +504 bytes

    // LDP X0, X1, [X2, #504] (0xA95F8440u)

    memory.write_u32(8, 0xA95F8440u);

    cpu.write_register(2, 0x100);

    memory.write_u64(0x100 + 504, 0xAAAAAAAAAAAAAAAAull);

    memory.write_u64(0x100 + 512, 0xBBBBBBBBBBBBBBBBull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xAAAAAAAAAAAAAAAAull);

    CHECK(cpu.read_register(1) == 0xBBBBBBBBBBBBBBBBull);



    // 64-bit min negative imm7 = -64 -> offset = -512 bytes

    // LDP X0, X1, [X2, #-512] (0xA9600440u)

    memory.write_u32(12, 0xA9600440u);

    cpu.write_register(2, 0x600);

    memory.write_u64(0x400, 0xCCCCCCCCCCCCCCCCull);

    memory.write_u64(0x408, 0xDDDDDDDDDDDDDDDDull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xCCCCCCCCCCCCCCCCull);

    CHECK(cpu.read_register(1) == 0xDDDDDDDDDDDDDDDDull);

}



void load_store_pair_sp_and_zero_register_semantics() {

    CPU cpu;

    Memory memory(0x2000);



    // SP push: STP X29, X30, [SP, #-16]! (0xA9BF7BFDu)

    memory.write_u32(0, 0xA9BF7BFDu);

    cpu.set_stack_pointer(0x1000);

    cpu.write_register(29, 0xAAAA1111BBBB2222ull);

    cpu.write_register(30, 0xCCCC3333DDDD4444ull);

    cpu.step(memory);

    CHECK(cpu.get_stack_pointer() == 0x0FF0);

    CHECK(memory.read_u64(0x0FF0) == 0xAAAA1111BBBB2222ull);

    CHECK(memory.read_u64(0x0FF8) == 0xCCCC3333DDDD4444ull);



    // SP pop: LDP X29, X30, [SP], #16 (0xA8C17BFDu)

    memory.write_u32(4, 0xA8C17BFDu);

    cpu.write_register(29, 0);

    cpu.write_register(30, 0);

    cpu.step(memory);

    CHECK(cpu.get_stack_pointer() == 0x1000);

    CHECK(cpu.read_register(29) == 0xAAAA1111BBBB2222ull);

    CHECK(cpu.read_register(30) == 0xCCCC3333DDDD4444ull);



    // Zero register stores: Rt=31 or Rt2=31

    // STP WZR, W1, [X2, #8] (0x2901045Fu) -> writes 0 at 0x108, W1 at 0x10C

    memory.write_u32(8, 0x2901045Fu);

    cpu.write_register(1, 0x12345678u);

    cpu.write_register(2, 0x100);

    memory.write_u64(0x108, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(memory.read_u32(0x108) == 0u);

    CHECK(memory.read_u32(0x10C) == 0x12345678u);



    // STP X0, XZR, [X2, #16] (0xA9017C40u) -> writes X0 at 0x110, 0 at 0x118

    memory.write_u32(12, 0xA9017C40u);

    cpu.write_register(0, 0x8877665544332211ull);

    memory.write_u64(0x118, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(memory.read_u64(0x110) == 0x8877665544332211ull);

    CHECK(memory.read_u64(0x118) == 0ull);



    // Zero register loads: Rt=31 or Rt2=31 discards the value

    // LDP WZR, W1, [X2, #8] (0x2941045Fu)

    memory.write_u32(16, 0x2941045Fu);

    memory.write_u32(0x108, 0xDEADBEEFu);

    memory.write_u32(0x10C, 0xCAFEBABEu);

    cpu.write_register(1, 0);

    cpu.step(memory);

    CHECK(cpu.read_register(1) == 0xCAFEBABEu);



    // LDP X0, XZR, [X2, #16] (0xA9417C40u)

    memory.write_u32(20, 0xA9417C40u);

    memory.write_u64(0x110, 0x0102030405060708ull);

    memory.write_u64(0x118, 0x090A0B0C0D0E0F00ull);

    cpu.write_register(0, 0);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x0102030405060708ull);

}



void load_store_pair_width_and_zero_extension() {

    CPU cpu;

    Memory memory(0x200);



    // 32-bit LDP zero-extends into 64-bit destination registers

    memory.write_u32(0, 0x29400440u); // LDP W0, W1, [X2]

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.write_register(1, 0xFFFFFFFFFFFFFFFFull);

    cpu.write_register(2, 0x100);

    memory.write_u32(0x100, 0x80000001u);

    memory.write_u32(0x104, 0xFFFFFFFFu);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x0000000080000001ull);

    CHECK(cpu.read_register(1) == 0x00000000FFFFFFFFull);



    // 32-bit STP writes only low 32 bits and leaves high byte unmodified

    memory.write_u32(4, 0x29000440u); // STP W0, W1, [X2]

    cpu.write_register(0, 0x1111222233334444ull);

    cpu.write_register(1, 0x5555666677778888ull);

    memory.write_u64(0x108, 0xDEADBEEFDEADBEEFull);

    cpu.step(memory);

    CHECK(memory.read_u32(0x100) == 0x33334444u);

    CHECK(memory.read_u32(0x104) == 0x77778888u);

    CHECK(memory.read_u64(0x108) == 0xDEADBEEFDEADBEEFull); // untouched

}



void load_store_pair_ordering_and_overlap_policy() {

    CPU cpu;

    Memory memory(0x200);



    // Overlap: Rt == Rt2 on LDP.

    // LDP X0, X0, [X2] (0xA9400040u). Rt is written first, then Rt2.

    // Destination X0 receives the value loaded from element 2.

    memory.write_u32(0, 0xA9400040u);

    cpu.write_register(2, 0x100);

    memory.write_u64(0x100, 0x1111111111111111ull);

    memory.write_u64(0x108, 0x2222222222222222ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x2222222222222222ull);



    // Overlap: Rn == Rt on pre-index writeback.

    // LDP X2, X0, [X2, #16]! (0xA9C10042u). Base write-back occurs last.

    memory.write_u32(4, 0xA9C10042u);

    cpu.write_register(2, 0x100);

    memory.write_u64(0x110, 0x3333333333333333ull);

    memory.write_u64(0x118, 0x4444444444444444ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x4444444444444444ull);

    CHECK(cpu.read_register(2) == 0x110); // writeback wins



    // Overlap: Rn == Rt2 on post-index writeback.

    // LDP X0, X2, [X2], #16 (0xA8C10840u). Base write-back occurs last.

    memory.write_u32(8, 0xA8C10840u);

    cpu.write_register(2, 0x100);

    memory.write_u64(0x100, 0x5555555555555555ull);

    memory.write_u64(0x108, 0x6666666666666666ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x5555555555555555ull);

    CHECK(cpu.read_register(2) == 0x110); // writeback wins

}



void load_store_pair_memory_boundaries_and_unaligned() {

    CPU cpu;

    Memory memory(0x100);



    // Unaligned accesses are permitted

    memory.write_u32(0, 0x29000440u); // STP W0, W1, [X2] at 0x21

    cpu.write_register(0, 0x12345678u);

    cpu.write_register(1, 0x9ABCDEF0u);

    cpu.write_register(2, 0x21);

    cpu.step(memory);

    CHECK(memory.read_u32(0x21) == 0x12345678u);

    CHECK(memory.read_u32(0x25) == 0x9ABCDEF0u);



    // Access exactly reaching the end of memory: 64-bit pair at 0xF0 (0xF0 + 16 == 0x100)

    memory.write_u32(4, 0xA9000440u); // STP X0, X1, [X2]

    cpu.write_register(0, 0x1122334455667788ull);

    cpu.write_register(1, 0x99AABBCCDDEEFF00ull);

    cpu.write_register(2, 0xF0);

    cpu.step(memory);

    CHECK(memory.read_u64(0xF0) == 0x1122334455667788ull);

    CHECK(memory.read_u64(0xF8) == 0x99AABBCCDDEEFF00ull);

}



void load_store_pair_fault_atomicity_and_address_overflow() {

    CPU cpu;

    Memory memory(0x100);



    // 1. Partial out of bounds: 64-bit STP at 0xF8 (first word fits, second overflows)

    // STP X0, X1, [X2] at 0xF8

    memory.write_u32(0, 0xA9000440u);

    cpu.write_register(0, 0x1111111111111111ull);

    cpu.write_register(1, 0x2222222222222222ull);

    cpu.write_register(2, 0xF8);

    memory.write_u64(0xF8, 0);

    CHECK_OUT_OF_RANGE(cpu.step(memory));

    // Fault atomicity: no bytes written, PC unchanged

    CHECK(memory.read_u64(0xF8) == 0ull);

    CHECK(cpu.get_program_counter() == 0);



    // 2. Pre-index fault atomicity: writeback does not occur if access faults

    memory.write_u32(0, 0xA9810440u); // STP X0, X1, [X2, #16]!

    cpu.write_register(2, 0xF0); // 0xF0 + 16 = 0x100 -> out of bounds

    CHECK_OUT_OF_RANGE(cpu.step(memory));

    CHECK(cpu.read_register(2) == 0xF0); // base register unchanged

    CHECK(cpu.get_program_counter() == 0);



    // 3. 64-bit integer address overflow

    memory.write_u32(0, 0xA9010440u); // STP X0, X1, [X2, #16]

    cpu.write_register(2, 0xFFFFFFFFFFFFFFF8ull);

    CHECK_OUT_OF_RANGE(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 0);



    // 4. 64-bit integer address underflow

    memory.write_u32(0, 0xA97F0440u); // STP X0, X1, [X2, #-16]

    cpu.write_register(2, 0x8ull);

    CHECK_OUT_OF_RANGE(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 0);

}



void conditional_compare_register_forms() {

    CPU cpu;

    Memory memory(0x100);



    // CCMP W: 0x7A410004 = CCMP W0, W1, #4, EQ

    // When EQ (Z=1), compares W0 - W1.

    // When not EQ, sets NZCV = 4.

    memory.write_u32(0, 0x7A410004u);



    // True path: W0 = 10, W1 = 10. Initial NZCV = 4 (Z=1, so EQ holds).

    // Compare 10 - 10 = 0 -> NZCV becomes 6 (Z=1, C=1).

    cpu.reset();

    cpu.write_register(0, 10);

    cpu.write_register(1, 10);

    // Set Z flag via a test step: SUBS WZR, W0, W1

    memory.write_u32(4, 0x6B01001Fu);

    cpu.set_program_counter(4);

    cpu.step(memory);

    CHECK(cpu.get_nzcv() == 0x6); // Z=1, C=1 -> EQ holds



    cpu.set_program_counter(0);

    cpu.step(memory);

    CHECK(cpu.get_nzcv() == 0x6);

    CHECK(cpu.get_program_counter() == 4);



    // False path: Set N flag via SUBS with left < right: 5 - 10

    cpu.write_register(0, 5);

    cpu.write_register(1, 10);

    cpu.set_program_counter(4);

    cpu.step(memory);

    CHECK((cpu.get_nzcv() & 0x4) == 0); // Z=0, so EQ fails



    cpu.set_program_counter(0);

    cpu.step(memory);

    CHECK(cpu.get_nzcv() == 0x4); // fallback immediate nzcv=4



    // CCMN X: 0xBA412002 = CCMN X0, X1, #2, CS (cond=2)

    // When CS (C=1), compares X0 + X1.

    // True path with signed overflow:

    memory.write_u32(8, 0xBA412002u);

    // Ensure C=1 (NZCV=2)

    cpu.write_register(0, 0x7FFFFFFFFFFFFFFFull);

    cpu.write_register(1, 1ull);

    // Set C=1 via CMP X0, #0

    memory.write_u32(12, 0xF100001Fu);

    cpu.set_program_counter(12);

    cpu.step(memory);

    CHECK((cpu.get_nzcv() & 0x2) != 0); // CS holds



    cpu.set_program_counter(8);

    cpu.step(memory);

    // 0x7FFFFFFFFFFFFFFF + 1 overflows signed 64-bit -> V=1, N=1, C=0 -> NZCV = 9

    CHECK(cpu.get_nzcv() == 0x9);



    // Register 31 as WZR / XZR

    // CCMP X0, XZR, #5, AL: 0xFA5FE005 (AL always true)

    memory.write_u32(16, 0xFA5FE005u);

    cpu.write_register(0, 0);

    cpu.set_program_counter(16);

    cpu.step(memory);

    // 0 - 0 = 0 -> Z=1, C=1 -> NZCV = 6

    CHECK(cpu.get_nzcv() == 0x6);

}



void conditional_compare_immediate_forms() {

    CPU cpu;

    Memory memory(0x100);



    // CCMP W imm: 0x7A456800 = CCMP W0, #5, #0, VS (cond=6)

    memory.write_u32(0, 0x7A456800u);

    // Set V=1 via ADDS XZR, X2, #1 with X2=0x7FFFFFFFFFFFFFFF

    memory.write_u32(4, 0xB100045Fu);

    cpu.write_register(2, 0x7FFFFFFFFFFFFFFFull);

    cpu.set_program_counter(4);

    cpu.step(memory);

    CHECK((cpu.get_nzcv() & 0x1) != 0); // VS holds



    // True path: W0 = 5. 5 - 5 = 0 -> NZCV = 6

    cpu.write_register(0, 5);

    cpu.set_program_counter(0);

    cpu.step(memory);

    CHECK(cpu.get_nzcv() == 0x6);



    // False path: VC (cond=7): 0x7A45780A = CCMP W0, #5, #0xA, VC

    memory.write_u32(8, 0x7A45780Au);

    // Set V=1 again so VC fails

    cpu.set_program_counter(4);

    cpu.step(memory);

    cpu.set_program_counter(8);

    cpu.step(memory);

    CHECK(cpu.get_nzcv() == 0xA); // fallback immediate nzcv = 0xA



    // Immediate boundary imm5 = 31: 0x7A5F8801 = CCMP W0, #31, #1, HI (cond=8)

    memory.write_u32(12, 0x7A5F8801u);

    // Set C=1, Z=0 (HI holds) via CMP X0, #0 with X0=1

    cpu.write_register(0, 40);

    memory.write_u32(16, 0xF100001Fu);

    cpu.set_program_counter(16);

    cpu.step(memory);

    CHECK(cpu.get_nzcv() == 0x2); // C=1, Z=0 -> HI holds



    cpu.set_program_counter(12);

    cpu.step(memory);

    // 40 - 31 = 9 -> C=1, Z=0, N=0, V=0 -> NZCV = 2

    CHECK(cpu.get_nzcv() == 0x2);

}



void conditional_select_instructions_and_width() {

    CPU cpu;

    Memory memory(0x100);



    // 1. CSEL W: 0x1A820020 = CSEL W0, W1, W2, EQ

    memory.write_u32(0, 0x1A820020u);

    // Set EQ (Z=1)

    memory.write_u32(4, 0xF100001Fu); // CMP X0, #0

    cpu.write_register(0, 0);

    cpu.write_register(1, 0xDEADBEEF11223344ull);

    cpu.write_register(2, 0xCAFEF00D55667788ull);

    cpu.set_program_counter(4);

    cpu.step(memory);

    CHECK(cpu.get_nzcv() == 0x6); // EQ holds



    cpu.set_program_counter(0);

    cpu.step(memory);

    // W write zero-extends into X0

    CHECK(cpu.read_register(0) == 0x0000000011223344ull);

    CHECK(cpu.get_nzcv() == 0x6); // NZCV unchanged



    // 2. CSINC W: 0x1A821420 = CSINC W0, W1, W2, NE (false path increments W2)

    memory.write_u32(8, 0x1A821420u);

    cpu.write_register(2, 0xDEADBEEFFFFFFFFFull); // tests 32-bit wrap

    cpu.set_program_counter(8);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x0000000000000000ull); // wrapped & zero-extended



    // 3. CSINV X: 0xDA827020 = CSINV X0, X1, X2, VC (false path inverts X2)

    memory.write_u32(12, 0xDA827020u);

    // Set V=1 so VC fails

    memory.write_u32(16, 0xB100045Fu);

    cpu.write_register(2, 0x7FFFFFFFFFFFFFFFull);

    cpu.set_program_counter(16);

    cpu.step(memory);

    CHECK((cpu.get_nzcv() & 0x1) != 0);



    cpu.write_register(1, 0x1111111111111111ull);

    cpu.write_register(2, 0x5555555555555555ull);

    cpu.set_program_counter(12);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xAAAAAAAAAAAAAAAAull);



    // 4. CSNEG X: 0xDA829420 = CSNEG X0, X1, X2, LS (false path negates X2)

    memory.write_u32(20, 0xDA829420u);

    // Set C=1, Z=0 (HI holds, LS fails)

    memory.write_u32(24, 0xF100001Fu);

    cpu.write_register(0, 1);

    cpu.set_program_counter(24);

    cpu.step(memory);

    CHECK(cpu.get_nzcv() == 0x2);



    cpu.write_register(1, 0x42);

    cpu.write_register(2, 1);

    cpu.set_program_counter(20);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFFFFFFull);



    // 5. Register 31 as WZR / XZR

    // CSEL XZR, X1, X2, EQ (0x9A82003F)

    memory.write_u32(28, 0x9A82003Fu);

    cpu.set_stack_pointer(0x12345678ull);

    cpu.set_program_counter(28);

    cpu.step(memory);

    CHECK(cpu.get_stack_pointer() == 0x12345678ull); // SP untouched

}



void conditional_aliases_equivalence() {

    CPU cpu;

    Memory memory(0x100);



    // CSET W0, EQ: 0x1A9F17E0 (CSINC W0, WZR, WZR, NE)

    memory.write_u32(0, 0x1A9F17E0u);

    // EQ holds (Z=1) -> NE is false -> Rd = WZR + 1 = 1

    memory.write_u32(4, 0xF100001Fu);

    cpu.write_register(0, 0);

    cpu.set_program_counter(4);

    cpu.step(memory); // Z=1

    cpu.set_program_counter(0);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 1);



    // CSETM X0, MI: 0xDA9F53E0 (CSINV X0, XZR, XZR, PL)

    memory.write_u32(8, 0xDA9F53E0u);

    // MI holds (N=1) -> PL is false -> Rd = ~XZR = ~0 = all ones

    memory.write_u32(12, 0xF100041Fu); // CMP X0, #1 with X0=0 -> N=1

    cpu.write_register(0, 0);

    cpu.set_program_counter(12);

    cpu.step(memory);

    CHECK((cpu.get_nzcv() & 0x8) != 0);



    cpu.set_program_counter(8);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFFFFFFull);



    // CINC W0, W1, GT: 0x1A81D420 (CSINC W0, W1, W1, LE)

    memory.write_u32(16, 0x1A81D420u);

    // GT holds -> LE false -> Rd = W1 + 1

    cpu.write_register(0, 5); // CMP X0, #0 -> C=1, Z=0 -> GT holds

    cpu.set_program_counter(4);

    cpu.step(memory);

    cpu.write_register(1, 7);

    cpu.set_program_counter(16);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 8);



    // CINV X0, X1, CS: 0xDA813020 (CSINV X0, X1, X1, CC)

    memory.write_u32(20, 0xDA813020u);

    // CS holds -> CC false -> Rd = ~X1

    cpu.write_register(1, 0x0Full);

    cpu.set_program_counter(20);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFFFFF0ull);



    // CNEG W0, W1, LT: 0x5A81A420 (CSNEG W0, W1, W1, GE)

    memory.write_u32(24, 0x5A81A420u);

    // LT holds -> GE false -> Rd = -W1

    // Set N=1, V=0 -> LT holds

    cpu.set_program_counter(12);

    cpu.step(memory);

    cpu.write_register(1, 5);

    cpu.set_program_counter(24);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x00000000FFFFFFFBull);

}



void condition_codes_evaluation_and_nv_semantics() {

    CPU cpu;

    Memory memory(0x100);



    // CSEL W0, W1, W2, NV: 0x1A82F020

    // NV (1111) is defined as always true in data processing (like AL)

    memory.write_u32(0, 0x1A82F020u);

    cpu.write_register(1, 0x12345678);

    cpu.write_register(2, 0x87654321);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x12345678);



    // CCMP with NV: 0xFA40F3E3 = CCMP XZR, X0, #3, NV

    // Evaluates as AL (always true)

    memory.write_u32(4, 0xFA40F3E3u);

    cpu.write_register(0, 1);

    cpu.set_program_counter(4);

    cpu.step(memory);

    // 0 - 1 = -1 -> N=1, C=0 -> NZCV = 8

    CHECK(cpu.get_nzcv() == 0x8);

}



void unsupported_conditional_encodings_are_rejected() {

    CPU cpu;

    Memory memory(0x100);



    // CCMP bit 10 set (unallocated)

    memory.write_u32(0, 0x7A400400u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 0);



    // CCMP bit 4 set (unallocated)

    memory.write_u32(0, 0x7A400010u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 0);



    // CSEL op2 = 10 (unallocated)

    memory.write_u32(0, 0x1A800800u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 0);



    // CSEL op2 = 11 (unallocated)

    memory.write_u32(0, 0x1A800C00u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 0);



    // CSEL S=1 (bit 29 set, unallocated)

    memory.write_u32(0, 0x3A800000u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 0);

}



void bitfield_ubfm_extract_and_insert_forms() {

    CPU cpu;

    Memory memory(0x100);



    // 1. UBFX W0, W1, #4, #8 (0x53042C20u): imms=11, immr=4

    memory.write_u32(0, 0x53042C20u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.write_register(1, 0x0000000000001234ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x23ull); // Zero-extended into X0



    // 2. LSR W0, W1, #4 (0x53047C20u): imms=31, immr=4

    memory.write_u32(4, 0x53047C20u);

    cpu.write_register(1, 0xA5A5A5A5ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x0A5A5A5Aull);



    // 3. UXTB W0, W1 (0x53001C20u): imms=7, immr=0

    memory.write_u32(8, 0x53001C20u);

    cpu.write_register(1, 0x123456F8ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xF8ull);



    // 4. UXTH W0, W1 (0x53003C20u): imms=15, immr=0

    memory.write_u32(12, 0x53003C20u);

    cpu.write_register(1, 0x1234F6A5ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xF6A5ull);



    // 5. LSL W0, W1, #4 (0x531C6C20u): immr=28, imms=27

    memory.write_u32(16, 0x531C6C20u);

    cpu.write_register(1, 0x12345678ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x23456780ull);



    // 6. UBFIZ W0, W1, #8, #12 (0x53182C20u): immr=24, imms=11

    memory.write_u32(20, 0x53182C20u);

    cpu.write_register(1, 0x0FFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x000FFF00ull);



    // 7. UBFX X0, X1, #16, #24 (0xD3509C20u): immr=16, imms=39

    memory.write_u32(24, 0xD3509C20u);

    cpu.write_register(1, 0x123456789ABCDEF0ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x0000000000789ABCull);



    // 8. LSR X0, X1, #8 (0xD348FC20u): immr=8, imms=63

    memory.write_u32(28, 0xD348FC20u);

    cpu.write_register(1, 0x0123456789ABCDEFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x000123456789ABCDull);



    // 9. LSL X0, X1, #8 (0xD378DC20u): immr=56, imms=55

    memory.write_u32(32, 0xD378DC20u);

    cpu.write_register(1, 0x0123456789ABCDEFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x23456789ABCDEF00ull);



    // 10. UBFIZ X0, X1, #16, #16 (0xD3703C20u): immr=48, imms=15

    memory.write_u32(36, 0xD3703C20u);

    cpu.write_register(1, 0xFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFF0000ull);

}



void bitfield_sbfm_extract_and_sign_extension() {

    CPU cpu;

    Memory memory(0x100);



    // 1. SXTB W0, W1 (0x13001C20u): positive byte (0x5A -> 0x5A)

    memory.write_u32(0, 0x13001C20u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.write_register(1, 0x5Aull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x5Aull);



    // 2. SXTB W0, W1: negative byte (0xC3 -> 0xFFFFFFC3, zero-extended into X0)

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.write_register(1, 0xC3ull);

    cpu.set_program_counter(0);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x00000000FFFFFFC3ull);



    // 3. SXTH W0, W1 (0x13003C20u): negative halfword (0xC000 -> 0xFFFFC000)

    memory.write_u32(4, 0x13003C20u);

    cpu.write_register(1, 0xC000ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x00000000FFFFC000ull);



    // 4. ASR W0, W1, #4 (0x13047C20u): negative word (0x80000000 -> 0xF8000000)

    memory.write_u32(8, 0x13047C20u);

    cpu.write_register(1, 0x80000000ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x00000000F8000000ull);



    // 5. SXTW X0, X1 (0x93407C20u): positive word (0x7FFFFFFF -> 0x000000007FFFFFFF)

    memory.write_u32(12, 0x93407C20u);

    cpu.write_register(1, 0x7FFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x000000007FFFFFFFull);



    // 6. SXTW X0, X1: negative word (0x80000000 -> 0xFFFFFFFF80000000)

    cpu.write_register(1, 0x80000000ull);

    cpu.set_program_counter(12);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFF80000000ull);



    // 7. ASR X0, X1, #8 (0x9348FC20u): negative 64-bit (0x8000000000000000 -> 0xFF80000000000000)

    memory.write_u32(16, 0x9348FC20u);

    cpu.write_register(1, 0x8000000000000000ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFF80000000000000ull);



    // 8. SBFX X0, X1, #16, #16 (0x93507C20u): negative halfword extracted

    memory.write_u32(20, 0x93507C20u);

    cpu.write_register(1, 0x0000000080000000ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFF8000ull);

}



void bitfield_sbfm_insert_and_sign_extension() {

    CPU cpu;

    Memory memory(0x100);



    // 1. SBFIZ W0, W1, #8, #12 (0x13182C20u): positive 12-bit field (0x7FF) -> 0x0007FF00

    memory.write_u32(0, 0x13182C20u);

    cpu.write_register(1, 0x07FFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x0007FF00ull);



    // 2. SBFIZ W0, W1, #8, #12: negative 12-bit field (0x800) -> 0x00000000FFF80000

    cpu.write_register(1, 0x0800ull);

    cpu.set_program_counter(0);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x00000000FFF80000ull);



    // 3. SBFIZ X0, X1, #16, #8 (0x93701C20u): positive byte (0x7F) -> 0x00000000007F0000

    memory.write_u32(4, 0x93701C20u);

    cpu.write_register(1, 0x7Full);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x007F0000ull);



    // 4. SBFIZ X0, X1, #16, #8: negative byte (0x80) -> 0xFFFFFFFFFF800000

    cpu.write_register(1, 0x80ull);

    cpu.set_program_counter(4);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFFFF800000ull);



    // 5. SBFIZ X0, X1, #32, #16 (0x93603C20u): negative halfword (0xF000) -> 0xFFFFF00000000000

    memory.write_u32(8, 0x93603C20u);

    cpu.write_register(1, 0xF000ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFF00000000000ull);

}



void bitfield_bfm_destination_preservation() {

    CPU cpu;

    Memory memory(0x100);



    // 1. BFXIL W0, W1, #8, #8 (0x33083C20u): inserts W1<15:8> at W0<7:0>, preserves W0<31:8>, zeroes X0<63:32>

    memory.write_u32(0, 0x33083C20u);

    cpu.write_register(0, 0xFFFFFFFF12345678ull);

    cpu.write_register(1, 0xAB00ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x00000000123456ABull);



    // 2. BFI W0, W1, #8, #8 (0x33181C20u): inserts W1<7:0> at W0<15:8>, preserves other bits, zeroes X0<63:32>

    memory.write_u32(4, 0x33181C20u);

    cpu.write_register(0, 0xFFFFFFFF12345678ull);

    cpu.write_register(1, 0xABull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x000000001234AB78ull);



    // 3. BFXIL X0, X1, #16, #16 (0xB3507C20u): inserts X1<31:16> at X0<15:0>, preserves X0<63:16>

    memory.write_u32(8, 0xB3507C20u);

    cpu.write_register(0, 0x123456789ABCDEF0ull);

    cpu.write_register(1, 0xCAFE0000ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x123456789ABCCAFEull);



    // 4. BFI X0, X1, #16, #16 (0xB3703C20u): inserts X1<15:0> at X0<31:16>, preserves other bits

    memory.write_u32(12, 0xB3703C20u);

    cpu.write_register(0, 0x0123456789ABCDEFull);

    cpu.write_register(1, 0xAAAAull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x01234567AAAACDEFull);



    // 5. BFI X0, X1, #48, #16 (0xB3503C20u): inserts at top 16 bits

    memory.write_u32(16, 0xB3503C20u);

    cpu.write_register(0, 0x0123456789ABCDEFull);

    cpu.write_register(1, 0xCCCCull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xCCCC456789ABCDEFull);

}



void bitfield_register_31_and_boundaries() {

    CPU cpu;

    Memory memory(0x100);



    // 1. Rn = 31 (WZR): reads 0 -> UBFM W0, WZR, #4, #8 (0x53042FE0u)

    memory.write_u32(0, 0x53042FE0u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0ull);



    // 2. Rn = 31 (XZR): reads 0 -> SBFM X0, XZR, #0, #31 (0x93407FE0u)

    memory.write_u32(4, 0x93407FE0u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0ull);



    // 3. Rd = 31 (WZR): write discarded, SP untouched -> UBFM WZR, W1, #4, #8 (0x53042C3Fu)

    memory.write_u32(8, 0x53042C3Fu);

    cpu.set_stack_pointer(0x1000ull);

    cpu.write_register(1, 0x12345678ull);

    cpu.step(memory);

    CHECK(cpu.get_stack_pointer() == 0x1000ull);



    // 4. Rd = 31 (XZR): write discarded, SP untouched -> SBFM XZR, X1, #0, #31 (0x93407C3Fu)

    memory.write_u32(12, 0x93407C3Fu);

    cpu.step(memory);

    CHECK(cpu.get_stack_pointer() == 0x1000ull);



    // 5. BFI X0, XZR, #16, #16 (0xB3703FE0u): clears bits 31:16 of X0

    memory.write_u32(16, 0xB3703FE0u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFF0000FFFFull);



    // 6. Single bit boundary: UBFM X0, X1, #63, #0 (0xD37F0020u) -> shifts bit 0 to bit 1

    memory.write_u32(20, 0xD37F0020u);

    cpu.write_register(1, 1ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 2ull);

}



void unsupported_bitfield_encodings_are_rejected() {

    CPU cpu;

    Memory memory(0x100);



    // 1. opc == 11 is unallocated (0x73000000u)

    memory.write_u32(0, 0x73000000u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 0);



    // 2. sf=0, N=1 is unallocated (0x13400000u)

    memory.write_u32(0, 0x13400000u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 0);



    // 3. sf=1, N=0 is unallocated (0x93000000u)

    memory.write_u32(0, 0x93000000u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 0);



    // 4. 32-bit with immr<5> set is unallocated (0x53200000u)

    memory.write_u32(0, 0x53200000u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 0);



    // 5. 32-bit with imms<5> set is unallocated (0x53008000u)

    memory.write_u32(0, 0x53008000u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 0);

}



void logical_shifted_register_basic_and_widths() {

    CPU cpu;

    Memory memory(0x100);



    // 1. AND X0, X1, X2 (0x8A020020u)

    memory.write_u32(0, 0x8A020020u);

    cpu.write_register(1, 0x0F0F0F0F0F0F0F0Full);

    cpu.write_register(2, 0x00FF00FF00FF00FFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x000F000F000F000Full);



    // 2. BIC X0, X1, X2 (0x8A220020u): X1 & ~X2

    memory.write_u32(4, 0x8A220020u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x0F000F000F000F00ull);



    // 3. ORR X0, X1, X2 (0xAA020020u)

    memory.write_u32(8, 0xAA020020u);

    cpu.write_register(1, 0x0F000F000F000F00ull);

    cpu.write_register(2, 0x00F000F000F000F0ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x0FF00FF00FF00FF0ull);



    // 4. ORN X0, X1, X2 (0xAA220020u): X1 | ~X2

    memory.write_u32(12, 0xAA220020u);

    cpu.write_register(1, 0x000000000000000Full);

    cpu.write_register(2, 0x000000000000000Full);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFFFFFFull);



    // 5. EOR X0, X1, X2 (0xCA020020u)

    memory.write_u32(16, 0xCA020020u);

    cpu.write_register(1, 0xAAAAAAAAAAAAAAAAull);

    cpu.write_register(2, 0x5555555555555555ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFFFFFFull);



    // 6. EON X0, X1, X2 (0xCA220020u): X1 ^ ~X2

    memory.write_u32(20, 0xCA220020u);

    cpu.write_register(1, 0xAAAAAAAAAAAAAAAAull);

    cpu.write_register(2, 0x5555555555555555ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x0000000000000000ull);



    // 7. 32-bit AND W0, W1, W2 (0x0A020020u): zero-extends into X0

    memory.write_u32(24, 0x0A020020u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.write_register(1, 0x111111110F0F0F0Full);

    cpu.write_register(2, 0x2222222200FF00FFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x00000000000F000Full);



    // 8. 32-bit BIC W0, W1, W2 (0x0A220020u): zero-extends

    memory.write_u32(28, 0x0A220020u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x000000000F000F00ull);



    // 9. 32-bit ORN W0, W1, W2 (0x2A220020u): zero-extends

    memory.write_u32(32, 0x2A220020u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.write_register(1, 0x000000000000000Full);

    cpu.write_register(2, 0x000000000000000Full);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x00000000FFFFFFFFull);



    // 10. 32-bit EON W0, W1, W2 (0x4A220020u): zero-extends

    memory.write_u32(36, 0x4A220020u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.write_register(1, 0x000000000000AAAAull);

    cpu.write_register(2, 0x00000000FFFF5555ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x0000000000000000ull);

}



void logical_shifted_register_shift_types() {

    CPU cpu;

    Memory memory(0x100);



    // 1. LSL: AND X0, X1, X2, LSL #4 (0x8A021020u)

    memory.write_u32(0, 0x8A021020u);

    cpu.write_register(1, 0x00000000000000FFull);

    cpu.write_register(2, 0x000000000000000Full);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x00000000000000F0ull);



    // 2. LSL Max: ORR X0, X1, X2, LSL #63 (0xAA02FC20u)

    memory.write_u32(4, 0xAA02FC20u);

    cpu.write_register(1, 0x0000000000000001ull);

    cpu.write_register(2, 0x0000000000000001ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x8000000000000001ull);



    // 3. LSR: AND X0, X1, X2, LSR #4 (0x8A421020u)

    memory.write_u32(8, 0x8A421020u);

    cpu.write_register(1, 0x00000000000000FFull);

    cpu.write_register(2, 0x00000000000000F0ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x000000000000000Full);



    // 4. ASR negative 64-bit: ORR X0, X1, X2, ASR #4 (0xAA821020u)

    memory.write_u32(12, 0xAA821020u);

    cpu.write_register(1, 0x0000000000000000ull);

    cpu.write_register(2, 0x8000000000000000ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xF800000000000000ull);



    // 5. ASR negative 32-bit: ORR W0, W1, W2, ASR #4 (0x2A821020u)

    memory.write_u32(16, 0x2A821020u);

    cpu.write_register(1, 0x0000000000000000ull);

    cpu.write_register(2, 0x0000000080000000ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x00000000F8000000ull);



    // 6. ROR 64-bit: ORR X0, X1, X2, ROR #4 (0xAAC21020u)

    memory.write_u32(20, 0xAAC21020u);

    cpu.write_register(1, 0x0000000000000000ull);

    cpu.write_register(2, 0x0000000000000001ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x1000000000000000ull);



    // 7. ROR 32-bit: ORR W0, W1, W2, ROR #4 (0x2AC21020u)

    memory.write_u32(24, 0x2AC21020u);

    cpu.write_register(1, 0x0000000000000000ull);

    cpu.write_register(2, 0x0000000000000001ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x0000000010000000ull);



    // 8. ROR amount 0: identity

    memory.write_u32(28, 0xAAC20020u);

    cpu.write_register(1, 0x0000000000000000ull);

    cpu.write_register(2, 0x123456789ABCDEF0ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x123456789ABCDEF0ull);

}



void logical_shifted_register_aliases_and_zr() {

    CPU cpu;

    Memory memory(0x100);

    cpu.set_stack_pointer(0x40008000ull);



    // 1. MOV X0, X1 alias (0xAA0103E0u)

    memory.write_u32(0, 0xAA0103E0u);

    cpu.write_register(1, 0xFEDCBA9876543210ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFEDCBA9876543210ull);

    CHECK(cpu.get_stack_pointer() == 0x40008000ull);



    // 2. MOV W0, W1 alias (0x2A0103E0u): zero-extends

    memory.write_u32(4, 0x2A0103E0u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.write_register(1, 0x1234567887654321ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x0000000087654321ull);



    // 3. MVN X0, X1 alias (0xAA2103E0u)

    memory.write_u32(8, 0xAA2103E0u);

    cpu.write_register(1, 0x000000000000000Full);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFFFFF0ull);



    // 4. MVN W0, W1 alias (0x2A2103E0u): zero-extends

    memory.write_u32(12, 0x2A2103E0u);

    cpu.write_register(1, 0x000000000000000Full);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x00000000FFFFFFF0ull);



    // 5. TST X1, X2 (0xEA02003Fu): Rd=31 is ZR, SP untouched

    memory.write_u32(16, 0xEA02003Fu);

    cpu.write_register(1, 0x000000000000000Full);

    cpu.write_register(2, 0x000000000000000Full);

    cpu.step(memory);

    CHECK(cpu.get_stack_pointer() == 0x40008000ull);



    // 6. AND XZR, XZR, XZR (0x8A1F03FFu): SP completely untouched

    memory.write_u32(20, 0x8A1F03FFu);

    cpu.step(memory);

    CHECK(cpu.get_stack_pointer() == 0x40008000ull);

    CHECK(cpu.get_program_counter() == 24);

}



void logical_immediate_decoding_and_element_sizes() {

    CPU cpu;

    Memory memory(0x100);



    // 1. e=2: AND X0, X1, #0x5555555555555555 (0x9200F020u)

    memory.write_u32(0, 0x9200F020u);

    cpu.write_register(1, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x5555555555555555ull);



    // 2. e=2 rotated: AND X0, X1, #0xAAAAAAAAAAAAAAAA (0x9201F020u)

    memory.write_u32(4, 0x9201F020u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xAAAAAAAAAAAAAAAAull);



    // 3. e=4: AND X0, X1, #0x3333333333333333 (0x9200E420u)

    memory.write_u32(8, 0x9200E420u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x3333333333333333ull);



    // 4. e=8: ORR X0, X1, #0x0F0F0F0F0F0F0F0F (0xB200CC20u)

    memory.write_u32(12, 0xB200CC20u);

    cpu.write_register(1, 0xF0F0F0F0F0F0F0F0ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFFFFFFull);



    // 5. e=16: EOR X0, X1, #0x00FF00FF00FF00FF (0xD2009C20u)

    memory.write_u32(16, 0xD2009C20u);

    cpu.write_register(1, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFF00FF00FF00FF00ull);



    // 6. e=32: AND X0, X1, #0x0000FFFF0000FFFF (0x92003C20u)

    memory.write_u32(20, 0x92003C20u);

    cpu.write_register(1, 0x123456789ABCDEF0ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x000056780000DEF0ull);



    // 7. e=64: AND X0, X1, #0x00000000FFFFFFFF (0x92407C20u)

    memory.write_u32(24, 0x92407C20u);

    cpu.write_register(1, 0x123456789ABCDEF0ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x000000009ABCDEF0ull);



    // 8. 32-bit immediate zero-extension: AND W0, W1, #0x55555555 (0x1200F020u)

    memory.write_u32(28, 0x1200F020u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.write_register(1, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x0000000055555555ull);

}



void logical_immediate_aliases_and_flags() {

    CPU cpu;

    Memory memory(0x100);



    // 1. MOV X0, #0x5555555555555555 alias (ORR X0, XZR, #imm) (0xB200F3E0u)

    memory.write_u32(0, 0xB200F3E0u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x5555555555555555ull);



    // 2. MOV W0, #0x55555555 alias (0x3200F3E0u): zero-extends

    memory.write_u32(4, 0x3200F3E0u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x0000000055555555ull);



    // 3. TST X1, #imm (0x92407C3Fu)

    memory.write_u32(8, 0x92407C3Fu);

    cpu.write_register(1, 0x123456789ABCDEF0ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x0000000055555555ull);



    // 4. BICS X0, X1, X2 with X1=X2 sets Z=1, clears C and V

    memory.write_u32(12, 0xEB00001Fu); // SUBS XZR, X0, X0

    memory.write_u32(16, 0xEA220020u); // BICS X0, X1, X2

    cpu.write_register(0, 1ull);

    cpu.write_register(1, 0x1234ull);

    cpu.write_register(2, 0x1234ull);

    cpu.step(memory);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0ull);



    // 5. Flag invariance: AND X does not modify NZCV

    memory.write_u32(20, 0x8A020020u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x1234ull);



    // 6. Flag invariance: ORR X does not modify NZCV

    memory.write_u32(24, 0xAA020020u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x1234ull);

}



void unsupported_logical_encodings_are_rejected() {

    CPU cpu;

    Memory memory(0x100);



    // 1. 32-bit shifted register with bit 15 set (amount >= 32): 0x0A028020u

    memory.write_u32(0, 0x0A028020u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 0);



    // 2. 32-bit logical immediate with N=1: 0x12400020u

    memory.write_u32(0, 0x12400020u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 0);



    // 3. Reserved DecodeBitMasks: immn=0, imms=0x3E (111110): 0x9200F820u

    memory.write_u32(0, 0x9200F820u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 0);



    // 4. Reserved DecodeBitMasks: s == levels (all ones run): 0x9240FC20u

    memory.write_u32(0, 0x9240FC20u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 0);

}



void multiply_32_bit_madd_msub_mul_mneg() {

    CPU cpu;

    Memory memory(0x100);



    // 1. MADD W0, W1, W2, W3: 10 + (7 * 6) = 52 (0x34)

    // Word: 0x1B020C20u

    memory.write_u32(0, 0x1B020C20u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull); // verify zero-extension

    cpu.write_register(1, 7);

    cpu.write_register(2, 6);

    cpu.write_register(3, 10);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x34ull);

    CHECK(cpu.get_program_counter() == 4);



    // 2. MUL W0, W1, W2 (alias MADD with Ra=WZR): 12 * 5 = 60 (0x3C)

    // Word: 0x1B027C20u

    memory.write_u32(4, 0x1B027C20u);

    cpu.write_register(0, 0);

    cpu.write_register(1, 12);

    cpu.write_register(2, 5);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x3Cull);

    CHECK(cpu.get_program_counter() == 8);



    // 3. MSUB W0, W1, W2, W3: 50 - (6 * 7) = 8

    // Word: 0x1B028C20u

    memory.write_u32(8, 0x1B028C20u);

    cpu.write_register(1, 6);

    cpu.write_register(2, 7);

    cpu.write_register(3, 50);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 8ull);



    // 4. MNEG W0, W1, W2 (alias MSUB with Ra=WZR): 0 - (5 * 6) = -30 = 0xFFFFFFE2

    // Word: 0x1B02FC20u

    memory.write_u32(12, 0x1B02FC20u);

    cpu.write_register(1, 5);

    cpu.write_register(2, 6);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFE2ull);



    // 5. Overflow modulo 2^32: (0x10000 * 0x20000) = 0x200000000 -> 0 mod 2^32. + 1 = 1

    memory.write_u32(16, 0x1B020C20u);

    cpu.write_register(1, 0x10000);

    cpu.write_register(2, 0x20000);

    cpu.write_register(3, 1);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 1ull);



    // 6. Underflow modulo 2^32: 50 - 100 = -50 = 0xFFFFFFCE

    memory.write_u32(20, 0x1B028C20u);

    cpu.write_register(1, 10);

    cpu.write_register(2, 10);

    cpu.write_register(3, 50);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFCEull);



    // 7. Negative operands: 20 + (-3 * 4) = 8

    memory.write_u32(24, 0x1B020C20u);

    cpu.write_register(1, 0xFFFFFFFDull); // -3

    cpu.write_register(2, 4);

    cpu.write_register(3, 20);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 8ull);

}



void multiply_64_bit_madd_msub_mul_mneg() {

    CPU cpu;

    Memory memory(0x100);



    // 1. MADD X0, X1, X2, X3: 300 + (100 * 200) = 20300 (0x4F4C)

    // Word: 0x9B020C20u

    memory.write_u32(0, 0x9B020C20u);

    cpu.write_register(1, 100);

    cpu.write_register(2, 200);

    cpu.write_register(3, 300);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 20300ull);

    CHECK(cpu.get_program_counter() == 4);



    // 2. MUL X0, X1, X2 (alias MADD with Ra=XZR): 1234567 * 8910 = 10999991970

    // Word: 0x9B027C20u

    memory.write_u32(4, 0x9B027C20u);

    cpu.write_register(1, 1234567);

    cpu.write_register(2, 8910);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 10999991970ull);



    // 3. MSUB X0, X1, X2, X3: 25000 - (100 * 200) = 5000 (0x1388)

    // Word: 0x9B028C20u

    memory.write_u32(8, 0x9B028C20u);

    cpu.write_register(1, 100);

    cpu.write_register(2, 200);

    cpu.write_register(3, 25000);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 5000ull);



    // 4. MNEG X0, X1, X2 (alias MSUB with Ra=XZR): 0 - (10 * 20) = -200 = 0xFFFFFFFFFFFFFF38

    // Word: 0x9B02FC20u

    memory.write_u32(12, 0x9B02FC20u);

    cpu.write_register(1, 10);

    cpu.write_register(2, 20);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFFFF38ull);



    // 5. 64-bit modulo wrap: (0x8000000000000001 * 3) + 5 = 0x8000000000000008

    memory.write_u32(16, 0x9B020C20u);

    cpu.write_register(1, 0x8000000000000001ull);

    cpu.write_register(2, 3);

    cpu.write_register(3, 5);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x8000000000000008ull);



    // 6. MSUB negative operand: 10 - (-1 * 5) = 15

    memory.write_u32(20, 0x9B028C20u);

    cpu.write_register(1, 0xFFFFFFFFFFFFFFFFull);

    cpu.write_register(2, 5);

    cpu.write_register(3, 10);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 15ull);



    // 7. Max unsigned: (-1 * -1) = 1

    memory.write_u32(24, 0x9B027C20u);

    cpu.write_register(1, 0xFFFFFFFFFFFFFFFFull);

    cpu.write_register(2, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 1ull);

}



void multiply_widening_signed_smaddl_smsubl_smull_smnegl() {

    CPU cpu;

    Memory memory(0x100);



    // 1. SMADDL X0, W1, W2, X3: 500 + (1000 * 2000) = 2000500 (0x1E86F4)

    // Word: 0x9B220C20u

    memory.write_u32(0, 0x9B220C20u);

    cpu.write_register(1, 1000);

    cpu.write_register(2, 2000);

    cpu.write_register(3, 500);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 2000500ull);

    CHECK(cpu.get_program_counter() == 4);



    // 2. SMULL X0, W1, W2 (alias SMADDL with Ra=XZR): 3000 * 4000 = 12000000

    // Word: 0x9B227C20u

    memory.write_u32(4, 0x9B227C20u);

    cpu.write_register(1, 3000);

    cpu.write_register(2, 4000);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 12000000ull);



    // 3. SMSUBL X0, W1, W2, X3: 3000000 - (1000 * 2000) = 1000000 (0xF4240)

    // Word: 0x9B228C20u

    memory.write_u32(8, 0x9B228C20u);

    cpu.write_register(1, 1000);

    cpu.write_register(2, 2000);

    cpu.write_register(3, 3000000);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 1000000ull);



    // 4. SMNEGL X0, W1, W2 (alias SMSUBL with Ra=XZR): 0 - (1000 * 2000) = -2000000

    // Word: 0x9B22FC20u

    memory.write_u32(12, 0x9B22FC20u);

    cpu.write_register(1, 1000);

    cpu.write_register(2, 2000);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFFFFE17B80ull);



    // 5. Negative operand: W1=-10, W2=20, X3=500 -> 500 + (-200) = 300

    memory.write_u32(16, 0x9B220C20u);

    cpu.write_register(1, 0xFFFFFFF6ull);

    cpu.write_register(2, 20);

    cpu.write_register(3, 500);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 300ull);



    // 6. Two negatives: W1=-10, W2=-20, X3=100 -> 100 + 200 = 300

    memory.write_u32(20, 0x9B220C20u);

    cpu.write_register(1, 0xFFFFFFF6ull);

    cpu.write_register(2, 0xFFFFFFECull);

    cpu.write_register(3, 100);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 300ull);



    // 7. Sign boundary: -2^31 * 2 = -2^32 = 0xFFFFFFFF00000000

    memory.write_u32(24, 0x9B227C20u);

    cpu.write_register(1, 0x80000000ull);

    cpu.write_register(2, 2);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFF00000000ull);



    // 8. Sign boundary square: -2^31 * -1 = +2^31 = 0x0000000080000000

    memory.write_u32(28, 0x9B227C20u);

    cpu.write_register(1, 0x80000000ull);

    cpu.write_register(2, 0xFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x0000000080000000ull);

}



void multiply_widening_unsigned_umaddl_umsubl_umull_umnegl() {

    CPU cpu;

    Memory memory(0x100);



    // 1. UMADDL X0, W1, W2, X3: 500 + (1000 * 2000) = 2000500

    // Word: 0x9BA20C20u

    memory.write_u32(0, 0x9BA20C20u);

    cpu.write_register(1, 1000);

    cpu.write_register(2, 2000);

    cpu.write_register(3, 500);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 2000500ull);

    CHECK(cpu.get_program_counter() == 4);



    // 2. UMULL X0, W1, W2 (alias UMADDL with Ra=XZR): 0x10000 * 0x20000 = 0x200000000

    // Word: 0x9BA27C20u

    memory.write_u32(4, 0x9BA27C20u);

    cpu.write_register(1, 0x10000);

    cpu.write_register(2, 0x20000);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x200000000ull);



    // 3. UMSUBL X0, W1, W2, X3: 0x300000000 - 0x200000000 = 0x100000000

    // Word: 0x9BA28C20u

    memory.write_u32(8, 0x9BA28C20u);

    cpu.write_register(1, 0x10000);

    cpu.write_register(2, 0x20000);

    cpu.write_register(3, 0x300000000ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x100000000ull);



    // 4. UMNEGL X0, W1, W2 (alias UMSUBL with Ra=XZR): 0 - (1 * 2) = -2 = 0xFFFFFFFFFFFFFFFE

    // Word: 0x9BA2FC20u

    memory.write_u32(12, 0x9BA2FC20u);

    cpu.write_register(1, 1);

    cpu.write_register(2, 2);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFFFFFEull);



    // 5. Unsigned contrast: 0x80000000 (2^31) * 2 = 2^32 = 0x100000000

    memory.write_u32(16, 0x9BA20C20u);

    cpu.write_register(1, 0x80000000ull);

    cpu.write_register(2, 2);

    cpu.write_register(3, 0);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x100000000ull);



    // 6. Max 32-bit: (2^32 - 1)^2 = 0xFFFFFFFE00000001

    memory.write_u32(20, 0x9BA27C20u);

    cpu.write_register(1, 0xFFFFFFFFull);

    cpu.write_register(2, 0xFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFE00000001ull);

}



void multiply_high_smulh_umulh() {

    CPU cpu;

    Memory memory(0x100);



    // 1. SMULH X0, X1, X2: 0x100000000 * 0x200000000 = 2 * 2^64 -> high = 2

    // Word: 0x9B427C20u

    memory.write_u32(0, 0x9B427C20u);

    cpu.write_register(1, 0x100000000ull);

    cpu.write_register(2, 0x200000000ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 2ull);

    CHECK(cpu.get_program_counter() == 4);



    // 2. SMULH negative x positive: (-1) * 1 = -1 -> high = 0xFFFFFFFFFFFFFFFF

    memory.write_u32(4, 0x9B427C20u);

    cpu.write_register(1, 0xFFFFFFFFFFFFFFFFull);

    cpu.write_register(2, 1);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFFFFFFull);



    // 3. SMULH negative x negative: (-1) * (-1) = 1 -> high = 0

    memory.write_u32(8, 0x9B427C20u);

    cpu.write_register(1, 0xFFFFFFFFFFFFFFFFull);

    cpu.write_register(2, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0ull);



    // 4. SMULH sign boundary: (-2^63) * 2 = -2^64 -> high = 0xFFFFFFFFFFFFFFFF

    memory.write_u32(12, 0x9B427C20u);

    cpu.write_register(1, 0x8000000000000000ull);

    cpu.write_register(2, 2);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFFFFFFull);



    // 5. UMULH X0, X1, X2: 0x100000000 * 0x200000000 = 2 * 2^64 -> high = 2

    // Word: 0x9BC27C20u

    memory.write_u32(16, 0x9BC27C20u);

    cpu.write_register(1, 0x100000000ull);

    cpu.write_register(2, 0x200000000ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 2ull);



    // 6. UMULH max unsigned: (2^64 - 1)^2 -> high = 2^64 - 2 = 0xFFFFFFFFFFFFFFFE

    memory.write_u32(20, 0x9BC27C20u);

    cpu.write_register(1, 0xFFFFFFFFFFFFFFFFull);

    cpu.write_register(2, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFFFFFEull);



    // 7. UMULH with zero operand -> high = 0

    memory.write_u32(24, 0x9BC27C20u);

    cpu.write_register(1, 0xFFFFFFFFFFFFFFFFull);

    cpu.write_register(2, 0);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0ull);

}



void multiply_register_31_flags_and_unallocated() {

    CPU cpu;

    Memory memory(0x100);



    // 1. Rn=31 reads 0: MADD W0, WZR, W1, W2 -> 0 * W1 + W2 = W2

    // Word: 0x1B010BE0u

    memory.write_u32(0, 0x1B010BE0u);

    cpu.write_register(1, 0x12345678);

    cpu.write_register(2, 0x55);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x55ull);



    // 2. Rm=31 reads 0: MADD X0, X1, XZR, X2 -> X1 * 0 + X2 = X2

    // Word: 0x9B1F0820u

    memory.write_u32(4, 0x9B1F0820u);

    cpu.write_register(1, 0x123456789ABCDEF0ull);

    cpu.write_register(2, 0xBEEFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xBEEFull);



    // 3. Rd=31 discards write: MADD WZR, W1, W2, W3

    // Word: 0x1B020C3Fu

    memory.write_u32(8, 0x1B020C3Fu);

    cpu.set_stack_pointer(0x1000);

    cpu.step(memory);

    CHECK(cpu.get_stack_pointer() == 0x1000);



    // 4. Rd=31 discards write: MADD XZR, X1, X2, X3

    // Word: 0x9B020C3Fu

    memory.write_u32(12, 0x9B020C3Fu);

    cpu.set_stack_pointer(0x1000);

    cpu.step(memory);

    CHECK(cpu.get_stack_pointer() == 0x1000);



    // 5. Flags invariance: pre-set NZCV to 15 (0xF), execute MADD, SMULH, UMADDL; NZCV unchanged

    cpu.set_nzcv(0xF);

    memory.write_u32(16, 0x1B020C20u); // MADD W

    cpu.step(memory);

    CHECK(cpu.get_nzcv() == 0xF);



    memory.write_u32(20, 0x9B427C20u); // SMULH

    cpu.step(memory);

    CHECK(cpu.get_nzcv() == 0xF);



    memory.write_u32(24, 0x9BA20C20u); // UMADDL

    cpu.step(memory);

    CHECK(cpu.get_nzcv() == 0xF);



    // 6. Unallocated encodings rejection

    // 32-bit with op31 = 1: 0x1B200000u

    memory.write_u32(28, 0x1B200000u);

    cpu.set_program_counter(28);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 28); // failure atomicity



    // 64-bit with op31 = 3: 0x9B600000u

    memory.write_u32(32, 0x9B600000u);

    cpu.set_program_counter(32);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 32);



    // 64-bit with op31 = 4: 0x9B800000u

    memory.write_u32(36, 0x9B800000u);

    cpu.set_program_counter(36);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 36);



    // 64-bit with op31 = 7: 0x9BE00000u

    memory.write_u32(40, 0x9BE00000u);

    cpu.set_program_counter(40);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 40);



    // SMULH with Ra != 31: 0x9B420C20u

    memory.write_u32(44, 0x9B420C20u);

    cpu.set_program_counter(44);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 44);



    // SMULH with o0 == 1: 0x9B42FC20u

    memory.write_u32(48, 0x9B42FC20u);

    cpu.set_program_counter(48);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 48);



    // UMULH with Ra != 31: 0x9BC20C20u

    memory.write_u32(52, 0x9BC20C20u);

    cpu.set_program_counter(52);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 52);



    // UMULH with o0 == 1: 0x9BC2FC20u

    memory.write_u32(56, 0x9BC2FC20u);

    cpu.set_program_counter(56);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 56);

}



void division_32_and_64_bit() {

    CPU cpu;

    Memory memory(0x100);



    // 1. UDIV W0, W1, W2: 100 / 7 = 14 (0xE)

    // Word: 0x1AC20820u (sf=0, rm=2, opcode=000010, rn=1, rd=0)

    memory.write_u32(0, 0x1AC20820u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull); // verify zero-extension

    cpu.write_register(1, 100);

    cpu.write_register(2, 7);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 14ull);

    CHECK(cpu.get_program_counter() == 4);



    // 2. UDIV W0, W1, W2 division by zero returns 0

    memory.write_u32(4, 0x1AC20820u);

    cpu.write_register(0, 0x12345678ull);

    cpu.write_register(1, 100);

    cpu.write_register(2, 0);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0ull);



    // 3. SDIV W0, W1, W2: (-100) / 7 = -14

    // Word: 0x1AC20C20u (sf=0, rm=2, opcode=000011, rn=1, rd=0)

    memory.write_u32(8, 0x1AC20C20u);

    cpu.write_register(1, static_cast<std::uint32_t>(-100));

    cpu.write_register(2, 7);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == (static_cast<std::uint64_t>(static_cast<std::uint32_t>(-14))));



    // 4. SDIV W0, W1, W2 INT32_MIN / -1 = INT32_MIN

    memory.write_u32(12, 0x1AC20C20u);

    cpu.write_register(1, 0x80000000u);

    cpu.write_register(2, static_cast<std::uint32_t>(-1));

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x80000000ull);



    // 5. SDIV W0, W1, W2 division by zero returns 0

    memory.write_u32(16, 0x1AC20C20u);

    cpu.write_register(0, 0x55555555ull);

    cpu.write_register(1, 100);

    cpu.write_register(2, 0);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0ull);



    // 6. UDIV X0, X1, X2: 100000000000ULL / 3000 = 33333333ULL

    // Word: 0x9AC20820u

    memory.write_u32(20, 0x9AC20820u);

    cpu.write_register(1, 100000000000ull);

    cpu.write_register(2, 3000ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 33333333ull);



    // 7. UDIV X0, X1, X2 division by zero returns 0

    memory.write_u32(24, 0x9AC20820u);

    cpu.write_register(0, 0x9999ull);

    cpu.write_register(1, 1000ull);

    cpu.write_register(2, 0ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0ull);



    // 8. SDIV X0, X1, X2 INT64_MIN / -1 = INT64_MIN

    // Word: 0x9AC20C20u

    memory.write_u32(28, 0x9AC20C20u);

    cpu.write_register(1, 0x8000000000000000ull);

    cpu.write_register(2, static_cast<std::uint64_t>(-1ll));

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x8000000000000000ull);



    // 9. SDIV X0, X1, X2 division by zero returns 0

    memory.write_u32(32, 0x9AC20C20u);

    cpu.write_register(0, 0x8888ull);

    cpu.write_register(1, 500ull);

    cpu.write_register(2, 0ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0ull);

}



void variable_shifts_lslv_lsrv_asrv_rorv() {

    CPU cpu;

    Memory memory(0x100);



    // 1. LSLV W0, W1, W2: 0x12 << 4 = 0x120

    // Word: 0x1AC22020u (sf=0, rm=2, opcode=001000, rn=1, rd=0)

    memory.write_u32(0, 0x1AC22020u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.write_register(1, 0x12u);

    cpu.write_register(2, 4u | 0xE0u); // bits beyond 5 masked out!

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x120ull);



    // 2. LSRV W0, W1, W2: 0x120 >> 4 = 0x12

    // Word: 0x1AC22420u (opcode=001001)

    memory.write_u32(4, 0x1AC22420u);

    cpu.write_register(1, 0x120u);

    cpu.write_register(2, 4u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x12ull);



    // 3. ASRV W0, W1, W2 sign extension: 0x80000000 >> 4 = 0xF8000000

    // Word: 0x1AC22820u (opcode=001010)

    memory.write_u32(8, 0x1AC22820u);

    cpu.write_register(1, 0x80000000u);

    cpu.write_register(2, 4u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xF8000000ull);



    // 4. RORV W0, W1, W2: 0x12345678 ROR 8 = 0x78123456

    // Word: 0x1AC22C20u (opcode=001011)

    memory.write_u32(12, 0x1AC22C20u);

    cpu.write_register(1, 0x12345678u);

    cpu.write_register(2, 8u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x78123456ull);



    // 5. LSLV X0, X1, X2: 0x1234ULL << 16 = 0x12340000ULL

    // Word: 0x9AC22020u

    memory.write_u32(16, 0x9AC22020u);

    cpu.write_register(1, 0x1234ull);

    cpu.write_register(2, 16u | 0x1C0u); // upper bits masked to 6 bits!

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x12340000ull);



    // 6. ASRV X0, X1, X2: 0x8000000000000000ULL >> 8 = 0xFF80000000000000ULL

    // Word: 0x9AC22820u

    memory.write_u32(20, 0x9AC22820u);

    cpu.write_register(1, 0x8000000000000000ull);

    cpu.write_register(2, 8u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFF80000000000000ull);



    // 7. RORV X0, X1, X2: 0x0123456789ABCDEFULL ROR 16 = 0xCDEF0123456789ABULL

    // Word: 0x9AC22C20u

    memory.write_u32(24, 0x9AC22C20u);

    cpu.write_register(1, 0x0123456789ABCDEFull);

    cpu.write_register(2, 16u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xCDEF0123456789ABull);

}



void bitfield_extract_extr() {

    CPU cpu;

    Memory memory(0x100);



    // 1. EXTR W0, W1, W2, #8: concat(W1, W2) >> 8

    // W1 = 0x12345678, W2 = 0x9ABCDEF0 -> concat = 0x123456789ABCDEF0

    // Result = 0x789ABCDE

    // Word: 0x13822020u (sf=0, N=0, rm=2, imms=8 (001000), rn=1, rd=0)

    memory.write_u32(0, 0x13822020u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.write_register(1, 0x12345678u);

    cpu.write_register(2, 0x9ABCDEF0u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x789ABCDEull);



    // 2. EXTR W0, W1, W2, #0: returns W2

    // Word: 0x13820020u (imms=0)

    memory.write_u32(4, 0x13820020u);

    cpu.write_register(1, 0x11112222u);

    cpu.write_register(2, 0x33334444u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x33334444ull);



    // 3. EXTR W0, W1, W2, #31:

    // Word: 0x13827C20u (imms=31 = 011111)

    memory.write_u32(8, 0x13827C20u);

    cpu.write_register(1, 0x00000001u);

    cpu.write_register(2, 0x80000000u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 3ull); // (1 << 1) | 1 = 3



    // 4. EXTR X0, X1, X2, #16:

    // Word: 0x93C24020u (sf=1, N=1, rm=2, imms=16, rn=1, rd=0)

    memory.write_u32(12, 0x93C24020u);

    cpu.write_register(1, 0x0123456789ABCDEFull);

    cpu.write_register(2, 0xFEDCBA9876543210ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xCDEF0123456789ABull || cpu.read_register(0) == ((0xFEDCBA9876543210ull >> 16u) | (0x0123456789ABCDEFull << 48u)));



    // 5. ROR X0, X1, #16 alias (Rn == Rm):

    // Word: 0x93C14020u (rn=1, rm=1)

    memory.write_u32(16, 0x93C14020u);

    cpu.write_register(1, 0x0123456789ABCDEFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xCDEF0123456789ABull);

}



void bit_manipulation_rbit_rev_clz_cls() {

    CPU cpu;

    Memory memory(0x100);



    // 1. RBIT W0, W1: 0x80000000 -> 0x00000001

    // Word: 0x5AC00020u (sf=0, opcode=000000, rn=1, rd=0)

    memory.write_u32(0, 0x5AC00020u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.write_register(1, 0x80000000u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 1ull);



    // 2. RBIT X0, X1: 0x8000000000000000ULL -> 0x0000000000000001ULL

    // Word: 0xDAC00020u

    memory.write_u32(4, 0xDAC00020u);

    cpu.write_register(1, 0x8000000000000000ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 1ull);



    // 3. REV16 W0, W1: 0x12345678 -> 0x34127856

    // Word: 0x5AC00420u (opcode=000001)

    memory.write_u32(8, 0x5AC00420u);

    cpu.write_register(1, 0x12345678u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x34127856ull);



    // 4. REV W0, W1: 0x12345678 -> 0x78563412

    // Word: 0x5AC00820u (opcode=000010)

    memory.write_u32(12, 0x5AC00820u);

    cpu.write_register(1, 0x12345678u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x78563412ull);



    // 5. REV32 X0, X1: 0x123456789ABCDEF0ULL -> 0x78563412F0DEBC9AULL

    // Word: 0xDAC00820u (sf=1, opcode=000010)

    memory.write_u32(16, 0xDAC00820u);

    cpu.write_register(1, 0x123456789ABCDEF0ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x78563412F0DEBC9Aull);



    // 6. REV X0, X1: 0x123456789ABCDEF0ULL -> 0xF0DEBC9A78563412ULL

    // Word: 0xDAC00C20u (sf=1, opcode=000011)

    memory.write_u32(20, 0xDAC00C20u);

    cpu.write_register(1, 0x123456789ABCDEF0ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xF0DEBC9A78563412ull);



    // 7. CLZ W0, W1 on 0 -> 32, on 0x00FF0000 -> 8

    // Word: 0x5AC01020u (opcode=000100)

    memory.write_u32(24, 0x5AC01020u);

    cpu.write_register(1, 0u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 32ull);



    memory.write_u32(28, 0x5AC01020u);

    cpu.write_register(1, 0x00FF0000u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 8ull);



    // 8. CLZ X0, X1 on 0 -> 64

    // Word: 0xDAC01020u

    memory.write_u32(32, 0xDAC01020u);

    cpu.write_register(1, 0ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 64ull);



    // 9. CLS W0, W1 on 0 -> 31, on 0xFFFFFFFF -> 31, on 0x80000000 -> 0

    // Word: 0x5AC01420u (opcode=000101)

    memory.write_u32(36, 0x5AC01420u);

    cpu.write_register(1, 0u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 31ull);



    memory.write_u32(40, 0x5AC01420u);

    cpu.write_register(1, 0xFFFFFFFFu);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 31ull);



    memory.write_u32(44, 0x5AC01420u);

    cpu.write_register(1, 0x80000000u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0ull);



    // 10. CLS X0, X1 on 0 -> 63, on 0xFFFFFFFFFFFFFFFF -> 63

    // Word: 0xDAC01420u

    memory.write_u32(48, 0xDAC01420u);

    cpu.write_register(1, 0ull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 63ull);



    memory.write_u32(52, 0xDAC01420u);

    cpu.write_register(1, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 63ull);

}



void phase27_register_31_flags_and_unallocated() {

    CPU cpu;

    Memory memory(0x100);



    // 1. Destination WZR/XZR discards write and never touches SP

    cpu.set_stack_pointer(0x1234567890ABCDEFull);

    // UDIV WZR, W1, W2: word 0x1AC2083Fu (rd=31)

    memory.write_u32(0, 0x1AC2083Fu);

    cpu.write_register(1, 100);

    cpu.write_register(2, 5);

    cpu.step(memory);

    CHECK(cpu.get_stack_pointer() == 0x1234567890ABCDEFull);



    // EXTR XZR, X1, X2, #8: word 0x93C2203Fu (rd=31)

    memory.write_u32(4, 0x93C2203Fu);

    cpu.write_register(1, 100);

    cpu.write_register(2, 5);

    cpu.step(memory);

    CHECK(cpu.get_stack_pointer() == 0x1234567890ABCDEFull);



    // 2. NZCV flag invariance: flags preserved across all instructions

    cpu.set_nzcv(0xFu); // N=1, Z=1, C=1, V=1

    memory.write_u32(8, 0x1AC20820u); // UDIV W0, W1, W2

    cpu.write_register(1, 40);

    cpu.write_register(2, 4);

    cpu.step(memory);

    CHECK(cpu.get_nzcv() == 0xFu);



    memory.write_u32(12, 0x5AC00020u); // RBIT W0, W1

    cpu.write_register(1, 0x12345678u);

    cpu.step(memory);

    CHECK(cpu.get_nzcv() == 0xFu);



    // 3. Rejection of unallocated encodings:

    // a) 32-bit REV with opcode 3 (unallocated for 32-bit): word 0x5AC00C20u

    memory.write_u32(16, 0x5AC00C20u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));



    // b) DP 2-source invalid opcode (e.g. opcode 0x00): word 0x1AC20020u

    memory.write_u32(16, 0x1AC20020u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));



    // c) DP 1-source with Rm != 0 (bits 20:16 set): word 0x5AC10020u

    memory.write_u32(16, 0x5AC10020u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));



    // d) EXTR 32-bit with N=1 (word 0x13C22020u):

    memory.write_u32(16, 0x13C22020u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));



    // e) EXTR 32-bit with imms >= 32 (bit 15 set, word 0x1382A020u):

    memory.write_u32(16, 0x1382A020u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

}



void branch_unconditional_immediate_b_and_bl() {

    CPU cpu;

    Memory memory(4096);



    // 1. Forward B: PC starts at 0x100, B #+16 (imm26 = 4 -> 4 words = 16 bytes).

    // Word for B: 0x14000000u | (4 & 0x03FFFFFFu) = 0x14000004u

    cpu.set_program_counter(0x100);

    memory.write_u32(0x100, 0x14000004u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x110);



    // 2. Backward B: PC at 0x200, B #-16 (imm26 = -4 = 0x03FFFFFCu).

    // Word: 0x14000000u | 0x03FFFFFCu = 0x17FFFFFCu

    cpu.set_program_counter(0x200);

    memory.write_u32(0x200, 0x17FFFFFCu);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x1F0);



    // 3. BL forward: PC at 0x300, BL #+24 (imm26 = 6).

    // Word for BL: 0x94000000u | 6 = 0x94000006u

    // Should update PC to 0x318 and X30 to 0x304

    cpu.set_program_counter(0x300);

    cpu.write_register(30, 0);

    memory.write_u32(0x300, 0x94000006u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x318);

    CHECK(cpu.read_register(30) == 0x304);



    // 4. BL backward: PC at 0x400, BL #-8 (imm26 = -2 = 0x03FFFFFEu).

    // Word: 0x94000000u | 0x03FFFFFEu = 0x97FFFFFEu

    // Should update PC to 0x3F8 and X30 to 0x404

    cpu.set_program_counter(0x400);

    memory.write_u32(0x400, 0x97FFFFFEu);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x3F8);

    CHECK(cpu.read_register(30) == 0x404);



    // 5. NZCV flags are preserved across B and BL

    cpu.set_nzcv(0xAu); // N=1, Z=0, C=1, V=0

    cpu.set_program_counter(0x500);

    memory.write_u32(0x500, 0x14000002u); // B #+8

    cpu.step(memory);

    CHECK(cpu.get_nzcv() == 0xAu);



    cpu.set_program_counter(0x508);

    memory.write_u32(0x508, 0x94000002u); // BL #+8

    cpu.step(memory);

    CHECK(cpu.get_nzcv() == 0xAu);

}



void branch_unconditional_register_br_blr_ret() {

    CPU cpu;

    Memory memory(4096);



    // 1. BR X1: jumps to address in X1

    // Encoding: 0xD61F0000u | (rn << 5) -> for rn=1: 0xD61F0020u

    cpu.set_program_counter(0x100);

    cpu.write_register(1, 0x240);

    memory.write_u32(0x100, 0xD61F0020u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x240);



    // 2. BLR X2: jumps to address in X2 and sets X30 to PC + 4

    // Encoding: 0xD63F0000u | (rn << 5) -> for rn=2: 0xD63F0040u

    cpu.set_program_counter(0x200);

    cpu.write_register(2, 0x380);

    cpu.write_register(30, 0);

    memory.write_u32(0x200, 0xD63F0040u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x380);

    CHECK(cpu.read_register(30) == 0x204);



    // 3. BLR X30 (overlap case): target is read from X30 before X30 is overwritten!

    // Encoding: 0xD63F0000u | (30 << 5) = 0xD63F03C0u

    cpu.set_program_counter(0x400);

    cpu.write_register(30, 0x600);

    memory.write_u32(0x400, 0xD63F03C0u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x600);

    CHECK(cpu.read_register(30) == 0x404);



    // 4. RET (default Rn=30): opcode 0xD65F03C0u

    cpu.set_program_counter(0x500);

    cpu.write_register(30, 0x700);

    memory.write_u32(0x500, 0xD65F03C0u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x700);



    // 5. RET X3: explicit register Rn=3 -> opcode 0xD65F0060u

    cpu.set_program_counter(0x700);

    cpu.write_register(3, 0x850);

    memory.write_u32(0x700, 0xD65F0060u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x850);



    // 6. Register 31 as target: BR XZR reads 0

    // Rn=31 -> 0xD61F03E0u

    cpu.set_program_counter(0x100);

    memory.write_u32(0x100, 0xD61F03E0u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0);



    // 7. NZCV preserved across BR, BLR, RET

    cpu.set_nzcv(0x5u);

    cpu.set_program_counter(0x200);

    cpu.write_register(1, 0x300);

    memory.write_u32(0x200, 0xD61F0020u); // BR X1

    cpu.step(memory);

    CHECK(cpu.get_nzcv() == 0x5u);

}



void branch_register_misaligned_target_rejected() {

    CPU cpu;

    Memory memory(4096);



    // 1. BR X1 with misaligned address (target & 3 != 0)

    cpu.set_program_counter(0x100);

    cpu.write_register(1, 0x201); // not 4-byte aligned

    memory.write_u32(0x100, 0xD61F0020u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    // Failure atomicity: PC unchanged

    CHECK(cpu.get_program_counter() == 0x100);



    // 2. BLR X2 with misaligned target: throws exception, X30 is NOT modified

    cpu.set_program_counter(0x200);

    cpu.write_register(2, 0x303);

    cpu.write_register(30, 0xCAFE);

    memory.write_u32(0x200, 0xD63F0040u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 0x200);

    CHECK(cpu.read_register(30) == 0xCAFE);



    // 3. RET X3 with misaligned target

    cpu.set_program_counter(0x300);

    cpu.write_register(3, 0x402);

    memory.write_u32(0x300, 0xD65F0060u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 0x300);

}



void branch_compare_and_branch_cbz_cbnz() {

    CPU cpu;

    Memory memory(4096);



    // 1. CBZ W0 taken when W0 == 0

    // Encoding: sf=0 (bit 31=0), op=0 (bit 24=0), imm19 = 4 (offset +16) -> 0x34000080u

    cpu.set_program_counter(0x100);

    cpu.write_register(0, 0);

    memory.write_u32(0x100, 0x34000080u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x110);



    // 2. CBZ W0 untaken when W0 != 0

    cpu.set_program_counter(0x110);

    cpu.write_register(0, 42);

    memory.write_u32(0x110, 0x34000080u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x114);



    // 3. CBZ W0 with upper 32 bits non-zero but low 32 bits zero -> taken!

    cpu.set_program_counter(0x120);

    cpu.write_register(0, 0x1234567800000000ULL);

    memory.write_u32(0x120, 0x34000080u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x130);



    // 4. CBNZ W0 taken when low 32 bits != 0

    // Encoding: sf=0, op=1 (bit 24=1) -> 0x35000080u

    cpu.set_program_counter(0x200);

    cpu.write_register(0, 1);

    memory.write_u32(0x200, 0x35000080u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x210);



    // 5. CBNZ W0 untaken when low 32 bits == 0 (even if upper bits set)

    cpu.set_program_counter(0x210);

    cpu.write_register(0, 0x999900000000ULL);

    memory.write_u32(0x210, 0x35000080u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x214);



    // 6. CBZ X0 (sf=1): 0x34000080u | 0x80000000u = 0xB4000080u

    // Untaken when upper bits are set because full 64-bit value is non-zero

    cpu.set_program_counter(0x300);

    cpu.write_register(0, 0x1234567800000000ULL);

    memory.write_u32(0x300, 0xB4000080u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x304);



    // Taken when full 64-bit value is zero

    cpu.set_program_counter(0x310);

    cpu.write_register(0, 0);

    memory.write_u32(0x310, 0xB4000080u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x320);



    // 7. CBNZ X0 (sf=1): 0xB5000080u

    cpu.set_program_counter(0x400);

    cpu.write_register(0, 0x100000000ULL);

    memory.write_u32(0x400, 0xB5000080u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x410);



    // 8. Register 31: CBZ WZR / XZR always taken, CBNZ WZR / XZR never taken

    // CBZ WZR, #+16: rt=31 -> 0x3400009Fu

    cpu.set_program_counter(0x500);

    memory.write_u32(0x500, 0x3400009Fu);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x510);



    // CBNZ WZR, #+16: rt=31 -> 0x3500009Fu

    cpu.set_program_counter(0x520);

    memory.write_u32(0x520, 0x3500009Fu);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x524);



    // 9. Negative offset (backward branch): imm19 = -2 (0x7FFFE) -> offset -8

    // sf=1, op=0, imm19=0x7FFFE, rt=0 -> 0xB4000000u | (0x7FFFEu << 5) = 0xB4FFFFC0u

    cpu.set_program_counter(0x600);

    cpu.write_register(0, 0);

    memory.write_u32(0x600, 0xB4FFFFC0u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x5F8);



    // 10. NZCV untouched across CBZ/CBNZ

    cpu.set_nzcv(0xCu);

    cpu.set_program_counter(0x700);

    cpu.write_register(0, 0);

    memory.write_u32(0x700, 0x34000080u);

    cpu.step(memory);

    CHECK(cpu.get_nzcv() == 0xCu);

}



void branch_test_bit_and_branch_tbz_tbnz() {

    CPU cpu;

    Memory memory(4096);



    // TBZ encoding: bit 31 = b5, bits 23:19 = b40. bitpos = (b5 << 5) | b40.

    // op = bit 24 (0 = TBZ, 1 = TBNZ). imm14 = bits 18:5. Rt = bits 4:0.

    // Base: 0x36000000u.



    // 1. TBZ X0, #0, #+16 (b5=0, b40=0 -> bit 0, imm14=4, Rt=0)

    // Word: 0x36000000u | (0 << 19) | (4 << 5) | 0 = 0x36000080u

    cpu.set_program_counter(0x100);

    cpu.write_register(0, 0xFE); // bit 0 is 0 -> taken

    memory.write_u32(0x100, 0x36000080u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x110);



    cpu.set_program_counter(0x110);

    cpu.write_register(0, 0xFF); // bit 0 is 1 -> untaken

    memory.write_u32(0x110, 0x36000080u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x114);



    // 2. TBNZ X0, #0, #+16 (op=1 -> bit 24=1) -> 0x37000080u

    cpu.set_program_counter(0x200);

    cpu.write_register(0, 0xFF); // bit 0 is 1 -> taken

    memory.write_u32(0x200, 0x37000080u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x210);



    // 3. Bit 31 test: b5=0, b40=31 (0x1F) -> (0x1F << 19) = 0x00F80000u

    // TBZ X0, #31, #+16 -> 0x36F80080u

    cpu.set_program_counter(0x300);

    cpu.write_register(0, 0x80000000ULL); // bit 31 is 1 -> untaken

    memory.write_u32(0x300, 0x36F80080u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x304);



    // 4. Bit 32 test: b5=1 (bit 31=1), b40=0 -> bitpos = 32

    // TBZ X0, #32, #+16 -> 0xB6000080u

    cpu.set_program_counter(0x400);

    cpu.write_register(0, 0x100000000ULL); // bit 32 is 1 -> untaken

    memory.write_u32(0x400, 0xB6000080u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x404);



    // TBNZ X0, #32, #+16 -> 0xB7000080u

    cpu.set_program_counter(0x410);

    cpu.write_register(0, 0x100000000ULL); // bit 32 is 1 -> taken

    memory.write_u32(0x410, 0xB7000080u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x420);



    // 5. Bit 63 (MSB): b5=1, b40=31 -> bitpos = 63

    // TBNZ X0, #63, #+16 -> 0xB7F80080u

    cpu.set_program_counter(0x500);

    cpu.write_register(0, 0x8000000000000000ULL); // bit 63 is 1 -> taken

    memory.write_u32(0x500, 0xB7F80080u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x510);



    // 6. Register 31 (XZR): TBZ XZR always taken, TBNZ XZR never taken

    // TBZ XZR, #5, #+16 -> b5=0, b40=5, imm14=4, Rt=31 -> 0x3628009Fu

    cpu.set_program_counter(0x600);

    memory.write_u32(0x600, 0x3628009Fu);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x610);



    // TBNZ XZR, #5, #+16 -> 0x3728009Fu

    cpu.set_program_counter(0x620);

    memory.write_u32(0x620, 0x3728009Fu);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x624);



    // 7. Backward branch: imm14 = -2 (0x3FFE) -> offset -8

    // TBZ X0, #0, #-8: 0x36000000u | (0x3FFEu << 5) = 0x3607FFC0u

    cpu.set_program_counter(0x700);

    cpu.write_register(0, 0); // bit 0 is 0 -> taken

    memory.write_u32(0x700, 0x3607FFC0u);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x6F8);



    // 8. NZCV untouched across TBZ/TBNZ

    cpu.set_nzcv(0x7u);

    cpu.set_program_counter(0x800);

    cpu.write_register(0, 0);

    memory.write_u32(0x800, 0x36000080u);

    cpu.step(memory);

    CHECK(cpu.get_nzcv() == 0x7u);

}



void branch_conditional_b_cond_all_conditions_and_nv() {

    CPU cpu;

    Memory memory(4096);



    // Test each condition code with both matching and non-matching flags.

    // B.cond format: 0x54000000u | (imm19 << 5) | cond.

    // imm19 = 4 (+16 bytes) -> (4 << 5) = 0x80u.

    auto make_b_cond = [](uint32_t cond, int32_t imm19 = 4) -> uint32_t {

        return 0x54000000u | ((static_cast<uint32_t>(imm19) & 0x7FFFFu) << 5) | (cond & 0xFu);

    };



    struct CondTest {

        uint32_t cond;

        uint8_t true_nzcv;

        uint8_t false_nzcv;

    };



    // Flags: N=bit3 (8), Z=bit2 (4), C=bit1 (2), V=bit0 (1)

    CondTest tests[] = {

        { 0x0, 4, 0 },     // EQ (Z=1) vs (Z=0)

        { 0x1, 0, 4 },     // NE (Z=0) vs (Z=1)

        { 0x2, 2, 0 },     // CS/HS (C=1) vs (C=0)

        { 0x3, 0, 2 },     // CC/LO (C=0) vs (C=1)

        { 0x4, 8, 0 },     // MI (N=1) vs (N=0)

        { 0x5, 0, 8 },     // PL (N=0) vs (N=1)

        { 0x6, 1, 0 },     // VS (V=1) vs (V=0)

        { 0x7, 0, 1 },     // VC (V=0) vs (V=1)

        { 0x8, 2, 0 },     // HI (C=1 && Z=0) vs (C=0 && Z=0)

        { 0x9, 4, 2 },     // LS (C=0 || Z=1) vs (C=1 && Z=0)

        { 0xA, 0, 8 },     // GE (N==V, 0==0) vs (N=1, V=0)

        { 0xB, 8, 0 },     // LT (N!=V, 1!=0) vs (N=0, V=0)

        { 0xC, 0, 4 },     // GT (Z=0 && N==V) vs (Z=1)

        { 0xD, 4, 0 },     // LE (Z=1 || N!=V) vs (Z=0 && N==V)

    };



    uint64_t pc = 0x100;

    for (const auto& t : tests) {

        // Taken case

        cpu.set_program_counter(pc);

        cpu.set_nzcv(t.true_nzcv);

        memory.write_u32(pc, make_b_cond(t.cond, 4));

        cpu.step(memory);

        CHECK(cpu.get_program_counter() == pc + 16);

        CHECK(cpu.get_nzcv() == t.true_nzcv);



        // Untaken case

        pc += 16;

        cpu.set_program_counter(pc);

        cpu.set_nzcv(t.false_nzcv);

        memory.write_u32(pc, make_b_cond(t.cond, 4));

        cpu.step(memory);

        CHECK(cpu.get_program_counter() == pc + 4);

        CHECK(cpu.get_nzcv() == t.false_nzcv);



        pc += 4;

    }



    // Condition 0xE: AL (always taken)

    cpu.set_program_counter(pc);

    cpu.set_nzcv(0);

    memory.write_u32(pc, make_b_cond(0xE, 4));

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == pc + 16);



    // Condition 0xF: NV (reserved in AArch64) -> must throw std::runtime_error

    pc += 16;

    cpu.set_program_counter(pc);

    memory.write_u32(pc, make_b_cond(0xF, 4));

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == pc); // Failure atomicity

}



void branch_unallocated_encodings_and_fault_atomicity() {

    CPU cpu;

    Memory memory(4096);



    // 1. Unallocated opcode in unconditional branch register space:

    // Bits 22:21 = 11 (0xD67F0000u)

    cpu.set_program_counter(0x100);

    memory.write_u32(0x100, 0xD67F0000u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 0x100);



    // 2. Unallocated bits in B.cond: bit 4 must be 0

    // 0x54000010u has bit 4 = 1

    cpu.set_program_counter(0x200);

    memory.write_u32(0x200, 0x54000010u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 0x200);



    // 3. Unconditional branch register with non-zero op2/op3/op4:

    // e.g. bit 0 set: 0xD61F0001u

    cpu.set_program_counter(0x300);

    memory.write_u32(0x300, 0xD61F0001u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

    CHECK(cpu.get_program_counter() == 0x300);

}



void sign_extending_byte_loads_ldrsb() {

    Memory memory(0x1000);

    CPU cpu;



    memory.write_u8(0x100, 0x7Fu);

    memory.write_u8(0x101, 0x80u);

    memory.write_u8(0x102, 0xFEu);



    // 1. LDRSB W0, [X1, #0] -> 0x39C00020

    memory.write_u32(0, 0x39C00020u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.write_register(1, 0x100);

    cpu.set_program_counter(0);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x7Full);

    CHECK(cpu.get_program_counter() == 4);



    // 2. LDRSB W0, [X1, #1] -> 0x39C00420

    memory.write_u32(4, 0x39C00420u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x00000000FFFFFF80ull);

    CHECK(cpu.get_program_counter() == 8);



    // 3. LDRSB X0, [X1, #0] -> 0x39800020

    memory.write_u32(8, 0x39800020u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x7Full);

    CHECK(cpu.get_program_counter() == 12);



    // 4. LDRSB X0, [X1, #1] -> 0x39800420

    memory.write_u32(12, 0x39800420u);

    cpu.write_register(0, 0);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFFFF80ull);

    CHECK(cpu.get_program_counter() == 16);



    // 5. LDRSB X0, [X1, #2] -> 0x39800820

    memory.write_u32(16, 0x39800820u);

    cpu.write_register(0, 0);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFFFFFEull);

    CHECK(cpu.get_program_counter() == 20);

}



void sign_extending_halfword_loads_ldrsh() {

    Memory memory(0x1000);

    CPU cpu;



    memory.write_u16(0x100, 0x1234u);

    memory.write_u16(0x102, 0x8000u);

    memory.write_u16(0x104, 0xABCDu);



    // 1. LDRSH W0, [X1, #0] -> 0x79C00020

    memory.write_u32(0, 0x79C00020u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.write_register(1, 0x100);

    cpu.set_program_counter(0);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x1234ull);



    // 2. LDRSH W0, [X1, #2] (offset=1) -> 0x79C00420

    memory.write_u32(4, 0x79C00420u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x00000000FFFF8000ull);



    // 3. LDRSH X0, [X1, #2] (offset=1) -> 0x79800420

    memory.write_u32(8, 0x79800420u);

    cpu.write_register(0, 0);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFF8000ull);



    // 4. LDRSH X0, [X1, #4] (offset=2) -> 0x79800820

    memory.write_u32(12, 0x79800820u);

    cpu.write_register(0, 0);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFFABCDull);

}



void sign_extending_word_loads_ldrsw() {

    Memory memory(0x1000);

    CPU cpu;



    memory.write_u32(0x100, 0x12345678u);

    memory.write_u32(0x104, 0x80000000u);

    memory.write_u32(0x108, 0xCAFEBABEu);



    // 1. LDRSW X0, [X1, #0] -> 0xB9800020

    memory.write_u32(0, 0xB9800020u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.write_register(1, 0x100);

    cpu.set_program_counter(0);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x12345678ull);



    // 2. LDRSW X0, [X1, #4] (offset=1) -> 0xB9800420

    memory.write_u32(4, 0xB9800420u);

    cpu.write_register(0, 0);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFF80000000ull);



    // 3. LDRSW X0, [X1, #8] (offset=2) -> 0xB9800820

    memory.write_u32(8, 0xB9800820u);

    cpu.write_register(0, 0);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFFCAFEBABEull);

}



void sign_extending_addressing_modes() {

    Memory memory(0x1000);

    CPU cpu;



    memory.write_u8(0x200, 0x88u);

    memory.write_u8(0x204, 0x99u);

    memory.write_u16(0x208, 0x8888u);

    memory.write_u32(0x210, 0x80000000u);



    // LDURSB W0, [X1, #4] -> 0x38C04020

    memory.write_u32(0, 0x38C04020u);

    cpu.write_register(1, 0x200);

    cpu.set_program_counter(0);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x00000000FFFFFF99ull);

    CHECK(cpu.read_register(1) == 0x200);



    // LDRSB W0, [X1, #4]! pre-index -> 0x38C04C20

    memory.write_u32(4, 0x38C04C20u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x00000000FFFFFF99ull);

    CHECK(cpu.read_register(1) == 0x204);



    // LDRSB W0, [X1], #-4 post-index -> 0x38DFC420

    memory.write_u32(8, 0x38DFC420u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x00000000FFFFFF99ull);

    CHECK(cpu.read_register(1) == 0x200);



    // Register offset: LDRSH X0, [X1, X2, LSL #1] -> 0x78A27820 (X2=4 -> offset=8 -> 0x208)

    memory.write_u32(12, 0x78A27820u);

    cpu.write_register(2, 4);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFF8888ull);

}



void pc_relative_literal_loads() {

    Memory memory(0x1000);

    CPU cpu;



    memory.write_u32(0x10, 0x87654321u);

    memory.write_u64(0x18, 0x0123456789ABCDEFull);



    // 1. LDR W0, [PC, #16] -> 0x18000080

    memory.write_u32(0, 0x18000080u);

    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    cpu.set_program_counter(0);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x87654321ull);



    // 2. LDRSW X0, [PC, #12] -> at PC=4, displacement=12 -> 0x10 -> 0x98000060

    memory.write_u32(4, 0x98000060u);

    cpu.write_register(0, 0);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFF87654321ull);



    // 3. LDR X0, [PC, #16] -> at PC=8, displacement=16 -> 0x18 -> 0x58000080

    memory.write_u32(8, 0x58000080u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x0123456789ABCDEFull);

}



void phase29_register_31_flags_and_unallocated() {

    Memory memory(0x1000);

    CPU cpu;



    memory.write_u8(0x100, 0x80u);

    memory.write_u32(0x100, 0x12345678u);



    // Destination register 31 (WZR / XZR): write is discarded

    memory.write_u32(0, 0x39C0003Fu);

    cpu.write_register(1, 0x100);

    cpu.set_stack_pointer(0x200);

    cpu.set_nzcv(0b1010);

    cpu.set_program_counter(0);

    cpu.step(memory);

    CHECK(cpu.get_stack_pointer() == 0x200);

    CHECK(cpu.get_nzcv() == 0b1010);



    // Base register 31 (SP): reads SP as base

    memory.write_u8(0x200, 0x55u);

    memory.write_u32(4, 0x39C003E0u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x55u);



    // Pre-index with SP writeback: LDRSB W0, [SP, #4]! -> 0x38C04FE0

    memory.write_u32(8, 0x38C04FE0u);

    memory.write_u8(0x204, 0x66u);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x66u);

    CHECK(cpu.get_stack_pointer() == 0x204);



    // Unallocated byte register offset with S=1: throws std::runtime_error

    memory.write_u32(12, 0x38E27820u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));



    // Unallocated literal load opc=11 (PRFM literal): throws std::runtime_error

    memory.write_u32(16, 0xD8000000u);

    cpu.set_program_counter(16);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

}



void scalar_byte_loads_and_stores() {

    Memory memory(0x1000);

    CPU cpu;



    // 1. STRB W0, [X1, #0] (0x39000020u)

    memory.write_u32(0, 0x39000020u);

    cpu.write_register(0, 0x123456ABull);

    cpu.write_register(1, 0x200ull);

    cpu.set_program_counter(0);

    cpu.step(memory);

    CHECK(memory.read_u8(0x200) == 0xABu);



    // 2. LDRB W2, [X1, #0] (0x39400022u) -> zero-extends to 64-bit

    memory.write_u32(4, 0x39400022u);

    cpu.write_register(2, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(2) == 0x00000000000000ABull);



    // 3. STURB W0, [X1, #-1] (0x381FF020u)

    memory.write_u32(8, 0x381FF020u);

    cpu.write_register(0, 0xCDull);

    cpu.step(memory);

    CHECK(memory.read_u8(0x1FF) == 0xCDu);



    // 4. LDURB W3, [X1, #-1] (0x385FF023u)

    memory.write_u32(12, 0x385FF023u);

    cpu.write_register(3, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(3) == 0x00000000000000CDull);



    // 5. STRB W0, [X1, #2]! pre-index (0x38002C20u)

    memory.write_u32(16, 0x38002C20u);

    cpu.write_register(0, 0xEFull);

    cpu.step(memory);

    CHECK(memory.read_u8(0x202) == 0xEFu);

    CHECK(cpu.read_register(1) == 0x202ull);



    // 6. LDRB W4, [X1], #-2 post-index (0x385FE424u)

    memory.write_u32(20, 0x385FE424u);

    cpu.write_register(4, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(4) == 0x00000000000000EFull);

    CHECK(cpu.read_register(1) == 0x200ull);



    // 7. STRB W0, [X1, X2] reg-off (0x38226820u), X2=5

    memory.write_u32(24, 0x38226820u);

    cpu.write_register(0, 0x77ull);

    cpu.write_register(2, 5ull);

    cpu.step(memory);

    CHECK(memory.read_u8(0x205) == 0x77u);



    // 8. LDRB W5, [X1, X2] reg-off (0x38626825u)

    memory.write_u32(28, 0x38626825u);

    cpu.write_register(5, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(5) == 0x0000000000000077ull);



    // 9. Register 31: STRB WZR, [X1, #0] stores 0; LDRB WZR discards

    memory.write_u32(32, 0x3900003Fu); // STRB WZR, [X1, #0]

    cpu.step(memory);

    CHECK(memory.read_u8(0x200) == 0x00u);



    memory.write_u32(36, 0x3940003Fu); // LDRB WZR, [X1, #0]

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 40);

}



void scalar_halfword_loads_and_stores() {

    Memory memory(0x1000);

    CPU cpu;



    // 1. STRH W0, [X1, #0] (0x79000020u)

    memory.write_u32(0, 0x79000020u);

    cpu.write_register(0, 0x1234ABCDull);

    cpu.write_register(1, 0x300ull);

    cpu.set_program_counter(0);

    cpu.step(memory);

    CHECK(memory.read_u16(0x300) == 0xABCDu);



    // 2. LDRH W2, [X1, #0] (0x79400022u) -> zero-extends to 64-bit

    memory.write_u32(4, 0x79400022u);

    cpu.write_register(2, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(2) == 0x000000000000ABCDull);



    // 3. STRH W0, [X1, #4] (0x79000820u, imm12=2 -> scaled offset 4)

    memory.write_u32(8, 0x79000820u);

    cpu.write_register(0, 0x12345678ull);

    cpu.step(memory);

    CHECK(memory.read_u16(0x304) == 0x5678u);



    // 4. LDRH W2, [X1, #4] (0x79400822u)

    memory.write_u32(12, 0x79400822u);

    cpu.write_register(2, 0);

    cpu.step(memory);

    CHECK(cpu.read_register(2) == 0x5678ull);



    // 5. STURH W0, [X1, #-2] (0x781FE020u)

    memory.write_u32(16, 0x781FE020u);

    cpu.write_register(0, 0xCAFEull);

    cpu.step(memory);

    CHECK(memory.read_u16(0x2FE) == 0xCAFEu);



    // 6. LDURH W3, [X1, #-2] (0x785FE023u)

    memory.write_u32(20, 0x785FE023u);

    cpu.write_register(3, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(3) == 0x000000000000CAFEull);



    // 7. STRH W0, [X1, #4]! pre-index (0x78004C20u)

    memory.write_u32(24, 0x78004C20u);

    cpu.write_register(0, 0xBEEFull);

    cpu.step(memory);

    CHECK(memory.read_u16(0x304) == 0xBEEFu);

    CHECK(cpu.read_register(1) == 0x304ull);



    // 8. LDRH W4, [X1], #-4 post-index (0x785FC424u)

    memory.write_u32(28, 0x785FC424u);

    cpu.write_register(4, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(4) == 0x000000000000BEEFull);

    CHECK(cpu.read_register(1) == 0x300ull);



    // 9. STRH W0, [X1, X2, LSL #1] reg-off (0x78227820u), X2=3 -> offset 6

    memory.write_u32(32, 0x78227820u);

    cpu.write_register(0, 0x4321ull);

    cpu.write_register(2, 3ull);

    cpu.step(memory);

    CHECK(memory.read_u16(0x306) == 0x4321u);



    // 10. LDRH W5, [X1, X2, LSL #1] reg-off (0x78627825u)

    memory.write_u32(36, 0x78627825u);

    cpu.write_register(5, 0xFFFFFFFFFFFFFFFFull);

    cpu.step(memory);

    CHECK(cpu.read_register(5) == 0x0000000000004321ull);

}



void load_pair_signed_words_ldpsw() {

    Memory memory(0x1000);

    CPU cpu;



    // Set up two 32-bit words at 0x400:

    // element 1: 0x80000001 (negative as signed 32-bit word -> sign-extends to 0xFFFFFFFF80000001)

    // element 2: 0x7FFFFFFF (positive as signed 32-bit word -> zero-extends upper 32 bits)

    memory.write_u32(0x400, 0x80000001u);

    memory.write_u32(0x404, 0x7FFFFFFFu);



    // 1. LDPSW X0, X1, [X2, #0] (0x69400440u)

    memory.write_u32(0, 0x69400440u);

    cpu.write_register(2, 0x400ull);

    cpu.set_program_counter(0);

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0xFFFFFFFF80000001ull);

    CHECK(cpu.read_register(1) == 0x000000007FFFFFFFull);

    CHECK(cpu.read_register(2) == 0x400ull);



    // 2. LDPSW X3, X4, [X2, #8]! pre-index (0x69C11043u) -> loads from 0x408

    memory.write_u32(4, 0x69C11043u);

    memory.write_u32(0x408, 0xFFFFFFFEu); // -2

    memory.write_u32(0x40C, 0x00000005u); // +5

    cpu.step(memory);

    CHECK(cpu.read_register(3) == 0xFFFFFFFFFFFFFFFEull);

    CHECK(cpu.read_register(4) == 0x0000000000000005ull);

    CHECK(cpu.read_register(2) == 0x408ull);



    // 3. LDPSW X5, X6, [X2], #-8 post-index (0x68FF1845u) -> loads from 0x408, writeback to 0x400

    memory.write_u32(8, 0x68FF1845u);

    cpu.step(memory);

    CHECK(cpu.read_register(5) == 0xFFFFFFFFFFFFFFFEull);

    CHECK(cpu.read_register(6) == 0x0000000000000005ull);

    CHECK(cpu.read_register(2) == 0x400ull);



    // 4. Overlap Rt == Rt2: LDPSW X0, X0, [X2, #0] (0x69400040u with target2=0 -> 0x69400040u)

    // Rt2 overwrites Rt1

    memory.write_u32(12, 0x69400040u); // target1=0, target2=0

    cpu.step(memory);

    CHECK(cpu.read_register(0) == 0x000000007FFFFFFFull);



    // 5. Register 31 as destination: LDPSW XZR, X1, [X2, #0] (0x6940045Fu)

    memory.write_u32(16, 0x6940045Fu);

    cpu.write_register(1, 0);

    cpu.set_stack_pointer(0x1000);

    cpu.step(memory);

    CHECK(cpu.read_register(1) == 0x000000007FFFFFFFull);

    CHECK(cpu.get_stack_pointer() == 0x1000);

}



void phase30_prfm_and_unallocated_encodings_rejected() {

    Memory memory(0x1000);

    CPU cpu;



    // 1. PRFM immediate: 0xF9800000u

    memory.write_u32(0, 0xF9800000u);

    CHECK_RUNTIME_ERROR(cpu.step(memory));



    // 2. PRFM unscaled: 0xF8800000u

    memory.write_u32(4, 0xF8800000u);

    cpu.set_program_counter(4);

    CHECK_RUNTIME_ERROR(cpu.step(memory));



    // 3. PRFM register offset: 0xF8A00800u

    memory.write_u32(8, 0xF8A00800u);

    cpu.set_program_counter(8);

    CHECK_RUNTIME_ERROR(cpu.step(memory));



    // 4. STRB register offset with invalid S=1: 0x38201800u

    memory.write_u32(12, 0x38201800u);

    cpu.set_program_counter(12);

    CHECK_RUNTIME_ERROR(cpu.step(memory));



    // 5. STRB register offset with option bit 1 = 0: 0x38200800u (option=0)

    memory.write_u32(16, 0x38200800u);

    cpu.set_program_counter(16);

    CHECK_RUNTIME_ERROR(cpu.step(memory));



    // 6. STRH register offset with option bit 1 = 0: 0x78200800u (option=0)

    memory.write_u32(20, 0x78200800u);

    cpu.set_program_counter(20);

    CHECK_RUNTIME_ERROR(cpu.step(memory));



    // 7. SIMD/FP unallocated immediate mode (mode=10): 0x3C000800u

    memory.write_u32(24, 0x3C000800u);

    cpu.set_program_counter(24);

    CHECK_RUNTIME_ERROR(cpu.step(memory));

}



void phase30_flags_and_stack_pointer() {

    Memory memory(0x1000);

    CPU cpu;



    cpu.set_nzcv(0b1101);

    cpu.set_stack_pointer(0x500);



    // 1. STRB W0, [SP, #0] (0x390003E0u)

    memory.write_u32(0, 0x390003E0u);

    cpu.write_register(0, 0x99ull);

    cpu.set_program_counter(0);

    cpu.step(memory);

    CHECK(memory.read_u8(0x500) == 0x99u);

    CHECK(cpu.get_nzcv() == 0b1101);

    CHECK(cpu.get_stack_pointer() == 0x500);



    // 2. LDRB W1, [SP, #4]! pre-index (0x38004FE1u) -> SP becomes 0x504

    memory.write_u8(0x504, 0x88u);

    memory.write_u32(4, 0x38404FE1u);

    cpu.step(memory);

    CHECK(cpu.read_register(1) == 0x88u);

    CHECK(cpu.get_nzcv() == 0b1101);

    CHECK(cpu.get_stack_pointer() == 0x504);



    // 3. LDRH W2, [SP], #-4 post-index (0x785FC7E2u) -> SP returns to 0x500

    memory.write_u16(0x504, 0x1234u);

    memory.write_u32(8, 0x785FC7E2u);

    cpu.step(memory);

    CHECK(cpu.read_register(2) == 0x1234u);

    CHECK(cpu.get_nzcv() == 0b1101);

    CHECK(cpu.get_stack_pointer() == 0x500);



    // 4. LDPSW X3, X4, [SP, #0] (0x694003E3u)

    memory.write_u32(0x500, 0x11223344u);

    memory.write_u32(0x504, 0x55667788u);

    memory.write_u32(12, 0x694013E3u); // Rt2=4, Rt1=3, Rn=31(SP)

    cpu.step(memory);

    CHECK(cpu.read_register(3) == 0x11223344ull);

    CHECK(cpu.read_register(4) == 0x55667788ull);

    CHECK(cpu.get_nzcv() == 0b1101);

    CHECK(cpu.get_stack_pointer() == 0x500);

}



void phase31_run_one(CPU& cpu, Memory& memory, std::uint32_t word) {

    const std::size_t offset =

        static_cast<std::size_t>(cpu.get_program_counter());

    memory.write_u32(offset, word);

    cpu.step(memory);

}



void phase31_exclusive_loads_all_widths() {

    CPU cpu;

    Memory memory(0x200);

    cpu.set_nzcv(0b1010);

    cpu.write_register(1, 0x100);

    memory.write_u8(0x100, 0xAB);

    memory.write_u16(0x102, 0xCDEF);

    memory.write_u32(0x104, 0x12345678);

    memory.write_u64(0x108, 0x0123456789ABCDEFull);



    phase31_run_one(cpu, memory, 0x085F7C20u); // LDXRB W0, [X1]

    CHECK(cpu.read_register(0) == 0xAB);

    CHECK(cpu.exclusive_monitor_valid());

    CHECK(cpu.exclusive_monitor_address() == 0x100);

    CHECK(cpu.exclusive_monitor_size() == 1);

    CHECK(cpu.exclusive_monitor_low_value() == 0xAB);

    CHECK(cpu.exclusive_monitor_high_value() == 0);

    CHECK(cpu.get_program_counter() == 4);



    cpu.write_register(1, 0x102);

    phase31_run_one(cpu, memory, 0x485F7C20u); // LDXRH W0, [X1]

    CHECK(cpu.read_register(0) == 0xCDEF);

    CHECK(cpu.exclusive_monitor_address() == 0x102);

    CHECK(cpu.exclusive_monitor_size() == 2);

    CHECK(cpu.exclusive_monitor_low_value() == 0xCDEF);



    cpu.write_register(1, 0x104);

    phase31_run_one(cpu, memory, 0x885F7C20u); // LDXR W0, [X1]

    CHECK(cpu.read_register(0) == 0x12345678);

    CHECK(cpu.exclusive_monitor_size() == 4);



    cpu.write_register(1, 0x108);

    phase31_run_one(cpu, memory, 0xC85F7C20u); // LDXR X0, [X1]

    CHECK(cpu.read_register(0) == 0x0123456789ABCDEFull);

    CHECK(cpu.exclusive_monitor_size() == 8);

    CHECK(cpu.exclusive_monitor_low_value() == 0x0123456789ABCDEFull);

    CHECK(cpu.get_nzcv() == 0b1010);

    CHECK(cpu.get_program_counter() == 16);

}



void phase31_exclusive_load_acquire_variants() {

    CPU cpu;

    Memory memory(0x200);

    cpu.set_nzcv(0b0011);

    memory.write_u8(0x100, 0x5A);

    memory.write_u16(0x102, 0x1234);

    memory.write_u32(0x104, 0xDEADBEEF);

    memory.write_u64(0x108, 0x0F1E2D3C4B5A6978ull);



    cpu.write_register(1, 0x100);

    phase31_run_one(cpu, memory, 0x085FFC20u); // LDAXRB W0, [X1]

    CHECK(cpu.read_register(0) == 0x5A);

    CHECK(cpu.exclusive_monitor_size() == 1);



    cpu.write_register(1, 0x102);

    phase31_run_one(cpu, memory, 0x485FFC20u); // LDAXRH W0, [X1]

    CHECK(cpu.read_register(0) == 0x1234);

    CHECK(cpu.exclusive_monitor_size() == 2);



    cpu.write_register(1, 0x104);

    phase31_run_one(cpu, memory, 0x885FFC20u); // LDAXR W0, [X1]

    CHECK(cpu.read_register(0) == 0xDEADBEEFu);



    cpu.write_register(1, 0x108);

    phase31_run_one(cpu, memory, 0xC85FFC20u); // LDAXR X0, [X1]

    CHECK(cpu.read_register(0) == 0x0F1E2D3C4B5A6978ull);

    CHECK(cpu.get_nzcv() == 0b0011);

    CHECK(cpu.get_program_counter() == 16);

}



void phase31_exclusive_store_success_and_failure() {

    CPU cpu;

    Memory memory(0x200);

    cpu.set_nzcv(0b1100);

    cpu.write_register(1, 0x100);

    memory.write_u64(0x100, 0x1111222233334444ull);



    phase31_run_one(cpu, memory, 0xC85F7C20u); // LDXR X0, [X1]

    CHECK(cpu.read_register(0) == 0x1111222233334444ull);

    cpu.write_register(0, 0xAABBCCDDEEFF0011ull);

    phase31_run_one(cpu, memory, 0xC8027C20u); // STXR W2, X0, [X1]

    CHECK(memory.read_u64(0x100) == 0xAABBCCDDEEFF0011ull);

    CHECK(cpu.read_register(2) == 0);

    CHECK(!cpu.exclusive_monitor_valid());



    phase31_run_one(cpu, memory, 0xC8027C20u); // STXR without a reservation

    CHECK(cpu.read_register(2) == 1);

    CHECK(memory.read_u64(0x100) == 0xAABBCCDDEEFF0011ull);

    CHECK(cpu.get_nzcv() == 0b1100);

    CHECK(cpu.get_program_counter() == 12);



    // Store-release variant succeeds on a fresh reservation.

    phase31_run_one(cpu, memory, 0xC85F7C20u); // LDXR X0, [X1]

    cpu.write_register(0, 0x0102030405060708ull);

    phase31_run_one(cpu, memory, 0xC802FC20u); // STLXR W2, X0, [X1]

    CHECK(memory.read_u64(0x100) == 0x0102030405060708ull);

    CHECK(cpu.read_register(2) == 0);

    CHECK(!cpu.exclusive_monitor_valid());

}



void phase31_exclusive_store_address_and_size_mismatch() {

    CPU cpu;

    Memory memory(0x200);

    cpu.write_register(1, 0x100);

    memory.write_u64(0x100, 0x1111111111111111ull);

    memory.write_u64(0x108, 0x2222222222222222ull);



    phase31_run_one(cpu, memory, 0xC85F7C20u); // LDXR X0, [X1] at 0x100

    cpu.write_register(0, 0x9999999999999999ull);

    cpu.write_register(1, 0x108);

    phase31_run_one(cpu, memory, 0xC8027C20u); // STXR W2, X0, [X1] at 0x108

    CHECK(cpu.read_register(2) == 1);

    CHECK(memory.read_u64(0x108) == 0x2222222222222222ull);

    CHECK(!cpu.exclusive_monitor_valid());



    cpu.write_register(1, 0x100);

    phase31_run_one(cpu, memory, 0xC85F7C20u); // 8-byte reservation

    cpu.write_register(0, 0x7777777777777777ull);

    phase31_run_one(cpu, memory, 0x88027C20u); // STXR W2, W0, [X1] (4-byte)

    CHECK(cpu.read_register(2) == 1);

    CHECK(memory.read_u64(0x100) == 0x1111111111111111ull);

    CHECK(!cpu.exclusive_monitor_valid());

}



void phase31_exclusive_monitor_invalidation_by_normal_stores() {

    CPU cpu;

    Memory memory(0x200);

    cpu.write_register(1, 0x100);

    cpu.write_register(3, 0x108);

    cpu.write_register(0, 0x42);



    phase31_run_one(cpu, memory, 0xC85F7C20u); // LDXR X0, [X1]

    phase31_run_one(cpu, memory, 0xF9000060u); // STR X0, [X3] at 0x108

    CHECK(cpu.exclusive_monitor_valid());

    CHECK(cpu.exclusive_monitor_address() == 0x100);



    cpu.write_register(3, 0x107);

    phase31_run_one(cpu, memory, 0x39000060u); // STRB W0, [X3] overlaps last byte

    CHECK(!cpu.exclusive_monitor_valid());



    phase31_run_one(cpu, memory, 0xC85F7C20u); // new reservation [0x100,0x108)

    cpu.write_register(3, 0x0FF);

    phase31_run_one(cpu, memory, 0x39000060u); // STRB W0, [X3] disjoint

    CHECK(cpu.exclusive_monitor_valid());



    cpu.write_register(3, 0x100);

    phase31_run_one(cpu, memory, 0xF9000060u); // STR X0, [X3] overlapping

    CHECK(!cpu.exclusive_monitor_valid());



    // A 16-byte pair reservation is invalidated by a store to its last byte

    // and survives a store that starts exactly at its end.

    cpu.write_register(2, 0x140);

    phase31_run_one(cpu, memory, 0xC87F0440u); // LDXP X0, X1, [X2]

    cpu.write_register(3, 0x14F);

    phase31_run_one(cpu, memory, 0x39000060u); // STRB W0, [X3]

    CHECK(!cpu.exclusive_monitor_valid());



    phase31_run_one(cpu, memory, 0xC87F0440u); // LDXP again

    cpu.write_register(3, 0x150);

    phase31_run_one(cpu, memory, 0x39000060u); // STRB W0, [X3] at range end

    CHECK(cpu.exclusive_monitor_valid());

}



void phase31_exclusive_monitor_replacement_and_reset() {

    CPU cpu;

    Memory memory(0x200);

    cpu.write_register(1, 0x100);

    memory.write_u64(0x100, 0xAAAAAAAABBBBBBBBull);

    memory.write_u64(0x108, 0xCCCCCCCCDDDDDDDDull);



    phase31_run_one(cpu, memory, 0xC85F7C20u); // LDXR X0, [X1]

    CHECK(cpu.exclusive_monitor_address() == 0x100);

    cpu.write_register(1, 0x108);

    phase31_run_one(cpu, memory, 0xC85F7C20u); // LDXR X0, [X1]

    CHECK(cpu.exclusive_monitor_valid());

    CHECK(cpu.exclusive_monitor_address() == 0x108);

    CHECK(cpu.exclusive_monitor_low_value() == 0xCCCCCCCCDDDDDDDDull);



    cpu.clear_exclusive_monitor();

    CHECK(!cpu.exclusive_monitor_valid());



    cpu.write_register(1, 0x100);

    phase31_run_one(cpu, memory, 0xC85F7C20u);

    CHECK(cpu.exclusive_monitor_valid());

    cpu.reset();

    CHECK(!cpu.exclusive_monitor_valid());

    CHECK(cpu.get_program_counter() == 0);

}



void phase31_exclusive_pair_loads() {

    CPU cpu;

    Memory memory(0x200);

    cpu.write_register(2, 0x100);

    memory.write_u64(0x100, 0x1111111111111111ull);

    memory.write_u64(0x108, 0x2222222222222222ull);



    phase31_run_one(cpu, memory, 0xC87F0440u); // LDXP X0, X1, [X2]

    CHECK(cpu.read_register(0) == 0x1111111111111111ull);

    CHECK(cpu.read_register(1) == 0x2222222222222222ull);

    CHECK(cpu.exclusive_monitor_address() == 0x100);

    CHECK(cpu.exclusive_monitor_size() == 16);

    CHECK(cpu.exclusive_monitor_low_value() == 0x1111111111111111ull);

    CHECK(cpu.exclusive_monitor_high_value() == 0x2222222222222222ull);



    cpu.write_register(2, 0x120);

    memory.write_u32(0x120, 0xAAAAAAAAu);

    memory.write_u32(0x124, 0xBBBBBBBBu);

    phase31_run_one(cpu, memory, 0x887F0440u); // LDXP W0, W1, [X2]

    CHECK(cpu.read_register(0) == 0xAAAAAAAAu);

    CHECK(cpu.read_register(1) == 0xBBBBBBBBu);

    CHECK(cpu.exclusive_monitor_size() == 8);

    CHECK(cpu.exclusive_monitor_low_value() == 0xAAAAAAAAull);

    CHECK(cpu.exclusive_monitor_high_value() == 0xBBBBBBBBull);



    cpu.write_register(2, 0x140);

    memory.write_u64(0x140, 0x0123456789ABCDEFull);

    memory.write_u64(0x148, 0xFEDCBA9876543210ull);

    phase31_run_one(cpu, memory, 0xC87F8440u); // LDAXP X0, X1, [X2]

    CHECK(cpu.read_register(0) == 0x0123456789ABCDEFull);

    CHECK(cpu.read_register(1) == 0xFEDCBA9876543210ull);

    CHECK(cpu.exclusive_monitor_size() == 16);

}



void phase31_exclusive_pair_stores_and_failure_atomicity() {

    CPU cpu;

    Memory memory(0x200);

    cpu.write_register(2, 0x100);

    memory.write_u64(0x100, 0x0102030405060708ull);

    memory.write_u64(0x108, 0x1112131415161718ull);



    phase31_run_one(cpu, memory, 0xC87F0440u); // LDXP X0, X1, [X2]

    cpu.write_register(0, 0xAAAAAAAAAAAAAAAAull);

    cpu.write_register(1, 0xBBBBBBBBBBBBBBBBull);

    phase31_run_one(cpu, memory, 0xC8230440u); // STXP W3, X0, X1, [X2]

    CHECK(memory.read_u64(0x100) == 0xAAAAAAAAAAAAAAAAull);

    CHECK(memory.read_u64(0x108) == 0xBBBBBBBBBBBBBBBBull);

    CHECK(cpu.read_register(3) == 0);

    CHECK(!cpu.exclusive_monitor_valid());



    // External modification of the second element makes the pair store fail

    // without writing either element.

    phase31_run_one(cpu, memory, 0xC87F0440u); // LDXP captures both elements

    cpu.write_register(0, 0xCCCCCCCCCCCCCCCCull);

    cpu.write_register(1, 0xDDDDDDDDDDDDDDDDull);

    memory.write_u64(0x108, 0x9999999999999999ull);

    phase31_run_one(cpu, memory, 0xC8230440u); // STXP

    CHECK(cpu.read_register(3) == 1);

    CHECK(memory.read_u64(0x100) == 0xAAAAAAAAAAAAAAAAull);

    CHECK(memory.read_u64(0x108) == 0x9999999999999999ull);

    CHECK(!cpu.exclusive_monitor_valid());



    // Pair size mismatch: a 16-byte reservation cannot be consumed by STXP W.

    phase31_run_one(cpu, memory, 0xC87F0440u); // LDXP X

    cpu.write_register(0, 1);

    cpu.write_register(1, 2);

    phase31_run_one(cpu, memory, 0x88230440u); // STXP W3, W0, W1, [X2]

    CHECK(cpu.read_register(3) == 1);

    CHECK(memory.read_u64(0x100) == 0xAAAAAAAAAAAAAAAAull);



    // A 32-bit pair reservation is 8 bytes and stores two words.

    cpu.write_register(2, 0x120);

    memory.write_u32(0x120, 0x11111111u);

    memory.write_u32(0x124, 0x22222222u);

    phase31_run_one(cpu, memory, 0x887F0440u); // LDXP W0, W1, [X2]

    cpu.write_register(0, 0x33333333u);

    cpu.write_register(1, 0x44444444u);

    phase31_run_one(cpu, memory, 0x88230440u); // STXP W3, W0, W1, [X2]

    CHECK(memory.read_u32(0x120) == 0x33333333u);

    CHECK(memory.read_u32(0x124) == 0x44444444u);

    CHECK(cpu.read_register(3) == 0);

}



void phase31_exclusive_fault_atomicity() {

    CPU cpu;

    Memory memory(0x10C);

    cpu.write_register(1, 0x100);

    memory.write_u64(0x100, 0x1122334455667788ull);

    phase31_run_one(cpu, memory, 0xC85F7C20u); // valid LDXR

    CHECK(cpu.exclusive_monitor_valid());



    cpu.set_nzcv(0b0101);

    cpu.write_register(1, 0x108); // 8 bytes at 0x108 exceed the 0x10C-byte RAM

    CHECK_OUT_OF_RANGE(phase31_run_one(cpu, memory, 0xC85F7C20u));

    CHECK(cpu.get_program_counter() == 4);

    CHECK(cpu.exclusive_monitor_valid());

    CHECK(cpu.exclusive_monitor_address() == 0x100);

    CHECK(cpu.exclusive_monitor_low_value() == 0x1122334455667788ull);

    CHECK(cpu.get_nzcv() == 0b0101);



    cpu.write_register(2, 0xCAFE);

    CHECK_OUT_OF_RANGE(phase31_run_one(cpu, memory, 0xC8027C20u)); // STXR fault

    CHECK(cpu.get_program_counter() == 4);

    CHECK(cpu.read_register(2) == 0xCAFE);

    CHECK(cpu.exclusive_monitor_valid());

    CHECK(cpu.exclusive_monitor_address() == 0x100);



    CPU pair_cpu;

    Memory pair_memory(0x114);

    pair_cpu.write_register(2, 0x100);

    pair_memory.write_u64(0x100, 0xAAAAAAAABBBBBBBBull);

    pair_memory.write_u64(0x108, 0xCCCCCCCCDDDDDDDDull);

    phase31_run_one(pair_cpu, pair_memory, 0xC87F0440u); // LDXP X0, X1, [X2]

    const std::uint64_t saved0 = pair_cpu.read_register(0);

    const std::uint64_t saved1 = pair_cpu.read_register(1);

    pair_cpu.write_register(2, 0x108);

    CHECK_OUT_OF_RANGE(phase31_run_one(pair_cpu, pair_memory, 0xC87F0440u));

    CHECK(pair_cpu.get_program_counter() == 4);

    CHECK(pair_cpu.read_register(0) == saved0);

    CHECK(pair_cpu.read_register(1) == saved1);

    CHECK(pair_cpu.exclusive_monitor_valid());

    CHECK(pair_cpu.exclusive_monitor_address() == 0x100);

}



void phase31_exclusive_zero_register_sp_and_overlap() {

    CPU cpu;

    Memory memory(0x200);

    cpu.write_register(1, 0x100);

    memory.write_u64(0x100, 0x1111222233334444ull);

    cpu.write_register(0, 0x9999);



    phase31_run_one(cpu, memory, 0xC85F7C3Fu); // LDXR XZR, [X1]

    CHECK(cpu.read_register(0) == 0x9999);

    CHECK(cpu.exclusive_monitor_valid());



    phase31_run_one(cpu, memory, 0xC8027C3Fu); // STXR W2, XZR, [X1]

    CHECK(memory.read_u64(0x100) == 0);

    CHECK(cpu.read_register(2) == 0);



    cpu.set_stack_pointer(0x110);

    memory.write_u64(0x110, 0x0102030405060708ull);

    phase31_run_one(cpu, memory, 0xC85F7FE0u); // LDXR X0, [SP]

    CHECK(cpu.read_register(0) == 0x0102030405060708ull);

    CHECK(cpu.exclusive_monitor_address() == 0x110);



    cpu.write_register(0, 0x5555);

    cpu.write_register(1, 0x110);

    phase31_run_one(cpu, memory, 0xC81F7C20u); // STXR WZR, X0, [X1]

    CHECK(memory.read_u64(0x110) == 0x5555);



    cpu.write_register(1, 0x120);

    memory.write_u64(0x120, 0xAAAAAAAAAAAAAAAAull);

    phase31_run_one(cpu, memory, 0xC85F7C20u); // LDXR X0, [X1]

    cpu.write_register(2, 0xDEAD);

    phase31_run_one(cpu, memory, 0xC8027C22u); // STXR W2, X2, [X1]

    CHECK(memory.read_u64(0x120) == 0xDEAD);

    CHECK(cpu.read_register(2) == 0);



    cpu.write_register(1, 0x140);

    memory.write_u64(0x140, 0x123456789ABCDEF0ull);

    phase31_run_one(cpu, memory, 0xC85F7C21u); // LDXR X1, [X1]

    CHECK(cpu.read_register(1) == 0x123456789ABCDEF0ull);

    CHECK(cpu.exclusive_monitor_address() == 0x140);



    cpu.write_register(1, 0x148);

    memory.write_u64(0x148, 0x0F0F0F0F0F0F0F0Full);

    phase31_run_one(cpu, memory, 0xC85F7C20u); // LDXR X0, [X1]

    cpu.write_register(0, 0x42);

    phase31_run_one(cpu, memory, 0xC8017C20u); // STXR W1, X0, [X1]

    CHECK(memory.read_u64(0x148) == 0x42);

    CHECK(cpu.read_register(1) == 0);

}



void phase31_exclusive_unallocated_encodings() {

    CPU cpu;

    Memory memory(8);

    const std::uint32_t rejected[] = {

        0x08207C20u, // STXP with pair size field 0

        0x48207C20u, // STXP with pair size field 1

        0xC8407C20u, // LDXR with op2 (Rs) != 11111

        0xC85F0020u, // LDXR with Rt2 != 11111

        0xC8020020u, // STXR with Rt2 != 11111

        0xC89F7C20u, // STLR (not implemented)

        0xC8DF7C20u, // LDAR (not implemented)

        0xC8A00020u, // CAS (out of scope)

        0xC8E00020u, // CASP (out of scope)

    };

    for (const std::uint32_t word : rejected) {

        CHECK_RUNTIME_ERROR(phase31_run_one(cpu, memory, word));

        CHECK(cpu.get_program_counter() == 0);

    }

}



void phase31_lse_add_all_widths() {

    CPU cpu;

    Memory memory(0x200);

    cpu.set_nzcv(0b1001);

    cpu.write_register(3, 1);



    cpu.write_register(5, 0x100);

    memory.write_u8(0x100, 0xFF);

    phase31_run_one(cpu, memory, 0x382300A4u); // LDADD B

    CHECK(memory.read_u8(0x100) == 0);

    CHECK(cpu.read_register(4) == 0xFF);



    cpu.write_register(5, 0x102);

    memory.write_u16(0x102, 0xFFFF);

    phase31_run_one(cpu, memory, 0x782300A4u); // LDADD H

    CHECK(memory.read_u16(0x102) == 0);

    CHECK(cpu.read_register(4) == 0xFFFF);



    cpu.write_register(5, 0x104);

    memory.write_u32(0x104, 0xFFFFFFFFu);

    phase31_run_one(cpu, memory, 0xB82300A4u); // LDADD W

    CHECK(memory.read_u32(0x104) == 0);

    CHECK(cpu.read_register(4) == 0xFFFFFFFFu);



    cpu.write_register(5, 0x108);

    memory.write_u64(0x108, 0xFFFFFFFFFFFFFFFFull);

    phase31_run_one(cpu, memory, 0xF82300A4u); // LDADD X

    CHECK(memory.read_u64(0x108) == 0);

    CHECK(cpu.read_register(4) == 0xFFFFFFFFFFFFFFFFull);

    CHECK(cpu.get_nzcv() == 0b1001);

    CHECK(cpu.get_program_counter() == 16);

}



void phase31_lse_bitwise_operations() {

    CPU cpu;

    Memory memory(0x200);

    cpu.write_register(5, 0x100);

    cpu.write_register(3, 0x0FF00FF0u);

    memory.write_u32(0x100, 0xF0F0F0F0u);

    phase31_run_one(cpu, memory, 0xB82310A4u); // LDCLR W

    CHECK(memory.read_u32(0x100) == 0xF000F000u);

    CHECK(cpu.read_register(4) == 0xF0F0F0F0u);



    cpu.write_register(5, 0x104);

    cpu.write_register(3, 0xFFFF0000u);

    memory.write_u32(0x104, 0xAAAAAAAAu);

    phase31_run_one(cpu, memory, 0xB82320A4u); // LDEOR W

    CHECK(memory.read_u32(0x104) == 0x5555AAAAu);

    CHECK(cpu.read_register(4) == 0xAAAAAAAAu);



    cpu.write_register(5, 0x108);

    cpu.write_register(3, 0x0F0F0F0Fu);

    memory.write_u32(0x108, 0x00FF00FFu);

    phase31_run_one(cpu, memory, 0xB82330A4u); // LDSET W

    CHECK(memory.read_u32(0x108) == 0x0FFF0FFFu);

    CHECK(cpu.read_register(4) == 0x00FF00FFu);



    cpu.write_register(5, 0x110);

    cpu.write_register(3, 0xFFFFFFFF00000000ull);

    memory.write_u64(0x110, 0x00000000FFFFFFFFull);

    phase31_run_one(cpu, memory, 0xF82330A4u); // LDSET X

    CHECK(memory.read_u64(0x110) == 0xFFFFFFFFFFFFFFFFull);

    CHECK(cpu.read_register(4) == 0x00000000FFFFFFFFull);



    cpu.write_register(5, 0x120);

    cpu.write_register(3, 0x0Fu);

    memory.write_u8(0x120, 0xFF);

    phase31_run_one(cpu, memory, 0x382310A4u); // LDCLR B

    CHECK(memory.read_u8(0x120) == 0xF0);

    CHECK(cpu.read_register(4) == 0xFF);

}



void phase31_lse_signed_and_unsigned_minmax() {

    CPU cpu;

    Memory memory(0x200);

    cpu.write_register(5, 0x100);



    cpu.write_register(3, 0x7F);

    memory.write_u8(0x100, 0x80);

    phase31_run_one(cpu, memory, 0x382340A4u); // LDSMAXB

    CHECK(memory.read_u8(0x100) == 0x7F);

    CHECK(cpu.read_register(4) == 0x80);



    cpu.write_register(5, 0x101);

    cpu.write_register(3, 0x01);

    memory.write_u8(0x101, 0x80);

    phase31_run_one(cpu, memory, 0x382350A4u); // LDSMINB

    CHECK(memory.read_u8(0x101) == 0x80);



    cpu.write_register(5, 0x102);

    cpu.write_register(3, 0x7F);

    memory.write_u8(0x102, 0x80);

    phase31_run_one(cpu, memory, 0x382360A4u); // LDUMAXB

    CHECK(memory.read_u8(0x102) == 0x80);



    cpu.write_register(5, 0x103);

    cpu.write_register(3, 0x7F);

    memory.write_u8(0x103, 0x80);

    phase31_run_one(cpu, memory, 0x382370A4u); // LDUMINB

    CHECK(memory.read_u8(0x103) == 0x7F);



    cpu.write_register(5, 0x104);

    cpu.write_register(3, 1);

    memory.write_u32(0x104, 0xFFFFFFFFu);

    phase31_run_one(cpu, memory, 0xB82340A4u); // LDSMAX W

    CHECK(memory.read_u32(0x104) == 1);

    CHECK(cpu.read_register(4) == 0xFFFFFFFFu);



    cpu.write_register(5, 0x108);

    cpu.write_register(3, 1);

    memory.write_u32(0x108, 0xFFFFFFFFu);

    phase31_run_one(cpu, memory, 0xB82360A4u); // LDUMAX W

    CHECK(memory.read_u32(0x108) == 0xFFFFFFFFu);



    cpu.write_register(5, 0x110);

    cpu.write_register(3, 0x7FFFFFFFFFFFFFFFull);

    memory.write_u64(0x110, 0x8000000000000000ull);

    phase31_run_one(cpu, memory, 0xF82350A4u); // LDSMIN X

    CHECK(memory.read_u64(0x110) == 0x8000000000000000ull);

    CHECK(cpu.read_register(4) == 0x8000000000000000ull);



    cpu.write_register(5, 0x118);

    cpu.write_register(3, 2);

    memory.write_u64(0x118, 1);

    phase31_run_one(cpu, memory, 0xF82370A4u); // LDUMIN X

    CHECK(memory.read_u64(0x118) == 1);



    cpu.write_register(5, 0x120);

    cpu.write_register(3, 0x7FFF);

    memory.write_u16(0x120, 0x8000);

    phase31_run_one(cpu, memory, 0x782370A4u); // LDUMINH

    CHECK(memory.read_u16(0x120) == 0x7FFF);

}



void phase31_lse_swap() {

    CPU cpu;

    Memory memory(0x200);

    cpu.write_register(5, 0x100);



    cpu.write_register(3, 0x55);

    memory.write_u8(0x100, 0xAA);

    phase31_run_one(cpu, memory, 0x382380A4u); // SWP B

    CHECK(memory.read_u8(0x100) == 0x55);

    CHECK(cpu.read_register(4) == 0xAA);



    cpu.write_register(5, 0x102);

    cpu.write_register(3, 0x5678);

    memory.write_u16(0x102, 0x1234);

    phase31_run_one(cpu, memory, 0x782380A4u); // SWP H

    CHECK(memory.read_u16(0x102) == 0x5678);

    CHECK(cpu.read_register(4) == 0x1234);



    cpu.write_register(5, 0x104);

    cpu.write_register(3, 0x22222222u);

    memory.write_u32(0x104, 0x11111111u);

    phase31_run_one(cpu, memory, 0xB82380A4u); // SWP W

    CHECK(memory.read_u32(0x104) == 0x22222222u);

    CHECK(cpu.read_register(4) == 0x11111111u);



    cpu.write_register(5, 0x108);

    cpu.write_register(3, 0xBBBBBBBBCCCCCCCCull);

    memory.write_u64(0x108, 0x1111111122222222ull);

    phase31_run_one(cpu, memory, 0xF82380A4u); // SWP X

    CHECK(memory.read_u64(0x108) == 0xBBBBBBBBCCCCCCCCull);

    CHECK(cpu.read_register(4) == 0x1111111122222222ull);

}



void phase31_lse_ordering_variants() {

    CPU cpu;

    Memory memory(0x200);

    cpu.set_nzcv(0b0110);

    cpu.write_register(3, 2);

    const std::uint32_t variants[] = {

        0xB82300A4u, // LDADD

        0xB8A300A4u, // LDADDA (acquire)

        0xB86300A4u, // LDADDL (release)

        0xB8E300A4u, // LDADDAL (acquire and release)

    };

    for (std::size_t i = 0; i < 4; ++i) {

        const std::size_t address = 0x100 + i * 4;

        cpu.write_register(5, static_cast<std::uint64_t>(address));

        memory.write_u32(address, 1);

        phase31_run_one(cpu, memory, variants[i]);

        CHECK(memory.read_u32(address) == 3);

        CHECK(cpu.read_register(4) == 1);

    }

    CHECK(cpu.get_nzcv() == 0b0110);

    CHECK(cpu.get_program_counter() == 16);

}



void phase31_lse_zero_register_sp_and_source_target_overlap() {

    CPU cpu;

    Memory memory(0x200);

    cpu.write_register(5, 0x100);

    cpu.write_register(4, 0x9999);

    memory.write_u32(0x100, 0x1234);

    phase31_run_one(cpu, memory, 0xB8BF00A4u); // LDADD WZR, W4, [X5]

    CHECK(memory.read_u32(0x100) == 0x1234);

    CHECK(cpu.read_register(4) == 0x1234);



    cpu.write_register(3, 3);

    phase31_run_one(cpu, memory, 0xB82300BFu); // LDADD W3, WZR, [X5]

    CHECK(memory.read_u32(0x100) == 0x1237);



    cpu.set_stack_pointer(0x108);

    cpu.write_register(3, 5);

    memory.write_u64(0x108, 0x10);

    phase31_run_one(cpu, memory, 0xF82303E4u); // LDADD X3, X4, [SP]

    CHECK(memory.read_u64(0x108) == 0x15);

    CHECK(cpu.read_register(4) == 0x10);



    cpu.write_register(5, 0x110);

    memory.write_u32(0x110, 5);

    cpu.write_register(3, 7);

    phase31_run_one(cpu, memory, 0xB82300A3u); // LDADD W3, W3, [X5]

    CHECK(memory.read_u32(0x110) == 12);

    CHECK(cpu.read_register(3) == 5);

}



void phase31_lse_faults_and_unallocated() {

    CPU cpu;

    Memory memory(0x100);

    cpu.write_register(5, 0x100);

    cpu.write_register(4, 0x55);

    CHECK_OUT_OF_RANGE(phase31_run_one(cpu, memory, 0xB82300A4u));

    CHECK(cpu.read_register(4) == 0x55);

    CHECK(cpu.get_program_counter() == 0);



    CPU partial_cpu;

    Memory partial_memory(0x104);

    partial_cpu.write_register(5, 0x102);

    partial_cpu.write_register(4, 0x77);

    partial_memory.write_u16(0x102, 0x1234);

    CHECK_OUT_OF_RANGE(phase31_run_one(partial_cpu, partial_memory, 0xB82300A4u));

    CHECK(partial_cpu.read_register(4) == 0x77);

    CHECK(partial_cpu.get_program_counter() == 0);

    CHECK(partial_memory.read_u16(0x102) == 0x1234);



    CPU bad_cpu;

    Memory bad_memory(0x100);

    const std::uint32_t rejected[] = {

        0xB82390A4u, // LSE opc 1001 (unallocated)

        0xB823F0A4u, // LSE opc 1111 (unallocated)

        0xB8BFC0A4u, // LDAPR (not implemented)

    };

    for (const std::uint32_t word : rejected) {

        CHECK_RUNTIME_ERROR(phase31_run_one(bad_cpu, bad_memory, word));

        CHECK(bad_cpu.get_program_counter() == 0);

    }

}



void phase31_lse_boundary_and_unaligned() {

    CPU cpu;

    Memory memory(0x104);

    cpu.write_register(5, 0x100);

    cpu.write_register(3, 1);

    memory.write_u32(0x100, 0x01020304u);

    phase31_run_one(cpu, memory, 0xB82300A4u); // LDADD W at the end of RAM

    CHECK(memory.read_u32(0x100) == 0x01020305u);

    CHECK(cpu.read_register(4) == 0x01020304u);



    CPU unaligned_cpu;

    Memory unaligned_memory(0x108);

    unaligned_cpu.write_register(5, 0x101);

    unaligned_cpu.write_register(3, 1);

    unaligned_memory.write_u32(0x101, 0x11223344u);

    phase31_run_one(unaligned_cpu, unaligned_memory, 0xB82300A4u);

    CHECK(unaligned_memory.read_u32(0x101) == 0x11223345u);

    CHECK(unaligned_cpu.read_register(4) == 0x11223344u);

    CHECK(unaligned_memory.read_u8(0x100) == 0);

    CHECK(unaligned_memory.read_u8(0x105) == 0);



    CPU exclusive_cpu;

    Memory exclusive_memory(0x110);

    exclusive_cpu.write_register(1, 0x101);

    exclusive_memory.write_u64(0x101, 0x1020304050607080ull);

    phase31_run_one(exclusive_cpu, exclusive_memory, 0xC85F7C20u); // LDXR X0

    CHECK(exclusive_cpu.read_register(0) == 0x1020304050607080ull);

    CHECK(exclusive_cpu.exclusive_monitor_address() == 0x101);

    CHECK(exclusive_cpu.exclusive_monitor_size() == 8);

    exclusive_cpu.write_register(0, 0xFFFFFFFFFFFFFFFFull);

    phase31_run_one(exclusive_cpu, exclusive_memory, 0xC8027C20u); // STXR W2, X0

    CHECK(exclusive_cpu.read_register(2) == 0);

    CHECK(exclusive_memory.read_u64(0x101) == 0xFFFFFFFFFFFFFFFFull);

    CHECK(exclusive_memory.read_u8(0x100) == 0);

    CHECK(exclusive_memory.read_u8(0x109) == 0);

}



void phase31_disassembly_names() {

    CPU cpu;

    Memory memory(0x400);

    std::vector<std::string> names;

    cpu.set_trace_sink([&](const CPU::TraceRecord& record) {

        names.push_back(record.disassembly);

    });

    cpu.write_register(1, 0x100);

    cpu.write_register(2, 0x200);

    cpu.write_register(5, 0x300);

    cpu.write_register(0, 0);



    phase31_run_one(cpu, memory, 0x085F7C20u); // LDXRB W0, [X1]

    phase31_run_one(cpu, memory, 0xC85FFC20u); // LDAXR X0, [X1]

    phase31_run_one(cpu, memory, 0xC8027C20u); // STXR W2, X0, [X1]

    phase31_run_one(cpu, memory, 0xC802FC20u); // STLXR W2, X0, [X1]

    cpu.write_register(2, 0x200);

    phase31_run_one(cpu, memory, 0xC87F0440u); // LDXP X0, X1, [X2]

    phase31_run_one(cpu, memory, 0xC8230440u); // STXP W3, X0, X1, [X2]

    phase31_run_one(cpu, memory, 0xB82300A4u); // LDADD W3, W4, [X5]

    phase31_run_one(cpu, memory, 0xF8E300A4u); // LDADDAL X3, X4, [X5]

    phase31_run_one(cpu, memory, 0xF82380A4u); // SWP X3, X4, [X5]

    phase31_run_one(cpu, memory, 0x382340A4u); // LDSMAXB W3, W4, [X5]



    const std::vector<std::string> expected = {

        "LDXRB W0, [X1]",

        "LDAXR X0, [X1]",

        "STXR W2, X0, [X1]",

        "STLXR W2, X0, [X1]",

        "LDXP X0, X1, [X2]",

        "STXP W3, X0, X1, [X2]",

        "LDADD W3, W4, [X5]",

        "LDADDAL X3, X4, [X5]",

        "SWP X3, X4, [X5]",

        "LDSMAXB W3, W4, [X5]",

    };

    CHECK(names.size() == expected.size());

    for (std::size_t i = 0; i < names.size() && i < expected.size(); ++i) {

        CHECK(names[i] == expected[i]);

    }

    CHECK(cpu.get_program_counter() == 40);

}



void phase32_register_state_and_upper_zeroing() {

    CPU cpu;

    CHECK(cpu.get_fpcr() == 0);

    CHECK(cpu.get_fpsr() == 0);

    cpu.set_fpcr(0x01000000u);

    cpu.set_fpsr(0x00000008u);

    CHECK(cpu.get_fpcr() == 0x01000000u);

    CHECK(cpu.get_fpsr() == 0x00000008u);

    cpu.reset();

    CHECK(cpu.get_fpcr() == 0);

    CHECK(cpu.get_fpsr() == 0);



    for (std::size_t i = 0; i < CPU::kVectorRegisterCount; ++i) {

        CHECK((cpu.read_vector_register(i) == CPU::VectorRegister{0, 0}));

    }



    // V31 is a valid architectural register

    cpu.write_vector_register(31, CPU::VectorRegister{0x1234, 0x5678});

    CHECK(cpu.read_vector_register(31).low == 0x1234);

    CHECK(cpu.read_vector_register(31).high == 0x5678);



    // Register 32 throws

    bool threw = false;

    try {

        cpu.read_vector_register(32);

    } catch (const std::out_of_range&) {

        threw = true;

    }

    CHECK(threw);



    // Scalar S write zeroes bits 127:32

    cpu.write_vector_register(0, CPU::VectorRegister{0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull});

    cpu.write_s_register(0, 0xAABBCCDDu);

    CHECK(cpu.read_s_register(0) == 0xAABBCCDDu);

    CHECK(cpu.read_vector_register(0).low == 0xAABBCCDDull);

    CHECK(cpu.read_vector_register(0).high == 0ull);



    // Scalar D write zeroes bits 127:64

    cpu.write_vector_register(1, CPU::VectorRegister{0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull});

    cpu.write_d_register(1, 0x1122334455667788ull);

    CHECK(cpu.read_d_register(1) == 0x1122334455667788ull);

    CHECK(cpu.read_vector_register(1).low == 0x1122334455667788ull);

    CHECK(cpu.read_vector_register(1).high == 0ull);



    // Scalar B write zeroes bits 127:8

    cpu.write_vector_register(2, CPU::VectorRegister{0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull});

    cpu.write_b_register(2, 0xEFu);

    CHECK(cpu.read_b_register(2) == 0xEFu);

    CHECK(cpu.read_vector_register(2).low == 0xEFull);

    CHECK(cpu.read_vector_register(2).high == 0ull);



    // Scalar H write zeroes bits 127:16

    cpu.write_vector_register(3, CPU::VectorRegister{0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull});

    cpu.write_h_register(3, 0xBEEFu);

    CHECK(cpu.read_h_register(3) == 0xBEEFu);

    CHECK(cpu.read_vector_register(3).low == 0xBEEFull);

    CHECK(cpu.read_vector_register(3).high == 0ull);

}



void phase32_fp_simd_loads_stores() {

    CPU cpu;

    Memory memory(0x1000);



    // 1. LDR / STR S (scaled immediate)

    cpu.write_s_register(0, 0x3F800000u); // 1.0f

    cpu.write_register(1, 0x100);

    phase31_run_one(cpu, memory, 0xBD000420u); // STR S0, [X1, #4]

    CHECK(memory.read_u32(0x104) == 0x3F800000u);

    phase31_run_one(cpu, memory, 0xBD400422u); // LDR S2, [X1, #4]

    CHECK(cpu.read_s_register(2) == 0x3F800000u);

    CHECK(cpu.read_vector_register(2).high == 0);



    // 2. LDR / STR D (pre/post-indexed)

    cpu.write_d_register(0, 0x4000000000000000ull); // 2.0

    cpu.write_register(1, 0x200);

    phase31_run_one(cpu, memory, 0xFC008C20u); // STR D0, [X1, #8]!

    CHECK(cpu.read_register(1) == 0x208);

    CHECK(memory.read_u64(0x208) == 0x4000000000000000ull);

    phase31_run_one(cpu, memory, 0xFC408423u); // LDR D3, [X1], #8

    CHECK(cpu.read_register(1) == 0x210);

    CHECK(cpu.read_d_register(3) == 0x4000000000000000ull);



    // 3. LDR / STR Q (16-byte vector transfer)

    cpu.write_vector_register(0, CPU::VectorRegister{0x0123456789ABCDEFull, 0xFEDCBA9876543210ull});

    cpu.write_register(1, 0x300);

    phase31_run_one(cpu, memory, 0x3D800420u); // STR Q0, [X1, #16]

    CHECK(memory.read_u64(0x310) == 0x0123456789ABCDEFull);

    CHECK(memory.read_u64(0x318) == 0xFEDCBA9876543210ull);

    phase31_run_one(cpu, memory, 0x3DC00424u); // LDR Q4, [X1, #16]

    CHECK(cpu.read_vector_register(4).low == 0x0123456789ABCDEFull);

    CHECK(cpu.read_vector_register(4).high == 0xFEDCBA9876543210ull);



    // 4. Register offset: LDR D5, [X1, X2, LSL #3]

    cpu.write_register(1, 0x400);

    cpu.write_register(2, 2); // offset 2 * 8 = 16 (0x410)

    memory.write_u64(0x410, 0xDEADBEEFCAFEull);

    phase31_run_one(cpu, memory, 0xFC627825u);

    CHECK(cpu.read_d_register(5) == 0xDEADBEEFCAFEull);



    // 5. Normal store invalidates exclusive monitor

    cpu.write_register(1, 0x500);

    phase31_run_one(cpu, memory, 0xC85F7C20u); // LDXR X0, [X1]

    CHECK(cpu.exclusive_monitor_valid());

    phase31_run_one(cpu, memory, 0xBD000020u); // STR S0, [X1]

    CHECK(!cpu.exclusive_monitor_valid());



    // 6. Target Rt = 31 (V31)

    cpu.write_vector_register(31, CPU::VectorRegister{0x7777, 0x8888});

    cpu.write_register(1, 0x600);

    phase31_run_one(cpu, memory, 0x3D80003Fu); // STR Q31, [X1]

    CHECK(memory.read_u64(0x600) == 0x7777);

    CHECK(memory.read_u64(0x608) == 0x8888);

}



void phase32_scalar_fp_arithmetic_and_movement() {

    CPU cpu;

    Memory memory(0x1000);



    // FMOV W0, S1 and FMOV S0, W1

    cpu.write_s_register(1, 0x40490FDBu); // ~3.14159f

    phase31_run_one(cpu, memory, 0x1E260020u);

    CHECK(cpu.read_register(0) == 0x40490FDBull);



    cpu.write_register(1, 0x3F800000ull); // 1.0f

    phase31_run_one(cpu, memory, 0x1E270020u);

    CHECK(cpu.read_s_register(0) == 0x3F800000u);

    CHECK(cpu.read_vector_register(0).high == 0);



    // FMOV X0, D1 and FMOV D0, X1

    cpu.write_d_register(1, 0x400921FB54442D18ull); // pi in double

    phase31_run_one(cpu, memory, 0x9E660020u);

    CHECK(cpu.read_register(0) == 0x400921FB54442D18ull);



    cpu.write_register(1, 0x4014000000000000ull); // 5.0

    phase31_run_one(cpu, memory, 0x9E670020u);

    CHECK(cpu.read_d_register(0) == 0x4014000000000000ull);



    // FMOV S0, #1.0 (imm8=0x70)

    phase31_run_one(cpu, memory, 0x1E2E1000u);

    CHECK(cpu.read_s_register(0) == 0x3F800000u);



    // FADD S0, S1, S2: 1.5f + 2.5f = 4.0f

    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(1.5f));

    cpu.write_s_register(2, std::bit_cast<std::uint32_t>(2.5f));

    phase31_run_one(cpu, memory, 0x1E222820u);

    CHECK(std::bit_cast<float>(cpu.read_s_register(0)) == 4.0f);



    // FSUB S0, S1, S2: 10.0f - 3.0f = 7.0f

    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(10.0f));

    cpu.write_s_register(2, std::bit_cast<std::uint32_t>(3.0f));

    phase31_run_one(cpu, memory, 0x1E223820u);

    CHECK(std::bit_cast<float>(cpu.read_s_register(0)) == 7.0f);



    // FMUL D0, D1, D2: 3.0 * 4.0 = 12.0

    cpu.write_d_register(1, std::bit_cast<std::uint64_t>(3.0));

    cpu.write_d_register(2, std::bit_cast<std::uint64_t>(4.0));

    phase31_run_one(cpu, memory, 0x1E620820u);

    CHECK(std::bit_cast<double>(cpu.read_d_register(0)) == 12.0);



    // FDIV D0, D1, D2: 15.0 / 3.0 = 5.0

    cpu.write_d_register(1, std::bit_cast<std::uint64_t>(15.0));

    cpu.write_d_register(2, std::bit_cast<std::uint64_t>(3.0));

    phase31_run_one(cpu, memory, 0x1E621820u);

    CHECK(std::bit_cast<double>(cpu.read_d_register(0)) == 5.0);



    // FABS D0, D1

    cpu.write_d_register(1, std::bit_cast<std::uint64_t>(-9.0));

    phase31_run_one(cpu, memory, 0x1E60C020u);

    CHECK(std::bit_cast<double>(cpu.read_d_register(0)) == 9.0);



    // FNEG D0, D1

    phase31_run_one(cpu, memory, 0x1E614020u);

    CHECK(std::bit_cast<double>(cpu.read_d_register(0)) == 9.0);



    // FSQRT D0, D1

    cpu.write_d_register(1, std::bit_cast<std::uint64_t>(16.0));

    phase31_run_one(cpu, memory, 0x1E61C020u);

    CHECK(std::bit_cast<double>(cpu.read_d_register(0)) == 4.0);

}



void phase32_scalar_fp_compare() {

    CPU cpu;

    Memory memory(0x1000);



    // Equal: 2.0f == 2.0f -> NZCV = 0x6

    cpu.write_s_register(0, std::bit_cast<std::uint32_t>(2.0f));

    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(2.0f));

    phase31_run_one(cpu, memory, 0x1E212000u);

    CHECK(cpu.get_nzcv() == 0x6);



    // Less: 1.0f < 2.0f -> NZCV = 0x8

    cpu.write_s_register(0, std::bit_cast<std::uint32_t>(1.0f));

    phase31_run_one(cpu, memory, 0x1E212000u);

    CHECK(cpu.get_nzcv() == 0x8);



    // Greater: 3.0f > 2.0f -> NZCV = 0x2

    cpu.write_s_register(0, std::bit_cast<std::uint32_t>(3.0f));

    phase31_run_one(cpu, memory, 0x1E212000u);

    CHECK(cpu.get_nzcv() == 0x2);



    // Unordered: NaN -> NZCV = 0x3

    cpu.write_s_register(0, 0x7FC00000u); // quiet NaN

    phase31_run_one(cpu, memory, 0x1E212000u);

    CHECK(cpu.get_nzcv() == 0x3);



    // FCMP D0, #0.0

    cpu.write_d_register(0, std::bit_cast<std::uint64_t>(0.0));

    phase31_run_one(cpu, memory, 0x1E602008u);

    CHECK(cpu.get_nzcv() == 0x6);

}



void phase32_fp_conversion_and_rounding() {

    CPU cpu;

    Memory memory(0x1000);



    // SCVTF S0, W1: -42 -> -42.0f

    cpu.write_register(1, static_cast<std::uint64_t>(static_cast<std::int32_t>(-42)));

    phase31_run_one(cpu, memory, 0x1E220020u);

    CHECK(std::bit_cast<float>(cpu.read_s_register(0)) == -42.0f);



    // UCVTF D0, X1: 1000 -> 1000.0

    cpu.write_register(1, 1000ull);

    phase31_run_one(cpu, memory, 0x9E630020u);

    CHECK(std::bit_cast<double>(cpu.read_d_register(0)) == 1000.0);



    // FCVTZS W0, S1: 3.7f -> 3

    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(3.7f));

    phase31_run_one(cpu, memory, 0x1E380020u);

    CHECK(cpu.read_register(0) == 3);



    // FCVTZS on NaN returns 0

    cpu.write_s_register(1, 0x7FC00000u);

    phase31_run_one(cpu, memory, 0x1E380020u);

    CHECK(cpu.read_register(0) == 0);



    // FCVTZU X0, D1: 5.9 -> 5

    cpu.write_d_register(1, std::bit_cast<std::uint64_t>(5.9));

    phase31_run_one(cpu, memory, 0x9E790020u);

    CHECK(cpu.read_register(0) == 5);



    // FCVT D0, S1: 1.5f -> 1.5 (double)

    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(1.5f));

    phase31_run_one(cpu, memory, 0x1E22C020u);

    CHECK(std::bit_cast<double>(cpu.read_d_register(0)) == 1.5);



    // FCVT S0, D1: 2.5 -> 2.5f

    cpu.write_d_register(1, std::bit_cast<std::uint64_t>(2.5));

    phase31_run_one(cpu, memory, 0x1E624020u);

    CHECK(std::bit_cast<float>(cpu.read_s_register(0)) == 2.5f);



    // FRINTZ S0, S1 (trunc): -2.7f -> -2.0f

    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(-2.7f));

    phase31_run_one(cpu, memory, 0x1E25C020u);

    CHECK(std::bit_cast<float>(cpu.read_s_register(0)) == -2.0f);



    // FRINTP S0, S1 (ceil): 2.1f -> 3.0f

    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(2.1f));

    phase31_run_one(cpu, memory, 0x1E24C020u);

    CHECK(std::bit_cast<float>(cpu.read_s_register(0)) == 3.0f);



    // FRINTM S0, S1 (floor): 2.9f -> 2.0f

    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(2.9f));

    phase31_run_one(cpu, memory, 0x1E254020u);

    CHECK(std::bit_cast<float>(cpu.read_s_register(0)) == 2.0f);



    // FRINTN S0, S1 (ties to even): 2.5f -> 2.0f, 3.5f -> 4.0f

    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(2.5f));

    phase31_run_one(cpu, memory, 0x1E244020u);

    CHECK(std::bit_cast<float>(cpu.read_s_register(0)) == 2.0f);

    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(3.5f));

    phase31_run_one(cpu, memory, 0x1E244020u);

    CHECK(std::bit_cast<float>(cpu.read_s_register(0)) == 4.0f);

}



void phase32_system_registers_fpcr_fpsr() {

    CPU cpu;

    Memory memory(0x1000);



    // MSR FPCR, X0 and MRS X1, FPCR

    cpu.write_register(0, 0x01000000ull);

    phase31_run_one(cpu, memory, 0xD51B4400u);

    CHECK(cpu.get_fpcr() == 0x01000000u);

    phase31_run_one(cpu, memory, 0xD53B4401u);

    CHECK(cpu.read_register(1) == 0x01000000ull);



    // MSR FPSR, X0 and MRS X2, FPSR

    cpu.write_register(0, 0x00000008ull);

    phase31_run_one(cpu, memory, 0xD51B4420u);

    CHECK(cpu.get_fpsr() == 0x00000008u);

    phase31_run_one(cpu, memory, 0xD53B4422u);

    CHECK(cpu.read_register(2) == 0x00000008ull);

}



void phase32_advanced_simd_operations() {

    CPU cpu;

    Memory memory(0x1000);



    // 1. Vector logical: AND V0.16B, V1.16B, V2.16B

    cpu.write_vector_register(1, CPU::VectorRegister{0x0F0F0F0F0F0F0F0Full, 0xFF00FF00FF00FF00ull});

    cpu.write_vector_register(2, CPU::VectorRegister{0x3333333333333333ull, 0xF0F0F0F0F0F0F0F0ull});

    phase31_run_one(cpu, memory, 0x4E221C20u);

    CHECK(cpu.read_vector_register(0).low == 0x0303030303030303ull);

    CHECK(cpu.read_vector_register(0).high == 0xF000F000F000F000ull);



    // 8B form zeroes upper 64 bits

    phase31_run_one(cpu, memory, 0x0E221C20u);

    CHECK(cpu.read_vector_register(0).low == 0x0303030303030303ull);

    CHECK(cpu.read_vector_register(0).high == 0ull);



    // NOT V0.16B, V1.16B

    phase31_run_one(cpu, memory, 0x6E205820u);

    CHECK(cpu.read_vector_register(0).low == ~0x0F0F0F0F0F0F0F0Full);

    CHECK(cpu.read_vector_register(0).high == ~0xFF00FF00FF00FF00ull);



    // 2. Vector ADD V0.4S, V1.4S, V2.4S

    cpu.write_vector_register(1, CPU::VectorRegister{0x0000000200000001ull, 0x0000000400000003ull});

    cpu.write_vector_register(2, CPU::VectorRegister{0x0000002000000010ull, 0x0000004000000030ull});

    phase31_run_one(cpu, memory, 0x4EA28420u);

    CHECK(cpu.read_vector_register(0).low == 0x0000002200000011ull);

    CHECK(cpu.read_vector_register(0).high == 0x0000004400000033ull);



    // Vector SUB V0.2D, V1.2D, V2.2D

    cpu.write_vector_register(1, CPU::VectorRegister{100ull, 200ull});

    cpu.write_vector_register(2, CPU::VectorRegister{30ull, 50ull});

    phase31_run_one(cpu, memory, 0x6EE28420u);

    CHECK(cpu.read_vector_register(0).low == 70ull);

    CHECK(cpu.read_vector_register(0).high == 150ull);



    // 3. DUP V0.4S, W1

    cpu.write_register(1, 0x12345678ull);

    phase31_run_one(cpu, memory, 0x4E040C20u);

    CHECK(cpu.read_vector_register(0).low == 0x1234567812345678ull);

    CHECK(cpu.read_vector_register(0).high == 0x1234567812345678ull);



    // INS V0.S[1], W2

    cpu.write_register(2, 0xDEADBEEFull);

    phase31_run_one(cpu, memory, 0x4E0C1C40u);

    CHECK(cpu.read_vector_register(0).low == 0xDEADBEEF12345678ull);

    CHECK(cpu.read_vector_register(0).high == 0x1234567812345678ull);



    // UMOV W0, V0.S[1]

    phase31_run_one(cpu, memory, 0x0E0C3C00u);

    CHECK(cpu.read_register(0) == 0xDEADBEEFull);

}



void phase32_disassembly_names() {

    CPU cpu;

    Memory memory(0x1000);

    std::vector<std::string> names;

    cpu.set_trace_sink([&names](const CPU::TraceRecord& r) {

        names.push_back(r.disassembly);

    });



    phase31_run_one(cpu, memory, 0xBD000420u); // STR S0, [X1, #4]

    phase31_run_one(cpu, memory, 0xBD400422u); // LDR S2, [X1, #4]

    phase31_run_one(cpu, memory, 0x1E222820u); // FADD S0, S1, S2

    phase31_run_one(cpu, memory, 0x1E212000u); // FCMP S0, S1

    phase31_run_one(cpu, memory, 0x1E220020u); // SCVTF S0, W1

    phase31_run_one(cpu, memory, 0xD51B4400u); // MSR FPCR, X0

    phase31_run_one(cpu, memory, 0x4E221C20u); // AND V0.16B, V1.16B, V2.16B

    phase31_run_one(cpu, memory, 0x4EA28420u); // ADD V0.4S, V1.4S, V2.4S

    phase31_run_one(cpu, memory, 0x4E040C20u); // DUP V0.4S, W1



    const std::vector<std::string> expected = {

        "STR S0, [X1, #4]",

        "LDR S2, [X1, #4]",

        "FADD S0, S1, S2",

        "FCMP S0, S1",

        "SCVTF S0, W1",

        "MSR FPCR, X0",

        "AND V0.16B, V1.16B, V2.16B",

        "ADD V0.4S, V1.4S, V2.4S",

        "DUP V0.4S, W1",

    };

    CHECK(names.size() == expected.size());

    for (std::size_t i = 0; i < names.size() && i < expected.size(); ++i) {

        CHECK(names[i] == expected[i]);

    }

}



void phase33_simd_integer_arithmetic_mul_sat_minmax_addp() {

    CPU cpu;

    Memory memory(0x1000);



    // 1. MUL V0.8B, V1.8B, V2.8B

    cpu.write_vector_register(1, CPU::VectorRegister{0x0102030405060708ull, 0});

    cpu.write_vector_register(2, CPU::VectorRegister{0x0202020202020202ull, 0});

    phase31_run_one(cpu, memory, 0x0E229C20u);

    CHECK(cpu.read_vector_register(0).low == 0x020406080A0C0E10ull);

    CHECK(cpu.read_vector_register(0).high == 0ull);



    // 2. MUL V0.4S, V1.4S, V2.4S

    cpu.write_vector_register(1, CPU::VectorRegister{0x0000000200000001ull, 0x0000000400000003ull});

    cpu.write_vector_register(2, CPU::VectorRegister{0x0000000A0000000Aull, 0x0000000A0000000Aull});

    phase31_run_one(cpu, memory, 0x4EA29C20u);

    CHECK(cpu.read_vector_register(0).low == 0x000000140000000Aull);

    CHECK(cpu.read_vector_register(0).high == 0x000000280000001Eull);



    // 3. SQADD V0.8B, V1.8B, V2.8B (saturation -> 127 = 0x7F and sets FPSR.QC)

    cpu.set_fpsr(0);

    cpu.write_vector_register(1, CPU::VectorRegister{0x70ull, 0});

    cpu.write_vector_register(2, CPU::VectorRegister{0x20ull, 0});

    phase31_run_one(cpu, memory, 0x0E220C20u);

    CHECK((cpu.read_vector_register(0).low & 0xFFu) == 0x7Fu);

    CHECK((cpu.get_fpsr() & 0x08000000u) != 0);



    // 4. UQADD V0.8B, V1.8B, V2.8B (saturation -> 255 = 0xFF)

    cpu.set_fpsr(0);

    cpu.write_vector_register(1, CPU::VectorRegister{0xF0ull, 0});

    cpu.write_vector_register(2, CPU::VectorRegister{0x20ull, 0});

    phase31_run_one(cpu, memory, 0x2E220C20u);

    CHECK((cpu.read_vector_register(0).low & 0xFFu) == 0xFFu);

    CHECK((cpu.get_fpsr() & 0x08000000u) != 0);



    // 5. UQSUB V0.8B, V1.8B, V2.8B (underflow -> 0)

    cpu.set_fpsr(0);

    cpu.write_vector_register(1, CPU::VectorRegister{5ull, 0});

    cpu.write_vector_register(2, CPU::VectorRegister{10ull, 0});

    phase31_run_one(cpu, memory, 0x2E222C20u);

    CHECK((cpu.read_vector_register(0).low & 0xFFu) == 0);

    CHECK((cpu.get_fpsr() & 0x08000000u) != 0);



    // 6. SMAX / UMAX / SMIN / UMIN

    cpu.write_vector_register(1, CPU::VectorRegister{0xFFull, 0}); // -1 signed, 255 unsigned

    cpu.write_vector_register(2, CPU::VectorRegister{0x01ull, 0}); // +1 signed, 1 unsigned

    phase31_run_one(cpu, memory, 0x0E226420u); // SMAX

    CHECK((cpu.read_vector_register(0).low & 0xFFu) == 0x01u);

    phase31_run_one(cpu, memory, 0x2E226420u); // UMAX

    CHECK((cpu.read_vector_register(0).low & 0xFFu) == 0xFFu);



    // 7. ADDP V0.8B, V1.8B, V2.8B

    cpu.write_vector_register(1, CPU::VectorRegister{0x0403020104030201ull, 0});

    cpu.write_vector_register(2, CPU::VectorRegister{0x0807060508070605ull, 0});

    phase31_run_one(cpu, memory, 0x0E22BC20u);

    CHECK(cpu.read_vector_register(0).low == 0x0F0B0F0B07030703ull);

    CHECK(cpu.read_vector_register(0).high == 0ull);

}



void phase33_simd_vector_comparisons() {

    CPU cpu;

    Memory memory(0x1000);



    // CMEQ reg 8B

    cpu.write_vector_register(1, CPU::VectorRegister{0x0102030405060708ull, 0});

    cpu.write_vector_register(2, CPU::VectorRegister{0x0100030005000700ull, 0});

    phase31_run_one(cpu, memory, 0x2E228C20u);

    CHECK(cpu.read_vector_register(0).low == 0xFF00FF00FF00FF00ull);



    // CMGT reg 8B

    cpu.write_vector_register(1, CPU::VectorRegister{0x01007F8005FE0001ull, 0});

    cpu.write_vector_register(2, CPU::VectorRegister{0x0001007FFE050000ull, 0});

    phase31_run_one(cpu, memory, 0x0E223420u);

    CHECK(cpu.read_vector_register(0).low == 0xFF00FF00FF0000FFull);



    // CMTST reg 4S

    cpu.write_vector_register(1, CPU::VectorRegister{0x1234567800000000ull, 0xF0F0F0F00F0F0F0Full});

    cpu.write_vector_register(2, CPU::VectorRegister{0x00000008FFFFFFFFull, 0x0F0F0F0F0F0F0F0Full});

    phase31_run_one(cpu, memory, 0x4EA28C20u);

    CHECK(cpu.read_vector_register(0).low == 0xFFFFFFFF00000000ull);

    CHECK(cpu.read_vector_register(0).high == 0x00000000FFFFFFFFull);



    // CMEQ zero 8B

    cpu.write_vector_register(1, CPU::VectorRegister{0x000100FF005500AAull, 0});

    phase31_run_one(cpu, memory, 0x0E209820u);

    CHECK(cpu.read_vector_register(0).low == 0xFF00FF00FF00FF00ull);



    // CMGT zero 4S

    cpu.write_vector_register(1, CPU::VectorRegister{0x000000007FFFFFFFull, 0x00000001FFFFFFFFull});

    phase31_run_one(cpu, memory, 0x4EA08820u);

    CHECK(cpu.read_vector_register(0).low == 0x00000000FFFFFFFFull);

    CHECK(cpu.read_vector_register(0).high == 0xFFFFFFFF00000000ull);

}



void phase33_simd_shifts() {

    CPU cpu;

    Memory memory(0x1000);



    // SSHL reg 8B

    cpu.write_vector_register(1, CPU::VectorRegister{0x01020408ull, 0});

    cpu.write_vector_register(2, CPU::VectorRegister{0x00010203ull, 0});

    phase31_run_one(cpu, memory, 0x0E224420u);

    CHECK((cpu.read_vector_register(0).low & 0xFFFFFFFFull) == 0x01041040ull);



    // SHL imm 8B (#3)

    cpu.write_vector_register(1, CPU::VectorRegister{0x0102030405060708ull, 0});

    phase31_run_one(cpu, memory, 0x0F0B5420u);

    CHECK(cpu.read_vector_register(0).low == 0x0810182028303840ull);



    // SSHR imm 4S (#4)

    cpu.write_vector_register(1, CPU::VectorRegister{0x8000000000000080ull, 0xF000000000000040ull});

    phase31_run_one(cpu, memory, 0x4F3C0420u);

    CHECK(cpu.read_vector_register(0).low == 0xF800000000000008ull);

    CHECK(cpu.read_vector_register(0).high == 0xFF00000000000004ull);



    // SRSHR imm 4H (#2)

    cpu.write_vector_register(1, CPU::VectorRegister{0x00070008ull, 0});

    phase31_run_one(cpu, memory, 0x0F1E2420u);

    CHECK((cpu.read_vector_register(0).low & 0xFFFFFFFFull) == 0x00020002ull);

}



void phase33_simd_widening_and_narrowing() {

    CPU cpu;

    Memory memory(0x1000);



    // SADDL 8B -> 8H

    cpu.write_vector_register(1, CPU::VectorRegister{0x7F102030ull, 0});

    cpu.write_vector_register(2, CPU::VectorRegister{0x01010101ull, 0});

    phase31_run_one(cpu, memory, 0x0E220020u);

    CHECK(cpu.read_vector_register(0).low == 0x0080001100210031ull);



    // XTN 8H -> 8B

    cpu.write_vector_register(1, CPU::VectorRegister{0x0404030302020101ull, 0x0808070706060505ull});

    cpu.write_vector_register(0, CPU::VectorRegister{~0ull, ~0ull});

    phase31_run_one(cpu, memory, 0x0E212820u);

    CHECK(cpu.read_vector_register(0).low == 0x0807060504030201ull);

    CHECK(cpu.read_vector_register(0).high == 0ull);



    // XTN2 8H -> 16B (upper half)

    cpu.write_vector_register(1, CPU::VectorRegister{0x1414131312121111ull, 0x1818171716161515ull});

    phase31_run_one(cpu, memory, 0x4E212820u);

    CHECK(cpu.read_vector_register(0).low == 0x0807060504030201ull);

    CHECK(cpu.read_vector_register(0).high == 0x1817161514131211ull);



    // SQXTN 4S -> 4H (saturation)

    cpu.set_fpsr(0);

    cpu.write_vector_register(1, CPU::VectorRegister{0x00001234FFFFEDCCull, 0x00010000FFFF0000ull});

    phase31_run_one(cpu, memory, 0x0E614820u);

    CHECK(cpu.read_vector_register(0).low == 0x7FFF80001234EDCCull);

    CHECK((cpu.get_fpsr() & 0x08000000u) != 0);

}



void phase33_simd_reductions_and_misc() {

    CPU cpu;

    Memory memory(0x1000);



    // ADDV 8B

    cpu.write_vector_register(1, CPU::VectorRegister{0x0102030405060708ull, 0});

    phase31_run_one(cpu, memory, 0x0E31B820u);

    CHECK(cpu.read_vector_register(0).low == 36ull);

    CHECK(cpu.read_vector_register(0).high == 0ull);



    // SMAXV 16B

    cpu.write_vector_register(1, CPU::VectorRegister{0x0001020304050607ull, 0x8081828384858687ull});

    phase31_run_one(cpu, memory, 0x4E30A820u);

    CHECK(cpu.read_vector_register(0).low == 7ull);

    CHECK(cpu.read_vector_register(0).high == 0ull);



    // SADDLV 8B -> 1H

    cpu.write_vector_register(1, CPU::VectorRegister{0x01010101ull, 0});

    phase31_run_one(cpu, memory, 0x0E303820u);

    CHECK(cpu.read_vector_register(0).low == 4ull);

    CHECK(cpu.read_vector_register(0).high == 0ull);



    // ABS 4S

    cpu.write_vector_register(1, CPU::VectorRegister{0x800000007FFFFFFFull, 0xFFFFFFFF00000005ull});

    phase31_run_one(cpu, memory, 0x4EA0B820u);

    CHECK(cpu.read_vector_register(0).low == 0x800000007FFFFFFFull);

    CHECK(cpu.read_vector_register(0).high == 0x0000000100000005ull);



    // NEG 8B

    cpu.write_vector_register(1, CPU::VectorRegister{0x01020304FEFDFCFBull, 0});

    phase31_run_one(cpu, memory, 0x2E20B820u);

    CHECK(cpu.read_vector_register(0).low == 0xFFFEFDFC02030405ull);

}



void phase33_disassembly_names() {

    CPU cpu;

    Memory memory(0x1000);

    std::vector<std::string> names;

    cpu.set_trace_sink([&names](const CPU::TraceRecord& r) {

        names.push_back(r.disassembly);

    });



    phase31_run_one(cpu, memory, 0x0E229C20u); // MUL V0.8B, V1.8B, V2.8B

    phase31_run_one(cpu, memory, 0x0E220C20u); // SQADD V0.8B, V1.8B, V2.8B

    phase31_run_one(cpu, memory, 0x0E22BC20u); // ADDP V0.8B, V1.8B, V2.8B

    phase31_run_one(cpu, memory, 0x2E228C20u); // CMEQ V0.8B, V1.8B, V2.8B

    phase31_run_one(cpu, memory, 0x0E209820u); // CMEQ V0.8B, V1.8B, #0

    phase31_run_one(cpu, memory, 0x0F0B5420u); // SHL V0.8B, V1.8B, #3

    phase31_run_one(cpu, memory, 0x0E220020u); // SADDL V0.8H, V1.8B, V2.8B

    phase31_run_one(cpu, memory, 0x0E212820u); // XTN V0.8B, V1.8H

    phase31_run_one(cpu, memory, 0x0E31B820u); // ADDV H0, V1.8B



    const std::vector<std::string> expected = {

        "MUL V0.8B, V1.8B, V2.8B",

        "SQADD V0.8B, V1.8B, V2.8B",

        "ADDP V0.8B, V1.8B, V2.8B",

        "CMEQ V0.8B, V1.8B, V2.8B",

        "CMEQ V0.8B, V1.8B, #0",

        "SHL V0.8B, V1.8B, #3",

        "SADDL V0.8H, V1.8B, V2.8B",

        "XTN V0.8B, V1.8H",

        "ADDV B0, V1.8B",

    };

    CHECK(names.size() == expected.size());

    for (std::size_t i = 0; i < names.size() && i < expected.size(); ++i) {

        CHECK(names[i] == expected[i]);

    }

}



void phase34_simd_fp_arithmetic() {

    CPU cpu;

    Memory memory(0x1000);



    // FADD V0.4S, V1.4S, V2.4S (0x4E22D420u)

    cpu.write_vector_register(1, CPU::VectorRegister{0x402000003FC00000ull, 0x4090000040600000ull});

    cpu.write_vector_register(2, CPU::VectorRegister{0x3F0000003F000000ull, 0x3F0000003F000000ull});

    phase31_run_one(cpu, memory, 0x4E22D420u);

    CHECK(cpu.read_vector_register(0).low == 0x4040000040000000ull);

    CHECK(cpu.read_vector_register(0).high == 0x40A0000040800000ull);



    // FSUB V0.2D, V1.2D, V2.2D (0x4EE2D420u)

    cpu.write_vector_register(1, CPU::VectorRegister{0x4024000000000000ull, 0x4034000000000000ull});

    cpu.write_vector_register(2, CPU::VectorRegister{0x4008000000000000ull, 0x4014000000000000ull});

    phase31_run_one(cpu, memory, 0x4EE2D420u);

    CHECK(cpu.read_vector_register(0).low == 0x401C000000000000ull);

    CHECK(cpu.read_vector_register(0).high == 0x402E000000000000ull);



    // FMUL V0.4S, V1.4S, V2.4S (0x6E22DC20u)

    cpu.write_vector_register(1, CPU::VectorRegister{0xC040000040000000ull, 0xC0A0000040800000ull});

    cpu.write_vector_register(2, CPU::VectorRegister{0x4000000040400000ull, 0xBF800000C0000000ull});

    phase31_run_one(cpu, memory, 0x6E22DC20u);

    CHECK(cpu.read_vector_register(0).low == 0xC0C0000040C00000ull);

    CHECK(cpu.read_vector_register(0).high == 0x40A00000C1000000ull);



    // FDIV V0.2S, V1.2S, V2.2S (0x2E22FC20u)

    cpu.set_fpsr(0);

    cpu.write_vector_register(1, CPU::VectorRegister{0x000000003F800000ull, 0});

    cpu.write_vector_register(2, CPU::VectorRegister{0x0000000000000000ull, 0});

    phase31_run_one(cpu, memory, 0x2E22FC20u);

    CHECK((cpu.read_vector_register(0).low & 0xFFFFFFFFull) == 0x7F800000ull);

    CHECK((cpu.read_vector_register(0).low >> 32) == 0x7FC00000ull);

    CHECK((cpu.get_fpsr() & 0x3u) == 0x3u);

}



void phase34_simd_fp_unary() {

    CPU cpu;

    Memory memory(0x1000);



    // FABS V0.4S, V1.4S (0x4EA0F820u)

    cpu.write_vector_register(1, CPU::VectorRegister{0x40000000BF800000ull, 0xFFC00000C0400000ull});

    phase31_run_one(cpu, memory, 0x4EA0F820u);

    CHECK(cpu.read_vector_register(0).low == 0x400000003F800000ull);

    CHECK(cpu.read_vector_register(0).high == 0x7FC0000040400000ull);



    // FNEG V0.2D, V1.2D (0x6EE0F820u)

    cpu.write_vector_register(1, CPU::VectorRegister{0x3FF0000000000000ull, 0xBFF0000000000000ull});

    phase31_run_one(cpu, memory, 0x6EE0F820u);

    CHECK(cpu.read_vector_register(0).low == 0xBFF0000000000000ull);

    CHECK(cpu.read_vector_register(0).high == 0x3FF0000000000000ull);



    // FSQRT V0.4S, V1.4S (0x6EA1F820u)

    cpu.set_fpsr(0);

    cpu.write_vector_register(1, CPU::VectorRegister{0xC080000040800000ull, 0x4110000080000000ull});

    phase31_run_one(cpu, memory, 0x6EA1F820u);

    CHECK(cpu.read_vector_register(0).low == 0x7FC0000040000000ull);

    CHECK(cpu.read_vector_register(0).high == 0x4040000080000000ull);

    CHECK((cpu.get_fpsr() & 1u) == 1u);

}



void phase34_simd_fp_comparisons() {

    CPU cpu;

    Memory memory(0x1000);

    cpu.set_nzcv(0b1100);



    // FCMEQ V0.4S, V1.4S, V2.4S (0x4E22E420u)

    cpu.write_vector_register(1, CPU::VectorRegister{0x400000003F800000ull, 0x4080000040400000ull});

    cpu.write_vector_register(2, CPU::VectorRegister{0x000000003F800000ull, 0x40A0000040400000ull});

    phase31_run_one(cpu, memory, 0x4E22E420u);

    CHECK(cpu.read_vector_register(0).low == 0x00000000FFFFFFFFull);

    CHECK(cpu.read_vector_register(0).high == 0x00000000FFFFFFFFull);

    CHECK(cpu.get_nzcv() == 0b1100);



    // FCMGE V0.2D, V1.2D, V2.2D (0x6E62E420u)

    cpu.write_vector_register(1, CPU::VectorRegister{0x4000000000000000ull, 0x3FF0000000000000ull});

    cpu.write_vector_register(2, CPU::VectorRegister{0x3FF0000000000000ull, 0x4000000000000000ull});

    phase31_run_one(cpu, memory, 0x6E62E420u);

    CHECK(cpu.read_vector_register(0).low == ~0ull);

    CHECK(cpu.read_vector_register(0).high == 0ull);



    // FCMGT #0 V0.4S, V1.4S, #0 (0x4EA0C820u)

    cpu.write_vector_register(1, CPU::VectorRegister{0x00000000BF800000ull, 0x3F80000080000000ull});

    phase31_run_one(cpu, memory, 0x4EA0C820u);

    CHECK(cpu.read_vector_register(0).low == 0ull);

    CHECK(cpu.read_vector_register(0).high == 0xFFFFFFFF00000000ull);



    // FCMLE #0 V0.4S, V1.4S, #0 (0x6EA0D820u)

    cpu.write_vector_register(1, CPU::VectorRegister{0x00000000BF800000ull, 0x3F80000080000000ull});

    phase31_run_one(cpu, memory, 0x6EA0D820u);

    CHECK(cpu.read_vector_register(0).low == ~0ull);

    CHECK(cpu.read_vector_register(0).high == 0x00000000FFFFFFFFull);

}



void phase34_simd_fp_pairwise_minmax() {

    CPU cpu;

    Memory memory(0x1000);



    // FADDP V0.4S, V1.4S, V2.4S (0x6E22D420u)

    cpu.write_vector_register(1, CPU::VectorRegister{0x400000003F800000ull, 0x4080000040400000ull});

    cpu.write_vector_register(2, CPU::VectorRegister{0x40C0000040A00000ull, 0x4100000040E00000ull});

    phase31_run_one(cpu, memory, 0x6E22D420u);

    CHECK(cpu.read_vector_register(0).low == 0x40E0000040400000ull);

    CHECK(cpu.read_vector_register(0).high == 0x4170000041300000ull);



    // FMAX V0.2D, V1.2D, V2.2D (0x4E62F420u)

    cpu.write_vector_register(1, CPU::VectorRegister{0x0000000000000000ull, 0x7FF8000000000001ull});

    cpu.write_vector_register(2, CPU::VectorRegister{0x8000000000000000ull, 0x3FF0000000000000ull});

    phase31_run_one(cpu, memory, 0x4E62F420u);

    CHECK(cpu.read_vector_register(0).low == 0x0000000000000000ull);

    CHECK(cpu.read_vector_register(0).high == 0x7FF8000000000000ull);



    // FMAXNM V0.2D, V1.2D, V2.2D (0x4E62C420u)

    cpu.write_vector_register(1, CPU::VectorRegister{0x0000000000000000ull, 0x7FF8000000000001ull});

    cpu.write_vector_register(2, CPU::VectorRegister{0x8000000000000000ull, 0x3FF0000000000000ull});

    phase31_run_one(cpu, memory, 0x4E62C420u);

    CHECK(cpu.read_vector_register(0).low == 0x0000000000000000ull);

    CHECK(cpu.read_vector_register(0).high == 0x3FF0000000000000ull);



    // FMIN V0.2D, V1.2D, V2.2D (0x4EE2F420u)

    cpu.write_vector_register(1, CPU::VectorRegister{0x0000000000000000ull, 0});

    cpu.write_vector_register(2, CPU::VectorRegister{0x8000000000000000ull, 0});

    phase31_run_one(cpu, memory, 0x4EE2F420u);

    CHECK(cpu.read_vector_register(0).low == 0x8000000000000000ull);

}



void phase34_simd_fp_conversions() {

    CPU cpu;

    Memory memory(0x1000);



    // SCVTF V0.4S, V1.4S (0x4E21D820u)

    cpu.write_vector_register(1, CPU::VectorRegister{

        (20ull << 32) | static_cast<std::uint32_t>(-10),

        (40ull << 32) | static_cast<std::uint32_t>(-30)

    });

    phase31_run_one(cpu, memory, 0x4E21D820u);

    CHECK(cpu.read_vector_register(0).low == 0x41A00000C1200000ull);

    CHECK(cpu.read_vector_register(0).high == 0x42200000C1F00000ull);



    // UCVTF V0.2D, V1.2D (0x6E61D820u)

    cpu.write_vector_register(1, CPU::VectorRegister{100ull, 200ull});

    phase31_run_one(cpu, memory, 0x6E61D820u);

    CHECK(cpu.read_vector_register(0).low == 0x4059000000000000ull);

    CHECK(cpu.read_vector_register(0).high == 0x4069000000000000ull);



    // FCVTZS V0.4S, V1.4S (0x4EA1B820u)

    cpu.set_fpsr(0);

    cpu.write_vector_register(1, CPU::VectorRegister{0xBFF333333FF33333ull, 0x7FC000005368D4A5ull});

    phase31_run_one(cpu, memory, 0x4EA1B820u);

    CHECK(cpu.read_vector_register(0).low == 0xFFFFFFFF00000001ull);

    CHECK(cpu.read_vector_register(0).high == 0x000000007FFFFFFFull);

    CHECK((cpu.get_fpsr() & 0x11u) == 0x11u);



    // FCVTZU V0.2D, V1.2D (0x6EE1B820u)

    cpu.set_fpsr(0);

    cpu.write_vector_register(1, CPU::VectorRegister{0x3FFE666666666666ull, 0xBFFE666666666666ull});

    phase31_run_one(cpu, memory, 0x6EE1B820u);

    CHECK(cpu.read_vector_register(0).low == 1ull);

    CHECK(cpu.read_vector_register(0).high == 0ull);

    CHECK((cpu.get_fpsr() & 0x11u) == 0x11u);

}



void phase34_unallocated_and_disassembly() {

    CPU cpu;

    Memory memory(0x1000);



    // 1D SIMD 3-same float is unallocated (Q=0, sz=1): e.g. 0x0E62D420u

    CHECK_RUNTIME_ERROR(phase31_run_one(cpu, memory, 0x0E62D420u));



    // 1D 2-reg misc float is unallocated (Q=0, sz=1): e.g. 0x0EE0F820u

    CHECK_RUNTIME_ERROR(phase31_run_one(cpu, memory, 0x0EE0F820u));



    // Disassembly tests

    std::vector<std::string> names;

    cpu.set_trace_sink([&names](const CPU::TraceRecord& r) {

        names.push_back(r.disassembly);

    });



    phase31_run_one(cpu, memory, 0x4E22D420u); // FADD V0.4S, V1.4S, V2.4S

    phase31_run_one(cpu, memory, 0x4EE2D420u); // FSUB V0.2D, V1.2D, V2.2D

    phase31_run_one(cpu, memory, 0x6E22DC20u); // FMUL V0.4S, V1.4S, V2.4S

    phase31_run_one(cpu, memory, 0x6E62FC20u); // FDIV V0.2D, V1.2D, V2.2D

    phase31_run_one(cpu, memory, 0x4EA0F820u); // FABS V0.4S, V1.4S

    phase31_run_one(cpu, memory, 0x6EE0F820u); // FNEG V0.2D, V1.2D

    phase31_run_one(cpu, memory, 0x6EA1F820u); // FSQRT V0.4S, V1.4S

    phase31_run_one(cpu, memory, 0x4E22E420u); // FCMEQ V0.4S, V1.4S, V2.4S

    phase31_run_one(cpu, memory, 0x4EA0C820u); // FCMGT V0.4S, V1.4S, #0

    phase31_run_one(cpu, memory, 0x4E22F420u); // FMAX V0.4S, V1.4S, V2.4S

    phase31_run_one(cpu, memory, 0x4E21D820u); // SCVTF V0.4S, V1.4S

    phase31_run_one(cpu, memory, 0x4EA1B820u); // FCVTZS V0.4S, V1.4S



    const std::vector<std::string> expected = {

        "FADD V0.4S, V1.4S, V2.4S",

        "FSUB V0.2D, V1.2D, V2.2D",

        "FMUL V0.4S, V1.4S, V2.4S",

        "FDIV V0.2D, V1.2D, V2.2D",

        "FABS V0.4S, V1.4S",

        "FNEG V0.2D, V1.2D",

        "FSQRT V0.4S, V1.4S",

        "FCMEQ V0.4S, V1.4S, V2.4S",

        "FCMGT V0.4S, V1.4S, #0",

        "FMAX V0.4S, V1.4S, V2.4S",

        "SCVTF V0.4S, V1.4S",

        "FCVTZS V0.4S, V1.4S"

    };



    CHECK(names.size() == expected.size());

    for (std::size_t i = 0; i < names.size() && i < expected.size(); ++i) {

        CHECK(names[i] == expected[i]);

    }

}





void phase35_system_registers_mrs_msr() {

    CPU cpu;

    Memory memory(0x1000);



    // 1. NZCV: round-trip via MRS/MSR

    cpu.write_register(0, 0xA0000000ull); // N=1, Z=0, C=1, V=0

    phase31_run_one(cpu, memory, 0xD51B4200u); // MSR NZCV, X0

    CHECK(cpu.get_nzcv() == 0b1010);

    phase31_run_one(cpu, memory, 0xD53B4201u); // MRS X1, NZCV

    CHECK(cpu.read_register(1) == 0xA0000000ull);



    // 2. DAIF: round-trip via MRS/MSR

    cpu.write_register(0, 0x00000180ull); // DAIF = 0b0110 (I=1, F=0, A=1, D=0) -> bits [9:6] = 0x180

    phase31_run_one(cpu, memory, 0xD51B4220u); // MSR DAIF, X0 (op2=1)

    CHECK(cpu.get_daif() == 0b0110);

    phase31_run_one(cpu, memory, 0xD53B4221u); // MRS X1, DAIF

    CHECK(cpu.read_register(1) == 0x00000180ull);



    // 3. CurrentEL: read-only, returns (current_el_ << 2)

    // CPU starts at EL1: CurrentEL[3:2] = 1 -> value = 4

    phase31_run_one(cpu, memory, 0xD5384242u); // MRS X2, CurrentEL

    CHECK(cpu.read_register(2) == 4);

    // Writing to CurrentEL throws (unallocated)

    CHECK_RUNTIME_ERROR(phase31_run_one(cpu, memory, 0xD5184242u)); // MSR CurrentEL, X2



    // 4. SPSR_EL1: round-trip

    cpu.write_register(0, 0x600003C5ull); // NZCV=6, DAIF=0xF, M=EL1h (0x5)

    phase31_run_one(cpu, memory, 0xD5184000u); // MSR SPSR_EL1, X0

    CHECK(cpu.get_spsr_el1() == 0x600003C5u);

    phase31_run_one(cpu, memory, 0xD5384003u); // MRS X3, SPSR_EL1

    CHECK(cpu.read_register(3) == 0x600003C5ull);



    // 5. ELR_EL1: round-trip

    cpu.write_register(0, 0x0000000080004000ull);

    phase31_run_one(cpu, memory, 0xD5184020u); // MSR ELR_EL1, X0

    CHECK(cpu.get_elr_el1() == 0x0000000080004000ull);

    phase31_run_one(cpu, memory, 0xD5384024u); // MRS X4, ELR_EL1

    CHECK(cpu.read_register(4) == 0x0000000080004000ull);



    // 6. SP_EL0: round-trip

    cpu.write_register(0, 0x000000007FFFF000ull);

    phase31_run_one(cpu, memory, 0xD5184100u); // MSR SP_EL0, X0

    CHECK(cpu.get_sp_el0() == 0x000000007FFFF000ull);

    phase31_run_one(cpu, memory, 0xD5384105u); // MRS X5, SP_EL0

    CHECK(cpu.read_register(5) == 0x000000007FFFF000ull);



    // 7. SPSel: round-trip

    cpu.write_register(0, 0);

    phase31_run_one(cpu, memory, 0xD5184200u); // MSR SPSel, X0

    CHECK(cpu.get_sp_sel() == false);

    phase31_run_one(cpu, memory, 0xD5384206u); // MRS X6, SPSel

    CHECK(cpu.read_register(6) == 0);

    cpu.write_register(0, 1);

    phase31_run_one(cpu, memory, 0xD5184200u); // MSR SPSel, X0

    CHECK(cpu.get_sp_sel() == true);

    phase31_run_one(cpu, memory, 0xD5384206u); // MRS X6, SPSel

    CHECK(cpu.read_register(6) == 1);



    // 8. ESR_EL1: round-trip

    cpu.write_register(0, 0x54000000ull); // EC=0x15 (SVC)

    phase31_run_one(cpu, memory, 0xD5185200u); // MSR ESR_EL1, X0

    CHECK(cpu.get_esr_el1() == 0x54000000u);

    phase31_run_one(cpu, memory, 0xD5385207u); // MRS X7, ESR_EL1

    CHECK(cpu.read_register(7) == 0x54000000ull);



    // 9. FAR_EL1: round-trip

    cpu.write_register(0, 0xFFFFFF8012345678ull);

    phase31_run_one(cpu, memory, 0xD5186000u); // MSR FAR_EL1, X0

    CHECK(cpu.get_far_el1() == 0xFFFFFF8012345678ull);

    phase31_run_one(cpu, memory, 0xD5386008u); // MRS X8, FAR_EL1

    CHECK(cpu.read_register(8) == 0xFFFFFF8012345678ull);



    // 10. VBAR_EL1: round-trip

    cpu.write_register(0, 0x0000000088000000ull);

    phase31_run_one(cpu, memory, 0xD518C000u); // MSR VBAR_EL1, X0

    CHECK(cpu.get_vbar_el1() == 0x0000000088000000ull);

    phase31_run_one(cpu, memory, 0xD538C009u); // MRS X9, VBAR_EL1

    CHECK(cpu.read_register(9) == 0x0000000088000000ull);

}



void phase35_msr_immediate() {

    CPU cpu;

    Memory memory(0x1000);



    // DAIFClr #15 -> unmask all (DAIF = 0)

    cpu.set_daif(0xF);

    phase31_run_one(cpu, memory, 0xD5034FFFu); // MSR DAIFClr, #15

    CHECK(cpu.get_daif() == 0);



    // DAIFSet #2 (mask I bit, bit 1)

    phase31_run_one(cpu, memory, 0xD50342DFu); // MSR DAIFSet, #2

    CHECK(cpu.get_daif() == 0x2);



    // DAIFSet #8 (mask D bit, bit 3)

    phase31_run_one(cpu, memory, 0xD50348DFu); // MSR DAIFSet, #8

    CHECK(cpu.get_daif() == 0xA);



    // SPSel #0 -> use SP_EL0

    phase31_run_one(cpu, memory, 0xD50040BFu); // MSR SPSel, #0

    CHECK(cpu.get_sp_sel() == false);



    // SPSel #1 -> use SP_EL1

    phase31_run_one(cpu, memory, 0xD50041BFu); // MSR SPSel, #1

    CHECK(cpu.get_sp_sel() == true);

}



void phase35_el_foundation_and_sp_selection() {

    CPU cpu;

    Memory memory(0x1000);



    // Reset state

    CHECK(cpu.get_current_el() == 1);

    CHECK(cpu.get_sp_sel() == true);

    CHECK(cpu.get_daif() == 0xF);



    // Set SP_EL1 and SP_EL0

    cpu.set_current_el(1);

    cpu.set_sp_sel(true); // EL1h

    cpu.set_stack_pointer(0x8000); // writes SP_EL1

    CHECK(cpu.get_stack_pointer() == 0x8000);



    cpu.set_sp_el0(0x4000); // writes banked SP_EL0

    CHECK(cpu.get_sp_el0() == 0x4000);

    CHECK(cpu.get_stack_pointer() == 0x8000); // SP is still SP_EL1



    // Switch to EL1t (SPSel=0)

    cpu.set_sp_sel(false);

    CHECK(cpu.get_stack_pointer() == 0x4000); // now uses SP_EL0



    // Switch to EL0

    cpu.set_current_el(0);

    CHECK(cpu.get_stack_pointer() == 0x4000); // always uses SP_EL0 at EL0



    // Modifying SP at EL0 updates SP_EL0

    cpu.set_stack_pointer(0x4080);

    CHECK(cpu.get_sp_el0() == 0x4080);



    // Switch back to EL1h

    cpu.set_current_el(1);

    cpu.set_sp_sel(true);

    CHECK(cpu.get_stack_pointer() == 0x8000); // restores SP_EL1

    CHECK(cpu.get_sp_el0() == 0x4080);       // SP_EL0 preserved

}



void phase35_svc_exception_entry() {

    CPU cpu;

    Memory memory(0x2000);



    cpu.set_vbar_el1(0x1000);



    // Test 1: SVC from EL0

    cpu.set_current_el(0);

    cpu.set_sp_sel(false);

    cpu.set_nzcv(0b1100);

    cpu.set_daif(0x0); // unmasked

    cpu.set_program_counter(0x100);



    // Execute SVC #42

    phase31_run_one(cpu, memory, 0xD4000001u | (42u << 5u)); // SVC #42



    // Verify exception state

    CHECK(cpu.get_current_el() == 1);       // entered EL1

    CHECK(cpu.get_sp_sel() == true);        // EL1h

    CHECK(cpu.get_daif() == 0xF);           // interrupts masked

    CHECK(cpu.get_elr_el1() == 0x104);      // return address is PC+4

    CHECK(cpu.get_esr_el1() == ((0x15u << 26u) | 42u)); // EC=0x15, imm16=42

    CHECK(cpu.get_program_counter() == 0x1400); // VBAR_EL1 + 0x400 (lower EL AArch64)



    // Verify SPSR_EL1

    const std::uint32_t spsr = cpu.get_spsr_el1();

    CHECK(((spsr >> 28u) & 0xFu) == 0b1100); // NZCV preserved

    CHECK(((spsr >> 6u) & 0xFu) == 0x0);     // DAIF preserved

    CHECK((spsr & 0x1Fu) == 0x0);            // M = EL0t (0b00000)



    // Test 2: SVC from EL1h

    cpu.set_current_el(1);

    cpu.set_sp_sel(true);

    cpu.set_program_counter(0x200);



    phase31_run_one(cpu, memory, 0xD4000001u | (7u << 5u)); // SVC #7

    CHECK(cpu.get_elr_el1() == 0x204);

    CHECK(cpu.get_esr_el1() == ((0x15u << 26u) | 7u));

    CHECK(cpu.get_program_counter() == 0x1200); // VBAR_EL1 + 0x200 (current EL, SP_ELx)



    // Test 3: SVC from EL1t

    cpu.set_current_el(1);

    cpu.set_sp_sel(false);

    cpu.set_program_counter(0x300);



    phase31_run_one(cpu, memory, 0xD4000001u | (99u << 5u)); // SVC #99

    CHECK(cpu.get_elr_el1() == 0x304);

    CHECK(cpu.get_esr_el1() == ((0x15u << 26u) | 99u));

    CHECK(cpu.get_program_counter() == 0x1000); // VBAR_EL1 + 0x000 (current EL, SP_EL0)

}



void phase35_eret_exception_return() {

    CPU cpu;

    Memory memory(0x2000);



    // Set up state as if inside exception handler at EL1h

    cpu.set_current_el(1);

    cpu.set_sp_sel(true);

    cpu.set_program_counter(0x100);

    cpu.set_elr_el1(0x500); // return address in memory range

    // SPSR_EL1: NZCV=0b1001, DAIF=0, Mode=EL0 (0b00000)

    cpu.set_spsr_el1((0x9u << 28u) | (0x0u << 6u) | 0x0u);

    cpu.set_stack_pointer(0x8000); // SP_EL1

    cpu.set_sp_el0(0x4000);        // SP_EL0



    // Execute ERET

    phase31_run_one(cpu, memory, 0xD69F03E0u); // ERET



    CHECK(cpu.get_program_counter() == 0x500);

    CHECK(cpu.get_current_el() == 0);        // returned to EL0

    CHECK(cpu.get_sp_sel() == false);

    CHECK(cpu.get_nzcv() == 0b1001);         // NZCV restored

    CHECK(cpu.get_daif() == 0x0);            // DAIF restored

    CHECK(cpu.get_stack_pointer() == 0x4000);// active SP is now SP_EL0



    // Test return to EL1t

    cpu.set_current_el(1);

    cpu.set_sp_sel(true);

    cpu.set_program_counter(0x200);

    cpu.set_elr_el1(0x600);

    cpu.set_spsr_el1((0x2u << 28u) | (0xFu << 6u) | 0x4u); // M=EL1t (0b00100)

    phase31_run_one(cpu, memory, 0xD69F03E0u);

    CHECK(cpu.get_program_counter() == 0x600);

    CHECK(cpu.get_current_el() == 1);

    CHECK(cpu.get_sp_sel() == false); // EL1t

    CHECK(cpu.get_stack_pointer() == 0x4000); // SP_EL0



    // Test return to EL1h

    cpu.set_current_el(1);

    cpu.set_sp_sel(false);

    cpu.set_program_counter(0x300);

    cpu.set_elr_el1(0x700);

    cpu.set_spsr_el1((0x4u << 28u) | (0xFu << 6u) | 0x5u); // M=EL1h (0b00101)

    phase31_run_one(cpu, memory, 0xD69F03E0u);

    CHECK(cpu.get_program_counter() == 0x700);

    CHECK(cpu.get_current_el() == 1);

    CHECK(cpu.get_sp_sel() == true); // EL1h

    CHECK(cpu.get_stack_pointer() == 0x8000); // SP_EL1



    // Test invalid SPSR_EL1 mode throws

    cpu.set_current_el(1);

    cpu.set_program_counter(0x400);

    cpu.set_spsr_el1(0x2u); // M=0b00010 (invalid mode)

    CHECK_RUNTIME_ERROR(phase31_run_one(cpu, memory, 0xD69F03E0u));

}



void phase35_privilege_violations() {

    CPU cpu;

    Memory memory(0x1000);



    // Switch to EL0

    cpu.set_current_el(0);



    // At EL0: ERET throws

    CHECK_RUNTIME_ERROR(phase31_run_one(cpu, memory, 0xD69F03E0u));



    // At EL0: MSR immediate throws

    CHECK_RUNTIME_ERROR(phase31_run_one(cpu, memory, 0xD5034FFFu)); // MSR DAIFClr, #15

    CHECK_RUNTIME_ERROR(phase31_run_one(cpu, memory, 0xD50040BFu)); // MSR SPSel, #0



    // At EL0: EL1-only MRS throws

    CHECK_RUNTIME_ERROR(phase31_run_one(cpu, memory, 0xD5384242u)); // MRS CurrentEL

    CHECK_RUNTIME_ERROR(phase31_run_one(cpu, memory, 0xD5384003u)); // MRS SPSR_EL1

    CHECK_RUNTIME_ERROR(phase31_run_one(cpu, memory, 0xD5384024u)); // MRS ELR_EL1

    CHECK_RUNTIME_ERROR(phase31_run_one(cpu, memory, 0xD5384105u)); // MRS SP_EL0

    CHECK_RUNTIME_ERROR(phase31_run_one(cpu, memory, 0xD5384206u)); // MRS SPSel

    CHECK_RUNTIME_ERROR(phase31_run_one(cpu, memory, 0xD5385207u)); // MRS ESR_EL1

    CHECK_RUNTIME_ERROR(phase31_run_one(cpu, memory, 0xD5386008u)); // MRS FAR_EL1

    CHECK_RUNTIME_ERROR(phase31_run_one(cpu, memory, 0xD538C009u)); // MRS VBAR_EL1

    CHECK_RUNTIME_ERROR(phase31_run_one(cpu, memory, 0xD53B4221u)); // MRS DAIF



    // At EL0: User-accessible system registers succeed

    cpu.set_program_counter(0x100);

    phase31_run_one(cpu, memory, 0xD53B4200u); // MRS X0, NZCV

    phase31_run_one(cpu, memory, 0xD51B4200u); // MSR NZCV, X0

    phase31_run_one(cpu, memory, 0xD53B4400u); // MRS X0, FPCR

    phase31_run_one(cpu, memory, 0xD51B4400u); // MSR FPCR, X0

    phase31_run_one(cpu, memory, 0xD53B4420u); // MRS X0, FPSR

    phase31_run_one(cpu, memory, 0xD51B4420u); // MSR FPSR, X0

}



void phase35_disassembly_names() {

    CPU cpu;

    Memory memory(0x2000);



    std::vector<std::string> names;

    cpu.set_trace_sink([&names](const CPU::TraceRecord& r) {

        names.push_back(r.disassembly);

    });



    phase31_run_one(cpu, memory, 0xD53B4200u); // MRS X0, NZCV

    phase31_run_one(cpu, memory, 0xD51B4200u); // MSR NZCV, X0

    phase31_run_one(cpu, memory, 0xD53B4221u); // MRS X1, DAIF

    phase31_run_one(cpu, memory, 0xD51B4221u); // MSR DAIF, X1

    phase31_run_one(cpu, memory, 0xD5384242u); // MRS X2, CurrentEL

    phase31_run_one(cpu, memory, 0xD5384003u); // MRS X3, SPSR_EL1

    phase31_run_one(cpu, memory, 0xD5384024u); // MRS X4, ELR_EL1

    phase31_run_one(cpu, memory, 0xD5384105u); // MRS X5, SP_EL0

    phase31_run_one(cpu, memory, 0xD5384206u); // MRS X6, SPSel

    phase31_run_one(cpu, memory, 0xD5385207u); // MRS X7, ESR_EL1

    phase31_run_one(cpu, memory, 0xD5386008u); // MRS X8, FAR_EL1

    phase31_run_one(cpu, memory, 0xD538C009u); // MRS X9, VBAR_EL1

    phase31_run_one(cpu, memory, 0xD50041BFu); // MSR SPSel, #1

    phase31_run_one(cpu, memory, 0xD50040BFu); // MSR SPSel, #0

    phase31_run_one(cpu, memory, 0xD5034FDFu); // MSR DAIFSet, #15

    phase31_run_one(cpu, memory, 0xD5034FFFu); // MSR DAIFClr, #15

    phase31_run_one(cpu, memory, 0xD4000841u); // SVC #66

    phase31_run_one(cpu, memory, 0xD69F03E0u); // ERET



    const std::vector<std::string> expected = {

        "MRS X0, NZCV",

        "MSR NZCV, X0",

        "MRS X1, DAIF",

        "MSR DAIF, X1",

        "MRS X2, CurrentEL",

        "MRS X3, SPSR_EL1",

        "MRS X4, ELR_EL1",

        "MRS X5, SP_EL0",

        "MRS X6, SPSel",

        "MRS X7, ESR_EL1",

        "MRS X8, FAR_EL1",

        "MRS X9, VBAR_EL1",

        "MSR SPSel, #1",

        "MSR SPSel, #0",

        "MSR DAIFSet, #15",

        "MSR DAIFClr, #15",

        "SVC #66",

        "ERET"

    };



    CHECK(names.size() == expected.size());

    for (std::size_t i = 0; i < names.size() && i < expected.size(); ++i) {

        CHECK(names[i] == expected[i]);

    }

}



void phase36_synchronous_exception_model() {

    // 1. Undefined instruction

    auto undef = make_undefined_instruction(0x1000);

    CHECK(undef.ec == ExceptionClass::Unknown);

    CHECK(undef.iss == 0);

    CHECK(undef.instruction_length_4 == true);

    CHECK(undef.preferred_return == 0x1000);

    CHECK(undef.has_fault_address == false);

    CHECK(undef.esr() == 0x02000000u);



    // 2. SVC

    auto svc = make_svc(0x2000, 42);

    CHECK(svc.ec == ExceptionClass::SvcAArch64);

    CHECK(svc.iss == 42);

    CHECK(svc.preferred_return == 0x2004);

    CHECK(svc.esr() == 0x5600002Au);



    // 3. PC Alignment

    auto pcalign = make_pc_alignment(0x3001);

    CHECK(pcalign.ec == ExceptionClass::PcAlignment);

    CHECK(pcalign.preferred_return == 0x3001);

    CHECK(pcalign.has_fault_address == true);

    CHECK(pcalign.fault_address == 0x3001);

    CHECK(pcalign.esr() == 0x8A000000u);



    // 4. Instruction Aborts

    auto insn_abort_same = make_instruction_abort(true, FaultStatus::SynchronousExternal, 0x4000, 4);

    CHECK(insn_abort_same.ec == ExceptionClass::InstructionAbortSame);

    CHECK(insn_abort_same.iss == 0x10);

    CHECK(insn_abort_same.esr() == 0x86000010u);

    CHECK(insn_abort_same.fault_address == 0x4000);



    auto insn_abort_lower = make_instruction_abort(false, FaultStatus::AddressSizeLevel0, 0x5000, 4);

    CHECK(insn_abort_lower.ec == ExceptionClass::InstructionAbortLower);

    CHECK(insn_abort_lower.iss == 0x00);

    CHECK(insn_abort_lower.esr() == 0x82000000u);



    // 5. Data Aborts

    auto data_read_same = make_data_abort(true, AccessType::Read, FaultStatus::Alignment, 0x6003, 0x100, 4);

    CHECK(data_read_same.ec == ExceptionClass::DataAbortSame);

    CHECK(data_read_same.iss == 0x21); // WnR=0, FSC=0x21

    CHECK(data_read_same.esr() == 0x96000021u);

    CHECK(data_read_same.fault_address == 0x6003);

    CHECK(data_read_same.preferred_return == 0x100);



    auto data_write_lower = make_data_abort(false, AccessType::Write, FaultStatus::Alignment, 0x7005, 0x200, 8);

    CHECK(data_write_lower.ec == ExceptionClass::DataAbortLower);

    CHECK(data_write_lower.iss == 0x61); // WnR=1 (bit 6), FSC=0x21

    CHECK(data_write_lower.esr() == 0x92000061u);

    CHECK(data_write_lower.fault_address == 0x7005);

    CHECK(data_write_lower.preferred_return == 0x200);



    // 6. Illegal State

    auto illegal = make_illegal_state(0x8000);

    CHECK(illegal.ec == ExceptionClass::IllegalExecutionState);

    CHECK(illegal.preferred_return == 0x8000);

    CHECK(illegal.esr() == 0x3A000000u);



    // 7. Unallocated Encoding Predicate

    CHECK(is_unallocated_encoding(0x00000000u));

    CHECK(is_unallocated_encoding(0x02000000u));

    CHECK(is_unallocated_encoding(0x06000000u));

    CHECK(is_unallocated_encoding(0x1E000000u));

    CHECK(!is_unallocated_encoding(0x91000000u)); // ADD X0, X0, #0

    CHECK(!is_unallocated_encoding(0xD500401Fu)); // MSR immediate

}



void phase36_undefined_instruction_execution() {

    CPU cpu;

    Memory memory(0x2000);

    cpu.set_architectural_exceptions(true);

    cpu.set_vbar_el1(0x1000);

    cpu.set_program_counter(0x100);



    // Unallocated opcode at 0x100

    memory.write_u32(0x100, 0x00000000u);

    cpu.step(memory);



    CHECK(cpu.get_program_counter() == 0x1200); // VBAR_EL1 + 0x200

    CHECK(cpu.get_elr_el1() == 0x100);          // faulting instruction address

    CHECK(cpu.get_esr_el1() == 0x02000000u);     // EC=0x00, IL=1

    CHECK(cpu.get_current_el() == 1);

    CHECK(cpu.get_sp_sel() == true);

    CHECK(cpu.get_daif() == 0xFu);

}



void phase36_data_abort_alignment_and_range() {

    CPU cpu;

    Memory memory(0x2000);

    cpu.set_architectural_exceptions(true);

    cpu.set_vbar_el1(0x1000);



    // 1. Read alignment fault

    cpu.set_program_counter(0x100);

    cpu.write_register(1, 0x201); // unaligned 8-byte load address

    phase31_run_one(cpu, memory, 0xF9400020u); // LDR X0, [X1]

    CHECK(cpu.get_program_counter() == 0x1200);

    CHECK(cpu.get_far_el1() == 0x201);

    CHECK(cpu.get_elr_el1() == 0x100);

    CHECK(cpu.get_esr_el1() == 0x96000021u); // EC=0x25, IL=1, WnR=0, DFSC=0x21



    // 2. Write alignment fault

    cpu.set_program_counter(0x104);

    cpu.write_register(1, 0x203); // unaligned 8-byte store address

    phase31_run_one(cpu, memory, 0xF9000020u); // STR X0, [X1]

    CHECK(cpu.get_program_counter() == 0x1200);

    CHECK(cpu.get_far_el1() == 0x203);

    CHECK(cpu.get_elr_el1() == 0x104);

    CHECK(cpu.get_esr_el1() == 0x96000061u); // EC=0x25, IL=1, WnR=1, DFSC=0x21



    // 3. Address size fault (PA >= 2^52)

    cpu.set_program_counter(0x108);

    cpu.write_register(1, 1ull << 52);

    phase31_run_one(cpu, memory, 0xF9400020u); // LDR X0, [X1]

    CHECK(cpu.get_program_counter() == 0x1200);

    CHECK(cpu.get_far_el1() == (1ull << 52));

    CHECK(cpu.get_esr_el1() == 0x96000000u); // DFSC=0x00 (AddressSizeLevel0)



    // 4. Synchronous external abort (beyond RAM size 0x2000)

    cpu.set_program_counter(0x10C);

    cpu.write_register(1, 0x4000);

    phase31_run_one(cpu, memory, 0xF9400020u); // LDR X0, [X1]

    CHECK(cpu.get_program_counter() == 0x1200);

    CHECK(cpu.get_far_el1() == 0x4000);

    CHECK(cpu.get_esr_el1() == 0x96000010u); // DFSC=0x10 (SynchronousExternal)

}



void phase36_pc_alignment_fault() {

    CPU cpu;

    Memory memory(0x2000);

    cpu.set_architectural_exceptions(true);

    cpu.set_vbar_el1(0x1000);



    // Set misaligned PC

    cpu.set_program_counter(0x101);

    cpu.step(memory);



    CHECK(cpu.get_program_counter() == 0x1200);

    CHECK(cpu.get_far_el1() == 0x101);

    CHECK(cpu.get_elr_el1() == 0x101);

    CHECK(cpu.get_esr_el1() == 0x8A000000u); // EC=0x22, IL=1, ISS=0

}



void phase36_exception_vectors_and_privilege() {

    CPU cpu;

    Memory memory(0x4000);

    cpu.set_architectural_exceptions(true);

    cpu.set_vbar_el1(0x2000);



    // 1. From EL0: vectors to VBAR + 0x400

    cpu.set_current_el(0);

    cpu.set_program_counter(0x100);

    memory.write_u32(0x100, 0x00000000u); // undef

    cpu.step(memory);



    CHECK(cpu.get_program_counter() == 0x2400); // 0x2000 + 0x400

    CHECK(cpu.get_current_el() == 1);          // elevated to EL1h

    CHECK(cpu.get_sp_sel() == true);

    CHECK((cpu.get_spsr_el1() & 0x1Fu) == 0x0u); // saved mode EL0t



    // 2. From EL1 with SP_EL0 (EL1t): vectors to VBAR + 0x000

    cpu.set_current_el(1);

    cpu.set_sp_sel(false);

    cpu.set_program_counter(0x104);

    memory.write_u32(0x104, 0x00000000u);

    cpu.step(memory);



    CHECK(cpu.get_program_counter() == 0x2000); // 0x2000 + 0x000

    CHECK(cpu.get_current_el() == 1);

    CHECK(cpu.get_sp_sel() == true);

    CHECK((cpu.get_spsr_el1() & 0x1Fu) == 0x4u); // saved mode EL1t



    // 3. EL0 privilege violations generate Undefined Instruction

    cpu.set_current_el(0);

    cpu.set_program_counter(0x108);

    memory.write_u32(0x108, 0xD69F03E0u); // ERET at EL0

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x2400); // vectored from EL0

    CHECK(cpu.get_esr_el1() == 0x02000000u);   // EC=0x00 (Undef)

}



void phase36_eret_recovery_and_illegal_state() {

    CPU cpu;

    Memory memory(0x2000);

    cpu.set_architectural_exceptions(true);

    cpu.set_vbar_el1(0x1000);



    // 1. Clean SVC and ERET recovery

    memory.write_u32(0x100, 0xD4000C61u); // SVC #99

    memory.write_u32(0x104, 0xD503201Fu); // NOP

    memory.write_u32(0x1200, 0xD69F03E0u); // ERET

    cpu.set_program_counter(0x100);



    cpu.step(memory); // SVC takes exception to 0x1200

    CHECK(cpu.get_program_counter() == 0x1200);

    CHECK(cpu.get_esr_el1() == 0x56000063u); // EC=0x15, IL=1, imm=99

    CHECK(cpu.get_elr_el1() == 0x104);



    cpu.step(memory); // ERET returns to 0x104

    CHECK(cpu.get_program_counter() == 0x104);



    // 2. Illegal State via invalid SPSR Mode

    cpu.set_elr_el1(0x200);

    cpu.set_spsr_el1(0x2u); // M=0b00010 (invalid mode)

    memory.write_u32(0x1204, 0xD69F03E0u); // ERET

    cpu.set_program_counter(0x1204);

    cpu.step(memory); // ERET sets PSTATE.IL and resumes at ELR_EL1 (0x200)



    CHECK(cpu.get_program_counter() == 0x200);

    CHECK(cpu.get_il() == true);



    // Next fetch at 0x200 takes Illegal Execution State exception

    memory.write_u32(0x200, 0xD503201Fu); // NOP

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x1200);

    CHECK(cpu.get_esr_el1() == 0x3A000000u); // EC=0x0E (IllegalExecutionState), IL=1

    CHECK(cpu.get_elr_el1() == 0x200);

    CHECK(cpu.get_il() == false);

}



void phase36_failure_atomicity() {

    CPU cpu;

    Memory memory(0x2000);

    cpu.set_architectural_exceptions(true);

    cpu.set_vbar_el1(0x1000);



    // 1. Faulting LDP does not modify destination registers

    cpu.write_register(0, 0x1111222233334444ull);

    cpu.write_register(1, 0x5555666677778888ull);

    cpu.write_register(2, 0x301); // unaligned pair load address

    cpu.set_program_counter(0x100);

    phase31_run_one(cpu, memory, 0xA9400440u); // LDP X0, X1, [X2]



    CHECK(cpu.read_register(0) == 0x1111222233334444ull);

    CHECK(cpu.read_register(1) == 0x5555666677778888ull);



    // 2. Faulting STP does not write memory

    for (std::size_t i = 0x300; i < 0x320; ++i) {

        memory.write_u8(i, 0xAA);

    }

    cpu.set_program_counter(0x104);

    phase31_run_one(cpu, memory, 0xA9000440u); // STP X0, X1, [X2]



    for (std::size_t i = 0x300; i < 0x320; ++i) {

        CHECK(memory.read_u8(i) == 0xAA);

    }

}



} // namespace





// ===========================================================================

// Phase 37 Unit Tests: MMU and Virtual Memory Foundation

// ===========================================================================



void phase37_mmu_system_registers() {

    CPU cpu;

    Memory memory(0x4000);

    cpu.set_architectural_exceptions(true);

    cpu.set_vbar_el1(0x1000);



    // 1. Reset values

    CHECK(cpu.get_sctlr_el1() == 0x00C50838ULL);

    CHECK(cpu.get_tcr_el1() == 0ULL);

    CHECK(cpu.get_ttbr0_el1() == 0ULL);

    CHECK(cpu.get_ttbr1_el1() == 0ULL);

    CHECK(cpu.get_mair_el1() == 0ULL);

    CHECK(cpu.get_id_aa64mmfr0_el1() == 0x2100032310201126ULL);

    CHECK(cpu.get_par_el1() == 0ULL);



    // 2. MRS SCTLR_EL1 -> X1

    cpu.set_program_counter(0x100);

    phase31_run_one(cpu, memory, 0xD5381001u); // MRS X1, SCTLR_EL1

    CHECK(cpu.read_register(1) == 0x00C50838ULL);



    // 3. MSR SCTLR_EL1, X2

    cpu.write_register(2, 0x00C50838ULL); // test write with M=0

    cpu.set_program_counter(0x104);

    phase31_run_one(cpu, memory, 0xD5181002u); // MSR SCTLR_EL1, X2

    CHECK(cpu.get_sctlr_el1() == 0x00C50838ULL);



    // 4. TCR_EL1 read/write

    cpu.write_register(3, 0x0000000500800010ULL); // 48-bit VA, 4KB, IPS=5

    cpu.set_program_counter(0x108);

    phase31_run_one(cpu, memory, 0xD5182043u); // MSR TCR_EL1, X3

    CHECK(cpu.get_tcr_el1() == 0x0000000500800010ULL);



    cpu.set_program_counter(0x10C);

    phase31_run_one(cpu, memory, 0xD5382044u); // MRS X4, TCR_EL1

    CHECK(cpu.read_register(4) == 0x0000000500800010ULL);



    // 5. TTBR0_EL1 and TTBR1_EL1 read/write

    cpu.write_register(5, 0x0000123456789000ULL);

    cpu.set_program_counter(0x110);

    phase31_run_one(cpu, memory, 0xD5182005u); // MSR TTBR0_EL1, X5

    CHECK(cpu.get_ttbr0_el1() == 0x0000123456789000ULL);



    cpu.write_register(6, 0x0000987654321000ULL);

    cpu.set_program_counter(0x114);

    phase31_run_one(cpu, memory, 0xD5182026u); // MSR TTBR1_EL1, X6

    CHECK(cpu.get_ttbr1_el1() == 0x0000987654321000ULL);



    // 6. MAIR_EL1 read/write

    cpu.write_register(7, 0xFF440400ULL);

    cpu.set_program_counter(0x118);

    phase31_run_one(cpu, memory, 0xD518A207u); // MSR MAIR_EL1, X7

    CHECK(cpu.get_mair_el1() == 0xFF440400ULL);



    // 7. ID_AA64MMFR0_EL1 read-only

    cpu.set_program_counter(0x11C);

    phase31_run_one(cpu, memory, 0xD5380708u); // MRS X8, ID_AA64MMFR0_EL1

    CHECK(cpu.read_register(8) == 0x2100032310201126ULL);



    // MSR to ID_AA64MMFR0_EL1 traps to Undefined Instruction

    cpu.set_program_counter(0x120);

    phase31_run_one(cpu, memory, 0xD5180708u);

    CHECK(cpu.get_program_counter() == 0x1200); // vector + 0x200

    CHECK(cpu.get_esr_el1() == 0x02000000u);   // EC=0x00, IL=1



    // 8. EL0 privilege check: reading EL1 sysreg at EL0 triggers EC=0x00

    cpu.set_current_el(0);

    cpu.set_program_counter(0x124);

    phase31_run_one(cpu, memory, 0xD5381001u); // MRS X1, SCTLR_EL1 at EL0

    CHECK(cpu.get_program_counter() == 0x1400); // vector + 0x400 from EL0

    CHECK(cpu.get_esr_el1() == 0x02000000u);   // EC=0x00, IL=1

}



void phase37_page_table_walker_mappings() {

    CPU cpu;

    Memory memory(0x20000);

    cpu.set_architectural_exceptions(true);

    cpu.set_vbar_el1(0x1000);



    const std::uint64_t l0_base = 0x2000;

    const std::uint64_t l1_base = 0x3000;

    const std::uint64_t l2_base = 0x4000;

    const std::uint64_t l3_base = 0x5000;

    const std::uint64_t target_pa = 0x8000;



    memory.write_u64(l0_base, l1_base | 0x3ULL);

    memory.write_u64(l1_base, l2_base | 0x3ULL);

    memory.write_u64(l2_base, l3_base | 0x3ULL);



    // Identity-map page 0 (0x0..0xFFF) for code execution (PC 0x100, 0x104, etc.)

    memory.write_u64(l3_base + 0u, 0x0ULL | 0x3ULL | (1ULL << 10u));



    // Map VA 0x1000 to PA target_pa (0x8000), AF=1, AP=0b00 (RW by EL1)

    memory.write_u64(l3_base + 8u, target_pa | 0x3ULL | (1ULL << 10u));



    // Prepopulate physical page 0x8000 with a known 64-bit value

    memory.write_u64(target_pa, 0x1122334455667788ULL);



    // Configure CPU MMU registers:

    cpu.set_ttbr0_el1(l0_base);

    cpu.set_tcr_el1(0x0000000500800010ULL); // 48-bit VA

    cpu.set_sctlr_el1(0x00C50839ULL);       // MMU enabled (M=1)



    // Execute LDR X1, [X2] where X2 = 0x1000 (virtual address)

    cpu.write_register(2, 0x1000);

    cpu.set_program_counter(0x100);

    phase31_run_one(cpu, memory, 0xF9400041u); // LDR X1, [X2]

    CHECK(cpu.read_register(1) == 0x1122334455667788ULL);



    // Execute STR X3, [X2] with X3 = 0xAABBCCDDEEFF0011ULL

    cpu.write_register(3, 0xAABBCCDDEEFF0011ULL);

    cpu.set_program_counter(0x104);

    phase31_run_one(cpu, memory, 0xF9000043u); // STR X3, [X2]

    CHECK(memory.read_u64(target_pa) == 0xAABBCCDDEEFF0011ULL);

}



void phase37_translation_and_permission_faults() {

    CPU cpu;

    Memory memory(0x20000);

    cpu.set_architectural_exceptions(true);

    cpu.set_vbar_el1(0x1000);



    const std::uint64_t l0_base = 0x2000;

    const std::uint64_t l1_base = 0x3000;

    const std::uint64_t l2_base = 0x4000;

    const std::uint64_t l3_base = 0x5000;



    memory.write_u64(l0_base, l1_base | 0x3ULL);

    memory.write_u64(l1_base, l2_base | 0x3ULL);

    memory.write_u64(l2_base, l3_base | 0x3ULL);



    // Identity-map page 0 (0x0..0xFFF) for code execution at 0x100

    memory.write_u64(l3_base + 0u, 0x0ULL | 0x3ULL | (1ULL << 10u));



    cpu.set_ttbr0_el1(l0_base);

    cpu.set_sctlr_el1(0x00C50839ULL); // MMU on



    // 1. Translation Fault Level 3 on LDR (VA 0x1000 is not mapped in L3)

    cpu.write_register(2, 0x1000);

    cpu.set_program_counter(0x100);

    phase31_run_one(cpu, memory, 0xF9400041u); // LDR X1, [X2]

    CHECK(cpu.get_program_counter() == 0x1200); // vector + 0x200

    CHECK(cpu.get_esr_el1() == 0x96000007u);   // EC=0x25, IL=1, DFSC=0x07

    CHECK(cpu.get_far_el1() == 0x1000);



    // 2. Access Flag Fault Level 3 (desc valid but AF=0)

    // Map VA 0x2000 to PA 0x8000, but with AF=0 (desc = 0x8000 | 0x3)

    memory.write_u64(l3_base + 16u, 0x8000 | 0x3ULL);

    cpu.set_program_counter(0x104);

    cpu.write_register(2, 0x2000);

    phase31_run_one(cpu, memory, 0xF9400041u);

    CHECK(cpu.get_program_counter() == 0x1200);

    CHECK(cpu.get_esr_el1() == 0x9600000Bu);

    CHECK(cpu.get_far_el1() == 0x2000);



    // 3. Permission Fault Level 3 (write to read-only page: AP[2]=1)

    // Map VA 0x3000 to PA 0x8000, AF=1, AP=0b10 (ReadOnly by EL1) -> desc = 0x8000 | 0x3 | (1<<10) | (2<<6)

    memory.write_u64(l3_base + 24u, 0x8000 | 0x3ULL | (1ULL << 10u) | (2ULL << 6u));

    cpu.write_register(2, 0x3000);

    cpu.set_program_counter(0x108);

    phase31_run_one(cpu, memory, 0xF9000041u); // STR X1, [X2]

    CHECK(cpu.get_program_counter() == 0x1200);

    CHECK(cpu.get_esr_el1() == 0x9600004Fu);   // EC=0x25, IL=1, WnR=1, DFSC=0x0F

    CHECK(cpu.get_far_el1() == 0x3000);



    // 4. Address Size Fault Level 0 (VA out of 48-bit range)

    cpu.write_register(2, 0x0001000000000000ULL);

    cpu.set_program_counter(0x10C);

    phase31_run_one(cpu, memory, 0xF9400041u);

    CHECK(cpu.get_program_counter() == 0x1200);

    CHECK(cpu.get_esr_el1() == 0x96000000u);   // EC=0x25, IL=1, DFSC=0x00

    CHECK(cpu.get_far_el1() == 0x0001000000000000ULL);

}



void phase37_instruction_fetch_and_wxn_faults() {

    CPU cpu;

    Memory memory(0x20000);

    cpu.set_architectural_exceptions(true);

    cpu.set_vbar_el1(0x1000);



    const std::uint64_t l0_base = 0x2000;

    const std::uint64_t l1_base = 0x3000;

    const std::uint64_t l2_base = 0x4000;

    const std::uint64_t l3_base = 0x5000;

    const std::uint64_t code_pa = 0x8000;



    memory.write_u64(l0_base, l1_base | 0x3ULL);

    memory.write_u64(l1_base, l2_base | 0x3ULL);

    memory.write_u64(l2_base, l3_base | 0x3ULL);



    // Map VA 0x4000 to PA code_pa (0x8000), AF=1, AP=0b00 (RW), PXN=0

    memory.write_u64(l3_base + 32u, code_pa | 0x3ULL | (1ULL << 10u));

    // Write MOVZ X0, #0 at PA 0x8000

    memory.write_u32(code_pa, 0xD2800000u);



    cpu.set_ttbr0_el1(l0_base);

    cpu.set_sctlr_el1(0x00C50839ULL); // MMU on



    // 1. Fetch from translated VA 0x4000 succeeds

    cpu.set_program_counter(0x4000);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x4004);



    // 2. Fetch from PXN page fails at EL1

    // Map VA 0x5000 to PA code_pa, PXN=1 (bit 53)

    memory.write_u64(l3_base + 40u, code_pa | 0x3ULL | (1ULL << 10u) | (1ULL << 53u));

    cpu.set_program_counter(0x5000);

    cpu.step(memory);

    CHECK(cpu.get_program_counter() == 0x1200); // vector + 0x200

    CHECK(cpu.get_esr_el1() == 0x8600000Fu);   // EC=0x21 (InstructionAbortSame), IL=1, IFSC=0x0F

    CHECK(cpu.get_far_el1() == 0x5000);

}



void phase37_tlb_and_tlbi_invalidation() {

    CPU cpu;

    Memory memory(0x20000);

    cpu.set_architectural_exceptions(true);

    cpu.set_vbar_el1(0x1000);



    const std::uint64_t l0_base = 0x2000;

    const std::uint64_t l1_base = 0x3000;

    const std::uint64_t l2_base = 0x4000;

    const std::uint64_t l3_base = 0x5000;

    const std::uint64_t page_a = 0x8000;

    const std::uint64_t page_b = 0x9000;



    memory.write_u64(l0_base, l1_base | 0x3ULL);

    memory.write_u64(l1_base, l2_base | 0x3ULL);

    memory.write_u64(l2_base, l3_base | 0x3ULL);



    // Identity-map page 0 for code execution

    memory.write_u64(l3_base + 0u, 0x0ULL | 0x3ULL | (1ULL << 10u));



    // Map VA 0x1000 to page_a (0x8000)

    memory.write_u64(l3_base + 8u, page_a | 0x3ULL | (1ULL << 10u));



    memory.write_u64(page_a, 0xAAAAAAAAAAAAAAAAULL);

    memory.write_u64(page_b, 0xBBBBBBBBBBBBBBBBULL);



    cpu.set_ttbr0_el1(l0_base);

    cpu.set_sctlr_el1(0x00C50839ULL); // MMU on



    // Initial read caches translation in TLB

    cpu.write_register(2, 0x1000);

    cpu.set_program_counter(0x100);

    phase31_run_one(cpu, memory, 0xF9400041u); // LDR X1, [X2]

    CHECK(cpu.read_register(1) == 0xAAAAAAAAAAAAAAAAULL);



    // Modify page table entry to point to page_b in memory (without TLBI)

    memory.write_u64(l3_base + 8u, page_b | 0x3ULL | (1ULL << 10u));



    // TLB Hit: should still return page_a value

    cpu.set_program_counter(0x104);

    phase31_run_one(cpu, memory, 0xF9400041u);

    CHECK(cpu.read_register(1) == 0xAAAAAAAAAAAAAAAAULL);



    // Execute TLBI VMALLE1 (0xD508871F)

    cpu.set_program_counter(0x108);

    phase31_run_one(cpu, memory, 0xD508871Fu); // TLBI VMALLE1



    // Now TLB Miss: should walk page table and read page_b

    cpu.set_program_counter(0x10C);

    phase31_run_one(cpu, memory, 0xF9400041u);

    CHECK(cpu.read_register(1) == 0xBBBBBBBBBBBBBBBBULL);

}



void phase37_memory_operations_and_failure_atomicity() {

    CPU cpu;

    Memory memory(0x20000);

    cpu.set_architectural_exceptions(true);

    cpu.set_vbar_el1(0x1000);



    const std::uint64_t l0_base = 0x2000;

    const std::uint64_t l1_base = 0x3000;

    const std::uint64_t l2_base = 0x4000;

    const std::uint64_t l3_base = 0x5000;

    const std::uint64_t page_pa = 0x8000;



    memory.write_u64(l0_base, l1_base | 0x3ULL);

    memory.write_u64(l1_base, l2_base | 0x3ULL);

    memory.write_u64(l2_base, l3_base | 0x3ULL);



    // Identity-map page 0 for code execution

    memory.write_u64(l3_base + 0u, 0x0ULL | 0x3ULL | (1ULL << 10u));



    // Map VA 0x1000 to page_pa (0x8000), but leave VA 0x2000 UNMAPPED!

    memory.write_u64(l3_base + 8u, page_pa | 0x3ULL | (1ULL << 10u));



    cpu.set_ttbr0_el1(l0_base);

    cpu.set_sctlr_el1(0x00C50839ULL); // MMU on



    // LDP X0, X1 spanning page boundary where 2nd page is unmapped:

    // Access at VA 0x1FF8 (reads 8 bytes at 0x1FF8 in page 0x1000, and 8 bytes at 0x2000 in unmapped page 0x2000)

    cpu.write_register(0, 0x1111ULL);

    cpu.write_register(1, 0x2222ULL);

    cpu.write_register(2, 0x1FF8);



    cpu.set_program_counter(0x100);

    phase31_run_one(cpu, memory, 0xA9400440u); // LDP X0, X1, [X2]



    // Failure atomicity: registers X0 and X1 MUST remain unmodified!

    CHECK(cpu.read_register(0) == 0x1111ULL);

    CHECK(cpu.read_register(1) == 0x2222ULL);

    CHECK(cpu.get_program_counter() == 0x1200);

    CHECK(cpu.get_esr_el1() == 0x96000007u); // Translation fault level 3

}





void phase38_barriers_hints_clrex() {

    CPU cpu;

    Memory memory(0x10000);



    cpu.set_program_counter(0x100);

    phase31_run_one(cpu, memory, 0xD5033FBFu); // DMB SY

    CHECK(cpu.get_program_counter() == 0x104);



    phase31_run_one(cpu, memory, 0xD5033F9Fu); // DSB SY

    CHECK(cpu.get_program_counter() == 0x108);



    phase31_run_one(cpu, memory, 0xD5033FDFu); // ISB SY

    CHECK(cpu.get_program_counter() == 0x10C);



    phase31_run_one(cpu, memory, 0xD503201Fu); // NOP

    CHECK(cpu.get_program_counter() == 0x110);

    phase31_run_one(cpu, memory, 0xD503203Fu); // YIELD

    CHECK(cpu.get_program_counter() == 0x114);

    phase31_run_one(cpu, memory, 0xD503205Fu); // WFE

    CHECK(cpu.get_program_counter() == 0x118);

    phase31_run_one(cpu, memory, 0xD503207Fu); // WFI

    CHECK(cpu.get_program_counter() == 0x11C);

    phase31_run_one(cpu, memory, 0xD503209Fu); // SEV

    CHECK(cpu.get_program_counter() == 0x120);

    phase31_run_one(cpu, memory, 0xD50320BFu); // SEVL

    CHECK(cpu.get_program_counter() == 0x124);



    memory.write_u64(0x2000, 0x12345678ULL);

    cpu.write_register(1, 0x2000);

    phase31_run_one(cpu, memory, 0xC85F7C20u); // LDXR X0, [X1]

    CHECK(cpu.read_register(0) == 0x12345678ULL);



    phase31_run_one(cpu, memory, 0xD5033F5Fu); // CLREX

    CHECK(cpu.get_program_counter() == 0x12C);



    cpu.write_register(2, 0x99999999ULL);

    phase31_run_one(cpu, memory, 0xC8007C22u); // STXR W0, X2, [X1]

    CHECK(cpu.read_register(0) == 1ULL);

    CHECK(memory.read_u64(0x2000) == 0x12345678ULL);

}



void phase38_ordered_load_store() {

    CPU cpu;

    Memory memory(0x10000);



    cpu.write_register(1, 0x2000);

    cpu.write_register(2, 0x0123456789ABCDEFULL);



    // 1. STLR 64-bit

    cpu.set_program_counter(0x100);

    phase31_run_one(cpu, memory, 0xC89FFC22u); // STLR X2, [X1]

    CHECK(cpu.get_program_counter() == 0x104);

    CHECK(memory.read_u64(0x2000) == 0x0123456789ABCDEFULL);



    // 2. LDAR 64-bit

    phase31_run_one(cpu, memory, 0xC8DFFC20u); // LDAR X0, [X1]

    CHECK(cpu.read_register(0) == 0x0123456789ABCDEFULL);



    // 3. STLR 32-bit

    cpu.write_register(1, 0x2010);

    cpu.write_register(3, 0xAABBCCDDULL);

    phase31_run_one(cpu, memory, 0x889FFC23u); // STLR W3, [X1]

    CHECK(memory.read_u32(0x2010) == 0xAABBCCDDu);



    // 4. LDAR 32-bit (zero extended)

    phase31_run_one(cpu, memory, 0x88DFFC24u); // LDAR W4, [X1]

    CHECK(cpu.read_register(4) == 0xAABBCCDDULL);



    // 5. STLRH / LDARH 16-bit

    cpu.write_register(1, 0x2020);

    cpu.write_register(5, 0x1234ULL);

    phase31_run_one(cpu, memory, 0x489FFC25u); // STLRH W5, [X1]

    CHECK(memory.read_u16(0x2020) == 0x1234u);

    phase31_run_one(cpu, memory, 0x48DFFC26u); // LDARH W6, [X1]

    CHECK(cpu.read_register(6) == 0x1234ULL);



    // 6. STLRB / LDARB 8-bit

    cpu.write_register(1, 0x2030);

    cpu.write_register(7, 0x5Au);

    phase31_run_one(cpu, memory, 0x089FFC27u); // STLRB W7, [X1]

    CHECK(memory.read_u8(0x2030) == 0x5Au);

    phase31_run_one(cpu, memory, 0x08DFFC28u); // LDARB W8, [X1]

    CHECK(cpu.read_register(8) == 0x5AULL);



    // 7. Natural alignment fault on unaligned address

    cpu.set_architectural_exceptions(true);

    cpu.set_vbar_el1(0x4000);

    cpu.write_register(1, 0x2001); // not 8-byte aligned for 64-bit LDAR

    phase31_run_one(cpu, memory, 0xC8DFFC29u); // LDAR X9, [X1]

    CHECK(cpu.get_program_counter() == 0x4200); // routed to Current EL with SPx Sync vector

    CHECK((cpu.get_esr_el1() >> 26u) == 0x25u); // EC = Data Abort

    CHECK((cpu.get_esr_el1() & 0x3Fu) == 0x21u); // FSC = Alignment Fault

    CHECK(cpu.get_far_el1() == 0x2001ULL);

}



void phase38_dczid_and_dc_zva() {

    CPU cpu;

    Memory memory(0x10000);



    // 1. MRS DCZID_EL0

    cpu.set_program_counter(0x100);

    phase31_run_one(cpu, memory, 0xD53B00E0u); // MRS X0, DCZID_EL0

    CHECK(cpu.read_register(0) == 4ULL); // BS=4 (64 bytes), DZP=0



    // 2. MSR DCZID_EL0 is read-only -> undefined instruction

    cpu.set_architectural_exceptions(true);

    cpu.set_vbar_el1(0x4000);

    phase31_run_one(cpu, memory, 0xD51B00E0u); // MSR DCZID_EL0, X0

    CHECK(cpu.get_program_counter() == 0x4200);

    CHECK((cpu.get_esr_el1() >> 26u) == 0x00u); // EC = Unknown/Undefined



    // 3. DC ZVA on aligned address

    for (std::size_t i = 0; i < 64; ++i) {

        memory.write_u8(0x2000 + i, 0xFFu);

    }

    cpu.write_register(1, 0x2000);

    cpu.set_program_counter(0x200);

    phase31_run_one(cpu, memory, 0xD50B7421u); // DC ZVA, X1

    for (std::size_t i = 0; i < 64; ++i) {

        CHECK(memory.read_u8(0x2000 + i) == 0x00u);

    }



    // 4. DC ZVA on unaligned address (aligns down to 64-byte block)

    for (std::size_t i = 0; i < 64; ++i) {

        memory.write_u8(0x3000 + i, 0xAAu);

    }

    cpu.write_register(1, 0x3018); // offset 0x18 into 0x3000 block

    phase31_run_one(cpu, memory, 0xD50B7421u); // DC ZVA, X1

    for (std::size_t i = 0; i < 64; ++i) {

        CHECK(memory.read_u8(0x3000 + i) == 0x00u);

    }

}



void phase38_cache_and_tlbi_non_global() {

    CPU cpu;

    Memory memory(0x20000);



    cpu.write_register(1, 0x2000);

    cpu.set_program_counter(0x100);

    phase31_run_one(cpu, memory, 0xD50B7A21u); // DC CVAC, X1

    CHECK(cpu.get_program_counter() == 0x104);

    phase31_run_one(cpu, memory, 0xD50B7B21u); // DC CVAU, X1

    CHECK(cpu.get_program_counter() == 0x108);

    phase31_run_one(cpu, memory, 0xD50B7E21u); // DC CIVAC, X1

    CHECK(cpu.get_program_counter() == 0x10C);

    phase31_run_one(cpu, memory, 0xD5087621u); // DC IVAC, X1

    CHECK(cpu.get_program_counter() == 0x110);

    phase31_run_one(cpu, memory, 0xD50B7521u); // IC IVAU, X1

    CHECK(cpu.get_program_counter() == 0x114);

    phase31_run_one(cpu, memory, 0xD508751Fu); // IC IALLU

    CHECK(cpu.get_program_counter() == 0x118);

    phase31_run_one(cpu, memory, 0xD508711Fu); // IC IALLUIS

    CHECK(cpu.get_program_counter() == 0x11C);



    cpu.set_architectural_exceptions(true);

    cpu.set_vbar_el1(0x1000);



    const std::uint64_t l0_base = 0x2000;

    const std::uint64_t l1_base = 0x3000;

    const std::uint64_t l2_base = 0x4000;

    const std::uint64_t l3_base = 0x5000;

    const std::uint64_t page_a = 0x8000;

    const std::uint64_t page_b = 0x9000;



    memory.write_u64(l0_base, l1_base | 0x3ULL);

    memory.write_u64(l1_base, l2_base | 0x3ULL);

    memory.write_u64(l2_base, l3_base | 0x3ULL);



    // Identity-map page 0

    memory.write_u64(l3_base + 0u, 0x0ULL | 0x3ULL | (1ULL << 10u));



    // Map VA 0x1000 with nG=1 (bit 11) to page_a (0x8000)

    memory.write_u64(l3_base + 8u, page_a | 0x3ULL | (1ULL << 10u) | (1ULL << 11u));



    memory.write_u64(page_a, 0x1111111111111111ULL);

    memory.write_u64(page_b, 0x2222222222222222ULL);



    // Set TTBR0 with ASID 1 (bits[63:48] = 0x0001)

    cpu.set_ttbr0_el1(l0_base | (0x0001ULL << 48u));

    cpu.set_sctlr_el1(0x00C50839ULL); // MMU on



    cpu.write_register(2, 0x1000);

    cpu.set_program_counter(0x200);

    phase31_run_one(cpu, memory, 0xF9400041u); // LDR X1, [X2]

    CHECK(cpu.read_register(1) == 0x1111111111111111ULL);



    cpu.write_register(3, 0x0001ULL << 48u); // ASID 1 in register

    phase31_run_one(cpu, memory, 0xD5088743u); // TLBI ASIDE1, X3



    memory.write_u64(l3_base + 8u, page_b | 0x3ULL | (1ULL << 10u) | (1ULL << 11u));



    phase31_run_one(cpu, memory, 0xF9400041u);

    CHECK(cpu.read_register(1) == 0x2222222222222222ULL);

}



void phase38_disassembly() {

    CPU cpu;

    Memory memory(0x10000);

    std::vector<std::string> names;

    cpu.set_trace_sink([&names](const CPU::TraceRecord& r) {

        names.push_back(r.disassembly);

    });



    cpu.write_register(1, 0x2000);

    phase31_run_one(cpu, memory, 0xD5033FBFu); // DMB #15

    phase31_run_one(cpu, memory, 0xD5033F9Fu); // DSB #15

    phase31_run_one(cpu, memory, 0xD5033FDFu); // ISB #15

    phase31_run_one(cpu, memory, 0xD503201Fu); // NOP

    phase31_run_one(cpu, memory, 0xD503203Fu); // YIELD

    phase31_run_one(cpu, memory, 0xD5033F5Fu); // CLREX

    phase31_run_one(cpu, memory, 0xD50B7421u); // DC ZVA, X1

    phase31_run_one(cpu, memory, 0xD50B7A21u); // DC CVAC, X1

    phase31_run_one(cpu, memory, 0xD50B7B21u); // DC CVAU, X1

    phase31_run_one(cpu, memory, 0xD50B7E21u); // DC CIVAC, X1

    phase31_run_one(cpu, memory, 0xD5087621u); // DC IVAC, X1

    phase31_run_one(cpu, memory, 0xD50B7521u); // IC IVAU, X1

    phase31_run_one(cpu, memory, 0xD508751Fu); // IC IALLU

    phase31_run_one(cpu, memory, 0xD508711Fu); // IC IALLUIS

    phase31_run_one(cpu, memory, 0xD53B00E0u); // MRS X0, DCZID_EL0

    phase31_run_one(cpu, memory, 0xC8DFFC20u); // LDAR X0, [X1]

    phase31_run_one(cpu, memory, 0xC89FFC20u); // STLR X0, [X1]

    phase31_run_one(cpu, memory, 0x48DFFC20u); // LDARH W0, [X1]

    phase31_run_one(cpu, memory, 0x089FFC20u); // STLRB W0, [X1]



    const std::vector<std::string> expected = {

        "DMB #15", "DSB #15", "ISB #15", "NOP", "YIELD", "CLREX",

        "DC ZVA, X1", "DC CVAC, X1", "DC CVAU, X1", "DC CIVAC, X1", "DC IVAC, X1",

        "IC IVAU, X1", "IC IALLU", "IC IALLUIS", "MRS X0, DCZID_EL0",

        "LDAR X0, [X1]", "STLR X0, [X1]", "LDARH W0, [X1]", "STLRB W0, [X1]"

    };

    CHECK(names.size() == expected.size());

    for (std::size_t i = 0; i < names.size() && i < expected.size(); ++i) {

        CHECK(names[i] == expected[i]);

    }

}



void phase39_timer_system_registers() {
    CPU cpu;
    Memory memory(0x10000);

    // Initial default values
    CHECK(cpu.get_cntfrq_el0() == 24000000ull); // 24 MHz default
    CHECK(cpu.get_guest_counter() == 0ull);
    CHECK(cpu.get_cntkctl_el1() == 0ull);
    CHECK(cpu.get_phys_timer().enable == false);
    CHECK(cpu.get_phys_timer().imask == false);
    CHECK(cpu.get_phys_timer().istatus == false);
    CHECK(cpu.get_phys_timer().cval == 0xFFFFFFFFFFFFFFFFull);

    // MRS / MSR for CNTFRQ_EL0 (op1=3, CRn=14, CRm=0, op2=0)
    cpu.write_register(0, 50000000ull);
    phase31_run_one(cpu, memory, 0xD51BE000u); // MSR CNTFRQ_EL0, X0
    CHECK(cpu.get_cntfrq_el0() == 50000000ull);
    cpu.write_register(1, 0);
    phase31_run_one(cpu, memory, 0xD53BE001u); // MRS X1, CNTFRQ_EL0
    CHECK(cpu.read_register(1) == 50000000ull);

    // MRS / MSR for CNTKCTL_EL1 (op1=0, CRn=14, CRm=1, op2=0)
    cpu.write_register(0, 0x303ull);
    phase31_run_one(cpu, memory, 0xD518E100u); // MSR CNTKCTL_EL1, X0
    CHECK(cpu.get_cntkctl_el1() == 0x303ull);
    cpu.write_register(1, 0);
    phase31_run_one(cpu, memory, 0xD538E101u); // MRS X1, CNTKCTL_EL1
    CHECK(cpu.read_register(1) == 0x303ull);

    // Physical timer control and compare: CNTP_CTL_EL0 and CNTP_CVAL_EL0
    cpu.write_register(0, 3ull); // ENABLE=1, IMASK=1
    phase31_run_one(cpu, memory, 0xD51BE220u); // MSR CNTP_CTL_EL0, X0
    CHECK(cpu.get_phys_timer().enable == true);
    CHECK(cpu.get_phys_timer().imask == true);

    cpu.write_register(0, 1000ull);
    phase31_run_one(cpu, memory, 0xD51BE240u); // MSR CNTP_CVAL_EL0, X0
    CHECK(cpu.get_phys_timer().cval == 1000ull);
    cpu.write_register(1, 0);
    phase31_run_one(cpu, memory, 0xD53BE241u); // MRS X1, CNTP_CVAL_EL0
    CHECK(cpu.read_register(1) == 1000ull);

    // Virtual timer control and compare: CNTV_CTL_EL0 and CNTV_CVAL_EL0
    cpu.write_register(0, 1ull); // ENABLE=1, IMASK=0
    phase31_run_one(cpu, memory, 0xD51BE320u); // MSR CNTV_CTL_EL0, X0
    CHECK(cpu.get_virt_timer().enable == true);
    CHECK(cpu.get_virt_timer().imask == false);

    cpu.write_register(0, 2000ull);
    phase31_run_one(cpu, memory, 0xD51BE340u); // MSR CNTV_CVAL_EL0, X0
    CHECK(cpu.get_virt_timer().cval == 2000ull);
    cpu.write_register(1, 0);
    phase31_run_one(cpu, memory, 0xD53BE341u); // MRS X1, CNTV_CVAL_EL0
    CHECK(cpu.read_register(1) == 2000ull);

    // Read-only MSR rejected in architectural mode
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x10000);
    cpu.set_program_counter(0x200);
    phase31_run_one(cpu, memory, 0xD51BE020u); // MSR CNTPCT_EL0, X0 (read-only!)
    CHECK(cpu.get_program_counter() == 0x10200); // Synchronous exception vector at EL1h
    CHECK((cpu.get_esr_el1() >> 26u) == 0x00u); // EC = Unknown/Undefined
}

void phase39_tval_cval_conversion() {
    CPU cpu;
    Memory memory(0x10000);

    cpu.set_guest_counter(100ull);
    // Writing positive offset 50 to CNTP_TVAL_EL0 sets CVAL = 100 + 50 = 150
    cpu.write_register(0, 50ull);
    phase31_run_one(cpu, memory, 0xD51BE200u); // MSR CNTP_TVAL_EL0, X0
    CHECK(cpu.get_phys_timer().cval == 150ull);
    CHECK(cpu.get_guest_counter() == 101ull);

    // Reading TVAL now returns CVAL - counter = 150 - 101 = 49
    cpu.write_register(1, 0);
    phase31_run_one(cpu, memory, 0xD53BE201u); // MRS X1, CNTP_TVAL_EL0
    CHECK(cpu.read_register(1) == 49ull);

    // Negative offset: write -20 to TVAL
    // counter before step is 102: CVAL = 102 + (-20) = 82
    cpu.write_register(0, static_cast<std::uint64_t>(static_cast<std::int64_t>(-20)));
    phase31_run_one(cpu, memory, 0xD51BE200u); // MSR CNTP_TVAL_EL0, X0
    CHECK(cpu.get_phys_timer().cval == 82ull);

    // Since counter (103) >= CVAL (82), timer has expired!
    cpu.write_register(0, 1ull); // ENABLE=1
    phase31_run_one(cpu, memory, 0xD51BE220u); // MSR CNTP_CTL_EL0, X0
    CHECK(cpu.get_phys_timer().istatus == true);
}

void phase39_counter_progression() {
    CPU cpu;
    Memory memory(0x10000);

    CHECK(cpu.get_guest_counter() == 0ull);
    // Run 5 NOP instructions
    for (int i = 0; i < 5; ++i) {
        phase31_run_one(cpu, memory, 0xD503201Fu); // NOP
    }
    CHECK(cpu.get_guest_counter() == 5ull);

    // Read CNTPCT_EL0 into X0
    phase31_run_one(cpu, memory, 0xD53BE020u); // MRS X0, CNTPCT_EL0
    CHECK(cpu.read_register(0) == 5ull); // Counter at time of MRS execution was 5
    CHECK(cpu.get_guest_counter() == 6ull); // Incremented to 6 after MRS stepped

    // Read CNTVCT_EL0 into X1
    phase31_run_one(cpu, memory, 0xD53BE041u); // MRS X1, CNTVCT_EL0
    CHECK(cpu.read_register(1) == 6ull);
    CHECK(cpu.get_guest_counter() == 7ull);
}

void phase39_interrupt_masking_and_delivery() {
    CPU cpu;
    Memory memory(0x20000);

    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x10000);

    // 1. Interrupt masked by DAIF.I (bit 1 of DAIF = 0x2)
    cpu.set_daif(0x2u); // Mask IRQ
    cpu.set_guest_counter(10ull);
    // Enable timer with CVAL = 10, IMASK = 0
    CPU::TimerState timer{};
    timer.enable = true;
    timer.imask = false;
    timer.cval = 10ull;
    cpu.set_phys_timer(timer);

    cpu.set_program_counter(0x500);
    memory.write_u32(0x500, 0xD503201Fu); // NOP
    cpu.step(memory);
    // IRQ was pending, but masked by DAIF.I -> NOP executed, PC advanced to 0x504
    CHECK(cpu.get_program_counter() == 0x504);

    // 2. Unmask IRQ (DAIF = 0) from EL1h
    cpu.set_daif(0x0u);
    memory.write_u32(0x504, 0xD503201Fu); // NOP
    cpu.step(memory);
    // IRQ taken before instruction execution!
    // From EL1h (SPx), IRQ vector is VBAR + 0x280
    CHECK(cpu.get_program_counter() == 0x10280);
    CHECK(cpu.get_elr_el1() == 0x504);
    CHECK(cpu.get_daif() == 0xFu); // All interrupts masked upon entry
    CHECK(cpu.get_current_el() == 1);
    CHECK(cpu.get_sp_sel() == true);

    // 3. IRQ from EL0
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_daif(0x0u);
    cpu.set_program_counter(0x800);
    memory.write_u32(0x800, 0xD503201Fu); // NOP
    cpu.step(memory);
    // From EL0 (lower EL, AArch64), IRQ vector is VBAR + 0x480
    CHECK(cpu.get_program_counter() == 0x10480);
    CHECK(cpu.get_elr_el1() == 0x800);
    CHECK(cpu.get_current_el() == 1); // Raised to EL1
    CHECK((cpu.get_spsr_el1() & 0x1Fu) == 0x0u); // Interrupted EL was EL0t
}

void phase39_hints_wfi_wfe_sev() {
    CPU cpu;
    Memory memory(0x10000);

    CHECK(cpu.get_event_register() == false);

    // SEV sets event register
    phase31_run_one(cpu, memory, 0xD503209Fu); // SEV
    CHECK(cpu.get_event_register() == true);

    // WFE clears event register if set
    phase31_run_one(cpu, memory, 0xD503205Fu); // WFE
    CHECK(cpu.get_event_register() == false);

    // SEVL sets event register
    phase31_run_one(cpu, memory, 0xD50320BFu); // SEVL
    CHECK(cpu.get_event_register() == true);

    // WFE clears it again
    phase31_run_one(cpu, memory, 0xD503205Fu); // WFE
    CHECK(cpu.get_event_register() == false);

    // WFI with no pending interrupt advances PC to PC+4
    cpu.set_program_counter(0x300);
    phase31_run_one(cpu, memory, 0xD503207Fu); // WFI
    CHECK(cpu.get_program_counter() == 0x304);
}

void phase39_disassembly_names() {
    CPU cpu;
    Memory memory(0x10000);
    std::vector<std::string> names;
    cpu.set_trace_sink([&names](const CPU::TraceRecord& r) {
        names.push_back(r.disassembly);
    });

    phase31_run_one(cpu, memory, 0xD53BE000u); // MRS X0, CNTFRQ_EL0
    phase31_run_one(cpu, memory, 0xD51BE000u); // MSR CNTFRQ_EL0, X0
    phase31_run_one(cpu, memory, 0xD53BE021u); // MRS X1, CNTPCT_EL0
    phase31_run_one(cpu, memory, 0xD53BE042u); // MRS X2, CNTVCT_EL0
    phase31_run_one(cpu, memory, 0xD538E103u); // MRS X3, CNTKCTL_EL1
    phase31_run_one(cpu, memory, 0xD518E103u); // MSR CNTKCTL_EL1, X3
    phase31_run_one(cpu, memory, 0xD53BE224u); // MRS X4, CNTP_CTL_EL0
    phase31_run_one(cpu, memory, 0xD51BE224u); // MSR CNTP_CTL_EL0, X4
    phase31_run_one(cpu, memory, 0xD53BE245u); // MRS X5, CNTP_CVAL_EL0
    phase31_run_one(cpu, memory, 0xD51BE245u); // MSR CNTP_CVAL_EL0, X5
    phase31_run_one(cpu, memory, 0xD53BE206u); // MRS X6, CNTP_TVAL_EL0
    phase31_run_one(cpu, memory, 0xD51BE206u); // MSR CNTP_TVAL_EL0, X6
    phase31_run_one(cpu, memory, 0xD53BE327u); // MRS X7, CNTV_CTL_EL0
    phase31_run_one(cpu, memory, 0xD51BE327u); // MSR CNTV_CTL_EL0, X7
    phase31_run_one(cpu, memory, 0xD53BE348u); // MRS X8, CNTV_CVAL_EL0
    phase31_run_one(cpu, memory, 0xD51BE348u); // MSR CNTV_CVAL_EL0, X8
    phase31_run_one(cpu, memory, 0xD53BE309u); // MRS X9, CNTV_TVAL_EL0
    phase31_run_one(cpu, memory, 0xD51BE309u); // MSR CNTV_TVAL_EL0, X9
    phase31_run_one(cpu, memory, 0xD503205Fu); // WFE
    phase31_run_one(cpu, memory, 0xD503207Fu); // WFI
    phase31_run_one(cpu, memory, 0xD503209Fu); // SEV
    phase31_run_one(cpu, memory, 0xD50320BFu); // SEVL

    const std::vector<std::string> expected = {
        "MRS X0, CNTFRQ_EL0",
        "MSR CNTFRQ_EL0, X0",
        "MRS X1, CNTPCT_EL0",
        "MRS X2, CNTVCT_EL0",
        "MRS X3, CNTKCTL_EL1",
        "MSR CNTKCTL_EL1, X3",
        "MRS X4, CNTP_CTL_EL0",
        "MSR CNTP_CTL_EL0, X4",
        "MRS X5, CNTP_CVAL_EL0",
        "MSR CNTP_CVAL_EL0, X5",
        "MRS X6, CNTP_TVAL_EL0",
        "MSR CNTP_TVAL_EL0, X6",
        "MRS X7, CNTV_CTL_EL0",
        "MSR CNTV_CTL_EL0, X7",
        "MRS X8, CNTV_CVAL_EL0",
        "MSR CNTV_CVAL_EL0, X8",
        "MRS X9, CNTV_TVAL_EL0",
        "MSR CNTV_TVAL_EL0, X9",
        "WFE",
        "WFI",
        "SEV",
        "SEVL"
    };

    CHECK(names.size() == expected.size());
    for (std::size_t i = 0; i < names.size() && i < expected.size(); ++i) {
        CHECK(names[i] == expected[i]);
    }
}


void phase40_el2_system_registers() {
    CPU cpu;
    Memory memory(0x40000);
    cpu.set_current_el(2);

    // CurrentEL at EL2 reads 0x8
    phase31_run_one(cpu, memory, 0xD5384240u); // MRS X0, CurrentEL
    CHECK(cpu.read_register(0) == 0x8u);

    // SPSR_EL2
    cpu.write_register(1, 0x3C5ull);
    phase31_run_one(cpu, memory, 0xD51C4001u); // MSR SPSR_EL2, X1
    CHECK(cpu.get_spsr_el2() == 0x3C5u);
    cpu.write_register(0, 0);
    phase31_run_one(cpu, memory, 0xD53C4000u); // MRS X0, SPSR_EL2
    CHECK(cpu.read_register(0) == 0x3C5u);

    // ELR_EL2
    cpu.write_register(1, 0x12345678ULL);
    phase31_run_one(cpu, memory, 0xD51C4021u); // MSR ELR_EL2, X1
    CHECK(cpu.get_elr_el2() == 0x12345678ULL);
    phase31_run_one(cpu, memory, 0xD53C4020u); // MRS X0, ELR_EL2
    CHECK(cpu.read_register(0) == 0x12345678ULL);

    // ESR_EL2
    cpu.write_register(1, 0x5A000042u);
    phase31_run_one(cpu, memory, 0xD51C5201u); // MSR ESR_EL2, X1
    CHECK(cpu.get_esr_el2() == 0x5A000042u);
    phase31_run_one(cpu, memory, 0xD53C5200u); // MRS X0, ESR_EL2
    CHECK(cpu.read_register(0) == 0x5A000042u);

    // FAR_EL2
    cpu.write_register(1, 0xDEADBEEF000ULL);
    phase31_run_one(cpu, memory, 0xD51C6001u); // MSR FAR_EL2, X1
    CHECK(cpu.get_far_el2() == 0xDEADBEEF000ULL);
    phase31_run_one(cpu, memory, 0xD53C6000u); // MRS X0, FAR_EL2
    CHECK(cpu.read_register(0) == 0xDEADBEEF000ULL);

    // HPFAR_EL2
    cpu.write_register(1, 0x0000123456789000ULL);
    phase31_run_one(cpu, memory, 0xD51C6081u); // MSR HPFAR_EL2, X1
    CHECK(cpu.get_hpfar_el2() == 0x0000123456789000ULL);
    phase31_run_one(cpu, memory, 0xD53C6080u); // MRS X0, HPFAR_EL2
    CHECK(cpu.read_register(0) == 0x0000123456789000ULL);

    // VBAR_EL2
    cpu.write_register(1, 0x80000000ULL);
    phase31_run_one(cpu, memory, 0xD51CC001u); // MSR VBAR_EL2, X1
    CHECK(cpu.get_vbar_el2() == 0x80000000ULL);
    phase31_run_one(cpu, memory, 0xD53CC000u); // MRS X0, VBAR_EL2
    CHECK(cpu.read_register(0) == 0x80000000ULL);

    // HCR_EL2
    cpu.write_register(1, 0x80000001ULL);
    phase31_run_one(cpu, memory, 0xD51C1101u); // MSR HCR_EL2, X1
    CHECK(cpu.get_hcr_el2() == 0x80000001ULL);
    phase31_run_one(cpu, memory, 0xD53C1100u); // MRS X0, HCR_EL2
    CHECK(cpu.read_register(0) == 0x80000001ULL);

    // VTTBR_EL2
    cpu.write_register(1, 0x0002000010004000ULL);
    phase31_run_one(cpu, memory, 0xD51C2101u); // MSR VTTBR_EL2, X1
    CHECK(cpu.get_vttbr_el2() == 0x0002000010004000ULL);
    phase31_run_one(cpu, memory, 0xD53C2100u); // MRS X0, VTTBR_EL2
    CHECK(cpu.read_register(0) == 0x0002000010004000ULL);

    // VTCR_EL2
    cpu.write_register(1, 0x80003550ULL);
    phase31_run_one(cpu, memory, 0xD51C2141u); // MSR VTCR_EL2, X1
    CHECK(cpu.get_vtcr_el2() == 0x80003550ULL);
    phase31_run_one(cpu, memory, 0xD53C2140u); // MRS X0, VTCR_EL2
    CHECK(cpu.read_register(0) == 0x80003550ULL);

    // CNTVOFF_EL2
    cpu.write_register(1, 1000ull);
    phase31_run_one(cpu, memory, 0xD51CE061u); // MSR CNTVOFF_EL2, X1
    CHECK(cpu.get_cntvoff_el2() == 1000ull);
    phase31_run_one(cpu, memory, 0xD53CE060u); // MRS X0, CNTVOFF_EL2
    CHECK(cpu.read_register(0) == 1000ull);

    // CNTHCTL_EL2
    cpu.write_register(1, 0x3ull);
    phase31_run_one(cpu, memory, 0xD51CE101u); // MSR CNTHCTL_EL2, X1
    CHECK(cpu.get_cnthctl_el2() == 0x3ull);
    phase31_run_one(cpu, memory, 0xD53CE100u); // MRS X0, CNTHCTL_EL2
    CHECK(cpu.read_register(0) == 0x3ull);

    // CNTHP_CTL_EL2, CNTHP_CVAL_EL2, CNTHP_TVAL_EL2
    cpu.write_register(1, 3ull); // ENABLE=1, IMASK=1
    phase31_run_one(cpu, memory, 0xD51CE221u); // MSR CNTHP_CTL_EL2, X1
    CHECK(cpu.get_cnthp_timer().enable == true);
    CHECK(cpu.get_cnthp_timer().imask == true);
    phase31_run_one(cpu, memory, 0xD53CE220u); // MRS X0, CNTHP_CTL_EL2
    CHECK(cpu.read_register(0) == 3ull);

    cpu.write_register(1, 5000ull);
    phase31_run_one(cpu, memory, 0xD51CE241u); // MSR CNTHP_CVAL_EL2, X1
    CHECK(cpu.get_cnthp_timer().cval == 5000ull);
    phase31_run_one(cpu, memory, 0xD53CE240u); // MRS X0, CNTHP_CVAL_EL2
    CHECK(cpu.read_register(0) == 5000ull);

    // EL1 accessing EL2 registers trapped to Undefined Instruction
    cpu.set_hcr_el2(0);
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x10000);
    cpu.set_program_counter(0x200);
    phase31_run_one(cpu, memory, 0xD53C1100u); // MRS X0, HCR_EL2 at EL1
    CHECK(cpu.get_program_counter() == 0x10200);
    CHECK((cpu.get_esr_el1() >> 26u) == 0x00u);
}

void phase40_banked_stack_pointers() {
    CPU cpu;
    Memory memory(0x10000);
    cpu.set_current_el(2);

    // Banked stack pointer switching
    cpu.set_sp_el0(0x1000);
    cpu.set_sp_el1(0x2000);
    cpu.set_sp_el2(0x3000);
    cpu.set_sp_sel(true); // SP_EL2
    CHECK(cpu.get_stack_pointer() == 0x3000);

    // MSR SPSel, #0 selects SP_EL0
    phase31_run_one(cpu, memory, 0xD50040BFu); // MSR SPSel, #0
    CHECK(cpu.get_sp_sel() == false);
    CHECK(cpu.get_stack_pointer() == 0x1000);

    // MSR SPSel, #1 selects SP_EL2
    phase31_run_one(cpu, memory, 0xD50041BFu); // MSR SPSel, #1
    CHECK(cpu.get_sp_sel() == true);
    CHECK(cpu.get_stack_pointer() == 0x3000);

    // MRS / MSR SP_EL1 from EL2
    cpu.write_register(1, 0x5555ull);
    phase31_run_one(cpu, memory, 0xD51C4101u); // MSR SP_EL1, X1
    CHECK(cpu.get_sp_el1() == 0x5555ull);
    phase31_run_one(cpu, memory, 0xD53C4100u); // MRS X0, SP_EL1
    CHECK(cpu.read_register(0) == 0x5555ull);
}

void phase40_hvc_and_eret() {
    CPU cpu;
    Memory memory(0x40000);
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el2(0x20000);

    // HVC #42 from EL1
    cpu.set_current_el(1);
    cpu.set_program_counter(0x1000);
    phase31_run_one(cpu, memory, 0xD4000542u); // HVC #42
    CHECK(cpu.get_current_el() == 2);
    CHECK(cpu.get_program_counter() == 0x20400); // lower EL vector
    CHECK(cpu.get_elr_el2() == 0x1004); // preferred return PC+4
    CHECK(cpu.get_esr_el2() == 0x5A00002Au); // EC=0x16, IL=1, imm16=42

    // ERET from EL2 returning to EL1
    cpu.set_spsr_el2(0x3C5u); // EL1h, DAIF masked
    cpu.set_elr_el2(0x5000);
    phase31_run_one(cpu, memory, 0xD69F03E0u); // ERET
    CHECK(cpu.get_current_el() == 1);
    CHECK(cpu.get_sp_sel() == true);
    CHECK(cpu.get_program_counter() == 0x5000);
}

void phase40_hcr_exception_routing() {
    CPU cpu;
    Memory memory(0x40000);
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el2(0x20000);

    // HCR_EL2.TGE=1 routes EL0 SVC to EL2
    cpu.set_hcr_el2(1ULL << 27u); // TGE=1
    cpu.set_current_el(0);
    cpu.set_program_counter(0x100);
    phase31_run_one(cpu, memory, 0xD4000001u); // SVC #0
    CHECK(cpu.get_current_el() == 2);
    CHECK(cpu.get_program_counter() == 0x20400);
    CHECK((cpu.get_esr_el2() >> 26u) == 0x15u); // EC=SVC64

    // HCR_EL2.IMO=1 routes physical IRQ from EL1 to EL2
    cpu.set_current_el(1);
    cpu.set_daif(0); // IRQ unmasked
    cpu.set_hcr_el2(1ULL << 4u); // IMO=1
    cpu.set_phys_timer(CPU::TimerState{true, false, true, 0}); // trigger IRQ
    cpu.set_program_counter(0x500);
    memory.write_u32(0x500, 0xD503201Fu); // NOP
    cpu.step(memory);
    CHECK(cpu.get_current_el() == 2);
    CHECK(cpu.get_program_counter() == 0x20480); // lower EL IRQ vector
}

void phase40_stage2_translation_and_faults() {
    CPU cpu;
    Memory memory(0x100000);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);

    // Identity Stage-2 setup: VTTBR -> 0x4000 (L1) -> 0x5000 (L2) -> 0x6000 (L3)
    cpu.set_hcr_el2(0x80000001ULL); // RW=1, VM=1
    cpu.set_vtcr_el2(0x80003550ULL);
    cpu.set_vttbr_el2(0x0001000000004000ULL);
    cpu.set_vbar_el2(0x20000);

    // L1 table descriptor pointing to 0x5000
    memory.write_u64(0x4000, 0x5003ULL);
    // L2 table descriptor pointing to 0x6000
    memory.write_u64(0x5000, 0x6003ULL);
    // L3 page 0 (PC=0..0xFFF): RW, AF=1
    memory.write_u64(0x6000, 0x04FDULL);

    // 1. Successful read through Stage 2
    memory.write_u64(0x800, 0x1122334455667788ULL);
    cpu.write_register(1, 0x800);
    cpu.set_program_counter(0);
    phase31_run_one(cpu, memory, 0xF9400020u); // LDR X0, [X1]
    CHECK(cpu.read_register(0) == 0x1122334455667788ULL);

    // 2. Unmapped IPA (Page 2 at 0x2000, entry at 0x6010 is 0) triggers Stage-2 fault to EL2
    cpu.write_register(1, 0x2000);
    cpu.set_program_counter(4);
    memory.write_u32(4, 0xF9400020u); // LDR X0, [X1]
    cpu.step(memory);
    CHECK(cpu.get_current_el() == 2);
    CHECK(cpu.get_program_counter() == 0x20400); // lower EL sync vector
    CHECK(cpu.get_esr_el2() == 0x92000007u); // Data Abort L3 translation fault
    CHECK(cpu.get_far_el2() == 0x2000u);
    CHECK(cpu.get_hpfar_el2() == 0x20u); // 0x2000 >> 8 = 0x20
}

void phase40_disassembly_names() {
    CPU cpu;
    Memory memory(0x10000);
    cpu.set_current_el(2);
    std::vector<std::string> names;
    cpu.set_trace_sink([&names](const CPU::TraceRecord& r) {
        names.push_back(r.disassembly);
    });

    phase31_run_one(cpu, memory, 0xD4000542u); // HVC #42
    phase31_run_one(cpu, memory, 0xD53C4000u); // MRS X0, SPSR_EL2
    phase31_run_one(cpu, memory, 0xD51C4000u); // MSR SPSR_EL2, X0
    phase31_run_one(cpu, memory, 0xD53C4020u); // MRS X0, ELR_EL2
    phase31_run_one(cpu, memory, 0xD51C4020u); // MSR ELR_EL2, X0
    phase31_run_one(cpu, memory, 0xD53C5200u); // MRS X0, ESR_EL2
    phase31_run_one(cpu, memory, 0xD51C5200u); // MSR ESR_EL2, X0
    phase31_run_one(cpu, memory, 0xD53C6000u); // MRS X0, FAR_EL2
    phase31_run_one(cpu, memory, 0xD51C6000u); // MSR FAR_EL2, X0
    phase31_run_one(cpu, memory, 0xD53C6080u); // MRS X0, HPFAR_EL2
    phase31_run_one(cpu, memory, 0xD51C6080u); // MSR HPFAR_EL2, X0
    phase31_run_one(cpu, memory, 0xD53CC000u); // MRS X0, VBAR_EL2
    phase31_run_one(cpu, memory, 0xD51CC000u); // MSR VBAR_EL2, X0
    phase31_run_one(cpu, memory, 0xD53C1100u); // MRS X0, HCR_EL2
    phase31_run_one(cpu, memory, 0xD51C1100u); // MSR HCR_EL2, X0
    phase31_run_one(cpu, memory, 0xD53C2100u); // MRS X0, VTTBR_EL2
    phase31_run_one(cpu, memory, 0xD51C2100u); // MSR VTTBR_EL2, X0
    phase31_run_one(cpu, memory, 0xD53C2140u); // MRS X0, VTCR_EL2
    phase31_run_one(cpu, memory, 0xD51C2140u); // MSR VTCR_EL2, X0
    phase31_run_one(cpu, memory, 0xD53CE060u); // MRS X0, CNTVOFF_EL2
    phase31_run_one(cpu, memory, 0xD51CE060u); // MSR CNTVOFF_EL2, X0
    phase31_run_one(cpu, memory, 0xD53CE100u); // MRS X0, CNTHCTL_EL2
    phase31_run_one(cpu, memory, 0xD51CE100u); // MSR CNTHCTL_EL2, X0
    phase31_run_one(cpu, memory, 0xD53CE220u); // MRS X0, CNTHP_CTL_EL2
    phase31_run_one(cpu, memory, 0xD51CE220u); // MSR CNTHP_CTL_EL2, X0
    phase31_run_one(cpu, memory, 0xD53CE240u); // MRS X0, CNTHP_CVAL_EL2
    phase31_run_one(cpu, memory, 0xD51CE240u); // MSR CNTHP_CVAL_EL2, X0
    phase31_run_one(cpu, memory, 0xD53CE200u); // MRS X0, CNTHP_TVAL_EL2
    phase31_run_one(cpu, memory, 0xD51CE200u); // MSR CNTHP_TVAL_EL2, X0

    const std::vector<std::string> expected = {
        "HVC #42",
        "MRS X0, SPSR_EL2",
        "MSR SPSR_EL2, X0",
        "MRS X0, ELR_EL2",
        "MSR ELR_EL2, X0",
        "MRS X0, ESR_EL2",
        "MSR ESR_EL2, X0",
        "MRS X0, FAR_EL2",
        "MSR FAR_EL2, X0",
        "MRS X0, HPFAR_EL2",
        "MSR HPFAR_EL2, X0",
        "MRS X0, VBAR_EL2",
        "MSR VBAR_EL2, X0",
        "MRS X0, HCR_EL2",
        "MSR HCR_EL2, X0",
        "MRS X0, VTTBR_EL2",
        "MSR VTTBR_EL2, X0",
        "MRS X0, VTCR_EL2",
        "MSR VTCR_EL2, X0",
        "MRS X0, CNTVOFF_EL2",
        "MSR CNTVOFF_EL2, X0",
        "MRS X0, CNTHCTL_EL2",
        "MSR CNTHCTL_EL2, X0",
        "MRS X0, CNTHP_CTL_EL2",
        "MSR CNTHP_CTL_EL2, X0",
        "MRS X0, CNTHP_CVAL_EL2",
        "MSR CNTHP_CVAL_EL2, X0",
        "MRS X0, CNTHP_TVAL_EL2",
        "MSR CNTHP_TVAL_EL2, X0"
    };

    CHECK(names.size() == expected.size());
    for (std::size_t i = 0; i < names.size() && i < expected.size(); ++i) {
        CHECK(names[i] == expected[i]);
    }
}

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



    unallocated_register_offset_option_is_rejected();

    load_store_pair_offset_pre_post_indexing();

    load_store_pair_signed_imm7_scaling_and_boundaries();

    load_store_pair_sp_and_zero_register_semantics();

    load_store_pair_width_and_zero_extension();

    load_store_pair_ordering_and_overlap_policy();

    load_store_pair_memory_boundaries_and_unaligned();

    load_store_pair_fault_atomicity_and_address_overflow();

    conditional_compare_register_forms();

    conditional_compare_immediate_forms();

    conditional_select_instructions_and_width();

    conditional_aliases_equivalence();

    condition_codes_evaluation_and_nv_semantics();

    unsupported_conditional_encodings_are_rejected();

    bitfield_ubfm_extract_and_insert_forms();

    bitfield_sbfm_extract_and_sign_extension();

    bitfield_sbfm_insert_and_sign_extension();

    bitfield_bfm_destination_preservation();

    bitfield_register_31_and_boundaries();

    unsupported_bitfield_encodings_are_rejected();

    logical_shifted_register_basic_and_widths();

    logical_shifted_register_shift_types();

    logical_shifted_register_aliases_and_zr();

    logical_immediate_decoding_and_element_sizes();

    logical_immediate_aliases_and_flags();

    unsupported_logical_encodings_are_rejected();

    multiply_32_bit_madd_msub_mul_mneg();

    multiply_64_bit_madd_msub_mul_mneg();

    multiply_widening_signed_smaddl_smsubl_smull_smnegl();

    multiply_widening_unsigned_umaddl_umsubl_umull_umnegl();

    multiply_high_smulh_umulh();

    multiply_register_31_flags_and_unallocated();

    division_32_and_64_bit();

    variable_shifts_lslv_lsrv_asrv_rorv();

    bitfield_extract_extr();

    bit_manipulation_rbit_rev_clz_cls();

    phase27_register_31_flags_and_unallocated();

    branch_unconditional_immediate_b_and_bl();

    branch_unconditional_register_br_blr_ret();

    branch_register_misaligned_target_rejected();

    branch_compare_and_branch_cbz_cbnz();

    branch_test_bit_and_branch_tbz_tbnz();

    branch_conditional_b_cond_all_conditions_and_nv();

    branch_unallocated_encodings_and_fault_atomicity();



    sign_extending_byte_loads_ldrsb();

    sign_extending_halfword_loads_ldrsh();

    sign_extending_word_loads_ldrsw();

    sign_extending_addressing_modes();

    pc_relative_literal_loads();

    phase29_register_31_flags_and_unallocated();



    scalar_byte_loads_and_stores();

    scalar_halfword_loads_and_stores();

    load_pair_signed_words_ldpsw();

    phase30_prfm_and_unallocated_encodings_rejected();

    phase30_flags_and_stack_pointer();



    phase31_exclusive_loads_all_widths();

    phase31_exclusive_load_acquire_variants();

    phase31_exclusive_store_success_and_failure();

    phase31_exclusive_store_address_and_size_mismatch();

    phase31_exclusive_monitor_invalidation_by_normal_stores();

    phase31_exclusive_monitor_replacement_and_reset();

    phase31_exclusive_pair_loads();

    phase31_exclusive_pair_stores_and_failure_atomicity();

    phase31_exclusive_fault_atomicity();

    phase31_exclusive_zero_register_sp_and_overlap();

    phase31_exclusive_unallocated_encodings();

    phase31_lse_add_all_widths();

    phase31_lse_bitwise_operations();

    phase31_lse_signed_and_unsigned_minmax();

    phase31_lse_swap();

    phase31_lse_ordering_variants();

    phase31_lse_zero_register_sp_and_source_target_overlap();

    phase31_lse_faults_and_unallocated();

    phase31_lse_boundary_and_unaligned();

    phase31_disassembly_names();



    phase32_register_state_and_upper_zeroing();

    phase32_fp_simd_loads_stores();

    phase32_scalar_fp_arithmetic_and_movement();

    phase32_scalar_fp_compare();

    phase32_fp_conversion_and_rounding();

    phase32_system_registers_fpcr_fpsr();

    phase32_advanced_simd_operations();

    phase32_disassembly_names();



    phase33_simd_integer_arithmetic_mul_sat_minmax_addp();

    phase33_simd_vector_comparisons();

    phase33_simd_shifts();

    phase33_simd_widening_and_narrowing();

    phase33_simd_reductions_and_misc();

    phase33_disassembly_names();



    phase34_simd_fp_arithmetic();

    phase34_simd_fp_unary();

    phase34_simd_fp_comparisons();

    phase34_simd_fp_pairwise_minmax();

    phase34_simd_fp_conversions();

    phase34_unallocated_and_disassembly();



    phase35_system_registers_mrs_msr();

    phase35_msr_immediate();

    phase35_el_foundation_and_sp_selection();

    phase35_svc_exception_entry();

    phase35_eret_exception_return();

    phase35_privilege_violations();

    phase35_disassembly_names();



    phase36_synchronous_exception_model();

    phase36_undefined_instruction_execution();

    phase36_data_abort_alignment_and_range();

    phase36_pc_alignment_fault();

    phase36_exception_vectors_and_privilege();

    phase36_eret_recovery_and_illegal_state();

    phase36_failure_atomicity();

    phase37_mmu_system_registers();

    phase37_page_table_walker_mappings();

    phase37_translation_and_permission_faults();

    phase37_instruction_fetch_and_wxn_faults();

    phase37_tlb_and_tlbi_invalidation();

    phase37_memory_operations_and_failure_atomicity();

    phase38_barriers_hints_clrex();

    phase38_ordered_load_store();

    phase38_dczid_and_dc_zva();

    phase38_cache_and_tlbi_non_global();

    phase38_disassembly();



    phase39_timer_system_registers();
    phase39_tval_cval_conversion();
    phase39_counter_progression();
    phase39_interrupt_masking_and_delivery();
    phase39_hints_wfi_wfe_sev();
    phase39_disassembly_names();

    phase40_el2_system_registers();
    phase40_banked_stack_pointers();
    phase40_hvc_and_eret();
    phase40_hcr_exception_routing();
    phase40_stage2_translation_and_faults();
    phase40_disassembly_names();

    return test::summarize("cpu_tests");

}
// tests/unit/stage197_3_barrier_semantics_tests.cpp
// Campaign 197: Acceptance Tests A, B, and C
// Verifies ARMv8-A DMB, DSB, and ISB barrier semantics, completion behavior,
// and instruction synchronization.

#include <iostream>
#include <vector>
#include <cstdint>
#include <memory>
#include "cpu/cpu.h"
#include "cpu/guest_memory_order.h"
#include "memory/memory.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;

static void write_u32(Memory& mem, std::uint64_t addr, std::uint32_t val) {
    mem.write_u32(addr, val);
}

// Opcode construction:
// MSR / System instruction encoding:
// 1101 0101 0000 0011 0011 <crm:4> <op2:3> 11111
// op2=4: DSB, op2=5: DMB, op2=6: ISB
static std::uint32_t encode_barrier(std::uint32_t op2, std::uint32_t crm, std::uint32_t rt = 31u) {
    return 0xD5033000u | ((crm & 0xFu) << 8) | ((op2 & 0x7u) << 5) | (rt & 0x1Fu);
}

// ============================================================================
// Acceptance Test A: DMB Semantics & CRM Option Matrix
// ============================================================================
static bool test_dmb_crm_matrix_and_semantics() {
    std::cout << "  Testing DMB CRM matrix and semantics (Acceptance Test A)..." << std::endl;
    Memory mem(0x20000);
    CPU cpu;
    cpu.set_core_id(0);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true); // SP_EL1 -> Vector offset for same EL sync exception is 0x200
    cpu.set_architectural_exceptions(true);

    GuestMemoryOrderModel model;
    cpu.set_memory_order_model(&model);

    // 12 Valid CRM options for DMB (ARM DDI 0487 C6.2.79)
    const std::vector<std::pair<std::uint8_t, const char*>> valid_dmb_opts = {
        {std::uint8_t{1}, "OSHLD"}, {std::uint8_t{2}, "OSHST"}, {std::uint8_t{3}, "OSH"},
        {std::uint8_t{5}, "NSHLD"}, {std::uint8_t{6}, "NSHST"}, {std::uint8_t{7}, "NSH"},
        {std::uint8_t{9}, "ISHLD"}, {std::uint8_t{10}, "ISHST"}, {std::uint8_t{11}, "ISH"},
        {std::uint8_t{13}, "LD"},   {std::uint8_t{14}, "ST"},    {std::uint8_t{15}, "SY"}
    };

    std::uint64_t pc = 0x2000;
    std::uint64_t expected_dmb_count = 0;

    for (const auto& [crm, name] : valid_dmb_opts) {
        std::uint32_t insn = encode_barrier(5, crm);
        write_u32(mem, pc, insn);
        cpu.set_program_counter(pc);

        cpu.step(mem);
        ++expected_dmb_count;

        TEST_ASSERT(cpu.get_program_counter() == pc + 4, std::string("DMB ") + name + " PC increment");
        TEST_ASSERT(cpu.get_dmb_count() == expected_dmb_count, "dmb_count_ incremented");
        TEST_ASSERT(cpu.get_dsb_count() == 0, "dsb_count_ remained zero during DMB");
        TEST_ASSERT(cpu.get_isb_count() == 0, "isb_count_ remained zero during DMB");
        pc += 4;
    }

    // Invalid CRM options: 0, 4, 8, 12 must trigger undefined instruction exception
    const std::vector<std::uint8_t> invalid_dmb_opts = {0, 4, 8, 12};
    for (std::uint8_t crm : invalid_dmb_opts) {
        std::uint32_t insn = encode_barrier(5, crm);
        write_u32(mem, pc, insn);
        cpu.set_program_counter(pc);

        // Vector table base for EL1
        cpu.set_vbar_el1(0x80000);
        cpu.step(mem);

        // Exception taken: PC jumps to VBAR_EL1 + 0x200 (Current EL with SP_ELx Synchronous)
        TEST_ASSERT(cpu.get_program_counter() == 0x80200, "Invalid DMB CRM raised synchronous exception");
        TEST_ASSERT(cpu.get_dmb_count() == expected_dmb_count, "Invalid DMB does not increment dmb_count_");
        pc += 4;
    }

    // Verify DMB does NOT drain store buffers
    model.reset();
    model.record_write(0, 0x1000, 4, 0xAABB);
    TEST_ASSERT(model.get_core_state(0).pending_store_count() == 1, "Store buffer has 1 pending store");

    std::uint32_t dmb_sy = encode_barrier(5, 15);
    write_u32(mem, 0x3000, dmb_sy);
    cpu.set_program_counter(0x3000);
    cpu.step(mem);

    TEST_ASSERT(model.get_core_state(0).pending_store_count() == 1, "DMB must NOT drain pending store buffer (only DSB does)");
    return true;
}

// ============================================================================
// Acceptance Test B: DSB Completion Semantics & Buffer Draining
// ============================================================================
static bool test_dsb_completion_and_draining() {
    std::cout << "  Testing DSB completion semantics and buffer draining (Acceptance Test B)..." << std::endl;
    Memory mem(0x20000);
    CPU cpu;
    cpu.set_core_id(0);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true); // SP_EL1 -> Vector offset for same EL sync exception is 0x200
    cpu.set_architectural_exceptions(true);

    GuestMemoryOrderModel model;
    cpu.set_memory_order_model(&model);

    // 12 Valid CRM options for DSB (ARM DDI 0487 C6.2.80)
    const std::vector<std::pair<std::uint8_t, const char*>> valid_dsb_opts = {
        {std::uint8_t{1}, "OSHLD"}, {std::uint8_t{2}, "OSHST"}, {std::uint8_t{3}, "OSH"},
        {std::uint8_t{5}, "NSHLD"}, {std::uint8_t{6}, "NSHST"}, {std::uint8_t{7}, "NSH"},
        {std::uint8_t{9}, "ISHLD"}, {std::uint8_t{10}, "ISHST"}, {std::uint8_t{11}, "ISH"},
        {std::uint8_t{13}, "LD"},   {std::uint8_t{14}, "ST"},    {std::uint8_t{15}, "SY"}
    };

    std::uint64_t pc = 0x4000;
    std::uint64_t expected_dsb_count = 0;

    for (const auto& [crm, name] : valid_dsb_opts) {
        std::uint32_t insn = encode_barrier(4, crm);
        write_u32(mem, pc, insn);
        cpu.set_program_counter(pc);

        cpu.step(mem);
        ++expected_dsb_count;

        TEST_ASSERT(cpu.get_program_counter() == pc + 4, std::string("DSB ") + name + " PC increment");
        TEST_ASSERT(cpu.get_dsb_count() == expected_dsb_count, "dsb_count_ incremented");
        pc += 4;
    }

    // Invalid CRM options: 0, 4, 8, 12 must trigger undefined instruction exception
    const std::vector<std::uint8_t> invalid_dsb_opts = {0, 4, 8, 12};
    for (std::uint8_t crm : invalid_dsb_opts) {
        std::uint32_t insn = encode_barrier(4, crm);
        write_u32(mem, pc, insn);
        cpu.set_program_counter(pc);

        cpu.set_vbar_el1(0x80000);
        cpu.step(mem);

        TEST_ASSERT(cpu.get_program_counter() == 0x80200, "Invalid DSB CRM raised synchronous exception");
        TEST_ASSERT(cpu.get_dsb_count() == expected_dsb_count, "Invalid DSB does not increment dmb_count_");
        pc += 4;
    }

    // Verify DSB DRAINS store buffers immediately (completion semantics)
    model.reset();
    model.record_write(0, 0x1000, 4, 0x12345678);
    model.record_write(0, 0x1004, 4, 0x9ABCDEF0);
    TEST_ASSERT(model.get_core_state(0).pending_store_count() == 2, "Store buffer has 2 pending stores");

    std::uint32_t dsb_sy = encode_barrier(4, 15);
    write_u32(mem, 0x5000, dsb_sy);
    cpu.set_program_counter(0x5000);
    cpu.step(mem);

    TEST_ASSERT(model.get_core_state(0).pending_store_count() == 0, "DSB MUST drain pending store buffer (completion semantics)");
    return true;
}

// ============================================================================
// Acceptance Test C: ISB Instruction Synchronization & Context Flush
// ============================================================================
static bool test_isb_instruction_synchronization() {
    std::cout << "  Testing ISB instruction synchronization and context flush (Acceptance Test C)..." << std::endl;
    Memory mem(0x20000);
    CPU cpu;
    cpu.set_core_id(0);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true); // SP_EL1 -> Vector offset for same EL sync exception is 0x200
    cpu.set_architectural_exceptions(true);

    GuestMemoryOrderModel model;
    cpu.set_memory_order_model(&model);

    // In ARMv8-A DDI 0487 Section C6.2.117:
    // ISB only accepts CRM=15 (0b1111 / SY). All other CRM values are unallocated / undefined!
    std::uint32_t isb_sy = encode_barrier(6, 15);
    write_u32(mem, 0x6000, isb_sy);
    cpu.set_program_counter(0x6000);

    // Populate a TLB entry in MMU
    mmu::MMU& mmu = cpu.get_mmu();
    mmu.set_tlb_enabled(true);
    TEST_ASSERT(mmu.is_tlb_enabled(), "TLB is enabled");

    // Also place stores in model to verify ISB does NOT drain data store buffers
    model.reset();
    model.record_write(0, 0x2000, 4, 0x5555AAAA);
    TEST_ASSERT(model.get_core_state(0).pending_store_count() == 1, "Store buffer has 1 pending store");

    cpu.step(mem);

    TEST_ASSERT(cpu.get_program_counter() == 0x6004, "ISB PC increment");
    TEST_ASSERT(cpu.get_isb_count() == 1, "isb_count_ incremented");
    TEST_ASSERT(cpu.get_dmb_count() == 0, "dmb_count_ remained 0");
    TEST_ASSERT(cpu.get_dsb_count() == 0, "dsb_count_ remained 0");
    TEST_ASSERT(model.get_core_state(0).pending_store_count() == 1, "ISB does NOT drain data store buffers");

    // Test invalid CRM values for ISB (e.g. CRM=0..14)
    for (std::uint8_t crm = 0; crm < 15; ++crm) {
        std::uint32_t invalid_isb = encode_barrier(6, crm);
        write_u32(mem, 0x7000 + crm * 4, invalid_isb);
        cpu.set_program_counter(0x7000 + crm * 4);
        cpu.set_vbar_el1(0x80000);

        cpu.step(mem);
        TEST_ASSERT(cpu.get_program_counter() == 0x80200, "ISB with CRM != 15 raised undefined instruction");
        TEST_ASSERT(cpu.get_isb_count() == 1, "Invalid ISB did not increment isb_count_");
    }

    return true;
}

int main() {
    std::cout << "[STAGE 197.3-197.5] Running DMB, DSB & ISB Architectural Semantics Tests..." << std::endl;

    if (!test_dmb_crm_matrix_and_semantics()) {
        std::cerr << "test_dmb_crm_matrix_and_semantics FAILED" << std::endl;
        return 1;
    }

    if (!test_dsb_completion_and_draining()) {
        std::cerr << "test_dsb_completion_and_draining FAILED" << std::endl;
        return 1;
    }

    if (!test_isb_instruction_synchronization()) {
        std::cerr << "test_isb_instruction_synchronization FAILED" << std::endl;
        return 1;
    }

    std::cout << "[STAGE 197.3-197.5] All DMB, DSB & ISB Architectural Tests PASSED." << std::endl;
    return 0;
}

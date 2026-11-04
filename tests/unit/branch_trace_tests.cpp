#include "cpu/cpu.h"
#include "memory/memory.h"
#include "trace/trace_manager.h"
#include "trace/trace_event.h"
#include "trace/trace_file_reader.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
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

// Branch Encoders
constexpr std::uint32_t enc_b(std::int32_t word_offset) {
    return 0x14000000u | (static_cast<std::uint32_t>(word_offset) & 0x03FFFFFFu);
}

constexpr std::uint32_t enc_bl(std::int32_t word_offset) {
    return 0x94000000u | (static_cast<std::uint32_t>(word_offset) & 0x03FFFFFFu);
}

constexpr std::uint32_t enc_b_cond(unsigned cond, std::int32_t word_offset) {
    return 0x54000000u | ((static_cast<std::uint32_t>(word_offset) & 0x7FFFFu) << 5u) | (cond & 0xFu);
}

constexpr std::uint32_t enc_cbz_w(unsigned rt, std::int32_t word_offset) {
    return 0x34000000u | ((static_cast<std::uint32_t>(word_offset) & 0x7FFFFu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_cbz_x(unsigned rt, std::int32_t word_offset) {
    return 0xB4000000u | ((static_cast<std::uint32_t>(word_offset) & 0x7FFFFu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_cbnz_w(unsigned rt, std::int32_t word_offset) {
    return 0x35000000u | ((static_cast<std::uint32_t>(word_offset) & 0x7FFFFu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_cbnz_x(unsigned rt, std::int32_t word_offset) {
    return 0xB5000000u | ((static_cast<std::uint32_t>(word_offset) & 0x7FFFFu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_tbz(unsigned rt, unsigned bit, std::int32_t word_offset) {
    return 0x36000000u | (((bit >> 5u) & 1u) << 31u) | ((bit & 0x1Fu) << 19u) |
           ((static_cast<std::uint32_t>(word_offset) & 0x3FFFu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_tbnz(unsigned rt, unsigned bit, std::int32_t word_offset) {
    return 0x37000000u | (((bit >> 5u) & 1u) << 31u) | ((bit & 0x1Fu) << 19u) |
           ((static_cast<std::uint32_t>(word_offset) & 0x3FFFu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_br(unsigned rn) {
    return 0xD61F0000u | ((rn & 0x1Fu) << 5u);
}

constexpr std::uint32_t enc_blr(unsigned rn) {
    return 0xD63F0000u | ((rn & 0x1Fu) << 5u);
}

constexpr std::uint32_t enc_ret(unsigned rn = 30) {
    return 0xD65F0000u | ((rn & 0x1Fu) << 5u);
}

constexpr std::uint32_t enc_nop() {
    return 0xD503201Fu;
}

constexpr std::uint32_t enc_movz(unsigned rd, std::uint16_t imm16, unsigned shift = 0) {
    return 0xD2800000u | ((shift / 16u) << 21u) | (static_cast<std::uint32_t>(imm16) << 5u) | (rd & 0x1Fu);
}

// -----------------------------------------------------------------------------
// Unconditional Branch Tests (Tests 1-5)
// -----------------------------------------------------------------------------
void test_1_b_emits_retirement_and_branch() {
    std::cout << "  Test 1: B taken emits retirement and branch events... ";
    Memory memory(0x10000);
    CPU cpu;
    write_instruction(memory, 0x1000, enc_b(4)); // Branch to 0x1010 (+16 bytes)
    cpu.set_program_counter(0x1000);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 64;
    cfg.event_types.set(TraceEventType::ExceptionEntered, false);
    cfg.event_types.set(TraceEventType::ExceptionReturned, false);
    cfg.event_types.set(TraceEventType::DebugEvent, false);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    cpu.bind_trace_manager(&mgr);
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0x1010);
    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_accepted == 2);

    TraceEvent ev_ret{}, ev_br{};
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev_ret));
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev_br));

    // Event 1: InstructionRetired
    TEST_ASSERT(ev_ret.get_event_type() == TraceEventType::InstructionRetired);
    TEST_ASSERT(ev_ret.pc == 0x1000);
    TEST_ASSERT(ev_ret.sequence_number == 1);

    // Event 2: BranchTaken
    TEST_ASSERT(ev_br.get_event_type() == TraceEventType::BranchTaken);
    TEST_ASSERT(ev_br.pc == 0x1000);
    TEST_ASSERT(ev_br.address == 0x1010);
    TEST_ASSERT(ev_br.sequence_number == 2);
    TEST_ASSERT(ev_br.has_flag(TraceEventFlags::IsTaken));
    TEST_ASSERT(ev_br.has_flag(TraceEventFlags::HasVirtualAddress));
    TEST_ASSERT(ev_br.get_address_space() == TraceAddressSpace::Virtual);

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

void test_2_and_3_bl_events_and_link_register() {
    std::cout << "  Tests 2 & 3: BL emits both events and updates link register... ";
    Memory memory(0x10000);
    CPU cpu;
    write_instruction(memory, 0x2000, enc_bl(8)); // BL to 0x2020 (+32 bytes)
    cpu.set_program_counter(0x2000);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 64;
    cfg.event_types.set(TraceEventType::ExceptionEntered, false);
    cfg.event_types.set(TraceEventType::ExceptionReturned, false);
    cfg.event_types.set(TraceEventType::DebugEvent, false);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    cpu.bind_trace_manager(&mgr);
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0x2020);
    TEST_ASSERT(cpu.read_register(30) == 0x2004); // LR updated to PC + 4

    TraceEvent ev_ret{}, ev_br{};
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev_ret));
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev_br));

    TEST_ASSERT(ev_ret.get_event_type() == TraceEventType::InstructionRetired);
    TEST_ASSERT(ev_ret.pc == 0x2000);

    TEST_ASSERT(ev_br.get_event_type() == TraceEventType::BranchTaken);
    TEST_ASSERT(ev_br.pc == 0x2000);
    TEST_ASSERT(ev_br.address == 0x2020);
    TEST_ASSERT(ev_br.has_flag(TraceEventFlags::IsTaken));

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

void test_4_and_5_branch_target_and_pc_recording() {
    std::cout << "  Tests 4 & 5: Branch target and PC accurately recorded on backward branch... ";
    Memory memory(0x10000);
    CPU cpu;
    // Backward branch: at 0x3020 jump to 0x3000 (-8 words = -32 bytes)
    write_instruction(memory, 0x3020, enc_b(-8));
    cpu.set_program_counter(0x3020);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 64;
    cfg.event_types.set(TraceEventType::ExceptionEntered, false);
    cfg.event_types.set(TraceEventType::ExceptionReturned, false);
    cfg.event_types.set(TraceEventType::DebugEvent, false);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    cpu.bind_trace_manager(&mgr);
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0x3000);

    TraceEvent ev_ret{}, ev_br{};
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev_ret));
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev_br));

    TEST_ASSERT(ev_br.pc == 0x3020);
    TEST_ASSERT(ev_br.address == 0x3000);
    TEST_ASSERT(ev_br.opcode_or_syndrome == enc_b(-8));

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Conditional Branch Tests (Tests 6-9)
// -----------------------------------------------------------------------------
void test_6_and_7_b_cond_taken_vs_not_taken() {
    std::cout << "  Tests 6 & 7: B.cond taken emits branch event, not taken emits only retirement... ";
    Memory memory(0x10000);
    CPU cpu;

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 64;
    cfg.event_types.set(TraceEventType::ExceptionEntered, false);
    cfg.event_types.set(TraceEventType::ExceptionReturned, false);
    cfg.event_types.set(TraceEventType::DebugEvent, false);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());
    cpu.bind_trace_manager(&mgr);

    // Case 1: B.EQ taken (Z=1)
    write_instruction(memory, 0x4000, enc_b_cond(0, 10)); // target 0x4028
    cpu.set_program_counter(0x4000);
    cpu.set_nzcv(0x4); // Z=1
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0x4028);
    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_accepted == 2);

    TraceEvent ev_ret{}, ev_br{};
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev_ret));
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev_br));
    TEST_ASSERT(ev_ret.get_event_type() == TraceEventType::InstructionRetired);
    TEST_ASSERT(ev_br.get_event_type() == TraceEventType::BranchTaken);
    TEST_ASSERT(ev_br.address == 0x4028);

    // Case 2: B.EQ not taken (Z=0)
    write_instruction(memory, 0x4028, enc_b_cond(0, 10)); // target 0x4050
    cpu.set_nzcv(0x0); // Z=0
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0x402C); // Advanced PC+4
    stats = mgr.statistics();
    TEST_ASSERT(stats.events_accepted == 3); // 2 from previous + 1 retirement

    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev_ret));
    TEST_ASSERT(ev_ret.get_event_type() == TraceEventType::InstructionRetired);
    TEST_ASSERT(ev_ret.pc == 0x4028);
    // Ensure no BranchTaken event was emitted
    TEST_ASSERT(mgr.ring_buffer()->empty());

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

void test_8_and_9_multiple_condition_codes_and_nzcv_preservation() {
    std::cout << "  Tests 8 & 9: Multiple condition codes evaluated and NZCV preserved... ";
    Memory memory(0x10000);
    CPU cpu;

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 64;
    cfg.event_types.set(TraceEventType::ExceptionEntered, false);
    cfg.event_types.set(TraceEventType::ExceptionReturned, false);
    cfg.event_types.set(TraceEventType::DebugEvent, false);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());
    cpu.bind_trace_manager(&mgr);

    // Test NE (cond 1) with Z=0 -> taken
    write_instruction(memory, 0x5000, enc_b_cond(1, 4));
    cpu.set_program_counter(0x5000);
    cpu.set_nzcv(0x8); // N=1, Z=0, C=0, V=0
    cpu.step(memory);
    TEST_ASSERT(cpu.get_program_counter() == 0x5010);
    TEST_ASSERT(cpu.get_nzcv() == 0x8); // NZCV intact

    // Test CS/HS (cond 2) with C=1 -> taken
    write_instruction(memory, 0x5010, enc_b_cond(2, 4));
    cpu.set_nzcv(0x2); // C=1
    cpu.step(memory);
    TEST_ASSERT(cpu.get_program_counter() == 0x5020);
    TEST_ASSERT(cpu.get_nzcv() == 0x2);

    // Test MI (cond 4) with N=0 -> NOT taken
    write_instruction(memory, 0x5020, enc_b_cond(4, 4));
    cpu.set_nzcv(0x0); // N=0
    cpu.step(memory);
    TEST_ASSERT(cpu.get_program_counter() == 0x5024);
    TEST_ASSERT(cpu.get_nzcv() == 0x0);

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Compare-and-Branch Tests (Tests 10-14)
// -----------------------------------------------------------------------------
void test_10_to_14_cbz_cbnz_width_and_outcomes() {
    std::cout << "  Tests 10-14: CBZ/CBNZ taken and untaken for 32-bit and 64-bit... ";
    Memory memory(0x10000);
    CPU cpu;

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 64;
    cfg.event_types.set(TraceEventType::ExceptionEntered, false);
    cfg.event_types.set(TraceEventType::ExceptionReturned, false);
    cfg.event_types.set(TraceEventType::DebugEvent, false);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());
    cpu.bind_trace_manager(&mgr);

    // CBZ X0 (value = 0) -> Taken
    write_instruction(memory, 0x6000, enc_cbz_x(0, 4)); // target 0x6010
    cpu.set_program_counter(0x6000);
    cpu.write_register(0, 0);
    cpu.step(memory);
    TEST_ASSERT(cpu.get_program_counter() == 0x6010);
    TEST_ASSERT(mgr.statistics().events_accepted == 2);

    // CBZ X0 (value != 0) -> Not taken
    write_instruction(memory, 0x6010, enc_cbz_x(0, 4));
    cpu.write_register(0, 0x100);
    cpu.step(memory);
    TEST_ASSERT(cpu.get_program_counter() == 0x6014);
    TEST_ASSERT(mgr.statistics().events_accepted == 3);

    // CBNZ X0 (value != 0) -> Taken
    write_instruction(memory, 0x6014, enc_cbnz_x(0, 4)); // target 0x6024
    cpu.step(memory);
    TEST_ASSERT(cpu.get_program_counter() == 0x6024);
    TEST_ASSERT(mgr.statistics().events_accepted == 5);

    // CBNZ X0 (value == 0) -> Not taken
    write_instruction(memory, 0x6024, enc_cbnz_x(0, 4));
    cpu.write_register(0, 0);
    cpu.step(memory);
    TEST_ASSERT(cpu.get_program_counter() == 0x6028);
    TEST_ASSERT(mgr.statistics().events_accepted == 6);

    // 32-bit CBZ W0 with upper 32 bits set: value is 0 in lower 32 bits
    write_instruction(memory, 0x6028, enc_cbz_w(0, 4)); // target 0x6038
    cpu.write_register(0, 0xFFFFFFFF00000000ULL); // W0 is 0
    cpu.step(memory);
    TEST_ASSERT(cpu.get_program_counter() == 0x6038);
    TEST_ASSERT(mgr.statistics().events_accepted == 8);

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test-Bit-and-Branch Tests (Test 15)
// -----------------------------------------------------------------------------
void test_15_tbz_tbnz() {
    std::cout << "  Test 15: TBZ and TBNZ bit testing across bit positions... ";
    Memory memory(0x10000);
    CPU cpu;

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 64;
    cfg.event_types.set(TraceEventType::ExceptionEntered, false);
    cfg.event_types.set(TraceEventType::ExceptionReturned, false);
    cfg.event_types.set(TraceEventType::DebugEvent, false);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());
    cpu.bind_trace_manager(&mgr);

    // TBZ X0, #3, +4 words -> bit 3 is 0, so TAKEN
    write_instruction(memory, 0x7000, enc_tbz(0, 3, 4)); // target 0x7010
    cpu.set_program_counter(0x7000);
    cpu.write_register(0, 0x00);
    cpu.step(memory);
    TEST_ASSERT(cpu.get_program_counter() == 0x7010);
    TEST_ASSERT(mgr.statistics().events_accepted == 2);

    // TBZ X0, #3, +4 words -> bit 3 is 1 (0x8), so NOT taken
    write_instruction(memory, 0x7010, enc_tbz(0, 3, 4));
    cpu.write_register(0, 0x08);
    cpu.step(memory);
    TEST_ASSERT(cpu.get_program_counter() == 0x7014);
    TEST_ASSERT(mgr.statistics().events_accepted == 3);

    // TBNZ X0, #63, +4 words -> bit 63 is 1, so TAKEN
    write_instruction(memory, 0x7014, enc_tbnz(0, 63, 4)); // target 0x7024
    cpu.write_register(0, 1ULL << 63u);
    cpu.step(memory);
    TEST_ASSERT(cpu.get_program_counter() == 0x7024);
    TEST_ASSERT(mgr.statistics().events_accepted == 5);

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Indirect Branch Tests (Tests 16-17)
// -----------------------------------------------------------------------------
void test_16_and_17_br_blr_ret_indirect_branches() {
    std::cout << "  Tests 16 & 17: BR, BLR, and RET indirect branches with resolved targets... ";
    Memory memory(0x10000);
    CPU cpu;

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 64;
    cfg.event_types.set(TraceEventType::ExceptionEntered, false);
    cfg.event_types.set(TraceEventType::ExceptionReturned, false);
    cfg.event_types.set(TraceEventType::DebugEvent, false);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());
    cpu.bind_trace_manager(&mgr);

    // BR X1 to 0x8050
    write_instruction(memory, 0x8000, enc_br(1));
    cpu.set_program_counter(0x8000);
    cpu.write_register(1, 0x8050);
    cpu.step(memory);
    TEST_ASSERT(cpu.get_program_counter() == 0x8050);

    TraceEvent ev_ret{}, ev_br{};
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev_ret));
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev_br));
    TEST_ASSERT(ev_br.get_event_type() == TraceEventType::BranchTaken);
    TEST_ASSERT(ev_br.pc == 0x8000);
    TEST_ASSERT(ev_br.address == 0x8050);

    // BLR X2 to 0x8080
    write_instruction(memory, 0x8050, enc_blr(2));
    cpu.write_register(2, 0x8080);
    cpu.step(memory);
    TEST_ASSERT(cpu.get_program_counter() == 0x8080);
    TEST_ASSERT(cpu.read_register(30) == 0x8054); // LR set

    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev_ret));
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev_br));
    TEST_ASSERT(ev_br.address == 0x8080);

    // RET (defaults to X30) returns to 0x8054
    write_instruction(memory, 0x8080, enc_ret(30));
    cpu.step(memory);
    TEST_ASSERT(cpu.get_program_counter() == 0x8054);

    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev_ret));
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev_br));
    TEST_ASSERT(ev_br.pc == 0x8080);
    TEST_ASSERT(ev_br.address == 0x8054);

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Fault and Debugger Tests (Tests 18-22)
// -----------------------------------------------------------------------------
void test_18_to_22_faults_and_debugger_isolation() {
    std::cout << "  Tests 18-22: Faulting branches, fetch aborts, and debugger isolation... ";
    Memory memory(0x1000); // 4 KiB
    CPU cpu;

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 64;
    cfg.event_types.set(TraceEventType::ExceptionEntered, false);
    cfg.event_types.set(TraceEventType::ExceptionReturned, false);
    cfg.event_types.set(TraceEventType::DebugEvent, false);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());
    cpu.bind_trace_manager(&mgr);

    // 18. Faulting branch (reserved condition code NV: 0x5400000Fu)
    write_instruction(memory, 0x500, 0x5400000Fu);
    cpu.set_program_counter(0x500);
    bool caught_fault = false;
    try {
        cpu.step(memory);
    } catch (const std::exception&) {
        caught_fault = true;
    }
    TEST_ASSERT(caught_fault);
    TEST_ASSERT(mgr.statistics().events_accepted == 0); // No trace events

    // 19. Instruction-fetch abort (fetching outside 4 KiB memory at 0x2000)
    cpu.set_program_counter(0x2000);
    bool caught_fetch = false;
    try {
        cpu.step(memory);
    } catch (const std::exception&) {
        caught_fetch = true;
    }
    TEST_ASSERT(caught_fetch);
    TEST_ASSERT(mgr.statistics().events_accepted == 0);

    // 20. Debugger breakpoint stop before execution
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x800);
    write_instruction(memory, 0x200, enc_b(4));
    cpu.set_program_counter(0x200);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x200);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u)); // Enabled at EL1/EL0
    cpu.step(memory);

    // Breakpoint triggered exception vector, branch did NOT retire or take
    TEST_ASSERT(cpu.get_program_counter() == 0x800 + 0x200);
    TEST_ASSERT(mgr.statistics().events_accepted == 0);

    // 21. After disabling breakpoint, execution retires and emits events cleanly
    cpu.get_debug_arch().set_dbgbcr(0, 0); // Disable breakpoint
    cpu.set_program_counter(0x200);
    cpu.step(memory);
    TEST_ASSERT(cpu.get_program_counter() == 0x210);
    TEST_ASSERT(mgr.statistics().events_accepted == 2);

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Filtering and Pipeline Tests (Tests 23-28)
// -----------------------------------------------------------------------------
void test_23_to_28_filtering_and_file_persistence() {
    std::cout << "  Tests 23-28: Filtering, serialization, file reader, and drain... ";
    const std::filesystem::path test_dir = std::filesystem::temp_directory_path() / "winios_branch_test";
    std::filesystem::create_directories(test_dir);
    const std::filesystem::path trace_path = test_dir / "branch_trace.bin";

    if (std::filesystem::exists(trace_path)) {
        std::filesystem::remove(trace_path);
    }

    // 23 & 24. Filter BranchTaken out, keep InstructionRetired enabled
    {
        Memory memory(0x10000);
        CPU cpu;
        write_instruction(memory, 0x1000, enc_b(4)); // target 0x1010
        cpu.set_program_counter(0x1000);

        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 64;
        cfg.event_types.set(TraceEventType::BranchTaken, false); // FILTER OUT BranchTaken
        TEST_ASSERT(mgr.configure(cfg));
        TEST_ASSERT(mgr.start());

        cpu.bind_trace_manager(&mgr);
        cpu.step(memory);

        TEST_ASSERT(cpu.get_program_counter() == 0x1010);
        // Exactly 1 event accepted (InstructionRetired), 0 BranchTaken
        TEST_ASSERT(mgr.statistics().events_accepted == 1);

        TraceEvent ev;
        TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev));
        TEST_ASSERT(ev.get_event_type() == TraceEventType::InstructionRetired);
        TEST_ASSERT(mgr.ring_buffer()->empty());
        TEST_ASSERT(mgr.stop());
    }

    // 26, 27, 28: Full file pipeline persistence and reader verification
    {
        Memory memory(0x10000);
        CPU cpu;
        // Instruction stream: B to 0x1008, then MOV X0, #42
        write_instruction(memory, 0x1000, enc_b(2));
        write_instruction(memory, 0x1004, enc_nop()); // skipped
        write_instruction(memory, 0x1008, enc_movz(0, 42));
        cpu.set_program_counter(0x1000);

        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.file_output_enabled = true;
        cfg.output_path = trace_path;
        cfg.ring_capacity = 256;
        cfg.writer_batch_size = 128;
        TEST_ASSERT(mgr.configure(cfg));
        TEST_ASSERT(mgr.start());

        cpu.bind_trace_manager(&mgr);
        // Step 1: B 0x1008 (emits InstructionRetired seq 1, BranchTaken seq 2)
        cpu.step(memory);
        // Step 2: MOV X0, #42 (emits InstructionRetired seq 3)
        cpu.step(memory);

        TEST_ASSERT(cpu.read_register(0) == 42);
        TEST_ASSERT(mgr.stop()); // Flushes and drains to disk

        // Read file back using TraceFileReader
        TraceFileReader reader;
        TEST_ASSERT(reader.open(trace_path));
        TEST_ASSERT(reader.footer().total_events == 3);

        std::vector<TraceEvent> events;
        TEST_ASSERT(reader.read_all_events(events) == 3);
        TEST_ASSERT(events.size() == 3);

        // Event 1: InstructionRetired at 0x1000
        TEST_ASSERT(events[0].get_event_type() == TraceEventType::InstructionRetired);
        TEST_ASSERT(events[0].pc == 0x1000);
        TEST_ASSERT(events[0].sequence_number == 1);

        // Event 2: BranchTaken from 0x1000 to 0x1008
        TEST_ASSERT(events[1].get_event_type() == TraceEventType::BranchTaken);
        TEST_ASSERT(events[1].pc == 0x1000);
        TEST_ASSERT(events[1].address == 0x1008);
        TEST_ASSERT(events[1].sequence_number == 2);
        TEST_ASSERT(events[1].has_flag(TraceEventFlags::IsTaken));
        TEST_ASSERT(events[1].has_flag(TraceEventFlags::HasVirtualAddress));

        // Event 3: InstructionRetired at 0x1008
        TEST_ASSERT(events[2].get_event_type() == TraceEventType::InstructionRetired);
        TEST_ASSERT(events[2].pc == 0x1008);
        TEST_ASSERT(events[2].sequence_number == 3);

        reader.close();
        std::filesystem::remove_all(test_dir);
    }

    std::cout << "PASS\n";
}

} // namespace

int main() {
    std::cout << "\n=== WinIOS Phase 52 Stage 7: Branch-Taken Trace Unit Tests ===\n\n";

    test_1_b_emits_retirement_and_branch();
    test_2_and_3_bl_events_and_link_register();
    test_4_and_5_branch_target_and_pc_recording();
    test_6_and_7_b_cond_taken_vs_not_taken();
    test_8_and_9_multiple_condition_codes_and_nzcv_preservation();
    test_10_to_14_cbz_cbnz_width_and_outcomes();
    test_15_tbz_tbnz();
    test_16_and_17_br_blr_ret_indirect_branches();
    test_18_to_22_faults_and_debugger_isolation();
    test_23_to_28_filtering_and_file_persistence();

    std::cout << "\n=== All Branch Trace Unit Tests PASSED Successfully! ===\n\n";
    return 0;
}

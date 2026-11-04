#include "cpu/cpu.h"
#include "cpu/debug_architecture.h"
#include "memory/memory.h"
#include "trace/trace_config.h"
#include "trace/trace_file_reader.h"
#include "trace/trace_file_writer.h"
#include "trace/trace_manager.h"
#include "trace/trace_types.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
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

// Drain all events currently available in the TraceManager ring buffer
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

constexpr std::uint32_t enc_movz(unsigned rd, std::uint16_t imm16, unsigned shift = 0) {
    return 0xD2800000u | ((shift / 16u) << 21u) | (static_cast<std::uint32_t>(imm16) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_add_imm(unsigned rd, unsigned rn, std::uint16_t imm12) {
    return 0x91000000u | ((imm12 & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_ldr_x(unsigned rt, unsigned rn) {
    return 0xF9400000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_str_x(unsigned rt, unsigned rn) {
    return 0xF9000000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_ldr_w(unsigned rt, unsigned rn) {
    return 0xB9400000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_str_w(unsigned rt, unsigned rn) {
    return 0xB9000000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_ldxr_x(unsigned rt, unsigned rn) {
    return 0xC85F7C00u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_b(std::int32_t word_offset) {
    return 0x14000000u | (static_cast<std::uint32_t>(word_offset) & 0x03FFFFFFu);
}

constexpr std::uint32_t enc_eret() {
    return 0xD69F03E0u;
}

// Helper: construct DBGWCR value
constexpr std::uint32_t make_dbgwcr(bool enable, unsigned pac, unsigned lsc, std::uint8_t bas) {
    return (enable ? 1u : 0u) | ((pac & 3u) << 1u) | ((lsc & 3u) << 3u) | (static_cast<std::uint32_t>(bas) << 5u);
}

void setup_test_cpu(CPU& cpu, Memory& mem, unsigned el = 1, std::uint64_t vbar = 0x10000) {
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(el);
    cpu.set_vbar_el1(vbar);
    cpu.set_vbar_el2(vbar + 0x10000);
    cpu.set_vbar_el3(vbar + 0x20000);
    for (std::size_t i = 0; i < mem.size(); i += 4) {
        mem.write_u32(i, enc_nop());
    }
}

} // namespace

// =============================================================================
// Group A: Execution Breakpoint Tests (Tests 1 - 8)
// =============================================================================

void test_01_breakpoint_hit_slot0() {
    std::cout << "  Running Test 01: Breakpoint hit on Slot 0... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 1024;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    const std::uint64_t bp_pc = 0x2000;
    cpu.set_program_counter(bp_pc);
    mem.write_u32(bp_pc, enc_movz(0, 42));

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, bp_pc);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    cpu.step(mem);

    TEST_ASSERT(cpu.get_program_counter() == 0x10200ULL);
    TEST_ASSERT(cpu.read_register(0) == 0); // Instruction suppressed

    const auto events = pop_all_events(mgr);
    // Expected: DebugEvent (breakpoint) then ExceptionEntered
    TEST_ASSERT(events.size() == 2);
    const auto& dev = events[0];
    TEST_ASSERT(dev.event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
    TEST_ASSERT((dev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsBreakpoint)) != 0);
    TEST_ASSERT((dev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsSynchronous)) != 0);
    TEST_ASSERT((dev.flags & static_cast<std::uint32_t>(TraceEventFlags::HasData)) != 0);
    TEST_ASSERT(dev.pc == bp_pc);
    TEST_ASSERT(dev.address == bp_pc);
    TEST_ASSERT(dev.data == 0); // Slot 0
    TEST_ASSERT((dev.opcode_or_syndrome >> 26u) == 0x31u); // BreakpointSame
    TEST_ASSERT(dev.access_size == 4);

    const auto& eev = events[1];
    TEST_ASSERT(eev.event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));
    TEST_ASSERT(eev.sequence_number == dev.sequence_number + 1);
    TEST_ASSERT(eev.pc == bp_pc);
    TEST_ASSERT(eev.address == 0x10200ULL);
    std::cout << "PASS\n";
}

void test_02_breakpoint_hit_slot1() {
    std::cout << "  Running Test 02: Breakpoint hit on Slot 1... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    const std::uint64_t bp_pc = 0x2400;
    cpu.set_program_counter(bp_pc);
    mem.write_u32(bp_pc, enc_movz(1, 100));

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(1, bp_pc);
    cpu.get_debug_arch().set_dbgbcr(1, 0x1u | (0x3u << 1u));

    cpu.step(mem);

    const auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
    TEST_ASSERT(events[0].data == 1); // Slot 1
    TEST_ASSERT(events[0].pc == bp_pc);
    std::cout << "PASS\n";
}

void test_03_breakpoint_hit_slot2() {
    std::cout << "  Running Test 03: Breakpoint hit on Slot 2... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    const std::uint64_t bp_pc = 0x2800;
    cpu.set_program_counter(bp_pc);
    mem.write_u32(bp_pc, enc_movz(2, 200));

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(2, bp_pc);
    cpu.get_debug_arch().set_dbgbcr(2, 0x1u | (0x3u << 1u));

    cpu.step(mem);

    const auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
    TEST_ASSERT(events[0].data == 2); // Slot 2
    std::cout << "PASS\n";
}

void test_04_breakpoint_hit_slot3() {
    std::cout << "  Running Test 04: Breakpoint hit on Slot 3... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    const std::uint64_t bp_pc = 0x2C00;
    cpu.set_program_counter(bp_pc);
    mem.write_u32(bp_pc, enc_movz(3, 300));

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(3, bp_pc);
    cpu.get_debug_arch().set_dbgbcr(3, 0x1u | (0x3u << 1u));

    cpu.step(mem);

    const auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
    TEST_ASSERT(events[0].data == 3); // Slot 3
    std::cout << "PASS\n";
}

void test_05_breakpoint_non_matching_address() {
    std::cout << "  Running Test 05: Breakpoint non-matching address... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_movz(0, 42));

    // Breakpoint at 0x3000, not 0x2000
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x3000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    cpu.step(mem);

    TEST_ASSERT(cpu.read_register(0) == 42);
    TEST_ASSERT(cpu.get_program_counter() == 0x2004);

    const auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 1);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::InstructionRetired));
    TEST_ASSERT(events[0].pc == 0x2000);
    std::cout << "PASS\n";
}

void test_06_breakpoint_disabled() {
    std::cout << "  Running Test 06: Breakpoint disabled (E=0)... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_movz(0, 99));

    // MDE enabled, but DBGBCR0.E = 0
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x0u);

    cpu.step(mem);

    TEST_ASSERT(cpu.read_register(0) == 99);
    const auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 1);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::InstructionRetired));
    std::cout << "PASS\n";
}

void test_07_breakpoint_privilege_filtering() {
    std::cout << "  Running Test 07: Breakpoint privilege filtering (PMC)... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 0, 0x10000); // EL0

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_movz(0, 77));

    // PMC = 0b01 (EL1 only), current_el = 0 -> should NOT match
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x1u << 1u));

    cpu.step(mem);

    TEST_ASSERT(cpu.read_register(0) == 77);
    const auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 1);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::InstructionRetired));
    std::cout << "PASS\n";
}

void test_08_linked_breakpoint_hit() {
    std::cout << "  Running Test 08: Linked breakpoint hit... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    const std::uint64_t bp_pc = 0x3000;
    cpu.set_program_counter(bp_pc);
    mem.write_u32(bp_pc, enc_movz(0, 123));

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    // Slot 1: unlinked address match (BT=0, LBN=0) at bp_pc
    cpu.get_debug_arch().set_dbgbvr(1, bp_pc);
    cpu.get_debug_arch().set_dbgbcr(1, 0x1u | (0x3u << 1u) | (0xFu << 5u) | (0u << 20u));

    // Slot 0: Linked instruction address match (BT=1, LBN=1) at bp_pc
    cpu.get_debug_arch().set_dbgbvr(0, bp_pc);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u) | (0xFu << 5u) | (1u << 20u) | (1u << 16u));

    cpu.step(mem);

    TEST_ASSERT(cpu.get_program_counter() == 0x10200ULL);
    TEST_ASSERT(cpu.read_register(0) == 0); // Suppressed
    const auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
    TEST_ASSERT(events[0].data == 0); // Slot 0
    std::cout << "PASS\n";
}

// =============================================================================
// Group B: Data Watchpoint Tests (Tests 9 - 15)
// =============================================================================

void test_09_watchpoint_read_hit() {
    std::cout << "  Running Test 09: Watchpoint read hit (Load)... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    const std::uint64_t target_va = 0x5000;
    mem.write_u64(target_va, 0x1122334455667788ULL);

    const std::uint64_t pc = 0x2000;
    cpu.set_program_counter(pc);
    cpu.write_register(1, target_va);
    mem.write_u32(pc, enc_ldr_x(0, 1)); // LDR X0, [X1]

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, target_va);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 3, 1, 0xFF)); // LSC=1 (Load only)

    cpu.step(mem);

    TEST_ASSERT(cpu.get_program_counter() == 0x10200ULL);
    TEST_ASSERT(cpu.read_register(0) == 0); // Not committed!
    TEST_ASSERT(cpu.get_far_el1() == target_va);
    TEST_ASSERT((cpu.get_esr_el1() >> 26u) == 0x35u); // WatchpointSame

    const auto events = pop_all_events(mgr);
    // Expected: DebugEvent then ExceptionEntered. No MemoryRead or InstructionRetired!
    TEST_ASSERT(events.size() == 2);
    const auto& dev = events[0];
    TEST_ASSERT(dev.event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
    TEST_ASSERT((dev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsWatchpoint)) != 0);
    TEST_ASSERT((dev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsSynchronous)) != 0);
    TEST_ASSERT((dev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsWrite)) == 0); // Read!
    TEST_ASSERT((dev.flags & static_cast<std::uint32_t>(TraceEventFlags::HasData)) != 0);
    TEST_ASSERT(dev.pc == pc);
    TEST_ASSERT(dev.address == target_va);
    TEST_ASSERT(dev.data == 0); // Slot 0
    TEST_ASSERT(dev.access_size == 8);

    const auto& eev = events[1];
    TEST_ASSERT(eev.event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));
    TEST_ASSERT(eev.sequence_number == dev.sequence_number + 1);
    TEST_ASSERT(eev.pc == pc);
    std::cout << "PASS\n";
}

void test_10_watchpoint_write_hit() {
    std::cout << "  Running Test 10: Watchpoint write hit (Store)... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    const std::uint64_t target_va = 0x6000;
    mem.write_u64(target_va, 0xAAAAAAAAAAAAAAAAULL);

    const std::uint64_t pc = 0x2000;
    cpu.set_program_counter(pc);
    cpu.write_register(0, 0xBBBBBBBBBBBBBBBBULL);
    cpu.write_register(1, target_va);
    mem.write_u32(pc, enc_str_x(0, 1)); // STR X0, [X1]

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(1, target_va);
    cpu.get_debug_arch().set_dbgwcr(1, make_dbgwcr(true, 3, 2, 0xFF)); // LSC=2 (Store only)

    cpu.step(mem);

    TEST_ASSERT(cpu.get_program_counter() == 0x10200ULL);
    TEST_ASSERT(mem.read_u64(target_va) == 0xAAAAAAAAAAAAAAAAULL); // Memory untouched!

    const auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    const auto& dev = events[0];
    TEST_ASSERT(dev.event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
    TEST_ASSERT((dev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsWatchpoint)) != 0);
    TEST_ASSERT((dev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsWrite)) != 0); // Write!
    TEST_ASSERT(dev.pc == pc);
    TEST_ASSERT(dev.address == target_va);
    TEST_ASSERT(dev.data == 1); // Slot 1
    TEST_ASSERT(dev.access_size == 8);
    std::cout << "PASS\n";
}

void test_11_watchpoint_bas_matching() {
    std::cout << "  Running Test 11: Watchpoint Byte Address Select (BAS)... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    const std::uint64_t base_va = 0x7000;
    // BAS = 0b00000010 (byte 1 only)
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, base_va);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 3, 3, 0x02)); // BAS bit 1

    // Access byte 0 (0x7000) -> should NOT trigger
    cpu.set_program_counter(0x2000);
    cpu.write_register(1, base_va);
    mem.write_u32(0x2000, enc_ldr_w(0, 1)); // LDR W0, [X1] touches bytes 0..3 -> DOES overlap bit 1!
    // Let's do single-byte read at 0x7000 (byte 0)
    mem.write_u32(0x2000, 0x39400020u); // LDRB W0, [X1] (byte 0)
    cpu.step(mem);

    TEST_ASSERT(cpu.get_program_counter() == 0x2004); // Completed without fault!
    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2); // MemoryRead + InstructionRetired, zero DebugEvent!
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::MemoryRead));
    TEST_ASSERT(events[1].event_type == static_cast<std::uint16_t>(TraceEventType::InstructionRetired));

    // Now access byte 1 (0x7001) -> SHOULD trigger watchpoint
    cpu.write_register(1, base_va + 1);
    mem.write_u32(0x2004, 0x39400020u); // LDRB W0, [X1]
    cpu.step(mem);

    TEST_ASSERT(cpu.get_program_counter() == 0x10200ULL); // Trapped!
    events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
    TEST_ASSERT(events[0].address == base_va + 1);
    std::cout << "PASS\n";
}

void test_12_watchpoint_non_matching_address() {
    std::cout << "  Running Test 12: Watchpoint non-matching address... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    const std::uint64_t watched_va = 0x8000;
    const std::uint64_t access_va = 0x9000;

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, watched_va);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 3, 3, 0xFF));

    cpu.set_program_counter(0x2000);
    cpu.write_register(1, access_va);
    mem.write_u32(0x2000, enc_ldr_x(0, 1));

    cpu.step(mem);

    TEST_ASSERT(cpu.get_program_counter() == 0x2004);
    const auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::MemoryRead));
    TEST_ASSERT(events[1].event_type == static_cast<std::uint16_t>(TraceEventType::InstructionRetired));
    std::cout << "PASS\n";
}

void test_13_watchpoint_lsc_filtering() {
    std::cout << "  Running Test 13: Watchpoint LSC filtering (Store-only vs Load)... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    const std::uint64_t watched_va = 0x8000;
    // Watchpoint configured for Store only (LSC=2)
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, watched_va);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 3, 2, 0xFF));

    // Perform a Load (LDR) at watched_va
    cpu.set_program_counter(0x2000);
    cpu.write_register(1, watched_va);
    mem.write_u32(0x2000, enc_ldr_x(0, 1));

    cpu.step(mem);

    TEST_ASSERT(cpu.get_program_counter() == 0x2004); // Load succeeds!
    const auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::MemoryRead));
    TEST_ASSERT(events[1].event_type == static_cast<std::uint16_t>(TraceEventType::InstructionRetired));
    std::cout << "PASS\n";
}

void test_14_watchpoint_zero_memory_mutation() {
    std::cout << "  Running Test 14: Watchpoint prevents memory mutation... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    const std::uint64_t target_va = 0xA000;
    mem.write_u64(target_va, 0x12345678ULL);

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(2, target_va);
    cpu.get_debug_arch().set_dbgwcr(2, make_dbgwcr(true, 3, 2, 0xFF));

    cpu.set_program_counter(0x2000);
    cpu.write_register(0, 0xDEADBEEFULL);
    cpu.write_register(1, target_va);
    mem.write_u32(0x2000, enc_str_x(0, 1));

    cpu.step(mem);

    TEST_ASSERT(mem.read_u64(target_va) == 0x12345678ULL); // Completely unchanged!
    const auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
    TEST_ASSERT((events[0].flags & static_cast<std::uint32_t>(TraceEventFlags::IsWrite)) != 0);
    std::cout << "PASS\n";
}

void test_15_linked_watchpoint_hit() {
    std::cout << "  Running Test 15: Linked watchpoint hit (WT=1)... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    const std::uint64_t pc = 0x2000;
    const std::uint64_t data_va = 0xB000;

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);

    // Breakpoint slot 1: matches PC 0x2000
    cpu.get_debug_arch().set_dbgbvr(1, pc);
    cpu.get_debug_arch().set_dbgbcr(1, 0x1u | (0x3u << 1u) | (0x2u << 20u)); // BT=2 (linked addr match)

    // Watchpoint slot 0: linked to breakpoint slot 1 (WT=1, LBN=1)
    cpu.get_debug_arch().set_dbgwvr(0, data_va);
    const std::uint32_t wt_lbn = (1u << 20u) | (1u << 16u); // WT=1, LBN=1
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 3, 3, 0xFF) | wt_lbn);

    cpu.set_program_counter(pc);
    cpu.write_register(1, data_va);
    mem.write_u32(pc, enc_ldr_x(0, 1));

    cpu.step(mem);

    TEST_ASSERT(cpu.get_program_counter() == 0x10200ULL);
    const auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
    TEST_ASSERT(events[0].data == 0); // Watchpoint slot 0
    std::cout << "PASS\n";
}

// =============================================================================
// Group C: Single-Step Debug Events (Tests 16 - 20)
// =============================================================================

void test_16_single_step_post_retirement() {
    std::cout << "  Running Test 16: Single-step post-retirement... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    const std::uint64_t pc = 0x2000;
    cpu.set_program_counter(pc);
    mem.write_u32(pc, enc_add_imm(0, 0, 10)); // ADD X0, X0, #10

    // Enable MDE and SS, unmask DAIF.D, set PSTATE.SS
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    TEST_ASSERT(cpu.read_register(0) == 10); // Instruction committed!
    TEST_ASSERT(cpu.get_program_counter() == 0x10200ULL); // Trapped to vector
    TEST_ASSERT(cpu.get_elr_el1() == 0x2004ULL); // Return address is next PC

    const auto events = pop_all_events(mgr);
    // Expected: InstructionRetired (seq N) -> DebugEvent (seq N+1) -> ExceptionEntered (seq N+2)
    TEST_ASSERT(events.size() == 3);
    const auto& iret = events[0];
    TEST_ASSERT(iret.event_type == static_cast<std::uint16_t>(TraceEventType::InstructionRetired));
    TEST_ASSERT(iret.pc == pc);

    const auto& dev = events[1];
    TEST_ASSERT(dev.event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
    TEST_ASSERT((dev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsSingleStep)) != 0);
    TEST_ASSERT((dev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsSynchronous)) != 0);
    TEST_ASSERT(dev.pc == pc);
    TEST_ASSERT(dev.address == 0x2004ULL); // Target / return PC
    TEST_ASSERT(dev.sequence_number == iret.sequence_number + 1);

    const auto& eev = events[2];
    TEST_ASSERT(eev.event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));
    TEST_ASSERT(eev.sequence_number == dev.sequence_number + 1);
    TEST_ASSERT(eev.address == 0x10200ULL);
    std::cout << "PASS\n";
}

void test_17_single_step_branch_instruction() {
    std::cout << "  Running Test 17: Single-step branch instruction... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    const std::uint64_t pc = 0x2000;
    cpu.set_program_counter(pc);
    // B +8 (word_offset = 2) -> branch to 0x2008
    mem.write_u32(pc, enc_b(2));

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    TEST_ASSERT(cpu.get_elr_el1() == 0x2008ULL); // Return address is branch target
    TEST_ASSERT(cpu.get_program_counter() == 0x10200ULL);

    const auto events = pop_all_events(mgr);
    // Expected: InstructionRetired -> BranchTaken -> DebugEvent -> ExceptionEntered
    TEST_ASSERT(events.size() == 4);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::InstructionRetired));
    TEST_ASSERT(events[1].event_type == static_cast<std::uint16_t>(TraceEventType::BranchTaken));
    TEST_ASSERT(events[2].event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
    TEST_ASSERT(events[2].pc == pc);
    TEST_ASSERT(events[2].address == 0x2008ULL); // Branch target address
    TEST_ASSERT(events[3].event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));
    std::cout << "PASS\n";
}

void test_18_single_step_active_pending_at_boundary() {
    std::cout << "  Running Test 18: Single-step active-pending at instruction boundary... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    const std::uint64_t pc = 0x2000;
    cpu.set_program_counter(pc);
    mem.write_u32(pc, enc_add_imm(0, 0, 50));

    // SS active at start, but PSTATE.SS is FALSE (active-pending state!)
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(false);

    cpu.step(mem);

    TEST_ASSERT(cpu.read_register(0) == 0); // Instruction was NOT executed
    TEST_ASSERT(cpu.get_elr_el1() == pc);   // Preferred return address is current PC
    TEST_ASSERT(cpu.get_program_counter() == 0x10200ULL);

    const auto events = pop_all_events(mgr);
    // Expected: DebugEvent (active-pending) -> ExceptionEntered
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
    TEST_ASSERT((events[0].flags & static_cast<std::uint32_t>(TraceEventFlags::IsSingleStep)) != 0);
    TEST_ASSERT(events[0].pc == pc);
    TEST_ASSERT(events[1].event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));
    std::cout << "PASS\n";
}

void test_19_single_step_exclusive_access() {
    std::cout << "  Running Test 19: Single-step exclusive access (LDXR)... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    const std::uint64_t pc = 0x2000;
    const std::uint64_t target_va = 0x5000;
    mem.write_u64(target_va, 0x999);
    cpu.set_program_counter(pc);
    cpu.write_register(1, target_va);
    mem.write_u32(pc, enc_ldxr_x(0, 1)); // LDXR X0, [X1]

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    TEST_ASSERT(cpu.read_register(0) == 0x999);
    const auto events = pop_all_events(mgr);
    // Expected: MemoryRead -> InstructionRetired -> DebugEvent -> ExceptionEntered
    TEST_ASSERT(events.size() == 4);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::MemoryRead));
    TEST_ASSERT((events[0].flags & static_cast<std::uint32_t>(TraceEventFlags::IsAtomic)) != 0);
    TEST_ASSERT(events[1].event_type == static_cast<std::uint16_t>(TraceEventType::InstructionRetired));
    TEST_ASSERT(events[2].event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
    TEST_ASSERT((events[2].flags & static_cast<std::uint32_t>(TraceEventFlags::IsSingleStep)) != 0);
    TEST_ASSERT(events[2].data == 1); // is_exclusive = 1
    std::cout << "PASS\n";
}

void test_20_single_step_disabled() {
    std::cout << "  Running Test 20: Single-step disabled (SS=0)... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_add_imm(0, 0, 1));

    // SS bit 0 is 0
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    TEST_ASSERT(cpu.get_program_counter() == 0x2004);
    const auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 1);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::InstructionRetired));
    std::cout << "PASS\n";
}

// =============================================================================
// Group D: Event Interaction, Precedence & Causal Chains (Tests 21 - 25)
// =============================================================================

void test_21_breakpoint_priority_over_watchpoint() {
    std::cout << "  Running Test 21: Breakpoint priority over watchpoint... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    const std::uint64_t pc = 0x2000;
    const std::uint64_t target_va = 0x5000;
    cpu.set_program_counter(pc);
    cpu.write_register(1, target_va);
    mem.write_u32(pc, enc_str_x(0, 1)); // Instruction at 0x2000 stores to 0x5000

    // Set breakpoint on PC 0x2000 AND watchpoint on data address 0x5000
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, pc);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));
    cpu.get_debug_arch().set_dbgwvr(0, target_va);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 3, 2, 0xFF));

    cpu.step(mem);

    // Breakpoint must fire BEFORE the instruction is fetched or executed!
    const auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
    TEST_ASSERT((events[0].flags & static_cast<std::uint32_t>(TraceEventFlags::IsBreakpoint)) != 0);
    TEST_ASSERT(events[0].data == 0); // Slot 0 breakpoint
    TEST_ASSERT(events[1].event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));
    std::cout << "PASS\n";
}

void test_22_watchpoint_priority_over_step() {
    std::cout << "  Running Test 22: Watchpoint priority over single-step... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    const std::uint64_t pc = 0x2000;
    const std::uint64_t target_va = 0x5000;
    cpu.set_program_counter(pc);
    cpu.write_register(1, target_va);
    mem.write_u32(pc, enc_ldr_x(0, 1));

    // Enable both Watchpoint on 0x5000 AND Single-Step
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);
    cpu.get_debug_arch().set_dbgwvr(0, target_va);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 3, 1, 0xFF));

    cpu.step(mem);

    // Watchpoint occurs during memory access -> unwinds instruction -> step does NOT fire!
    const auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
    TEST_ASSERT((events[0].flags & static_cast<std::uint32_t>(TraceEventFlags::IsWatchpoint)) != 0);
    TEST_ASSERT((events[0].flags & static_cast<std::uint32_t>(TraceEventFlags::IsSingleStep)) == 0);
    TEST_ASSERT(events[1].event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));
    std::cout << "PASS\n";
}

void test_23_breakpoint_exception_pairing() {
    std::cout << "  Running Test 23: Breakpoint exception pairing... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_nop());

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    cpu.step(mem);

    const auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].sequence_number + 1 == events[1].sequence_number);
    TEST_ASSERT(events[0].pc == events[1].pc);
    TEST_ASSERT(events[1].event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));
    std::cout << "PASS\n";
}

void test_24_watchpoint_exception_pairing() {
    std::cout << "  Running Test 24: Watchpoint exception pairing... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    const std::uint64_t target_va = 0x6000;
    cpu.set_program_counter(0x2000);
    cpu.write_register(1, target_va);
    mem.write_u32(0x2000, enc_ldr_x(0, 1));

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, target_va);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 3, 1, 0xFF));

    cpu.step(mem);

    const auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].sequence_number + 1 == events[1].sequence_number);
    TEST_ASSERT(events[1].event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));
    TEST_ASSERT(events[1].data == target_va); // FAR recorded in ExceptionEntered
    std::cout << "PASS\n";
}

void test_25_single_step_exception_pairing() {
    std::cout << "  Running Test 25: Single-step exception pairing... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_nop());

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    const auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 3);
    TEST_ASSERT(events[0].sequence_number + 1 == events[1].sequence_number);
    TEST_ASSERT(events[1].sequence_number + 1 == events[2].sequence_number);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::InstructionRetired));
    TEST_ASSERT(events[1].event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
    TEST_ASSERT(events[2].event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));
    std::cout << "PASS\n";
}

// =============================================================================
// Group E: Trace Filtering, Dynamic Configuration & Serialization (Tests 26 - 29)
// =============================================================================

void test_26_debug_event_filtering_enabled() {
    std::cout << "  Running Test 26: Debug event filter enabled... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.set_all(false);
    cfg.event_types.set(TraceEventType::DebugEvent, true);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_nop());

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    cpu.step(mem);

    const auto events = pop_all_events(mgr);
    // Only DebugEvent passed the filter! ExceptionEntered was filtered out!
    TEST_ASSERT(events.size() == 1);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
    std::cout << "PASS\n";
}

void test_27_debug_event_filtering_disabled() {
    std::cout << "  Running Test 27: Debug event filter disabled... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.set(TraceEventType::DebugEvent, false); // Explicitly disable DebugEvent
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_nop());

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    cpu.step(mem);

    const auto events = pop_all_events(mgr);
    // DebugEvent was filtered out, only ExceptionEntered remains!
    TEST_ASSERT(events.size() == 1);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));
    std::cout << "PASS\n";
}

void test_28_dynamic_filtering_reconfiguration() {
    std::cout << "  Running Test 28: Dynamic filtering reconfiguration... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.set(TraceEventType::DebugEvent, false);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_nop());

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    cpu.step(mem);
    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 1);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));

    // Reconfigure: stop, configure with DebugEvent enabled, start
    TEST_ASSERT(mgr.stop());
    cfg.event_types.set(TraceEventType::DebugEvent, true);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    // Reset CPU to hit breakpoint again
    cpu.set_program_counter(0x2000);
    cpu.step(mem);

    events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
    TEST_ASSERT(events[1].event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));
    std::cout << "PASS\n";
}

void test_29_binary_serialization_roundtrip() {
    std::cout << "  Running Test 29: Binary serialization roundtrip (.wtr)... ";
    const std::filesystem::path trace_path = "debug_trace_test.wtr";
    if (std::filesystem::exists(trace_path)) {
        std::filesystem::remove(trace_path);
    }

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.file_output_enabled = true;
    cfg.output_path = trace_path;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.reset_instruction_trace_sequence();

    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    // 1. Breakpoint event
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_nop());
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));
    cpu.step(mem);

    // 2. Watchpoint event
    cpu.set_program_counter(0x3000);
    cpu.write_register(1, 0x5000);
    mem.write_u32(0x3000, enc_ldr_x(0, 1));
    cpu.get_debug_arch().set_dbgbcr(0, 0); // Disable breakpoint
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 3, 1, 0xFF));
    cpu.step(mem);

    // Draining and flushing to file
    TEST_ASSERT(mgr.flush());
    TEST_ASSERT(mgr.stop());

    TEST_ASSERT(std::filesystem::exists(trace_path));

    // Read back via TraceFileReader
    TraceFileReader reader;
    TEST_ASSERT(reader.open(trace_path));
    TEST_ASSERT(reader.is_cleanly_finalized());
    TEST_ASSERT(reader.header().schema_version == TRACE_SCHEMA_VERSION);

    std::vector<TraceEvent> read_events;
    TraceEvent ev{};
    while (reader.read_event(ev)) {
        read_events.push_back(ev);
    }
    reader.close();

    // Verify events:
    // Breakpoint step: DebugEvent (0), ExceptionEntered (1)
    // Watchpoint step: DebugEvent (2), ExceptionEntered (3)
    TEST_ASSERT(read_events.size() == 4);
    TEST_ASSERT(read_events[0].event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
    TEST_ASSERT((read_events[0].flags & static_cast<std::uint32_t>(TraceEventFlags::IsBreakpoint)) != 0);
    TEST_ASSERT(read_events[0].pc == 0x2000);

    TEST_ASSERT(read_events[2].event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
    TEST_ASSERT((read_events[2].flags & static_cast<std::uint32_t>(TraceEventFlags::IsWatchpoint)) != 0);
    TEST_ASSERT(read_events[2].pc == 0x3000);
    TEST_ASSERT(read_events[2].address == 0x5000);

    std::filesystem::remove(trace_path);
    std::cout << "PASS\n";
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
    std::cout << "===================================================================\n";
    std::cout << "WinIOS Phase 52 Stage 11: Debug Trace Unit Tests\n";
    std::cout << "===================================================================\n";

    std::cout << "\n[Group A: Execution Breakpoints]\n";
    test_01_breakpoint_hit_slot0();
    test_02_breakpoint_hit_slot1();
    test_03_breakpoint_hit_slot2();
    test_04_breakpoint_hit_slot3();
    test_05_breakpoint_non_matching_address();
    test_06_breakpoint_disabled();
    test_07_breakpoint_privilege_filtering();
    test_08_linked_breakpoint_hit();

    std::cout << "\n[Group B: Data Watchpoints]\n";
    test_09_watchpoint_read_hit();
    test_10_watchpoint_write_hit();
    test_11_watchpoint_bas_matching();
    test_12_watchpoint_non_matching_address();
    test_13_watchpoint_lsc_filtering();
    test_14_watchpoint_zero_memory_mutation();
    test_15_linked_watchpoint_hit();

    std::cout << "\n[Group C: Single-Step Debug Events]\n";
    test_16_single_step_post_retirement();
    test_17_single_step_branch_instruction();
    test_18_single_step_active_pending_at_boundary();
    test_19_single_step_exclusive_access();
    test_20_single_step_disabled();

    std::cout << "\n[Group D: Event Interaction, Precedence & Causal Chains]\n";
    test_21_breakpoint_priority_over_watchpoint();
    test_22_watchpoint_priority_over_step();
    test_23_breakpoint_exception_pairing();
    test_24_watchpoint_exception_pairing();
    test_25_single_step_exception_pairing();

    std::cout << "\n[Group E: Trace Filtering, Dynamic Configuration & Serialization]\n";
    test_26_debug_event_filtering_enabled();
    test_27_debug_event_filtering_disabled();
    test_28_dynamic_filtering_reconfiguration();
    test_29_binary_serialization_roundtrip();

    std::cout << "\n===================================================================\n";
    std::cout << "All 29 Debug Trace Unit Tests Passed Successfully!\n";
    std::cout << "===================================================================\n";
    return 0;
}

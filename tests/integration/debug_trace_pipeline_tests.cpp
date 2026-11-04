#include "cpu/cpu.h"
#include "cpu/debug_architecture.h"
#include "memory/memory.h"
#include "trace/trace_file_reader.h"
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
            std::cerr << "Assertion failed: " #cond " at line " << __LINE__    \
                      << std::endl;                                            \
            std::abort();                                                      \
        }                                                                      \
    } while (0)

using namespace winios;
using namespace winios::trace;

namespace {

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

constexpr std::uint32_t enc_eret() {
    return 0xD69F03E0u;
}

constexpr std::uint32_t make_dbgwcr(bool enable, unsigned pac, unsigned lsc, std::uint8_t bas) {
    return (enable ? 1u : 0u) | ((pac & 3u) << 1u) | ((lsc & 3u) << 3u) | (static_cast<std::uint32_t>(bas) << 5u);
}

void write_instruction(Memory& memory, std::uint64_t address, std::uint32_t instruction) {
    memory.write_u32(address, instruction);
}

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

struct CpuSnapshot {
    std::uint64_t x[31];
    std::uint64_t pc;
    std::uint64_t sp_el0;
    std::uint64_t sp_el1;
    std::uint64_t elr_el1;
    std::uint32_t spsr_el1;
    std::uint32_t esr_el1;
    std::uint64_t far_el1;
    std::uint8_t nzcv;
    std::uint8_t daif;
};

CpuSnapshot capture_snapshot(CPU& cpu) {
    CpuSnapshot s{};
    for (unsigned i = 0; i < 31; ++i) {
        s.x[i] = cpu.read_register(i);
    }
    s.pc = cpu.get_program_counter();
    s.sp_el0 = cpu.get_sp_el0();
    s.sp_el1 = cpu.get_sp_el1();
    s.elr_el1 = cpu.get_elr_el1();
    s.spsr_el1 = cpu.get_spsr_el1();
    s.esr_el1 = cpu.get_esr_el1();
    s.far_el1 = cpu.get_far_el1();
    s.nzcv = cpu.get_nzcv();
    s.daif = cpu.get_daif();
    return s;
}

void setup_debug_cpu(CPU& cpu, Memory& mem, unsigned el = 1, std::uint64_t vbar = 0x10000) {
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

int main() {
    std::cout << "=== WinIOS Phase 52 Stage 11: Debug Trace Pipeline & Integration Tests ===\n\n";

    // -------------------------------------------------------------------------
    // Test 1: Differential Validation (Run A: Untraced vs Run B: Traced)
    // -------------------------------------------------------------------------
    std::cout << "  Test 1: Differential Validation (Run A: Disabled vs Run B: Traced)... " << std::flush;
    CpuSnapshot snapshot_untraced{};
    CpuSnapshot snapshot_traced{};
    std::vector<std::uint8_t> ram_untraced(0x2000);
    std::vector<std::uint8_t> ram_traced(0x2000);

    auto setup_workload = [](Memory& mem) {
        // Code at 0x2000:
        // 0x2000: MOVZ X0, #42            (Normal instruction)
        // 0x2004: MOVZ X1, #0x5000        (Data pointer)
        // 0x2008: STR X0, [X1]            (Watchpoint will trigger on write to 0x5000)
        // 0x200C: ADD X2, X0, #8          (Breakpoint will trigger on PC 0x200C)
        // 0x2010: ADD X3, X2, #1          (Single-step will step this instruction)
        write_instruction(mem, 0x2000, enc_movz(0, 42));
        write_instruction(mem, 0x2004, enc_movz(1, 0x5000));
        write_instruction(mem, 0x2008, enc_str_x(0, 1));
        write_instruction(mem, 0x200C, enc_add_imm(2, 0, 8));
        write_instruction(mem, 0x2010, enc_add_imm(3, 2, 1));

        // Exception vector handler at 0x10200:
        // 0x10200: ERET
        write_instruction(mem, 0x10200, enc_eret());
    };

    // Run A: Untraced
    {
        Memory memA(0x40000);
        setup_workload(memA);
        CPU cpuA;
        setup_debug_cpu(cpuA, memA, 1, 0x10000);

        // Configure breakpoints and watchpoints
        cpuA.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpuA.get_debug_arch().set_dbgbvr(0, 0x200C);
        cpuA.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u)); // Breakpoint on 0x200C
        cpuA.get_debug_arch().set_dbgwvr(0, 0x5000);
        cpuA.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 3, 2, 0xFF)); // Watchpoint on 0x5000 write

        cpuA.set_program_counter(0x2000);

        // Step 1: 0x2000 (MOVZ X0, #42)
        cpuA.step(memA);
        // Step 2: 0x2004 (MOVZ X1, #0x5000)
        cpuA.step(memA);
        // Step 3: 0x2008 (STR X0, [X1]) -> triggers watchpoint! Jumps to 0x10200
        cpuA.step(memA);
        // Step 4: 0x10200 (ERET) -> returns to 0x2008
        // Disable watchpoint so we can continue
        cpuA.get_debug_arch().set_dbgwcr(0, 0);
        cpuA.step(memA);
        // Step 5: 0x2008 (STR X0, [X1]) -> succeeds
        cpuA.step(memA);
        // Step 6: 0x200C (ADD X2, X0, #8) -> triggers breakpoint! Jumps to 0x10200
        cpuA.step(memA);
        // Step 7: 0x10200 (ERET) -> returns to 0x200C
        // Disable breakpoint and enable single step
        cpuA.get_debug_arch().set_dbgbcr(0, 0);
        cpuA.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
        cpuA.set_daif(0x0u);
        cpuA.set_pstate_ss(true);
        cpuA.step(memA);
        // Step 8: 0x200C (ADD X2, X0, #8) -> single-step retires instruction then takes exception!
        cpuA.step(memA);

        snapshot_untraced = capture_snapshot(cpuA);
        for (std::size_t i = 0; i < ram_untraced.size(); ++i) {
            ram_untraced[i] = memA.read_u8(0x4000 + i);
        }
    }

    // Run B: Traced
    {
        Memory memB(0x40000);
        setup_workload(memB);
        CPU cpuB;
        setup_debug_cpu(cpuB, memB, 1, 0x10000);

        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 2048;
        mgr.configure(cfg);
        mgr.start();

        cpuB.bind_trace_manager(&mgr);
        cpuB.reset_instruction_trace_sequence();

        // Identical configuration
        cpuB.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpuB.get_debug_arch().set_dbgbvr(0, 0x200C);
        cpuB.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));
        cpuB.get_debug_arch().set_dbgwvr(0, 0x5000);
        cpuB.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 3, 2, 0xFF));

        cpuB.set_program_counter(0x2000);

        // Step 1: 0x2000
        cpuB.step(memB);
        // Step 2: 0x2004
        cpuB.step(memB);
        // Step 3: 0x2008 (Watchpoint)
        cpuB.step(memB);
        // Step 4: 0x10200 (ERET)
        cpuB.get_debug_arch().set_dbgwcr(0, 0);
        cpuB.step(memB);
        // Step 5: 0x2008 (STR)
        cpuB.step(memB);
        // Step 6: 0x200C (Breakpoint)
        cpuB.step(memB);
        // Step 7: 0x10200 (ERET)
        cpuB.get_debug_arch().set_dbgbcr(0, 0);
        cpuB.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
        cpuB.set_daif(0x0u);
        cpuB.set_pstate_ss(true);
        cpuB.step(memB);
        // Step 8: 0x200C (Single-step)
        cpuB.step(memB);

        snapshot_traced = capture_snapshot(cpuB);
        for (std::size_t i = 0; i < ram_traced.size(); ++i) {
            ram_traced[i] = memB.read_u8(0x4000 + i);
        }

        mgr.stop();
    }

    // Compare snapshots bit-for-bit
    for (unsigned i = 0; i < 31; ++i) {
        TEST_ASSERT(snapshot_untraced.x[i] == snapshot_traced.x[i]);
    }
    TEST_ASSERT(snapshot_untraced.pc == snapshot_traced.pc);
    TEST_ASSERT(snapshot_untraced.sp_el0 == snapshot_traced.sp_el0);
    TEST_ASSERT(snapshot_untraced.sp_el1 == snapshot_traced.sp_el1);
    TEST_ASSERT(snapshot_untraced.elr_el1 == snapshot_traced.elr_el1);
    TEST_ASSERT(snapshot_untraced.spsr_el1 == snapshot_traced.spsr_el1);
    TEST_ASSERT(snapshot_untraced.esr_el1 == snapshot_traced.esr_el1);
    TEST_ASSERT(snapshot_untraced.far_el1 == snapshot_traced.far_el1);
    TEST_ASSERT(snapshot_untraced.nzcv == snapshot_traced.nzcv);
    TEST_ASSERT(snapshot_untraced.daif == snapshot_traced.daif);
    TEST_ASSERT(ram_untraced == ram_traced);
    std::cout << "PASS (100% bit-exact equivalence)\n";

    // -------------------------------------------------------------------------
    // Test 2: Stream Monotonicity & Event Correlation
    // -------------------------------------------------------------------------
    std::cout << "  Test 2: Stream Monotonicity & Event Correlation... " << std::flush;
    {
        Memory mem(0x40000);
        CPU cpu;
        setup_debug_cpu(cpu, mem, 1, 0x10000);
        setup_workload(mem);

        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 2048;
        mgr.configure(cfg);
        mgr.start();

        cpu.bind_trace_manager(&mgr);
        cpu.reset_instruction_trace_sequence();

        // 1. Breakpoint
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
        cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));
        cpu.set_program_counter(0x2000);
        cpu.step(mem);

        // 2. Watchpoint
        cpu.get_debug_arch().set_dbgbcr(0, 0);
        cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
        cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 3, 2, 0xFF));
        cpu.write_register(1, 0x5000);
        cpu.set_program_counter(0x2008);
        cpu.step(mem);

        // 3. Single-step
        cpu.get_debug_arch().set_dbgwcr(0, 0);
        cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
        cpu.set_daif(0x0u);
        cpu.set_pstate_ss(true);
        cpu.set_program_counter(0x2000);
        cpu.step(mem);

        const auto events = pop_all_events(mgr);
        TEST_ASSERT(!events.empty());

        // Verify strictly monotonic sequence numbers starting at 1
        for (std::size_t i = 0; i < events.size(); ++i) {
            TEST_ASSERT(events[i].sequence_number == i + 1);
            TEST_ASSERT(events[i].core_id == 0);
            if (i > 0) {
                TEST_ASSERT(events[i].virtual_time >= events[i - 1].virtual_time);
            }
        }

        // Verify sequence structure:
        // Breakpoint: DebugEvent (0) -> ExceptionEntered (1)
        TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
        TEST_ASSERT((events[0].flags & static_cast<std::uint32_t>(TraceEventFlags::IsBreakpoint)) != 0);
        TEST_ASSERT(events[1].event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));

        // Watchpoint: DebugEvent (2) -> ExceptionEntered (3)
        TEST_ASSERT(events[2].event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
        TEST_ASSERT((events[2].flags & static_cast<std::uint32_t>(TraceEventFlags::IsWatchpoint)) != 0);
        TEST_ASSERT(events[3].event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));

        // Single-step: InstructionRetired (4) -> DebugEvent (5) -> ExceptionEntered (6)
        TEST_ASSERT(events[4].event_type == static_cast<std::uint16_t>(TraceEventType::InstructionRetired));
        TEST_ASSERT(events[5].event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
        TEST_ASSERT((events[5].flags & static_cast<std::uint32_t>(TraceEventFlags::IsSingleStep)) != 0);
        TEST_ASSERT(events[6].event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));

        mgr.stop();
    }
    std::cout << "PASS (Strict sequence monotonicity & causal pairing)\n";

    // -------------------------------------------------------------------------
    // Test 3: Binary File Persistence (.wtr) & TraceFileReader Ingestion
    // -------------------------------------------------------------------------
    std::cout << "  Test 3: Binary File Persistence (.wtr) & TraceFileReader Ingestion... " << std::flush;
    {
        const std::filesystem::path trace_file = "debug_pipeline_test.wtr";
        if (std::filesystem::exists(trace_file)) {
            std::filesystem::remove(trace_file);
        }

        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.file_output_enabled = true;
        cfg.output_path = trace_file;
        mgr.configure(cfg);
        mgr.start();

        Memory mem(0x40000);
        CPU cpu;
        setup_debug_cpu(cpu, mem, 1, 0x10000);
        setup_workload(mem);
        cpu.bind_trace_manager(&mgr);
        cpu.reset_instruction_trace_sequence();

        // Trigger Breakpoint
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
        cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));
        cpu.set_program_counter(0x2000);
        cpu.step(mem);

        // Trigger Watchpoint
        cpu.get_debug_arch().set_dbgbcr(0, 0);
        cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
        cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 3, 2, 0xFF));
        cpu.write_register(1, 0x5000);
        cpu.set_program_counter(0x2008);
        cpu.step(mem);

        mgr.flush();
        mgr.stop();

        TEST_ASSERT(std::filesystem::exists(trace_file));

        TraceFileReader reader;
        TEST_ASSERT(reader.open(trace_file));
        TEST_ASSERT(reader.is_cleanly_finalized());
        TEST_ASSERT(reader.header().magic == TRACE_FILE_MAGIC);
        TEST_ASSERT(reader.header().schema_version == TRACE_SCHEMA_VERSION);

        std::vector<TraceEvent> file_events;
        TraceEvent ev{};
        while (reader.read_event(ev)) {
            file_events.push_back(ev);
        }
        reader.close();

        TEST_ASSERT(file_events.size() == 4);
        // Event 0: Breakpoint DebugEvent
        TEST_ASSERT(file_events[0].event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
        TEST_ASSERT((file_events[0].flags & static_cast<std::uint32_t>(TraceEventFlags::IsBreakpoint)) != 0);
        TEST_ASSERT(file_events[0].pc == 0x2000);
        TEST_ASSERT(file_events[0].address == 0x2000);
        TEST_ASSERT(file_events[0].data == 0); // Slot 0

        // Event 1: ExceptionEntered
        TEST_ASSERT(file_events[1].event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));

        // Event 2: Watchpoint DebugEvent
        TEST_ASSERT(file_events[2].event_type == static_cast<std::uint16_t>(TraceEventType::DebugEvent));
        TEST_ASSERT((file_events[2].flags & static_cast<std::uint32_t>(TraceEventFlags::IsWatchpoint)) != 0);
        TEST_ASSERT((file_events[2].flags & static_cast<std::uint32_t>(TraceEventFlags::IsWrite)) != 0);
        TEST_ASSERT(file_events[2].pc == 0x2008);
        TEST_ASSERT(file_events[2].address == 0x5000);

        // Event 3: ExceptionEntered
        TEST_ASSERT(file_events[3].event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));

        std::filesystem::remove(trace_file);
    }
    std::cout << "PASS (Valid header, footer, checksum, and recovered events)\n";

    std::cout << "\nAll Debug Trace Pipeline & Integration Tests Passed Successfully!\n";
    return 0;
}

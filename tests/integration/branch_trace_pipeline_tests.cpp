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

void write_inst(Memory& memory, std::uint64_t address, std::uint32_t inst) {
    memory.write_u32(static_cast<std::size_t>(address), inst);
}

// Encoders
constexpr std::uint32_t enc_movz(unsigned rd, std::uint16_t imm16) {
    return 0xD2800000u | (static_cast<std::uint32_t>(imm16) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_add_imm(unsigned rd, unsigned rn, std::uint16_t imm12) {
    return 0x91000000u | ((imm12 & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_sub_imm(unsigned rd, unsigned rn, std::uint16_t imm12) {
    return 0xD1000000u | ((imm12 & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_subs_imm(unsigned rd, unsigned rn, std::uint16_t imm12) {
    return 0xF1000000u | ((imm12 & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_b(std::int32_t word_offset) {
    return 0x14000000u | (static_cast<std::uint32_t>(word_offset) & 0x03FFFFFFu);
}

constexpr std::uint32_t enc_bl(std::int32_t word_offset) {
    return 0x94000000u | (static_cast<std::uint32_t>(word_offset) & 0x03FFFFFFu);
}

constexpr std::uint32_t enc_b_cond(unsigned cond, std::int32_t word_offset) {
    return 0x54000000u | ((static_cast<std::uint32_t>(word_offset) & 0x7FFFFu) << 5u) | (cond & 0xFu);
}

constexpr std::uint32_t enc_cbz_x(unsigned rt, std::int32_t word_offset) {
    return 0xB4000000u | ((static_cast<std::uint32_t>(word_offset) & 0x7FFFFu) << 5u) | (rt & 0x1Fu);
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

constexpr std::uint32_t enc_ret(unsigned rn = 30) {
    return 0xD65F0000u | ((rn & 0x1Fu) << 5u);
}

constexpr std::uint32_t enc_br(unsigned rn) {
    return 0xD61F0000u | ((rn & 0x1Fu) << 5u);
}

constexpr std::uint32_t enc_nop() {
    return 0xD503201Fu;
}

// -----------------------------------------------------------------------------
// Benchmark/Program Setup:
// A realistic control-flow sequence containing loops, conditional branches,
// function call via BL, return via RET, compare-and-branch, and bit tests.
// -----------------------------------------------------------------------------
void populate_program(Memory& mem) {
    // 0x1000: MOV X0, #0               (counter)
    write_inst(mem, 0x1000, enc_movz(0, 0));
    // 0x1004: MOV X1, #10              (loop limit)
    write_inst(mem, 0x1004, enc_movz(1, 10));
    // Loop head:
    // 0x1008: ADD X0, X0, #1
    write_inst(mem, 0x1008, enc_add_imm(0, 0, 1));
    // 0x100C: BL 0x1040                (call subroutine)
    write_inst(mem, 0x100C, enc_bl((0x1040 - 0x100C) / 4));
    // 0x1010: SUBS X2, X0, #10         (check if X0 == 10)
    write_inst(mem, 0x1010, enc_subs_imm(2, 0, 10));
    // 0x1014: B.NE 0x1008              (loop back while != 10)
    write_inst(mem, 0x1014, enc_b_cond(1, (0x1008 - 0x1014) / 4));
    // 0x1018: CBZ X2, 0x1020           (X2 is 0 after loop, so taken to 0x1020)
    write_inst(mem, 0x1018, enc_cbz_x(2, (0x1020 - 0x1018) / 4));
    // 0x101C: NOP
    write_inst(mem, 0x101C, enc_nop());
    // 0x1020: TBNZ X0, #3, 0x1028      (bit 3 of 10 (0b1010) is 1, so taken to 0x1028)
    write_inst(mem, 0x1020, enc_tbnz(0, 3, (0x1028 - 0x1020) / 4));
    // 0x1024: NOP
    write_inst(mem, 0x1024, enc_nop());
    // 0x1028: MOV X3, #0x2000
    write_inst(mem, 0x1028, enc_movz(3, 0x2000));
    // 0x102C: BR X3                    (indirect jump to 0x2000)
    write_inst(mem, 0x102C, enc_br(3));

    // Subroutine at 0x1040:
    // 0x1040: ADD X4, X4, #5
    write_inst(mem, 0x1040, enc_add_imm(4, 4, 5));
    // 0x1044: RET
    write_inst(mem, 0x1044, enc_ret(30));

    // Final landing point at 0x2000:
    // 0x2000: NOP
    write_inst(mem, 0x2000, enc_nop());
}

// -----------------------------------------------------------------------------
// Test 1: Differential State Validation (Untraced vs Traced)
// -----------------------------------------------------------------------------
void test_differential_state_validation() {
    std::cout << "  Integration Test 1: Differential CPU state validation (Traced vs Untraced)... ";

    // --- RUN A: Tracing Disabled ---
    Memory mem_a(0x10000);
    populate_program(mem_a);
    CPU cpu_a;
    cpu_a.set_program_counter(0x1000);

    std::size_t steps_a = 0;
    while (cpu_a.get_program_counter() != 0x2000 && steps_a < 1000) {
        cpu_a.step(mem_a);
        ++steps_a;
    }
    // Execute final NOP at 0x2000
    cpu_a.step(mem_a);
    ++steps_a;

    // --- RUN B: Retirement + Branch Tracing Enabled ---
    Memory mem_b(0x10000);
    populate_program(mem_b);
    CPU cpu_b;
    cpu_b.set_program_counter(0x1000);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 1024;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());
    cpu_b.bind_trace_manager(&mgr);

    std::size_t steps_b = 0;
    while (cpu_b.get_program_counter() != 0x2000 && steps_b < 1000) {
        cpu_b.step(mem_b);
        ++steps_b;
    }
    cpu_b.step(mem_b);
    ++steps_b;

    // 1. Step counts must be identical
    TEST_ASSERT(steps_a == steps_b);

    // 2. All 31 general-purpose registers must be bit-exact
    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
        TEST_ASSERT(cpu_a.read_register(i) == cpu_b.read_register(i));
    }

    // 3. Program counter, SPs, NZCV must be identical
    TEST_ASSERT(cpu_a.get_program_counter() == cpu_b.get_program_counter());
    TEST_ASSERT(cpu_a.get_sp_el0() == cpu_b.get_sp_el0());
    TEST_ASSERT(cpu_a.get_sp_el1() == cpu_b.get_sp_el1());
    TEST_ASSERT(cpu_a.get_nzcv() == cpu_b.get_nzcv());
    TEST_ASSERT(cpu_a.get_current_el() == cpu_b.get_current_el());

    // 4. Guest memory must be identical
    for (std::size_t i = 0; i < 0x3000; i += 4) {
        TEST_ASSERT(mem_a.read_u32(i) == mem_b.read_u32(i));
    }

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS (100% bit-exact across all registers, PC, NZCV, RAM)\n";
}

// -----------------------------------------------------------------------------
// Test 2: Sequence Monotonicity and Event Correlation
// -----------------------------------------------------------------------------
void test_monotonicity_and_event_correlation() {
    std::cout << "  Integration Test 2: Monotonic sequence numbers and event correlation... ";

    Memory mem(0x10000);
    populate_program(mem);
    CPU cpu;
    cpu.set_program_counter(0x1000);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 2048;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());
    cpu.bind_trace_manager(&mgr);

    while (cpu.get_program_counter() != 0x2000) {
        cpu.step(mem);
    }
    cpu.step(mem);

    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_accepted > 0);

    std::uint64_t last_seq = 0;
    std::uint64_t last_vt = 0;
    TraceEvent ev;
    TraceEvent prev_ev{};
    bool have_prev = false;

    std::size_t retirement_count = 0;
    std::size_t branch_count = 0;

    while (const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev)) {
        // Monotonic sequence numbers
        TEST_ASSERT(ev.sequence_number == last_seq + 1);
        last_seq = ev.sequence_number;

        // Monotonic virtual time
        TEST_ASSERT(ev.virtual_time >= last_vt);
        last_vt = ev.virtual_time;

        if (ev.get_event_type() == TraceEventType::InstructionRetired) {
            ++retirement_count;
        } else if (ev.get_event_type() == TraceEventType::BranchTaken) {
            ++branch_count;
            // BranchTaken must immediately follow InstructionRetired for the same branch instruction
            TEST_ASSERT(have_prev);
            TEST_ASSERT(prev_ev.get_event_type() == TraceEventType::InstructionRetired);
            TEST_ASSERT(prev_ev.pc == ev.pc);
            TEST_ASSERT(prev_ev.opcode_or_syndrome == ev.opcode_or_syndrome);
            TEST_ASSERT(ev.sequence_number == prev_ev.sequence_number + 1);
        }

        prev_ev = ev;
        have_prev = true;
    }

    TEST_ASSERT(retirement_count > 0);
    TEST_ASSERT(branch_count > 0);
    TEST_ASSERT(mgr.stop());
    std::cout << "PASS (" << retirement_count << " retirements, " << branch_count << " branches correlated)\n";
}

// -----------------------------------------------------------------------------
// Test 3: Binary Persistence and TraceFileReader Verification
// -----------------------------------------------------------------------------
void test_binary_persistence_and_file_reader() {
    std::cout << "  Integration Test 3: Binary file persistence and TraceFileReader verification... ";

    const std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "winios_branch_pipeline_test";
    std::filesystem::create_directories(temp_dir);
    const std::filesystem::path trace_file = temp_dir / "pipeline_trace.bin";

    if (std::filesystem::exists(trace_file)) {
        std::filesystem::remove(trace_file);
    }

    Memory mem(0x10000);
    populate_program(mem);
    CPU cpu;
    cpu.set_program_counter(0x1000);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.file_output_enabled = true;
    cfg.output_path = trace_file;
    cfg.ring_capacity = 1024;
    cfg.writer_batch_size = 256;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());
    cpu.bind_trace_manager(&mgr);

    while (cpu.get_program_counter() != 0x2000) {
        cpu.step(mem);
    }
    cpu.step(mem);

    TEST_ASSERT(mgr.stop());

    // Read back and validate using TraceFileReader
    TraceFileReader reader;
    TEST_ASSERT(reader.open(trace_file));
    TEST_ASSERT(reader.footer().total_events > 0);

    std::vector<TraceEvent> events;
    TEST_ASSERT(reader.read_all_events(events) > 0);
    TEST_ASSERT(events.size() == reader.footer().total_events);

    for (std::size_t i = 0; i < events.size(); ++i) {
        TEST_ASSERT(events[i].sequence_number == i + 1);
        if (events[i].get_event_type() == TraceEventType::BranchTaken) {
            TEST_ASSERT(events[i].has_flag(TraceEventFlags::IsTaken));
            TEST_ASSERT(events[i].has_flag(TraceEventFlags::HasVirtualAddress));
            TEST_ASSERT(events[i].address != 0);
        }
    }

    reader.close();
    std::filesystem::remove_all(temp_dir);
    std::cout << "PASS (" << events.size() << " events persisted and verified)\n";
}

} // namespace

int main() {
    std::cout << "\n=== WinIOS Phase 52 Stage 7: Branch Trace Integration & Pipeline Tests ===\n\n";

    test_differential_state_validation();
    test_monotonicity_and_event_correlation();
    test_binary_persistence_and_file_reader();

    std::cout << "\n=== All Branch Trace Pipeline Tests PASSED Successfully! ===\n\n";
    return 0;
}

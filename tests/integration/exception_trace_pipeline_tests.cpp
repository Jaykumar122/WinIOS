#include "cpu/cpu.h"
#include "cpu/virtual_time.h"
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

inline std::vector<TraceEvent> pop_all_events(TraceManager& mgr) {
    std::vector<TraceEvent> evs;
    TraceEvent ev;
    auto* ring = const_cast<InMemoryRingBuffer*>(mgr.ring_buffer());
    if (ring) {
        while (ring->try_pop(ev)) {
            evs.push_back(ev);
        }
    }
    return evs;
}

// Instruction encoders
constexpr std::uint32_t enc_movz(unsigned rd, std::uint16_t imm16) {
    return 0xD2800000u | (static_cast<std::uint32_t>(imm16) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_add_imm(unsigned rd, unsigned rn, std::uint16_t imm12) {
    return 0x91000000u | ((imm12 & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_svc(std::uint16_t imm) {
    return 0xD4000001u | ((static_cast<std::uint32_t>(imm) & 0xFFFFu) << 5u);
}

constexpr std::uint32_t enc_eret() {
    return 0xD69F03E0u;
}

constexpr std::uint32_t enc_nop() {
    return 0xD503201Fu;
}

// Populates an exception and return test workload:
// EL0 user code at 0x1000:
//   0x1000: MOV X0, #100
//   0x1004: MOV X1, #200
//   0x1008: SVC #42          -> routes to VBAR_EL1 + 0x400 (0x80400)
//   0x100C: ADD X0, X0, #5
//   0x1010: SVC #99          -> routes to VBAR_EL1 + 0x400 (0x80400)
//   0x1014: ADD X1, X1, #10
//   0x1018: NOP
//
// EL1 handler code at 0x80400 (VBAR_EL1 + 0x400):
//   0x80400: ADD X0, X0, #1
//   0x80404: ERET            -> returns to EL0
void populate_exception_program(Memory& mem) {
    // EL0 user program
    write_inst(mem, 0x1000, enc_movz(0, 100));
    write_inst(mem, 0x1004, enc_movz(1, 200));
    write_inst(mem, 0x1008, enc_svc(42));
    write_inst(mem, 0x100C, enc_add_imm(0, 0, 5));
    write_inst(mem, 0x1010, enc_svc(99));
    write_inst(mem, 0x1014, enc_add_imm(1, 1, 10));
    write_inst(mem, 0x1018, enc_nop());

    // EL1 handler (lower EL, AArch64 vector offset 0x400)
    write_inst(mem, 0x80400, enc_add_imm(0, 0, 1));
    write_inst(mem, 0x80404, enc_eret());
}

struct ArchitecturalStateSnapshot {
    std::uint64_t x[31]{};
    std::uint64_t pc{0};
    std::uint64_t sp_el0{0};
    std::uint64_t sp_el1{0};
    std::uint8_t nzcv{0};
    std::uint8_t daif{0};
    unsigned current_el{0};
    std::uint64_t elr_el1{0};
    std::uint32_t spsr_el1{0};
    std::uint32_t esr_el1{0};
    std::uint64_t far_el1{0};
    std::uint64_t vbar_el1{0};
    std::uint64_t mem_digest{0};
};

ArchitecturalStateSnapshot capture_state(CPU& cpu, Memory& mem) {
    ArchitecturalStateSnapshot s{};
    for (unsigned i = 0; i < 31; ++i) {
        s.x[i] = cpu.read_register(i);
    }
    s.pc = cpu.get_program_counter();
    s.sp_el0 = cpu.get_sp_el0();
    s.sp_el1 = cpu.get_sp_el1();
    s.nzcv = cpu.get_nzcv();
    s.daif = cpu.get_daif();
    s.current_el = static_cast<unsigned>(cpu.get_current_el());
    s.elr_el1 = cpu.get_elr_el1();
    s.spsr_el1 = cpu.get_spsr_el1();
    s.esr_el1 = cpu.get_esr_el1();
    s.far_el1 = cpu.get_far_el1();
    s.vbar_el1 = cpu.get_vbar_el1();

    // Simple FNV-1a hash over memory range 0x1000..0x2000 and 0x80000..0x81000
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t addr = 0x1000; addr < 0x2000; addr += 8) {
        hash = (hash ^ mem.read_u64(addr)) * 1099511628211ULL;
    }
    for (std::size_t addr = 0x80000; addr < 0x81000; addr += 8) {
        hash = (hash ^ mem.read_u64(addr)) * 1099511628211ULL;
    }
    s.mem_digest = hash;
    return s;
}

// -----------------------------------------------------------------------------
// Test 1: Differential Validation (Run A: Disabled vs Run B: Enabled)
// -----------------------------------------------------------------------------
void test_1_differential_validation() {
    std::cout << "  Test 1: Differential validation (Run A: Tracing Disabled vs Run B: Enabled)... ";

    constexpr std::size_t kExecutionSteps = 10;

    // Run A: Tracing Disabled
    ArchitecturalStateSnapshot state_a{};
    {
        Memory mem(0x100000);
        populate_exception_program(mem);
        CPU cpu;
        winios::VirtualTime vtime;
        cpu.bind_virtual_time(&vtime, cpu.get_core_id());
        cpu.set_architectural_exceptions(true);
        cpu.set_vbar_el1(0x80000);
        cpu.set_current_el(0);
        cpu.set_program_counter(0x1000);

        // Run exactly kExecutionSteps instructions/transitions
        for (std::size_t i = 0; i < kExecutionSteps; ++i) {
            cpu.step(mem);
        }
        state_a = capture_state(cpu, mem);
    }

    // Run B: Full Tracing Enabled (Retirement, Branch, ExceptionEntered, ExceptionReturned)
    ArchitecturalStateSnapshot state_b{};
    {
        Memory mem(0x100000);
        populate_exception_program(mem);
        CPU cpu;
        winios::VirtualTime vtime;
        cpu.bind_virtual_time(&vtime, cpu.get_core_id());

        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 256;
        cfg.file_output_enabled = false;
        TEST_ASSERT(mgr.configure(cfg));
        TEST_ASSERT(mgr.start());
        cpu.bind_trace_manager(&mgr);

        cpu.set_architectural_exceptions(true);
        cpu.set_vbar_el1(0x80000);
        cpu.set_current_el(0);
        cpu.set_program_counter(0x1000);

        for (std::size_t i = 0; i < kExecutionSteps; ++i) {
            cpu.step(mem);
        }
        state_b = capture_state(cpu, mem);

        TEST_ASSERT(mgr.stop());
    }

    // Verify 100% bit-exact equivalence of all architectural state
    for (unsigned i = 0; i < 31; ++i) {
        TEST_ASSERT(state_a.x[i] == state_b.x[i]);
    }
    TEST_ASSERT(state_a.pc == state_b.pc);
    TEST_ASSERT(state_a.sp_el0 == state_b.sp_el0);
    TEST_ASSERT(state_a.sp_el1 == state_b.sp_el1);
    TEST_ASSERT(state_a.nzcv == state_b.nzcv);
    TEST_ASSERT(state_a.daif == state_b.daif);
    TEST_ASSERT(state_a.current_el == state_b.current_el);
    TEST_ASSERT(state_a.elr_el1 == state_b.elr_el1);
    TEST_ASSERT(state_a.spsr_el1 == state_b.spsr_el1);
    TEST_ASSERT(state_a.esr_el1 == state_b.esr_el1);
    TEST_ASSERT(state_a.far_el1 == state_b.far_el1);
    TEST_ASSERT(state_a.vbar_el1 == state_b.vbar_el1);
    TEST_ASSERT(state_a.mem_digest == state_b.mem_digest);

    std::cout << "PASS (100% Bit-Exact Equivalence)\n";
}

// -----------------------------------------------------------------------------
// Test 2: Event Stream Monotonicity, Ordering, and Correlation
// -----------------------------------------------------------------------------
void test_2_stream_monotonicity_and_correlation() {
    std::cout << "  Test 2: Stream monotonicity, ordering, and correlation... ";

    Memory mem(0x100000);
    populate_exception_program(mem);
    CPU cpu;
    winios::VirtualTime vtime;
    cpu.bind_virtual_time(&vtime, cpu.get_core_id());

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 256;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());
    cpu.bind_trace_manager(&mgr);

    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000);
    cpu.set_current_el(0);
    cpu.set_program_counter(0x1000);

    // Step sequence:
    // Step 1: MOV X0, #100           -> emits InstructionRetired (seq 1)
    // Step 2: MOV X1, #200           -> emits InstructionRetired (seq 2)
    // Step 3: SVC #42                -> emits InstructionRetired (seq 3), ExceptionEntered (seq 4)
    // Step 4: ADD X0, X0, #1         -> emits InstructionRetired (seq 5)
    // Step 5: ERET                   -> emits InstructionRetired (seq 6), ExceptionReturned (seq 7)
    // Step 6: ADD X0, X0, #5         -> emits InstructionRetired (seq 8)
    for (int i = 0; i < 6; ++i) {
        cpu.step(mem);
    }

    std::vector<TraceEvent> evs = pop_all_events(mgr);
    TEST_ASSERT(evs.size() == 8);

    // Verify sequence monotonicity
    for (std::size_t i = 0; i < evs.size(); ++i) {
        TEST_ASSERT(evs[i].sequence_number == i + 1);
        TEST_ASSERT(evs[i].core_id == 0);
        if (i > 0) {
            TEST_ASSERT(evs[i].virtual_time >= evs[i - 1].virtual_time);
        }
    }

    // Verify individual events in order:
    // 0: MOV X0, #100 at 0x1000
    TEST_ASSERT(evs[0].get_event_type() == TraceEventType::InstructionRetired);
    TEST_ASSERT(evs[0].pc == 0x1000);

    // 1: MOV X1, #200 at 0x1004
    TEST_ASSERT(evs[1].get_event_type() == TraceEventType::InstructionRetired);
    TEST_ASSERT(evs[1].pc == 0x1004);

    // 2: SVC #42 retirement at 0x1008
    TEST_ASSERT(evs[2].get_event_type() == TraceEventType::InstructionRetired);
    TEST_ASSERT(evs[2].pc == 0x1008);

    // 3: ExceptionEntered at 0x1008 -> vector 0x80400
    TEST_ASSERT(evs[3].get_event_type() == TraceEventType::ExceptionEntered);
    TEST_ASSERT(evs[3].pc == 0x1008);
    TEST_ASSERT(evs[3].address == 0x80400);
    TEST_ASSERT(evs[3].exception_level == 1);
    TEST_ASSERT((evs[3].opcode_or_syndrome >> 26u) == 0x15u); // SVC64

    // 4: ADD X0, X0, #1 at 0x80400
    TEST_ASSERT(evs[4].get_event_type() == TraceEventType::InstructionRetired);
    TEST_ASSERT(evs[4].pc == 0x80400);

    // 5: ERET retirement at 0x80404
    TEST_ASSERT(evs[5].get_event_type() == TraceEventType::InstructionRetired);
    TEST_ASSERT(evs[5].pc == 0x80404);

    // 6: ExceptionReturned from 0x80404 -> 0x100C
    TEST_ASSERT(evs[6].get_event_type() == TraceEventType::ExceptionReturned);
    TEST_ASSERT(evs[6].pc == 0x80404);
    TEST_ASSERT(evs[6].address == 0x100C);
    TEST_ASSERT(evs[6].exception_level == 0); // Destination EL is EL0

    // 7: ADD X0, X0, #5 at 0x100C
    TEST_ASSERT(evs[7].get_event_type() == TraceEventType::InstructionRetired);
    TEST_ASSERT(evs[7].pc == 0x100C);

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 3: Binary File Sink and TraceFileReader Ingestion
// -----------------------------------------------------------------------------
void test_3_binary_file_persistence_and_reader() {
    std::cout << "  Test 3: Binary file persistence (.wtr) and TraceFileReader ingestion... ";

    const std::filesystem::path trace_path = std::filesystem::temp_directory_path() / "winios_stage8_pipeline.wtr";
    std::filesystem::remove(trace_path);

    Memory mem(0x100000);
    populate_exception_program(mem);
    CPU cpu;
    winios::VirtualTime vtime;
    cpu.bind_virtual_time(&vtime, cpu.get_core_id());

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.file_output_enabled = true;
    cfg.output_path = trace_path;
    cfg.ring_capacity = 256;
    cfg.writer_batch_size = 64;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());
    cpu.bind_trace_manager(&mgr);

    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000);
    cpu.set_current_el(0);
    cpu.set_program_counter(0x1000);

    for (int i = 0; i < 6; ++i) {
        cpu.step(mem);
    }

    TEST_ASSERT(mgr.stop());
    TEST_ASSERT(std::filesystem::exists(trace_path));

    TraceFileReader reader;
    TEST_ASSERT(reader.open(trace_path));
    TEST_ASSERT(reader.is_open());
    TEST_ASSERT(reader.footer().total_events == 8);

    std::vector<TraceEvent> events;
    TEST_ASSERT(reader.read_all_events(events));
    TEST_ASSERT(events.size() == 8);

    // Verify round-trip event integrity
    TEST_ASSERT(events[2].get_event_type() == TraceEventType::InstructionRetired);
    TEST_ASSERT(events[3].get_event_type() == TraceEventType::ExceptionEntered);
    TEST_ASSERT(events[3].pc == 0x1008);
    TEST_ASSERT(events[3].address == 0x80400);

    TEST_ASSERT(events[5].get_event_type() == TraceEventType::InstructionRetired);
    TEST_ASSERT(events[6].get_event_type() == TraceEventType::ExceptionReturned);
    TEST_ASSERT(events[6].pc == 0x80404);
    TEST_ASSERT(events[6].address == 0x100C);

    reader.close();
    std::filesystem::remove(trace_path);
    std::cout << "PASS\n";
}

} // namespace

int main() {
    std::cout << "\n=== WinIOS Phase 52 Stage 8: Exception Trace Pipeline & Integration Tests ===\n\n";

    test_1_differential_validation();
    test_2_stream_monotonicity_and_correlation();
    test_3_binary_file_persistence_and_reader();

    std::cout << "\n=== All Exception Trace Pipeline Tests PASSED Successfully! ===\n\n";
    return 0;
}

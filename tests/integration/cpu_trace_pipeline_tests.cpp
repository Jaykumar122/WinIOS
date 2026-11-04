#include "cpu/cpu.h"
#include "memory/memory.h"
#include "trace/trace_manager.h"
#include "trace/trace_file_reader.h"
#include "trace/trace_event.h"

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

const std::filesystem::path kTestDir = std::filesystem::temp_directory_path() / "winios_cpu_trace_pipeline_tests";

void setup_test_dir() {
    std::error_code ec;
    std::filesystem::create_directories(kTestDir, ec);
}

void cleanup_test_dir() {
    std::error_code ec;
    std::filesystem::remove_all(kTestDir, ec);
}

void write_instruction(Memory& memory, std::uint64_t address, std::uint32_t inst) {
    memory.write_u32(static_cast<std::size_t>(address), inst);
}

// Runs a standard deterministic sequence of instructions on the given CPU and Memory
void run_deterministic_sequence(CPU& cpu, Memory& memory) {
    // 0x1000: MOV X0, #100       (0xD2800C80)
    // 0x1004: MOV X1, #200       (0xD2801901)
    // 0x1008: ADD X2, X0, X1     (0x8B010002)
    // 0x100C: SUB X3, X2, #50    (0xD100C843)
    // 0x1010: B #8 (to 0x1018)   (0x14000002)
    // 0x1014: MOV X4, #999       (0xD2807CE4) - skipped
    // 0x1018: MOV X4, #42        (0xD2800544)
    write_instruction(memory, 0x1000, 0xD2800C80u);
    write_instruction(memory, 0x1004, 0xD2801901u);
    write_instruction(memory, 0x1008, 0x8B010002u);
    write_instruction(memory, 0x100C, 0xD100C843u);
    write_instruction(memory, 0x1010, 0x14000002u);
    write_instruction(memory, 0x1014, 0xD2807CE4u);
    write_instruction(memory, 0x1018, 0xD2800544u);

    cpu.set_program_counter(0x1000);
    // Execute 5 instructions: 0x1000, 0x1004, 0x1008, 0x100C, 0x1010 (branch), 0x1018
    for (int i = 0; i < 6; ++i) {
        cpu.step(memory);
    }
}

void test_15_differential_cpu_state_validation() {
    std::cout << "  Test 15: Differential CPU state validation (Tracing Disabled vs Enabled)... ";

    // Run A: Tracing Disabled
    Memory memA(0x10000);
    CPU cpuA;
    cpuA.bind_trace_manager(nullptr);
    run_deterministic_sequence(cpuA, memA);

    // Run B: Tracing Enabled
    Memory memB(0x10000);
    CPU cpuB;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 256;
    cfg.file_output_enabled = false;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    cpuB.bind_trace_manager(&mgr);
    run_deterministic_sequence(cpuB, memB);

    // Assert that every piece of architectural state is 100% IDENTICAL:
    // 1. General-purpose registers X0..X30
    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
        TEST_ASSERT(cpuA.read_register(i) == cpuB.read_register(i));
    }

    // 2. Program Counter
    TEST_ASSERT(cpuA.get_program_counter() == cpuB.get_program_counter());

    // 3. Stack Pointer
    TEST_ASSERT(cpuA.get_stack_pointer() == cpuB.get_stack_pointer());

    // 4. NZCV flags
    TEST_ASSERT(cpuA.get_nzcv() == cpuB.get_nzcv());

    // 5. Exception level & DAIF
    TEST_ASSERT(cpuA.get_current_el() == cpuB.get_current_el());
    TEST_ASSERT(cpuA.get_daif() == cpuB.get_daif());

    // 6. Halt and waiting flags
    TEST_ASSERT(cpuA.is_halted() == cpuB.is_halted());
    TEST_ASSERT(cpuA.is_waiting() == cpuB.is_waiting());

    // 7. Memory contents (sample memory at execution addresses)
    for (std::size_t addr = 0x1000; addr <= 0x1018; addr += 4) {
        TEST_ASSERT(memA.read_u32(addr) == memB.read_u32(addr));
    }

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS (All architectural states identical)\n";
}

void test_16_repeated_execution_no_duplicate_events() {
    std::cout << "  Test 16: Repeated execution does not duplicate events... ";
    Memory memory(0x10000);
    CPU cpu;
    write_instruction(memory, 0x1000, 0xD503201Fu); // NOP
    write_instruction(memory, 0x1004, 0xD503201Fu); // NOP
    cpu.set_program_counter(0x1000);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 64;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    cpu.bind_trace_manager(&mgr);
    cpu.step(memory); // 1
    cpu.step(memory); // 2

    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_submitted == 2);
    TEST_ASSERT(stats.events_accepted == 2);

    TraceEvent ev1, ev2;
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev1));
    TEST_ASSERT(const_cast<InMemoryRingBuffer*>(mgr.ring_buffer())->try_pop(ev2));
    TEST_ASSERT(ev1.sequence_number == 1);
    TEST_ASSERT(ev2.sequence_number == 2);
    TEST_ASSERT(ev1.sequence_number != ev2.sequence_number);

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

void test_17_trace_file_persistence_and_reader_verification() {
    std::cout << "  Test 17: Trace file persistence and reader verification... ";
    const auto file_path = kTestDir / "cpu_retired_stream.wtr";
    std::filesystem::remove(file_path);

    Memory memory(0x10000);
    CPU cpu;
    write_instruction(memory, 0x1000, 0xD2800020u); // MOV X0, #1
    write_instruction(memory, 0x1004, 0xD2800041u); // MOV X1, #2
    write_instruction(memory, 0x1008, 0x8B010002u); // ADD X2, X0, X1
    write_instruction(memory, 0x100C, 0xD503201Fu); // NOP
    cpu.set_program_counter(0x1000);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.file_output_enabled = true;
    cfg.output_path = file_path;
    cfg.ring_capacity = 256;
    cfg.writer_batch_size = 64;

    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    cpu.bind_trace_manager(&mgr);
    for (int i = 0; i < 4; ++i) {
        cpu.step(memory);
    }

    TEST_ASSERT(mgr.stop());
    TEST_ASSERT(mgr.state() == TraceManagerState::Stopped);

    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_persisted == 4);

    // Read back with Stage 4 TraceFileReader
    TraceFileReader reader;
    TEST_ASSERT(reader.open(file_path));
    TEST_ASSERT(reader.is_cleanly_finalized());
    TEST_ASSERT(reader.footer().total_events == 4);

    std::vector<TraceEvent> events;
    TEST_ASSERT(reader.read_all_events(events) == 4);
    TEST_ASSERT(events.size() == 4);

    TEST_ASSERT(events[0].pc == 0x1000 && events[0].opcode_or_syndrome == 0xD2800020u);
    TEST_ASSERT(events[1].pc == 0x1004 && events[1].opcode_or_syndrome == 0xD2800041u);
    TEST_ASSERT(events[2].pc == 0x1008 && events[2].opcode_or_syndrome == 0x8B010002u);
    TEST_ASSERT(events[3].pc == 0x100C && events[3].opcode_or_syndrome == 0xD503201Fu);

    reader.close();
    std::cout << "PASS (4 retired events persisted and verified)\n";
}

void test_18_trace_shutdown_drains_accepted_events() {
    std::cout << "  Test 18: Trace shutdown drains accepted events correctly... ";
    const auto file_path = kTestDir / "cpu_shutdown_drain.wtr";
    std::filesystem::remove(file_path);

    Memory memory(0x10000);
    CPU cpu;
    // Fill 100 instructions
    for (std::uint64_t addr = 0x1000; addr < 0x1000 + (100 * 4); addr += 4) {
        write_instruction(memory, addr, 0xD503201Fu); // NOP
    }
    cpu.set_program_counter(0x1000);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.file_output_enabled = true;
    cfg.output_path = file_path;
    cfg.ring_capacity = 512;
    cfg.writer_batch_size = 256;

    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    cpu.bind_trace_manager(&mgr);
    for (int i = 0; i < 100; ++i) {
        cpu.step(memory);
    }

    // Immediately stop without flush; consumer must drain all 100 events
    TEST_ASSERT(mgr.stop());

    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_persisted == 100);

    TraceFileReader reader;
    TEST_ASSERT(reader.open(file_path));
    TEST_ASSERT(reader.is_cleanly_finalized());
    TEST_ASSERT(reader.footer().total_events == 100);
    reader.close();
    std::cout << "PASS\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Phase 52 Stage 6: CPU Trace Pipeline Tests ===\n\n";

    setup_test_dir();
    try {
        test_15_differential_cpu_state_validation();
        test_16_repeated_execution_no_duplicate_events();
        test_17_trace_file_persistence_and_reader_verification();
        test_18_trace_shutdown_drains_accepted_events();

        cleanup_test_dir();
        std::cout << "\n=== All CPU Trace Pipeline Tests PASSED Successfully! ===\n";
        return 0;
    } catch (const std::exception& e) {
        cleanup_test_dir();
        std::cerr << "\n=== Test FAILED with exception: " << e.what() << " ===\n";
        return 1;
    }
}

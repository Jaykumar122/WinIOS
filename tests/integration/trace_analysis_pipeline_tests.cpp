#include "cpu/cpu.h"
#include "memory/memory.h"
#include "trace/trace_analyzer.h"
#include "trace/trace_analysis_report.h"
#include "trace/trace_config.h"
#include "trace/trace_manager.h"
#include "trace/trace_types.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

const std::filesystem::path kPipelineTestDir =
    std::filesystem::temp_directory_path() / "winios_trace_pipeline_stage15_tests";

void setup_test_dir() {
    std::error_code ec;
    std::filesystem::create_directories(kPipelineTestDir, ec);
}

void cleanup_test_dir() {
    std::error_code ec;
    std::filesystem::remove_all(kPipelineTestDir, ec);
}

// -----------------------------------------------------------------------------
// Test 1: End-to-end Trace Generation & Inspection
// -----------------------------------------------------------------------------
void test_end_to_end_trace_generation_and_inspection() {
    const auto trace_path = kPipelineTestDir / "pipeline_test1.wtr";
    if (std::filesystem::exists(trace_path)) std::filesystem::remove(trace_path);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.file_output_enabled = true;
    cfg.output_path = trace_path.string();
    cfg.event_types.set(TraceEventType::InstructionRetired, true);
    cfg.event_types.set(TraceEventType::BranchTaken, true);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    Memory mem(0x10000);
    cpu.bind_trace_manager(&mgr);
    cpu.set_program_counter(0x1000);
    cpu.set_architectural_exceptions(false);

    // Code: NOP, B +1
    mem.write_u32(0x1000, 0xD503201Fu); // NOP
    mem.write_u32(0x1004, 0x14000002u); // B 0x100C
    mem.write_u32(0x1008, 0xD503201Fu); // NOP (skipped)
    mem.write_u32(0x100C, 0xD503201Fu); // NOP

    cpu.step(mem); // NOP at 0x1000
    cpu.step(mem); // B at 0x1004
    cpu.step(mem); // NOP at 0x100C

    TEST_ASSERT(mgr.stop());

    TraceAnalyzer analyzer;
    const auto res = analyzer.inspect_file(trace_path);
    TEST_ASSERT(res.integrity_status == TraceFileIntegrityStatus::ValidComplete);
    TEST_ASSERT(res.header_valid);
    TEST_ASSERT(res.footer_valid);
    TEST_ASSERT(res.crc_valid);
    TEST_ASSERT(res.event_count >= 4); // 3 retired + 1 branch
    TEST_ASSERT(res.per_core_event_counts.at(0) >= 4);
}

// -----------------------------------------------------------------------------
// Test 2: Full Subsystem Execution Analysis Pipeline
// -----------------------------------------------------------------------------
void test_full_subsystem_analysis_pipeline() {
    const auto trace_path = kPipelineTestDir / "pipeline_test2.wtr";
    if (std::filesystem::exists(trace_path)) std::filesystem::remove(trace_path);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.file_output_enabled = true;
    cfg.output_path = trace_path.string();
    cfg.event_types.set(TraceEventType::InstructionRetired, true);
    cfg.event_types.set(TraceEventType::MemoryRead, true);
    cfg.event_types.set(TraceEventType::MemoryWrite, true);
    cfg.event_types.set(TraceEventType::SystemRegisterAccess, true);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    Memory mem(0x20000);
    cpu.bind_trace_manager(&mgr);
    cpu.set_program_counter(0x1000);
    cpu.set_architectural_exceptions(false);

    // Write X0 = 0x4000, write memory [0x4000] = 0x12345678, read memory
    cpu.write_register(0, 0x4000ULL);
    cpu.write_register(1, 0x12345678ULL);

    // Code:
    // STR X1, [X0]
    // LDR X2, [X0]
    mem.write_u32(0x1000, 0xF9000001u); // STR X1, [X0]
    mem.write_u32(0x1004, 0xF9400002u); // LDR X2, [X0]

    cpu.step(mem);
    cpu.step(mem);

    TEST_ASSERT(mgr.stop());

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(trace_path);
    TEST_ASSERT(rep.instructions.total_retired == 2);
    TEST_ASSERT(rep.memory.total_writes == 1);
    TEST_ASSERT(rep.memory.total_reads == 1);
    TEST_ASSERT(rep.memory.min_address == 0x4000);
    TEST_ASSERT(rep.memory.max_address == 0x4000);
}

// -----------------------------------------------------------------------------
// Test 3: Timer & Interrupt Causal Pipeline Analysis
// -----------------------------------------------------------------------------
void test_timer_and_interrupt_pipeline_analysis() {
    const auto trace_path = kPipelineTestDir / "pipeline_test3.wtr";
    if (std::filesystem::exists(trace_path)) std::filesystem::remove(trace_path);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.file_output_enabled = true;
    cfg.output_path = trace_path.string();
    cfg.event_types.set(TraceEventType::TimerEvent, true);
    cfg.event_types.set(TraceEventType::InterruptDelivered, true);
    cfg.event_types.set(TraceEventType::ExceptionEntered, true);
    cfg.event_types.set(TraceEventType::ExceptionReturned, true);
    cfg.event_types.set(TraceEventType::ContextChanged, true);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    Memory mem(0x20000);
    cpu.bind_trace_manager(&mgr);
    cpu.start();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x10000);
    cpu.set_daif(0); // Unmask IRQ
    cpu.set_program_counter(0x2000);

    // Main: NOP; NOP
    mem.write_u32(0x2000, 0xD503201Fu);
    mem.write_u32(0x2004, 0xD503201Fu);

    // ISR at EL1h IRQ vector 0x10280:
    // MSR CNTP_CTL_EL0, X0 (where X0=0 to disable timer)
    // ERET
    mem.write_u32(0x10280, 0xD513E220u); // MSR CNTP_CTL_EL0, X0
    mem.write_u32(0x10284, 0xD69F03E0u); // ERET

    cpu.write_register(0, 0ULL);
    cpu.set_guest_counter(100ULL);
    cpu.set_phys_timer(CPU::TimerState{true, false, false, 200ULL});

    // Step 1: NOP
    cpu.step(mem);
    // Step 2: advance counter and step -> timer interrupt fires!
    cpu.set_guest_counter(250ULL);
    cpu.step(mem); // Enters vector at 0x10280

    // Step 3 & 4: ISR execution
    cpu.step(mem); // Disable timer
    cpu.step(mem); // ERET returns to 0x2004

    TEST_ASSERT(mgr.stop());

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(trace_path);
    TEST_ASSERT(!rep.causal_chains.empty());

    bool found_confirmed = false;
    for (const auto& c : rep.causal_chains) {
        if (c.status == CausalCorrelationStatus::ConfirmedSequence) {
            found_confirmed = true;
            TEST_ASSERT(c.core_id == 0);
            TEST_ASSERT(c.expire_seq > 0);
            TEST_ASSERT(c.assert_seq > c.expire_seq);
            TEST_ASSERT(c.deliver_seq > c.assert_seq);
            TEST_ASSERT(c.entry_seq > c.deliver_seq);
            TEST_ASSERT(c.context_change_seq > c.entry_seq);
        }
    }
    TEST_ASSERT(found_confirmed);
}

// -----------------------------------------------------------------------------
// Test 4: Streaming Multi-block Large File Pipeline
// -----------------------------------------------------------------------------
void test_streaming_multiblock_large_file() {
    const auto trace_path = kPipelineTestDir / "pipeline_test4.wtr";
    if (std::filesystem::exists(trace_path)) std::filesystem::remove(trace_path);

    constexpr std::size_t kTotalEvents = 2500;
    {
        TraceFileWriter writer(100);
        TEST_ASSERT(writer.open(trace_path, true));
        for (std::size_t i = 1; i <= kTotalEvents; ++i) {
            TraceEvent ev{};
            ev.sequence_number = i;
            ev.virtual_time = i * 10;
            ev.pc = 0x1000 + (i % 64) * 4;
            ev.event_type = static_cast<std::uint16_t>(TraceEventType::InstructionRetired);
            TEST_ASSERT(writer.write_event(ev));
        }
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto insp = analyzer.inspect_file(trace_path);
    TEST_ASSERT(insp.integrity_status == TraceFileIntegrityStatus::ValidComplete);
    TEST_ASSERT(insp.event_count == kTotalEvents);
    TEST_ASSERT(insp.block_count == 25);

    std::size_t count = 0;
    analyzer.stream_timeline(trace_path, TraceFilter{}, [&](const TimelineEntry& te) {
        TEST_ASSERT(te.sequence_number == count + 1);
        count++;
        return true;
    });
    TEST_ASSERT(count == kTotalEvents);
}

// -----------------------------------------------------------------------------
// Test 5: Formatted Summary and Validation Reports
// -----------------------------------------------------------------------------
void test_formatted_reports() {
    const auto trace_path = kPipelineTestDir / "pipeline_test5.wtr";
    if (std::filesystem::exists(trace_path)) std::filesystem::remove(trace_path);

    {
        TraceFileWriter writer(50);
        TEST_ASSERT(writer.open(trace_path, true));
        TraceEvent ev{};
        ev.sequence_number = 1;
        ev.event_type = static_cast<std::uint16_t>(TraceEventType::InstructionRetired);
        ev.pc = 0x1000;
        writer.write_event(ev);
        writer.close();
    }

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(trace_path);

    const std::string insp_text = format_inspection_report(rep.inspection);
    TEST_ASSERT(insp_text.find("Integrity Status:  ValidComplete") != std::string::npos);

    const std::string sum_text = format_summary_report(rep);
    TEST_ASSERT(sum_text.find("Total Retired:     1") != std::string::npos);

    const std::string val_text = format_validation_report(rep);
    TEST_ASSERT(val_text.find("Validation Status: PASSED") != std::string::npos);
}

// -----------------------------------------------------------------------------
// Test 6: JSON Export Integrity
// -----------------------------------------------------------------------------
void test_json_export_integrity() {
    const auto trace_path = kPipelineTestDir / "pipeline_test6.wtr";
    if (std::filesystem::exists(trace_path)) std::filesystem::remove(trace_path);

    {
        TraceFileWriter writer(50);
        TEST_ASSERT(writer.open(trace_path, true));
        TraceEvent ev{};
        ev.sequence_number = 1;
        ev.event_type = static_cast<std::uint16_t>(TraceEventType::InstructionRetired);
        ev.pc = 0x8000;
        writer.write_event(ev);
        writer.close();
    }

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(trace_path);
    const std::string json = export_report_to_json(rep);

    // Basic JSON syntax validation checks:
    TEST_ASSERT(json.front() == '{');
    TEST_ASSERT(json.back() == '\n');
    TEST_ASSERT(json.find("\"status\": \"ValidComplete\"") != std::string::npos);
    TEST_ASSERT(json.find("\"total_retired\": 1") != std::string::npos);
    TEST_ASSERT(json.find("\"first_pc\": \"0x8000\"") != std::string::npos);
}

// -----------------------------------------------------------------------------
// Test 7: Corrupted Block Pipeline Diagnostics
// -----------------------------------------------------------------------------
void test_corrupted_block_pipeline_diagnostics() {
    const auto trace_path = kPipelineTestDir / "pipeline_test7.wtr";
    if (std::filesystem::exists(trace_path)) std::filesystem::remove(trace_path);

    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(trace_path, true));
        for (std::uint64_t i = 1; i <= 10; ++i) {
            TraceEvent ev{};
            ev.sequence_number = i;
            ev.event_type = static_cast<std::uint16_t>(TraceEventType::InstructionRetired);
            writer.write_event(ev);
        }
        writer.close();
    }

    // Corrupt one byte inside block 0 payload
    {
        std::fstream fs(trace_path.string(), std::ios::in | std::ios::out | std::ios::binary);
        fs.seekp(100);
        const char bad = '\xCC';
        fs.write(&bad, 1);
    }

    TraceAnalyzer analyzer;
    const auto insp = analyzer.inspect_file(trace_path);
    TEST_ASSERT(insp.integrity_status == TraceFileIntegrityStatus::CrcMismatch);
    TEST_ASSERT(!insp.crc_valid);
    TEST_ASSERT(!insp.diagnostics.empty());
    TEST_ASSERT(insp.diagnostics[0].code == DiagnosticCode::BlockPayloadCrcMismatch);
}

// -----------------------------------------------------------------------------
// Test 8: Unfinalized Trace Graceful Salvage
// -----------------------------------------------------------------------------
void test_unfinalized_trace_graceful_recovery() {
    const auto trace_path = kPipelineTestDir / "pipeline_test8.wtr";
    if (std::filesystem::exists(trace_path)) std::filesystem::remove(trace_path);

    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(trace_path, true));
        for (std::uint64_t i = 1; i <= 20; ++i) {
            TraceEvent ev{};
            ev.sequence_number = i;
            ev.event_type = static_cast<std::uint16_t>(TraceEventType::InstructionRetired);
            writer.write_event(ev);
        }
        // Flush blocks, but do not close (simulate abrupt emulator termination)
        writer.flush();
    }

    // Strip footer
    const auto sz = std::filesystem::file_size(trace_path);
    std::filesystem::resize_file(trace_path, sz - TRACE_FILE_FOOTER_SIZE);

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(trace_path);
    TEST_ASSERT(!rep.inspection.footer_valid);
    TEST_ASSERT(rep.inspection.integrity_status == TraceFileIntegrityStatus::InvalidFooter);
    // Even without footer, all 20 valid events are successfully salvaged and summarized!
    TEST_ASSERT(rep.instructions.total_retired == 20);
}

} // namespace

int main() {
    std::cout << "=== WinIOS Phase 52 Stage 15: Trace Analysis Pipeline Integration Tests ===\n";

    setup_test_dir();

    std::cout << "  Test 1: End-to-end trace generation and inspection... " << std::flush;
    test_end_to_end_trace_generation_and_inspection();
    std::cout << "PASS\n";

    std::cout << "  Test 2: Full subsystem execution analysis pipeline... " << std::flush;
    test_full_subsystem_analysis_pipeline();
    std::cout << "PASS\n";

    std::cout << "  Test 3: Timer & interrupt causal pipeline analysis... " << std::flush;
    test_timer_and_interrupt_pipeline_analysis();
    std::cout << "PASS\n";

    std::cout << "  Test 4: Streaming multi-block large file pipeline... " << std::flush;
    test_streaming_multiblock_large_file();
    std::cout << "PASS\n";

    std::cout << "  Test 5: Formatted inspection, summary, and validation reports... " << std::flush;
    test_formatted_reports();
    std::cout << "PASS\n";

    std::cout << "  Test 6: Machine-readable JSON export integrity... " << std::flush;
    test_json_export_integrity();
    std::cout << "PASS\n";

    std::cout << "  Test 7: Corrupted block pipeline diagnostics... " << std::flush;
    test_corrupted_block_pipeline_diagnostics();
    std::cout << "PASS\n";

    std::cout << "  Test 8: Unfinalized trace graceful event salvage... " << std::flush;
    test_unfinalized_trace_graceful_recovery();
    std::cout << "PASS\n";

    cleanup_test_dir();

    std::cout << "All 8/8 Phase 52 Stage 15 Trace Analysis Pipeline Tests PASSED successfully!\n";
    return 0;
}

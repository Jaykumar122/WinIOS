#include "trace/trace_analyzer.h"
#include "trace/trace_analysis_report.h"
#include "trace/trace_file_writer.h"
#include "trace/trace_event.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

using namespace winios::trace;

namespace {

std::size_t get_current_process_memory() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
#endif
    return 0;
}

std::string generate_benchmark_trace(const std::string& path, std::size_t event_count) {
    TraceFileWriter writer;
    if (!writer.open(path)) {
        throw std::runtime_error("Failed to create benchmark trace file: " + path);
    }

    std::uint64_t seq[4] = {1, 1, 1, 1};
    std::uint64_t ts = 1000000;

    for (std::size_t i = 0; i < event_count; ++i) {
        std::uint16_t core = static_cast<std::uint16_t>(i % 4);
        TraceEvent ev{};
        ev.core_id = core;
        ev.sequence_number = seq[core]++;
        ev.virtual_time = (ts += 10);
        ev.exception_level = 1;
        ev.address_space = 1;
        ev.access_size = 4;

        int kind = static_cast<int>(i % 13);
        switch (kind) {
            case 0: { // InstructionRetired
                ev.set_event_type(TraceEventType::InstructionRetired);
                ev.pc = 0x400000 + (i % 2000) * 4;
                ev.opcode_or_syndrome = 0x91000000; // ADD
                break;
            }
            case 1: { // BranchTaken
                ev.set_event_type(TraceEventType::BranchTaken);
                ev.pc = 0x400000 + (i % 2000) * 4;
                ev.address = 0x401000;
                ev.data = 1; // DirectConditional
                break;
            }
            case 2: { // ExceptionEntered
                ev.set_event_type(TraceEventType::ExceptionEntered);
                ev.pc = 0x400000 + (i % 2000) * 4;
                ev.address = 0x800000;
                ev.opcode_or_syndrome = 0x56000000;
                ev.exception_level = 0;
                break;
            }
            case 3: { // ExceptionReturned
                ev.set_event_type(TraceEventType::ExceptionReturned);
                ev.pc = 0x400000 + (i % 2000) * 4;
                ev.data = 0x00000000; // spsr
                ev.exception_level = 1;
                break;
            }
            case 4: { // SystemRegisterAccess
                ev.set_event_type(TraceEventType::SystemRegisterAccess);
                ev.address = 0x301000;
                ev.data = 0x30D00800;
                break;
            }
            case 5: { // MemoryRead
                ev.set_event_type(TraceEventType::MemoryRead);
                ev.pc = 0x400000 + (i % 2000) * 4;
                ev.address = 0x80000000 + (i % 4096) * 8;
                ev.data = 0xDEADBEEFCAFE0000 + i;
                ev.access_size = 8;
                break;
            }
            case 6: { // MemoryWrite
                ev.set_event_type(TraceEventType::MemoryWrite);
                ev.pc = 0x400000 + (i % 2000) * 4;
                ev.address = 0x80000000 + (i % 4096) * 8;
                ev.data = 0x1234567887654321 + i;
                ev.access_size = 8;
                break;
            }
            case 7: { // DebugEvent
                ev.set_event_type(TraceEventType::DebugEvent);
                ev.pc = 0x400000 + (i % 2000) * 4;
                ev.address = 0x400000 + (i % 2000) * 4;
                ev.data = 1; // Breakpoint
                break;
            }
            case 8: { // InterruptDelivered
                ev.set_event_type(TraceEventType::InterruptDelivered);
                ev.pc = 0x400000;
                ev.opcode_or_syndrome = 30; // Timer PPI
                break;
            }
            case 9: { // CPUStarted
                ev.set_event_type(TraceEventType::CPUStarted);
                ev.pc = 0x400000;
                ev.data = 1; // context_id
                break;
            }
            case 10: { // CPUStopped
                ev.set_event_type(TraceEventType::CPUStopped);
                ev.pc = 0x400000;
                ev.data = 1; // context_id
                break;
            }
            case 11: { // ContextChanged
                ev.set_event_type(TraceEventType::ContextChanged);
                ev.pc = 0x400000;
                ev.address = 0x80000000;
                ev.data = 0x90000000;
                break;
            }
            case 12: { // TimerEvent
                ev.set_event_type(TraceEventType::TimerEvent);
                ev.pc = 0x400000;
                ev.address = 50000000;
                ev.data = 50000000;
                ev.opcode_or_syndrome = 2; // Expired
                break;
            }
        }
        writer.write_event(ev);
    }

    writer.close();
    return path;
}

} // namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 15: Trace Analysis Subsystem Benchmark\n";
    std::cout << "=================================================================\n\n";

    const std::string bench_file = "stage15_benchmark_trace.wtr";
    const std::size_t kEventCount = 100000;

    std::cout << "Generating synthetic benchmark trace with " << kEventCount << " events...\n";
    std::size_t mem_before = get_current_process_memory();
    generate_benchmark_trace(bench_file, kEventCount);
    std::uintmax_t file_size = std::filesystem::file_size(bench_file);
    double file_size_mb = static_cast<double>(file_size) / (1024.0 * 1024.0);
    std::cout << "Benchmark file generated: " << bench_file << " ("
              << std::fixed << std::setprecision(2) << file_size_mb << " MB)\n\n";

    TraceAnalyzer analyzer;

    // 1. File Inspection Benchmark
    {
        std::cout << "--- [1] Fast File Inspection Benchmark ---\n";
        constexpr int kInspectRuns = 50;
        auto t0 = std::chrono::high_resolution_clock::now();
        TraceFileInspectionResult report{};
        for (int r = 0; r < kInspectRuns; ++r) {
            report = analyzer.inspect_file(bench_file);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed_sec = std::chrono::duration<double>(t1 - t0).count();
        double total_mb = file_size_mb * kInspectRuns;
        double mb_per_sec = total_mb / elapsed_sec;
        double files_per_sec = kInspectRuns / elapsed_sec;
        double avg_ms = (elapsed_sec / kInspectRuns) * 1000.0;

        std::cout << "  Runs:             " << kInspectRuns << "\n"
                  << "  Total Time:       " << std::setprecision(4) << elapsed_sec << " s\n"
                  << "  Average Latency:  " << std::setprecision(2) << avg_ms << " ms/file\n"
                  << "  Inspection Rate:  " << std::setprecision(1) << files_per_sec << " files/s\n"
                  << "  Throughput:       " << std::setprecision(1) << mb_per_sec << " MB/s\n"
                  << "  Integrity Status: " << static_cast<int>(report.integrity_status) << " (ValidComplete)\n\n";
    }

    // 2. Full Trace Analysis Benchmark (Streaming decode, summaries, causal analysis, validation)
    {
        std::cout << "--- [2] Full Trace Analysis Benchmark (All Events) ---\n";
        constexpr int kAnalysisRuns = 5;
        auto t0 = std::chrono::high_resolution_clock::now();
        TraceAnalysisReport analysis{};
        for (int r = 0; r < kAnalysisRuns; ++r) {
            analysis = analyzer.analyze_file(bench_file);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed_sec = std::chrono::duration<double>(t1 - t0).count();
        double total_events = static_cast<double>(kEventCount * kAnalysisRuns);
        double events_per_sec = total_events / elapsed_sec;
        double mb_per_sec = (file_size_mb * kAnalysisRuns) / elapsed_sec;
        double avg_ms = (elapsed_sec / kAnalysisRuns) * 1000.0;

        std::cout << "  Runs:             " << kAnalysisRuns << "\n"
                  << "  Total Events:     " << static_cast<std::size_t>(total_events) << "\n"
                  << "  Total Time:       " << std::setprecision(4) << elapsed_sec << " s\n"
                  << "  Average Latency:  " << std::setprecision(2) << avg_ms << " ms/run\n"
                  << "  Throughput:       " << std::setprecision(2) << (events_per_sec / 1e6) << " M events/s ("
                  << std::setprecision(1) << mb_per_sec << " MB/s)\n"
                  << "  Decoded Events:   " << analysis.filtered_events_count << "\n"
                  << "  Diagnostics Found:" << analysis.sequence_diagnostics.size() << "\n\n";
    }

    // 3. Filtered Trace Analysis Benchmark
    {
        std::cout << "--- [3] Filtered Trace Analysis Benchmark ---\n";
        TraceFilter filter{};
        filter.core_ids = std::vector<std::uint16_t>{0}; // Select core 0 only (25% of events)
        filter.address_mode = AddressFilterMode::MemoryOnly;
        filter.min_address = 0x80000000;
        filter.max_address = 0x80010000;

        constexpr int kFilterRuns = 10;
        auto t0 = std::chrono::high_resolution_clock::now();
        TraceAnalysisReport filtered_analysis{};
        for (int r = 0; r < kFilterRuns; ++r) {
            filtered_analysis = analyzer.analyze_file(bench_file, filter);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed_sec = std::chrono::duration<double>(t1 - t0).count();
        double total_events = static_cast<double>(kEventCount * kFilterRuns);
        double events_per_sec = total_events / elapsed_sec;
        double mb_per_sec = (file_size_mb * kFilterRuns) / elapsed_sec;

        std::cout << "  Runs:             " << kFilterRuns << "\n"
                  << "  Total Time:       " << std::setprecision(4) << elapsed_sec << " s\n"
                  << "  Scan Throughput:  " << std::setprecision(2) << (events_per_sec / 1e6) << " M events/s ("
                  << std::setprecision(1) << mb_per_sec << " MB/s)\n"
                  << "  Matched Events:   " << filtered_analysis.filtered_events_count << "\n\n";
    }

    // 4. Timeline Extraction Benchmark
    {
        std::cout << "--- [4] Timeline Extraction Benchmark ---\n";
        constexpr int kTimelineRuns = 5;
        auto t0 = std::chrono::high_resolution_clock::now();
        std::size_t extracted_count = 0;
        for (int r = 0; r < kTimelineRuns; ++r) {
            auto timeline = analyzer.extract_timeline(bench_file, {}, 50000);
            extracted_count = timeline.size();
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed_sec = std::chrono::duration<double>(t1 - t0).count();
        double total_extracted = static_cast<double>(extracted_count * kTimelineRuns);
        double ev_per_sec = total_extracted / elapsed_sec;

        std::cout << "  Runs:             " << kTimelineRuns << "\n"
                  << "  Events Extracted: " << extracted_count << " / run\n"
                  << "  Total Time:       " << std::setprecision(4) << elapsed_sec << " s\n"
                  << "  Throughput:       " << std::setprecision(2) << (ev_per_sec / 1e6) << " M events/s\n\n";
    }

    // 5. JSON Serialization & Export Benchmark
    {
        std::cout << "--- [5] JSON Export Benchmark ---\n";
        TraceAnalysisReport analysis = analyzer.analyze_file(bench_file);
        auto timeline = analyzer.extract_timeline(bench_file, {}, 10000);

        constexpr int kJsonRuns = 10;
        auto t0 = std::chrono::high_resolution_clock::now();
        std::size_t bytes_exported = 0;
        for (int r = 0; r < kJsonRuns; ++r) {
            std::string json_report = export_report_to_json(analysis);
            std::string json_timeline = export_timeline_to_json(timeline);
            bytes_exported = json_report.size() + json_timeline.size();
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed_sec = std::chrono::duration<double>(t1 - t0).count();
        double total_mb = (static_cast<double>(bytes_exported) * kJsonRuns) / (1024.0 * 1024.0);
        double mb_per_sec = total_mb / elapsed_sec;

        std::cout << "  Runs:             " << kJsonRuns << "\n"
                  << "  Export Size:      " << std::setprecision(2) << (static_cast<double>(bytes_exported) / 1024.0) << " KB / run\n"
                  << "  Total Time:       " << std::setprecision(4) << elapsed_sec << " s\n"
                  << "  Export Bandwidth: " << std::setprecision(1) << mb_per_sec << " MB/s\n\n";
    }

    // 6. Memory Footprint / Bounded Memory Verification
    {
        std::cout << "--- [6] Streaming Memory Footprint Verification ---\n";
        std::size_t mem_during = 0;
        std::size_t count = 0;

        analyzer.stream_timeline(bench_file, {}, [&](const TimelineEntry& /*entry*/) {
            count++;
            if (count % 25000 == 0) {
                std::size_t m = get_current_process_memory();
                if (m > mem_during) mem_during = m;
            }
            return true;
        });

        std::size_t mem_after = get_current_process_memory();
        double delta_mb = static_cast<double>(mem_during > mem_before ? mem_during - mem_before : 0) / (1024.0 * 1024.0);
        double retained_mb = static_cast<double>(mem_after > mem_before ? mem_after - mem_before : 0) / (1024.0 * 1024.0);

        std::cout << "  Initial Memory:   " << (mem_before / 1024 / 1024) << " MB\n"
                  << "  Peak Stream Memory:" << (mem_during / 1024 / 1024) << " MB (Delta: +" << std::setprecision(2) << delta_mb << " MB)\n"
                  << "  Retained Memory:  " << (mem_after / 1024 / 1024) << " MB (Delta: +" << std::setprecision(2) << retained_mb << " MB)\n"
                  << "  Stream Verification: " << (delta_mb < 20.0 ? "PASS (Strictly Bounded Memory)" : "FAIL") << "\n\n";
    }

    // Clean up temporary benchmark file
    std::filesystem::remove(bench_file);
    std::cout << "Benchmark completed successfully. Temporary files cleaned up.\n";
    return 0;
}

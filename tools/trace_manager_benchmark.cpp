#include "trace/trace_manager.h"
#include "trace/trace_event.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace winios::trace;

namespace {

TraceEvent make_test_event(std::uint64_t seq, TraceEventType type = TraceEventType::InstructionRetired, std::uint16_t core = 0) {
    TraceEvent ev{};
    ev.sequence_number = seq;
    ev.virtual_time = seq * 10;
    ev.pc = 0x4000 + (seq & 0xFF);
    ev.address = 0x8000 + seq * 8;
    ev.data = 0xDEADBEEF00000000ULL | seq;
    ev.opcode_or_syndrome = 0xD503201F;
    ev.flags = static_cast<std::uint32_t>(TraceEventFlags::HasVirtualAddress | TraceEventFlags::HasData);
    ev.event_type = static_cast<std::uint16_t>(type);
    ev.core_id = core;
    ev.exception_level = 1;
    ev.address_space = 1;
    ev.access_size = 4;
    ev.reserved = 0;
    return ev;
}

void bench_disabled_submission(std::uint64_t count) {
    TraceManager mgr;
    // Unconfigured / disabled
    const auto t0 = std::chrono::high_resolution_clock::now();
    for (std::uint64_t i = 1; i <= count; ++i) {
        mgr.submit(make_test_event(i));
    }
    const auto t1 = std::chrono::high_resolution_clock::now();
    const double sec = std::chrono::duration<double>(t1 - t0).count();
    const double m_events = (static_cast<double>(count) / 1e6) / sec;

    std::cout << "  [1] Tracing Disabled / Rejected (" << count << " events): "
              << std::fixed << std::setprecision(3) << sec << "s | "
              << std::setprecision(2) << m_events << " M events/s | "
              << std::setprecision(1) << (sec * 1e9 / static_cast<double>(count)) << " ns/event\n";
}

void bench_filtered_submission(std::uint64_t count) {
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    // Only accept InstructionRetired; we submit BranchTaken so all are filtered
    cfg.event_types.reset();
    cfg.event_types.set(TraceEventType::InstructionRetired, true);

    mgr.configure(cfg);
    mgr.start();

    const auto t0 = std::chrono::high_resolution_clock::now();
    for (std::uint64_t i = 1; i <= count; ++i) {
        mgr.submit(make_test_event(i, TraceEventType::BranchTaken));
    }
    const auto t1 = std::chrono::high_resolution_clock::now();
    mgr.stop();

    const double sec = std::chrono::duration<double>(t1 - t0).count();
    const double m_events = (static_cast<double>(count) / 1e6) / sec;

    std::cout << "  [2] Filtered Submission (" << count << " events): "
              << std::fixed << std::setprecision(3) << sec << "s | "
              << std::setprecision(2) << m_events << " M events/s | "
              << std::setprecision(1) << (sec * 1e9 / static_cast<double>(count)) << " ns/event\n";
}

void bench_in_memory_submission(std::uint64_t count) {
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 1048576; // 1M events buffer
    cfg.file_output_enabled = false;

    mgr.configure(cfg);
    mgr.start();

    const auto t0 = std::chrono::high_resolution_clock::now();
    for (std::uint64_t i = 1; i <= count; ++i) {
        mgr.submit(make_test_event(i));
    }
    const auto t1 = std::chrono::high_resolution_clock::now();
    mgr.stop();

    const double sec = std::chrono::duration<double>(t1 - t0).count();
    const double m_events = (static_cast<double>(count) / 1e6) / sec;
    const double mib_per_sec = (static_cast<double>(count) * 64.0 / (1024.0 * 1024.0)) / sec;

    std::cout << "  [3] In-Memory Ring Buffer Only (" << count << " events): "
              << std::fixed << std::setprecision(3) << sec << "s | "
              << std::setprecision(2) << m_events << " M events/s | "
              << std::setprecision(2) << mib_per_sec << " MiB/s | "
              << std::setprecision(1) << (sec * 1e9 / static_cast<double>(count)) << " ns/event\n";
}

void bench_pipeline_streaming(std::uint64_t count, std::size_t batch_size) {
    const auto file_path = std::filesystem::temp_directory_path() / ("winios_mgr_bench_" + std::to_string(batch_size) + ".wtr");
    std::filesystem::remove(file_path);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.file_output_enabled = true;
    cfg.output_path = file_path;
    cfg.ring_capacity = 262144;
    cfg.writer_batch_size = batch_size;

    mgr.configure(cfg);
    mgr.start();

    const auto t0 = std::chrono::high_resolution_clock::now();
    for (std::uint64_t i = 1; i <= count; ++i) {
        mgr.submit(make_test_event(i));
    }
    mgr.stop();
    const auto t1 = std::chrono::high_resolution_clock::now();

    const double sec = std::chrono::duration<double>(t1 - t0).count();
    const double m_events = (static_cast<double>(count) / 1e6) / sec;
    const double mib_per_sec = (static_cast<double>(count) * 64.0 / (1024.0 * 1024.0)) / sec;
    const auto stats = mgr.statistics();

    std::cout << "  [4] Full Pipeline (Batch " << std::setw(4) << batch_size << ", " << count << " events): "
              << std::fixed << std::setprecision(3) << sec << "s | "
              << std::setprecision(2) << m_events << " M events/s | "
              << std::setprecision(2) << mib_per_sec << " MiB/s | Persisted: "
              << stats.events_persisted << " | Dropped: " << stats.events_dropped << "\n";

    std::error_code ec;
    std::filesystem::remove(file_path, ec);
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 5: TraceManager Performance Benchmark\n";
    std::cout << "=================================================================\n\n";

    bench_disabled_submission(1000000);
    bench_filtered_submission(1000000);
    bench_in_memory_submission(1000000);

    std::cout << "\nBatch Size Evaluation (Ring Buffer + File Consumer):\n";
    bench_pipeline_streaming(500000, 64);
    bench_pipeline_streaming(500000, 256);
    bench_pipeline_streaming(500000, 1024);
    bench_pipeline_streaming(500000, 4096);

    std::cout << "\nBenchmark complete.\n";
    return 0;
}

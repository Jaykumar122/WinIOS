#include "trace/trace_event.h"
#include "trace/trace_serializer.h"
#include "trace/trace_file_writer.h"
#include "trace/trace_file_reader.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace winios::trace;

namespace {

TraceEvent make_bench_event(std::uint64_t i) {
    TraceEvent ev{};
    ev.sequence_number = i;
    ev.virtual_time = i * 10;
    ev.pc = 0x400000 + (i & 0xFFF);
    ev.address = 0x80000000 + (i * 8);
    ev.data = 0x1122334455667788ULL;
    ev.opcode_or_syndrome = 0xD503201F;
    ev.flags = 0x5;
    ev.event_type = 1;
    ev.core_id = 0;
    ev.exception_level = 1;
    ev.access_size = 4;
    return ev;
}

void bench_serialization_only(std::size_t num_events) {
    std::vector<TraceEvent> events(num_events);
    for (std::size_t i = 0; i < num_events; ++i) {
        events[i] = make_bench_event(i + 1);
    }
    std::vector<std::uint8_t> buffer(64);

    auto start = std::chrono::high_resolution_clock::now();
    for (std::size_t i = 0; i < num_events; ++i) {
        serialize_trace_event(events[i], buffer.data(), buffer.size());
    }
    auto end = std::chrono::high_resolution_clock::now();

    double sec = std::chrono::duration<double>(end - start).count();
    double mops = (static_cast<double>(num_events) / sec) / 1e6;
    double mbytes = (static_cast<double>(num_events * 64) / sec) / (1024.0 * 1024.0);

    std::cout << "  Serialization-only (" << num_events << " events): "
              << std::fixed << std::setprecision(2) << sec << "s | "
              << mops << " M events/s | "
              << mbytes << " MiB/s\n";
}

void bench_file_writer(const std::filesystem::path& file_path, std::size_t num_events, std::size_t block_cap) {
    std::filesystem::remove(file_path);
    TraceFileWriter writer(block_cap);
    writer.open(file_path, true);

    TraceEvent ev = make_bench_event(1);

    auto start = std::chrono::high_resolution_clock::now();
    for (std::size_t i = 0; i < num_events; ++i) {
        ev.sequence_number = i + 1;
        writer.write_event(ev);
    }
    writer.close();
    auto end = std::chrono::high_resolution_clock::now();

    double sec = std::chrono::duration<double>(end - start).count();
    double mops = (static_cast<double>(num_events) / sec) / 1e6;
    double mbytes = (static_cast<double>(num_events * 64) / sec) / (1024.0 * 1024.0);
    std::uintmax_t sz = std::filesystem::file_size(file_path);

    std::cout << "  FileWriter (" << num_events << " events, block cap " << block_cap << "): "
              << std::fixed << std::setprecision(2) << sec << "s | "
              << mops << " M events/s | "
              << mbytes << " MiB/s | File Size: "
              << (sz / 1024.0 / 1024.0) << " MiB\n";
}

void bench_file_reader(const std::filesystem::path& file_path) {
    TraceFileReader reader;
    reader.open(file_path);

    TraceEvent ev;
    std::size_t count = 0;

    auto start = std::chrono::high_resolution_clock::now();
    while (reader.read_event(ev)) {
        ++count;
    }
    reader.close();
    auto end = std::chrono::high_resolution_clock::now();

    double sec = std::chrono::duration<double>(end - start).count();
    double mops = (static_cast<double>(count) / sec) / 1e6;
    double mbytes = (static_cast<double>(count * 64) / sec) / (1024.0 * 1024.0);

    std::cout << "  FileReader (" << count << " events read): "
              << std::fixed << std::setprecision(2) << sec << "s | "
              << mops << " M events/s | "
              << mbytes << " MiB/s\n";

    std::filesystem::remove(file_path);
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 4: Binary File Sink Performance Benchmark\n";
    std::cout << " Record Size: " << TRACE_EVENT_DISK_SIZE << " bytes | Block Header: "
              << TRACE_BLOCK_HEADER_SIZE << " bytes\n";
#if defined(_MSC_VER)
    std::cout << " Compiler: MSVC " << _MSC_VER << "\n";
#endif
#if defined(NDEBUG)
    std::cout << " Build Mode: Release (Optimized)\n";
#else
    std::cout << " Build Mode: Debug (Unoptimized)\n";
#endif
    std::cout << "=================================================================\n\n";

    const auto temp_file = std::filesystem::temp_directory_path() / "winios_trace_bench.wtr";
    constexpr std::size_t kCount = 1000000; // 1M events

    std::cout << "[1] CPU Serialization Throughput (RAM Only):\n";
    bench_serialization_only(kCount);

    std::cout << "\n[2] Stream Writer Throughput (Disk / OS Cache):\n";
    bench_file_writer(temp_file, kCount, 512);

    std::cout << "\n[3] Stream Reader Throughput (Disk / OS Cache):\n";
    bench_file_reader(temp_file);

    std::cout << "\nBenchmark complete.\n";
    return 0;
}

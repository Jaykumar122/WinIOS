#include "trace/trace_ring_buffer.h"
#include "trace/trace_event.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <thread>
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

void bench_single_thread_push_pop(std::size_t capacity, std::size_t num_events) {
    InMemoryRingBuffer rb(capacity);
    TraceEvent ev = make_bench_event(1);

    auto start = std::chrono::high_resolution_clock::now();
    for (std::size_t i = 0; i < num_events; ++i) {
        rb.try_push(ev);
        TraceEvent out;
        rb.try_pop(out);
    }
    auto end = std::chrono::high_resolution_clock::now();

    double sec = std::chrono::duration<double>(end - start).count();
    double mops = (static_cast<double>(num_events) / sec) / 1e6;
    double mbytes = (static_cast<double>(num_events * sizeof(TraceEvent)) / sec) / (1024.0 * 1024.0);

    std::cout << "  Single-thread push+pop (" << capacity << " cap, " << num_events << " events): "
              << std::fixed << std::setprecision(2) << sec << "s | "
              << mops << " M events/s | "
              << mbytes << " MiB/s\n";
}

void bench_concurrent_spsc(std::size_t capacity, std::size_t num_events) {
    InMemoryRingBuffer rb(capacity);
    std::atomic<bool> producer_done{false};

    auto start = std::chrono::high_resolution_clock::now();

    std::thread producer([&]() {
        TraceEvent ev = make_bench_event(1);
        for (std::size_t i = 0; i < num_events; ++i) {
            ev.sequence_number = i;
            while (!rb.try_push(ev)) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&]() {
        TraceEvent out;
        std::size_t received = 0;
        while (received < num_events) {
            if (rb.try_pop(out)) {
                ++received;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();
    auto end = std::chrono::high_resolution_clock::now();

    double sec = std::chrono::duration<double>(end - start).count();
    double mops = (static_cast<double>(num_events) / sec) / 1e6;
    double mbytes = (static_cast<double>(num_events * sizeof(TraceEvent)) / sec) / (1024.0 * 1024.0);

    std::cout << "  Concurrent SPSC throughput (" << capacity << " cap, " << num_events << " events): "
              << std::fixed << std::setprecision(2) << sec << "s | "
              << mops << " M events/s | "
              << mbytes << " MiB/s\n";
}

void bench_full_rejection(std::size_t capacity, std::size_t num_events) {
    InMemoryRingBuffer rb(capacity);
    TraceEvent ev = make_bench_event(1);
    // Fill buffer completely
    while (rb.try_push(ev));

    auto start = std::chrono::high_resolution_clock::now();
    for (std::size_t i = 0; i < num_events; ++i) {
        rb.try_push(ev);
    }
    auto end = std::chrono::high_resolution_clock::now();

    double sec = std::chrono::duration<double>(end - start).count();
    double mops = (static_cast<double>(num_events) / sec) / 1e6;

    std::cout << "  Full-buffer rejection rate (" << capacity << " cap, " << num_events << " attempts): "
              << std::fixed << std::setprecision(2) << sec << "s | "
              << mops << " M rejections/s\n";
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 3: InMemoryRingBuffer Performance Benchmark\n";
    std::cout << " TraceEvent Size: " << sizeof(TraceEvent) << " bytes | Alignment: " << alignof(TraceEvent) << " bytes\n";
#if defined(_MSC_VER)
    std::cout << " Compiler: MSVC " << _MSC_VER << "\n";
#endif
#if defined(NDEBUG)
    std::cout << " Build Mode: Release (Optimized)\n";
#else
    std::cout << " Build Mode: Debug (Unoptimized)\n";
#endif
    std::cout << "=================================================================\n\n";

    constexpr std::size_t kSmallCount = 1000000;
    constexpr std::size_t kLargeCount = 5000000;

    std::cout << "[1] Single-Threaded Latency & Throughput:\n";
    bench_single_thread_push_pop(1024, kSmallCount);
    bench_single_thread_push_pop(65536, kLargeCount);
    bench_single_thread_push_pop(262144, kLargeCount);

    std::cout << "\n[2] Concurrent SPSC Multi-Threaded Throughput:\n";
    bench_concurrent_spsc(4096, kSmallCount);
    bench_concurrent_spsc(65536, kLargeCount);
    bench_concurrent_spsc(262144, kLargeCount);

    std::cout << "\n[3] Full-Buffer Fast Rejection Rate:\n";
    bench_full_rejection(1024, kLargeCount);
    bench_full_rejection(65536, kLargeCount);

    std::cout << "\nBenchmark complete.\n";
    return 0;
}

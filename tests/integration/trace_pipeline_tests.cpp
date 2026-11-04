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

const std::filesystem::path kTestDir = std::filesystem::temp_directory_path() / "winios_trace_pipeline_tests";

void setup_test_dir() {
    std::error_code ec;
    std::filesystem::create_directories(kTestDir, ec);
}

void cleanup_test_dir() {
    std::error_code ec;
    std::filesystem::remove_all(kTestDir, ec);
}

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

void test_1_file_pipeline_end_to_end() {
    std::cout << "  Test 1: File pipeline end-to-end (1,000 events)... ";
    const auto file_path = kTestDir / "pipeline_1000.wtr";
    std::filesystem::remove(file_path);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.file_output_enabled = true;
    cfg.output_path = file_path;
    cfg.ring_capacity = 2048;
    cfg.writer_batch_size = 256;

    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    constexpr std::uint64_t kCount = 1000;
    for (std::uint64_t i = 1; i <= kCount; ++i) {
        TEST_ASSERT(mgr.submit(make_test_event(i)));
    }

    TEST_ASSERT(mgr.stop());
    TEST_ASSERT(mgr.state() == TraceManagerState::Stopped);

    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_submitted == kCount);
    TEST_ASSERT(stats.events_accepted == kCount);
    TEST_ASSERT(stats.events_persisted == kCount);
    TEST_ASSERT(stats.events_dropped == 0);
    TEST_ASSERT(stats.writer_failures == 0);

    // Read back through Stage 4 TraceFileReader
    TraceFileReader reader;
    TEST_ASSERT(reader.open(file_path));
    TEST_ASSERT(reader.is_cleanly_finalized());
    TEST_ASSERT(reader.footer().total_events == kCount);

    std::vector<TraceEvent> loaded;
    loaded.reserve(kCount);
    TEST_ASSERT(reader.read_all_events(loaded) == kCount);
    TEST_ASSERT(loaded.size() == kCount);

    for (std::uint64_t i = 0; i < kCount; ++i) {
        TEST_ASSERT(loaded[i].sequence_number == i + 1);
        TEST_ASSERT(loaded[i] == make_test_event(i + 1));
    }

    reader.close();
    std::cout << "PASS (1,000 events verified)\n";
}

void test_2_empty_file_pipeline() {
    std::cout << "  Test 2: Empty file pipeline finalization... ";
    const auto file_path = kTestDir / "pipeline_empty.wtr";
    std::filesystem::remove(file_path);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.file_output_enabled = true;
    cfg.output_path = file_path;

    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());
    TEST_ASSERT(mgr.stop());

    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_submitted == 0);
    TEST_ASSERT(stats.events_persisted == 0);

    // Read back
    TraceFileReader reader;
    TEST_ASSERT(reader.open(file_path));
    TEST_ASSERT(reader.is_cleanly_finalized());
    TEST_ASSERT(reader.footer().total_events == 0);
    TEST_ASSERT(reader.footer().total_blocks == 0);
    reader.close();
    std::cout << "PASS\n";
}

void test_3_concurrent_producer_consumer_stress() {
    std::cout << "  Test 3: Concurrent producer-consumer stress (5,000 events)... ";
    const auto file_path = kTestDir / "pipeline_stress.wtr";
    std::filesystem::remove(file_path);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.file_output_enabled = true;
    cfg.output_path = file_path;
    // Bounded ring buffer to exercise concurrent wraparound and consumer wakeup
    cfg.ring_capacity = 512;
    cfg.writer_batch_size = 128;

    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    constexpr std::uint64_t kCount = 5000;
    std::uint64_t submitted_count = 0;
    for (std::uint64_t i = 1; i <= kCount; ++i) {
        if (mgr.submit(make_test_event(i))) {
            ++submitted_count;
        }
    }

    TEST_ASSERT(mgr.stop());
    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_submitted == kCount);
    TEST_ASSERT(stats.events_accepted == submitted_count);
    TEST_ASSERT(stats.events_persisted == submitted_count);
    TEST_ASSERT(stats.writer_failures == 0);

    // Validate generated stream
    TraceFileReader reader;
    TEST_ASSERT(reader.open(file_path));
    TEST_ASSERT(reader.is_cleanly_finalized());
    TEST_ASSERT(reader.footer().total_events == submitted_count);

    std::vector<TraceEvent> loaded;
    loaded.reserve(submitted_count);
    TEST_ASSERT(reader.read_all_events(loaded) == submitted_count);

    // Verify monotonic ordering (FIFO)
    for (std::size_t i = 1; i < loaded.size(); ++i) {
        TEST_ASSERT(loaded[i].sequence_number > loaded[i - 1].sequence_number);
    }

    reader.close();
    std::cout << "PASS (" << submitted_count << " events verified in FIFO order)\n";
}

void test_4_stop_with_pending_events() {
    std::cout << "  Test 4: Stop with pending events cleanly drained... ";
    const auto file_path = kTestDir / "pipeline_pending.wtr";
    std::filesystem::remove(file_path);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.file_output_enabled = true;
    cfg.output_path = file_path;
    cfg.ring_capacity = 1024;
    cfg.writer_batch_size = 512;

    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    // Submit 200 events and immediately call stop() without manual flush
    for (std::uint64_t i = 1; i <= 200; ++i) {
        TEST_ASSERT(mgr.submit(make_test_event(i)));
    }

    TEST_ASSERT(mgr.stop());
    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_persisted == 200);

    TraceFileReader reader;
    TEST_ASSERT(reader.open(file_path));
    TEST_ASSERT(reader.is_cleanly_finalized());
    TEST_ASSERT(reader.footer().total_events == 200);
    reader.close();
    std::cout << "PASS\n";
}

void test_5_explicit_flush() {
    std::cout << "  Test 5: Explicit flush... ";
    const auto file_path = kTestDir / "pipeline_flush.wtr";
    std::filesystem::remove(file_path);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.file_output_enabled = true;
    cfg.output_path = file_path;
    cfg.ring_capacity = 1024;

    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    for (std::uint64_t i = 1; i <= 50; ++i) {
        TEST_ASSERT(mgr.submit(make_test_event(i)));
    }

    TEST_ASSERT(mgr.flush());
    // After flush, events should be persisted
    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_persisted >= 50);

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

void test_6_repeated_start_stop_cycles() {
    std::cout << "  Test 6: Repeated start-stop cycles... ";
    const auto file_path = kTestDir / "pipeline_cycles.wtr";

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.file_output_enabled = true;
    cfg.output_path = file_path;
    cfg.ring_capacity = 256;
    cfg.writer_batch_size = 128;

    TEST_ASSERT(mgr.configure(cfg));

    for (int cycle = 0; cycle < 3; ++cycle) {
        TEST_ASSERT(mgr.start());
        TEST_ASSERT(mgr.submit(make_test_event(1)));
        TEST_ASSERT(mgr.stop());
    }

    std::cout << "PASS\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Phase 52 Stage 5: Trace Pipeline Integration Tests ===\n\n";

    setup_test_dir();
    try {
        test_1_file_pipeline_end_to_end();
        test_2_empty_file_pipeline();
        test_3_concurrent_producer_consumer_stress();
        test_4_stop_with_pending_events();
        test_5_explicit_flush();
        test_6_repeated_start_stop_cycles();

        cleanup_test_dir();
        std::cout << "\n=== All 6 Trace Pipeline Tests PASSED Successfully! ===\n";
        return 0;
    } catch (const std::exception& e) {
        cleanup_test_dir();
        std::cerr << "\n=== Test FAILED with exception: " << e.what() << " ===\n";
        return 1;
    }
}

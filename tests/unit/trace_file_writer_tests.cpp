#include "trace/trace_file_writer.h"
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

TraceEvent make_test_event(std::uint64_t seq) {
    TraceEvent ev{};
    ev.sequence_number = seq;
    ev.virtual_time = seq * 10;
    ev.pc = 0x4000 + (seq & 0xFF);
    ev.address = 0x8000 + seq * 8;
    ev.data = 0xDEADBEEF00000000ULL | seq;
    ev.opcode_or_syndrome = 0xD503201F;
    ev.flags = static_cast<std::uint32_t>(TraceEventFlags::HasVirtualAddress | TraceEventFlags::HasData);
    ev.event_type = static_cast<std::uint16_t>(TraceEventType::InstructionRetired);
    ev.core_id = 0;
    ev.exception_level = 1;
    ev.address_space = 1;
    ev.access_size = 4;
    ev.reserved = 0;
    return ev;
}

const std::filesystem::path kTestDir = std::filesystem::temp_directory_path() / "winios_trace_writer_tests";

void setup_test_dir() {
    std::error_code ec;
    std::filesystem::create_directories(kTestDir, ec);
}

void cleanup_test_dir() {
    std::error_code ec;
    std::filesystem::remove_all(kTestDir, ec);
}

void test_1_create_new_trace_file() {
    std::cout << "  Test 1: Create a new trace file... ";
    const auto file_path = kTestDir / "test1.wtr";
    std::filesystem::remove(file_path);

    TraceFileWriter writer;
    TEST_ASSERT(writer.open(file_path));
    TEST_ASSERT(writer.is_open());
    TEST_ASSERT(writer.events_written() == 0);
    TEST_ASSERT(writer.blocks_written() == 0);
    TEST_ASSERT(writer.close());
    TEST_ASSERT(!writer.is_open());

    // File should exist and have size 128 (header 64 + footer 64)
    TEST_ASSERT(std::filesystem::exists(file_path));
    TEST_ASSERT(std::filesystem::file_size(file_path) == 128);
    std::cout << "PASS (128 bytes)\n";
}

void test_2_write_one_event() {
    std::cout << "  Test 2: Write one event and finalize... ";
    const auto file_path = kTestDir / "test2.wtr";
    std::filesystem::remove(file_path);

    TraceFileWriter writer(64);
    TEST_ASSERT(writer.open(file_path));
    TEST_ASSERT(writer.write_event(make_test_event(1)));
    TEST_ASSERT(writer.close());

    // Header (64) + 1 Block (32 hdr + 64 event) + Footer (64) = 224 bytes
    TEST_ASSERT(std::filesystem::file_size(file_path) == 224);
    TEST_ASSERT(writer.events_written() == 1);
    TEST_ASSERT(writer.blocks_written() == 1);
    std::cout << "PASS (224 bytes)\n";
}

void test_3_write_multiple_events() {
    std::cout << "  Test 3: Write multiple events across blocks... ";
    const auto file_path = kTestDir / "test3.wtr";
    std::filesystem::remove(file_path);

    // Block capacity 10 events
    TraceFileWriter writer(10);
    TEST_ASSERT(writer.open(file_path));
    for (std::uint64_t i = 1; i <= 25; ++i) {
        TEST_ASSERT(writer.write_event(make_test_event(i)));
    }
    TEST_ASSERT(writer.close());

    // 25 events with cap 10 = 3 blocks (10, 10, 5)
    TEST_ASSERT(writer.events_written() == 25);
    TEST_ASSERT(writer.blocks_written() == 3);
    // Size: 64 (hdr) + 3 * 32 (blk hdrs) + 25 * 64 (events) + 64 (ftr) = 64 + 96 + 1600 + 64 = 1824 bytes
    TEST_ASSERT(std::filesystem::file_size(file_path) == 1824);
    std::cout << "PASS (3 blocks, 1824 bytes)\n";
}

void test_4_write_large_batch() {
    std::cout << "  Test 4: Write large batch (1,000 events)... ";
    const auto file_path = kTestDir / "test4.wtr";
    std::filesystem::remove(file_path);

    std::vector<TraceEvent> batch;
    batch.reserve(1000);
    for (std::uint64_t i = 1; i <= 1000; ++i) {
        batch.push_back(make_test_event(i));
    }

    TraceFileWriter writer(256);
    TEST_ASSERT(writer.open(file_path));
    TEST_ASSERT(writer.write_events(batch));
    TEST_ASSERT(writer.close());

    TEST_ASSERT(writer.events_written() == 1000);
    // 1000 with cap 256 = 4 blocks (256, 256, 256, 232)
    TEST_ASSERT(writer.blocks_written() == 4);
    std::cout << "PASS (1,000 events written)\n";
}

void test_5_flush() {
    std::cout << "  Test 5: Explicit flush... ";
    const auto file_path = kTestDir / "test5.wtr";
    std::filesystem::remove(file_path);

    TraceFileWriter writer(100);
    TEST_ASSERT(writer.open(file_path));
    for (int i = 0; i < 5; ++i) {
        writer.write_event(make_test_event(i + 1));
    }
    TEST_ASSERT(writer.flush());
    // After flush, block is written out
    TEST_ASSERT(writer.blocks_written() == 1);
    TEST_ASSERT(writer.events_written() == 5);
    TEST_ASSERT(writer.close());
    std::cout << "PASS\n";
}

void test_6_reject_write_after_close() {
    std::cout << "  Test 6: Reject write after close... ";
    const auto file_path = kTestDir / "test6.wtr";
    std::filesystem::remove(file_path);

    TraceFileWriter writer;
    TEST_ASSERT(writer.open(file_path));
    TEST_ASSERT(writer.close());

    TEST_ASSERT(!writer.write_event(make_test_event(1)));
    TEST_ASSERT(writer.last_error() == TraceFileError::NotOpen);
    std::cout << "PASS (rejected with NotOpen)\n";
}

void test_7_repeated_close() {
    std::cout << "  Test 7: Repeated close is safe... ";
    const auto file_path = kTestDir / "test7.wtr";
    std::filesystem::remove(file_path);

    TraceFileWriter writer;
    TEST_ASSERT(writer.open(file_path));
    TEST_ASSERT(writer.close());
    TEST_ASSERT(writer.close()); // 2nd close should safely return true
    TEST_ASSERT(!writer.is_open());
    std::cout << "PASS\n";
}

void test_8_file_already_exists_behavior() {
    std::cout << "  Test 8: File already exists rejection & overwrite... ";
    const auto file_path = kTestDir / "test8.wtr";
    std::filesystem::remove(file_path);

    TraceFileWriter writer;
    TEST_ASSERT(writer.open(file_path));
    TEST_ASSERT(writer.close());

    // Attempting open without overwrite must fail
    TraceFileWriter writer2;
    TEST_ASSERT(!writer2.open(file_path, false));
    TEST_ASSERT(writer2.last_error() == TraceFileError::FileAlreadyExists);

    // Attempting open with overwrite must succeed
    TEST_ASSERT(writer2.open(file_path, true));
    TEST_ASSERT(writer2.close());
    std::cout << "PASS\n";
}

void test_9_empty_trace_file() {
    std::cout << "  Test 9: Empty trace file finalization... ";
    const auto file_path = kTestDir / "test9.wtr";
    std::filesystem::remove(file_path);

    TraceFileWriter writer;
    TEST_ASSERT(writer.open(file_path));
    TEST_ASSERT(writer.close());

    TEST_ASSERT(writer.events_written() == 0);
    TEST_ASSERT(writer.blocks_written() == 0);
    TEST_ASSERT(std::filesystem::file_size(file_path) == 128); // Header 64 + Footer 64
    std::cout << "PASS\n";
}

void test_10_buffer_boundary_behavior() {
    std::cout << "  Test 10: Buffer boundary exact multiples... ";
    const auto file_path = kTestDir / "test10.wtr";
    std::filesystem::remove(file_path);

    // Exact block capacity: 16
    TraceFileWriter writer(16);
    TEST_ASSERT(writer.open(file_path));
    for (std::uint64_t i = 1; i <= 32; ++i) { // Exactly 2 full blocks
        writer.write_event(make_test_event(i));
    }
    TEST_ASSERT(writer.close());

    TEST_ASSERT(writer.events_written() == 32);
    TEST_ASSERT(writer.blocks_written() == 2);
    std::cout << "PASS (exactly 2 blocks)\n";
}

void test_11_invalid_path() {
    std::cout << "  Test 11: Invalid path rejection... ";
    TraceFileWriter writer;
    TEST_ASSERT(!writer.open(""));
    TEST_ASSERT(writer.last_error() == TraceFileError::InvalidPath);
    std::cout << "PASS\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Phase 52 Stage 4: Trace File Writer Tests ===\n\n";

    setup_test_dir();
    try {
        test_1_create_new_trace_file();
        test_2_write_one_event();
        test_3_write_multiple_events();
        test_4_write_large_batch();
        test_5_flush();
        test_6_reject_write_after_close();
        test_7_repeated_close();
        test_8_file_already_exists_behavior();
        test_9_empty_trace_file();
        test_10_buffer_boundary_behavior();
        test_11_invalid_path();

        cleanup_test_dir();
        std::cout << "\n=== All 11 Writer Tests PASSED Successfully! ===\n";
        return 0;
    } catch (const std::exception& e) {
        cleanup_test_dir();
        std::cerr << "\n=== Test FAILED with exception: " << e.what() << " ===\n";
        return 1;
    }
}

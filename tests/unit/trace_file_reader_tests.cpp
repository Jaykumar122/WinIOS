#include "trace/trace_file_writer.h"
#include "trace/trace_file_reader.h"
#include "trace/trace_event.h"

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
    ev.data = 0xABCD000000000000ULL | seq;
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

const std::filesystem::path kTestDir = std::filesystem::temp_directory_path() / "winios_trace_reader_tests";

void setup_test_dir() {
    std::error_code ec;
    std::filesystem::create_directories(kTestDir, ec);
}

void cleanup_test_dir() {
    std::error_code ec;
    std::filesystem::remove_all(kTestDir, ec);
}

void test_1_to_5_read_valid_header_blocks_events_footer() {
    std::cout << "  Tests 1-5: Read valid file, header, blocks, events, and footer... ";
    const auto file_path = kTestDir / "valid_stream.wtr";

    // Write file with 50 events in blocks of 10
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(file_path, true));
        for (std::uint64_t i = 1; i <= 50; ++i) {
            TEST_ASSERT(writer.write_event(make_test_event(i)));
        }
        TEST_ASSERT(writer.close());
    }

    // Read back
    TraceFileReader reader;
    TEST_ASSERT(reader.open(file_path));
    TEST_ASSERT(reader.is_open());
    TEST_ASSERT(reader.is_cleanly_finalized());

    // Test 1: Header verification
    TEST_ASSERT(reader.header().magic == TRACE_FILE_MAGIC);
    TEST_ASSERT(reader.header().file_format_version == 1);
    TEST_ASSERT(reader.header().schema_version == TRACE_SCHEMA_VERSION);

    // Test 2 & 4: Read all events in order
    for (std::uint64_t i = 1; i <= 50; ++i) {
        TraceEvent ev;
        TEST_ASSERT(reader.read_event(ev));
        TEST_ASSERT(ev.sequence_number == i);
        TEST_ASSERT(ev == make_test_event(i));
    }

    // EOF reached
    TraceEvent extra;
    TEST_ASSERT(!reader.read_event(extra));
    TEST_ASSERT(reader.is_eof());

    // Test 3 & 5: Footer verification
    TEST_ASSERT(reader.total_events_read() == 50);
    TEST_ASSERT(reader.total_blocks_read() == 5);
    TEST_ASSERT(reader.footer().total_events == 50);
    TEST_ASSERT(reader.footer().total_blocks == 5);
    TEST_ASSERT(reader.footer().finalization_status == TRACE_STATUS_CLEANLY_FINALIZED);

    reader.close();
    std::cout << "PASS\n";
}

void test_6_reject_invalid_magic() {
    std::cout << "  Test 6: Reject invalid file magic... ";
    const auto file_path = kTestDir / "corrupt_magic.wtr";

    {
        TraceFileWriter writer;
        TEST_ASSERT(writer.open(file_path, true));
        TEST_ASSERT(writer.close());
    }

    // Corrupt magic
    {
        std::fstream fs(file_path, std::ios::in | std::ios::out | std::ios::binary);
        char bad_magic[8] = {'B', 'A', 'D', 'M', 'A', 'G', 'I', 'C'};
        fs.seekp(0);
        fs.write(bad_magic, 8);
    }

    TraceFileReader reader;
    TEST_ASSERT(!reader.open(file_path));
    TEST_ASSERT(reader.last_error() == TraceFileError::InvalidMagic);
    std::cout << "PASS\n";
}

void test_7_reject_unsupported_version() {
    std::cout << "  Test 7: Reject unsupported version... ";
    const auto file_path = kTestDir / "bad_ver.wtr";

    {
        TraceFileWriter writer;
        TEST_ASSERT(writer.open(file_path, true));
        TEST_ASSERT(writer.close());
    }

    // Corrupt version at offset 8 to 99
    {
        std::fstream fs(file_path, std::ios::in | std::ios::out | std::ios::binary);
        std::uint8_t bad_ver[2] = {99, 0};
        fs.seekp(8);
        fs.write(reinterpret_cast<char*>(bad_ver), 2);
    }

    TraceFileReader reader;
    TEST_ASSERT(!reader.open(file_path));
    // Header version changed, so either UnsupportedVersion or ChecksumMismatch
    TEST_ASSERT(reader.last_error() == TraceFileError::UnsupportedVersion ||
                reader.last_error() == TraceFileError::ChecksumMismatch);
    std::cout << "PASS\n";
}

void test_8_reject_invalid_schema() {
    std::cout << "  Test 8: Reject invalid schema version... ";
    const auto file_path = kTestDir / "bad_schema.wtr";

    {
        TraceFileWriter writer;
        TEST_ASSERT(writer.open(file_path, true));
        TEST_ASSERT(writer.close());
    }

    {
        std::fstream fs(file_path, std::ios::in | std::ios::out | std::ios::binary);
        std::uint8_t bad_schema[4] = {0, 0, 2, 0}; // v2.0
        fs.seekp(12);
        fs.write(reinterpret_cast<char*>(bad_schema), 4);
    }

    TraceFileReader reader;
    TEST_ASSERT(!reader.open(file_path));
    TEST_ASSERT(reader.last_error() == TraceFileError::UnsupportedSchema ||
                reader.last_error() == TraceFileError::ChecksumMismatch);
    std::cout << "PASS\n";
}

void test_9_reject_invalid_record_size() {
    std::cout << "  Test 9: Reject invalid record size... ";
    const auto file_path = kTestDir / "bad_rec_size.wtr";

    {
        TraceFileWriter writer;
        TEST_ASSERT(writer.open(file_path, true));
        TEST_ASSERT(writer.close());
    }

    {
        std::fstream fs(file_path, std::ios::in | std::ios::out | std::ios::binary);
        std::uint8_t bad_size[2] = {128, 0}; // 128 instead of 64
        fs.seekp(16);
        fs.write(reinterpret_cast<char*>(bad_size), 2);
    }

    TraceFileReader reader;
    TEST_ASSERT(!reader.open(file_path));
    TEST_ASSERT(reader.last_error() == TraceFileError::InvalidRecordSize ||
                reader.last_error() == TraceFileError::ChecksumMismatch);
    std::cout << "PASS\n";
}

void test_10_detect_corrupted_header_checksum() {
    std::cout << "  Test 10: Detect corrupted header checksum... ";
    const auto file_path = kTestDir / "bad_hdr_crc.wtr";

    {
        TraceFileWriter writer;
        TEST_ASSERT(writer.open(file_path, true));
        TEST_ASSERT(writer.close());
    }

    // Invert a byte in creation timestamp (offset 25)
    {
        std::fstream fs(file_path, std::ios::in | std::ios::out | std::ios::binary);
        fs.seekp(25);
        char c;
        fs.read(&c, 1);
        c ^= 0xFF;
        fs.seekp(25);
        fs.write(&c, 1);
    }

    TraceFileReader reader;
    TEST_ASSERT(!reader.open(file_path));
    TEST_ASSERT(reader.last_error() == TraceFileError::ChecksumMismatch);
    std::cout << "PASS\n";
}

void test_11_detect_corrupted_payload_checksum() {
    std::cout << "  Test 11: Detect corrupted block payload checksum... ";
    const auto file_path = kTestDir / "bad_payload_crc.wtr";

    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(file_path, true));
        writer.write_event(make_test_event(1));
        TEST_ASSERT(writer.close());
    }

    // File layout:
    // 0..63: Header
    // 64..95: Block Header
    // 96..159: Payload (TraceEvent 1)
    // 160..223: Footer
    // Invert a byte in payload (e.g. offset 100)
    {
        std::fstream fs(file_path, std::ios::in | std::ios::out | std::ios::binary);
        fs.seekp(100);
        char c;
        fs.read(&c, 1);
        c ^= 0xFF;
        fs.seekp(100);
        fs.write(&c, 1);
    }

    TraceFileReader reader;
    TEST_ASSERT(reader.open(file_path)); // Header and Footer are valid
    TraceEvent ev;
    // Reading event must detect payload checksum mismatch
    TEST_ASSERT(!reader.read_event(ev));
    TEST_ASSERT(reader.last_error() == TraceFileError::ChecksumMismatch);
    std::cout << "PASS (detected ChecksumMismatch)\n";
}

void test_12_detect_truncated_header() {
    std::cout << "  Test 12: Detect truncated header... ";
    const auto file_path = kTestDir / "truncated_hdr.wtr";

    // Write file with only 32 bytes total
    {
        std::ofstream ofs(file_path, std::ios::binary);
        std::vector<char> dummy(32, 0);
        ofs.write(dummy.data(), dummy.size());
    }

    TraceFileReader reader;
    TEST_ASSERT(!reader.open(file_path));
    TEST_ASSERT(reader.last_error() == TraceFileError::TruncatedFile);
    std::cout << "PASS\n";
}

void test_13_detect_missing_footer() {
    std::cout << "  Test 13: Detect missing footer (unfinalized / crashed)... ";
    const auto file_path = kTestDir / "missing_footer.wtr";

    // Write file but truncate footer
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(file_path, true));
        TEST_ASSERT(writer.write_event(make_test_event(1)));
        TEST_ASSERT(writer.flush());
    }

    // Strip the last 64 bytes (the footer written by destructor if any)
    {
        std::uintmax_t sz = std::filesystem::file_size(file_path);
        // Truncate last 64 bytes
        std::filesystem::resize_file(file_path, sz - 64);
    }

    TraceFileReader reader;
    TEST_ASSERT(!reader.open(file_path));
    TEST_ASSERT(!reader.is_cleanly_finalized());
    TEST_ASSERT(reader.last_error() == TraceFileError::InvalidMagic ||
                reader.last_error() == TraceFileError::MissingFooter ||
                reader.last_error() == TraceFileError::TruncatedFile);
    std::cout << "PASS\n";
}

void test_14_end_to_end_roundtrip() {
    std::cout << "  Test 14: End-to-end roundtrip (5,000 events)... ";
    const auto file_path = kTestDir / "roundtrip_5000.wtr";
    constexpr std::uint64_t kTotal = 5000;

    // Write
    {
        TraceFileWriter writer(256);
        TEST_ASSERT(writer.open(file_path, true));
        for (std::uint64_t i = 1; i <= kTotal; ++i) {
            TEST_ASSERT(writer.write_event(make_test_event(i)));
        }
        TEST_ASSERT(writer.close());
    }

    // Read all
    TraceFileReader reader;
    TEST_ASSERT(reader.open(file_path));
    TEST_ASSERT(reader.is_cleanly_finalized());

    std::vector<TraceEvent> loaded;
    loaded.reserve(kTotal);
    TEST_ASSERT(reader.read_all_events(loaded) == kTotal);
    TEST_ASSERT(loaded.size() == kTotal);

    for (std::uint64_t i = 0; i < kTotal; ++i) {
        TEST_ASSERT(loaded[i].sequence_number == i + 1);
        TEST_ASSERT(loaded[i] == make_test_event(i + 1));
    }
    TEST_ASSERT(reader.is_eof());
    reader.close();
    std::cout << "PASS (5,000 events verified)\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Phase 52 Stage 4: Trace File Reader Tests ===\n\n";

    setup_test_dir();
    try {
        test_1_to_5_read_valid_header_blocks_events_footer();
        test_6_reject_invalid_magic();
        test_7_reject_unsupported_version();
        test_8_reject_invalid_schema();
        test_9_reject_invalid_record_size();
        test_10_detect_corrupted_header_checksum();
        test_11_detect_corrupted_payload_checksum();
        test_12_detect_truncated_header();
        test_13_detect_missing_footer();
        test_14_end_to_end_roundtrip();

        cleanup_test_dir();
        std::cout << "\n=== All Reader Tests PASSED Successfully! ===\n";
        return 0;
    } catch (const std::exception& e) {
        cleanup_test_dir();
        std::cerr << "\n=== Test FAILED with exception: " << e.what() << " ===\n";
        return 1;
    }
}

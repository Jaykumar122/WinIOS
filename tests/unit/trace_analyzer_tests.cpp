#include "trace/trace_analyzer.h"
#include "trace/trace_analysis_report.h"
#include "trace/trace_event.h"
#include "trace/trace_file_reader.h"
#include "trace/trace_file_writer.h"
#include "trace/trace_types.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
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

using namespace winios::trace;

namespace {

const std::filesystem::path kTestDir = std::filesystem::temp_directory_path() / "winios_analyzer_unit_tests";

void setup_test_dir() {
    std::error_code ec;
    std::filesystem::create_directories(kTestDir, ec);
}

void cleanup_test_dir() {
    std::error_code ec;
    std::filesystem::remove_all(kTestDir, ec);
}

TraceEvent make_event(
    TraceEventType type,
    std::uint64_t seq,
    std::uint16_t core_id = 0,
    std::uint64_t vtime = 0,
    std::uint64_t pc = 0x1000,
    std::uint64_t addr = 0,
    std::uint64_t data = 0,
    std::uint32_t op_syndrome = 0,
    std::uint32_t flags = 0,
    std::uint8_t el = 1) {

    TraceEvent ev{};
    ev.sequence_number = seq;
    ev.virtual_time = (vtime != 0 ? vtime : seq * 10);
    ev.pc = pc;
    ev.address = addr;
    ev.data = data;
    ev.opcode_or_syndrome = op_syndrome;
    ev.flags = flags;
    ev.event_type = static_cast<std::uint16_t>(type);
    ev.core_id = core_id;
    ev.exception_level = el;
    ev.address_space = 1;
    ev.access_size = 4;
    return ev;
}

// -----------------------------------------------------------------------------
// Group A: File Format Inspection & Integrity (Tests 1–13)
// -----------------------------------------------------------------------------

void test_1_valid_empty_trace() {
    const auto p = kTestDir / "empty.wtr";
    {
        TraceFileWriter writer;
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto res = analyzer.inspect_file(p);
    TEST_ASSERT(res.integrity_status == TraceFileIntegrityStatus::ValidNoEvents);
    TEST_ASSERT(res.header_valid);
    TEST_ASSERT(res.footer_valid);
    TEST_ASSERT(res.crc_valid);
    TEST_ASSERT(res.event_count == 0);
    TEST_ASSERT(res.block_count == 0);
}

void test_2_valid_single_block_trace() {
    const auto p = kTestDir / "single_block.wtr";
    {
        TraceFileWriter writer(100);
        TEST_ASSERT(writer.open(p, true));
        for (std::uint64_t i = 1; i <= 25; ++i) {
            TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, i)));
        }
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto res = analyzer.inspect_file(p);
    TEST_ASSERT(res.integrity_status == TraceFileIntegrityStatus::ValidComplete);
    TEST_ASSERT(res.event_count == 25);
    TEST_ASSERT(res.block_count == 1);
    TEST_ASSERT(res.header_valid);
    TEST_ASSERT(res.footer_valid);
    TEST_ASSERT(res.crc_valid);
    TEST_ASSERT(res.per_core_event_counts.at(0) == 25);
}

void test_3_valid_multi_block_trace() {
    const auto p = kTestDir / "multi_block.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        for (std::uint64_t i = 1; i <= 45; ++i) {
            TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, i)));
        }
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto res = analyzer.inspect_file(p);
    TEST_ASSERT(res.integrity_status == TraceFileIntegrityStatus::ValidComplete);
    TEST_ASSERT(res.event_count == 45);
    TEST_ASSERT(res.block_count == 5); // 4 full blocks of 10 + 1 partial block of 5
    TEST_ASSERT(res.footer_expected_events == 45);
    TEST_ASSERT(res.footer_expected_blocks == 5);
}

void test_4_valid_footer_verification() {
    const auto p = kTestDir / "footer_check.wtr";
    {
        TraceFileWriter writer(50);
        TEST_ASSERT(writer.open(p, true));
        for (std::uint64_t i = 1; i <= 50; ++i) {
            TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, i)));
        }
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto res = analyzer.inspect_file(p);
    TEST_ASSERT(res.footer_valid);
    TEST_ASSERT(res.footer_expected_events == 50);
    TEST_ASSERT(res.footer_expected_blocks == 1);
}

void test_5_missing_footer_unfinalized() {
    const auto p = kTestDir / "missing_footer.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        for (std::uint64_t i = 1; i <= 10; ++i) {
            TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, i)));
        }
        // Do not close cleanly: flush blocks then truncate footer
        writer.flush();
    }

    // Strip footer
    const auto sz = std::filesystem::file_size(p);
    std::filesystem::resize_file(p, sz - TRACE_FILE_FOOTER_SIZE);

    TraceAnalyzer analyzer;
    const auto res = analyzer.inspect_file(p);
    TEST_ASSERT(!res.footer_valid);
    TEST_ASSERT(res.integrity_status == TraceFileIntegrityStatus::InvalidFooter);
    TEST_ASSERT(res.event_count == 10);
}

void test_6_truncated_header() {
    const auto p = kTestDir / "trunc_hdr.wtr";
    {
        std::ofstream ofs(p, std::ios::binary);
        const char bad[30] = "too_short";
        ofs.write(bad, sizeof(bad));
    }

    TraceAnalyzer analyzer;
    const auto res = analyzer.inspect_file(p);
    TEST_ASSERT(res.integrity_status == TraceFileIntegrityStatus::TruncatedTrace);
    TEST_ASSERT(!res.header_valid);
}

void test_7_truncated_block() {
    const auto p = kTestDir / "trunc_blk.wtr";
    {
        TraceFileWriter writer(20);
        TEST_ASSERT(writer.open(p, true));
        for (std::uint64_t i = 1; i <= 15; ++i) {
            TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, i)));
        }
        TEST_ASSERT(writer.close());
    }

    // Truncate 30 bytes off payload (truncate footer + 30 bytes of payload)
    const auto sz = std::filesystem::file_size(p);
    std::filesystem::resize_file(p, sz - TRACE_FILE_FOOTER_SIZE - 30);

    TraceAnalyzer analyzer;
    const auto res = analyzer.inspect_file(p);
    TEST_ASSERT(res.integrity_status == TraceFileIntegrityStatus::TruncatedTrace ||
                res.integrity_status == TraceFileIntegrityStatus::InvalidBlockHeader);
}

void test_8_invalid_header_magic() {
    const auto p = kTestDir / "bad_magic.wtr";
    {
        TraceFileWriter writer;
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.close());
    }

    {
        std::fstream fs(p, std::ios::in | std::ios::out | std::ios::binary);
        const char bad_magic[8] = {'B', 'A', 'D', 'M', 'A', 'G', 'I', 'C'};
        fs.seekp(0);
        fs.write(bad_magic, 8);
    }

    TraceAnalyzer analyzer;
    const auto res = analyzer.inspect_file(p);
    TEST_ASSERT(res.integrity_status == TraceFileIntegrityStatus::InvalidHeader);
    TEST_ASSERT(!res.header_valid);
}

void test_9_unsupported_version() {
    const auto p = kTestDir / "bad_ver.wtr";
    {
        TraceFileWriter writer;
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.close());
    }

    {
        std::fstream fs(p, std::ios::in | std::ios::out | std::ios::binary);
        const std::uint8_t bad_ver[2] = {99, 0};
        fs.seekp(8);
        fs.write(reinterpret_cast<const char*>(bad_ver), 2);
    }

    TraceAnalyzer analyzer;
    const auto res = analyzer.inspect_file(p);
    TEST_ASSERT(res.integrity_status == TraceFileIntegrityStatus::UnsupportedVersion ||
                res.integrity_status == TraceFileIntegrityStatus::CrcMismatch);
}

void test_10_crc_mismatch() {
    const auto p = kTestDir / "bad_crc.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        for (std::uint64_t i = 1; i <= 10; ++i) {
            TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, i)));
        }
        TEST_ASSERT(writer.close());
    }

    // Corrupt one payload byte inside block 0
    {
        std::fstream fs(p, std::ios::in | std::ios::out | std::ios::binary);
        fs.seekp(100);
        const char bad = '\xEE';
        fs.write(&bad, 1);
    }

    TraceAnalyzer analyzer;
    const auto res = analyzer.inspect_file(p);
    TEST_ASSERT(res.integrity_status == TraceFileIntegrityStatus::CrcMismatch);
    TEST_ASSERT(!res.crc_valid);
}

void test_11_corrupted_footer_magic() {
    const auto p = kTestDir / "bad_ftr_magic.wtr";
    {
        TraceFileWriter writer;
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.close());
    }

    const auto sz = std::filesystem::file_size(p);
    {
        std::fstream fs(p, std::ios::in | std::ios::out | std::ios::binary);
        fs.seekp(sz - TRACE_FILE_FOOTER_SIZE);
        const char bad_magic[8] = {'B', 'A', 'D', 'F', 'O', 'O', 'T', 'R'};
        fs.write(bad_magic, 8);
    }

    TraceAnalyzer analyzer;
    const auto res = analyzer.inspect_file(p);
    TEST_ASSERT(!res.footer_valid);
    TEST_ASSERT(res.integrity_status == TraceFileIntegrityStatus::InvalidFooter ||
                res.integrity_status == TraceFileIntegrityStatus::CrcMismatch);
}

void test_12_unknown_event_type() {
    const auto p = kTestDir / "unknown_type.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        // Write normal event
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 1)));
        // Write event with unallocated type (0)
        auto bad_ev = make_event(TraceEventType::InstructionRetired, 2);
        bad_ev.event_type = 0;
        TEST_ASSERT(writer.write_event(bad_ev));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto res = analyzer.inspect_file(p);
    TEST_ASSERT(res.integrity_status == TraceFileIntegrityStatus::UnknownEventType);
    TEST_ASSERT(res.unknown_event_types_count == 1);
    TEST_ASSERT(res.event_count == 2);
}

void test_13_dropped_marker_detection() {
    const auto p = kTestDir / "dropped_marker.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 1)));
        // Write DroppedMarker event
        auto drop_ev = make_event(TraceEventType::DroppedMarker, 2);
        drop_ev.data = 120; // 120 dropped events
        TEST_ASSERT(writer.write_event(drop_ev));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 3)));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto res = analyzer.inspect_file(p);
    TEST_ASSERT(res.integrity_status == TraceFileIntegrityStatus::ContainsDroppedMarkers);
    TEST_ASSERT(res.dropped_markers_count == 1);
}

// -----------------------------------------------------------------------------
// Group B: Event Filtering (Tests 14–21)
// -----------------------------------------------------------------------------

void test_14_filter_by_event_type() {
    const auto p = kTestDir / "filter_type.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 1)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::BranchTaken, 2)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::MemoryRead, 3)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 4)));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    TraceFilter filter;
    filter.event_types = std::vector<TraceEventType>{TraceEventType::InstructionRetired};

    const auto rep = analyzer.analyze_file(p, filter);
    TEST_ASSERT(rep.filtered_events_count == 2);
    TEST_ASSERT(rep.instructions.total_retired == 2);
}

void test_15_filter_by_core_id() {
    const auto p = kTestDir / "filter_core.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 1, 0)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 2, 1)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 3, 0)));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    TraceFilter filter;
    filter.core_ids = std::vector<std::uint16_t>{1};

    const auto rep = analyzer.analyze_file(p, filter);
    TEST_ASSERT(rep.filtered_events_count == 1);
    TEST_ASSERT(rep.core_summaries.size() == 1);
    TEST_ASSERT(rep.core_summaries[0].core_id == 1);
}

void test_16_filter_by_exception_level() {
    const auto p = kTestDir / "filter_el.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 1, 0, 10, 0x1000, 0, 0, 0, 0, 0))); // EL0
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 2, 0, 20, 0x2000, 0, 0, 0, 0, 1))); // EL1
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 3, 0, 30, 0x3000, 0, 0, 0, 0, 2))); // EL2
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    TraceFilter filter;
    filter.min_el = std::uint8_t{1};
    filter.max_el = std::uint8_t{1};

    const auto rep = analyzer.analyze_file(p, filter);
    TEST_ASSERT(rep.filtered_events_count == 1);
    TEST_ASSERT(rep.sample_timeline[0].exception_level == 1);
}

void test_17_filter_by_address_mode() {
    const auto p = kTestDir / "filter_addr.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        // Branch target address 0x5000
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::BranchTaken, 1, 0, 10, 0x1000, 0x5000)));
        // Memory address 0x5000
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::MemoryRead, 2, 0, 20, 0x1004, 0x5000)));
        // Timer compare 0x5000
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::TimerEvent, 3, 0, 30, 0x1008, 0x5000)));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    TraceFilter filter;
    filter.min_address = 0x5000;
    filter.max_address = 0x5000;
    filter.address_mode = AddressFilterMode::MemoryOnly;

    const auto rep = analyzer.analyze_file(p, filter);
    TEST_ASSERT(rep.filtered_events_count == 1);
    TEST_ASSERT(rep.sample_timeline[0].event_type == TraceEventType::MemoryRead);
}

void test_18_filter_by_timestamp_range() {
    const auto p = kTestDir / "filter_time.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 1, 0, 100)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 2, 0, 200)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 3, 0, 300)));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    TraceFilter filter;
    filter.min_timestamp = 150;
    filter.max_timestamp = 250;

    const auto rep = analyzer.analyze_file(p, filter);
    TEST_ASSERT(rep.filtered_events_count == 1);
    TEST_ASSERT(rep.sample_timeline[0].timestamp == 200);
}

void test_19_filter_by_sequence_range() {
    const auto p = kTestDir / "filter_seq.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        for (std::uint64_t i = 1; i <= 20; ++i) {
            TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, i)));
        }
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    TraceFilter filter;
    filter.min_sequence = 5;
    filter.max_sequence = 10;

    const auto rep = analyzer.analyze_file(p, filter);
    TEST_ASSERT(rep.filtered_events_count == 6);
}

void test_20_combined_filters() {
    const auto p = kTestDir / "filter_comb.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 1, 0, 10, 0x1000)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 2, 1, 20, 0x1004)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::BranchTaken, 3, 0, 30, 0x2000)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 4, 0, 40, 0x1008)));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    TraceFilter filter;
    filter.event_types = std::vector<TraceEventType>{TraceEventType::InstructionRetired};
    filter.core_ids = std::vector<std::uint16_t>{0};
    filter.min_pc = 0x1000;
    filter.max_pc = 0x1004;

    const auto rep = analyzer.analyze_file(p, filter);
    TEST_ASSERT(rep.filtered_events_count == 1);
    TEST_ASSERT(rep.sample_timeline[0].sequence_number == 1);
}

void test_21_invalid_filter_range() {
    TraceFilter filter;
    filter.min_sequence = 100;
    filter.max_sequence = 50; // invalid!

    std::string err;
    TEST_ASSERT(!filter.is_valid(&err));
    TEST_ASSERT(!err.empty());
}

// -----------------------------------------------------------------------------
// Group C: Per-Core Timeline & Decoded Details (Tests 22–26)
// -----------------------------------------------------------------------------

void test_22_single_core_timeline() {
    const auto p = kTestDir / "tl_single.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::CPUStarted, 1, 0, 10, 0x8000)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 2, 0, 20, 0x8000)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::CPUStopped, 3, 0, 30, 0x8004)));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto tl = analyzer.extract_timeline(p);
    TEST_ASSERT(tl.size() == 3);
    TEST_ASSERT(tl[0].sequence_number == 1);
    TEST_ASSERT(tl[1].sequence_number == 2);
    TEST_ASSERT(tl[2].sequence_number == 3);
}

void test_23_multicore_timeline_preservation() {
    const auto p = kTestDir / "tl_multi.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 1, 0, 10, 0x1000)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 1, 1, 15, 0x5000)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 2, 0, 20, 0x1004)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 2, 1, 25, 0x5004)));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    TraceFilter filter0;
    filter0.core_ids = std::vector<std::uint16_t>{0};
    const auto tl0 = analyzer.extract_timeline(p, filter0);
    TEST_ASSERT(tl0.size() == 2);
    TEST_ASSERT(tl0[0].sequence_number == 1);
    TEST_ASSERT(tl0[1].sequence_number == 2);
}

void test_24_per_core_order_preservation() {
    const auto p = kTestDir / "tl_order.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        for (std::uint64_t i = 1; i <= 15; ++i) {
            TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, i)));
        }
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto tl = analyzer.extract_timeline(p);
    for (std::size_t i = 0; i < tl.size(); ++i) {
        TEST_ASSERT(tl[i].sequence_number == i + 1);
    }
}

void test_25_timestamp_ordering() {
    const auto p = kTestDir / "tl_time.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 1, 0, 100)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 2, 0, 200)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 3, 0, 300)));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto tl = analyzer.extract_timeline(p);
    TEST_ASSERT(tl[0].timestamp <= tl[1].timestamp);
    TEST_ASSERT(tl[1].timestamp <= tl[2].timestamp);
}

void test_26_decoded_metadata_details() {
    TraceEvent ev = make_event(TraceEventType::MemoryRead, 1, 0, 10, 0x1000, 0x4000, 0xCAFE);
    ev.access_size = 8;
    ev.add_flag(TraceEventFlags::IsAtomic);

    const std::string text = TraceAnalyzer::decode_event_details(ev);
    TEST_ASSERT(text.find("LDR [0x4000]") != std::string::npos);
    TEST_ASSERT(text.find("Atomic") != std::string::npos);
}

// -----------------------------------------------------------------------------
// Group D: Subsystem Summaries (Tests 27–31)
// -----------------------------------------------------------------------------

void test_27_instruction_summary() {
    const auto p = kTestDir / "sum_inst.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 1, 0, 10, 0x1000)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 2, 0, 20, 0x1004)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 3, 0, 30, 0x1008)));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(p);
    TEST_ASSERT(rep.instructions.total_retired == 3);
    TEST_ASSERT(rep.instructions.first_pc == 0x1000);
    TEST_ASSERT(rep.instructions.last_pc == 0x1008);
    TEST_ASSERT(rep.instructions.min_pc == 0x1000);
    TEST_ASSERT(rep.instructions.max_pc == 0x1008);
    TEST_ASSERT(rep.instructions.unique_pc_count == 3);
}

void test_28_branch_summary() {
    const auto p = kTestDir / "sum_branch.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        // Forward branch 0x1000 -> 0x1020
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::BranchTaken, 1, 0, 10, 0x1000, 0x1020)));
        // Backward branch 0x1040 -> 0x1000
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::BranchTaken, 2, 0, 20, 0x1040, 0x1000)));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(p);
    TEST_ASSERT(rep.branches.total_branches == 2);
    TEST_ASSERT(rep.branches.forward_branches == 1);
    TEST_ASSERT(rep.branches.backward_branches == 1);
}

void test_29_exception_summary() {
    const auto p = kTestDir / "sum_ex.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        // Sync Exception entry at EL1
        auto e1 = make_event(TraceEventType::ExceptionEntered, 1, 0, 10, 0x1000, 0x10080);
        e1.add_flag(TraceEventFlags::IsSynchronous);
        TEST_ASSERT(writer.write_event(e1));

        // Exception return
        auto r1 = make_event(TraceEventType::ExceptionReturned, 2, 0, 20, 0x10084, 0x1004);
        TEST_ASSERT(writer.write_event(r1));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(p);
    TEST_ASSERT(rep.exceptions.total_entries == 1);
    TEST_ASSERT(rep.exceptions.total_returns == 1);
    TEST_ASSERT(rep.exceptions.synchronous_entries == 1);
    TEST_ASSERT(rep.exceptions.unmatched_returns == 0);
    TEST_ASSERT(rep.exceptions.unmatched_entries == 0);
}

void test_30_memory_summary() {
    const auto p = kTestDir / "sum_mem.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        auto rd = make_event(TraceEventType::MemoryRead, 1, 0, 10, 0x1000, 0x4000, 0x1234);
        rd.access_size = 4;
        TEST_ASSERT(writer.write_event(rd));

        auto wr = make_event(TraceEventType::MemoryWrite, 2, 0, 20, 0x1004, 0x8000, 0x5678);
        wr.access_size = 8;
        wr.add_flag(TraceEventFlags::IsAtomic);
        TEST_ASSERT(writer.write_event(wr));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(p);
    TEST_ASSERT(rep.memory.total_reads == 1);
    TEST_ASSERT(rep.memory.total_writes == 1);
    TEST_ASSERT(rep.memory.total_atomics == 1);
    TEST_ASSERT(rep.memory.size_4b == 1);
    TEST_ASSERT(rep.memory.size_8b == 1);
    TEST_ASSERT(rep.memory.min_address == 0x4000);
    TEST_ASSERT(rep.memory.max_address == 0x8000);
}

void test_31_sysreg_summary() {
    const auto p = kTestDir / "sum_sysreg.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        auto r = make_event(TraceEventType::SystemRegisterAccess, 1, 0, 10, 0x1000, 0x30E01, 0); // MRS
        TEST_ASSERT(writer.write_event(r));

        auto w = make_event(TraceEventType::SystemRegisterAccess, 2, 0, 20, 0x1004, 0x30E01, 1); // MSR
        w.add_flag(TraceEventFlags::IsWrite);
        TEST_ASSERT(writer.write_event(w));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(p);
    TEST_ASSERT(rep.sysregs.total_accesses == 2);
    TEST_ASSERT(rep.sysregs.total_reads == 1);
    TEST_ASSERT(rep.sysregs.total_writes == 1);
    TEST_ASSERT(rep.sysregs.register_counts.at(0x30E01) == 2);
}

// -----------------------------------------------------------------------------
// Group E: Timer and Interrupt Causal Analysis (Tests 32–35)
// -----------------------------------------------------------------------------

void test_32_confirmed_timer_causal_chain() {
    const auto p = kTestDir / "causal_confirmed.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));

        // 1. Timer Expired (Physical, op=3)
        auto ev_exp = make_event(TraceEventType::TimerEvent, 1, 0, 100, 0x1000, 500, 500, 3 | (0 << 8));
        TEST_ASSERT(writer.write_event(ev_exp));

        // 2. Timer Asserted (Physical, op=4)
        auto ev_assert = make_event(TraceEventType::TimerEvent, 2, 0, 105, 0x1000, 500, 500, 4 | (0 << 8));
        TEST_ASSERT(writer.write_event(ev_assert));

        // 3. Interrupt Delivered (INTID 30)
        auto ev_del = make_event(TraceEventType::InterruptDelivered, 3, 0, 110, 0x1000, 0x10280, 0, 30);
        TEST_ASSERT(writer.write_event(ev_del));

        // 4. Exception Entered (IRQ vector)
        auto ev_ent = make_event(TraceEventType::ExceptionEntered, 4, 0, 115, 0x1000, 0x10280);
        ev_ent.add_flag(TraceEventFlags::IsInterrupt);
        TEST_ASSERT(writer.write_event(ev_ent));

        // 5. Context Changed
        auto ev_ctx = make_event(TraceEventType::ContextChanged, 5, 0, 120, 0x1000, 0x10280, 0x05);
        ev_ctx.add_flag(TraceEventFlags::IsInterrupt);
        TEST_ASSERT(writer.write_event(ev_ctx));

        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(p);
    TEST_ASSERT(rep.causal_chains.size() == 1);
    const auto& chain = rep.causal_chains[0];
    TEST_ASSERT(chain.status == CausalCorrelationStatus::ConfirmedSequence);
    TEST_ASSERT(chain.expire_seq == 1);
    TEST_ASSERT(chain.assert_seq == 2);
    TEST_ASSERT(chain.deliver_seq == 3);
    TEST_ASSERT(chain.entry_seq == 4);
    TEST_ASSERT(chain.context_change_seq == 5);
}

void test_33_incomplete_timer_causal_chain() {
    const auto p = kTestDir / "causal_incomplete.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        // Expired & Asserted, but cleared before delivery
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::TimerEvent, 1, 0, 100, 0x1000, 500, 500, 3)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::TimerEvent, 2, 0, 105, 0x1000, 500, 500, 4)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::TimerEvent, 3, 0, 110, 0x1000, 500, 500, 5))); // Cleared
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(p);
    TEST_ASSERT(rep.causal_chains.size() == 1);
    TEST_ASSERT(rep.causal_chains[0].status == CausalCorrelationStatus::IncompleteSequence);
}

void test_34_uncorrelated_interrupt_delivery() {
    const auto p = kTestDir / "causal_uncorrelated.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        // Deliver INTID 30 without any prior timer expiration or assertion
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InterruptDelivered, 1, 0, 100, 0x1000, 0x10280, 0, 30)));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(p);
    TEST_ASSERT(!rep.causal_chains.empty());
    TEST_ASSERT(rep.causal_chains[0].status == CausalCorrelationStatus::UncorrelatedEvent);
}

void test_35_cross_core_causal_isolation() {
    const auto p = kTestDir / "causal_cross_core.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        // Core 0 timer expired and asserted
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::TimerEvent, 1, 0, 100, 0x1000, 500, 500, 3)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::TimerEvent, 2, 0, 105, 0x1000, 500, 500, 4)));
        // Core 1 interrupt delivery (must NOT correlate with Core 0 timer)
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InterruptDelivered, 1, 1, 110, 0x2000, 0x10280, 0, 30)));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(p);
    // Core 0 should be incomplete, Core 1 should be uncorrelated
    for (const auto& c : rep.causal_chains) {
        TEST_ASSERT(c.status != CausalCorrelationStatus::ConfirmedSequence);
    }
}

// -----------------------------------------------------------------------------
// Group F: Sequence Validation & JSON Export (Tests 36–48)
// -----------------------------------------------------------------------------

void test_36_exception_return_without_entry() {
    const auto p = kTestDir / "val_ex_ret.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::ExceptionReturned, 1, 0)));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(p);
    bool found_diag = false;
    for (const auto& d : rep.sequence_diagnostics) {
        if (d.code == DiagnosticCode::ExceptionReturnWithoutEntry) {
            found_diag = true;
            TEST_ASSERT(d.severity == DiagnosticSeverity::Warning);
        }
    }
    TEST_ASSERT(found_diag);
}

void test_37_interrupt_delivered_without_assertion() {
    const auto p = kTestDir / "val_irq.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InterruptDelivered, 1, 0, 100, 0x1000, 0x10280, 0, 30)));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(p);
    bool found_diag = false;
    for (const auto& d : rep.sequence_diagnostics) {
        if (d.code == DiagnosticCode::InterruptWithoutAssertion) {
            found_diag = true;
        }
    }
    TEST_ASSERT(found_diag);
}

void test_38_timer_assert_without_expired() {
    const auto p = kTestDir / "val_timer_assert.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        // Assert without expired
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::TimerEvent, 1, 0, 100, 0x1000, 500, 500, 4)));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(p);
    bool found_diag = false;
    for (const auto& d : rep.sequence_diagnostics) {
        if (d.code == DiagnosticCode::TimerAssertWithoutExpired) {
            found_diag = true;
        }
    }
    TEST_ASSERT(found_diag);
}

void test_39_cpu_stopped_without_start() {
    const auto p = kTestDir / "val_cpu_stop.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::CPUStopped, 1, 0)));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(p);
    bool found_diag = false;
    for (const auto& d : rep.sequence_diagnostics) {
        if (d.code == DiagnosticCode::CPUStoppedWithoutStart) {
            found_diag = true;
            TEST_ASSERT(d.severity == DiagnosticSeverity::Info);
        }
    }
    TEST_ASSERT(found_diag);
}

void test_40_duplicate_cpu_start_stop() {
    const auto p = kTestDir / "val_dup_start.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::CPUStarted, 1, 0)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::CPUStarted, 2, 0))); // Duplicate!
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(p);
    bool found_diag = false;
    for (const auto& d : rep.sequence_diagnostics) {
        if (d.code == DiagnosticCode::DuplicateCPUStart) {
            found_diag = true;
            TEST_ASSERT(d.severity == DiagnosticSeverity::Warning);
        }
    }
    TEST_ASSERT(found_diag);
}

void test_41_sequence_regression() {
    const auto p = kTestDir / "val_seq_regress.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 5, 0)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 4, 0))); // Regression!
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(p);
    bool found_diag = false;
    for (const auto& d : rep.sequence_diagnostics) {
        if (d.code == DiagnosticCode::SequenceRegression) {
            found_diag = true;
            TEST_ASSERT(d.is_definitely_invalid);
        }
    }
    TEST_ASSERT(found_diag);
}

void test_42_context_change_invalid_el() {
    const auto p = kTestDir / "val_ctx_el.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        // prev_el=1, new_el=7 (invalid EL!)
        auto ev = make_event(TraceEventType::ContextChanged, 1, 0);
        ev.data = 1 | (7 << 2);
        TEST_ASSERT(writer.write_event(ev));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(p);
    bool found_diag = false;
    for (const auto& d : rep.sequence_diagnostics) {
        if (d.code == DiagnosticCode::ContextChangeIncompleteMetadata) {
            found_diag = true;
            TEST_ASSERT(d.severity == DiagnosticSeverity::Error);
        }
    }
    TEST_ASSERT(found_diag);
}

void test_43_valid_json_export() {
    const auto p = kTestDir / "json_export.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 1)));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::MemoryRead, 2)));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(p);
    const std::string json = export_report_to_json(rep);
    TEST_ASSERT(json.find("\"trace_file\":") != std::string::npos);
    TEST_ASSERT(json.find("\"integrity\":") != std::string::npos);
    TEST_ASSERT(json.find("\"subsystems\":") != std::string::npos);
    TEST_ASSERT(json.find("\"instructions\":") != std::string::npos);
    TEST_ASSERT(json.find("\"memory\":") != std::string::npos);
}

void test_44_json_string_escaping() {
    const std::string test_str = "Quote: \" Backslash: \\ Newline: \n Tab: \t";
    const std::string escaped = escape_json_string(test_str);
    TEST_ASSERT(escaped.find("\\\"") != std::string::npos);
    TEST_ASSERT(escaped.find("\\\\") != std::string::npos);
    TEST_ASSERT(escaped.find("\\n") != std::string::npos);
    TEST_ASSERT(escaped.find("\\t") != std::string::npos);
}

void test_45_json_timeline_export() {
    const auto p = kTestDir / "json_tl.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 1)));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto tl = analyzer.extract_timeline(p);
    const std::string json = export_timeline_to_json(tl);
    TEST_ASSERT(json.find("\"sequence_number\": 1") != std::string::npos);
    TEST_ASSERT(json.find("\"event_type\": \"InstructionRetired\"") != std::string::npos);
}

void test_46_empty_trace_json_export() {
    const auto p = kTestDir / "json_empty.wtr";
    {
        TraceFileWriter writer;
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(p);
    const std::string json = export_report_to_json(rep);
    TEST_ASSERT(json.find("\"status\": \"ValidNoEvents\"") != std::string::npos);
}

void test_47_corrupt_trace_json_diagnostics() {
    const auto p = kTestDir / "json_corrupt.wtr";
    {
        TraceFileWriter writer(10);
        TEST_ASSERT(writer.open(p, true));
        TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, 1)));
        TEST_ASSERT(writer.close());
    }

    // Corrupt block CRC
    {
        std::fstream fs(p, std::ios::in | std::ios::out | std::ios::binary);
        fs.seekp(100);
        const char bad = '\xFF';
        fs.write(&bad, 1);
    }

    TraceAnalyzer analyzer;
    const auto rep = analyzer.analyze_file(p);
    const std::string json = export_report_to_json(rep);
    TEST_ASSERT(json.find("\"status\": \"CrcMismatch\"") != std::string::npos);
    TEST_ASSERT(json.find("\"diagnostics\": [") != std::string::npos);
}

void test_48_streaming_bounded_memory() {
    const auto p = kTestDir / "stream_large.wtr";
    constexpr std::size_t kTotalEvents = 5000;
    {
        TraceFileWriter writer(100);
        TEST_ASSERT(writer.open(p, true));
        for (std::size_t i = 1; i <= kTotalEvents; ++i) {
            TEST_ASSERT(writer.write_event(make_event(TraceEventType::InstructionRetired, i)));
        }
        TEST_ASSERT(writer.close());
    }

    TraceAnalyzer analyzer;
    std::size_t count = 0;
    const bool ok = analyzer.stream_timeline(p, TraceFilter{}, [&](const TimelineEntry& te) {
        TEST_ASSERT(te.sequence_number == count + 1);
        count++;
        return true;
    });
    TEST_ASSERT(ok);
    TEST_ASSERT(count == kTotalEvents);
}

} // namespace

int main() {
    std::cout << "=== WinIOS Phase 52 Stage 15: Trace Analyzer Unit Tests ===\n";

    setup_test_dir();

    std::cout << "  Group A: File Format Inspection & Integrity (Tests 1-13)... " << std::flush;
    test_1_valid_empty_trace();
    test_2_valid_single_block_trace();
    test_3_valid_multi_block_trace();
    test_4_valid_footer_verification();
    test_5_missing_footer_unfinalized();
    test_6_truncated_header();
    test_7_truncated_block();
    test_8_invalid_header_magic();
    test_9_unsupported_version();
    test_10_crc_mismatch();
    test_11_corrupted_footer_magic();
    test_12_unknown_event_type();
    test_13_dropped_marker_detection();
    std::cout << "PASS\n";

    std::cout << "  Group B: Event Filtering (Tests 14-21)... " << std::flush;
    test_14_filter_by_event_type();
    test_15_filter_by_core_id();
    test_16_filter_by_exception_level();
    test_17_filter_by_address_mode();
    test_18_filter_by_timestamp_range();
    test_19_filter_by_sequence_range();
    test_20_combined_filters();
    test_21_invalid_filter_range();
    std::cout << "PASS\n";

    std::cout << "  Group C: Per-Core Timeline & Decoded Details (Tests 22-26)... " << std::flush;
    test_22_single_core_timeline();
    test_23_multicore_timeline_preservation();
    test_24_per_core_order_preservation();
    test_25_timestamp_ordering();
    test_26_decoded_metadata_details();
    std::cout << "PASS\n";

    std::cout << "  Group D: Subsystem Summaries (Tests 27-31)... " << std::flush;
    test_27_instruction_summary();
    test_28_branch_summary();
    test_29_exception_summary();
    test_30_memory_summary();
    test_31_sysreg_summary();
    std::cout << "PASS\n";

    std::cout << "  Group E: Timer and Interrupt Causal Analysis (Tests 32-35)... " << std::flush;
    test_32_confirmed_timer_causal_chain();
    test_33_incomplete_timer_causal_chain();
    test_34_uncorrelated_interrupt_delivery();
    test_35_cross_core_causal_isolation();
    std::cout << "PASS\n";

    std::cout << "  Group F: Sequence Validation & JSON Export (Tests 36-48)... " << std::flush;
    test_36_exception_return_without_entry();
    test_37_interrupt_delivered_without_assertion();
    test_38_timer_assert_without_expired();
    test_39_cpu_stopped_without_start();
    test_40_duplicate_cpu_start_stop();
    test_41_sequence_regression();
    test_42_context_change_invalid_el();
    test_43_valid_json_export();
    test_44_json_string_escaping();
    test_45_json_timeline_export();
    test_46_empty_trace_json_export();
    test_47_corrupt_trace_json_diagnostics();
    test_48_streaming_bounded_memory();
    std::cout << "PASS\n";

    cleanup_test_dir();

    std::cout << "All 48/48 Phase 52 Stage 15 Trace Analyzer Unit Tests PASSED successfully!\n";
    return 0;
}

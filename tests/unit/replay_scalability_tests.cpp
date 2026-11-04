#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "replay/replay_diagnostics.h"
#include "replay/replay_engine.h"
#include "replay/replay_event_cursor.h"
#include "replay/replay_memory_report.h"
#include "replay/replay_profiler.h"
#include "replay/replay_session.h"
#include "replay/replay_state_hash.h"
#include "replay/replay_workload.h"
#include "sndl/sndl_event_encoder.h"
#include "sndl/sndl_validation.h"
#include "sndl/sndl_writer.h"
#include "snapshot/snapshot_manager.h"
#include "snapshot/wsnap_format.h"
#include "trace/trace_serializer.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace winios::replay;
using namespace winios::sndl;

#define CHECK(expr)                                                           \
    do {                                                                      \
        if (!(expr)) {                                                        \
            std::cerr << "CHECK failed: " #expr " at " << __FILE__ << ":"     \
                      << __LINE__ << "\n";                                    \
            std::abort();                                                     \
        }                                                                     \
    } while (false)

// -----------------------------------------------------------------------------
// Byte/file helpers
// -----------------------------------------------------------------------------
std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    CHECK(in.is_open());
    const auto size = static_cast<std::size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(size);
    if (size > 0) {
        in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
        CHECK(static_cast<std::size_t>(in.gcount()) == size);
    }
    return data;
}

void write_bytes(const std::filesystem::path& path,
                 const std::vector<std::uint8_t>& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    CHECK(out.is_open());
    if (!data.empty()) {
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    }
    CHECK(out.good());
}

void copy_file(const std::filesystem::path& src, const std::filesystem::path& dst) {
    std::error_code ec;
    std::filesystem::copy_file(src, dst,
        std::filesystem::copy_options::overwrite_existing, ec);
    CHECK(!ec);
}

void patch_u16(std::vector<std::uint8_t>& data, std::size_t offset,
               std::uint16_t value) {
    CHECK(offset + 2 <= data.size());
    winios::trace::encode_u16_le(data.data() + offset, value);
}

void patch_u32(std::vector<std::uint8_t>& data, std::size_t offset,
               std::uint32_t value) {
    CHECK(offset + 4 <= data.size());
    winios::trace::encode_u32_le(data.data() + offset, value);
}

void patch_u64(std::vector<std::uint8_t>& data, std::size_t offset,
               std::uint64_t value) {
    CHECK(offset + 8 <= data.size());
    winios::trace::encode_u64_le(data.data() + offset, value);
}

void refresh_wsnap_header_crc(std::vector<std::uint8_t>& data) {
    CHECK(data.size() >= 64);
    const auto crc = winios::trace::calculate_crc32c(data.data(), 60);
    patch_u32(data, 60, crc);
}

void refresh_sndl_header_crc(std::vector<std::uint8_t>& data) {
    CHECK(data.size() >= 64);
    const auto crc = winios::trace::calculate_crc32c(data.data(), 60);
    patch_u32(data, 60, crc);
}

void refresh_sndl_block_header_crc(std::vector<std::uint8_t>& data,
                                   std::size_t block_offset) {
    CHECK(block_offset + 48 <= data.size());
    const auto crc = winios::trace::calculate_crc32c(
        data.data() + block_offset, 44);
    patch_u32(data, block_offset + 44, crc);
}

// Bitwise reference for the WinIOS CRC32C convention:
// state is seeded with `initial`, bytes are processed LSB-first, and the final
// returned value is XORed with 0xFFFFFFFF. The zero-length case is defined as 0
// by calculate_crc32c, so it is handled explicitly.
std::uint32_t reference_crc32c(const std::uint8_t* data, std::size_t length,
                               std::uint32_t initial = 0xFFFFFFFFu) {
    if (!data || length == 0) {
        return 0;
    }
    std::uint32_t crc = initial;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1u) ? 0x82F63B78u : 0u);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

ReplayConfig make_config(std::uint64_t max_instructions,
                         std::uint64_t checkpoint_interval,
                         bool exact = true) {
    ReplayConfig config{};
    config.max_instructions = max_instructions;
    config.checkpoint_interval = checkpoint_interval;
    config.require_exact_event_consumption = exact;
    config.require_snapshot_association = true;
    config.divergence_report_path.clear();
    return config;
}

// Builds a structurally valid SNDL with matching/non-matching association.
void create_sndl(const std::filesystem::path& path,
                 std::uint64_t base_snapshot_hash,
                 std::uint64_t total_instructions,
                 const std::vector<SndlRecord>& records = {}) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    WSndlHeader header{};
    header.core_count = 1;
    header.base_snapshot_hash = base_snapshot_hash;
    header.total_instructions = total_instructions;

    SndlWriter writer;
    CHECK(writer.open(path, header).is_success());
    for (const auto& record : records) {
        CHECK(writer.write_record(record).is_success());
    }
    CHECK(writer.finalize(SndlFinalizationStatus::Completed,
                          total_instructions).is_success());
}

std::uint64_t create_nop_snapshot(const std::filesystem::path& path) {
    using winios::GICv3;
    using winios::VirtualTime;

    std::error_code ec;
    std::filesystem::remove(path, ec);

    CPU cpu;
    Memory memory(64 * 1024);
    VirtualTime vt;
    GICv3 gic;
    cpu.reset();
    cpu.bind_virtual_time(&vt, 0);
    for (std::size_t i = 0; i < 256; ++i) {
        memory.write_u32(i * 4, 0xD503201Fu); // NOP
    }
    cpu.set_program_counter(0);

    winios::snapshot::SnapshotRestoreContext context{};
    context.primary_cpu = &cpu;
    context.memory = &memory;
    context.virtual_time = &vt;
    context.gic = &gic;

    winios::snapshot::SnapshotCaptureOptions options{};
    options.description = "Stage 21 NOP completion baseline";
    const auto capture = winios::snapshot::SnapshotManager::capture_to_file(
        context, path, options);
    CHECK(capture.success);

    winios::snapshot::SnapshotInspection inspection{};
    const auto inspect = winios::snapshot::SnapshotManager::inspect_file(
        path, inspection);
    CHECK(inspect.success);
    return inspection.header.header_crc32c;
}

std::vector<SndlRecord> read_all_records(const std::filesystem::path& path) {    ReplayEventCursor cursor;
    CHECK(cursor.open(path).is_success());
    std::vector<SndlRecord> records;
    while (cursor.has_next()) {
        records.push_back(cursor.current());
        CHECK(cursor.advance().is_success());
    }
    cursor.close();
    return records;
}

// -----------------------------------------------------------------------------
// Group A: Opt-in profiling parity and counters
// -----------------------------------------------------------------------------
void test_group_a_profiling_parity(const std::filesystem::path& workload_snap,
                                   const std::filesystem::path& workload_sndl) {
    std::cout << "[Test Group A] Opt-in profiling parity and counters...\n";

    auto off_config = make_config(20000, 5000, true);
    ReplayExecutionStats off_stats{};
    const auto off_result = ReplayEngine::run(workload_snap, workload_sndl,
                                              off_config, off_stats);
    if (off_result.is_failure()) {
        std::cerr << "profiling-off replay failed: " << off_result.message
                  << " (status "
                  << replay_completion_status_name(off_stats.completion_status)
                  << ")\n";
    }
    CHECK(off_result.is_success());
    CHECK(off_stats.completion_status == ReplayCompletionStatus::CompletedNormally);
    CHECK(off_stats.instructions_executed == 20000);
    CHECK(off_stats.events_consumed == 200);
    CHECK(off_stats.checkpoints_taken == 6); // initial + 4 periodic + final

    auto on_config = off_config;
    on_config.enable_profiling = true;
    const std::filesystem::path profile_path = "stage21_test_profile.json";
    on_config.profile_report_path = profile_path;
    ReplayExecutionStats on_stats{};
    ReplayProfile profile{};
    CHECK(ReplayEngine::run(workload_snap, workload_sndl, on_config,
                            on_stats, &profile).is_success());

    CHECK(on_stats.instructions_executed == off_stats.instructions_executed);
    CHECK(on_stats.events_consumed == off_stats.events_consumed);
    CHECK(on_stats.checkpoints_taken == off_stats.checkpoints_taken);
    CHECK(on_stats.final_state_hashes == off_stats.final_state_hashes);

    CHECK(profile.enabled);
    CHECK(profile.instructions_executed == off_stats.instructions_executed);
    CHECK(profile.events_consumed == off_stats.events_consumed);
    CHECK(profile.checkpoints_recorded == off_stats.checkpoints_taken);
    CHECK(profile.completion_status == ReplayCompletionStatus::CompletedNormally);
    CHECK(!profile.diverged);

    CHECK(profile.cursor_advances == profile.events_consumed);
    CHECK(profile.blocks_loaded > 0);
    CHECK(profile.block_bytes_read > 0);
    CHECK(profile.records_decoded == profile.events_consumed);
    CHECK(profile.crc_calls >= profile.blocks_loaded);
    CHECK(profile.crc_bytes >= profile.block_bytes_read);
    CHECK(profile.hash_calls == off_stats.checkpoints_taken + 1);
    CHECK(profile.memory_hash_calls == profile.hash_calls);
    CHECK(profile.memory_bytes_hashed == profile.memory_hash_calls * 2ULL * 1024ULL * 1024ULL);
    CHECK(profile.input_route_attempts == profile.events_consumed);
    CHECK(profile.input_routes_consumed == profile.events_consumed);

    CHECK(profile.prepare_total.calls == 1);
    CHECK(profile.run_total.calls == 1);
    CHECK(profile.cpu_execution.calls == profile.instructions_executed);
    CHECK(profile.checkpoint_total.calls == off_stats.checkpoints_taken);
    CHECK(profile.checkpoint_hash.calls == off_stats.checkpoints_taken);
    CHECK(profile.memory_hash.calls == profile.memory_hash_calls);
    CHECK(profile.final_hash.calls == 1);
    CHECK(profile.completion_validation.calls > 0);

    CHECK(std::filesystem::exists(profile_path));
    const auto profile_text = read_bytes(profile_path);
    const std::string json(profile_text.begin(), profile_text.end());
    CHECK(json.find("\"phases\"") != std::string::npos);
    CHECK(json.find("\"instructions_executed\": 20000") != std::string::npos);
    CHECK(json.find("\"memory_hash_calls\"") != std::string::npos);

    // Text and JSON writers must round-trip the same counters.
    CHECK(ReplayProfiler::write_text("stage21_test_profile.txt", profile).is_success());
    CHECK(ReplayProfiler::write_json("stage21_test_profile_roundtrip.json",
                                     profile).is_success());
    CHECK(std::filesystem::exists("stage21_test_profile.txt"));
    CHECK(std::filesystem::exists("stage21_test_profile_roundtrip.json"));

    std::error_code ec;
    std::filesystem::remove(profile_path, ec);
    std::filesystem::remove("stage21_test_profile.txt", ec);
    std::filesystem::remove("stage21_test_profile_roundtrip.json", ec);

    std::cout << "  -> PASSED: profiling leaves architectural hashes and SNDL accounting identical\n";
}

// -----------------------------------------------------------------------------
// Group B: Deterministic workload generation
// -----------------------------------------------------------------------------
void test_group_b_workload_determinism() {
    std::cout << "[Test Group B] Deterministic workload generation...\n";

    CHECK(workload_counter_read_instruction() == 0xD53BE020u);
    CHECK(workload_nop_instruction() == 0xD503201Fu);
    CHECK(workload_branch_instruction(100) == 0x17FFFF9Du);
    CHECK(workload_branch_instruction(2) == 0x17FFFFFFu);

    ReplayWorkloadSpec invalid{};
    invalid.instructions = 1001;
    invalid.loop_instructions = 100;
    invalid.counter_event_stride = 100;
    CHECK(!generate_counter_loop_workload(invalid).success);

    invalid = {};
    invalid.instructions = 1000;
    invalid.loop_instructions = 1;
    CHECK(!generate_counter_loop_workload(invalid).success);

    invalid = {};
    invalid.instructions = 1000;
    invalid.loop_instructions = 100;
    invalid.counter_event_stride = 50;
    CHECK(!generate_counter_loop_workload(invalid).success);

    invalid = {};
    invalid.instructions = 1000;
    invalid.loop_instructions = 100;
    invalid.counter_event_stride = 100;
    invalid.memory_bytes = 128;
    CHECK(!generate_counter_loop_workload(invalid).success);

    ReplayWorkloadSpec spec_a{};
    spec_a.instructions = 1000;
    spec_a.loop_instructions = 100;
    spec_a.counter_event_stride = 100;
    spec_a.memory_bytes = 128 * 1024;
    spec_a.wsnap_path = "stage21_test_det_a.wsnap";
    spec_a.sndl_path = "stage21_test_det_a.sndl";
    spec_a.description = "Stage 21 determinism A";
    const auto result_a = generate_counter_loop_workload(spec_a);
    CHECK(result_a.success);
    CHECK(result_a.total_instructions == 1000);
    CHECK(result_a.total_records == 10);
    CHECK(result_a.wsnap_bytes > spec_a.memory_bytes);
    CHECK(result_a.sndl_bytes > 0);

    ReplayWorkloadSpec spec_b = spec_a;
    spec_b.wsnap_path = "stage21_test_det_b.wsnap";
    spec_b.sndl_path = "stage21_test_det_b.sndl";
    spec_b.description = "Stage 21 determinism B";
    const auto result_b = generate_counter_loop_workload(spec_b);
    CHECK(result_b.success);
    CHECK(result_b.total_instructions == result_a.total_instructions);
    CHECK(result_b.total_records == result_a.total_records);
    CHECK(result_b.memory_bytes == result_a.memory_bytes);

    // Replaying two independently generated artifacts must produce the same
    // architectural state even though snapshot timestamps differ.
    const auto config = make_config(1000, 0, true);
    ReplayExecutionStats stats_a{};
    ReplayExecutionStats stats_b{};
    CHECK(ReplayEngine::run(spec_a.wsnap_path, spec_a.sndl_path, config,
                            stats_a).is_success());
    CHECK(ReplayEngine::run(spec_b.wsnap_path, spec_b.sndl_path, config,
                            stats_b).is_success());
    CHECK(stats_a.final_state_hashes == stats_b.final_state_hashes);
    CHECK(stats_a.events_consumed == 10);
    CHECK(stats_b.events_consumed == 10);

    // Record streams must match field-for-field.
    const auto records_a = read_all_records(spec_a.sndl_path);
    const auto records_b = read_all_records(spec_b.sndl_path);
    CHECK(records_a.size() == records_b.size());
    for (std::size_t i = 0; i < records_a.size(); ++i) {
        CHECK(records_a[i] == records_b[i]);
        CHECK(records_a[i].event_type == SndlEventType::CounterReadResult);
        CHECK(records_a[i].instruction_retirement_count == i * 100);
        CHECK(records_a[i].payload1 == 0x5F01);
        CHECK(records_a[i].payload2 == i * 100);
        CHECK(records_a[i].sequence_num == i + 1);
    }

    std::error_code ec;
    for (const auto& path : {spec_a.wsnap_path, spec_a.sndl_path,
                             spec_b.wsnap_path, spec_b.sndl_path}) {
        std::filesystem::remove(path, ec);
    }

    std::cout << "  -> PASSED: workload word encodings, records and replay hashes are reproducible\n";
}

// -----------------------------------------------------------------------------
// Group C: Checkpoint interval scalability
// -----------------------------------------------------------------------------
void test_group_c_checkpoint_intervals(const std::filesystem::path& snap,
                                       const std::filesystem::path& sndl) {
    std::cout << "[Test Group C] Checkpoint interval scalability...\n";

    const auto baseline = make_config(1000, 0, true);
    ReplayExecutionStats baseline_stats{};
    CHECK(ReplayEngine::run(snap, sndl, baseline, baseline_stats).is_success());
    CHECK(baseline_stats.checkpoints_taken == 2); // initial + final
    CHECK(baseline_stats.final_state_hashes.combined_hash != 0);

    struct Case {
        std::uint64_t interval;
        std::uint64_t expected_checkpoints;
    };
    const Case cases[] = {
        {0, 2},
        {100, 12},
        {250, 6},
        {1000, 3},
        {2000, 2}
    };

    for (const auto& c : cases) {
        ReplayConfig config = make_config(1000, c.interval, true);
        ReplayExecutionStats stats{};
        CHECK(ReplayEngine::run(snap, sndl, config, stats).is_success());
        CHECK(stats.checkpoints_taken == c.expected_checkpoints);
        CHECK(stats.final_state_hashes == baseline_stats.final_state_hashes);

        ReplaySession session;
        CHECK(session.prepare(snap, sndl, config).is_success());
        CHECK(session.run().is_success());
        const auto& checkpoints = session.checkpoint_manager().checkpoints();
        CHECK(checkpoints.size() == c.expected_checkpoints);
        CHECK(checkpoints.front().instruction_retirement_count == 0);
        CHECK(checkpoints.back().instruction_retirement_count == 1000);
        CHECK(checkpoints.back().hashes == stats.final_state_hashes);
        for (std::size_t i = 1; i < checkpoints.size(); ++i) {
            CHECK(checkpoints[i].instruction_retirement_count >=
                  checkpoints[i - 1].instruction_retirement_count);
        }
        session.close();
    }

    std::cout << "  -> PASSED: checkpoint counts scale as configured with identical final hashes\n";
}

// -----------------------------------------------------------------------------
// Group D: CRC32C slicing-by-8 bit-exactness
// -----------------------------------------------------------------------------
void test_group_d_crc_reference() {
    std::cout << "[Test Group D] CRC32C slicing-by-8 bit-exactness...\n";

    std::vector<std::uint8_t> data(8192);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::uint8_t>((i * 131u + (i >> 3) + 0x5Au) & 0xFFu);
    }

    const std::size_t lengths[] = {0, 1, 2, 3, 7, 8, 9, 15, 16, 31, 32,
                                   63, 64, 255, 256, 1023, 4096, 8192};
    for (const std::size_t length : lengths) {
        const auto expected = reference_crc32c(data.data(), length);
        const auto actual = winios::trace::calculate_crc32c(data.data(), length);
        if (actual != expected) {
            std::cerr << "CRC mismatch length " << length << ": expected 0x"
                      << std::hex << expected << ", actual 0x" << actual
                      << std::dec << "\n";
        }
        CHECK(actual == expected);
    }

    const std::uint32_t seeds[] = {0u, 1u, 0xFFFFFFFFu, 0x12345678u, 0xDEADBEEFu};
    for (const std::uint32_t seed : seeds) {
        for (const std::size_t length : {1u, 7u, 8u, 9u, 16u, 255u, 4096u}) {
            const auto expected = reference_crc32c(data.data(), length, seed);
            const auto actual = winios::trace::calculate_crc32c(
                data.data(), length, seed);
            CHECK(actual == expected);
        }
    }

    // Chained double hashing used by ReplayStateHash::hash_memory.
    for (const std::size_t length : {1u, 8u, 64u, 1024u, 8192u}) {
        const auto expected_c1 = reference_crc32c(data.data(), length);
        const auto expected_c2 = reference_crc32c(data.data(), length, expected_c1);
        const auto actual_c1 = winios::trace::calculate_crc32c(data.data(), length);
        const auto actual_c2 = winios::trace::calculate_crc32c(
            data.data(), length, actual_c1);
        CHECK(actual_c1 == expected_c1);
        CHECK(actual_c2 == expected_c2);
        CHECK(((static_cast<std::uint64_t>(actual_c1) << 32u) | actual_c2) ==
              ((static_cast<std::uint64_t>(expected_c1) << 32u) | expected_c2));
    }

    std::cout << "  -> PASSED: slicing-by-8 matches bitwise reference for all lengths, seeds and chains\n";
}

// -----------------------------------------------------------------------------
// Group E: Large artifact validation and copy-only corruption handling
// -----------------------------------------------------------------------------
void test_group_e_large_artifact_validation() {
    std::cout << "[Test Group E] Large artifact validation and corruption handling...\n";

    ReplayWorkloadSpec spec{};
    spec.instructions = 100000;
    spec.loop_instructions = 100;
    spec.counter_event_stride = 100;
    spec.memory_bytes = 8ULL * 1024ULL * 1024ULL;
    spec.pattern_memory_bytes = spec.memory_bytes;
    spec.wsnap_path = "stage21_test_large.wsnap";
    spec.sndl_path = "stage21_test_large.sndl";
    spec.description = "Stage 21 large artifact validation";
    const auto generated = generate_counter_loop_workload(spec);
    CHECK(generated.success);
    CHECK(generated.total_records == 1000);
    CHECK(generated.wsnap_bytes > spec.memory_bytes);
    CHECK(generated.sndl_bytes > 60000);

    const auto report = ReplayDiagnostics::validate_artifacts(
        spec.wsnap_path, spec.sndl_path, true);
    CHECK(report.valid);
    CHECK(report.association_checked);
    CHECK(report.association_match);
    CHECK(report.total_records == 1000);
    CHECK(report.total_instructions == 100000);
    CHECK(report.base_snapshot_hash == report.actual_snapshot_hash);
    CHECK(report.checks.size() >= 4);
    for (const auto& check : report.checks) {
        CHECK(check.passed);
    }
    CHECK(ReplayDiagnostics::format_artifact_report(report).find("VALID") !=
          std::string::npos);

    ReplayConfig replay_config = make_config(100000, 50000, true);
    ReplayExecutionStats replay_stats{};
    CHECK(ReplayEngine::run(spec.wsnap_path, spec.sndl_path, replay_config,
                            replay_stats).is_success());
    CHECK(replay_stats.completion_status == ReplayCompletionStatus::CompletedNormally);
    CHECK(replay_stats.instructions_executed == 100000);
    CHECK(replay_stats.events_consumed == 1000);
    CHECK(replay_stats.checkpoints_taken == 4);

    const auto verification = ReplayDiagnostics::verify_replay(
        spec.wsnap_path, spec.sndl_path, replay_config, true);
    CHECK(verification.valid);
    CHECK(verification.events_consumed == verification.events_expected);
    CHECK(verification.events_expected == 1000);
    CHECK(verification.hashes == replay_stats.final_state_hashes);
    CHECK(ReplayDiagnostics::format_verification(verification).find("VERIFIED") !=
          std::string::npos);

    const auto original_snap = read_bytes(spec.wsnap_path);
    const auto original_sndl = read_bytes(spec.sndl_path);
    const auto original_size = original_snap.size();

    auto expect_wsnap_failure = [&](const std::filesystem::path& path,
                                    winios::snapshot::SnapshotErrorCode code) {
        const auto result = winios::snapshot::SnapshotManager::validate_file(path);
        CHECK(!result.valid);
        CHECK(result.error_code == code);
        const auto artifact = ReplayDiagnostics::validate_artifacts(
            path, spec.sndl_path, false);
        CHECK(!artifact.valid);
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    auto expect_sndl_failure = [&](const std::filesystem::path& path,
                                   SndlErrorCode code) {
        const auto result = SndlValidation::validate_file(path);
        CHECK(!result.is_valid);
        CHECK(result.code == code);
        const auto artifact = ReplayDiagnostics::validate_artifacts(
            spec.wsnap_path, path, false);
        CHECK(!artifact.valid);
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    // --- WSNap corruptions on copies ---
    {
        const std::filesystem::path truncated = "stage21_test_wsnap_truncated.wsnap";
        write_bytes(truncated, std::vector<std::uint8_t>(
            original_snap.begin(), original_snap.begin() + 32));
        expect_wsnap_failure(truncated,
                             winios::snapshot::SnapshotErrorCode::TruncatedFile);
    }
    {
        const std::filesystem::path bad_magic = "stage21_test_wsnap_bad_magic.wsnap";
        auto data = original_snap;
        data[0] ^= 0xFFu;
        write_bytes(bad_magic, data);
        expect_wsnap_failure(bad_magic,
                             winios::snapshot::SnapshotErrorCode::InvalidMagic);
    }
    {
        const std::filesystem::path bad_crc = "stage21_test_wsnap_header_crc.wsnap";
        auto data = original_snap;
        data[20] ^= 0xFFu;
        write_bytes(bad_crc, data);
        expect_wsnap_failure(bad_crc,
                             winios::snapshot::SnapshotErrorCode::HeaderCrcMismatch);
    }
    {
        const std::filesystem::path bad_version =
            "stage21_test_wsnap_version.wsnap";
        auto data = original_snap;
        patch_u16(data, 8, 2);
        refresh_wsnap_header_crc(data);
        write_bytes(bad_version, data);
        expect_wsnap_failure(bad_version,
                             winios::snapshot::SnapshotErrorCode::UnsupportedVersion);
    }
    {
        const std::filesystem::path truncated_tail =
            "stage21_test_wsnap_truncated_tail.wsnap";
        write_bytes(truncated_tail, std::vector<std::uint8_t>(
            original_snap.begin(),
            original_snap.begin() + static_cast<std::ptrdiff_t>(original_size / 2)));
        const auto result = winios::snapshot::SnapshotManager::validate_file(
            truncated_tail);
        CHECK(!result.valid);
        std::error_code ec;
        std::filesystem::remove(truncated_tail, ec);
    }

    winios::snapshot::WSnapHeader wsnap_header{};
    CHECK(winios::snapshot::decode_wsnap_header(
        original_snap.data(), 64, wsnap_header).success);
    CHECK(wsnap_header.section_count > 0);

    {
        std::vector<winios::snapshot::WSnapSectionDescriptor> descriptors(
            wsnap_header.section_count);
        for (std::size_t i = 0; i < descriptors.size(); ++i) {
            CHECK(winios::snapshot::decode_section_descriptor(
                original_snap.data() + wsnap_header.section_dir_offset +
                    i * winios::snapshot::kWSnapSectionDescriptorSize,
                winios::snapshot::kWSnapSectionDescriptorSize,
                descriptors[i]).success);
        }
        const auto payload = std::find_if(
            descriptors.begin(), descriptors.end(),
            [](const winios::snapshot::WSnapSectionDescriptor& d) {
                return d.compressed_size > 64;
            });
        CHECK(payload != descriptors.end());

        const std::filesystem::path bad_section_crc =
            "stage21_test_wsnap_section_crc.wsnap";
        auto data = original_snap;
        const auto payload_offset = static_cast<std::size_t>(payload->file_offset);
        data[payload_offset + 32] ^= 0xFFu;
        write_bytes(bad_section_crc, data);
        expect_wsnap_failure(bad_section_crc,
                             winios::snapshot::SnapshotErrorCode::SectionCrcMismatch);

        const std::filesystem::path bad_section_length =
            "stage21_test_wsnap_section_length.wsnap";
        data = original_snap;
        const auto desc_offset = static_cast<std::size_t>(
            wsnap_header.section_dir_offset +
            static_cast<std::uint64_t>(std::distance(descriptors.begin(), payload)) *
                winios::snapshot::kWSnapSectionDescriptorSize);
        patch_u64(data, desc_offset + 16, 0xFFFFFFFFULL);
        write_bytes(bad_section_length, data);
        expect_wsnap_failure(bad_section_length,
                             winios::snapshot::SnapshotErrorCode::OversizedSection);
    }

    // --- SNDL corruptions on copies ---
    {
        const std::filesystem::path truncated = "stage21_test_sndl_truncated.sndl";
        write_bytes(truncated, std::vector<std::uint8_t>(
            original_sndl.begin(), original_sndl.begin() + 32));
        expect_sndl_failure(truncated, SndlErrorCode::FileTooShort);
    }
    {
        const std::filesystem::path bad_crc = "stage21_test_sndl_header_crc.sndl";
        auto data = original_sndl;
        data[24] ^= 0xFFu;
        write_bytes(bad_crc, data);
        expect_sndl_failure(bad_crc, SndlErrorCode::HeaderCrcMismatch);
    }
    {
        const std::filesystem::path bad_version =
            "stage21_test_sndl_version.sndl";
        auto data = original_sndl;
        patch_u16(data, 8, 2);
        refresh_sndl_header_crc(data);
        write_bytes(bad_version, data);
        expect_sndl_failure(bad_version, SndlErrorCode::UnsupportedVersion);
    }
    {
        const std::filesystem::path truncated_tail =
            "stage21_test_sndl_truncated_tail.sndl";
        write_bytes(truncated_tail, std::vector<std::uint8_t>(
            original_sndl.begin(), original_sndl.end() - 40));
        const auto result = SndlValidation::validate_file(truncated_tail);
        CHECK(!result.is_valid);
        std::error_code ec;
        std::filesystem::remove(truncated_tail, ec);
    }
    {
        const std::filesystem::path bad_payload =
            "stage21_test_sndl_block_crc.sndl";
        auto data = original_sndl;
        data[64 + 48 + 8] ^= 0xFFu;
        write_bytes(bad_payload, data);
        expect_sndl_failure(bad_payload, SndlErrorCode::BlockPayloadCrcMismatch);
    }
    {
        const std::filesystem::path bad_length =
            "stage21_test_sndl_block_length.sndl";
        auto data = original_sndl;
        CHECK(data.size() > 64 + 48);
        patch_u32(data, 64 + 12, 0xFFFFFF00u);
        refresh_sndl_block_header_crc(data, 64);
        write_bytes(bad_length, data);
        expect_sndl_failure(bad_length, SndlErrorCode::TruncatedBlock);
    }
    {
        const std::filesystem::path invalid_sequence =
            "stage21_test_sndl_sequence.sndl";
        std::vector<SndlRecord> records;
        auto first = make_counter_read_record(0, 0, 0, 0, 0x5701, 0, 24000000);
        first.sequence_num = 2;
        auto second = make_counter_read_record(0, 4, 1, 0, 0x5701, 100, 24000000);
        second.sequence_num = 1;
        records.push_back(first);
        records.push_back(second);
        create_sndl(invalid_sequence, generated.base_snapshot_hash, 2, records);
        expect_sndl_failure(invalid_sequence, SndlErrorCode::InvalidSequence);
    }

    // --- Association enforcement ---
    const std::filesystem::path mismatch =
        "stage21_test_sndl_association_mismatch.sndl";
    create_sndl(mismatch,
                generated.base_snapshot_hash ^ 0x5A5A5A5A5A5A5A5AULL,
                0);
    CHECK(SndlValidation::validate_file(mismatch).is_valid);
    const auto mismatch_report = ReplayDiagnostics::validate_artifacts(
        spec.wsnap_path, mismatch, true);
    CHECK(!mismatch_report.valid);
    CHECK(mismatch_report.association_checked);
    CHECK(!mismatch_report.association_match);

    {
        ReplayConfig config = make_config(0, 0, true);
        ReplaySession session;
        const auto result = session.prepare(spec.wsnap_path, mismatch, config);
        CHECK(result.is_failure());
        CHECK(result.code == ReplayErrorCode::SnapshotMismatch);

        ReplayConfig allowed = config;
        allowed.require_snapshot_association = false;
        const auto allowed_result = session.prepare(
            spec.wsnap_path, mismatch, allowed);
        CHECK(allowed_result.is_success());
        CHECK(session.state() == ReplayLifecycleState::Ready);
        session.close();
    }

    std::error_code ec;
    std::filesystem::remove(mismatch, ec);
    std::filesystem::remove(spec.wsnap_path, ec);
    std::filesystem::remove(spec.sndl_path, ec);

    std::cout << "  -> PASSED: large artifacts validated; all copy-only corruptions rejected with specific codes\n";
}

// -----------------------------------------------------------------------------
// Group F: Exact consumption, completion semantics and 10-run reproducibility
// -----------------------------------------------------------------------------
void test_group_f_completion_and_repeatability(
    const std::filesystem::path& snap,
    const std::filesystem::path& sndl
) {
    std::cout << "[Test Group F] Exact consumption, completion and 10-run reproducibility...\n";

    // Full consumption.
    {
        auto config = make_config(1000, 250, true);
        ReplayExecutionStats stats{};
        CHECK(ReplayEngine::run(snap, sndl, config, stats).is_success());
        CHECK(stats.events_consumed == 10);
        CHECK(stats.completion_status == ReplayCompletionStatus::CompletedNormally);

        ReplaySession session;
        CHECK(session.prepare(snap, sndl, config).is_success());
        CHECK(session.run().is_success());
        CHECK(session.cursor().total_consumed() == 10);
        CHECK(!session.cursor().has_next());
        session.close();
    }

    // Strict exact consumption rejects a prematurely stopped replay.
    {
        auto config = make_config(500, 250, true);
        ReplayExecutionStats stats{};
        const auto result = ReplayEngine::run(snap, sndl, config, stats);
        CHECK(result.is_failure());
        CHECK(result.code == ReplayErrorCode::Diverged);
        CHECK(stats.completion_status == ReplayCompletionStatus::UnconsumedEvents);
        CHECK(stats.diverged);
        CHECK(stats.events_consumed == 5);

        config.require_exact_event_consumption = false;
        CHECK(ReplayEngine::run(snap, sndl, config, stats).is_success());
        CHECK(stats.completion_status == ReplayCompletionStatus::UnconsumedEvents);
        CHECK(stats.events_consumed == 5);
    }

    // Configured instruction limit with a complete, event-free log.
    {
        const std::filesystem::path nop_snap = "stage21_test_nop.wsnap";
        const std::filesystem::path empty_log = "stage21_test_empty.sndl";
        const auto nop_hash = create_nop_snapshot(nop_snap);
        create_sndl(empty_log, nop_hash, 1000, {});
        auto config = make_config(10, 0, true);
        ReplayExecutionStats stats{};
        CHECK(ReplayEngine::run(nop_snap, empty_log, config, stats).is_success());
        CHECK(stats.instructions_executed == 10);
        CHECK(stats.events_consumed == 0);
        CHECK(stats.completion_status ==
              ReplayCompletionStatus::CompletedAtConfiguredInstructionLimit);
        std::error_code ec;
        std::filesystem::remove(nop_snap, ec);
        std::filesystem::remove(empty_log, ec);
    }

    // Ten runs, alternating profiling on/off, must be bit-identical.
    ReplayComponentHashes baseline{};
    for (int run = 1; run <= 10; ++run) {
        ReplayConfig config = make_config(1000, 250, true);
        config.enable_profiling = (run % 2) == 0;
        ReplayExecutionStats stats{};
        ReplayProfile profile{};
        CHECK(ReplayEngine::run(snap, sndl, config, stats,
                                config.enable_profiling ? &profile : nullptr)
                  .is_success());
        CHECK(stats.instructions_executed == 1000);
        CHECK(stats.events_consumed == 10);
        CHECK(stats.checkpoints_taken == 6);
        if (run == 1) {
            baseline = stats.final_state_hashes;
            CHECK(baseline.combined_hash != 0);
        } else {
            CHECK(stats.final_state_hashes == baseline);
        }
        if (config.enable_profiling) {
            CHECK(profile.enabled);
            CHECK(profile.instructions_executed == 1000);
            CHECK(profile.events_consumed == 10);
        }
    }

    // Checkpoint-free replay must land on the same architectural state.
    {
        ReplayExecutionStats stats{};
        CHECK(ReplayEngine::run(snap, sndl, make_config(1000, 0, true),
                                stats).is_success());
        CHECK(stats.final_state_hashes == baseline);
    }

    std::cout << "  -> PASSED: exact consumption, completion statuses and 10-run hashes are consistent\n";
}

// -----------------------------------------------------------------------------
// Group G: Session isolation and router lifecycle
// -----------------------------------------------------------------------------
void test_group_g_session_isolation(const std::filesystem::path& snap,
                                    const std::filesystem::path& sndl) {
    std::cout << "[Test Group G] Session isolation and router lifecycle...\n";

    CHECK(!is_replay_active());
    for (int i = 0; i < 3; ++i) {
        ReplayConfig config = make_config(1000, 500, true);
        config.enable_profiling = (i == 1);
        ReplaySession session;
        CHECK(session.prepare(snap, sndl, config).is_success());
        CHECK(session.state() == ReplayLifecycleState::Ready);
        CHECK(session.profile().enabled == config.enable_profiling);
        CHECK(session.run().is_success());
        CHECK(session.state() == ReplayLifecycleState::Completed);
        session.close();
        CHECK(!is_replay_active());
        CHECK(session.state() == ReplayLifecycleState::Idle);
    }

    // The following prepare must reset profiling state from the previous run.
    ReplaySession session;
    ReplayConfig plain = make_config(1000, 0, true);
    CHECK(session.prepare(snap, sndl, plain).is_success());
    CHECK(!session.profile().enabled);
    CHECK(session.profile().instructions_executed == 0);
    session.close();
    CHECK(!is_replay_active());

    std::cout << "  -> PASSED: sessions clean up global router state and reset profiles\n";
}

// -----------------------------------------------------------------------------
// Group H: SMP readiness remains NOT READY and rejected
// -----------------------------------------------------------------------------
void test_group_h_smp_rejection(const std::filesystem::path& snap,
                                const std::filesystem::path& sndl) {
    std::cout << "[Test Group H] SMP readiness and rejection enforcement...\n";

    ReplayConfig config = make_config(1000, 0, true);
    config.mode = ReplayMode::MultiCore;
    config.allow_experimental_smp = false;
    ReplayExecutionStats stats{};
    const auto result = ReplayEngine::run(snap, sndl, config, stats);
    CHECK(result.is_failure());
    CHECK(result.code == ReplayErrorCode::UnsupportedConfiguration);
    CHECK(stats.completion_status ==
          ReplayCompletionStatus::UnsupportedConfiguration);
    CHECK(result.message.find("Multi-core") != std::string::npos);

    const auto assessment = ReplayDiagnostics::assess_smp_readiness();
    CHECK(!assessment.ready);
    CHECK(assessment.verdict == "NOT READY");
    CHECK(assessment.blockers.size() >= 10);
    CHECK(assessment.prerequisites.size() >= 6);
    for (const auto& blocker : assessment.blockers) {
        CHECK(blocker.status == "OPEN");
        CHECK(!blocker.id.empty());
        CHECK(!blocker.detail.empty());
    }
    const auto text = ReplayDiagnostics::format_smp_readiness(assessment);
    CHECK(text.find("NOT READY") != std::string::npos);
    CHECK(text.find("SMP Replay Enabled:     no") != std::string::npos);

    std::cout << "  -> PASSED: multi-core replay rejected and readiness remains NOT READY\n";
}

// -----------------------------------------------------------------------------
// Group I: Memory reporting on an opt-in profile run
// -----------------------------------------------------------------------------
void test_group_i_memory_report(const std::filesystem::path& snap,
                                const std::filesystem::path& sndl) {
    std::cout << "[Test Group I] Memory and checkpoint reporting...\n";

    ReplayConfig config = make_config(1000, 0, true);
    const auto report = ReplayMemoryReporter::measure(
        snap, sndl, config, "stage21_test_memory");
    CHECK(report.replay_success);
    CHECK(report.replay_error == ReplayErrorCode::Success);
    CHECK(report.instructions_executed == 1000);
    CHECK(report.events_consumed == 10);
    CHECK(report.checkpoints_taken == 2);
    CHECK(report.memory_capacity == 128ULL * 1024ULL);
    CHECK(report.wsnap_file_size > report.memory_capacity);
    CHECK(report.sndl_file_size > 0);
    CHECK(report.elapsed_ms >= 0.0);

    CHECK(ReplayMemoryReporter::write_text(
        "stage21_test_memory.txt", report).is_success());
    CHECK(ReplayMemoryReporter::write_json(
        "stage21_test_memory.json", report).is_success());
    const auto text_bytes = read_bytes("stage21_test_memory.txt");
    const std::string text(text_bytes.begin(), text_bytes.end());
    CHECK(text.find("stage21_test_memory") != std::string::npos);
    CHECK(text.find("Guest Memory Capacity") != std::string::npos);
    const auto json_bytes = read_bytes("stage21_test_memory.json");
    const std::string json(json_bytes.begin(), json_bytes.end());
    CHECK(json.find("\"replay_success\": true") != std::string::npos);
    CHECK(json.find("\"checkpoints_taken\": 2") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove("stage21_test_memory.txt", ec);
    std::filesystem::remove("stage21_test_memory.json", ec);

    std::cout << "  -> PASSED: memory report captures artifact sizes, counters and process samples\n";
}

} // namespace

#include <crtdbg.h>

int main() {
#ifdef _WIN32
    _set_abort_behavior(0, _WRITE_ABORT_MSG);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "========================================================================\n";
    std::cout << "     WINIOS PHASE 52 STAGE 21 REPLAY SCALABILITY & EFFICIENCY TESTS     \n";
    std::cout << "========================================================================\n";

    const std::filesystem::path workload_snap = "stage21_test_workload.wsnap";
    const std::filesystem::path workload_sndl = "stage21_test_workload.sndl";

    std::error_code ec;
    std::filesystem::remove(workload_snap, ec);
    std::filesystem::remove(workload_sndl, ec);

    ReplayWorkloadSpec spec{};
    spec.instructions = 20000;
    spec.loop_instructions = 100;
    spec.counter_event_stride = 100;
    spec.memory_bytes = 1024ULL * 1024ULL;
    spec.wsnap_path = workload_snap;
    spec.sndl_path = workload_sndl;
    spec.description = "Stage 21 scalability test workload";
    const auto workload = generate_counter_loop_workload(spec);
    CHECK(workload.success);
    CHECK(workload.total_records == 200);
    CHECK(workload.base_snapshot_hash != 0);

    const std::filesystem::path small_snap = "stage21_test_small.wsnap";
    const std::filesystem::path small_sndl = "stage21_test_small.sndl";
    std::filesystem::remove(small_snap, ec);
    std::filesystem::remove(small_sndl, ec);

    ReplayWorkloadSpec small_spec{};
    small_spec.instructions = 1000;
    small_spec.loop_instructions = 100;
    small_spec.counter_event_stride = 100;
    small_spec.memory_bytes = 128ULL * 1024ULL;
    small_spec.wsnap_path = small_snap;
    small_spec.sndl_path = small_sndl;
    small_spec.description = "Stage 21 checkpoint and completion workload";
    const auto small = generate_counter_loop_workload(small_spec);
    CHECK(small.success);
    CHECK(small.total_records == 10);

    test_group_a_profiling_parity(workload_snap, workload_sndl);
    test_group_b_workload_determinism();
    test_group_c_checkpoint_intervals(small_snap, small_sndl);
    test_group_d_crc_reference();
    test_group_e_large_artifact_validation();
    test_group_f_completion_and_repeatability(small_snap, small_sndl);
    test_group_g_session_isolation(small_snap, small_sndl);
    test_group_h_smp_rejection(small_snap, small_sndl);
    test_group_i_memory_report(small_snap, small_sndl);

    std::filesystem::remove(workload_snap, ec);
    std::filesystem::remove(workload_sndl, ec);
    std::filesystem::remove(small_snap, ec);
    std::filesystem::remove(small_sndl, ec);

    std::cout << "========================================================================\n";
    std::cout << "      ALL 9 STAGE 21 REPLAY SCALABILITY TEST GROUPS PASSED!             \n";
    std::cout << "========================================================================\n";
    return 0;
}

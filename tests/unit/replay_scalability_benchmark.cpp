#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "replay/replay_diagnostics.h"
#include "replay/replay_engine.h"
#include "replay/replay_memory_report.h"
#include "replay/replay_profiler.h"
#include "replay/replay_session.h"
#include "replay/replay_state_hash.h"
#include "replay/replay_workload.h"
#include "sndl/sndl_validation.h"
#include "snapshot/snapshot_manager.h"
#include "trace/trace_serializer.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace winios::replay;

#define CHECK(expr)                                                           \
    do {                                                                      \
        if (!(expr)) {                                                        \
            std::cerr << "CHECK failed: " #expr " at " << __FILE__ << ":"     \
                      << __LINE__ << "\n";                                    \
            std::abort();                                                     \
        }                                                                     \
    } while (false)

std::ostringstream g_report;

void log_line(const std::string& line) {
    std::cout << line << "\n";
    g_report << line << "\n";
}

double elapsed_ms(std::chrono::steady_clock::time_point start,
                  std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const auto mid = values.size() / 2;
    if (values.size() % 2 == 1) {
        return values[mid];
    }
    return (values[mid - 1] + values[mid]) / 2.0;
}

ReplayConfig benchmark_config(std::uint64_t max_instructions,
                              std::uint64_t checkpoint_interval,
                              bool profiling = false) {
    ReplayConfig config{};
    config.max_instructions = max_instructions;
    config.checkpoint_interval = checkpoint_interval;
    config.require_exact_event_consumption = true;
    config.require_snapshot_association = true;
    config.enable_profiling = profiling;
    config.divergence_report_path.clear();
    return config;
}

struct MatrixRow {
    std::uint64_t instructions{0};
    std::uint64_t records{0};
    std::uint64_t wsnap_bytes{0};
    std::uint64_t sndl_bytes{0};
    double generation_ms{0.0};
    double wsnap_validation_ms{0.0};
    double sndl_validation_ms{0.0};
    double replay_ms{0.0};
    double mips{0.0};
    double event_rate{0.0};
    std::uint64_t checkpoints{0};
    std::uint64_t peak_working_set{0};
    std::uint64_t combined_hash{0};
};

struct CheckpointRow {
    std::uint64_t interval{0};
    std::uint64_t checkpoints{0};
    double replay_ms{0.0};
    double mips{0.0};
    double checkpoint_ms{0.0};
    double memory_hash_ms{0.0};
    double memory_hash_mb_s{0.0};
    std::uint64_t combined_hash{0};
};

struct MemoryRow {
    std::uint64_t memory_bytes{0};
    std::uint64_t wsnap_bytes{0};
    double replay_ms{0.0};
    double memory_hash_ms{0.0};
    double memory_hash_mb_s{0.0};
    std::uint64_t checkpoints{0};
    std::uint64_t peak_working_set{0};
    std::uint64_t combined_hash{0};
};

struct OverheadRow {
    double median_off_ms{0.0};
    double min_off_ms{0.0};
    double median_on_ms{0.0};
    double min_on_ms{0.0};
    double overhead_percent{0.0};
    std::uint64_t combined_hash{0};
};

std::filesystem::path artifact_path(const std::filesystem::path& dir,
                                    const std::string& stem,
                                    const char* extension) {
    return dir / (stem + extension);
}

// -----------------------------------------------------------------------------
// 1. Replay scalability matrix
// -----------------------------------------------------------------------------
std::vector<MatrixRow> run_scalability_matrix(
    const std::filesystem::path& dir,
    MatrixRow* out_last = nullptr
) {
    log_line("================================================================================");
    log_line("1. REPLAY SCALABILITY MATRIX (16 MB snapshot, checkpoint interval 1,000,000)");
    log_line("================================================================================");
    log_line("  instr        records   wsnap_MB  sndl_MB   gen_ms  snapval_ms sndlval_ms"
             "   replay_ms        MIPS   events/s   checkpoints   peak_MB");

    const std::vector<std::uint64_t> instruction_counts = {
        100000ULL, 1000000ULL, 10000000ULL, 50000000ULL, 100000000ULL
    };
    std::vector<MatrixRow> rows;
    rows.reserve(instruction_counts.size());

    for (const auto count : instruction_counts) {
        ReplayWorkloadSpec spec{};
        spec.instructions = count;
        spec.loop_instructions = 100;
        spec.counter_event_stride = 100;
        spec.memory_bytes = 16ULL * 1024ULL * 1024ULL;
        spec.pattern_memory_bytes = spec.memory_bytes;
        const std::string stem = "matrix_" + std::to_string(count);
        spec.wsnap_path = artifact_path(dir, stem, ".wsnap");
        spec.sndl_path = artifact_path(dir, stem, ".sndl");
        spec.description = "Stage 21 scalability matrix " + std::to_string(count);

        const auto gen_start = std::chrono::steady_clock::now();
        const auto generated = generate_counter_loop_workload(spec);
        const auto gen_end = std::chrono::steady_clock::now();
        CHECK(generated.success);

        const auto snap_start = std::chrono::steady_clock::now();
        const auto snapval = winios::snapshot::SnapshotManager::validate_file(
            spec.wsnap_path);
        const auto snap_end = std::chrono::steady_clock::now();
        CHECK(snapval.valid);

        const auto sndl_start = std::chrono::steady_clock::now();
        const auto sndlval = winios::sndl::SndlValidation::validate_file(
            spec.sndl_path);
        const auto sndl_end = std::chrono::steady_clock::now();
        CHECK(sndlval.is_valid);
        CHECK(sndlval.total_records == count / 100);

        const auto config = benchmark_config(count, 1000000, false);
        ReplayExecutionStats stats{};
        const auto start = std::chrono::steady_clock::now();
        const auto result = ReplayEngine::run(spec.wsnap_path, spec.sndl_path,
                                               config, stats);
        const auto end = std::chrono::steady_clock::now();
        CHECK(result.is_success());
        CHECK(stats.instructions_executed == count);
        CHECK(stats.events_consumed == count / 100);
        CHECK(stats.completion_status == ReplayCompletionStatus::CompletedNormally);

        MatrixRow row{};
        row.instructions = count;
        row.records = stats.events_consumed;
        row.wsnap_bytes = generated.wsnap_bytes;
        row.sndl_bytes = generated.sndl_bytes;
        row.generation_ms = elapsed_ms(gen_start, gen_end);
        row.wsnap_validation_ms = elapsed_ms(snap_start, snap_end);
        row.sndl_validation_ms = elapsed_ms(sndl_start, sndl_end);
        row.replay_ms = elapsed_ms(start, end);
        row.mips = (static_cast<double>(stats.instructions_executed) /
                    (row.replay_ms / 1000.0)) / 1e6;
        row.event_rate = (static_cast<double>(stats.events_consumed) /
                          (row.replay_ms / 1000.0)) / 1e6;
        row.checkpoints = stats.checkpoints_taken;
        row.peak_working_set = sample_process_memory().peak_working_set_bytes;
        row.combined_hash = stats.final_state_hashes.combined_hash;
        rows.push_back(row);

        std::ostringstream line;
        line << std::right << std::setw(10) << count
             << std::setw(13) << row.records
             << std::setw(11) << std::fixed << std::setprecision(2)
             << (static_cast<double>(row.wsnap_bytes) / (1024.0 * 1024.0))
             << std::setw(10) << (static_cast<double>(row.sndl_bytes) / (1024.0 * 1024.0))
             << std::setw(10) << std::setprecision(1) << row.generation_ms
             << std::setw(12) << std::setprecision(2) << row.wsnap_validation_ms
             << std::setw(11) << row.sndl_validation_ms
             << std::setw(12) << std::setprecision(1) << row.replay_ms
             << std::setw(12) << std::setprecision(2) << row.mips
             << std::setw(11) << row.event_rate
             << std::setw(14) << row.checkpoints
             << std::setw(10) << (row.peak_working_set / (1024ULL * 1024ULL));
        log_line(line.str());
    }
    log_line("");
    if (out_last != nullptr) {
        *out_last = rows.back();
    }
    return rows;
}

// -----------------------------------------------------------------------------
// 2. Checkpoint interval sweep
// -----------------------------------------------------------------------------
std::vector<CheckpointRow> run_checkpoint_sweep(
    const std::filesystem::path& dir
) {
    log_line("================================================================================");
    log_line("2. CHECKPOINT INTERVAL SWEEP (1,000,000 instructions, 16 MB snapshot)");
    log_line("================================================================================");
    log_line("  interval   checkpoints  replay_ms        MIPS   checkpoint_ms  memhash_ms"
             "  memhash_MB/s");

    ReplayWorkloadSpec spec{};
    spec.instructions = 1000000;
    spec.loop_instructions = 100;
    spec.counter_event_stride = 100;
    spec.memory_bytes = 16ULL * 1024ULL * 1024ULL;
    spec.pattern_memory_bytes = spec.memory_bytes;
    spec.wsnap_path = artifact_path(dir, "checkpoint_sweep", ".wsnap");
    spec.sndl_path = artifact_path(dir, "checkpoint_sweep", ".sndl");
    spec.description = "Stage 21 checkpoint interval sweep";
    CHECK(generate_counter_loop_workload(spec).success);

    const std::vector<std::uint64_t> intervals = {
        0ULL, 100000ULL, 250000ULL, 500000ULL, 1000000ULL, 2000000ULL
    };
    std::vector<CheckpointRow> rows;
    std::uint64_t baseline_hash = 0;

    for (const auto interval : intervals) {
        const auto config = benchmark_config(1000000, interval, true);
        ReplayExecutionStats stats{};
        ReplayProfile profile{};
        const auto start = std::chrono::steady_clock::now();
        const auto result = ReplayEngine::run(spec.wsnap_path, spec.sndl_path,
                                               config, stats, &profile);
        const auto end = std::chrono::steady_clock::now();
        CHECK(result.is_success());
        CHECK(stats.events_consumed == 10000);
        if (baseline_hash == 0) {
            baseline_hash = stats.final_state_hashes.combined_hash;
            CHECK(baseline_hash != 0);
        } else {
            CHECK(stats.final_state_hashes.combined_hash == baseline_hash);
        }

        CheckpointRow row{};
        row.interval = interval;
        row.checkpoints = stats.checkpoints_taken;
        row.replay_ms = elapsed_ms(start, end);
        row.mips = (static_cast<double>(stats.instructions_executed) /
                    (row.replay_ms / 1000.0)) / 1e6;
        row.checkpoint_ms = profile.checkpoint_total.total_ms;
        row.memory_hash_ms = profile.memory_hash.total_ms;
        row.memory_hash_mb_s =
            profile.memory_hash.total_ms > 0.0
                ? (static_cast<double>(profile.memory_bytes_hashed) /
                   (profile.memory_hash.total_ms / 1000.0)) / (1024.0 * 1024.0)
                : 0.0;
        row.combined_hash = stats.final_state_hashes.combined_hash;
        rows.push_back(row);

        std::ostringstream line;
        line << std::right << std::setw(10) << interval
             << std::setw(13) << row.checkpoints
             << std::setw(11) << std::fixed << std::setprecision(1) << row.replay_ms
             << std::setw(12) << std::setprecision(2) << row.mips
             << std::setw(15) << std::setprecision(1) << row.checkpoint_ms
             << std::setw(12) << row.memory_hash_ms
             << std::setw(14) << std::setprecision(2) << row.memory_hash_mb_s;
        log_line(line.str());
    }
    log_line("");
    return rows;
}

// -----------------------------------------------------------------------------
// 3. Opt-in profiling overhead
// -----------------------------------------------------------------------------
OverheadRow run_profiling_overhead(
    const std::filesystem::path& dir,
    const std::filesystem::path& matrix_1m_snap,
    const std::filesystem::path& matrix_1m_sndl
) {
    log_line("================================================================================");
    log_line("3. OPT-IN PROFILING OVERHEAD (1,000,000 instructions, interval 100,000)");
    log_line("================================================================================");

    const auto config_off = benchmark_config(1000000, 100000, false);
    const auto config_on = benchmark_config(1000000, 100000, true);

    ReplayExecutionStats warmup_stats{};
    CHECK(ReplayEngine::run(matrix_1m_snap, matrix_1m_sndl, config_off,
                            warmup_stats).is_success());

    std::vector<double> off_times;
    std::vector<double> on_times;
    std::uint64_t baseline_hash = 0;

    for (int i = 0; i < 5; ++i) {
        ReplayExecutionStats stats{};
        const auto start = std::chrono::steady_clock::now();
        CHECK(ReplayEngine::run(matrix_1m_snap, matrix_1m_sndl, config_off,
                                stats).is_success());
        const auto end = std::chrono::steady_clock::now();
        off_times.push_back(elapsed_ms(start, end));
        if (baseline_hash == 0) {
            baseline_hash = stats.final_state_hashes.combined_hash;
        } else {
            CHECK(stats.final_state_hashes.combined_hash == baseline_hash);
        }
    }

    ReplayProfile last_profile{};
    for (int i = 0; i < 5; ++i) {
        ReplayExecutionStats stats{};
        ReplayProfile profile{};
        const auto start = std::chrono::steady_clock::now();
        CHECK(ReplayEngine::run(matrix_1m_snap, matrix_1m_sndl, config_on,
                                stats, &profile).is_success());
        const auto end = std::chrono::steady_clock::now();
        on_times.push_back(elapsed_ms(start, end));
        CHECK(profile.enabled);
        CHECK(stats.final_state_hashes.combined_hash == baseline_hash);
        last_profile = profile;
    }

    OverheadRow row{};
    row.median_off_ms = median(off_times);
    row.min_off_ms = *std::min_element(off_times.begin(), off_times.end());
    row.median_on_ms = median(on_times);
    row.min_on_ms = *std::min_element(on_times.begin(), on_times.end());
    row.overhead_percent = row.median_off_ms > 0.0
        ? ((row.median_on_ms - row.median_off_ms) / row.median_off_ms) * 100.0
        : 0.0;
    row.combined_hash = baseline_hash;

    std::ostringstream summary;
    summary << "  Disabled: median " << std::fixed << std::setprecision(2)
            << row.median_off_ms << " ms, min " << row.min_off_ms << " ms\n"
            << "  Enabled:  median " << row.median_on_ms << " ms, min "
            << row.min_on_ms << " ms\n"
            << "  Overhead: " << std::setprecision(2) << row.overhead_percent
            << " %\n"
            << "  Final combined hash (equal in all 11 runs): 0x" << std::hex
            << row.combined_hash << std::dec;
    log_line(summary.str());

    const auto profile_json = dir / "profiling_overhead_profile.json";
    const auto profile_text = dir / "profiling_overhead_profile.txt";
    CHECK(ReplayProfiler::write_json(profile_json, last_profile).is_success());
    CHECK(ReplayProfiler::write_text(profile_text, last_profile).is_success());

    log_line("");
    log_line("  Phase breakdown for the last profiled run:");
    std::istringstream phase_text(ReplayProfiler::to_text(last_profile));
    std::string line;
    while (std::getline(phase_text, line)) {
        if (line.find("prepare_total") != std::string::npos ||
            line.find("run_total") != std::string::npos ||
            line.find("cpu_execution") != std::string::npos ||
            line.find("interrupt_injection") != std::string::npos ||
            line.find("input_routing") != std::string::npos ||
            line.find("checkpoint_total") != std::string::npos ||
            line.find("checkpoint_hash") != std::string::npos ||
            line.find("memory_hash") != std::string::npos ||
            line.find("final_hash") != std::string::npos ||
            line.find("completion_validation") != std::string::npos ||
            line.find("cursor_block_load") != std::string::npos ||
            line.find("cursor_block_crc") != std::string::npos ||
            line.find("cursor_block_decode") != std::string::npos) {
            log_line("  " + line);
        }
    }
    log_line("");
    return row;
}

// -----------------------------------------------------------------------------
// 4. Guest memory size sweep
// -----------------------------------------------------------------------------
std::vector<MemoryRow> run_memory_sweep(const std::filesystem::path& dir) {
    log_line("================================================================================");
    log_line("4. GUEST MEMORY SIZE SWEEP (100,000 instructions, interval 50,000)");
    log_line("================================================================================");
    log_line("  memory_MB   wsnap_MB  replay_ms  checkpoints  memhash_ms  memhash_MB/s"
             "  peak_MB");

    const std::vector<std::uint64_t> memory_sizes = {
        1ULL * 1024ULL * 1024ULL,
        4ULL * 1024ULL * 1024ULL,
        16ULL * 1024ULL * 1024ULL,
        64ULL * 1024ULL * 1024ULL
    };
    std::vector<MemoryRow> rows;
    for (const auto memory_bytes : memory_sizes) {
        ReplayWorkloadSpec spec{};
        spec.instructions = 100000;
        spec.loop_instructions = 100;
        spec.counter_event_stride = 100;
        spec.memory_bytes = memory_bytes;
        spec.pattern_memory_bytes = memory_bytes;
        const std::string stem = "memory_" +
            std::to_string(memory_bytes / (1024ULL * 1024ULL)) + "MB";
        spec.wsnap_path = artifact_path(dir, stem, ".wsnap");
        spec.sndl_path = artifact_path(dir, stem, ".sndl");
        spec.description = "Stage 21 memory sweep " + stem;
        const auto generated = generate_counter_loop_workload(spec);
        CHECK(generated.success);

        const auto config = benchmark_config(100000, 50000, true);
        ReplayExecutionStats stats{};
        ReplayProfile profile{};
        const auto start = std::chrono::steady_clock::now();
        const auto result = ReplayEngine::run(spec.wsnap_path, spec.sndl_path,
                                               config, stats, &profile);
        const auto end = std::chrono::steady_clock::now();
        CHECK(result.is_success());
        CHECK(stats.events_consumed == 1000);

        MemoryRow row{};
        row.memory_bytes = memory_bytes;
        row.wsnap_bytes = generated.wsnap_bytes;
        row.replay_ms = elapsed_ms(start, end);
        row.memory_hash_ms = profile.memory_hash.total_ms;
        row.memory_hash_mb_s =
            profile.memory_hash.total_ms > 0.0
                ? (static_cast<double>(profile.memory_bytes_hashed) /
                   (profile.memory_hash.total_ms / 1000.0)) / (1024.0 * 1024.0)
                : 0.0;
        row.checkpoints = stats.checkpoints_taken;
        row.peak_working_set = sample_process_memory().peak_working_set_bytes;
        row.combined_hash = stats.final_state_hashes.combined_hash;
        rows.push_back(row);

        std::ostringstream line;
        line << std::right << std::setw(10)
             << (memory_bytes / (1024ULL * 1024ULL))
             << std::setw(11) << std::fixed << std::setprecision(2)
             << (static_cast<double>(row.wsnap_bytes) / (1024.0 * 1024.0))
             << std::setw(11) << std::setprecision(1) << row.replay_ms
             << std::setw(13) << row.checkpoints
             << std::setw(12) << std::setprecision(2) << row.memory_hash_ms
             << std::setw(15) << row.memory_hash_mb_s
             << std::setw(10) << (row.peak_working_set / (1024ULL * 1024ULL));
        log_line(line.str());
    }
    log_line("");
    return rows;
}

// -----------------------------------------------------------------------------
// 5. CRC32C throughput and bitwise reference comparison
// -----------------------------------------------------------------------------
void run_crc_comparison(const std::filesystem::path& dir) {
    log_line("================================================================================");
    log_line("5. CRC32C THROUGHPUT (16 MB) - SLICING-BY-8 VS BITWISE REFERENCE");
    log_line("================================================================================");

    constexpr std::size_t kBytes = 16ULL * 1024ULL * 1024ULL;
    std::vector<std::uint8_t> data(kBytes);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::uint8_t>((i * 37u + (i >> 5) + 0xA5u) & 0xFFu);
    }

    auto reference_crc32c = [](const std::uint8_t* bytes, std::size_t length,
                               std::uint32_t initial = 0xFFFFFFFFu) {
        if (!bytes || length == 0) {
            return 0u;
        }
        std::uint32_t crc = initial;
        for (std::size_t i = 0; i < length; ++i) {
            crc ^= bytes[i];
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc >> 1) ^ ((crc & 1u) ? 0x82F63B78u : 0u);
            }
        }
        return crc ^ 0xFFFFFFFFu;
    };

    const auto ref_start = std::chrono::steady_clock::now();
    const auto ref_c1 = reference_crc32c(data.data(), data.size());
    const auto ref_c2 = reference_crc32c(data.data(), data.size(), ref_c1);
    const auto ref_end = std::chrono::steady_clock::now();
    const double ref_ms = elapsed_ms(ref_start, ref_end);
    const double ref_mb_s =
        (2.0 * static_cast<double>(data.size()) / (ref_ms / 1000.0)) /
        (1024.0 * 1024.0);

    std::uint32_t new_c1 = 0;
    std::uint32_t new_c2 = 0;
    constexpr int kRepetitions = 5;
    double new_ms = 0.0;
    for (int i = 0; i < kRepetitions; ++i) {
        const auto start = std::chrono::steady_clock::now();
        new_c1 = winios::trace::calculate_crc32c(data.data(), data.size());
        new_c2 = winios::trace::calculate_crc32c(data.data(), data.size(), new_c1);
        const auto end = std::chrono::steady_clock::now();
        new_ms += elapsed_ms(start, end);
    }
    new_ms /= kRepetitions;
    const double new_mb_s =
        (2.0 * static_cast<double>(data.size()) / (new_ms / 1000.0)) /
        (1024.0 * 1024.0);

    CHECK(new_c1 == ref_c1);
    CHECK(new_c2 == ref_c2);

    std::ostringstream text;
    text << "  Bitwise reference: " << std::fixed << std::setprecision(2)
         << ref_ms << " ms, " << ref_mb_s << " MB/s\n"
         << "  Slicing-by-8:      " << new_ms << " ms, " << new_mb_s << " MB/s\n"
         << "  Speedup:           " << std::setprecision(2)
         << (ref_ms / new_ms) << "x (double-hash chain, 32 MB scanned)\n"
         << "  CRC equality:      c1=0x" << std::hex << new_c1
         << ", c2=0x" << new_c2 << std::dec;
    log_line(text.str());

    std::ofstream out(dir / "crc32c_comparison.txt", std::ios::trunc);
    CHECK(out.is_open());
    out << "bitwise_ms=" << ref_ms << "\n"
        << "bitwise_mb_s=" << ref_mb_s << "\n"
        << "slicing8_ms=" << new_ms << "\n"
        << "slicing8_mb_s=" << new_mb_s << "\n"
        << "speedup=" << (ref_ms / new_ms) << "\n"
        << "crc1=" << new_c1 << "\n"
        << "crc2=" << new_c2 << "\n";
    log_line("");
}

// -----------------------------------------------------------------------------
// 6. Process memory report for the 1M-instruction workload
// -----------------------------------------------------------------------------
void run_memory_report(
    const std::filesystem::path& dir,
    const std::filesystem::path& snap,
    const std::filesystem::path& sndl
) {
    log_line("================================================================================");
    log_line("6. PROCESS MEMORY REPORT (1,000,000 instructions, interval 1,000,000)");
    log_line("================================================================================");

    const auto report = ReplayMemoryReporter::measure(
        snap, sndl, benchmark_config(1000000, 1000000, false),
        "matrix_1000000");
    CHECK(report.replay_success);
    CHECK(report.instructions_executed == 1000000);
    CHECK(report.events_consumed == 10000);

    std::ostringstream line;
    line << "  Replay: " << report.elapsed_ms << " ms, memory " << report.memory_capacity
         << " B, checkpoints " << report.checkpoints_taken << "\n"
         << "  Working set: before " << (report.before.working_set_bytes / (1024ULL * 1024ULL))
         << " MB, after " << (report.after.working_set_bytes / (1024ULL * 1024ULL))
         << " MB, peak " << (report.after.peak_working_set_bytes / (1024ULL * 1024ULL))
         << " MB\n"
         << "  Private: before " << (report.before.private_bytes / (1024ULL * 1024ULL))
         << " MB, after " << (report.after.private_bytes / (1024ULL * 1024ULL))
         << " MB, handles " << report.before.handle_count << " -> "
         << report.after.handle_count;
    log_line(line.str());

    CHECK(ReplayMemoryReporter::write_json(
        dir / "memory_report_1M.json", report).is_success());
    CHECK(ReplayMemoryReporter::write_text(
        dir / "memory_report_1M.txt", report).is_success());
    log_line("");
}

void write_json_summary(
    const std::filesystem::path& dir,
    const std::vector<MatrixRow>& matrix,
    const std::vector<CheckpointRow>& checkpoints,
    const OverheadRow& overhead,
    const std::vector<MemoryRow>& memory
) {
    std::ofstream out(dir / "benchmark_summary.json", std::ios::trunc);
    CHECK(out.is_open());
    out << std::fixed << std::setprecision(6);
    out << "{\n";
    out << "  \"stage\": 21,\n";
    out << "  \"platform\": \"Windows x64 (Visual Studio 18 2026)\",\n";
    out << "  \"matrix\": [\n";
    for (std::size_t i = 0; i < matrix.size(); ++i) {
        const auto& r = matrix[i];
        out << "    {\"instructions\": " << r.instructions
            << ", \"records\": " << r.records
            << ", \"wsnap_bytes\": " << r.wsnap_bytes
            << ", \"sndl_bytes\": " << r.sndl_bytes
            << ", \"generation_ms\": " << r.generation_ms
            << ", \"wsnap_validation_ms\": " << r.wsnap_validation_ms
            << ", \"sndl_validation_ms\": " << r.sndl_validation_ms
            << ", \"replay_ms\": " << r.replay_ms
            << ", \"mips\": " << r.mips
            << ", \"events_per_second_millions\": " << r.event_rate
            << ", \"checkpoints\": " << r.checkpoints
            << ", \"peak_working_set_bytes\": " << r.peak_working_set
            << ", \"combined_hash\": " << r.combined_hash << "}"
            << (i + 1 == matrix.size() ? "\n" : ",\n");
    }
    out << "  ],\n";
    out << "  \"checkpoint_sweep\": [\n";
    for (std::size_t i = 0; i < checkpoints.size(); ++i) {
        const auto& r = checkpoints[i];
        out << "    {\"interval\": " << r.interval
            << ", \"checkpoints\": " << r.checkpoints
            << ", \"replay_ms\": " << r.replay_ms
            << ", \"mips\": " << r.mips
            << ", \"checkpoint_ms\": " << r.checkpoint_ms
            << ", \"memory_hash_ms\": " << r.memory_hash_ms
            << ", \"memory_hash_mb_s\": " << r.memory_hash_mb_s
            << ", \"combined_hash\": " << r.combined_hash << "}"
            << (i + 1 == checkpoints.size() ? "\n" : ",\n");
    }
    out << "  ],\n";
    out << "  \"profiling_overhead\": {\n"
        << "    \"median_off_ms\": " << overhead.median_off_ms << ",\n"
        << "    \"min_off_ms\": " << overhead.min_off_ms << ",\n"
        << "    \"median_on_ms\": " << overhead.median_on_ms << ",\n"
        << "    \"min_on_ms\": " << overhead.min_on_ms << ",\n"
        << "    \"overhead_percent\": " << overhead.overhead_percent << ",\n"
        << "    \"combined_hash\": " << overhead.combined_hash << "\n"
        << "  },\n";
    out << "  \"memory_sweep\": [\n";
    for (std::size_t i = 0; i < memory.size(); ++i) {
        const auto& r = memory[i];
        out << "    {\"memory_bytes\": " << r.memory_bytes
            << ", \"wsnap_bytes\": " << r.wsnap_bytes
            << ", \"replay_ms\": " << r.replay_ms
            << ", \"memory_hash_ms\": " << r.memory_hash_ms
            << ", \"memory_hash_mb_s\": " << r.memory_hash_mb_s
            << ", \"checkpoints\": " << r.checkpoints
            << ", \"peak_working_set_bytes\": " << r.peak_working_set
            << ", \"combined_hash\": " << r.combined_hash << "}"
            << (i + 1 == memory.size() ? "\n" : ",\n");
    }
    out << "  ]\n";
    out << "}\n";
    CHECK(out.good());
}

} // namespace

int main(int argc, char* argv[]) {
    const std::filesystem::path output_dir =
        argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("stage21_benchmark");
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
    CHECK(!ec);

    std::cout << "========================================================================\n";
    std::cout << "       WINIOS PHASE 52 STAGE 21 REPLAY SCALABILITY BENCHMARKS           \n";
    std::cout << "========================================================================\n";
    std::cout << "Output directory: " << output_dir.string() << "\n\n";

    const auto matrix = run_scalability_matrix(output_dir);
    CHECK(matrix.size() == 5);
    CHECK(matrix.back().instructions == 100000000ULL);
    CHECK(matrix.back().records == 1000000ULL);

    const auto checkpoints = run_checkpoint_sweep(output_dir);
    CHECK(checkpoints.size() == 6);

    const auto matrix_1m_snap = artifact_path(output_dir, "matrix_1000000", ".wsnap");
    const auto matrix_1m_sndl = artifact_path(output_dir, "matrix_1000000", ".sndl");
    const auto overhead = run_profiling_overhead(
        output_dir, matrix_1m_snap, matrix_1m_sndl);

    const auto memory = run_memory_sweep(output_dir);
    CHECK(memory.size() == 4);

    run_crc_comparison(output_dir);
    run_memory_report(output_dir, matrix_1m_snap, matrix_1m_sndl);
    write_json_summary(output_dir, matrix, checkpoints, overhead, memory);

    std::ofstream report_out(output_dir / "benchmark_report.txt", std::ios::trunc);
    CHECK(report_out.is_open());
    report_out << g_report.str();
    CHECK(report_out.good());

    std::cout << "========================================================================\n";
    std::cout << "        STAGE 21 SCALABILITY BENCHMARKS COMPLETED SUCCESSFULLY         \n";
    std::cout << "========================================================================\n";
    return 0;
}

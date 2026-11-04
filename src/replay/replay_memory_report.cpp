#include "replay/replay_memory_report.h"

#include "replay/replay_session.h"

#include <fstream>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace winios::replay {

ProcessMemorySnapshot sample_process_memory() noexcept {
    ProcessMemorySnapshot sample{};
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (::GetProcessMemoryInfo(
            ::GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            sizeof(counters)) != FALSE) {
        sample.available = true;
        sample.working_set_bytes = static_cast<std::uint64_t>(counters.WorkingSetSize);
        sample.peak_working_set_bytes = static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
        sample.private_bytes = static_cast<std::uint64_t>(counters.PrivateUsage);
        sample.pagefile_usage_bytes = static_cast<std::uint64_t>(counters.PagefileUsage);
    }
    DWORD handles = 0;
    if (::GetProcessHandleCount(::GetCurrentProcess(), &handles) != FALSE) {
        sample.handle_count = static_cast<std::uint64_t>(handles);
    }
#endif
    return sample;
}

namespace {

std::uint64_t delta_or_zero(std::uint64_t after, std::uint64_t before) {
    return after >= before ? after - before : 0;
}

std::string memory_to_text(const ProcessMemorySnapshot& m) {
    std::ostringstream ss;
    if (!m.available) {
        ss << "n/a";
        return ss.str();
    }
    ss << m.working_set_bytes << " B working set, "
       << m.private_bytes << " B private, "
       << m.handle_count << " handles";
    return ss.str();
}

} // namespace

ReplayMemoryReport ReplayMemoryReporter::measure(
    const std::filesystem::path& wsnap_path,
    const std::filesystem::path& sndl_path,
    const ReplayConfig& config,
    const std::string& label
) {
    ReplayMemoryReport report{};
    report.label = label;
    report.checkpoint_interval = config.checkpoint_interval;
    report.before = sample_process_memory();

    std::error_code ec;
    const auto snap_size = std::filesystem::file_size(wsnap_path, ec);
    if (!ec) {
        report.wsnap_file_size = static_cast<std::uint64_t>(snap_size);
    }
    ec.clear();
    const auto sndl_size = std::filesystem::file_size(sndl_path, ec);
    if (!ec) {
        report.sndl_file_size = static_cast<std::uint64_t>(sndl_size);
    }

    ReplaySession session;
    const auto prep = session.prepare(wsnap_path, sndl_path, config);
    if (prep.is_failure()) {
        report.replay_error = prep.code;
        report.after = sample_process_memory();
        return report;
    }

    const auto run = session.run();
    report.replay_error = run.code;
    report.replay_success = run.is_success();
    report.completion_status = session.stats().completion_status;
    report.instructions_executed = session.stats().instructions_executed;
    report.events_consumed = session.stats().events_consumed;
    report.checkpoints_taken = session.stats().checkpoints_taken;
    report.elapsed_ms = session.stats().elapsed_ms;
    report.memory_capacity = session.memory() != nullptr
        ? static_cast<std::uint64_t>(session.memory()->size())
        : 0;
    report.after = sample_process_memory();
    return report;
}

std::string ReplayMemoryReporter::to_text(const ReplayMemoryReport& r) {
    std::ostringstream ss;
    ss << "================================================================================\n";
    ss << "                 WINIOS STAGE 21 REPLAY MEMORY REPORT                            \n";
    ss << "================================================================================\n";
    ss << "Label:                  " << r.label << "\n";
    ss << "Replay Result:          " << (r.replay_success ? "SUCCESS" : "FAILED")
       << " [" << replay_completion_status_name(r.completion_status) << "]\n";
    ss << "Instructions:           " << r.instructions_executed << "\n";
    ss << "Events Consumed:        " << r.events_consumed << "\n";
    ss << "Checkpoints / Interval: " << r.checkpoints_taken << " / " << r.checkpoint_interval << "\n";
    ss << "Elapsed:                " << std::fixed << std::setprecision(3) << r.elapsed_ms << " ms\n";
    ss << "Guest Memory Capacity:  " << r.memory_capacity << " bytes\n";
    ss << "WSNap File Size:        " << r.wsnap_file_size << " bytes\n";
    ss << "SNDL File Size:         " << r.sndl_file_size << " bytes\n";
    ss << "--------------------------------------------------------------------------------\n";
    ss << "Before: " << memory_to_text(r.before) << "\n";
    ss << "After:  " << memory_to_text(r.after) << "\n";
    ss << "Delta working set:      " << delta_or_zero(r.after.working_set_bytes, r.before.working_set_bytes) << " B\n";
    ss << "Delta private bytes:    " << delta_or_zero(r.after.private_bytes, r.before.private_bytes) << " B\n";
    ss << "Delta handles:          " << delta_or_zero(r.after.handle_count, r.before.handle_count) << "\n";
    ss << "Process peak working set (lifetime): " << r.after.peak_working_set_bytes << " B\n";
    ss << "================================================================================\n";
    return ss.str();
}

std::string ReplayMemoryReporter::to_json(const ReplayMemoryReport& r) {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"label\": \"" << r.label << "\",\n";
    ss << "  \"replay_success\": " << (r.replay_success ? "true" : "false") << ",\n";
    ss << "  \"completion_status\": \"" << replay_completion_status_name(r.completion_status) << "\",\n";
    ss << "  \"replay_error\": \"" << replay_error_name(r.replay_error) << "\",\n";
    ss << "  \"artifacts\": {\n";
    ss << "    \"memory_capacity\": " << r.memory_capacity << ",\n";
    ss << "    \"wsnap_file_size\": " << r.wsnap_file_size << ",\n";
    ss << "    \"sndl_file_size\": " << r.sndl_file_size << "\n";
    ss << "  },\n";
    ss << "  \"replay\": {\n";
    ss << "    \"instructions_executed\": " << r.instructions_executed << ",\n";
    ss << "    \"events_consumed\": " << r.events_consumed << ",\n";
    ss << "    \"checkpoints_taken\": " << r.checkpoints_taken << ",\n";
    ss << "    \"checkpoint_interval\": " << r.checkpoint_interval << ",\n";
    ss << "    \"elapsed_ms\": " << std::fixed << std::setprecision(3) << r.elapsed_ms << "\n";
    ss << "  },\n";
    ss << "  \"memory\": {\n";
    ss << "    \"before_working_set_bytes\": " << r.before.working_set_bytes << ",\n";
    ss << "    \"after_working_set_bytes\": " << r.after.working_set_bytes << ",\n";
    ss << "    \"delta_working_set_bytes\": " << delta_or_zero(r.after.working_set_bytes, r.before.working_set_bytes) << ",\n";
    ss << "    \"before_private_bytes\": " << r.before.private_bytes << ",\n";
    ss << "    \"after_private_bytes\": " << r.after.private_bytes << ",\n";
    ss << "    \"delta_private_bytes\": " << delta_or_zero(r.after.private_bytes, r.before.private_bytes) << ",\n";
    ss << "    \"process_peak_working_set_bytes\": " << r.after.peak_working_set_bytes << ",\n";
    ss << "    \"handles_before\": " << r.before.handle_count << ",\n";
    ss << "    \"handles_after\": " << r.after.handle_count << "\n";
    ss << "  }\n";
    ss << "}\n";
    return ss.str();
}

ReplayResult ReplayMemoryReporter::write_text(
    const std::filesystem::path& path,
    const ReplayMemoryReport& report
) {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return ReplayResult::fail(ReplayErrorCode::FileIOError,
                                  "Failed to open memory report output: " + path.string());
    }
    out << to_text(report);
    return out.good() ? ReplayResult::ok()
                      : ReplayResult::fail(ReplayErrorCode::FileIOError,
                                           "Failed to write memory report output");
}

ReplayResult ReplayMemoryReporter::write_json(
    const std::filesystem::path& path,
    const ReplayMemoryReport& report
) {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return ReplayResult::fail(ReplayErrorCode::FileIOError,
                                  "Failed to open memory report output: " + path.string());
    }
    out << to_json(report);
    return out.good() ? ReplayResult::ok()
                      : ReplayResult::fail(ReplayErrorCode::FileIOError,
                                           "Failed to write memory report output");
}

} // namespace winios::replay

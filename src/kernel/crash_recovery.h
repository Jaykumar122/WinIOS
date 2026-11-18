#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace winios::kernel {

enum class TerminationReason : uint32_t {
    NormalExit         = 0,
    ExplicitCancel     = 1,
    BudgetExhausted    = 2,
    GuestException     = 3,
    HostFailure        = 4,
    SecurityViolation  = 5
};

[[nodiscard]] constexpr const char* termination_reason_to_string(TerminationReason reason) noexcept {
    switch (reason) {
        case TerminationReason::NormalExit:        return "NormalExit";
        case TerminationReason::ExplicitCancel:    return "ExplicitCancel";
        case TerminationReason::BudgetExhausted:   return "BudgetExhausted";
        case TerminationReason::GuestException:    return "GuestException";
        case TerminationReason::HostFailure:       return "HostFailure";
        case TerminationReason::SecurityViolation: return "SecurityViolation";
        default:                                   return "Unknown";
    }
}

struct DetailedCrashReport {
    uint32_t          pid{0};
    std::string       bundle_id;
    TerminationReason reason{TerminationReason::NormalExit};
    uint64_t          fault_pc{0};
    uint64_t          timestamp_us{0};
    std::string       diagnostic_notes;
    bool              resources_reclaimed{true};
};

class CrashRecoveryManager {
public:
    static constexpr size_t   kMaxCrashHistory = 32;
    static constexpr uint32_t kMaxRestartAttempts = 3;
    static constexpr uint64_t kCrashLoopWindowUs = 10000000; // 10 seconds

    CrashRecoveryManager() = default;
    ~CrashRecoveryManager() = default;

    void record_termination(uint32_t pid,
                            const std::string& bundle_id,
                            TerminationReason reason,
                            uint64_t fault_pc,
                            uint64_t timestamp_us,
                            std::string notes = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        DetailedCrashReport report{};
        report.pid = pid;
        report.bundle_id = bundle_id;
        report.reason = reason;
        report.fault_pc = fault_pc;
        report.timestamp_us = timestamp_us;
        report.diagnostic_notes = std::move(notes);
        report.resources_reclaimed = true;

        crash_history_.push_back(report);
        while (crash_history_.size() > kMaxCrashHistory) {
            crash_history_.pop_front();
        }

        if (reason != TerminationReason::NormalExit && reason != TerminationReason::ExplicitCancel) {
            crash_timestamps_[bundle_id].push_back(timestamp_us);
            while (crash_timestamps_[bundle_id].size() > 10) {
                crash_timestamps_[bundle_id].pop_front();
            }
        }
    }

    [[nodiscard]] bool is_in_crash_loop(const std::string& bundle_id, uint64_t current_time_us) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = crash_timestamps_.find(bundle_id);
        if (it == crash_timestamps_.end() || it->second.size() < 3) {
            return false;
        }

        // Count crashes within the crash-loop window
        size_t recent = 0;
        for (auto ts : it->second) {
            if (current_time_us >= ts && (current_time_us - ts) <= kCrashLoopWindowUs) {
                ++recent;
            }
        }
        return recent >= 3;
    }

    bool should_allow_restart(const std::string& bundle_id, uint64_t current_time_us) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_in_crash_loop_locked(bundle_id, current_time_us)) {
            return false; // Crash-loop detected: prevent indefinite restart
        }
        if (restart_counts_[bundle_id] >= kMaxRestartAttempts) {
            return false; // Exceeded maximum retry attempts
        }
        ++restart_counts_[bundle_id];
        return true;
    }

    void reset_restart_counter(const std::string& bundle_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        restart_counts_[bundle_id] = 0;
        crash_timestamps_[bundle_id].clear();
    }

    [[nodiscard]] uint32_t get_restart_count(const std::string& bundle_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = restart_counts_.find(bundle_id);
        return it != restart_counts_.end() ? it->second : 0;
    }

    [[nodiscard]] size_t history_size() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return crash_history_.size();
    }

    [[nodiscard]] std::vector<DetailedCrashReport> get_history() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::vector<DetailedCrashReport>(crash_history_.begin(), crash_history_.end());
    }

private:
    [[nodiscard]] bool is_in_crash_loop_locked(const std::string& bundle_id, uint64_t current_time_us) const {
        auto it = crash_timestamps_.find(bundle_id);
        if (it == crash_timestamps_.end() || it->second.size() < 3) {
            return false;
        }
        size_t recent = 0;
        for (auto ts : it->second) {
            if (current_time_us >= ts && (current_time_us - ts) <= kCrashLoopWindowUs) {
                ++recent;
            }
        }
        return recent >= 3;
    }

    mutable std::mutex                             mutex_;
    std::deque<DetailedCrashReport>                crash_history_;
    std::map<std::string, std::deque<uint64_t>>    crash_timestamps_;
    std::map<std::string, uint32_t>                restart_counts_;
};

} // namespace winios::kernel

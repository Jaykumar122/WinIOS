#pragma once

#include "platform/platform_profiler.h"
#include "kernel/process_identity.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

namespace winios::platform {

struct AppProcessMetrics {
    uint32_t    pid{0};
    std::string bundle_id;
    uint64_t    launch_latency_us{0};
    uint64_t    instructions_retired{0};
    uint64_t    syscalls_dispatched{0};
    uint64_t    total_syscall_ticks{0};
    uint64_t    current_memory_bytes{0};
    uint64_t    peak_memory_bytes{0};
    uint64_t    windows_created{0};
    bool        is_active{true};

    [[nodiscard]] double average_syscall_latency_ticks() const noexcept {
        return syscalls_dispatched > 0
            ? static_cast<double>(total_syscall_ticks) / static_cast<double>(syscalls_dispatched)
            : 0.0;
    }
};

class AppPerformanceProfiler {
public:
    AppPerformanceProfiler() = default;
    ~AppPerformanceProfiler() = default;

    void register_process_launch(uint32_t pid, const std::string& bundle_id, uint64_t launch_latency_us) {
        std::lock_guard<std::mutex> lock(mutex_);
        AppProcessMetrics m{};
        m.pid = pid;
        m.bundle_id = bundle_id;
        m.launch_latency_us = launch_latency_us;
        m.is_active = true;
        metrics_[pid] = m;
    }

    void record_process_instructions(uint32_t pid, uint64_t instructions) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = metrics_.find(pid);
        if (it != metrics_.end()) {
            it->second.instructions_retired += instructions;
        }
        PlatformProfiler::instance().record_instruction_retirement(instructions);
    }

    void record_process_syscall(uint32_t pid, uint32_t sys_num, uint64_t duration_ticks = 1) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = metrics_.find(pid);
        if (it != metrics_.end()) {
            it->second.syscalls_dispatched++;
            it->second.total_syscall_ticks += duration_ticks;
        }
        PlatformProfiler::instance().record_syscall(sys_num, duration_ticks);
    }

    void record_process_memory_delta(uint32_t pid, int64_t delta_bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = metrics_.find(pid);
        if (it != metrics_.end()) {
            if (delta_bytes > 0) {
                it->second.current_memory_bytes += static_cast<uint64_t>(delta_bytes);
                if (it->second.current_memory_bytes > it->second.peak_memory_bytes) {
                    it->second.peak_memory_bytes = it->second.current_memory_bytes;
                }
            } else if (static_cast<uint64_t>(-delta_bytes) <= it->second.current_memory_bytes) {
                it->second.current_memory_bytes -= static_cast<uint64_t>(-delta_bytes);
            } else {
                it->second.current_memory_bytes = 0;
            }
        }
        PlatformProfiler::instance().record_memory_allocation(delta_bytes);
    }

    void record_process_window(uint32_t pid) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = metrics_.find(pid);
        if (it != metrics_.end()) {
            it->second.windows_created++;
        }
        PlatformProfiler::instance().record_window_presentation();
    }

    void mark_process_terminated(uint32_t pid) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = metrics_.find(pid);
        if (it != metrics_.end()) {
            it->second.is_active = false;
        }
    }

    [[nodiscard]] std::optional<AppProcessMetrics> get_process_metrics(uint32_t pid) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = metrics_.find(pid);
        if (it != metrics_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::vector<AppProcessMetrics> get_all_metrics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<AppProcessMetrics> result;
        result.reserve(metrics_.size());
        for (const auto& [_, m] : metrics_) {
            result.push_back(m);
        }
        return result;
    }

    std::string generate_developer_report(uint32_t pid) const {
        auto m_opt = get_process_metrics(pid);
        if (!m_opt.has_value()) {
            return "No performance data available for PID " + std::to_string(pid);
        }
        const auto& m = *m_opt;
        std::ostringstream ss;
        ss << "=== APPLICATION PERFORMANCE REPORT ===\n"
           << "Target PID:           " << m.pid << "\n"
           << "Bundle ID:            " << m.bundle_id << "\n"
           << "Status:               " << (m.is_active ? "RUNNING" : "TERMINATED") << "\n"
           << "Launch Latency:       " << m.launch_latency_us << " us\n"
           << "Instructions Retired: " << m.instructions_retired << "\n"
           << "Syscalls Dispatched:  " << m.syscalls_dispatched << "\n"
           << "Average Syscall Ticks:" << std::fixed << std::setprecision(2) << m.average_syscall_latency_ticks() << "\n"
           << "Current Memory:       " << m.current_memory_bytes << " bytes\n"
           << "Peak Memory:          " << m.peak_memory_bytes << " bytes\n"
           << "Windows Created:      " << m.windows_created << "\n"
           << "======================================\n";
        return ss.str();
    }

    std::string generate_system_summary() const {
        auto list = get_all_metrics();
        auto global_snap = PlatformProfiler::instance().take_snapshot();

        std::ostringstream ss;
        ss << "=== SYSTEM APPLICATION PROFILING SUMMARY ===\n"
           << "Active Apps Profiled: " << list.size() << "\n"
           << "Total Global Syscalls:" << global_snap.syscalls_dispatched << "\n"
           << "Total Instructions:   " << global_snap.instructions_retired << "\n"
           << "Total Global Memory:  " << global_snap.memory_allocated_bytes << " bytes\n"
           << "--------------------------------------------\n"
           << std::setw(6) << "PID" << " | "
           << std::setw(24) << "Bundle ID" << " | "
           << std::setw(12) << "Instructions" << " | "
           << std::setw(10) << "Syscalls" << " | "
           << std::setw(12) << "Peak Mem (B)" << "\n"
           << "--------------------------------------------\n";
        for (const auto& m : list) {
            ss << std::setw(6) << m.pid << " | "
               << std::setw(24) << m.bundle_id << " | "
               << std::setw(12) << m.instructions_retired << " | "
               << std::setw(10) << m.syscalls_dispatched << " | "
               << std::setw(12) << m.peak_memory_bytes << "\n";
        }
        ss << "============================================\n";
        return ss.str();
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        metrics_.clear();
        PlatformProfiler::instance().reset();
    }

private:
    mutable std::mutex                  mutex_;
    std::map<uint32_t, AppProcessMetrics> metrics_;
};

} // namespace winios::platform

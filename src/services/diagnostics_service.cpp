#include "services/diagnostics_service.h"
#include <chrono>

namespace winios::services {

DiagnosticsService::DiagnosticsService(kernel::AppSupervisor* supervisor,
                                       packaging::ApplicationRegistry* registry,
                                       graphics::WindowManager* wm,
                                       security::CapabilityManager* cap_mgr)
    : supervisor_(supervisor)
    , registry_(registry)
    , wm_(wm)
    , cap_mgr_(cap_mgr) {}

void DiagnosticsService::log(const std::string& bundle_id, std::uint32_t pid, LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> guard(lock_);
    LogEntry entry{};
    entry.sequence = ++sequence_counter_;
    entry.timestamp_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    entry.bundle_id = bundle_id;
    entry.pid = pid;
    entry.level = level;
    entry.message = message;

    log_ring_.push_back(std::move(entry));
    while (log_ring_.size() > kMaxLogCapacity) {
        log_ring_.pop_front();
    }
}

std::vector<LogEntry> DiagnosticsService::get_logs(std::size_t max_entries, LogLevel min_level) const {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<LogEntry> res;
    for (auto it = log_ring_.rbegin(); it != log_ring_.rend() && res.size() < max_entries; ++it) {
        if (static_cast<std::uint32_t>(it->level) >= static_cast<std::uint32_t>(min_level)) {
            res.push_back(*it);
        }
    }
    return res;
}

std::size_t DiagnosticsService::total_log_count() const {
    std::lock_guard<std::mutex> guard(lock_);
    return sequence_counter_;
}

void DiagnosticsService::clear_logs() {
    std::lock_guard<std::mutex> guard(lock_);
    log_ring_.clear();
}

PlatformDiagnosticsSummary DiagnosticsService::get_summary() const {
    PlatformDiagnosticsSummary s{};
    if (supervisor_) {
        s.active_processes = supervisor_->running_count();
        s.total_crashes = supervisor_->total_crashes();
    }
    if (registry_) {
        s.installed_apps = registry_->app_count(false);
    }
    if (wm_) {
        s.open_windows = wm_->window_count();
    }
    s.ipc_ports = runtime::GuestIpcRouter::Get().GetPortCount();
    if (cap_mgr_) {
        s.security_stats = cap_mgr_->get_audit_stats();
    }
    return s;
}

} // namespace winios::services

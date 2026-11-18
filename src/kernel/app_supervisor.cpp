#include "kernel/app_supervisor.h"
#include <chrono>

namespace winios::kernel {

AppSupervisor::AppSupervisor(AppLifecycleManager* lifecycle_mgr,
                             security::CapabilityManager* cap_mgr,
                             packaging::ApplicationRegistry* registry,
                             graphics::WindowManager* wm,
                             graphics::Compositor* comp)
    : lifecycle_mgr_(lifecycle_mgr)
    , cap_mgr_(cap_mgr)
    , registry_(registry)
    , wm_(wm)
    , comp_(comp) {}

void AppSupervisor::register_process(std::uint32_t pid, const std::string& bundle_id) {
    if (pid == 0) return;
    std::lock_guard<std::mutex> guard(lock_);
    pid_to_bundle_[pid] = bundle_id;

    if (cap_mgr_) {
        cap_mgr_->bind_identity(pid, bundle_id);
    }
    if (lifecycle_mgr_) {
        lifecycle_mgr_->register_app(pid, bundle_id);
    }
}

void AppSupervisor::unregister_process(std::uint32_t pid) {
    std::lock_guard<std::mutex> guard(lock_);
    pid_to_bundle_.erase(pid);

    if (cap_mgr_) {
        cap_mgr_->unbind_identity(pid);
    }
}

void AppSupervisor::cleanup_process_resources(std::uint32_t pid) {
    // 1. Clean up IPC ports and pending queues
    runtime::GuestIpcRouter::Get().CleanupProcess(pid);

    // 2. Terminate lifecycle state
    if (lifecycle_mgr_) {
        lifecycle_mgr_->terminate_app(pid);
    }

    // 3. Remove windows owned by this PID
    if (wm_) {
        wm_->cleanup_process_windows(pid);
    }

    // 4. Invalidate compositor damage
    if (comp_) {
        comp_->invalidate_full_frame();
    }
}

bool AppSupervisor::request_termination(std::uint32_t pid) {
    std::string bundle;
    {
        std::lock_guard<std::mutex> guard(lock_);
        auto it = pid_to_bundle_.find(pid);
        if (it == pid_to_bundle_.end()) return false;
        bundle = it->second;
    }

    cleanup_process_resources(pid);
    unregister_process(pid);
    return true;
}

bool AppSupervisor::force_kill(std::uint32_t pid, int exit_code) {
    record_crash(pid, CrashCategory::Abort, exit_code);
    return request_termination(pid);
}

void AppSupervisor::record_crash(std::uint32_t pid,
                                 CrashCategory category,
                                 int exit_code,
                                 std::uint64_t pc,
                                 std::uint64_t retired_instructions) {
    std::string bundle;
    std::string state_str = "Unknown";
    {
        std::lock_guard<std::mutex> guard(lock_);
        auto it = pid_to_bundle_.find(pid);
        if (it != pid_to_bundle_.end()) {
            bundle = it->second;
        }
        if (lifecycle_mgr_) {
            state_str = AppLifecycleManager::state_to_string(lifecycle_mgr_->get_app_state(pid));
        }

        CrashRecord rec{};
        rec.pid = pid;
        rec.bundle_id = bundle;
        rec.exit_code = exit_code;
        rec.category = category;
        rec.guest_pc = pc;
        rec.instructions_retired = retired_instructions;
        rec.timestamp_us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        rec.last_lifecycle_state = state_str;
        rec.resources_cleaned_up = true;

        total_crash_count_++;
        crash_log_.push_back(std::move(rec));
        while (crash_log_.size() > kMaxCrashLogEntries) {
            crash_log_.pop_front();
        }
    }

    cleanup_process_resources(pid);
}

std::vector<CrashRecord> AppSupervisor::get_recent_crashes(std::size_t max_count) const {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<CrashRecord> res;
    const std::size_t count = (max_count < crash_log_.size()) ? max_count : crash_log_.size();
    auto it = crash_log_.rbegin();
    for (std::size_t i = 0; i < count && it != crash_log_.rend(); ++i, ++it) {
        res.push_back(*it);
    }
    return res;
}

std::size_t AppSupervisor::total_crashes() const {
    std::lock_guard<std::mutex> guard(lock_);
    return total_crash_count_;
}

void AppSupervisor::clear_crashes() {
    std::lock_guard<std::mutex> guard(lock_);
    crash_log_.clear();
}

bool AppSupervisor::is_running(const std::string& bundle_id) const {
    std::lock_guard<std::mutex> guard(lock_);
    for (const auto& [_, b] : pid_to_bundle_) {
        if (b == bundle_id) return true;
    }
    return false;
}

std::optional<std::uint32_t> AppSupervisor::find_pid(const std::string& bundle_id) const {
    std::lock_guard<std::mutex> guard(lock_);
    for (const auto& [pid, b] : pid_to_bundle_) {
        if (b == bundle_id) return pid;
    }
    return std::nullopt;
}

std::size_t AppSupervisor::running_count() const {
    std::lock_guard<std::mutex> guard(lock_);
    return pid_to_bundle_.size();
}

} // namespace winios::kernel

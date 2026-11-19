#pragma once

#include "loader/app_launcher.h"
#include "kernel/process_identity.h"
#include "packaging/app_registry.h"
#include "settings/task_manager.h"
#include "services/diagnostics_service.h"

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <deque>
#include <optional>

namespace winios::session {

enum class AppSessionState {
    Foreground,
    Background,
    Suspended,
    Terminated
};

inline const char* app_session_state_to_string(AppSessionState s) noexcept {
    switch (s) {
    case AppSessionState::Foreground: return "Foreground";
    case AppSessionState::Background: return "Background";
    case AppSessionState::Suspended: return "Suspended";
    case AppSessionState::Terminated: return "Terminated";
    default: return "Unknown";
    }
}

struct AppSessionEntry {
    kernel::ProcessHandle   handle;
    std::string             bundle_id;
    AppSessionState         state{AppSessionState::Background};
    uint64_t                primary_window{0};
    uint64_t                last_focused_time_us{0};
    std::vector<uint8_t>    saved_state_snapshot;
};

class MultiAppSessionController {
public:
    static constexpr size_t kDefaultMaxActiveApps = 4;

    MultiAppSessionController(std::shared_ptr<loader::AppLaunchPipeline> launcher,
                              std::shared_ptr<kernel::ProcessIdentityManager> identity_mgr,
                              std::shared_ptr<packaging::ApplicationRegistry> registry,
                              std::shared_ptr<settings::TaskManager> task_mgr,
                              std::shared_ptr<services::DiagnosticsService> diag_svc,
                              size_t max_active_apps = kDefaultMaxActiveApps)
        : launcher_(std::move(launcher)),
          identity_mgr_(std::move(identity_mgr)),
          registry_(std::move(registry)),
          task_mgr_(std::move(task_mgr)),
          diag_svc_(std::move(diag_svc)),
          max_active_apps_(max_active_apps) {}

    ~MultiAppSessionController() = default;

    // Launch an application into the session and bring it to foreground
    loader::LaunchResult launch_app(const std::string& bundle_id) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (!launcher_) {
            loader::LaunchResult r{};
            r.status = loader::LaunchStatus::ProcessAllocationFailed;
            r.rejection_reason = "No launcher configured";
            return r;
        }

        // Check active app capacity; if full, suspend oldest background app
        enforce_capacity_limit();

        auto res = launcher_->launch(bundle_id);
        if (res.status != loader::LaunchStatus::Success) {
            return res;
        }

        // Put current foreground app into background
        if (foreground_pid_ != 0 && foreground_pid_ != res.handle.pid) {
            transition_app_state(foreground_pid_, AppSessionState::Background);
        }

        AppSessionEntry entry{};
        entry.handle = res.handle;
        entry.bundle_id = bundle_id;
        entry.state = AppSessionState::Foreground;
        entry.last_focused_time_us = ++time_counter_;
        session_apps_[res.handle.pid] = entry;

        foreground_pid_ = res.handle.pid;

        if (task_mgr_) {
            task_mgr_->update_task_state(res.handle.pid, settings::TaskState::Running);
            task_mgr_->set_foreground_task(res.handle.pid);
        }

        if (diag_svc_) {
            diag_svc_->log(bundle_id, res.handle.pid, services::LogLevel::Info,
                           "Session: App brought to foreground");
        }

        return res;
    }

    // Switch active foreground application
    bool switch_to_app(uint32_t pid) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = session_apps_.find(pid);
        if (it == session_apps_.end() || it->second.state == AppSessionState::Terminated) {
            return false;
        }

        if (foreground_pid_ == pid) {
            return true; // Already foreground
        }

        // Transition current foreground to background
        if (foreground_pid_ != 0) {
            transition_app_state(foreground_pid_, AppSessionState::Background);
        }

        // Transition target to foreground
        transition_app_state(pid, AppSessionState::Foreground);
        it->second.last_focused_time_us = ++time_counter_;
        foreground_pid_ = pid;

        if (task_mgr_) {
            task_mgr_->update_task_state(pid, settings::TaskState::Running);
            task_mgr_->set_foreground_task(pid);
        }

        if (diag_svc_) {
            diag_svc_->log(it->second.bundle_id, pid, services::LogLevel::Info,
                           "Session: Switched to foreground");
        }

        return true;
    }

    // Suspend an application
    bool suspend_app(uint32_t pid, const std::vector<uint8_t>& state_snapshot = {}) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = session_apps_.find(pid);
        if (it == session_apps_.end() || it->second.state == AppSessionState::Terminated) {
            return false;
        }

        transition_app_state(pid, AppSessionState::Suspended);
        if (!state_snapshot.empty()) {
            it->second.saved_state_snapshot = state_snapshot;
        }

        if (foreground_pid_ == pid) {
            foreground_pid_ = 0;
            // Promote next most recently used background app to foreground
            promote_mru_background();
        }

        if (task_mgr_) {
            task_mgr_->update_task_state(pid, settings::TaskState::Suspended);
        }

        return true;
    }

    // Resume a suspended application
    bool resume_app(uint32_t pid) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = session_apps_.find(pid);
        if (it == session_apps_.end() || it->second.state != AppSessionState::Suspended) {
            return false;
        }

        enforce_capacity_limit();
        return switch_to_app(pid);
    }

    // Terminate an application in session
    bool terminate_app(uint32_t pid) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = session_apps_.find(pid);
        if (it == session_apps_.end()) {
            return false;
        }

        if (launcher_) {
            launcher_->terminate(it->second.handle);
        }

        it->second.state = AppSessionState::Terminated;

        if (foreground_pid_ == pid) {
            foreground_pid_ = 0;
            promote_mru_background();
        }

        session_apps_.erase(it);
        return true;
    }

    [[nodiscard]] uint32_t foreground_pid() const noexcept {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return foreground_pid_;
    }

    [[nodiscard]] std::vector<AppSessionEntry> list_session_apps() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        std::vector<AppSessionEntry> result;
        result.reserve(session_apps_.size());
        for (const auto& [_, e] : session_apps_) {
            result.push_back(e);
        }
        return result;
    }

    [[nodiscard]] std::optional<AppSessionEntry> get_session_entry(uint32_t pid) const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = session_apps_.find(pid);
        if (it != session_apps_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

private:
    void transition_app_state(uint32_t pid, AppSessionState new_state) {
        auto it = session_apps_.find(pid);
        if (it != session_apps_.end()) {
            it->second.state = new_state;
            if (task_mgr_) {
                if (new_state == AppSessionState::Foreground) {
                    task_mgr_->update_task_state(pid, settings::TaskState::Running);
                    task_mgr_->set_foreground_task(pid);
                } else if (new_state == AppSessionState::Background) {
                    task_mgr_->update_task_state(pid, settings::TaskState::Running);
                } else if (new_state == AppSessionState::Suspended) {
                    task_mgr_->update_task_state(pid, settings::TaskState::Suspended);
                }
            }
        }
    }

    void promote_mru_background() {
        uint32_t best_pid = 0;
        uint64_t max_time = 0;
        for (const auto& [pid, entry] : session_apps_) {
            if (entry.state == AppSessionState::Background) {
                if (entry.last_focused_time_us >= max_time) {
                    max_time = entry.last_focused_time_us;
                    best_pid = pid;
                }
            }
        }
        if (best_pid != 0) {
            transition_app_state(best_pid, AppSessionState::Foreground);
            foreground_pid_ = best_pid;
        }
    }

    void enforce_capacity_limit() {
        size_t active_count = 0;
        for (const auto& [_, e] : session_apps_) {
            if (e.state == AppSessionState::Foreground || e.state == AppSessionState::Background) {
                active_count++;
            }
        }

        if (active_count >= max_active_apps_) {
            // Find least recently used background app to suspend
            uint32_t lru_pid = 0;
            uint64_t min_time = UINT64_MAX;
            for (const auto& [pid, entry] : session_apps_) {
                if (entry.state == AppSessionState::Background) {
                    if (entry.last_focused_time_us < min_time) {
                        min_time = entry.last_focused_time_us;
                        lru_pid = pid;
                    }
                }
            }
            if (lru_pid != 0) {
                transition_app_state(lru_pid, AppSessionState::Suspended);
                if (diag_svc_) {
                    diag_svc_->log(session_apps_[lru_pid].bundle_id, lru_pid, services::LogLevel::Info,
                                   "Session: LRU suspended due to capacity limit");
                }
            }
        }
    }

    std::shared_ptr<loader::AppLaunchPipeline>      launcher_;
    std::shared_ptr<kernel::ProcessIdentityManager> identity_mgr_;
    std::shared_ptr<packaging::ApplicationRegistry> registry_;
    std::shared_ptr<settings::TaskManager>          task_mgr_;
    std::shared_ptr<services::DiagnosticsService>   diag_svc_;
    size_t                                          max_active_apps_{kDefaultMaxActiveApps};

    mutable std::recursive_mutex                    mutex_;
    uint32_t                                        foreground_pid_{0};
    uint64_t                                        time_counter_{0};
    std::map<uint32_t, AppSessionEntry>             session_apps_;
};

} // namespace winios::session

#include "kernel/app_lifecycle_manager.h"
#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "graphics/compositor.h"
#include "input/input_router.h"
#include "runtime/ui/theme.h"

namespace winios::kernel {

AppLifecycleManager::AppLifecycleManager(
    graphics::WindowManager* wm,
    graphics::SurfaceResourceManager* srm,
    graphics::Compositor* comp,
    input::InputRouter* ir)
    : wm_(wm), srm_(srm), compositor_(comp), input_router_(ir) {}

bool AppLifecycleManager::is_valid_transition(AppState from, AppState to) noexcept {
    if (from == to) return true;

    switch (from) {
    case AppState::Created:
        return (to == AppState::Starting || to == AppState::Terminating);
    case AppState::Starting:
        return (to == AppState::Foreground || to == AppState::Background || to == AppState::Terminating);
    case AppState::Foreground:
        return (to == AppState::Background || to == AppState::Terminating);
    case AppState::Background:
        return (to == AppState::Foreground || to == AppState::Suspended || to == AppState::Terminating);
    case AppState::Suspended:
        return (to == AppState::Resuming || to == AppState::Terminating);
    case AppState::Resuming:
        return (to == AppState::Foreground || to == AppState::Terminating);
    case AppState::Terminating:
        return (to == AppState::Terminated);
    case AppState::Terminated:
        return false; // Terminal state cannot transition anywhere
    default:
        return false;
    }
}

const char* AppLifecycleManager::state_to_string(AppState state) noexcept {
    switch (state) {
    case AppState::Created: return "Created";
    case AppState::Starting: return "Starting";
    case AppState::Foreground: return "Foreground";
    case AppState::Background: return "Background";
    case AppState::Suspended: return "Suspended";
    case AppState::Resuming: return "Resuming";
    case AppState::Terminating: return "Terminating";
    case AppState::Terminated: return "Terminated";
    default: return "Unknown";
    }
}

bool AppLifecycleManager::register_app(std::uint32_t pid, const std::string& app_id, std::uint64_t timestamp_us) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (apps_.find(pid) != apps_.end()) {
        return false; // Already registered
    }

    AppLifecycleRecord rec;
    rec.pid = pid;
    rec.app_id = app_id;
    rec.state = AppState::Created;
    rec.launch_timestamp_us = timestamp_us;
    rec.last_transition_timestamp_us = timestamp_us;
    apps_[pid] = rec;
    return true;
}

bool AppLifecycleManager::transition_to(std::uint32_t pid, AppState target_state, std::uint64_t timestamp_us) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = apps_.find(pid);
    if (it == apps_.end()) return false;

    AppState current = it->second.state;
    if (!is_valid_transition(current, target_state)) {
        return false; // Invalid transition
    }

    it->second.state = target_state;
    it->second.last_transition_timestamp_us = timestamp_us;

    if (target_state == AppState::Foreground) {
        foreground_pid_ = pid;
    } else if (foreground_pid_ == pid && target_state != AppState::Foreground) {
        foreground_pid_ = 0;
    }

    if (callback_) {
        callback_(pid, current, target_state);
    }
    return true;
}

AppState AppLifecycleManager::get_app_state(std::uint32_t pid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = apps_.find(pid);
    if (it != apps_.end()) {
        return it->second.state;
    }
    return AppState::Terminated;
}

bool AppLifecycleManager::is_registered(std::uint32_t pid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return apps_.find(pid) != apps_.end();
}

void AppLifecycleManager::associate_window(std::uint32_t pid, std::uint32_t window_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = apps_.find(pid);
    if (it != apps_.end()) {
        it->second.window_ids.push_back(window_id);
    }
}

void AppLifecycleManager::associate_surface(std::uint32_t pid, std::uint32_t surface_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = apps_.find(pid);
    if (it != apps_.end()) {
        it->second.surface_ids.push_back(surface_id);
    }
}

bool AppLifecycleManager::launch_app(std::uint32_t pid, const std::string& app_id, std::uint64_t timestamp_us) {
    if (!register_app(pid, app_id, timestamp_us)) return false;
    if (!transition_to(pid, AppState::Starting, timestamp_us)) return false;
    return transition_to(pid, AppState::Foreground, timestamp_us);
}

bool AppLifecycleManager::bring_to_foreground(std::uint32_t pid, std::uint64_t timestamp_us) {
    return transition_to(pid, AppState::Foreground, timestamp_us);
}

bool AppLifecycleManager::send_to_background(std::uint32_t pid, std::uint64_t timestamp_us) {
    return transition_to(pid, AppState::Background, timestamp_us);
}

bool AppLifecycleManager::suspend_app(std::uint32_t pid, std::uint64_t timestamp_us) {
    return transition_to(pid, AppState::Suspended, timestamp_us);
}

bool AppLifecycleManager::resume_app(std::uint32_t pid, std::uint64_t timestamp_us) {
    if (!transition_to(pid, AppState::Resuming, timestamp_us)) return false;
    return transition_to(pid, AppState::Foreground, timestamp_us);
}

bool AppLifecycleManager::terminate_app(std::uint32_t pid, std::uint64_t timestamp_us) {
    if (!transition_to(pid, AppState::Terminating, timestamp_us)) return false;

    // Perform complete resource teardown
    std::vector<std::uint32_t> wins_to_destroy;
    std::vector<std::uint32_t> surfs_to_destroy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = apps_.find(pid);
        if (it != apps_.end()) {
            wins_to_destroy = it->second.window_ids;
            surfs_to_destroy = it->second.surface_ids;
        }
    }

    // 1. Destroy windows
    if (wm_) {
        for (std::uint32_t wid : wins_to_destroy) {
            if (input_router_) {
                input_router_->on_window_destroyed(wid);
            }
            wm_->destroy_window(pid, wid);
        }
    }

    // 2. Destroy surfaces
    if (srm_) {
        for (std::uint32_t sid : surfs_to_destroy) {
            if (compositor_) {
                compositor_->remove_surface(sid);
            }
            srm_->destroy_surface(pid, sid);
        }
    }

    // 3. Trigger compositor pass to restore previous surface/shell
    if (compositor_) {
        compositor_->composite(false);
    }

    // 4. Release UI theme resources
    runtime::ui::ThemeManager::instance().release_resources_for_pid(pid);

    // 5. Restore focus
    if (wm_ && wm_->window_count() > 0) {
        auto wins = wm_->get_windows_for_process(1);
        if (!wins.empty() && wins[0]) {
            wm_->set_focus(wins[0]->id);
        }
    }

    return transition_to(pid, AppState::Terminated, timestamp_us);
}

std::uint32_t AppLifecycleManager::active_app_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint32_t count = 0;
    for (const auto& kv : apps_) {
        if (kv.second.state != AppState::Terminated) {
            ++count;
        }
    }
    return count;
}

} // namespace winios::kernel

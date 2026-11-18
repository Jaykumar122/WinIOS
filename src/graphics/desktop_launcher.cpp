#include "graphics/desktop_launcher.h"

namespace winios::graphics {

DesktopLauncher::DesktopLauncher(packaging::ApplicationRegistry* registry,
                                 kernel::AppSupervisor* supervisor,
                                 WindowManager* wm)
    : registry_(registry)
    , supervisor_(supervisor)
    , wm_(wm) {
    refresh();
}

void DesktopLauncher::refresh() {
    std::lock_guard<std::mutex> guard(lock_);
    items_.clear();
    last_error_.clear();

    if (!registry_) return;

    auto apps = registry_->list_apps(true); // Active apps only
    for (const auto& app : apps) {
        LauncherItem item{};
        item.bundle_id = app.bundle_id;
        item.display_name = app.name;
        item.version = app.version;
        item.icon_path = app.icon_path;

        if (supervisor_) {
            item.is_running = supervisor_->is_running(app.bundle_id);
            if (item.is_running) {
                auto pid_opt = supervisor_->find_pid(app.bundle_id);
                if (pid_opt.has_value()) {
                    item.active_pid = *pid_opt;
                }
            }
        }
        items_.push_back(std::move(item));
    }

    if (selected_index_ >= items_.size() && !items_.empty()) {
        selected_index_ = items_.size() - 1;
    }
}

std::vector<LauncherItem> DesktopLauncher::items() const {
    std::lock_guard<std::mutex> guard(lock_);
    return items_;
}

std::size_t DesktopLauncher::item_count() const {
    std::lock_guard<std::mutex> guard(lock_);
    return items_.size();
}

void DesktopLauncher::select_index(std::size_t idx) noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    if (!items_.empty() && idx < items_.size()) {
        selected_index_ = idx;
    }
}

void DesktopLauncher::select_next() noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    if (!items_.empty()) {
        selected_index_ = (selected_index_ + 1) % items_.size();
    }
}

void DesktopLauncher::select_previous() noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    if (!items_.empty()) {
        selected_index_ = (selected_index_ == 0) ? (items_.size() - 1) : (selected_index_ - 1);
    }
}

bool DesktopLauncher::launch_bundle(const std::string& bundle_id) {
    std::lock_guard<std::mutex> guard(lock_);
    last_error_.clear();

    if (!registry_ || !registry_->is_installed(bundle_id)) {
        last_error_ = "Application not installed in registry: " + bundle_id;
        return false;
    }

    // If already running, focus frontmost window
    if (supervisor_ && supervisor_->is_running(bundle_id)) {
        auto pid_opt = supervisor_->find_pid(bundle_id);
        if (pid_opt.has_value() && wm_) {
            auto wins = wm_->get_windows_for_process(*pid_opt);
            if (!wins.empty()) {
                wm_->set_focus(wins.front()->id);
                wm_->raise_to_top(wins.front()->id);
                return true;
            }
        }
    }

    // Launch via handler
    if (!launch_handler_) {
        last_error_ = "No launch handler registered";
        return false;
    }

    std::uint32_t out_pid = 0;
    if (!launch_handler_(bundle_id, out_pid)) {
        last_error_ = "Launch handler failed to execute: " + bundle_id;
        return false;
    }

    if (supervisor_) {
        supervisor_->register_process(out_pid, bundle_id);
    }

    // Update item status
    for (auto& it : items_) {
        if (it.bundle_id == bundle_id) {
            it.is_running = true;
            it.active_pid = out_pid;
            break;
        }
    }

    return true;
}

bool DesktopLauncher::launch_selected() {
    std::string target_bundle;
    {
        std::lock_guard<std::mutex> guard(lock_);
        if (items_.empty() || selected_index_ >= items_.size()) {
            last_error_ = "No item selected";
            return false;
        }
        target_bundle = items_[selected_index_].bundle_id;
    }
    return launch_bundle(target_bundle);
}

} // namespace winios::graphics

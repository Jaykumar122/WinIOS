#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <cstdint>

#include "packaging/app_registry.h"
#include "kernel/app_supervisor.h"
#include "graphics/window_manager.h"

namespace winios::graphics {

struct LauncherItem {
    std::string bundle_id;
    std::string display_name;
    std::string version;
    std::string icon_path;
    bool        is_running{false};
    std::uint32_t active_pid{0};
};

class DesktopLauncher {
public:
    using LaunchHandler = std::function<bool(const std::string& bundle_id, std::uint32_t& out_pid)>;

    DesktopLauncher(packaging::ApplicationRegistry* registry,
                    kernel::AppSupervisor* supervisor,
                    WindowManager* wm);
    ~DesktopLauncher() = default;

    // Refresh installed application list from registry
    void refresh();

    // Query items
    [[nodiscard]] std::vector<LauncherItem> items() const;
    [[nodiscard]] std::size_t item_count() const;
    [[nodiscard]] std::size_t selected_index() const noexcept { return selected_index_; }

    void select_index(std::size_t idx) noexcept;
    void select_next() noexcept;
    void select_previous() noexcept;

    // Launch actions
    bool launch_bundle(const std::string& bundle_id);
    bool launch_selected();

    void set_launch_handler(LaunchHandler handler) { launch_handler_ = std::move(handler); }

    [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }

private:
    packaging::ApplicationRegistry* registry_{nullptr};
    kernel::AppSupervisor*          supervisor_{nullptr};
    WindowManager*                  wm_{nullptr};
    LaunchHandler                   launch_handler_;

    mutable std::mutex              lock_;
    std::vector<LauncherItem>       items_;
    std::size_t                     selected_index_{0};
    std::string                     last_error_;
};

} // namespace winios::graphics

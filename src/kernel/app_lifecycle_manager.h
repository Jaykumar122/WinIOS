#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <functional>

namespace winios::graphics {
class WindowManager;
class SurfaceResourceManager;
class Compositor;
}

namespace winios::input {
class InputRouter;
}

namespace winios::kernel {

enum class AppState : std::uint8_t {
    Created = 0,
    Starting = 1,
    Foreground = 2,
    Background = 3,
    Suspended = 4,
    Resuming = 5,
    Terminating = 6,
    Terminated = 7
};

struct AppLifecycleRecord {
    std::uint32_t pid{0};
    std::string app_id;
    AppState state{AppState::Created};
    std::vector<std::uint32_t> window_ids;
    std::vector<std::uint32_t> surface_ids;
    std::uint64_t launch_timestamp_us{0};
    std::uint64_t last_transition_timestamp_us{0};
};

class AppLifecycleManager {
public:
    using StateChangeCallback = std::function<void(std::uint32_t pid, AppState old_state, AppState new_state)>;

    AppLifecycleManager(graphics::WindowManager* wm = nullptr,
                        graphics::SurfaceResourceManager* srm = nullptr,
                        graphics::Compositor* comp = nullptr,
                        input::InputRouter* ir = nullptr);

    bool register_app(std::uint32_t pid, const std::string& app_id, std::uint64_t timestamp_us = 0);
    bool transition_to(std::uint32_t pid, AppState target_state, std::uint64_t timestamp_us = 0);

    [[nodiscard]] AppState get_app_state(std::uint32_t pid) const;
    [[nodiscard]] bool is_registered(std::uint32_t pid) const;

    void associate_window(std::uint32_t pid, std::uint32_t window_id);
    void associate_surface(std::uint32_t pid, std::uint32_t surface_id);

    // Lifecycle transitions with side effects
    bool launch_app(std::uint32_t pid, const std::string& app_id, std::uint64_t timestamp_us = 0);
    bool bring_to_foreground(std::uint32_t pid, std::uint64_t timestamp_us = 0);
    bool send_to_background(std::uint32_t pid, std::uint64_t timestamp_us = 0);
    bool suspend_app(std::uint32_t pid, std::uint64_t timestamp_us = 0);
    bool resume_app(std::uint32_t pid, std::uint64_t timestamp_us = 0);
    bool terminate_app(std::uint32_t pid, std::uint64_t timestamp_us = 0);

    [[nodiscard]] std::uint32_t active_app_count() const;
    [[nodiscard]] std::uint32_t foreground_pid() const noexcept { return foreground_pid_; }

    void set_state_callback(StateChangeCallback cb) { callback_ = std::move(cb); }

    static bool is_valid_transition(AppState from, AppState to) noexcept;
    static const char* state_to_string(AppState state) noexcept;

private:
    graphics::WindowManager* wm_{nullptr};
    graphics::SurfaceResourceManager* srm_{nullptr};
    graphics::Compositor* compositor_{nullptr};
    input::InputRouter* input_router_{nullptr};

    mutable std::mutex mutex_;
    std::unordered_map<std::uint32_t, AppLifecycleRecord> apps_;
    std::uint32_t foreground_pid_{0};
    StateChangeCallback callback_;
};

} // namespace winios::kernel

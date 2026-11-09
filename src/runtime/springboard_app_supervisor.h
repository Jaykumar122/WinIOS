#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "graphics/window_server.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"

namespace winios::runtime {

// -----------------------------------------------------------------------------
// Architectural Execution Classification
// -----------------------------------------------------------------------------
enum class ExecutionClassification {
    SyntheticSimulated,       // Autonomous synthetic engineering test harness
    GenuineIOS,               // Genuine Apple-signed rootfs & decrypted kernel
    GenuineEncrypted_Blocked  // Encrypted Apple proprietary artifact requiring SEP
};

[[nodiscard]] const char* execution_classification_string(ExecutionClassification c) noexcept;

// -----------------------------------------------------------------------------
// Application Bundle & Lifecycle States
// -----------------------------------------------------------------------------
enum class AppLifecycleState {
    NotRunning,
    Launching,
    ForegroundActive,
    Background,
    Suspended,
    Terminated
};

[[nodiscard]] const char* app_lifecycle_state_string(AppLifecycleState s) noexcept;

struct AppBundleInfo {
    std::string   bundle_id;
    std::string   executable_name;
    std::string   bundle_path;
    std::string   display_name;
    std::string   min_os_version{"17.0"};
    std::uint32_t icon_color{0xFF007AFFu}; // iOS blue default
    graphics::Rect icon_grid_frame{0, 0, 180, 180};
};

struct RunningAppInstance {
    AppBundleInfo                       bundle_info;
    std::uint32_t                       pid{0};
    AppLifecycleState                   state{AppLifecycleState::NotRunning};
    std::shared_ptr<graphics::IOSurface> surface;
    std::uint32_t                       layer_id{0};
};

// -----------------------------------------------------------------------------
// SpringBoardSupervisor: FrontBoard Shell & Native Application Supervisor
// -----------------------------------------------------------------------------
class SpringBoardSupervisor {
public:
    explicit SpringBoardSupervisor(
        ExecutionClassification exec_class = ExecutionClassification::SyntheticSimulated);
    ~SpringBoardSupervisor() = default;

    [[nodiscard]] ExecutionClassification execution_class() const noexcept {
        return exec_class_;
    }

    // App Discovery & Registration
    void register_app(AppBundleInfo app);
    std::size_t discover_applications(const DarwinVfs& vfs);
    [[nodiscard]] bool has_app(std::string_view bundle_id) const noexcept;
    [[nodiscard]] const AppBundleInfo* find_app(std::string_view bundle_id) const noexcept;
    [[nodiscard]] std::vector<AppBundleInfo> get_all_installed_apps() const;

    // FrontBoard Application Lifecycle Management
    bool launch_app(
        std::string_view bundle_id,
        DarwinBsdSubsystem& bsd,
        MachIpcSubsystem& ipc,
        graphics::WindowServerCompositor& compositor,
        Memory& memory);

    bool send_to_background(
        std::string_view bundle_id,
        graphics::WindowServerCompositor& compositor);

    bool bring_to_foreground(
        std::string_view bundle_id,
        graphics::WindowServerCompositor& compositor);

    bool terminate_app(
        std::string_view bundle_id,
        DarwinBsdSubsystem& bsd,
        graphics::WindowServerCompositor& compositor);

    // Touch Event Dispatcher (Home Screen Hit Testing)
    bool handle_touch_down(
        std::uint32_t x,
        std::uint32_t y,
        DarwinBsdSubsystem& bsd,
        MachIpcSubsystem& ipc,
        graphics::WindowServerCompositor& compositor,
        Memory& memory);

    // Running App Inspection
    [[nodiscard]] const RunningAppInstance* get_running_app(std::string_view bundle_id) const noexcept;
    [[nodiscard]] std::size_t running_app_count() const noexcept;

private:
    ExecutionClassification                 exec_class_{ExecutionClassification::SyntheticSimulated};
    std::uint32_t                           next_layer_id_{200};
    mutable std::mutex                      lock_;
    std::map<std::string, AppBundleInfo>    installed_apps_;
    std::map<std::string, RunningAppInstance> running_apps_;
};

} // namespace winios::runtime

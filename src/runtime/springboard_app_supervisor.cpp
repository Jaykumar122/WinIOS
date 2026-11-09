#include "runtime/springboard_app_supervisor.h"

#include <algorithm>

namespace winios::runtime {

const char* execution_classification_string(ExecutionClassification c) noexcept {
    switch (c) {
        case ExecutionClassification::SyntheticSimulated:      return "SyntheticSimulated";
        case ExecutionClassification::GenuineIOS:              return "GenuineIOS";
        case ExecutionClassification::GenuineEncrypted_Blocked: return "GenuineEncrypted_Blocked";
        default:                                               return "Unknown";
    }
}

const char* app_lifecycle_state_string(AppLifecycleState s) noexcept {
    switch (s) {
        case AppLifecycleState::NotRunning:       return "NotRunning";
        case AppLifecycleState::Launching:        return "Launching";
        case AppLifecycleState::ForegroundActive: return "ForegroundActive";
        case AppLifecycleState::Background:       return "Background";
        case AppLifecycleState::Suspended:        return "Suspended";
        case AppLifecycleState::Terminated:       return "Terminated";
        default:                                  return "Unknown";
    }
}

SpringBoardSupervisor::SpringBoardSupervisor(ExecutionClassification exec_class)
    : exec_class_(exec_class) {

    // 1. MobileSafari
    AppBundleInfo safari;
    safari.bundle_id = "com.apple.mobilesafari";
    safari.display_name = "Safari";
    safari.executable_name = "MobileSafari";
    safari.bundle_path = "/Applications/MobileSafari.app/MobileSafari";
    safari.icon_color = 0xFF007AFFu; // iOS Blue
    safari.icon_grid_frame = graphics::Rect{100, 300, 180, 180};
    register_app(std::move(safari));

    // 2. Preferences
    AppBundleInfo settings;
    settings.bundle_id = "com.apple.Preferences";
    settings.display_name = "Settings";
    settings.executable_name = "Preferences";
    settings.bundle_path = "/Applications/Preferences.app/Preferences";
    settings.icon_color = 0xFF8E8E93u; // iOS Gray
    settings.icon_grid_frame = graphics::Rect{350, 300, 180, 180};
    register_app(std::move(settings));

    // 3. Camera
    AppBundleInfo camera;
    camera.bundle_id = "com.apple.camera";
    camera.display_name = "Camera";
    camera.executable_name = "Camera";
    camera.bundle_path = "/Applications/Camera.app/Camera";
    camera.icon_color = 0xFF34C759u; // iOS Green
    camera.icon_grid_frame = graphics::Rect{600, 300, 180, 180};
    register_app(std::move(camera));

    // 4. App Store
    AppBundleInfo store;
    store.bundle_id = "com.apple.MobileStore";
    store.display_name = "App Store";
    store.executable_name = "AppStore";
    store.bundle_path = "/Applications/AppStore.app/AppStore";
    store.icon_color = 0xFF007AFFu;
    store.icon_grid_frame = graphics::Rect{850, 300, 180, 180};
    register_app(std::move(store));
}

void SpringBoardSupervisor::register_app(AppBundleInfo app) {
    std::lock_guard<std::mutex> guard(lock_);
    installed_apps_[app.bundle_id] = std::move(app);
}

std::size_t SpringBoardSupervisor::discover_applications(const DarwinVfs& vfs) {
    auto apps_dir = vfs.resolve_path("/Applications");
    if (!apps_dir || !apps_dir->is_directory()) {
        return 0;
    }

    std::lock_guard<std::mutex> guard(lock_);
    return installed_apps_.size();
}

bool SpringBoardSupervisor::has_app(std::string_view bundle_id) const noexcept {
    return find_app(bundle_id) != nullptr;
}

const AppBundleInfo* SpringBoardSupervisor::find_app(std::string_view bundle_id) const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = installed_apps_.find(std::string(bundle_id));
    if (it != installed_apps_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<AppBundleInfo> SpringBoardSupervisor::get_all_installed_apps() const {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<AppBundleInfo> list;
    for (const auto& [_, app] : installed_apps_) {
        list.push_back(app);
    }
    return list;
}

bool SpringBoardSupervisor::launch_app(
    std::string_view bundle_id,
    DarwinBsdSubsystem& bsd,
    MachIpcSubsystem& ipc,
    graphics::WindowServerCompositor& compositor,
    Memory& memory) {

    (void)memory;
    std::lock_guard<std::mutex> guard(lock_);
    auto it = installed_apps_.find(std::string(bundle_id));
    if (it == installed_apps_.end()) {
        return false;
    }

    const auto& app_info = it->second;

    // Check if already running
    auto run_it = running_apps_.find(std::string(bundle_id));
    if (run_it != running_apps_.end()) {
        if (run_it->second.state == AppLifecycleState::Background) {
            run_it->second.state = AppLifecycleState::ForegroundActive;
            compositor.update_layer_frame(run_it->second.layer_id, graphics::Rect{0, 0, compositor.width(), compositor.height()});
            return true;
        }
        return true;
    }

    // Spawn process under mobile user (UID 501)
    std::uint32_t pid = bsd.create_process(1, app_info.bundle_path, 501);

    // Create application window surface
    auto surf = compositor.create_surface(compositor.width(), compositor.height());
    surf->fill_rect(graphics::Rect{0, 0, compositor.width(), compositor.height()}, app_info.icon_color);

    std::uint32_t layer_id = next_layer_id_++;
    graphics::WindowServerLayer app_layer;
    app_layer.layer_id = layer_id;
    app_layer.client_task_id = pid;
    app_layer.surface = surf;
    app_layer.frame = graphics::Rect{0, 0, compositor.width(), compositor.height()};
    app_layer.z_order = 50; // Active foreground layer
    app_layer.opacity = 1.0f;
    app_layer.is_hidden = false;

    compositor.add_layer(app_layer);

    // Register with bootstrap IPC
    auto task_space = ipc.get_task_port_space(pid);
    if (!task_space) {
        task_space = ipc.create_task_port_space(pid);
    }
    auto obj = std::make_shared<MachPortObject>(pid * 2000, pid);
    mach_port_t port = task_space->allocate_port(obj, kMachPortRightReceive);
    ipc.bootstrap_register(pid, app_info.bundle_id, port);

    RunningAppInstance inst;
    inst.bundle_info = app_info;
    inst.pid = pid;
    inst.state = AppLifecycleState::ForegroundActive;
    inst.surface = surf;
    inst.layer_id = layer_id;

    running_apps_[std::string(bundle_id)] = std::move(inst);
    return true;
}

bool SpringBoardSupervisor::send_to_background(
    std::string_view bundle_id,
    graphics::WindowServerCompositor& compositor) {

    std::lock_guard<std::mutex> guard(lock_);
    auto it = running_apps_.find(std::string(bundle_id));
    if (it == running_apps_.end()) {
        return false;
    }

    auto& inst = it->second;
    if (inst.state == AppLifecycleState::ForegroundActive) {
        inst.state = AppLifecycleState::Background;
        compositor.remove_layer(inst.layer_id);
        return true;
    }

    return false;
}

bool SpringBoardSupervisor::bring_to_foreground(
    std::string_view bundle_id,
    graphics::WindowServerCompositor& compositor) {

    std::lock_guard<std::mutex> guard(lock_);
    auto it = running_apps_.find(std::string(bundle_id));
    if (it == running_apps_.end()) {
        return false;
    }

    auto& inst = it->second;
    if (inst.state == AppLifecycleState::Background) {
        inst.state = AppLifecycleState::ForegroundActive;

        graphics::WindowServerLayer layer;
        layer.layer_id = inst.layer_id;
        layer.client_task_id = inst.pid;
        layer.surface = inst.surface;
        layer.frame = graphics::Rect{0, 0, compositor.width(), compositor.height()};
        layer.z_order = 50;
        layer.opacity = 1.0f;
        layer.is_hidden = false;

        compositor.add_layer(layer);
        return true;
    }

    return false;
}

bool SpringBoardSupervisor::terminate_app(
    std::string_view bundle_id,
    DarwinBsdSubsystem& bsd,
    graphics::WindowServerCompositor& compositor) {

    std::lock_guard<std::mutex> guard(lock_);
    auto it = running_apps_.find(std::string(bundle_id));
    if (it == running_apps_.end()) {
        return false;
    }

    auto& inst = it->second;
    bsd.kill(inst.pid, kDarwinSigKill);
    compositor.remove_layer(inst.layer_id);
    inst.state = AppLifecycleState::Terminated;

    running_apps_.erase(it);
    return true;
}

bool SpringBoardSupervisor::handle_touch_down(
    std::uint32_t x,
    std::uint32_t y,
    DarwinBsdSubsystem& bsd,
    MachIpcSubsystem& ipc,
    graphics::WindowServerCompositor& compositor,
    Memory& memory) {

    std::string hit_bundle_id;
    {
        std::lock_guard<std::mutex> guard(lock_);
        for (const auto& [bid, app] : installed_apps_) {
            if (app.icon_grid_frame.contains(x, y)) {
                hit_bundle_id = bid;
                break;
            }
        }
    }

    if (!hit_bundle_id.empty()) {
        return launch_app(hit_bundle_id, bsd, ipc, compositor, memory);
    }

    return false;
}

const RunningAppInstance* SpringBoardSupervisor::get_running_app(std::string_view bundle_id) const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = running_apps_.find(std::string(bundle_id));
    if (it != running_apps_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::size_t SpringBoardSupervisor::running_app_count() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return running_apps_.size();
}

} // namespace winios::runtime

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "graphics/display_surface.h"
#include "graphics/window_manager.h"
#include "packaging/app_package.h"
#include "runtime/guest_allocator.h"
#include "runtime/guest_runtime_lifecycle.h"
#include "runtime/guest_storage_api.h"
#include "runtime/app_preferences.h"
#include "resources/resource_bundle.h"
#include "resources/resource_manager.h"
#include "runtime/ui/guest_ui_framework.h"
#include "runtime/ui/guest_layout_engine.h"

namespace winios::apps {

// -----------------------------------------------------------------------------
// 1. Runtime Inspector Application (com.winios.runtimeinspector)
// -----------------------------------------------------------------------------
class RuntimeInspectorApp {
public:
    RuntimeInspectorApp(uint32_t pid,
                        std::shared_ptr<graphics::WindowManager> win_mgr,
                        std::shared_ptr<graphics::SurfaceResourceManager> surf_mgr,
                        std::shared_ptr<runtime::GuestRuntimeLifecycle> runtime);
    ~RuntimeInspectorApp();

    bool Launch();
    void Terminate();
    void Render();
    bool HandleEvent(const winios_event_t& ev);
    void RefreshStats();

    uint32_t GetWindowId() const { return wid_; }
    bool IsRunning() const { return is_running_; }
    std::shared_ptr<graphics::DisplaySurface> GetSurface() const { return surf_; }

private:
    uint32_t pid_{10};
    std::shared_ptr<graphics::WindowManager> win_mgr_;
    std::shared_ptr<graphics::SurfaceResourceManager> surf_mgr_;
    std::shared_ptr<runtime::GuestRuntimeLifecycle> runtime_;
    uint32_t wid_{0};
    std::shared_ptr<graphics::DisplaySurface> surf_{nullptr};
    bool is_running_{false};

    std::shared_ptr<runtime::ui::GuestContainer> root_container_;
    std::shared_ptr<runtime::ui::GuestLabel> version_label_;
    std::shared_ptr<runtime::ui::GuestLabel> memory_label_;
    std::shared_ptr<runtime::ui::GuestLabel> instructions_label_;
    std::shared_ptr<runtime::ui::GuestButton> refresh_btn_;
};

// -----------------------------------------------------------------------------
// 2. Resource Viewer Application (com.winios.resourceviewer)
// -----------------------------------------------------------------------------
class ResourceViewerApp {
public:
    ResourceViewerApp(uint32_t pid,
                      std::shared_ptr<graphics::WindowManager> win_mgr,
                      std::shared_ptr<graphics::SurfaceResourceManager> surf_mgr,
                      std::shared_ptr<resources::ResourceManager> res_mgr);
    ~ResourceViewerApp();

    bool Launch();
    void Terminate();
    void Render();
    bool HandleEvent(const winios_event_t& ev);

    uint32_t GetWindowId() const { return wid_; }
    bool IsRunning() const { return is_running_; }
    std::shared_ptr<graphics::DisplaySurface> GetSurface() const { return surf_; }
    const std::string& GetLoadedText() const { return loaded_text_; }

private:
    uint32_t pid_{11};
    std::shared_ptr<graphics::WindowManager> win_mgr_;
    std::shared_ptr<graphics::SurfaceResourceManager> surf_mgr_;
    std::shared_ptr<resources::ResourceManager> res_mgr_;
    uint32_t wid_{0};
    std::shared_ptr<graphics::DisplaySurface> surf_{nullptr};
    bool is_running_{false};

    std::shared_ptr<runtime::ui::GuestContainer> root_container_;
    std::shared_ptr<runtime::ui::GuestLabel> title_label_;
    std::shared_ptr<runtime::ui::GuestLabel> content_label_;
    std::shared_ptr<runtime::ui::GuestImageView> image_view_;
    std::string loaded_text_;
};

// -----------------------------------------------------------------------------
// 3. Task Notes Application (com.winios.tasknotes)
// -----------------------------------------------------------------------------
class TaskNotesApp {
public:
    TaskNotesApp(uint32_t pid,
                 std::shared_ptr<graphics::WindowManager> win_mgr,
                 std::shared_ptr<graphics::SurfaceResourceManager> surf_mgr,
                 std::shared_ptr<runtime::GuestStorageApi> storage_api,
                 std::shared_ptr<runtime::AppPreferences> preferences);
    ~TaskNotesApp();

    bool Launch();
    void Terminate();
    void Render();
    bool HandleEvent(const winios_event_t& ev);

    void AddTask(const std::string& task_text);
    void SaveNotes();
    void LoadNotes();
    void ProcessTimerTick();

    uint32_t GetWindowId() const { return wid_; }
    bool IsRunning() const { return is_running_; }
    std::shared_ptr<graphics::DisplaySurface> GetSurface() const { return surf_; }
    const std::vector<std::string>& GetTasks() const { return tasks_; }
    uint32_t GetTimerTickCount() const { return timer_ticks_; }

private:
    uint32_t pid_{12};
    std::shared_ptr<graphics::WindowManager> win_mgr_;
    std::shared_ptr<graphics::SurfaceResourceManager> surf_mgr_;
    std::shared_ptr<runtime::GuestStorageApi> storage_api_;
    std::shared_ptr<runtime::AppPreferences> preferences_;
    uint32_t wid_{0};
    std::shared_ptr<graphics::DisplaySurface> surf_{nullptr};
    bool is_running_{false};

    std::shared_ptr<runtime::ui::GuestContainer> root_container_;
    std::shared_ptr<runtime::ui::GuestLabel> title_label_;
    std::shared_ptr<runtime::ui::GuestTextInput> input_box_;
    std::shared_ptr<runtime::ui::GuestButton> add_btn_;
    std::shared_ptr<runtime::ui::GuestLabel> tasks_list_label_;
    std::vector<std::string> tasks_;
    uint32_t timer_ticks_{0};
};

// -----------------------------------------------------------------------------
// Package Creation Helpers for Campaign 217
// -----------------------------------------------------------------------------
packaging::AppPackage CreateRuntimeInspectorPackage();
packaging::AppPackage CreateResourceViewerPackage();
packaging::AppPackage CreateTaskNotesPackage();

} // namespace winios::apps

#include "apps/campaign217_apps.h"
#include <sstream>

namespace winios::apps {

// =============================================================================
// 1. Runtime Inspector Application
// =============================================================================
RuntimeInspectorApp::RuntimeInspectorApp(uint32_t pid,
                                         std::shared_ptr<graphics::WindowManager> win_mgr,
                                         std::shared_ptr<graphics::SurfaceResourceManager> surf_mgr,
                                         std::shared_ptr<runtime::GuestRuntimeLifecycle> runtime)
    : pid_(pid), win_mgr_(std::move(win_mgr)), surf_mgr_(std::move(surf_mgr)), runtime_(std::move(runtime)) {}

RuntimeInspectorApp::~RuntimeInspectorApp() {
    Terminate();
}

bool RuntimeInspectorApp::Launch() {
    if (is_running_) return true;
    if (!win_mgr_ || !surf_mgr_) return false;

    const uint32_t w = 360;
    const uint32_t h = 260;
    auto res = surf_mgr_->create_surface(pid_, w, h, graphics::PixelFormat::B8G8R8A8_UNORM, surf_);
    if (res != graphics::SurfaceResultCode::Success || !surf_) return false;

    wid_ = win_mgr_->create_window(pid_, "Runtime Inspector", {50, 50, w, h}, surf_);
    if (wid_ == 0) return false;

    root_container_ = std::make_shared<runtime::ui::GuestContainer>();
    root_container_->SetFrame(0, 0, w, h);
    root_container_->SetBackgroundColor(WINIOS_COLOR_RGB(240, 240, 245));

    version_label_ = std::make_shared<runtime::ui::GuestLabel>("WinIOS Runtime v1.2 (ABI 0x00010200)");
    version_label_->SetFrame(16, 16, 328, 24);

    memory_label_ = std::make_shared<runtime::ui::GuestLabel>("Heap Used: 0 KB / Active Allocs: 0");
    memory_label_->SetFrame(16, 48, 328, 24);

    instructions_label_ = std::make_shared<runtime::ui::GuestLabel>("Retired Instructions: 0");
    instructions_label_->SetFrame(16, 80, 328, 24);

    refresh_btn_ = std::make_shared<runtime::ui::GuestButton>("Refresh Diagnostics");
    refresh_btn_->SetFrame(16, 120, 180, 36);
    refresh_btn_->SetOnClick([this] {
        RefreshStats();
    });

    root_container_->AddChild(version_label_);
    root_container_->AddChild(memory_label_);
    root_container_->AddChild(instructions_label_);
    root_container_->AddChild(refresh_btn_);

    is_running_ = true;
    RefreshStats();
    Render();
    return true;
}

void RuntimeInspectorApp::Terminate() {
    if (!is_running_) return;
    if (surf_ && surf_mgr_) {
        surf_mgr_->destroy_surface(pid_, surf_->id());
        surf_ = nullptr;
    }
    if (win_mgr_ && wid_ != 0) {
        win_mgr_->destroy_window(pid_, wid_);
    }
    wid_ = 0;
    is_running_ = false;
}

void RuntimeInspectorApp::RefreshStats() {
    if (!runtime_) return;
    auto* alloc = runtime_->GetAllocator();
    size_t alloc_bytes = alloc ? alloc->GetStats().bytes_allocated : 0;
    size_t active_count = alloc ? alloc->GetStats().active_allocations_count : 0;

    std::ostringstream ss_mem;
    ss_mem << "Heap Used: " << (alloc_bytes / 1024) << " KB / Active: " << active_count;
    if (memory_label_) memory_label_->SetText(ss_mem.str());

    std::ostringstream ss_ins;
    ss_ins << "Retired Instructions: " << runtime_->GetRetiredInstructions();
    if (instructions_label_) instructions_label_->SetText(ss_ins.str());

    Render();
}

void RuntimeInspectorApp::Render() {
    if (!is_running_ || !surf_ || !root_container_) return;
    auto* pixels = reinterpret_cast<uint32_t*>(surf_->raw_data());
    root_container_->Render(pixels, surf_->width(), surf_->height());
}

bool RuntimeInspectorApp::HandleEvent(const winios_event_t& ev) {
    if (!is_running_ || !root_container_) return false;
    bool handled = root_container_->HandleEvent(ev);
    if (handled) {
        Render();
    }
    return handled;
}

// =============================================================================
// 2. Resource Viewer Application
// =============================================================================
ResourceViewerApp::ResourceViewerApp(uint32_t pid,
                                     std::shared_ptr<graphics::WindowManager> win_mgr,
                                     std::shared_ptr<graphics::SurfaceResourceManager> surf_mgr,
                                     std::shared_ptr<resources::ResourceManager> res_mgr)
    : pid_(pid), win_mgr_(std::move(win_mgr)), surf_mgr_(std::move(surf_mgr)), res_mgr_(std::move(res_mgr)) {}

ResourceViewerApp::~ResourceViewerApp() {
    Terminate();
}

bool ResourceViewerApp::Launch() {
    if (is_running_) return true;
    if (!win_mgr_ || !surf_mgr_) return false;

    const uint32_t w = 380;
    const uint32_t h = 300;
    auto res = surf_mgr_->create_surface(pid_, w, h, graphics::PixelFormat::B8G8R8A8_UNORM, surf_);
    if (res != graphics::SurfaceResultCode::Success || !surf_) return false;

    wid_ = win_mgr_->create_window(pid_, "Resource Viewer", {80, 80, w, h}, surf_);
    if (wid_ == 0) return false;

    root_container_ = std::make_shared<runtime::ui::GuestContainer>();
    root_container_->SetFrame(0, 0, w, h);
    root_container_->SetBackgroundColor(WINIOS_COLOR_RGB(250, 250, 250));

    title_label_ = std::make_shared<runtime::ui::GuestLabel>("Application Resource Viewer");
    title_label_->SetFrame(16, 16, 348, 24);

    content_label_ = std::make_shared<runtime::ui::GuestLabel>("Loading packaged resources...");
    content_label_->SetFrame(16, 48, 348, 48);

    image_view_ = std::make_shared<runtime::ui::GuestImageView>();
    image_view_->SetFrame(16, 110, 64, 64);

    if (res_mgr_) {
        std::string text;
        if (res_mgr_->LoadText("readme", &text)) {
            loaded_text_ = text;
            content_label_->SetText(text);
        }
        resources::DecodedImage img;
        if (res_mgr_->LoadImage("app_icon", &img)) {
            image_view_->SetImage(img);
        }
    }

    root_container_->AddChild(title_label_);
    root_container_->AddChild(content_label_);
    root_container_->AddChild(image_view_);

    is_running_ = true;
    Render();
    return true;
}

void ResourceViewerApp::Terminate() {
    if (!is_running_) return;
    if (surf_ && surf_mgr_) {
        surf_mgr_->destroy_surface(pid_, surf_->id());
        surf_ = nullptr;
    }
    if (win_mgr_ && wid_ != 0) {
        win_mgr_->destroy_window(pid_, wid_);
    }
    wid_ = 0;
    is_running_ = false;
}

void ResourceViewerApp::Render() {
    if (!is_running_ || !surf_ || !root_container_) return;
    auto* pixels = reinterpret_cast<uint32_t*>(surf_->raw_data());
    root_container_->Render(pixels, surf_->width(), surf_->height());
}

bool ResourceViewerApp::HandleEvent(const winios_event_t& ev) {
    if (!is_running_ || !root_container_) return false;
    bool handled = root_container_->HandleEvent(ev);
    if (handled) {
        Render();
    }
    return handled;
}

// =============================================================================
// 3. Task Notes Application
// =============================================================================
TaskNotesApp::TaskNotesApp(uint32_t pid,
                           std::shared_ptr<graphics::WindowManager> win_mgr,
                           std::shared_ptr<graphics::SurfaceResourceManager> surf_mgr,
                           std::shared_ptr<runtime::GuestStorageApi> storage_api,
                           std::shared_ptr<runtime::AppPreferences> preferences)
    : pid_(pid),
      win_mgr_(std::move(win_mgr)),
      surf_mgr_(std::move(surf_mgr)),
      storage_api_(std::move(storage_api)),
      preferences_(std::move(preferences)) {}

TaskNotesApp::~TaskNotesApp() {
    Terminate();
}

bool TaskNotesApp::Launch() {
    if (is_running_) return true;
    if (!win_mgr_ || !surf_mgr_) return false;

    const uint32_t w = 360;
    const uint32_t h = 420;
    auto res = surf_mgr_->create_surface(pid_, w, h, graphics::PixelFormat::B8G8R8A8_UNORM, surf_);
    if (res != graphics::SurfaceResultCode::Success || !surf_) return false;

    wid_ = win_mgr_->create_window(pid_, "Task Notes", {120, 120, w, h}, surf_);
    if (wid_ == 0) return false;

    root_container_ = std::make_shared<runtime::ui::GuestContainer>();
    root_container_->SetFrame(0, 0, w, h);
    root_container_->SetBackgroundColor(WINIOS_COLOR_RGB(245, 245, 248));

    title_label_ = std::make_shared<runtime::ui::GuestLabel>("Task Notes (Persisted)");
    title_label_->SetFrame(16, 16, 328, 24);

    input_box_ = std::make_shared<runtime::ui::GuestTextInput>("Enter task note...");
    input_box_->SetFrame(16, 48, 220, 32);

    add_btn_ = std::make_shared<runtime::ui::GuestButton>("Add");
    add_btn_->SetFrame(244, 48, 100, 32);
    add_btn_->SetOnClick([this] {
        if (input_box_ && !input_box_->GetText().empty()) {
            AddTask(input_box_->GetText());
            input_box_->SetText("");
            SaveNotes();
        }
    });

    tasks_list_label_ = std::make_shared<runtime::ui::GuestLabel>("No tasks");
    tasks_list_label_->SetFrame(16, 96, 328, 280);

    root_container_->AddChild(title_label_);
    root_container_->AddChild(input_box_);
    root_container_->AddChild(add_btn_);
    root_container_->AddChild(tasks_list_label_);

    is_running_ = true;
    LoadNotes();
    Render();
    return true;
}

void TaskNotesApp::Terminate() {
    if (!is_running_) return;
    SaveNotes();
    if (surf_ && surf_mgr_) {
        surf_mgr_->destroy_surface(pid_, surf_->id());
        surf_ = nullptr;
    }
    if (win_mgr_ && wid_ != 0) {
        win_mgr_->destroy_window(pid_, wid_);
    }
    wid_ = 0;
    is_running_ = false;
}

void TaskNotesApp::AddTask(const std::string& task_text) {
    tasks_.push_back(task_text);
    if (preferences_) {
        preferences_->SetString("last_task", task_text);
        preferences_->SetInt("task_count", static_cast<int32_t>(tasks_.size()));
        preferences_->Save();
    }

    std::ostringstream ss;
    for (size_t i = 0; i < tasks_.size(); ++i) {
        ss << (i + 1) << ". " << tasks_[i] << "\n";
    }
    if (tasks_list_label_) {
        tasks_list_label_->SetText(ss.str());
    }
    Render();
}

void TaskNotesApp::SaveNotes() {
    if (!storage_api_) return;
    int fd = storage_api_->OpenFile("notes.txt", static_cast<uint32_t>(runtime::FileMode::kWriteOnly) |
                                                static_cast<uint32_t>(runtime::FileMode::kCreate) |
                                                static_cast<uint32_t>(runtime::FileMode::kTruncate));
    if (fd >= 0) {
        std::ostringstream ss;
        for (const auto& t : tasks_) {
            ss << t << "\n";
        }
        std::string data = ss.str();
        storage_api_->WriteFile(fd, data.data(), data.size());
        storage_api_->CloseFile(fd);
    }
}

void TaskNotesApp::LoadNotes() {
    tasks_.clear();
    if (storage_api_) {
        int fd = storage_api_->OpenFile("notes.txt", static_cast<uint32_t>(runtime::FileMode::kReadOnly));
        if (fd >= 0) {
            std::vector<char> buf(4096, 0);
            int n = storage_api_->ReadFile(fd, buf.data(), buf.size() - 1);
            storage_api_->CloseFile(fd);
            if (n > 0) {
                std::istringstream ss(std::string(buf.data(), static_cast<size_t>(n)));
                std::string line;
                while (std::getline(ss, line)) {
                    if (!line.empty()) tasks_.push_back(line);
                }
            }
        }
    }

    std::ostringstream ss;
    if (tasks_.empty()) {
        ss << "No tasks";
    } else {
        for (size_t i = 0; i < tasks_.size(); ++i) {
            ss << (i + 1) << ". " << tasks_[i] << "\n";
        }
    }
    if (tasks_list_label_) {
        tasks_list_label_->SetText(ss.str());
    }
}

void TaskNotesApp::ProcessTimerTick() {
    timer_ticks_++;
    SaveNotes();
}

void TaskNotesApp::Render() {
    if (!is_running_ || !surf_ || !root_container_) return;
    auto* pixels = reinterpret_cast<uint32_t*>(surf_->raw_data());
    root_container_->Render(pixels, surf_->width(), surf_->height());
}

bool TaskNotesApp::HandleEvent(const winios_event_t& ev) {
    if (!is_running_ || !root_container_) return false;
    if (ev.type == WINIOS_EVENT_TIMER) {
        ProcessTimerTick();
        return true;
    }
    bool handled = root_container_->HandleEvent(ev);
    if (handled) {
        Render();
    }
    return handled;
}

// =============================================================================
// Package Creation Helpers
// =============================================================================
packaging::AppPackage CreateRuntimeInspectorPackage() {
    packaging::AppPackage pkg;
    pkg.manifest.bundle_id = "com.winios.runtimeinspector";
    pkg.manifest.name = "Runtime Inspector";
    pkg.manifest.version = "1.2.0";
    pkg.manifest.min_os_version = "217.0";
    pkg.manifest.executable_name = "bin/runtime_inspector";
    pkg.manifest.permissions = {"ui.window", "runtime.diagnostics"};

    std::vector<uint8_t> bin_data = {
        0x60, 0x00, 0x80, 0x52, // movz w0, #3
        0xc0, 0x03, 0x5f, 0xd6  // ret
    };
    pkg.add_file("bin/runtime_inspector", bin_data);
    return pkg;
}

packaging::AppPackage CreateResourceViewerPackage() {
    packaging::AppPackage pkg;
    pkg.manifest.bundle_id = "com.winios.resourceviewer";
    pkg.manifest.name = "Resource Viewer";
    pkg.manifest.version = "1.2.0";
    pkg.manifest.min_os_version = "217.0";
    pkg.manifest.executable_name = "bin/resource_viewer";
    pkg.manifest.permissions = {"ui.window", "resources.read"};

    std::vector<uint8_t> bin_data = {
        0x60, 0x00, 0x80, 0x52, // movz w0, #3
        0xc0, 0x03, 0x5f, 0xd6  // ret
    };
    pkg.add_file("bin/resource_viewer", bin_data);

    std::string readme_txt = "WinIOS Resource Subsystem v1.2: Packaged assets loaded correctly.";
    std::vector<uint8_t> txt_data(readme_txt.begin(), readme_txt.end());
    pkg.add_file("resources/readme.txt", txt_data);

    std::vector<uint8_t> bmp_data = {
        0x42, 0x4D, 0x3A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00,
        0x28, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
        0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00
    };
    pkg.add_file("resources/icon.bmp", bmp_data);

    return pkg;
}

packaging::AppPackage CreateTaskNotesPackage() {
    packaging::AppPackage pkg;
    pkg.manifest.bundle_id = "com.winios.tasknotes";
    pkg.manifest.name = "Task Notes";
    pkg.manifest.version = "1.2.0";
    pkg.manifest.min_os_version = "217.0";
    pkg.manifest.executable_name = "bin/task_notes";
    pkg.manifest.permissions = {"ui.window", "storage.isolated", "timer.create"};

    std::vector<uint8_t> bin_data = {
        0x60, 0x00, 0x80, 0x52, // movz w0, #3
        0xc0, 0x03, 0x5f, 0xd6  // ret
    };
    pkg.add_file("bin/task_notes", bin_data);
    return pkg;
}

} // namespace winios::apps

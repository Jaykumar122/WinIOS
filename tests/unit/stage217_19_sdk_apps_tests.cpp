#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "winios_sdk/winios_sdk.h"
#include "vfs/vfs.h"
#include "storage/isolated_storage.h"
#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "runtime/guest_runtime_lifecycle.h"
#include "runtime/guest_storage_api.h"
#include "runtime/app_preferences.h"
#include "resources/resource_bundle.h"
#include "resources/resource_manager.h"
#include "apps/campaign217_apps.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::apps;
using namespace winios::runtime;
using namespace winios::storage;
using namespace winios::graphics;
using namespace winios::resources;

void test_app_preferences_persistence() {
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    auto iso = std::make_shared<IsolatedStorage>(vfs, "com.test.prefs");

    {
        AppPreferences prefs(iso, "com.test.prefs");
        prefs.SetString("username", "AntigravityUser");
        prefs.SetInt("theme_mode", 2);
        prefs.SetBool("notifications", true);
        TEST_ASSERT(prefs.Save());
    }

    // Reload from storage
    {
        AppPreferences prefs(iso, "com.test.prefs");
        TEST_ASSERT(prefs.Load());
        TEST_ASSERT(prefs.GetString("username") == "AntigravityUser");
        TEST_ASSERT(prefs.GetInt("theme_mode") == 2);
        TEST_ASSERT(prefs.GetBool("notifications") == true);
    }

    std::cout << "[PASS] test_app_preferences_persistence\n";
}

void test_sdk_v12_abi_structures() {
    TEST_ASSERT(WINIOS_CATEGORY_C_ABI_VERSION >= WINIOS_CATEGORY_C_ABI_VERSION_1_1);
    TEST_ASSERT(WINIOS_CATEGORY_C_ABI_VERSION_1_2 == 0x00010200);
    TEST_ASSERT(WINIOS_SDK_VERSION >= WINIOS_SDK_VERSION_1_2);

    TEST_ASSERT(sizeof(winios_rect_t) == 16);
    TEST_ASSERT(sizeof(winios_event_t) == 32);
    TEST_ASSERT(sizeof(winios_window_geometry_t) == 32);
    TEST_ASSERT(sizeof(winios_window_list_t) == 136);
    TEST_ASSERT(sizeof(winios_storage_req_t) == 16);
    TEST_ASSERT(sizeof(winios_ipc_msg_desc_t) == 24);
    TEST_ASSERT(sizeof(winios_pref_entry_t) == 32);
    TEST_ASSERT(sizeof(winios_runtime_info_t) == 24);

    std::cout << "[PASS] test_sdk_v12_abi_structures\n";
}

void test_sample_apps_execution() {
    auto win_mgr = std::make_shared<WindowManager>();
    auto surf_mgr = std::make_shared<SurfaceResourceManager>();
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();

    // 1. Runtime Inspector App
    GuestProcessConfig cfg{};
    cfg.process_id = 10;
    auto runtime = std::make_shared<GuestRuntimeLifecycle>(cfg);
    runtime->Initialize();
    runtime->Launch();
    runtime->Step(1234);

    RuntimeInspectorApp inspector(10, win_mgr, surf_mgr, runtime);
    TEST_ASSERT(inspector.Launch());
    TEST_ASSERT(inspector.IsRunning());
    TEST_ASSERT(inspector.GetWindowId() != 0);
    TEST_ASSERT(inspector.GetSurface() != nullptr);
    inspector.RefreshStats();
    inspector.Terminate();
    TEST_ASSERT(!inspector.IsRunning());

    // 2. Resource Viewer App
    auto bundle = std::make_shared<ResourceBundle>();
    ResourceItem txt_item{};
    txt_item.id = "readme";
    txt_item.type = ResourceType::kText;
    std::string txt_content = "Resource Viewer test text";
    txt_item.data.assign(txt_content.begin(), txt_content.end());
    bundle->AddResource(txt_item);

    auto res_mgr = std::make_shared<ResourceManager>(bundle, 11);
    ResourceViewerApp viewer(11, win_mgr, surf_mgr, res_mgr);
    TEST_ASSERT(viewer.Launch());
    TEST_ASSERT(viewer.IsRunning());
    TEST_ASSERT(viewer.GetLoadedText() == txt_content);
    viewer.Terminate();
    TEST_ASSERT(!viewer.IsRunning());

    // 3. Task Notes App
    auto iso = std::make_shared<IsolatedStorage>(vfs, "com.winios.tasknotes");
    auto storage_api = std::make_shared<GuestStorageApi>(iso, 12);
    auto prefs = std::make_shared<AppPreferences>(iso, "com.winios.tasknotes");

    TaskNotesApp notes(12, win_mgr, surf_mgr, storage_api, prefs);
    TEST_ASSERT(notes.Launch());
    TEST_ASSERT(notes.IsRunning());

    notes.AddTask("First Note");
    notes.AddTask("Second Note");
    TEST_ASSERT(notes.GetTasks().size() == 2);

    // Timer tick simulation
    winios_event_t ev_timer{};
    ev_timer.type = WINIOS_EVENT_TIMER;
    notes.HandleEvent(ev_timer);
    TEST_ASSERT(notes.GetTimerTickCount() == 1);

    notes.Terminate();
    TEST_ASSERT(!notes.IsRunning());

    // Verify persistence on relaunch
    TaskNotesApp notes_relaunch(12, win_mgr, surf_mgr, storage_api, prefs);
    TEST_ASSERT(notes_relaunch.Launch());
    TEST_ASSERT(notes_relaunch.GetTasks().size() == 2);
    TEST_ASSERT(notes_relaunch.GetTasks()[0] == "First Note");
    notes_relaunch.Terminate();

    std::cout << "[PASS] test_sample_apps_execution\n";
}

int main() {
    test_app_preferences_persistence();
    test_sdk_v12_abi_structures();
    test_sample_apps_execution();
    std::cout << "All Stage 217.19-217.21 SDK and sample app tests passed.\n";
    return 0;
}

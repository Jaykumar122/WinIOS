#include "session/multi_app_session_controller.h"
#include "loader/app_launcher.h"
#include "loader/embedded_campaign230_workloads.inl"
#include "packaging/app_package.h"
#include "packaging/app_registry.h"
#include "kernel/process_identity.h"
#include "settings/task_manager.h"
#include "platform/resource_budget.h"
#include "security/capability_manager.h"
#include "services/diagnostics_service.h"
#include "vfs/vfs.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <string>

using namespace winios;

static void register_test_app(vfs::VirtualFileSystem& vfs, packaging::ApplicationRegistry& reg,
                              const std::string& bundle_id, const std::string& name,
                              const std::vector<uint8_t>& bin) {
    const std::string app_dir = "/Applications/" + bundle_id + ".app";
    vfs.mkdir("/Applications");
    vfs.mkdir(app_dir);
    vfs.mkdir(app_dir + "/bin");

    const std::string full_exec = app_dir + "/bin/" + name;
    vfs.create_file(full_exec);
    vfs.write_file(full_exec, bin.data(), bin.size());

    packaging::AppRegistration r{};
    r.bundle_id = bundle_id;
    r.name = name;
    r.display_name = name + " Display";
    r.version = "1.0.0";
    r.exec_path = "bin/" + name;
    r.install_dir = app_dir;
    r.min_sdk_version = 0x00020400;
    r.permissions = {"SettingsRead"};
    reg.register_app(r);
}

static void test_multi_app_session_orchestration() {
    std::cout << "[Test 1] MultiAppSessionController launch, switch, and state preservation...\n";
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    auto registry = std::make_shared<packaging::ApplicationRegistry>();
    auto identity_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 200, 4);
    auto task_mgr = std::make_shared<settings::TaskManager>();
    auto budget_mgr = std::make_shared<platform::ResourceBudgetManager>();
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    auto diag_svc = std::make_shared<services::DiagnosticsService>();

    // Register 3 workloads
    register_test_app(*vfs, *registry, "com.winios.appstartup", "startup",
                      loader::get_embedded_campaign230_app_startup());
    register_test_app(*vfs, *registry, "com.winios.appsettings", "settings",
                      loader::get_embedded_campaign230_app_settings());
    register_test_app(*vfs, *registry, "com.winios.appdocs", "docs",
                      loader::get_embedded_campaign230_app_docs());

    auto launcher = std::make_shared<loader::AppLaunchPipeline>(
        vfs, registry, identity_mgr, task_mgr, budget_mgr, cap_mgr, diag_svc);

    session::MultiAppSessionController controller(launcher, identity_mgr, registry, task_mgr, diag_svc, 2);

    // 1. Launch App 1
    std::cout << "Step 1: Launch App 1..." << std::endl;
    auto r1 = controller.launch_app("com.winios.appstartup");
    assert(r1.status == loader::LaunchStatus::Success);
    uint32_t pid1 = r1.handle.pid;
    assert(controller.foreground_pid() == pid1);

    auto entry1 = controller.get_session_entry(pid1);
    assert(entry1.has_value());
    assert(entry1->state == session::AppSessionState::Foreground);

    // 2. Launch App 2
    std::cout << "Step 2: Launch App 2..." << std::endl;
    auto r2 = controller.launch_app("com.winios.appsettings");
    assert(r2.status == loader::LaunchStatus::Success);
    uint32_t pid2 = r2.handle.pid;
    assert(controller.foreground_pid() == pid2);

    // Verify App 1 transitioned to background
    entry1 = controller.get_session_entry(pid1);
    assert(entry1->state == session::AppSessionState::Background);

    // 3. Switch back to App 1
    std::cout << "Step 3: Switch back to App 1..." << std::endl;
    bool sw = controller.switch_to_app(pid1);
    assert(sw);
    (void)sw;
    assert(controller.foreground_pid() == pid1);
    assert(controller.get_session_entry(pid1)->state == session::AppSessionState::Foreground);
    assert(controller.get_session_entry(pid2)->state == session::AppSessionState::Background);

    // 4. Suspend App 1 with state snapshot
    std::cout << "Step 4: Suspend App 1..." << std::endl;
    std::vector<uint8_t> snap = {'D', 'A', 'T', 'A', 0x01, 0x02};
    bool susp = controller.suspend_app(pid1, snap);
    assert(susp);
    (void)susp;
    entry1 = controller.get_session_entry(pid1);
    assert(entry1->state == session::AppSessionState::Suspended);
    assert(entry1->saved_state_snapshot == snap);

    // Automatic MRU promotion: App 2 becomes foreground
    assert(controller.foreground_pid() == pid2);

    // 5. Resume App 1
    std::cout << "Step 5: Resume App 1..." << std::endl;
    bool res_ok = controller.resume_app(pid1);
    assert(res_ok);
    (void)res_ok;
    assert(controller.foreground_pid() == pid1);
    assert(controller.get_session_entry(pid1)->saved_state_snapshot == snap);

    // 6. Capacity limit test: Max active apps is 2.
    std::cout << "Step 6: Capacity limit test..." << std::endl;
    auto r3 = controller.launch_app("com.winios.appdocs");
    assert(r3.status == loader::LaunchStatus::Success);
    uint32_t pid3 = r3.handle.pid;
    assert(controller.foreground_pid() == pid3);

    auto entry2 = controller.get_session_entry(pid2);
    assert(entry2->state == session::AppSessionState::Suspended);

    // 7. Terminate foreground App 3 -> App 1 promoted to foreground
    std::cout << "Step 7: Terminate foreground App 3..." << std::endl;
    bool term_ok = controller.terminate_app(pid3);
    assert(term_ok);
    (void)term_ok;
    assert(!controller.get_session_entry(pid3).has_value());
    assert(controller.foreground_pid() == pid1);

    // Clean up remaining apps
    std::cout << "Step 8: Clean up remaining apps..." << std::endl;
    controller.terminate_app(pid1);
    controller.terminate_app(pid2);
    assert(controller.list_session_apps().empty());

    std::cout << "  -> MultiAppSessionController tests passed.\n";
}

int main() {
    std::cout << "=== Stage 230.18: Multi-Application Session Orchestration Tests ===\n";
    test_multi_app_session_orchestration();
    std::cout << "=== All Stage 230.18 Tests PASSED ===\n";
    return 0;
}

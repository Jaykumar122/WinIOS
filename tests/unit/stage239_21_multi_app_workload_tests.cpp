#include "tests/support/test_assert.h"
#include "session/multi_app_session_controller.h"
#include "loader/app_launcher.h"
#include "loader/embedded_campaign230_workloads.inl"
#include "packaging/app_registry.h"
#include "kernel/process_identity.h"
#include "settings/task_manager.h"
#include "platform/resource_budget.h"
#include "security/capability_manager.h"
#include "services/diagnostics_service.h"
#include "vfs/vfs.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace winios;

namespace {

void register_app_helper(vfs::VirtualFileSystem& vfs, packaging::ApplicationRegistry& reg,
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
    r.display_name = name + " Test";
    r.version = "1.0.0";
    r.exec_path = "bin/" + name;
    r.install_dir = app_dir;
    r.min_sdk_version = 0x00020400;
    r.permissions = {"SettingsRead"};
    reg.register_app(r);
}

void test_multi_app_capacity_and_switching() {
    std::cout << "[Test 1] Multi-App Capacity Enforcement & Foreground Switching...\n";
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    auto registry = std::make_shared<packaging::ApplicationRegistry>();
    auto identity_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 200, 4);
    auto task_mgr = std::make_shared<settings::TaskManager>();
    auto budget_mgr = std::make_shared<platform::ResourceBudgetManager>();
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    auto diag_svc = std::make_shared<services::DiagnosticsService>();

    register_app_helper(*vfs, *registry, "com.winios.app1", "app1",
                        loader::get_embedded_campaign230_app_startup());
    register_app_helper(*vfs, *registry, "com.winios.app2", "app2",
                        loader::get_embedded_campaign230_app_settings());
    register_app_helper(*vfs, *registry, "com.winios.app3", "app3",
                        loader::get_embedded_campaign230_app_docs());

    auto launcher = std::make_shared<loader::AppLaunchPipeline>(
        vfs, registry, identity_mgr, task_mgr, budget_mgr, cap_mgr, diag_svc);

    // Limit active apps to 2
    session::MultiAppSessionController controller(launcher, identity_mgr, registry, task_mgr, diag_svc, 2);

    auto r1 = controller.launch_app("com.winios.app1");
    TEST_ASSERT(r1.status == loader::LaunchStatus::Success);
    uint32_t pid1 = r1.handle.pid;
    TEST_ASSERT(controller.foreground_pid() == pid1);

    auto r2 = controller.launch_app("com.winios.app2");
    TEST_ASSERT(r2.status == loader::LaunchStatus::Success);
    uint32_t pid2 = r2.handle.pid;
    TEST_ASSERT(controller.foreground_pid() == pid2);

    // Verify app1 was backgrounded
    auto e1 = controller.get_session_entry(pid1);
    TEST_ASSERT(e1.has_value() && e1->state == session::AppSessionState::Background);

    // Launch app3 -> Active capacity is 2, so oldest background (app1) must be suspended!
    auto r3 = controller.launch_app("com.winios.app3");
    TEST_ASSERT(r3.status == loader::LaunchStatus::Success);
    uint32_t pid3 = r3.handle.pid;
    TEST_ASSERT(controller.foreground_pid() == pid3);

    e1 = controller.get_session_entry(pid1);
    TEST_ASSERT(e1.has_value() && e1->state == session::AppSessionState::Suspended);

    // Switch foreground back to app2
    TEST_ASSERT(controller.switch_to_app(pid2));
    TEST_ASSERT(controller.foreground_pid() == pid2);

    std::cout << "  -> Passed multi-app capacity & switching\n";
}

void test_crash_isolation_and_reclaim() {
    std::cout << "[Test 2] Crash Isolation & Resource Reclaim...\n";
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    auto registry = std::make_shared<packaging::ApplicationRegistry>();
    auto identity_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 200, 4);
    auto task_mgr = std::make_shared<settings::TaskManager>();
    auto budget_mgr = std::make_shared<platform::ResourceBudgetManager>();
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    auto diag_svc = std::make_shared<services::DiagnosticsService>();

    register_app_helper(*vfs, *registry, "com.winios.safe", "safe_app",
                        loader::get_embedded_campaign230_app_startup());
    register_app_helper(*vfs, *registry, "com.winios.crasher", "crasher_app",
                        loader::get_embedded_campaign230_app_settings());

    auto launcher = std::make_shared<loader::AppLaunchPipeline>(
        vfs, registry, identity_mgr, task_mgr, budget_mgr, cap_mgr, diag_svc);

    session::MultiAppSessionController controller(launcher, identity_mgr, registry, task_mgr, diag_svc, 4);

    auto r_safe = controller.launch_app("com.winios.safe");
    auto r_crash = controller.launch_app("com.winios.crasher");

    TEST_ASSERT(r_safe.status == loader::LaunchStatus::Success);
    TEST_ASSERT(r_crash.status == loader::LaunchStatus::Success);

    uint32_t pid_safe = r_safe.handle.pid;
    uint32_t pid_crash = r_crash.handle.pid;

    // Simulate crash/termination of crasher_app
    TEST_ASSERT(controller.terminate_app(pid_crash));

    // Crasher is terminated and removed from session
    auto e_crash = controller.get_session_entry(pid_crash);
    TEST_ASSERT(!e_crash.has_value(), "Terminated app must be removed from active session");

    // Safe app must be promoted back to foreground and remain completely healthy
    TEST_ASSERT(controller.foreground_pid() == pid_safe);
    auto e_safe = controller.get_session_entry(pid_safe);
    TEST_ASSERT(e_safe.has_value() && e_safe->state == session::AppSessionState::Foreground);

    std::cout << "  -> Passed crash isolation & resource reclaim\n";
}

} // namespace

int main() {
    std::cout << "========================================================\n";
    std::cout << " Stage 239.21: Multi-App Integration & Isolation Suite  \n";
    std::cout << "========================================================\n";

    test_multi_app_capacity_and_switching();
    test_crash_isolation_and_reclaim();

    std::cout << "\nALL STAGE 239.21 TESTS COMPLETED SUCCESSFULLY!\n";
    return 0;
}

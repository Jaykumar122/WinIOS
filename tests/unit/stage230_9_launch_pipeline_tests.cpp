#include "loader/app_launcher.h"
#include "loader/embedded_campaign229_workloads.inl"
#include <iostream>
#include <vector>
#include <cstring>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;

static void setup_test_app_in_vfs(vfs::VirtualFileSystem& vfs, const std::string& bundle_id, const std::string& exec_name) {
    const std::string app_dir = "/Applications/" + bundle_id + ".app";
    vfs.mkdir("/Applications");
    vfs.mkdir(app_dir);
    vfs.mkdir(app_dir + "/bin");

    const auto& bin = loader::get_embedded_campaign229_dev_diag();
    const std::string full_exec = app_dir + "/" + exec_name;
    vfs.create_file(full_exec);
    vfs.write_file(full_exec, bin.data(), bin.size());
}

static void test_successful_launch_and_cleanup() {
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    auto registry = std::make_shared<packaging::ApplicationRegistry>();
    auto identity_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 200, 4);
    auto task_mgr = std::make_shared<settings::TaskManager>();
    auto budget_mgr = std::make_shared<platform::ResourceBudgetManager>();
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    auto diag_svc = std::make_shared<services::DiagnosticsService>();

    const std::string bundle_id = "com.winios.validapp";
    setup_test_app_in_vfs(*vfs, bundle_id, "bin/validapp");

    packaging::AppRegistration reg{};
    reg.bundle_id = bundle_id;
    reg.name = "ValidApp";
    reg.display_name = "Valid Application";
    reg.version = "1.0.0";
    reg.exec_path = "bin/validapp";
    reg.install_dir = "/Applications/" + bundle_id + ".app";
    reg.min_sdk_version = 0x00020300;
    reg.permissions = {"SettingsRead"};
    registry->register_app(reg);

    loader::AppLaunchPipeline pipeline(vfs, registry, identity_mgr, task_mgr, budget_mgr, cap_mgr, diag_svc);

    loader::LaunchResult res = pipeline.launch(bundle_id, 1000);
    if (res.status != loader::LaunchStatus::Success) {
        std::cerr << "FAILED: " << res.rejection_reason << " (status=" << static_cast<uint32_t>(res.status) << ")" << std::endl;
    }
    TEST_ASSERT(res.status == loader::LaunchStatus::Success);
    TEST_ASSERT(res.handle.is_valid());
    TEST_ASSERT(res.handle.pid == 100);
    TEST_ASSERT(identity_mgr->is_valid_handle(res.handle));
    TEST_ASSERT(task_mgr->get_task_count() == 1);
    TEST_ASSERT(budget_mgr->get_task_memory(res.handle.pid) > 0);
    TEST_ASSERT(cap_mgr->check(res.handle.pid, security::Capability::SettingsRead));

    // Verify diagnostics recorded
    auto logs = diag_svc->get_logs();
    TEST_ASSERT(!logs.empty());
    TEST_ASSERT(logs[0].level == services::LogLevel::Info);

    // Clean termination
    bool term_ok = pipeline.terminate(res.handle);
    TEST_ASSERT(term_ok);
    TEST_ASSERT(!identity_mgr->is_valid_handle(res.handle));
    TEST_ASSERT(task_mgr->get_task_count() == 0);
    TEST_ASSERT(budget_mgr->get_task_memory(res.handle.pid) == 0);
    TEST_ASSERT(!cap_mgr->check(res.handle.pid, security::Capability::SettingsRead));

    std::cout << "[PASS] test_successful_launch_and_cleanup\n";
}

static void test_launch_rejections_and_rollback() {
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    auto registry = std::make_shared<packaging::ApplicationRegistry>();
    auto identity_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 200, 4);
    auto task_mgr = std::make_shared<settings::TaskManager>();
    auto budget_mgr = std::make_shared<platform::ResourceBudgetManager>();
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    auto diag_svc = std::make_shared<services::DiagnosticsService>();

    loader::AppLaunchPipeline pipeline(vfs, registry, identity_mgr, task_mgr, budget_mgr, cap_mgr, diag_svc);

    // 1. Not installed
    auto res1 = pipeline.launch("com.winios.missing");
    TEST_ASSERT(res1.status == loader::LaunchStatus::AppNotInstalled);

    // 2. Corrupted (registered but VFS directory does not exist)
    packaging::AppRegistration reg_corrupt{};
    reg_corrupt.bundle_id = "com.winios.corrupt";
    reg_corrupt.name = "Corrupt";
    reg_corrupt.exec_path = "bin/corrupt";
    reg_corrupt.install_dir = "/Applications/com.winios.corrupt.app";
    registry->register_app(reg_corrupt);

    auto res2 = pipeline.launch("com.winios.corrupt");
    TEST_ASSERT(res2.status == loader::LaunchStatus::InstallationCorrupted);
    TEST_ASSERT(registry->get_installation_state("com.winios.corrupt") == packaging::InstallationState::Corrupted);

    // 3. SDK mismatch (app requires SDK 2.5, platform has 2.4)
    setup_test_app_in_vfs(*vfs, "com.winios.futuristic", "bin/futuristic");
    packaging::AppRegistration reg_future{};
    reg_future.bundle_id = "com.winios.futuristic";
    reg_future.name = "Futuristic";
    reg_future.exec_path = "bin/futuristic";
    reg_future.install_dir = "/Applications/com.winios.futuristic.app";
    reg_future.min_sdk_version = 0x00020500;
    registry->register_app(reg_future);

    auto res3 = pipeline.launch("com.winios.futuristic");
    TEST_ASSERT(res3.status == loader::LaunchStatus::CompatibilityMismatch);
    TEST_ASSERT(identity_mgr->active_process_count() == 0); // No PID leaked
    TEST_ASSERT(task_mgr->get_task_count() == 0); // No task leaked

    // 4. Capability denied (requests SystemAdmin)
    setup_test_app_in_vfs(*vfs, "com.winios.malicious", "bin/malicious");
    packaging::AppRegistration reg_mal{};
    reg_mal.bundle_id = "com.winios.malicious";
    reg_mal.name = "Malicious";
    reg_mal.exec_path = "bin/malicious";
    reg_mal.install_dir = "/Applications/com.winios.malicious.app";
    reg_mal.min_sdk_version = 0x00020300;
    reg_mal.permissions = {"SystemAdmin"};
    registry->register_app(reg_mal);

    auto res4 = pipeline.launch("com.winios.malicious");
    TEST_ASSERT(res4.status == loader::LaunchStatus::CapabilityDenied);
    TEST_ASSERT(identity_mgr->active_process_count() == 0);
    TEST_ASSERT(task_mgr->get_task_count() == 0);

    std::cout << "[PASS] test_launch_rejections_and_rollback\n";
}

int main() {
    std::cout << "=== Running Stage 230.9 Launch Pipeline Tests ===" << std::endl;
    test_successful_launch_and_cleanup();
    test_launch_rejections_and_rollback();
    std::cout << "=== All Stage 230.9 Tests Passed Successfully ===" << std::endl;
    return 0;
}

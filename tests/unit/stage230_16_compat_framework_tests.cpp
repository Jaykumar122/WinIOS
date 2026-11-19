#include "testing/app_compatibility_framework.h"
#include "loader/app_launcher.h"
#include "loader/embedded_campaign230_workloads.inl"
#include "packaging/app_package.h"
#include "packaging/app_registry.h"
#include "kernel/process_identity.h"
#include "settings/task_manager.h"
#include "settings/system_state_manager.h"
#include "platform/resource_budget.h"
#include "security/capability_manager.h"
#include "services/diagnostics_service.h"
#include "vfs/vfs.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <string>

using namespace winios;

static void setup_test_app(vfs::VirtualFileSystem& vfs, packaging::ApplicationRegistry& reg, const std::string& bundle_id) {
    const std::string app_dir = "/Applications/" + bundle_id + ".app";
    vfs.mkdir("/Applications");
    vfs.mkdir(app_dir);
    vfs.mkdir(app_dir + "/bin");

    const auto& bin = loader::get_embedded_campaign230_app_startup();
    const std::string full_exec = app_dir + "/bin/app";
    vfs.create_file(full_exec);
    vfs.write_file(full_exec, bin.data(), bin.size());

    packaging::AppRegistration r{};
    r.bundle_id = bundle_id;
    r.name = "StartupApp";
    r.display_name = "Startup Application";
    r.version = "1.0.0";
    r.exec_path = "bin/app";
    r.install_dir = app_dir;
    r.min_sdk_version = 0x00020400;
    r.permissions = {"SettingsRead"};
    reg.register_app(r);
}

static void test_compatibility_framework() {
    std::cout << "[Test 1] AppCompatibilityFramework scenario execution...\n";
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    auto registry = std::make_shared<packaging::ApplicationRegistry>();
    auto identity_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 200, 4);
    auto task_mgr = std::make_shared<settings::TaskManager>();
    auto budget_mgr = std::make_shared<platform::ResourceBudgetManager>();
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    auto diag_svc = std::make_shared<services::DiagnosticsService>();
    auto state_mgr = std::make_shared<settings::SystemStateManager>();

    const std::string bid = "com.winios.compatdemo";
    setup_test_app(*vfs, *registry, bid);

    auto launcher = std::make_shared<loader::AppLaunchPipeline>(
        vfs, registry, identity_mgr, task_mgr, budget_mgr, cap_mgr, diag_svc);

    testing::AppCompatibilityFramework framework(launcher, diag_svc, state_mgr, task_mgr);

    // Case 1: Pass scenario
    {
        testing::AppTestExpectations exp{};
        exp.expected_exit_code = 101;
        exp.required_diag_substrings = {"Successfully launched com.winios.compatdemo"};

        auto res = framework.run_scenario(bid, exp, [](kernel::ProcessHandle h) {
            (void)h;
            return 101;
        });
        assert(res.passed);
        assert(res.actual_exit_code == 101);
    }

    // Case 2: Exit code mismatch failure
    {
        testing::AppTestExpectations exp{};
        exp.expected_exit_code = 0; // expected 0, but app returns 101

        auto res = framework.run_scenario(bid, exp, [](kernel::ProcessHandle h) {
            (void)h;
            return 101;
        });
        assert(!res.passed);
        assert(res.actual_exit_code == 101);
        assert(res.failure_reason.find("Exit code mismatch") != std::string::npos);
    }

    // Case 3: Missing required diagnostic log failure
    {
        testing::AppTestExpectations exp{};
        exp.expected_exit_code = 101;
        exp.required_diag_substrings = {"NONEXISTENT_DIAGNOSTIC_LOG_STRING"};

        auto res = framework.run_scenario(bid, exp, [](kernel::ProcessHandle h) {
            (void)h;
            return 101;
        });
        assert(!res.passed);
        assert(res.failure_reason.find("Missing required diagnostic log substring") != std::string::npos);
    }

    // Case 4: State callback verification
    {
        testing::AppTestExpectations exp{};
        exp.expected_exit_code = 101;
        exp.verify_state = [](const settings::SystemStateManager& sm) {
            (void)sm;
            return true;
        };

        auto res = framework.run_scenario(bid, exp, [](kernel::ProcessHandle h) {
            (void)h;
            return 101;
        });
        assert(res.passed);
    }

    std::cout << "  -> Compatibility framework tests passed.\n";
}

int main() {
    std::cout << "=== Stage 230.16: Application Compatibility Test Framework Tests ===\n";
    test_compatibility_framework();
    std::cout << "=== All Stage 230.16 Tests PASSED ===\n";
    return 0;
}

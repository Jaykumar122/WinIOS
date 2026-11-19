#include "tools/winios_app_cli.h"
#include "tools/app_generator.h"
#include "loader/app_launcher.h"
#include "loader/embedded_campaign229_workloads.inl"
#include "packaging/app_package.h"
#include "packaging/app_registry.h"
#include "packaging/package_installer.h"
#include "kernel/process_identity.h"
#include "settings/task_manager.h"
#include "platform/resource_budget.h"
#include "security/capability_manager.h"
#include "services/diagnostics_service.h"
#include "vfs/vfs.h"

#include <cassert>
#include <iostream>
#include <sstream>
#include <memory>
#include <string>

using namespace winios;

static void test_app_generator() {
    std::cout << "[Test 1] AppGenerator template creation...\n";
    tools::AppTemplateConfig cfg{};
    cfg.bundle_id = "com.winios.testgen";
    cfg.app_name = "TestGenApp";
    cfg.display_name = "Test Generator App";
    cfg.version = "1.0.0";
    cfg.permissions = {"SettingsRead", "DiagnosticsWrite"};
    cfg.supported_doc_types = {"text/plain", "application/json"};

    std::string src = tools::AppGenerator::generate_c_source(cfg);
    assert(src.find("TestGenApp") != std::string::npos);
    assert(src.find("com.winios.testgen") != std::string::npos);
    assert(src.find("0x20400") != std::string::npos || src.find("0x00020400") != std::string::npos);

    std::string manifest_str = tools::AppGenerator::generate_manifest_ini(cfg);
    assert(manifest_str.find("bundle_id=com.winios.testgen") != std::string::npos);
    assert(manifest_str.find("display_name=Test Generator App") != std::string::npos);
    assert(manifest_str.find("target_abi_version=2.4") != std::string::npos);

    std::string build_scr = tools::AppGenerator::generate_build_script(cfg);
    assert(build_scr.find("TestGenApp") != std::string::npos);

    auto dev_diag_bin = loader::get_embedded_campaign229_dev_diag();
    assert(!dev_diag_bin.empty());

    auto pkg = tools::AppGenerator::generate_package(cfg, dev_diag_bin);
    assert(pkg.manifest.bundle_id == "com.winios.testgen");
    assert(pkg.manifest.files.size() == 2);
    assert(pkg.manifest.format_version == "3.0");

    auto packed = pkg.pack_v3();
    assert(!packed.empty());

    packaging::AppPackage unpacked{};
    auto val = packaging::AppPackage::validate_package_data(packed.data(), packed.size(), &unpacked);
    assert(val == packaging::PackageValidationResult::Valid);
    (void)val;
    assert(unpacked.manifest.bundle_id == "com.winios.testgen");
    std::cout << "  -> AppGenerator passed.\n";
}

static void test_cli_execution() {
    std::cout << "[Test 2] WiniosAppCli end-to-end command execution...\n";
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->mkdir("/Applications");
    vfs->mkdir("/tmp");

    auto registry = std::make_shared<packaging::ApplicationRegistry>();
    auto identity_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 200, 4);
    auto task_mgr = std::make_shared<settings::TaskManager>();
    auto budget_mgr = std::make_shared<platform::ResourceBudgetManager>();
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    auto diag_svc = std::make_shared<services::DiagnosticsService>();
    auto installer = std::make_shared<packaging::PackageInstaller>(vfs, registry);

    auto launcher = std::make_shared<loader::AppLaunchPipeline>(
        vfs, registry, identity_mgr, task_mgr, budget_mgr, cap_mgr, diag_svc);

    tools::WiniosAppCli cli(vfs, registry, installer, launcher, identity_mgr, diag_svc);

    // 1. Help
    {
        std::ostringstream out, err;
        int rc = cli.execute({"help"}, out, err);
        assert(rc == 0);
        (void)rc;
        assert(out.str().find("WinIOS Application Developer CLI") != std::string::npos);
    }

    // 2. Generate package and write to /tmp/cliapp.wios
    tools::AppTemplateConfig cfg{};
    cfg.bundle_id = "com.winios.cliapp";
    cfg.app_name = "cliapp";
    cfg.display_name = "CLI Test App";
    cfg.version = "1.0.0";
    auto dev_diag_bin = loader::get_embedded_campaign229_dev_diag();
    auto pkg = tools::AppGenerator::generate_package(cfg, dev_diag_bin);
    auto packed = pkg.pack_v3();
    vfs->create_file("/tmp/cliapp.wios");
    vfs->write_file("/tmp/cliapp.wios", packed.data(), packed.size());

    // 3. pkg-inspect
    {
        std::ostringstream out, err;
        int rc = cli.execute({"pkg-inspect", "/tmp/cliapp.wios"}, out, err);
        assert(rc == 0);
        (void)rc;
        assert(out.str().find("com.winios.cliapp") != std::string::npos);
        assert(out.str().find("CLI Test App") != std::string::npos);
    }

    // 4. pkg-validate
    {
        std::ostringstream out, err;
        int rc = cli.execute({"pkg-validate", "/tmp/cliapp.wios"}, out, err);
        assert(rc == 0);
        (void)rc;
        assert(out.str().find("Integrity:  PASSED") != std::string::npos);
    }

    // 5. app-install
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-install", "/tmp/cliapp.wios"}, out, err);
        assert(rc == 0);
        (void)rc;
        assert(out.str().find("Successfully installed com.winios.cliapp") != std::string::npos);
    }

    // 6. app-list
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-list"}, out, err);
        assert(rc == 0);
        (void)rc;
        assert(out.str().find("com.winios.cliapp") != std::string::npos);
    }

    // 7. app-info
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-info", "com.winios.cliapp"}, out, err);
        assert(rc == 0);
        (void)rc;
        assert(out.str().find("Target ABI:   2.4") != std::string::npos);
        assert(out.str().find("CLI Test App") != std::string::npos);
    }

    // 8. app-status before launch
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-status", "com.winios.cliapp"}, out, err);
        assert(rc == 0);
        (void)rc;
        assert(out.str().find("STOPPED") != std::string::npos);
    }

    // 9. app-launch
    uint32_t launched_pid = 0;
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-launch", "com.winios.cliapp"}, out, err);
        assert(rc == 0);
        (void)rc;
        assert(out.str().find("Launched com.winios.cliapp") != std::string::npos);
        // Extract PID
        auto str = out.str();
        auto pos = str.find("PID ");
        assert(pos != std::string::npos);
        launched_pid = static_cast<uint32_t>(std::stoul(str.substr(pos + 4)));
        assert(launched_pid >= 100);
    }

    // 10. app-status after launch
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-status", "com.winios.cliapp"}, out, err);
        assert(rc == 0);
        (void)rc;
        assert(out.str().find("RUNNING") != std::string::npos);
    }

    // 11. diag-query
    {
        std::ostringstream out, err;
        int rc = cli.execute({"diag-query"}, out, err);
        assert(rc == 0);
        (void)rc;
        assert(out.str().find("Diagnostics Logs") != std::string::npos);
    }

    // 12. app-terminate
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-terminate", std::to_string(launched_pid)}, out, err);
        assert(rc == 0);
        (void)rc;
        assert(out.str().find("Terminated process") != std::string::npos);
    }

    // 13. app-uninstall
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-uninstall", "com.winios.cliapp", "--purge"}, out, err);
        assert(rc == 0);
        (void)rc;
        assert(out.str().find("Successfully uninstalled com.winios.cliapp") != std::string::npos);
    }

    // 14. app-info after uninstall (should fail)
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-info", "com.winios.cliapp"}, out, err);
        assert(rc == 1);
        (void)rc;
        assert(err.str().find("not found") != std::string::npos);
    }

    std::cout << "  -> WiniosAppCli passed.\n";
}

int main() {
    std::cout << "=== Stage 230.10: Developer CLI & App Generator Tests ===\n";
    test_app_generator();
    test_cli_execution();
    std::cout << "=== All Stage 230.10 Tests PASSED ===\n";
    return 0;
}

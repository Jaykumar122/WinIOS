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
#include <vector>

using namespace winios;
using namespace winios::tools;

static void test_help_and_empty() {
    std::cout << "[Test 1] Help and empty command execution...\n";
    WiniosAppCli cli(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    {
        std::ostringstream out, err;
        int rc = cli.execute({}, out, err);
        assert(rc == CliExitCode::Success);
        (void)rc;
        assert(out.str().find("WinIOS Application Developer CLI") != std::string::npos);
    }
    {
        std::ostringstream out, err;
        int rc = cli.execute({"help"}, out, err);
        assert(rc == CliExitCode::Success);
        (void)rc;
        assert(out.str().find("Commands:") != std::string::npos);
    }
    {
        std::ostringstream out, err;
        int rc = cli.execute({"--help"}, out, err);
        assert(rc == CliExitCode::Success);
        (void)rc;
    }
    {
        std::ostringstream out, err;
        int rc = cli.execute({"-h"}, out, err);
        assert(rc == CliExitCode::Success);
        (void)rc;
    }
    std::cout << "  -> Passed.\n";
}

static void test_argument_bounds_and_unknown_commands() {
    std::cout << "[Test 2] Argument bounds and unknown commands...\n";
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    auto registry = std::make_shared<packaging::ApplicationRegistry>();
    auto installer = std::make_shared<packaging::PackageInstaller>(vfs, registry);
    auto identity_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 200, 4);
    auto task_mgr = std::make_shared<settings::TaskManager>();
    auto budget_mgr = std::make_shared<platform::ResourceBudgetManager>();
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    auto diag_svc = std::make_shared<services::DiagnosticsService>();
    auto launcher = std::make_shared<loader::AppLaunchPipeline>(
        vfs, registry, identity_mgr, task_mgr, budget_mgr, cap_mgr, diag_svc);

    WiniosAppCli cli(vfs, registry, installer, launcher, identity_mgr, diag_svc);

    // Unknown command
    {
        std::ostringstream out, err;
        int rc = cli.execute({"nonexistent_cmd"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
        assert(err.str().find("Unknown command") != std::string::npos);
    }

    // app-list with invalid option
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-list", "--invalid-option"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
        assert(err.str().find("Unknown option") != std::string::npos);
    }

    // app-list with too many arguments
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-list", "--active-only", "extra_arg"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
        assert(err.str().find("Usage: app-list") != std::string::npos);
    }

    // app-info missing argument
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-info"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
        assert(err.str().find("Usage: app-info") != std::string::npos);
    }

    // app-info too many arguments
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-info", "com.winios.app", "extra"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
    }

    // app-status missing argument
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-status"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
    }

    // pkg-inspect missing argument
    {
        std::ostringstream out, err;
        int rc = cli.execute({"pkg-inspect"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
    }

    // app-install missing argument
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-install"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
    }

    // app-uninstall missing argument
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-uninstall"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
    }

    // app-uninstall invalid flag
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-uninstall", "com.winios.app", "--force-delete"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
        assert(err.str().find("Unknown option") != std::string::npos);
    }

    // app-launch missing argument
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-launch"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
    }

    // app-terminate missing argument
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-terminate"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
    }

    std::cout << "  -> Passed.\n";
}

static void test_bundle_id_security() {
    std::cout << "[Test 3] Bundle identifier security and validation...\n";
    auto registry = std::make_shared<packaging::ApplicationRegistry>();
    WiniosAppCli cli(nullptr, registry, nullptr, nullptr, nullptr, nullptr);

    // Test validator directly
    assert(WiniosAppCli::is_valid_bundle_id("com.winios.calc"));
    assert(WiniosAppCli::is_valid_bundle_id("org.example.my-app_1"));
    assert(!WiniosAppCli::is_valid_bundle_id(""));
    assert(!WiniosAppCli::is_valid_bundle_id("ab")); // too short
    assert(!WiniosAppCli::is_valid_bundle_id("singleword")); // no dot
    assert(!WiniosAppCli::is_valid_bundle_id(".leadingdot.app"));
    assert(!WiniosAppCli::is_valid_bundle_id("trailingdot.app."));
    assert(!WiniosAppCli::is_valid_bundle_id("consecutive..dot"));
    assert(!WiniosAppCli::is_valid_bundle_id("com.bad/path")); // path injection
    assert(!WiniosAppCli::is_valid_bundle_id("com.bad;rm"));   // shell injection
    assert(!WiniosAppCli::is_valid_bundle_id("com.bad`cmd`"));

    // CLI rejection of bad bundle id
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-info", "invalid_no_dot"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
        assert(err.str().find("Invalid bundle identifier syntax") != std::string::npos);
    }
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-info", "../../etc/passwd"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
        assert(err.str().find("Invalid bundle identifier syntax") != std::string::npos);
    }
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-status", "com.bad;rm"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
        assert(err.str().find("Invalid bundle identifier syntax") != std::string::npos);
    }
    std::cout << "  -> Passed.\n";
}

static void test_path_traversal_and_canonicalization() {
    std::cout << "[Test 4] Path traversal and canonicalization defense...\n";
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->mkdir("/tmp");
    auto registry = std::make_shared<packaging::ApplicationRegistry>();
    auto installer = std::make_shared<packaging::PackageInstaller>(vfs, registry);
    WiniosAppCli cli(vfs, registry, installer, nullptr, nullptr, nullptr);

    // 1. Directory traversal attempt
    {
        std::ostringstream out, err;
        int rc = cli.execute({"pkg-inspect", "/tmp/../../Applications/secret.wios"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
        assert(err.str().find("Invalid package path") != std::string::npos);
        assert(err.str().find("DirectoryTraversal") != std::string::npos);
    }

    // 2. Windows reserved device name
    {
        std::ostringstream out, err;
        int rc = cli.execute({"pkg-inspect", "/tmp/CON.wios"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
        assert(err.str().find("WindowsReservedDevice") != std::string::npos);
    }

    // 3. Drive letter attempt
    {
        std::ostringstream out, err;
        int rc = cli.execute({"pkg-inspect", "C:/tmp/test.wios"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
        assert(err.str().find("DriveLetterForbidden") != std::string::npos);
    }

    // 4. UNC network path attempt
    {
        std::ostringstream out, err;
        int rc = cli.execute({"pkg-inspect", "//server/share/test.wios"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
        assert(err.str().find("UncPathForbidden") != std::string::npos);
    }

    // 5. Alternate data stream attempt
    {
        std::ostringstream out, err;
        int rc = cli.execute({"pkg-inspect", "/tmp/file.wios:stream"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
        assert(err.str().find("AlternateDataStreamForbidden") != std::string::npos);
    }

    // 6. Relative path (not starting with '/')
    {
        std::ostringstream out, err;
        int rc = cli.execute({"pkg-inspect", "relative/path/test.wios"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
        assert(err.str().find("DirectoryTraversal") != std::string::npos);
    }

    // 7. Non-existent valid path returns ExecutionFailure (2)
    {
        std::ostringstream out, err;
        int rc = cli.execute({"pkg-inspect", "/tmp/nonexistent.wios"}, out, err);
        assert(rc == CliExitCode::ExecutionFailure);
        (void)rc;
        assert(err.str().find("Failed to read package file") != std::string::npos);
    }

    std::cout << "  -> Passed.\n";
}

static void test_pid_parsing_and_termination() {
    std::cout << "[Test 5] PID parsing and process termination defense...\n";
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    auto registry = std::make_shared<packaging::ApplicationRegistry>();
    auto identity_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 200, 4);
    auto task_mgr = std::make_shared<settings::TaskManager>();
    auto budget_mgr = std::make_shared<platform::ResourceBudgetManager>();
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    auto diag_svc = std::make_shared<services::DiagnosticsService>();
    auto launcher = std::make_shared<loader::AppLaunchPipeline>(
        vfs, registry, identity_mgr, task_mgr, budget_mgr, cap_mgr, diag_svc);

    WiniosAppCli cli(vfs, registry, nullptr, launcher, identity_mgr, diag_svc);

    // Test parse_pid helper
    uint32_t p = 0;
    assert(WiniosAppCli::parse_pid("100", p) && p == 100);
    assert(WiniosAppCli::parse_pid("4294967295", p) && p == 0xFFFFFFFFUL);
    assert(!WiniosAppCli::parse_pid("", p));
    assert(!WiniosAppCli::parse_pid("0", p));
    assert(!WiniosAppCli::parse_pid("-10", p));
    assert(!WiniosAppCli::parse_pid("100abc", p));
    assert(!WiniosAppCli::parse_pid("abc100", p));
    assert(!WiniosAppCli::parse_pid("9999999999999999", p)); // overflow
    (void)p;

    // Non-numeric PID argument
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-terminate", "not_a_pid"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
        assert(err.str().find("Invalid PID") != std::string::npos);
    }

    // Zero PID
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-terminate", "0"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
        assert(err.str().find("Invalid PID") != std::string::npos);
    }

    // Non-existent PID returns ExecutionFailure (2)
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-terminate", "999"}, out, err);
        assert(rc == CliExitCode::ExecutionFailure);
        (void)rc;
        assert(err.str().find("No active process found") != std::string::npos);
    }

    std::cout << "  -> Passed.\n";
}

static void test_service_unavailable_handling() {
    std::cout << "[Test 6] Service unavailable (null subsystem) handling...\n";
    // Completely null CLI
    WiniosAppCli null_cli(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    {
        std::ostringstream out, err;
        int rc = null_cli.execute({"app-list"}, out, err);
        assert(rc == CliExitCode::ServiceUnavailable);
        (void)rc;
        assert(err.str().find("Application registry not available") != std::string::npos);
    }
    {
        std::ostringstream out, err;
        int rc = null_cli.execute({"app-info", "com.winios.app"}, out, err);
        assert(rc == CliExitCode::ServiceUnavailable);
        (void)rc;
    }
    {
        std::ostringstream out, err;
        int rc = null_cli.execute({"app-status", "com.winios.app"}, out, err);
        assert(rc == CliExitCode::ServiceUnavailable);
        (void)rc;
    }
    {
        std::ostringstream out, err;
        int rc = null_cli.execute({"pkg-inspect", "/tmp/pkg.wios"}, out, err);
        assert(rc == CliExitCode::ServiceUnavailable);
        (void)rc;
    }
    {
        std::ostringstream out, err;
        int rc = null_cli.execute({"app-install", "/tmp/pkg.wios"}, out, err);
        assert(rc == CliExitCode::ServiceUnavailable);
        (void)rc;
    }
    {
        std::ostringstream out, err;
        int rc = null_cli.execute({"app-uninstall", "com.winios.app"}, out, err);
        assert(rc == CliExitCode::ServiceUnavailable);
        (void)rc;
    }
    {
        std::ostringstream out, err;
        int rc = null_cli.execute({"app-launch", "com.winios.app"}, out, err);
        assert(rc == CliExitCode::ServiceUnavailable);
        (void)rc;
    }
    {
        std::ostringstream out, err;
        int rc = null_cli.execute({"app-terminate", "101"}, out, err);
        assert(rc == CliExitCode::ServiceUnavailable);
        (void)rc;
    }
    {
        std::ostringstream out, err;
        int rc = null_cli.execute({"diag-query"}, out, err);
        assert(rc == CliExitCode::ServiceUnavailable);
        (void)rc;
    }

    std::cout << "  -> Passed.\n";
}

static void test_alias_and_diag_options() {
    std::cout << "[Test 7] Command alias (app-inspect) and diag-query options...\n";
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->mkdir("/tmp");
    auto registry = std::make_shared<packaging::ApplicationRegistry>();
    auto diag_svc = std::make_shared<services::DiagnosticsService>();
    diag_svc->log("test", 100, services::LogLevel::Info, "Diagnostic entry 1");
    diag_svc->log("test", 100, services::LogLevel::Warning, "Diagnostic entry 2");

    WiniosAppCli cli(vfs, registry, nullptr, nullptr, nullptr, diag_svc);

    // Diag query without args
    {
        std::ostringstream out, err;
        int rc = cli.execute({"diag-query"}, out, err);
        assert(rc == CliExitCode::Success);
        (void)rc;
        assert(out.str().find("Diagnostics Logs (2)") != std::string::npos);
    }

    // Diag query with limit
    {
        std::ostringstream out, err;
        int rc = cli.execute({"diag-query", "--limit", "1"}, out, err);
        assert(rc == CliExitCode::Success);
        (void)rc;
        assert(out.str().find("Diagnostics Logs (1)") != std::string::npos);
    }

    // Diag query invalid limit
    {
        std::ostringstream out, err;
        int rc = cli.execute({"diag-query", "--limit", "0"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
    }
    {
        std::ostringstream out, err;
        int rc = cli.execute({"diag-query", "--bad-opt"}, out, err);
        assert(rc == CliExitCode::InvalidArgs);
        (void)rc;
    }

    // Create a dummy valid package to test app-inspect alias
    tools::AppTemplateConfig cfg{};
    cfg.bundle_id = "com.winios.aliasapp";
    cfg.app_name = "aliasapp";
    cfg.display_name = "Alias Test App";
    cfg.version = "1.0.0";
    auto dev_diag_bin = loader::get_embedded_campaign229_dev_diag();
    auto pkg = tools::AppGenerator::generate_package(cfg, dev_diag_bin);
    auto packed = pkg.pack_v3();
    vfs->create_file("/tmp/alias.wios");
    vfs->write_file("/tmp/alias.wios", packed.data(), packed.size());

    // Test app-inspect alias
    {
        std::ostringstream out, err;
        int rc = cli.execute({"app-inspect", "/tmp/alias.wios"}, out, err);
        assert(rc == CliExitCode::Success);
        (void)rc;
        assert(out.str().find("Package Inspection: /tmp/alias.wios") != std::string::npos);
        assert(out.str().find("com.winios.aliasapp") != std::string::npos);
    }

    std::cout << "  -> Passed.\n";
}

int main() {
    std::cout << "=== Stage 231.17: Developer CLI Security and Reliability Tests ===\n";
    test_help_and_empty();
    test_argument_bounds_and_unknown_commands();
    test_bundle_id_security();
    test_path_traversal_and_canonicalization();
    test_pid_parsing_and_termination();
    test_service_unavailable_handling();
    test_alias_and_diag_options();
    std::cout << "=== All Stage 231.17 Tests PASSED ===\n";
    return 0;
}

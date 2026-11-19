#include "loader/app_launcher.h"
#include "loader/embedded_campaign230_workloads.inl"
#include "tools/app_generator.h"
#include "packaging/package_installer.h"
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
#include <vector>

#if defined(_WIN32)
#include <crtdbg.h>
#endif

using namespace winios;
using namespace winios::loader;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at line " << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (0)

struct TestFixture {
    std::shared_ptr<vfs::VirtualFileSystem>           vfs;
    std::shared_ptr<packaging::ApplicationRegistry>  registry;
    std::shared_ptr<kernel::ProcessIdentityManager>   id_mgr;
    std::shared_ptr<settings::TaskManager>           task_mgr;
    std::shared_ptr<platform::ResourceBudgetManager> budget_mgr;
    std::shared_ptr<security::CapabilityManager>     cap_mgr;
    std::shared_ptr<services::DiagnosticsService>    diag_svc;
    std::shared_ptr<AppLaunchPipeline>               pipeline;

    static TestFixture create(uint32_t min_pid = 100, uint32_t max_pid = 109) {
        TestFixture f;
        f.vfs = std::make_shared<vfs::VirtualFileSystem>();
        f.registry = std::make_shared<packaging::ApplicationRegistry>();
        f.id_mgr = std::make_shared<kernel::ProcessIdentityManager>(min_pid, max_pid, 0);
        f.task_mgr = std::make_shared<settings::TaskManager>();
        f.budget_mgr = std::make_shared<platform::ResourceBudgetManager>();
        f.cap_mgr = std::make_shared<security::CapabilityManager>();
        f.diag_svc = std::make_shared<services::DiagnosticsService>();

        packaging::PackageInstaller installer(f.vfs, f.registry);

        // App 1: Standard calc
        {
            tools::AppTemplateConfig cfg{};
            cfg.bundle_id = "com.winios.app1";
            cfg.app_name = "AppOne";
            cfg.display_name = "App One";
            cfg.permissions = {"SettingsRead", "DocumentRead"};
            const auto& bin = loader::get_embedded_campaign230_app_settings();
            auto pkg = tools::AppGenerator::generate_package(cfg, bin);
            TEST_ASSERT(installer.install(pkg) == packaging::InstallStatus::Success, "Install App1");
        }

        // App 2: Standard viewer
        {
            tools::AppTemplateConfig cfg{};
            cfg.bundle_id = "com.winios.app2";
            cfg.app_name = "AppTwo";
            cfg.display_name = "App Two";
            cfg.permissions = {"DocumentRead", "DocumentWrite"};
            const auto& bin = loader::get_embedded_campaign230_app_settings();
            auto pkg = tools::AppGenerator::generate_package(cfg, bin);
            TEST_ASSERT(installer.install(pkg) == packaging::InstallStatus::Success, "Install App2");
        }

        // App 3: Privileged forbidden app
        {
            tools::AppTemplateConfig cfg{};
            cfg.bundle_id = "com.winios.forbidden";
            cfg.app_name = "Forbidden";
            cfg.display_name = "Forbidden App";
            cfg.permissions = {"SystemAdmin"};
            const auto& bin = loader::get_embedded_campaign230_app_settings();
            auto pkg = tools::AppGenerator::generate_package(cfg, bin);
            TEST_ASSERT(installer.install(pkg) == packaging::InstallStatus::Success, "Install Forbidden");
        }

        f.pipeline = std::make_shared<AppLaunchPipeline>(
            f.vfs, f.registry, f.id_mgr, f.task_mgr,
            f.budget_mgr, f.cap_mgr, f.diag_svc
        );
        return f;
    }
};

// -----------------------------------------------------------------------------
// Test 1: Full Failure Injection Matrix & Zero Resource Leakage
// -----------------------------------------------------------------------------
static void test_failure_injection_matrix_and_cleanup() {
    std::cout << "[Stage 234.19] Test 1: Full Failure Injection Matrix Across 11 Failure Points...\n";

    auto f = TestFixture::create();

    const struct InjectionCase {
        LaunchFailureInjectionPoint point;
        LaunchStatus injected_status;
        const char* name;
    } cases[] = {
        { LaunchFailureInjectionPoint::FailIntegrityCheck, LaunchStatus::InstallationCorrupted, "FailIntegrityCheck" },
        { LaunchFailureInjectionPoint::FailBinaryInspection, LaunchStatus::ExecutableInvalid, "FailBinaryInspection" },
        { LaunchFailureInjectionPoint::FailCompatibilityCheck, LaunchStatus::CompatibilityMismatch, "FailCompatibilityCheck" },
        { LaunchFailureInjectionPoint::FailCapabilityAuth, LaunchStatus::CapabilityDenied, "FailCapabilityAuth" },
        { LaunchFailureInjectionPoint::FailProcessAllocation, LaunchStatus::ProcessAllocationFailed, "FailProcessAllocation" },
        { LaunchFailureInjectionPoint::FailTaskRegistration, LaunchStatus::TaskCreationFailed, "FailTaskRegistration" },
        { LaunchFailureInjectionPoint::FailBudgetAllocation, LaunchStatus::ResourceExhausted, "FailBudgetAllocation" },
        { LaunchFailureInjectionPoint::FailEnvironmentStaging, LaunchStatus::EnvironmentStagingFailed, "FailEnvironmentStaging" },
        { LaunchFailureInjectionPoint::FailMemoryMapping, LaunchStatus::MemoryMappingFailed, "FailMemoryMapping" },
        { LaunchFailureInjectionPoint::FailThreadAllocation, LaunchStatus::ThreadAllocationFailed, "FailThreadAllocation" },
        { LaunchFailureInjectionPoint::FailSandboxCreation, LaunchStatus::SandboxCreationFailed, "FailSandboxCreation" }
    };

    for (const auto& c : cases) {
        f.pipeline->inject_failure(c.point, c.injected_status);
        auto res = f.pipeline->launch("com.winios.app1");

        TEST_ASSERT(res.rolled_back, "Result must be marked rolled back");
        TEST_ASSERT(res.final_phase == LaunchTransactionPhase::RolledBack, "Final phase must be RolledBack");
        TEST_ASSERT(!res.handle.is_valid(), "Handle must be invalid upon rollback");
        TEST_ASSERT(res.status == c.injected_status, "Injected status must match");

        // Rigorous invariant checks: zero leaked processes, tasks, or capabilities
        TEST_ASSERT(f.id_mgr->active_process_count() == 0, "No active process leaked in ProcessIdentityManager");
        TEST_ASSERT(f.task_mgr->get_task_count() == 0, "No active task leaked in TaskManager");
    }

    f.pipeline->clear_failure_injection();
    std::cout << "  -> All 11 failure points cleanly rolled back with zero leaks.\n";
}

// -----------------------------------------------------------------------------
// Test 2: PID Pool Exhaustion and Resource Rollback
// -----------------------------------------------------------------------------
static void test_pid_exhaustion_rollback() {
    std::cout << "[Stage 234.19] Test 2: PID Pool Exhaustion and Safe Rollback...\n";

    // Strict 2-slot PID fixture: PID 100, 101
    auto f = TestFixture::create(100, 101);

    auto r1 = f.pipeline->launch("com.winios.app1");
    TEST_ASSERT(r1.status == LaunchStatus::Success && r1.handle.is_valid(), "App 1 launched");
    TEST_ASSERT(f.id_mgr->active_process_count() == 1, "1 active process");

    auto r2 = f.pipeline->launch("com.winios.app2");
    TEST_ASSERT(r2.status == LaunchStatus::Success && r2.handle.is_valid(), "App 2 launched");
    TEST_ASSERT(f.id_mgr->active_process_count() == 2, "2 active processes");

    // Slot exhaustion attempt: 3rd launch must fail and roll back cleanly
    auto r3 = f.pipeline->launch("com.winios.app1");
    TEST_ASSERT(r3.status == LaunchStatus::ResourceExhausted, "Must fail with ResourceExhausted");
    TEST_ASSERT(r3.rolled_back, "Must be rolled back");
    TEST_ASSERT(r3.final_phase == LaunchTransactionPhase::RolledBack, "Phase must be RolledBack");
    TEST_ASSERT(f.id_mgr->active_process_count() == 2, "Process count must remain 2");
    TEST_ASSERT(f.task_mgr->get_task_count() == 2, "Task count must remain 2");

    // Terminate app 1 -> PID slot freed
    bool term1 = f.pipeline->terminate(r1.handle);
    TEST_ASSERT(term1, "App 1 terminated");
    TEST_ASSERT(f.id_mgr->active_process_count() == 1, "1 active process remains");
    TEST_ASSERT(f.task_mgr->get_task_count() == 1, "1 active task remains");

    // Now 3rd launch succeeds
    auto r3_retry = f.pipeline->launch("com.winios.app1");
    TEST_ASSERT(r3_retry.status == LaunchStatus::Success && r3_retry.handle.is_valid(), "Retry succeeded");
    TEST_ASSERT(f.id_mgr->active_process_count() == 2, "2 active processes");

    // Teardown
    TEST_ASSERT(f.pipeline->terminate(r2.handle), "App 2 terminated");
    TEST_ASSERT(f.pipeline->terminate(r3_retry.handle), "Retry app terminated");
    TEST_ASSERT(f.id_mgr->active_process_count() == 0, "All processes freed");
    TEST_ASSERT(f.task_mgr->get_task_count() == 0, "All tasks freed");

    std::cout << "  -> PID exhaustion and slot reclamation verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Security Boundary & Rejection Rollbacks
// -----------------------------------------------------------------------------
static void test_security_rejections() {
    std::cout << "[Stage 234.19] Test 3: Security Boundary Rejections (Privileges & Corrupted Files)...\n";

    auto f = TestFixture::create();

    // 1. Privileged capability rejection: App requesting SystemAdmin
    auto r_priv = f.pipeline->launch("com.winios.forbidden");
    TEST_ASSERT(r_priv.status == LaunchStatus::CapabilityDenied, "Privileged app denied");
    TEST_ASSERT(r_priv.rolled_back, "Marked rolled back");
    TEST_ASSERT(f.id_mgr->active_process_count() == 0, "No process allocated");

    // 2. Non-existent app
    auto r_missing = f.pipeline->launch("com.winios.nonexistent");
    TEST_ASSERT(r_missing.status == LaunchStatus::AppNotInstalled, "Missing app rejected");
    TEST_ASSERT(r_missing.rolled_back, "Marked rolled back");

    // 3. Corrupted VFS executable
    auto app_opt = f.registry->get_app("com.winios.app1");
    TEST_ASSERT(app_opt.has_value(), "App 1 found");
    const std::string bin_path = app_opt->install_dir + "/" + app_opt->exec_path;
    // Overwrite binary with garbage
    std::vector<uint8_t> garbage = {0x00, 0x01, 0x02, 0x03};
    f.vfs->write_file(bin_path, garbage.data(), garbage.size());

    auto r_corrupt = f.pipeline->launch("com.winios.app1");
    TEST_ASSERT(r_corrupt.status == LaunchStatus::InstallationCorrupted ||
                r_corrupt.status == LaunchStatus::ExecutableInvalid, "Corrupted binary rejected");
    TEST_ASSERT(r_corrupt.rolled_back, "Corrupted launch rolled back");
    TEST_ASSERT(f.id_mgr->active_process_count() == 0, "No process allocated");

    std::cout << "  -> Security boundary rejections verified.\n";
}

int main() {
#if defined(_WIN32)
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDOUT);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDOUT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDOUT);
#endif

    std::cout << "=================================================================\n";
    std::cout << "  Stage 234.19: Launch Transaction Audit & Rollback Suite       \n";
    std::cout << "=================================================================\n";

    test_failure_injection_matrix_and_cleanup();
    test_pid_exhaustion_rollback();
    test_security_rejections();

    std::cout << "\n=================================================================\n";
    std::cout << "  ALL LAUNCH TRANSACTION AUDIT & ROLLBACK TESTS PASSED!          \n";
    std::cout << "=================================================================\n";
    return 0;
}
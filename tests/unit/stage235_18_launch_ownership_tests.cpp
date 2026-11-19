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

using namespace winios;
using namespace winios::loader;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at line " << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (0)

struct LaunchOwnershipFixture {
    std::shared_ptr<vfs::VirtualFileSystem>           vfs;
    std::shared_ptr<packaging::ApplicationRegistry>  registry;
    std::shared_ptr<kernel::ProcessIdentityManager>   id_mgr;
    std::shared_ptr<settings::TaskManager>           task_mgr;
    std::shared_ptr<platform::ResourceBudgetManager> budget_mgr;
    std::shared_ptr<security::CapabilityManager>     cap_mgr;
    std::shared_ptr<services::DiagnosticsService>    diag_svc;
    std::shared_ptr<AppLaunchPipeline>               pipeline;

    static LaunchOwnershipFixture create(uint32_t min_pid = 100, uint32_t max_pid = 105) {
        LaunchOwnershipFixture f;
        f.vfs = std::make_shared<vfs::VirtualFileSystem>();
        f.registry = std::make_shared<packaging::ApplicationRegistry>();
        f.id_mgr = std::make_shared<kernel::ProcessIdentityManager>(min_pid, max_pid, 0);
        f.task_mgr = std::make_shared<settings::TaskManager>();
        f.budget_mgr = std::make_shared<platform::ResourceBudgetManager>();
        f.cap_mgr = std::make_shared<security::CapabilityManager>();
        f.diag_svc = std::make_shared<services::DiagnosticsService>();

        packaging::PackageInstaller installer(f.vfs, f.registry);

        // App Alpha: SettingsRead + DocumentRead
        {
            tools::AppTemplateConfig cfg{};
            cfg.bundle_id = "com.winios.alpha";
            cfg.app_name = "Alpha";
            cfg.display_name = "App Alpha";
            cfg.permissions = {"SettingsRead", "DocumentRead"};
            const auto& bin = loader::get_embedded_campaign230_app_settings();
            auto pkg = tools::AppGenerator::generate_package(cfg, bin);
            TEST_ASSERT(installer.install(pkg) == packaging::InstallStatus::Success, "Install Alpha");
        }

        // App Beta: DocumentWrite + DocumentCreate
        {
            tools::AppTemplateConfig cfg{};
            cfg.bundle_id = "com.winios.beta";
            cfg.app_name = "Beta";
            cfg.display_name = "App Beta";
            cfg.permissions = {"DocumentWrite", "DocumentCreate"};
            const auto& bin = loader::get_embedded_campaign230_app_settings();
            auto pkg = tools::AppGenerator::generate_package(cfg, bin);
            TEST_ASSERT(installer.install(pkg) == packaging::InstallStatus::Success, "Install Beta");
        }

        f.pipeline = std::make_shared<AppLaunchPipeline>(
            f.vfs, f.registry, f.id_mgr, f.task_mgr,
            f.budget_mgr, f.cap_mgr, f.diag_svc
        );
        return f;
    }
};

static void test_complete_14_phase_launch_and_commit() {
    std::cout << "[Test 1] 14-Phase launch transaction commit and ownership matrix...\n";
    auto f = LaunchOwnershipFixture::create();

    auto res = f.pipeline->launch("com.winios.alpha");
    TEST_ASSERT(res.status == LaunchStatus::Success, "Launch successful");
    TEST_ASSERT(!res.rolled_back, "Not rolled back");
    TEST_ASSERT(res.final_phase == LaunchTransactionPhase::Committed, "Transaction phase is Committed");
    TEST_ASSERT(res.handle.is_valid(), "Process handle is valid");
    TEST_ASSERT(f.pipeline->last_transaction_phase() == LaunchTransactionPhase::Committed, "Pipeline reports Committed");

    // Verify Process Identity Ownership
    auto inst = f.id_mgr->get_instance(res.handle);
    TEST_ASSERT(inst.has_value(), "Process instance retrieved");
    TEST_ASSERT(inst->app.bundle_id == "com.winios.alpha", "Bundle ID matches");
    TEST_ASSERT(inst->handle.pid == res.handle.pid, "PID matches");
    TEST_ASSERT(inst->handle.generation == res.handle.generation, "Generation matches");

    // Verify Capability Token Generation & Binding
    TEST_ASSERT(f.cap_mgr->check(res.handle.pid, security::Capability::SettingsRead), "Has SettingsRead");
    TEST_ASSERT(f.cap_mgr->check(res.handle.pid, security::Capability::DocumentRead), "Has DocumentRead");
    TEST_ASSERT(!f.cap_mgr->check(res.handle.pid, security::Capability::DocumentWrite), "Lacks DocumentWrite");
    TEST_ASSERT(!f.cap_mgr->check(res.handle.pid, security::Capability::SettingsWrite), "Lacks SettingsWrite");

    // Verify Task Manager Registration
    settings::TaskInfo tinfo{};
    TEST_ASSERT(f.task_mgr->get_task(res.handle.pid, tinfo), "Task registered in TaskManager");
    TEST_ASSERT(tinfo.pid == res.handle.pid, "Task PID matches");
    TEST_ASSERT(tinfo.generation > 0, "Task generation assigned");

    // Verify Clean Teardown
    bool term_ok = f.pipeline->terminate(res.handle);
    TEST_ASSERT(term_ok, "Termination successful");
    TEST_ASSERT(!f.id_mgr->is_valid_handle(res.handle), "Stale handle no longer valid");
    TEST_ASSERT(!f.task_mgr->get_task(res.handle.pid, tinfo), "Task removed from TaskManager");
    TEST_ASSERT(!f.cap_mgr->check(res.handle.pid, security::Capability::SettingsRead), "Capabilities revoked");
    std::cout << "  Passed!\n";
}

static void test_generational_handle_isolation() {
    std::cout << "[Test 2] Generational handle isolation across process termination and respawn...\n";
    // 1-slot PID pool to guarantee immediate slot recycling
    auto f = LaunchOwnershipFixture::create(100, 100);

    // Launch Alpha in PID slot 100
    auto r1 = f.pipeline->launch("com.winios.alpha");
    TEST_ASSERT(r1.status == LaunchStatus::Success, "First launch success");
    TEST_ASSERT(r1.handle.pid == 100, "PID is 100");
    auto stale_handle = r1.handle;

    // Terminate Alpha
    TEST_ASSERT(f.pipeline->terminate(stale_handle), "Terminate first instance");

    // Launch Beta (reusing the same PID slot with incremented generation)
    auto r2 = f.pipeline->launch("com.winios.beta");
    TEST_ASSERT(r2.status == LaunchStatus::Success, "Second launch success");
    TEST_ASSERT(r2.handle.pid == stale_handle.pid, "Same PID slot reused");
    TEST_ASSERT(r2.handle.generation > stale_handle.generation, "Generation strictly incremented");

    // Stale handle cannot terminate or access new process
    TEST_ASSERT(!f.pipeline->terminate(stale_handle), "Stale handle cannot terminate new process");
    TEST_ASSERT(!f.id_mgr->is_valid_handle(stale_handle), "Stale handle rejected by identity manager");
    TEST_ASSERT(f.id_mgr->is_valid_handle(r2.handle), "New handle is valid");

    // Clean up
    TEST_ASSERT(f.pipeline->terminate(r2.handle), "Terminate second instance");
    std::cout << "  Passed!\n";
}

static void test_rollback_ownership_matrix() {
    std::cout << "[Test 3] Rollback isolation and zero resource leak matrix...\n";
    auto f = LaunchOwnershipFixture::create();

    // Inject failure at late phase (Phase 12: InitializingSandbox)
    f.pipeline->inject_failure(LaunchFailureInjectionPoint::FailSandboxCreation, LaunchStatus::SandboxCreationFailed);
    auto res = f.pipeline->launch("com.winios.alpha");

    TEST_ASSERT(res.rolled_back, "Launch marked rolled back");
    TEST_ASSERT(res.final_phase == LaunchTransactionPhase::RolledBack, "Phase is RolledBack");
    TEST_ASSERT(res.status == LaunchStatus::SandboxCreationFailed, "Status matches injected");

    // Invariants: No active processes, tasks, budget allocations, or capability tokens
    TEST_ASSERT(f.id_mgr->active_process_count() == 0, "Zero leaked processes");
    TEST_ASSERT(f.task_mgr->get_task_count() == 0, "Zero leaked tasks");

    f.pipeline->clear_failure_injection();
    std::cout << "  Passed!\n";
}

int main() {
    std::cout << "=== Stage 235.18 Launch Ownership Matrix Tests ===" << std::endl;
    test_complete_14_phase_launch_and_commit();
    test_generational_handle_isolation();
    test_rollback_ownership_matrix();
    std::cout << "=== All Stage 235.18 tests PASSED successfully ===" << std::endl;
    return 0;
}

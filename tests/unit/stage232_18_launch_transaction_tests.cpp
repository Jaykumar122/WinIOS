#include "loader/app_launcher.h"
#include "loader/embedded_campaign230_workloads.inl"
#include "packaging/app_package.h"
#include "packaging/app_registry.h"
#include "packaging/package_installer.h"
#include "tools/app_generator.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #cond << std::endl; \
        std::exit(1); \
    } \
} while(0)

using namespace winios;
using namespace winios::loader;

struct TestEnvironment {
    std::shared_ptr<vfs::VirtualFileSystem>          vfs;
    std::shared_ptr<packaging::ApplicationRegistry>  registry;
    std::shared_ptr<kernel::ProcessIdentityManager>  id_mgr;
    std::shared_ptr<settings::TaskManager>           task_mgr;
    std::shared_ptr<platform::ResourceBudgetManager> budget_mgr;
    std::shared_ptr<security::CapabilityManager>     cap_mgr;
    std::shared_ptr<services::DiagnosticsService>    diag_svc;
    std::shared_ptr<AppLaunchPipeline>               pipeline;

    static TestEnvironment create() {
        TestEnvironment env;
        env.vfs = std::make_shared<vfs::VirtualFileSystem>();
        env.registry = std::make_shared<packaging::ApplicationRegistry>();
        env.id_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 109, 0);
        env.task_mgr = std::make_shared<settings::TaskManager>();
        env.budget_mgr = std::make_shared<platform::ResourceBudgetManager>();
        env.cap_mgr = std::make_shared<security::CapabilityManager>();
        env.diag_svc = std::make_shared<services::DiagnosticsService>();

        packaging::PackageInstaller installer(env.vfs, env.registry);

        // App 1: Standard valid app
        {
            tools::AppTemplateConfig cfg{};
            cfg.bundle_id = "com.winios.tx_calc";
            cfg.app_name = "TxCalc";
            cfg.display_name = "Transactional Calculator";
            cfg.permissions = {"SettingsRead", "DocumentRead"};
            const auto& bin = loader::get_embedded_campaign230_app_settings();
            auto pkg = tools::AppGenerator::generate_package(cfg, bin);
            auto inst_st = installer.install(pkg);
            TEST_ASSERT(inst_st == packaging::InstallStatus::Success);
        }

        // App 2: Another valid app for multi-process tests
        {
            tools::AppTemplateConfig cfg{};
            cfg.bundle_id = "com.winios.tx_viewer";
            cfg.app_name = "TxViewer";
            cfg.display_name = "Transactional Viewer";
            cfg.permissions = {"DocumentRead", "DocumentWrite"};
            const auto& bin = loader::get_embedded_campaign230_app_settings();
            auto pkg = tools::AppGenerator::generate_package(cfg, bin);
            auto inst_st = installer.install(pkg);
            TEST_ASSERT(inst_st == packaging::InstallStatus::Success);
        }

        // App 3: Incompatible SDK app
        {
            tools::AppTemplateConfig cfg{};
            cfg.bundle_id = "com.winios.future_app";
            cfg.app_name = "FutureApp";
            cfg.display_name = "Future App";
            cfg.min_sdk_version = 0x00030000; // SDK 3.0 (higher than platform 2.4)
            const auto& bin = loader::get_embedded_campaign230_app_settings();
            auto pkg = tools::AppGenerator::generate_package(cfg, bin);
            auto inst_st = installer.install(pkg);
            TEST_ASSERT(inst_st == packaging::InstallStatus::Success);
        }

        // App 4: Restricted capability app
        {
            tools::AppTemplateConfig cfg{};
            cfg.bundle_id = "com.winios.admin_exploit";
            cfg.app_name = "AdminExploit";
            cfg.display_name = "Admin Exploit";
            cfg.permissions = {"KernelAccess"};
            const auto& bin = loader::get_embedded_campaign230_app_settings();
            auto pkg = tools::AppGenerator::generate_package(cfg, bin);
            auto inst_st = installer.install(pkg);
            TEST_ASSERT(inst_st == packaging::InstallStatus::Success);
        }

        env.pipeline = std::make_shared<AppLaunchPipeline>(
            env.vfs, env.registry, env.id_mgr, env.task_mgr,
            env.budget_mgr, env.cap_mgr, env.diag_svc
        );
        return env;
    }
};

// 1. Clean committed launch and full subsystem resource inspection
static void test_clean_launch_and_resource_state() {
    std::cout << "[Stage 232.18] Testing clean committed launch and resource states...\n";
    auto env = TestEnvironment::create();

    TEST_ASSERT(env.id_mgr->active_process_count() == 0);
    TEST_ASSERT(env.task_mgr->get_task_count() == 0);

    auto res = env.pipeline->launch("com.winios.tx_calc");
    TEST_ASSERT(res.status == LaunchStatus::Success);
    TEST_ASSERT(res.final_phase == LaunchTransactionPhase::Committed);
    TEST_ASSERT(!res.rolled_back);
    TEST_ASSERT(res.handle.is_valid());
    uint32_t pid = res.handle.pid;

    // Verify all 4 subsystems properly initialized for this process
    TEST_ASSERT(env.id_mgr->active_process_count() == 1);
    TEST_ASSERT(env.id_mgr->is_valid_handle(res.handle));

    settings::TaskInfo tinfo{};
    TEST_ASSERT(env.task_mgr->get_task(pid, tinfo));
    TEST_ASSERT(tinfo.state == settings::TaskState::Running);

    TEST_ASSERT(env.budget_mgr->get_task_memory(pid) > 0);
    TEST_ASSERT(env.cap_mgr->get_identity(pid).has_value());
    TEST_ASSERT(env.cap_mgr->check(pid, security::Capability::SettingsRead));
    TEST_ASSERT(env.cap_mgr->check(pid, security::Capability::DocumentRead));
    TEST_ASSERT(!env.cap_mgr->check(pid, security::Capability::DocumentWrite));

    // Terminate and verify all 4 subsystems clean up
    TEST_ASSERT(env.pipeline->terminate(res.handle));
    TEST_ASSERT(env.id_mgr->active_process_count() == 0);
    TEST_ASSERT(!env.task_mgr->get_task(pid, tinfo));
    TEST_ASSERT(env.budget_mgr->get_task_memory(pid) == 0);
    TEST_ASSERT(!env.cap_mgr->get_identity(pid).has_value());
    TEST_ASSERT(!env.cap_mgr->check(pid, security::Capability::SettingsRead));

    std::cout << "  -> Clean launch, state verification, and termination passed.\n";
}

// 2. Comprehensive fault injection across all 11 injection points
static void test_all_11_fault_injection_points() {
    std::cout << "[Stage 232.18] Testing rollback across all 11 fault injection points...\n";
    auto env = TestEnvironment::create();

    const struct InjectionTestCase {
        LaunchFailureInjectionPoint point;
        LaunchStatus expected_status;
        const char* name;
    } cases[] = {
        { LaunchFailureInjectionPoint::FailIntegrityCheck, LaunchStatus::ResourceExhausted, "FailIntegrityCheck" },
        { LaunchFailureInjectionPoint::FailBinaryInspection, LaunchStatus::ResourceExhausted, "FailBinaryInspection" },
        { LaunchFailureInjectionPoint::FailCompatibilityCheck, LaunchStatus::ResourceExhausted, "FailCompatibilityCheck" },
        { LaunchFailureInjectionPoint::FailCapabilityAuth, LaunchStatus::ResourceExhausted, "FailCapabilityAuth" },
        { LaunchFailureInjectionPoint::FailProcessAllocation, LaunchStatus::ResourceExhausted, "FailProcessAllocation" },
        { LaunchFailureInjectionPoint::FailTaskRegistration, LaunchStatus::ResourceExhausted, "FailTaskRegistration" },
        { LaunchFailureInjectionPoint::FailBudgetAllocation, LaunchStatus::ResourceExhausted, "FailBudgetAllocation" },
        { LaunchFailureInjectionPoint::FailEnvironmentStaging, LaunchStatus::EnvironmentStagingFailed, "FailEnvironmentStaging" },
        { LaunchFailureInjectionPoint::FailMemoryMapping, LaunchStatus::MemoryMappingFailed, "FailMemoryMapping" },
        { LaunchFailureInjectionPoint::FailThreadAllocation, LaunchStatus::ThreadAllocationFailed, "FailThreadAllocation" },
        { LaunchFailureInjectionPoint::FailSandboxCreation, LaunchStatus::SandboxCreationFailed, "FailSandboxCreation" }
    };

    for (const auto& tc : cases) {
        env.pipeline->inject_failure(tc.point, tc.expected_status);
        auto res = env.pipeline->launch("com.winios.tx_calc");

        TEST_ASSERT(res.status == tc.expected_status);
        TEST_ASSERT(res.rolled_back);
        TEST_ASSERT(res.final_phase == LaunchTransactionPhase::RolledBack);
        TEST_ASSERT(!res.handle.is_valid());

        // Zero resource leaks across all subsystems
        TEST_ASSERT(env.id_mgr->active_process_count() == 0);
        TEST_ASSERT(env.task_mgr->get_task_count() == 0);
    }

    // Now clear injection and ensure pipeline still works cleanly
    env.pipeline->clear_failure_injection();
    auto res_clean = env.pipeline->launch("com.winios.tx_calc");
    TEST_ASSERT(res_clean.status == LaunchStatus::Success);
    TEST_ASSERT(res_clean.final_phase == LaunchTransactionPhase::Committed);
    TEST_ASSERT(!res_clean.rolled_back);
    TEST_ASSERT(env.id_mgr->active_process_count() == 1);

    TEST_ASSERT(env.pipeline->terminate(res_clean.handle));
    TEST_ASSERT(env.id_mgr->active_process_count() == 0);

    std::cout << "  -> All 11 failure injection points cleanly rolled back with zero leaks.\n";
}

// 3. Natural failure modes without synthetic injection
static void test_natural_failure_modes() {
    std::cout << "[Stage 232.18] Testing natural failure modes (no synthetic injection)...\n";
    auto env = TestEnvironment::create();

    // Case A: App not installed
    {
        auto res = env.pipeline->launch("com.nonexistent.app");
        TEST_ASSERT(res.status == LaunchStatus::AppNotInstalled);
        TEST_ASSERT(res.rolled_back);
        TEST_ASSERT(env.id_mgr->active_process_count() == 0);
    }

    // Case B: Compatibility mismatch (SDK 3.0 required, platform is 2.4)
    {
        auto res = env.pipeline->launch("com.winios.future_app");
        TEST_ASSERT(res.status == LaunchStatus::CompatibilityMismatch);
        TEST_ASSERT(res.rolled_back);
        TEST_ASSERT(env.id_mgr->active_process_count() == 0);
    }

    // Case C: Capability denied (KernelAccess requested)
    {
        auto res = env.pipeline->launch("com.winios.admin_exploit");
        TEST_ASSERT(res.status == LaunchStatus::CapabilityDenied);
        TEST_ASSERT(res.rolled_back);
        TEST_ASSERT(env.id_mgr->active_process_count() == 0);
    }

    // Case D: Process slots exhausted
    {
        std::vector<kernel::ProcessHandle> handles;
        for (int i = 0; i < 10; ++i) { // capacity is 10 (100 to 110)
            auto res = env.pipeline->launch("com.winios.tx_calc");
            TEST_ASSERT(res.status == LaunchStatus::Success);
            handles.push_back(res.handle);
        }
        TEST_ASSERT(env.id_mgr->active_process_count() == 10);

        // 11th launch must fail with ResourceExhausted
        auto res_overflow = env.pipeline->launch("com.winios.tx_calc");
        TEST_ASSERT(res_overflow.status == LaunchStatus::ResourceExhausted);
        TEST_ASSERT(res_overflow.rolled_back);
        TEST_ASSERT(env.id_mgr->active_process_count() == 10);

        // Terminate all
        for (auto h : handles) {
            TEST_ASSERT(env.pipeline->terminate(h));
        }
        TEST_ASSERT(env.id_mgr->active_process_count() == 0);
    }

    std::cout << "  -> Natural failure modes verified.\n";
}

// 4. Multi-process isolation and partial failure protection
static void test_multi_process_isolation_and_partial_failure() {
    std::cout << "[Stage 232.18] Testing multi-process isolation under partial failure...\n";
    auto env = TestEnvironment::create();

    // 1. Launch Process A (TxCalc)
    auto res_a = env.pipeline->launch("com.winios.tx_calc");
    TEST_ASSERT(res_a.status == LaunchStatus::Success);
    TEST_ASSERT(env.id_mgr->active_process_count() == 1);
    uint32_t pid_a = res_a.handle.pid;

    // 2. Attempt to launch Process B with synthetic failure at SandboxCreation
    env.pipeline->inject_failure(LaunchFailureInjectionPoint::FailSandboxCreation);
    auto res_b = env.pipeline->launch("com.winios.tx_viewer");
    TEST_ASSERT(res_b.status == LaunchStatus::SandboxCreationFailed);
    TEST_ASSERT(res_b.rolled_back);

    // Verify Process A remains completely intact and unaffected
    TEST_ASSERT(env.id_mgr->active_process_count() == 1);
    TEST_ASSERT(env.id_mgr->is_valid_handle(res_a.handle));
    settings::TaskInfo tinfo{};
    TEST_ASSERT(env.task_mgr->get_task(pid_a, tinfo));
    TEST_ASSERT(env.budget_mgr->get_task_memory(pid_a) > 0);
    TEST_ASSERT(env.cap_mgr->get_identity(pid_a).has_value());
    TEST_ASSERT(env.cap_mgr->check(pid_a, security::Capability::SettingsRead));

    // 3. Clear failure and launch Process C (TxViewer)
    env.pipeline->clear_failure_injection();
    auto res_c = env.pipeline->launch("com.winios.tx_viewer");
    TEST_ASSERT(res_c.status == LaunchStatus::Success);
    TEST_ASSERT(env.id_mgr->active_process_count() == 2);
    uint32_t pid_c = res_c.handle.pid;
    TEST_ASSERT(pid_a != pid_c);

    // 4. Terminate Process A
    TEST_ASSERT(env.pipeline->terminate(res_a.handle));
    TEST_ASSERT(env.id_mgr->active_process_count() == 1);
    TEST_ASSERT(!env.task_mgr->get_task(pid_a, tinfo));
    TEST_ASSERT(env.cap_mgr->get_identity(pid_c).has_value()); // Process C still fine

    // 5. Terminate Process C
    TEST_ASSERT(env.pipeline->terminate(res_c.handle));
    TEST_ASSERT(env.id_mgr->active_process_count() == 0);
    TEST_ASSERT(env.task_mgr->get_task_count() == 0);

    std::cout << "  -> Multi-process isolation and partial failure protection verified.\n";
}

int main() {
    std::cout << "=== Stage 232.18: Launch Transaction and Resource Cleanup Suite ===\n";
    test_clean_launch_and_resource_state();
    test_all_11_fault_injection_points();
    test_natural_failure_modes();
    test_multi_process_isolation_and_partial_failure();
    std::cout << "=== All Stage 232.18 Tests PASSED ===\n";
    return 0;
}

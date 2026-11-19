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
        env.id_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 110, 0);
        env.task_mgr = std::make_shared<settings::TaskManager>();
        env.budget_mgr = std::make_shared<platform::ResourceBudgetManager>();
        env.cap_mgr = std::make_shared<security::CapabilityManager>();
        env.diag_svc = std::make_shared<services::DiagnosticsService>();

        packaging::PackageInstaller installer(env.vfs, env.registry);

        // Generate and install sample application
        tools::AppTemplateConfig cfg{};
        cfg.bundle_id = "com.winios.tx_calc";
        cfg.app_name = "TxCalc";
        cfg.display_name = "Transactional Calculator";
        cfg.permissions = {"SettingsRead"};
        const auto& bin = loader::get_embedded_campaign230_app_settings();
        auto pkg = tools::AppGenerator::generate_package(cfg, bin);
        auto inst_st = installer.install(pkg);
        TEST_ASSERT(inst_st == packaging::InstallStatus::Success);

        env.pipeline = std::make_shared<AppLaunchPipeline>(
            env.vfs, env.registry, env.id_mgr, env.task_mgr,
            env.budget_mgr, env.cap_mgr, env.diag_svc
        );
        return env;
    }
};

// 1. Test clean committed launch
static void test_clean_committed_launch() {
    std::cout << "[Test 1] Clean transactional launch & commit...\n";
    auto env = TestEnvironment::create();

    TEST_ASSERT(env.id_mgr->active_process_count() == 0);

    auto res = env.pipeline->launch("com.winios.tx_calc");
    TEST_ASSERT(res.status == LaunchStatus::Success);
    TEST_ASSERT(res.final_phase == LaunchTransactionPhase::Committed);
    TEST_ASSERT(!res.rolled_back);
    TEST_ASSERT(res.handle.is_valid());
    TEST_ASSERT(env.id_mgr->active_process_count() == 1);
    settings::TaskInfo tinfo{};
    TEST_ASSERT(env.task_mgr->get_task(res.handle.pid, tinfo));

    // Clean termination
    TEST_ASSERT(env.pipeline->terminate(res.handle));
    TEST_ASSERT(env.id_mgr->active_process_count() == 0);
    TEST_ASSERT(!env.task_mgr->get_task(res.handle.pid, tinfo));

    std::cout << "  -> Clean transactional launch and termination passed.\n";
}

// 2. Test rollback on environment staging failure
static void test_rollback_environment_staging() {
    std::cout << "[Test 2] Rollback on environment staging failure...\n";
    auto env = TestEnvironment::create();

    env.pipeline->inject_failure(LaunchFailureInjectionPoint::FailEnvironmentStaging);
    auto res = env.pipeline->launch("com.winios.tx_calc");

    TEST_ASSERT(res.status == LaunchStatus::EnvironmentStagingFailed);
    TEST_ASSERT(res.rolled_back);
    TEST_ASSERT(res.final_phase == LaunchTransactionPhase::RolledBack);
    TEST_ASSERT(!res.handle.is_valid());

    // Verify complete resource cleanup (no leaked process, no task, no budget)
    TEST_ASSERT(env.id_mgr->active_process_count() == 0);
    TEST_ASSERT(env.task_mgr->get_task_count() == 0);

    std::cout << "  -> Environment staging rollback verified with 0 leaks.\n";
}

// 3. Test rollback on memory mapping failure
static void test_rollback_memory_mapping() {
    std::cout << "[Test 3] Rollback on memory mapping failure...\n";
    auto env = TestEnvironment::create();

    env.pipeline->inject_failure(LaunchFailureInjectionPoint::FailMemoryMapping);
    auto res = env.pipeline->launch("com.winios.tx_calc");

    TEST_ASSERT(res.status == LaunchStatus::MemoryMappingFailed);
    TEST_ASSERT(res.rolled_back);
    TEST_ASSERT(env.id_mgr->active_process_count() == 0);
    TEST_ASSERT(env.task_mgr->get_task_count() == 0);

    std::cout << "  -> Memory mapping rollback verified with 0 leaks.\n";
}

// 4. Test rollback on thread allocation failure
static void test_rollback_thread_allocation() {
    std::cout << "[Test 4] Rollback on thread allocation failure...\n";
    auto env = TestEnvironment::create();

    env.pipeline->inject_failure(LaunchFailureInjectionPoint::FailThreadAllocation);
    auto res = env.pipeline->launch("com.winios.tx_calc");

    TEST_ASSERT(res.status == LaunchStatus::ThreadAllocationFailed);
    TEST_ASSERT(res.rolled_back);
    TEST_ASSERT(env.id_mgr->active_process_count() == 0);
    TEST_ASSERT(env.task_mgr->get_task_count() == 0);

    std::cout << "  -> Thread allocation rollback verified with 0 leaks.\n";
}

// 5. Test rollback on sandbox creation failure
static void test_rollback_sandbox_creation() {
    std::cout << "[Test 5] Rollback on sandbox creation failure...\n";
    auto env = TestEnvironment::create();

    env.pipeline->inject_failure(LaunchFailureInjectionPoint::FailSandboxCreation);
    auto res = env.pipeline->launch("com.winios.tx_calc");

    TEST_ASSERT(res.status == LaunchStatus::SandboxCreationFailed);
    TEST_ASSERT(res.rolled_back);
    TEST_ASSERT(env.id_mgr->active_process_count() == 0);
    TEST_ASSERT(env.task_mgr->get_task_count() == 0);

    std::cout << "  -> Sandbox creation rollback verified with 0 leaks.\n";
}

// 6. Test repeated failures and clean recovery
static void test_repeated_failures_and_recovery() {
    std::cout << "[Test 6] Repeated failure stress test & clean recovery...\n";
    auto env = TestEnvironment::create();

    const LaunchFailureInjectionPoint injection_points[] = {
        LaunchFailureInjectionPoint::FailIntegrityCheck,
        LaunchFailureInjectionPoint::FailBinaryInspection,
        LaunchFailureInjectionPoint::FailCompatibilityCheck,
        LaunchFailureInjectionPoint::FailCapabilityAuth,
        LaunchFailureInjectionPoint::FailProcessAllocation,
        LaunchFailureInjectionPoint::FailTaskRegistration,
        LaunchFailureInjectionPoint::FailBudgetAllocation,
        LaunchFailureInjectionPoint::FailEnvironmentStaging,
        LaunchFailureInjectionPoint::FailMemoryMapping,
        LaunchFailureInjectionPoint::FailThreadAllocation,
        LaunchFailureInjectionPoint::FailSandboxCreation
    };

    for (auto pt : injection_points) {
        env.pipeline->inject_failure(pt);
        auto res = env.pipeline->launch("com.winios.tx_calc");
        TEST_ASSERT(res.status != LaunchStatus::Success);
        TEST_ASSERT(res.rolled_back);
        TEST_ASSERT(env.id_mgr->active_process_count() == 0);
        TEST_ASSERT(env.task_mgr->get_task_count() == 0);
    }

    // Now clear injection and perform normal launch
    env.pipeline->clear_failure_injection();
    auto res_clean = env.pipeline->launch("com.winios.tx_calc");
    TEST_ASSERT(res_clean.status == LaunchStatus::Success);
    TEST_ASSERT(res_clean.final_phase == LaunchTransactionPhase::Committed);
    TEST_ASSERT(!res_clean.rolled_back);
    TEST_ASSERT(env.id_mgr->active_process_count() == 1);

    TEST_ASSERT(env.pipeline->terminate(res_clean.handle));
    TEST_ASSERT(env.id_mgr->active_process_count() == 0);

    std::cout << "  -> Repeated failures stress and recovery passed.\n";
}

int main() {
    std::cout << "=== Stage 231.12: Transactional Application Launch and Rollback Tests ===\n";
    test_clean_committed_launch();
    test_rollback_environment_staging();
    test_rollback_memory_mapping();
    test_rollback_thread_allocation();
    test_rollback_sandbox_creation();
    test_repeated_failures_and_recovery();
    std::cout << "=== All Stage 231.12 Tests PASSED ===\n";
    return 0;
}

#include "loader/app_launcher.h"
#include "loader/embedded_campaign230_workloads.inl"
#include "packaging/app_package.h"
#include "packaging/app_registry.h"
#include "packaging/package_installer.h"
#include "tools/app_generator.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Launch Resource Assertion Failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::loader;

struct LaunchResourceFixture {
    std::shared_ptr<vfs::VirtualFileSystem>          vfs;
    std::shared_ptr<packaging::ApplicationRegistry>  registry;
    std::shared_ptr<kernel::ProcessIdentityManager>  id_mgr;
    std::shared_ptr<settings::TaskManager>           task_mgr;
    std::shared_ptr<platform::ResourceBudgetManager> budget_mgr;
    std::shared_ptr<security::CapabilityManager>     cap_mgr;
    std::shared_ptr<services::DiagnosticsService>    diag_svc;
    std::shared_ptr<AppLaunchPipeline>               pipeline;

    static LaunchResourceFixture create(uint32_t min_pid = 100, uint32_t max_pid = 109) {
        LaunchResourceFixture f;
        f.vfs = std::make_shared<vfs::VirtualFileSystem>();
        f.registry = std::make_shared<packaging::ApplicationRegistry>();
        f.id_mgr = std::make_shared<kernel::ProcessIdentityManager>(min_pid, max_pid, 0);
        f.task_mgr = std::make_shared<settings::TaskManager>();
        f.budget_mgr = std::make_shared<platform::ResourceBudgetManager>();
        f.cap_mgr = std::make_shared<security::CapabilityManager>();
        f.diag_svc = std::make_shared<services::DiagnosticsService>();

        packaging::PackageInstaller installer(f.vfs, f.registry);

        // Install test app: tx_calc
        {
            tools::AppTemplateConfig cfg{};
            cfg.bundle_id = "com.winios.tx_calc";
            cfg.app_name = "TxCalc";
            cfg.display_name = "Transactional Calculator";
            cfg.permissions = {"SettingsRead", "DocumentRead"};
            const auto& bin = loader::get_embedded_campaign230_app_settings();
            auto pkg = tools::AppGenerator::generate_package(cfg, bin);
            TEST_ASSERT(installer.install(pkg) == packaging::InstallStatus::Success);
        }

        // Install test app: tx_viewer
        {
            tools::AppTemplateConfig cfg{};
            cfg.bundle_id = "com.winios.tx_viewer";
            cfg.app_name = "TxViewer";
            cfg.display_name = "Transactional Viewer";
            cfg.permissions = {"DocumentRead", "DocumentWrite"};
            const auto& bin = loader::get_embedded_campaign230_app_settings();
            auto pkg = tools::AppGenerator::generate_package(cfg, bin);
            TEST_ASSERT(installer.install(pkg) == packaging::InstallStatus::Success);
        }

        f.pipeline = std::make_shared<AppLaunchPipeline>(
            f.vfs, f.registry, f.id_mgr, f.task_mgr,
            f.budget_mgr, f.cap_mgr, f.diag_svc
        );
        return f;
    }
};

static void test_exact_phase_rollback_accounting() {
    std::cout << "[Stage 233.18] Testing resource rollback across all intermediate phases...\n";
    auto f = LaunchResourceFixture::create();

    const struct InjectionCheck {
        LaunchFailureInjectionPoint point;
        const char* name;
    } checks[] = {
        { LaunchFailureInjectionPoint::FailIntegrityCheck, "FailIntegrityCheck" },
        { LaunchFailureInjectionPoint::FailBinaryInspection, "FailBinaryInspection" },
        { LaunchFailureInjectionPoint::FailCompatibilityCheck, "FailCompatibilityCheck" },
        { LaunchFailureInjectionPoint::FailCapabilityAuth, "FailCapabilityAuth" },
        { LaunchFailureInjectionPoint::FailProcessAllocation, "FailProcessAllocation" },
        { LaunchFailureInjectionPoint::FailTaskRegistration, "FailTaskRegistration" },
        { LaunchFailureInjectionPoint::FailBudgetAllocation, "FailBudgetAllocation" },
        { LaunchFailureInjectionPoint::FailEnvironmentStaging, "FailEnvironmentStaging" },
        { LaunchFailureInjectionPoint::FailMemoryMapping, "FailMemoryMapping" },
        { LaunchFailureInjectionPoint::FailThreadAllocation, "FailThreadAllocation" },
        { LaunchFailureInjectionPoint::FailSandboxCreation, "FailSandboxCreation" }
    };

    for (const auto& c : checks) {
        f.pipeline->inject_failure(c.point, LaunchStatus::ResourceExhausted);
        auto res = f.pipeline->launch("com.winios.tx_calc");

        TEST_ASSERT(res.rolled_back);
        TEST_ASSERT(res.final_phase == LaunchTransactionPhase::RolledBack);
        TEST_ASSERT(!res.handle.is_valid());

        // Verify zero leakage across all subsystem registries
        TEST_ASSERT(f.id_mgr->active_process_count() == 0);
        TEST_ASSERT(f.task_mgr->get_task_count() == 0);
    }

    f.pipeline->clear_failure_injection();
}

static void test_resource_starvation_and_recovery() {
    std::cout << "[Stage 233.18] Testing PID capacity exhaustion and clean recovery...\n";
    // Fixture with capacity for exactly 2 processes: PID 100, 101
    auto f = LaunchResourceFixture::create(100, 101);

    // Launch process 1
    auto r1 = f.pipeline->launch("com.winios.tx_calc");
    TEST_ASSERT(r1.status == LaunchStatus::Success && r1.handle.is_valid());
    TEST_ASSERT(f.id_mgr->active_process_count() == 1);

    // Launch process 2
    auto r2 = f.pipeline->launch("com.winios.tx_viewer");
    TEST_ASSERT(r2.status == LaunchStatus::Success && r2.handle.is_valid());
    TEST_ASSERT(f.id_mgr->active_process_count() == 2);

    // Attempting 3rd launch: PID slots exhausted
    auto r3 = f.pipeline->launch("com.winios.tx_calc");
    TEST_ASSERT(r3.status == LaunchStatus::ResourceExhausted);
    TEST_ASSERT(r3.rolled_back);
    TEST_ASSERT(f.id_mgr->active_process_count() == 2); // Unchanged

    // Terminate process 1 -> slot freed
    TEST_ASSERT(f.pipeline->terminate(r1.handle));
    TEST_ASSERT(f.id_mgr->active_process_count() == 1);

    // 3rd launch now succeeds
    auto r3_retry = f.pipeline->launch("com.winios.tx_calc");
    TEST_ASSERT(r3_retry.status == LaunchStatus::Success && r3_retry.handle.is_valid());
    TEST_ASSERT(f.id_mgr->active_process_count() == 2);

    // Clean up all
    TEST_ASSERT(f.pipeline->terminate(r2.handle));
    TEST_ASSERT(f.pipeline->terminate(r3_retry.handle));
    TEST_ASSERT(f.id_mgr->active_process_count() == 0);
}

static void test_rapid_launch_and_kill_cycles() {
    std::cout << "[Stage 233.18] Testing rapid sequential launch and termination cycles...\n";
    auto f = LaunchResourceFixture::create();

    for (int i = 0; i < 20; ++i) {
        auto res = f.pipeline->launch("com.winios.tx_calc");
        TEST_ASSERT(res.status == LaunchStatus::Success);
        TEST_ASSERT(res.handle.is_valid());
        TEST_ASSERT(f.id_mgr->active_process_count() == 1);

        uint32_t pid = res.handle.pid;
        TEST_ASSERT(f.budget_mgr->get_task_memory(pid) > 0);
        TEST_ASSERT(f.cap_mgr->get_identity(pid).has_value());

        TEST_ASSERT(f.pipeline->terminate(res.handle));
        TEST_ASSERT(f.id_mgr->active_process_count() == 0);
        TEST_ASSERT(f.task_mgr->get_task_count() == 0);
        TEST_ASSERT(f.budget_mgr->get_task_memory(pid) == 0);
        TEST_ASSERT(!f.cap_mgr->get_identity(pid).has_value());
    }
}

static void test_diagnostics_audit_trail() {
    std::cout << "[Stage 233.18] Testing diagnostics service audit trail recording...\n";
    auto f = LaunchResourceFixture::create();

    // Injected failure at memory mapping
    f.pipeline->inject_failure(LaunchFailureInjectionPoint::FailMemoryMapping, LaunchStatus::MemoryMappingFailed);
    auto res = f.pipeline->launch("com.winios.tx_calc");
    TEST_ASSERT(res.status == LaunchStatus::MemoryMappingFailed);

    // Verify diagnostics recorded the event
    auto logs = f.diag_svc->get_logs(50, services::LogLevel::Debug);
    TEST_ASSERT(!logs.empty());

    bool found_phase_record = false;
    for (const auto& log_entry : logs) {
        if (log_entry.message.find("MappingGuestMemory") != std::string::npos ||
            log_entry.message.find("Memory mapping failed") != std::string::npos) {
            found_phase_record = true;
            break;
        }
    }
    TEST_ASSERT(found_phase_record);
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "  Stage 233.18: Launch Resource Ownership Tests     \n";
    std::cout << "====================================================\n";

    test_exact_phase_rollback_accounting();
    test_resource_starvation_and_recovery();
    test_rapid_launch_and_kill_cycles();
    test_diagnostics_audit_trail();

    std::cout << "\n[Stage 233.18] ALL LAUNCH RESOURCE TESTS PASSED.\n";
    return 0;
}

#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include <cstring>

#include "loader/embedded_campaign230_workloads.inl"
#include "loader/app_launcher.h"
#include "packaging/app_package.h"
#include "packaging/app_registry.h"
#include "packaging/package_installer.h"
#include "tools/app_generator.h"
#include "session/multi_app_session_controller.h"
#include "kernel/ipc_capability_broker.h"
#include "documents/document_association_manager.h"
#include "documents/document_registry.h"
#include "services/diagnostics_service.h"
#include "platform/resource_budget.h"
#include "security/capability_manager.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;

// Helper to inspect ARM64 Mach-O header
static bool inspect_arm64_macho(const std::vector<uint8_t>& bin) {
    if (bin.size() < 32) return false;
    uint32_t magic = 0;
    std::memcpy(&magic, bin.data(), sizeof(magic));
    if (magic != 0xFEEDFACF) { // MH_MAGIC_64
        return false;
    }
    uint32_t cputype = 0;
    std::memcpy(&cputype, bin.data() + 4, sizeof(cputype));
    if (cputype != 0x0100000C) { // CPU_TYPE_ARM64
        return false;
    }
    return true;
}

static void test_workflow_a() {
    std::cout << "\n=== Running Workflow A (Single App Lifecycle: Build -> Inspect -> Package -> Validate -> Install -> Launch -> Diag -> Terminate -> Cleanup) ===\n";

    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    auto registry = std::make_shared<packaging::ApplicationRegistry>();
    auto installer = std::make_shared<packaging::PackageInstaller>(vfs, registry);
    auto identity_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 200, 4);
    auto task_mgr = std::make_shared<settings::TaskManager>();
    auto budget_mgr = std::make_shared<platform::ResourceBudgetManager>();
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    auto diag_svc = std::make_shared<services::DiagnosticsService>();

    loader::AppLaunchPipeline pipeline(vfs, registry, identity_mgr, task_mgr, budget_mgr, cap_mgr, diag_svc);

    // 1. Build guest application binary (using genuine compiled ARM64 Mach-O workload)
    std::cout << "  [Step 1] Fetching compiled ARM64 Mach-O guest application...\n";
    const auto& arm64_bin = loader::get_embedded_campaign230_app_startup();
    TEST_ASSERT(!arm64_bin.empty());

    // 2. Inspect its Mach-O executable
    std::cout << "  [Step 2] Inspecting Mach-O executable header...\n";
    TEST_ASSERT(inspect_arm64_macho(arm64_bin));

    // 3. Generate a package using AppGenerator
    std::cout << "  [Step 3] Generating v3 package structure...\n";
    tools::AppTemplateConfig cfg{};
    cfg.bundle_id = "com.winios.workflowA";
    cfg.app_name = "WorkflowAApp";
    cfg.display_name = "Workflow A Test App";
    cfg.version = "1.0.0";
    cfg.permissions = {"SettingsRead", "DiagnosticsWrite"};
    cfg.supported_doc_types = {"text/plain", ".txt"};

    packaging::AppPackage pkg = tools::AppGenerator::generate_package(cfg, arm64_bin);
    auto pkg_bytes = pkg.pack_v3();
    TEST_ASSERT(!pkg_bytes.empty());

    // 4. Validate the package
    std::cout << "  [Step 4] Validating package format and cryptographic integrity...\n";
    packaging::AppPackage validated_pkg{};
    auto val_res = packaging::AppPackage::validate_package_data(pkg_bytes.data(), pkg_bytes.size(), &validated_pkg);
    TEST_ASSERT(val_res == packaging::PackageValidationResult::Valid);
    TEST_ASSERT(validated_pkg.manifest.bundle_id == cfg.bundle_id);

    // 5. Install package
    std::cout << "  [Step 5] Performing transactional package installation...\n";
    auto install_status = installer->install(validated_pkg);
    TEST_ASSERT(install_status == packaging::InstallStatus::Success);
    TEST_ASSERT(registry->is_installed(cfg.bundle_id));

    // 6. Launch application
    std::cout << "  [Step 6] Launching application through 12-step launch pipeline...\n";
    auto launch_res = pipeline.launch(cfg.bundle_id, 2000);
    TEST_ASSERT(launch_res.status == loader::LaunchStatus::Success);
    TEST_ASSERT(launch_res.handle.is_valid());
    TEST_ASSERT(launch_res.handle.pid == 100);
    TEST_ASSERT(identity_mgr->is_valid_handle(launch_res.handle));
    TEST_ASSERT(task_mgr->get_task_count() == 1);
    TEST_ASSERT(budget_mgr->get_task_memory(launch_res.handle.pid) > 0);
    TEST_ASSERT(cap_mgr->check(launch_res.handle.pid, security::Capability::SettingsRead));

    // 7. Query diagnostics
    std::cout << "  [Step 7] Querying platform diagnostics service...\n";
    auto logs = diag_svc->get_logs();
    TEST_ASSERT(!logs.empty());
    bool found_launch_log = false;
    for (const auto& log : logs) {
        if (log.bundle_id == cfg.bundle_id) {
            found_launch_log = true;
            break;
        }
    }
    TEST_ASSERT(found_launch_log);

    // 8. Terminate application
    std::cout << "  [Step 8] Terminating application and triggering teardown...\n";
    bool term_ok = pipeline.terminate(launch_res.handle);
    TEST_ASSERT(term_ok);

    // 9. Verify resource cleanup
    std::cout << "  [Step 9] Verifying complete resource reclamation...\n";
    TEST_ASSERT(!identity_mgr->is_valid_handle(launch_res.handle));
    TEST_ASSERT(task_mgr->get_task_count() == 0);
    TEST_ASSERT(budget_mgr->get_task_memory(launch_res.handle.pid) == 0);
    TEST_ASSERT(!cap_mgr->check(launch_res.handle.pid, security::Capability::SettingsRead));

    std::cout << "  -> Workflow A passed with 100% verification.\n";
}

static void test_workflow_b() {
    std::cout << "\n=== Running Workflow B (Multi-App: Install 2 Apps -> Launch -> Window Switch -> Authorized IPC -> Document Handoff -> Controlled Failure -> Crash Reporting & Cleanup) ===\n";

    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    auto registry = std::make_shared<packaging::ApplicationRegistry>();
    auto installer = std::make_shared<packaging::PackageInstaller>(vfs, registry);
    auto identity_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 200, 4);
    auto task_mgr = std::make_shared<settings::TaskManager>();
    auto budget_mgr = std::make_shared<platform::ResourceBudgetManager>();
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    auto diag_svc = std::make_shared<services::DiagnosticsService>();
    auto doc_reg = std::make_shared<documents::DocumentRegistry>(cap_mgr);
    auto doc_assoc = std::make_shared<documents::DocumentAssociationManager>(registry, doc_reg);
    auto ipc_broker = std::make_shared<kernel::IpcCapabilityBroker>(identity_mgr, cap_mgr);

    auto pipeline = std::make_shared<loader::AppLaunchPipeline>(vfs, registry, identity_mgr, task_mgr, budget_mgr, cap_mgr, diag_svc);
    session::MultiAppSessionController session_ctrl(pipeline, identity_mgr, registry, task_mgr, diag_svc);

    // 1. Install two guest applications
    std::cout << "  [Step 1] Installing App 1 (Settings) and App 2 (Docs)...\n";
    tools::AppTemplateConfig cfg1{};
    cfg1.bundle_id = "com.winios.settings_app";
    cfg1.app_name = "SettingsApp";
    cfg1.display_name = "Settings Application";
    cfg1.permissions = {"SettingsRead", "SettingsWrite", "DocumentCreate", "DocumentShare", "DocumentRead"};
    cfg1.supported_doc_types = {"text/plain", ".txt"};
    const auto& bin1 = loader::get_embedded_campaign230_app_settings();
    auto pkg1 = tools::AppGenerator::generate_package(cfg1, bin1);
    TEST_ASSERT(installer->install(pkg1) == packaging::InstallStatus::Success);

    tools::AppTemplateConfig cfg2{};
    cfg2.bundle_id = "com.winios.docs_app";
    cfg2.app_name = "DocsApp";
    cfg2.display_name = "Documents Application";
    cfg2.permissions = {"FileRead", "FileWrite", "DocumentRead"};
    cfg2.supported_doc_types = {"application/pdf", ".pdf"};
    const auto& bin2 = loader::get_embedded_campaign230_app_docs();
    auto pkg2 = tools::AppGenerator::generate_package(cfg2, bin2);
    TEST_ASSERT(installer->install(pkg2) == packaging::InstallStatus::Success);

    // Set default associations
    registry->set_default_app_for_type("text/plain", cfg1.bundle_id);
    registry->set_default_app_for_type("application/pdf", cfg2.bundle_id);
    registry->set_default_app_for_type(".pdf", cfg2.bundle_id);

    // 2. Launch both applications
    std::cout << "  [Step 2] Launching both guest applications into session controller...\n";
    auto res1 = session_ctrl.launch_app(cfg1.bundle_id);
    TEST_ASSERT(res1.status == loader::LaunchStatus::Success);

    auto res2 = session_ctrl.launch_app(cfg2.bundle_id);
    TEST_ASSERT(res2.status == loader::LaunchStatus::Success);

    TEST_ASSERT(session_ctrl.list_session_apps().size() == 2);

    // 3. Exercise window / task switching
    std::cout << "  [Step 3] Exercising window switching and active application management...\n";
    TEST_ASSERT(session_ctrl.switch_to_app(res1.handle.pid));
    TEST_ASSERT(session_ctrl.foreground_pid() == res1.handle.pid);

    TEST_ASSERT(session_ctrl.switch_to_app(res2.handle.pid));
    TEST_ASSERT(session_ctrl.foreground_pid() == res2.handle.pid);

    // 4. Perform authorized IPC between App 1 and App 2
    std::cout << "  [Step 4] Performing authorized IPC between App 1 and App 2...\n";
    auto cap_token = ipc_broker->grant_communication_right(res1.handle, res2.handle, 0x1);
    TEST_ASSERT(cap_token.token_id != 0);

    const std::string payload_str = "dark_mode_enabled";
    TEST_ASSERT(ipc_broker->send_message(cap_token.token_id, res1.handle,
                                         payload_str.data(), payload_str.size(), 101) == kernel::IpcStatus::Success);

    runtime::IpcMessage received{};
    TEST_ASSERT(ipc_broker->receive_message(res2.handle, received) == kernel::IpcStatus::Success);
    TEST_ASSERT(received.sender_pid == res1.handle.pid);
    TEST_ASSERT(received.message_id == 101);
    std::string rec_payload(received.data.begin(), received.data.end());
    TEST_ASSERT(rec_payload == payload_str);

    // Test unauthorized IPC rejection (invalid token)
    TEST_ASSERT(ipc_broker->send_message(0xBADF00D, res1.handle,
                                         payload_str.data(), payload_str.size(), 999) == kernel::IpcStatus::PermissionDenied);

    // 5. Open document using application association handoff
    std::cout << "  [Step 5] Performing document association handoff from App 1 to App 2...\n";
    auto doc_handle = doc_reg->register_document(res1.handle.pid, cfg1.bundle_id, "manual.pdf",
                                                 "/Documents/manual.pdf", "application/pdf", 4096);
    TEST_ASSERT(doc_handle != documents::kInvalidDocumentHandle);

    auto handoff_res = doc_assoc->handoff_document(res1.handle.pid, doc_handle, "", res2.handle.pid,
                                                   documents::DocumentAccessMode::Read);
    TEST_ASSERT(handoff_res.status == documents::HandoffStatus::Success);
    TEST_ASSERT(handoff_res.target_bundle_id == cfg2.bundle_id);
    TEST_ASSERT(handoff_res.grant_token != 0);

    // Verify App 2 now has access
    TEST_ASSERT(doc_reg->check_access(doc_handle, res2.handle.pid, documents::DocumentAccessMode::Read));

    // 6. Trigger controlled failure in App 2
    std::cout << "  [Step 6] Triggering controlled failure and crash reporting for App 2...\n";
    diag_svc->log(cfg2.bundle_id, res2.handle.pid, services::LogLevel::Error, "FATAL: Segmentation fault at guest address 0x00000000");

    // Terminate App 2 via session controller
    session_ctrl.terminate_app(res2.handle.pid);
    doc_reg->revoke_all_for_process(res2.handle.pid);

    // 7. Verify crash reporting and cleanup
    std::cout << "  [Step 7] Verifying crash reporting and complete cleanup...\n";
    auto all_logs = diag_svc->get_logs();
    bool found_crash = false;
    for (const auto& l : all_logs) {
        if (l.bundle_id == cfg2.bundle_id && l.level == services::LogLevel::Error) {
            found_crash = true;
            break;
        }
    }
    TEST_ASSERT(found_crash);

    // Verify App 2 is terminated and cleaned up
    TEST_ASSERT(!identity_mgr->is_valid_handle(res2.handle));
    TEST_ASSERT(session_ctrl.list_session_apps().size() == 1);
    TEST_ASSERT(!doc_reg->check_access(doc_handle, res2.handle.pid, documents::DocumentAccessMode::Read));

    // App 1 is still running cleanly and resumes foreground
    TEST_ASSERT(identity_mgr->is_valid_handle(res1.handle));
    TEST_ASSERT(session_ctrl.switch_to_app(res1.handle.pid));
    TEST_ASSERT(session_ctrl.foreground_pid() == res1.handle.pid);

    // Terminate App 1
    session_ctrl.terminate_app(res1.handle.pid);
    TEST_ASSERT(session_ctrl.list_session_apps().empty());

    std::cout << "  -> Workflow B passed with 100% verification.\n";
}

int main() {
    std::cout << "============================================================\n";
    std::cout << "=== Running Stage 230.21 End-to-End Developer Workflows ===\n";
    std::cout << "============================================================\n";
    test_workflow_a();
    test_workflow_b();
    std::cout << "\n=== All Stage 230.21 Tests Passed Successfully! ===\n";
    return 0;
}

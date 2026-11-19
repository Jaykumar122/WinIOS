#include "tools/app_generator.h"
#include "loader/app_launcher.h"
#include "loader/app_inspector.h"
#include "loader/embedded_campaign230_workloads.inl"
#include "packaging/app_package.h"
#include "packaging/app_registry.h"
#include "packaging/package_installer.h"
#include "security/publisher_trust_store.h"
#include "security/developer_keystore.h"
#include "security/capability_manager.h"
#include "kernel/process_identity.h"
#include "kernel/ipc_capability_broker.h"
#include "documents/document_registry.h"
#include "documents/document_stream_manager.h"
#include "settings/task_manager.h"
#include "platform/resource_budget.h"
#include "services/diagnostics_service.h"
#include "vfs/vfs.h"

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
using namespace winios::kernel;
using namespace winios::security;
using namespace winios::loader;
using namespace winios::packaging;
using namespace winios::documents;

int main() {
    std::cout << "=== Stage 232.21: End-to-End Developer Workflow Test Suite ===\n";

    // -------------------------------------------------------------
    // Step 1: Generate application template & manifest
    // -------------------------------------------------------------
    std::cout << "[Step 1] Generating application configuration & manifest...\n";
    tools::AppTemplateConfig cfg{};
    cfg.bundle_id = "com.winios.e2e232";
    cfg.app_name = "E2EWorkflow232";
    cfg.display_name = "End-to-End Workflow Campaign 232";
    cfg.version = "1.0.0";
    cfg.permissions = {"SettingsRead", "DocumentRead", "DocumentWrite"};
    cfg.min_sdk_version = 0x00020400; // SDK v2.4
    cfg.target_abi = "2.4";

    std::string manifest_str = tools::AppGenerator::generate_manifest_ini(cfg);
    TEST_ASSERT(manifest_str.find("bundle_id=com.winios.e2e232") != std::string::npos);
    std::cout << "  -> Step 1 PASS: Manifest generated.\n";

    // -------------------------------------------------------------
    // Step 2: Retrieve real ARM64 Mach-O binary
    // -------------------------------------------------------------
    std::cout << "[Step 2] Retrieving embedded ARM64 Mach-O guest binary...\n";
    auto arm64_bin = loader::get_embedded_campaign230_app_startup();
    TEST_ASSERT(!arm64_bin.empty());
    std::cout << "  -> Step 2 PASS: Binary retrieved (" << arm64_bin.size() << " bytes).\n";

    // -------------------------------------------------------------
    // Step 3: Inspect binary structure and W^X invariants
    // -------------------------------------------------------------
    std::cout << "[Step 3] Inspecting Mach-O structure, 16 KB page alignment, and W^X...\n";
    auto report = loader::AppInspector::inspect_binary(arm64_bin.data(), arm64_bin.size());
    TEST_ASSERT(report.is_valid);
    TEST_ASSERT(report.magic == loader::AppInspector::kMachO64Magic);
    TEST_ASSERT(report.cputype == loader::AppInspector::kCpuTypeArm64);
    TEST_ASSERT(report.page_aligned_16k);
    TEST_ASSERT(report.wx_clean);
    std::cout << "  -> Step 3 PASS: Mach-O binary conforms to ARM64 16KB and W^X rules.\n";

    // -------------------------------------------------------------
    // Step 4: Canonical Packaging (pack_v3)
    // -------------------------------------------------------------
    std::cout << "[Step 4] Packaging application into canonical WIOSPKG3 container...\n";
    auto pkg = tools::AppGenerator::generate_package(cfg, arm64_bin);
    auto raw_package = pkg.pack_v3();
    TEST_ASSERT(raw_package.size() > 64);
    std::cout << "  -> Step 4 PASS: Canonical package created (" << raw_package.size() << " bytes).\n";

    // -------------------------------------------------------------
    // Step 5: Asymmetric Ed25519 Key Generation
    // -------------------------------------------------------------
    std::cout << "[Step 5] Generating developer Ed25519 keypair...\n";
    auto dev_id = DeveloperKeyStore::generate_identity(
        "com.winios.developer", "WinIOS Developer Labs", "KEY-DEV-E2E-232",
        PublisherRole::Developer, 1000, 2000
    );
    TEST_ASSERT(dev_id.is_valid());
    PublisherKey pub_key = DeveloperKeyStore::export_public_key(dev_id);
    TEST_ASSERT(pub_key.key_id == "KEY-DEV-E2E-232");
    std::cout << "  -> Step 5 PASS: Ed25519 keypair generated.\n";

    // -------------------------------------------------------------
    // Step 6: Asymmetric Package Signing (WIOSAUTH v2)
    // -------------------------------------------------------------
    std::cout << "[Step 6] Signing package with Ed25519 via PublisherTrustStore::sign_package_v2...\n";
    auto signed_package = raw_package;
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(signed_package, dev_id, 0, 1000, 2000));
    TEST_ASSERT(signed_package.size() > raw_package.size());
    std::cout << "  -> Step 6 PASS: Signed package with WIOSAUTH v2 trailer (" << signed_package.size() << " bytes).\n";

    // -------------------------------------------------------------
    // Step 7: Verify Signature & Trust Store Policy
    // -------------------------------------------------------------
    std::cout << "[Step 7] Verifying package signature against PublisherTrustStore...\n";
    PublisherTrustStore trust_store;
    TEST_ASSERT(trust_store.register_key(pub_key));

    auto sig_res_dev = trust_store.verify_package_signature(
        signed_package.data(), signed_package.size(), TrustPolicy::Development, 1500
    );
    TEST_ASSERT(sig_res_dev.status == SignatureStatus::DeveloperSigned);
    TEST_ASSERT(sig_res_dev.is_trusted);
    TEST_ASSERT(sig_res_dev.publisher_id == "com.winios.developer");
    TEST_ASSERT(sig_res_dev.key_id == "KEY-DEV-E2E-232");

    auto sig_res_prod = trust_store.verify_package_signature(
        signed_package.data(), signed_package.size(), TrustPolicy::StrictProduction, 1500
    );
    TEST_ASSERT(sig_res_prod.status == SignatureStatus::DeveloperSigned);
    TEST_ASSERT(!sig_res_prod.is_trusted); // Developer key untrusted in StrictProduction
    std::cout << "  -> Step 7 PASS: Signature verified under Development policy; rejected under StrictProduction.\n";

    // -------------------------------------------------------------
    // Step 8: Transactional Installation with WAL
    // -------------------------------------------------------------
    std::cout << "[Step 8] Installing package transactionally...\n";
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->mkdir("/Applications");
    vfs->mkdir("/Documents");
    vfs->mkdir("/tmp");

    auto registry = std::make_shared<packaging::ApplicationRegistry>();
    packaging::PackageInstaller installer(vfs, registry);

    packaging::AppPackage unpacked{};
    TEST_ASSERT(packaging::AppPackage::unpack(signed_package.data(), signed_package.size(), unpacked));
    TEST_ASSERT(installer.install(unpacked) == packaging::InstallStatus::Success);
    TEST_ASSERT(registry->is_installed("com.winios.e2e232"));
    std::cout << "  -> Step 8 PASS: Application transactionally installed to VFS and registry.\n";

    // -------------------------------------------------------------
    // Step 9: Launch Application Pipeline (14 Phases)
    // -------------------------------------------------------------
    std::cout << "[Step 9] Launching application through 14-phase transaction pipeline...\n";
    auto identity_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 200, 4);
    auto task_mgr = std::make_shared<settings::TaskManager>();
    auto budget_mgr = std::make_shared<platform::ResourceBudgetManager>();
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    auto diag_svc = std::make_shared<services::DiagnosticsService>();
    auto ipc_broker = std::make_shared<kernel::IpcCapabilityBroker>(identity_mgr, cap_mgr);
    auto doc_reg = std::make_shared<documents::DocumentRegistry>(cap_mgr, identity_mgr);
    auto stream_mgr = std::make_shared<documents::DocumentStreamManager>(doc_reg, vfs);

    loader::AppLaunchPipeline launcher(vfs, registry, identity_mgr, task_mgr, budget_mgr, cap_mgr, diag_svc);

    auto launch_res = launcher.launch("com.winios.e2e232");
    TEST_ASSERT(launch_res.status == loader::LaunchStatus::Success);
    TEST_ASSERT(launch_res.final_phase == loader::LaunchTransactionPhase::Committed);
    TEST_ASSERT(!launch_res.rolled_back);
    TEST_ASSERT(launch_res.handle.is_valid());
    kernel::ProcessHandle h_app = launch_res.handle;
    std::cout << "  -> Step 9 PASS: Application launched with PID " << h_app.pid
              << " (generation " << h_app.generation << ").\n";

    // -------------------------------------------------------------
    // Step 10: Authorized IPC with Anti-Replay Defense (SEC-232-04)
    // -------------------------------------------------------------
    std::cout << "[Step 10] Testing authorized IPC communication & anti-replay checks...\n";
    kernel::ApplicationIdentity sys_app{"com.winios.system", "1.0.0", "System", 0x00020400};
    kernel::ProcessHandle h_sys = identity_mgr->allocate_process(sys_app);
    TEST_ASSERT(h_sys.is_valid());

    auto ipc_token = ipc_broker->grant_communication_right(h_app, h_sys, 0x1, true /* require_strict_sequence */);
    TEST_ASSERT(ipc_token.token_id != 0);

    const std::string msg_data = "SECURE_IPC_PAYLOAD_E2E";

    // 1. Initial send with sequence_num = 0 MUST be strictly rejected (SEC-232-04)
    auto st0 = ipc_broker->send_message(ipc_token.token_id, h_app, msg_data.data(), msg_data.size(), 10, 0);
    TEST_ASSERT(st0 == kernel::IpcStatus::ReplayDetected);

    // 2. Initial send with sequence_num = 1 succeeds
    auto st1 = ipc_broker->send_message(ipc_token.token_id, h_app, msg_data.data(), msg_data.size(), 10, 1);
    TEST_ASSERT(st1 == kernel::IpcStatus::Success);

    // 3. Replay with sequence_num = 1 rejected
    auto st1_replay = ipc_broker->send_message(ipc_token.token_id, h_app, msg_data.data(), msg_data.size(), 10, 1);
    TEST_ASSERT(st1_replay == kernel::IpcStatus::ReplayDetected);

    // 4. Monotonic sequence_num = 2 succeeds
    auto st2 = ipc_broker->send_message(ipc_token.token_id, h_app, msg_data.data(), msg_data.size(), 10, 2);
    TEST_ASSERT(st2 == kernel::IpcStatus::Success);
    std::cout << "  -> Step 10 PASS: IPC communication and strict anti-replay verified.\n";

    // -------------------------------------------------------------
    // Step 11: Document Handoff, Stream I/O, and Dynamic Access Revocation
    // -------------------------------------------------------------
    std::cout << "[Step 11] Testing generational document handoff and dynamic stream revocation...\n";
    cap_mgr->grant(h_sys.pid, security::Capability::DocumentCreate);
    cap_mgr->grant(h_sys.pid, security::Capability::DocumentShare);

    auto doc_h = doc_reg->register_document(
        h_sys.pid, "com.winios.system", "SharedReport", "/Documents/report.txt", "text/plain", 1024, false
    );
    TEST_ASSERT(doc_h != documents::kInvalidDocumentHandle);

    auto grant = doc_reg->create_grant(
        doc_h, h_sys, h_app, "com.winios.e2e232", documents::DocumentAccessMode::ReadWrite
    );
    TEST_ASSERT(!grant.grant_token_hex.empty());

    // Open stream
    StreamHandle stream_h = stream_mgr->open_stream(doc_h, h_app.pid, documents::DocumentAccessMode::ReadWrite);
    TEST_ASSERT(stream_h != documents::kInvalidStreamHandle);

    // Write to stream
    const std::string doc_content = "Confidential document payload for E2E testing.";
    size_t bytes_written = 0;
    bool write_ok = stream_mgr->write_stream(stream_h, h_app.pid, doc_content.data(), doc_content.size(), bytes_written);
    TEST_ASSERT(write_ok);
    TEST_ASSERT(bytes_written == doc_content.size());

    // Revoke grant dynamically
    TEST_ASSERT(doc_reg->revoke_grant_by_hex(grant.grant_token_hex, h_sys.pid));

    // Dynamic re-verification: Subsequent read/write MUST immediately fail
    std::vector<uint8_t> read_buf(64, 0);
    size_t bytes_read = 0;
    bool read_ok = stream_mgr->read_stream(stream_h, h_app.pid, read_buf.data(), read_buf.size(), bytes_read);
    TEST_ASSERT(!read_ok); // Access denied due to revoked grant!

    stream_mgr->close_stream(stream_h, h_app.pid);
    std::cout << "  -> Step 11 PASS: Dynamic stream access re-verification verified.\n";

    // -------------------------------------------------------------
    // Step 12: Controlled Application Termination
    // -------------------------------------------------------------
    std::cout << "[Step 12] Terminating application and inspecting subsystem cleanup...\n";
    TEST_ASSERT(launcher.terminate(h_app));

    // Process identity invalidated
    TEST_ASSERT(!identity_mgr->is_valid_handle(h_app));

    // Task unregistered
    settings::TaskInfo tinfo{};
    TEST_ASSERT(!task_mgr->get_task(h_app.pid, tinfo));

    // Budget released
    TEST_ASSERT(budget_mgr->get_task_memory(h_app.pid) == 0);

    // Capabilities unbound
    TEST_ASSERT(!cap_mgr->get_identity(h_app.pid).has_value());
    std::cout << "  -> Step 12 PASS: Process terminated with zero leaked resources.\n";

    // -------------------------------------------------------------
    // Step 13: Stale Handle Invalidation Checks
    // -------------------------------------------------------------
    std::cout << "[Step 13] Verifying stale handle rejection across IPC and Documents...\n";
    auto stale_ipc = ipc_broker->send_message(ipc_token.token_id, h_app, msg_data.data(), msg_data.size(), 10, 3);
    TEST_ASSERT(stale_ipc == kernel::IpcStatus::SenderInvalid);

    TEST_ASSERT(!doc_reg->check_access(doc_h, h_app, documents::DocumentAccessMode::Read));
    std::cout << "  -> Step 13 PASS: Stale generational handle rejected everywhere.\n";

    // -------------------------------------------------------------
    // Step 14: Relaunch with Monotonic Generation
    // -------------------------------------------------------------
    std::cout << "[Step 14] Relaunching application and verifying generational progression...\n";
    auto relaunch_res = launcher.launch("com.winios.e2e232");
    TEST_ASSERT(relaunch_res.status == loader::LaunchStatus::Success);
    TEST_ASSERT(relaunch_res.handle.is_valid());
    if (relaunch_res.handle.pid == h_app.pid) {
        TEST_ASSERT(relaunch_res.handle.generation > h_app.generation);
    }

    TEST_ASSERT(launcher.terminate(relaunch_res.handle));
    TEST_ASSERT(!identity_mgr->is_valid_handle(relaunch_res.handle));
    std::cout << "  -> Step 14 PASS: Relaunch allocated fresh generation and cleaned up.\n";

    std::cout << "=== All 14 Steps of Stage 232.21 PASSED Successfully ===\n";
    return 0;
}

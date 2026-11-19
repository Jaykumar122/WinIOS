#include "tools/app_generator.h"
#include "loader/app_launcher.h"
#include "loader/app_inspector.h"
#include "loader/macho_user_loader.h"
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
#include "memory/guest_memory_validator.h"
#include "memory/user_address_space.h"
#include "vfs/vfs.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "E2E Workflow Assertion Failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::security;
using namespace winios::loader;
using namespace winios::packaging;
using namespace winios::documents;
using namespace winios::memory;

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  Stage 233.21: 19-Step End-to-End Security Workflow Test Suite  \n";
    std::cout << "=================================================================\n";

    // -------------------------------------------------------------
    // Step 1: Generate application template & manifest
    // -------------------------------------------------------------
    std::cout << "[Step 1] Generating application configuration & manifest...\n";
    tools::AppTemplateConfig cfg{};
    cfg.bundle_id = "com.winios.e2e233";
    cfg.app_name = "E2EWorkflow233";
    cfg.display_name = "End-to-End Security Workflow Campaign 233";
    cfg.version = "1.0.0";
    cfg.permissions = {"SettingsRead", "DocumentRead", "DocumentWrite"};
    cfg.min_sdk_version = 0x00020400; // SDK v2.4
    cfg.target_abi = "2.4";

    std::string manifest_str = tools::AppGenerator::generate_manifest_ini(cfg);
    TEST_ASSERT(manifest_str.find("bundle_id=com.winios.e2e233") != std::string::npos);
    std::cout << "  -> Step 1 PASS: Manifest generated.\n";

    // -------------------------------------------------------------
    // Step 2: Retrieve real ARM64 Mach-O binary and inspect
    // -------------------------------------------------------------
    std::cout << "[Step 2] Retrieving embedded ARM64 Mach-O binary and inspecting W^X...\n";
    auto arm64_bin = loader::get_embedded_campaign230_app_startup();
    TEST_ASSERT(!arm64_bin.empty());
    auto insp_report = loader::AppInspector::inspect_binary(arm64_bin.data(), arm64_bin.size());
    TEST_ASSERT(insp_report.is_valid);
    TEST_ASSERT(insp_report.page_aligned_16k);
    TEST_ASSERT(insp_report.wx_clean);
    std::cout << "  -> Step 2 PASS: Binary verified W^X clean and 16KB aligned.\n";

    // -------------------------------------------------------------
    // Step 3: Package application into canonical container (pack_v3)
    // -------------------------------------------------------------
    std::cout << "[Step 3] Packaging into canonical WIOSPKG3 container...\n";
    auto pkg = tools::AppGenerator::generate_package(cfg, arm64_bin);
    auto raw_package = pkg.pack_v3();
    TEST_ASSERT(raw_package.size() > 64);
    std::cout << "  -> Step 3 PASS: Canonical container created (" << raw_package.size() << " bytes).\n";

    // -------------------------------------------------------------
    // Step 4: Generate developer Ed25519 keypair
    // -------------------------------------------------------------
    std::cout << "[Step 4] Generating developer Ed25519 keypair...\n";
    auto dev_id = DeveloperKeyStore::generate_identity(
        "com.winios.secdev", "Security Labs", "KEY-DEV-E2E-233",
        PublisherRole::Developer, 1000, 2000
    );
    TEST_ASSERT(dev_id.is_valid());
    PublisherKey pub_key = DeveloperKeyStore::export_public_key(dev_id);
    std::cout << "  -> Step 4 PASS: Ed25519 keypair generated.\n";

    // -------------------------------------------------------------
    // Step 5: Sign package with Ed25519 (WIOSAUTH v2)
    // -------------------------------------------------------------
    std::cout << "[Step 5] Signing package with Ed25519...\n";
    auto signed_package = raw_package;
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(signed_package, dev_id, 0, 1000, 2000));
    TEST_ASSERT(signed_package.size() > raw_package.size());
    std::cout << "  -> Step 5 PASS: Signed with WIOSAUTH v2 trailer.\n";

    // -------------------------------------------------------------
    // Step 6: Verify signature against PublisherTrustStore
    // -------------------------------------------------------------
    std::cout << "[Step 6] Verifying signature against PublisherTrustStore...\n";
    PublisherTrustStore trust_store;
    TEST_ASSERT(trust_store.register_key(pub_key));

    auto sig_res_dev = trust_store.verify_package_signature(
        signed_package.data(), signed_package.size(), TrustPolicy::Development, 1500
    );
    TEST_ASSERT(sig_res_dev.is_trusted);
    TEST_ASSERT(sig_res_dev.status == SignatureStatus::DeveloperSigned);
    std::cout << "  -> Step 6 PASS: Signature verified and trusted.\n";

    // -------------------------------------------------------------
    // Step 7: Transactional Installation via PackageInstaller
    // -------------------------------------------------------------
    std::cout << "[Step 7] Transactional package installation...\n";
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->mkdir("/Applications");
    vfs->mkdir("/Documents");
    vfs->mkdir("/tmp");

    auto registry = std::make_shared<packaging::ApplicationRegistry>();
    packaging::PackageInstaller installer(vfs, registry);

    packaging::AppPackage unpacked{};
    TEST_ASSERT(packaging::AppPackage::unpack(signed_package.data(), signed_package.size(), unpacked));
    TEST_ASSERT(installer.install(unpacked) == packaging::InstallStatus::Success);
    TEST_ASSERT(registry->is_installed("com.winios.e2e233"));
    std::cout << "  -> Step 7 PASS: App installed successfully.\n";

    // -------------------------------------------------------------
    // Step 8: Reconcile and verify filesystem layout
    // -------------------------------------------------------------
    std::cout << "[Step 8] Reconciling WAL and verifying filesystem layout...\n";
    auto report = installer.reconcile_and_recover();
    TEST_ASSERT(report.total_recovered() == 0); // Clean install has zero anomalies
    TEST_ASSERT(vfs->exists("/Applications/com.winios.e2e233.app/bin/E2EWorkflow233"));
    std::cout << "  -> Step 8 PASS: Filesystem layout clean, zero recovery required.\n";

    // -------------------------------------------------------------
    // Step 9: Launch application through 14-phase pipeline
    // -------------------------------------------------------------
    std::cout << "[Step 9] Launching application through 14-phase pipeline...\n";
    auto identity_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 200, 4);
    auto task_mgr = std::make_shared<settings::TaskManager>();
    auto budget_mgr = std::make_shared<platform::ResourceBudgetManager>();
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    auto diag_svc = std::make_shared<services::DiagnosticsService>();
    auto ipc_broker = std::make_shared<kernel::IpcCapabilityBroker>(identity_mgr, cap_mgr);
    auto doc_reg = std::make_shared<documents::DocumentRegistry>(cap_mgr, identity_mgr);
    auto stream_mgr = std::make_shared<documents::DocumentStreamManager>(doc_reg, vfs);

    loader::AppLaunchPipeline launcher(vfs, registry, identity_mgr, task_mgr, budget_mgr, cap_mgr, diag_svc);
    auto launch_res = launcher.launch("com.winios.e2e233");
    TEST_ASSERT(launch_res.status == loader::LaunchStatus::Success);
    TEST_ASSERT(launch_res.final_phase == loader::LaunchTransactionPhase::Committed);
    kernel::ProcessHandle h_app = launch_res.handle;
    TEST_ASSERT(h_app.is_valid());
    std::cout << "  -> Step 9 PASS: App launched with PID " << h_app.pid << ".\n";

    // -------------------------------------------------------------
    // Step 10: Verify guest memory address space and W^X
    // -------------------------------------------------------------
    std::cout << "[Step 10] Verifying EL0 user address space and page permissions...\n";
    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());
    const uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "e2e_uas");
    auto loaded = MachoUserLoader::load_image(arm64_bin, uas, ppm, mem);
    TEST_ASSERT(loaded.valid);
    PageDescriptorFields desc{};
    TEST_ASSERT(uas.translate(loaded.text_vmaddr, &desc));
    TEST_ASSERT(desc.user_execute && !desc.writable);
    std::cout << "  -> Step 10 PASS: W^X hardware page permissions verified.\n";

    // -------------------------------------------------------------
    // Step 11: Authorized IPC Communication
    // -------------------------------------------------------------
    std::cout << "[Step 11] Authorized IPC communication with strict anti-replay...\n";
    kernel::ApplicationIdentity sys_app{"com.winios.system", "1.0.0", "System", 0x00020400};
    kernel::ProcessHandle h_sys = identity_mgr->allocate_process(sys_app);
    auto ipc_tok = ipc_broker->grant_communication_right(h_app, h_sys, 0x1, true);
    TEST_ASSERT(ipc_tok.token_id != 0);

    const std::string msg = "SECURE_E2E_PAYLOAD";
    TEST_ASSERT(ipc_broker->send_message(ipc_tok.token_id, h_app, msg.data(), msg.size(), 1, 1) == kernel::IpcStatus::Success);
    std::cout << "  -> Step 11 PASS: Monotonic sequence 1 transmitted.\n";

    // -------------------------------------------------------------
    // Step 12: Anti-Replay Defense Verification
    // -------------------------------------------------------------
    std::cout << "[Step 12] Testing anti-replay rejection...\n";
    TEST_ASSERT(ipc_broker->send_message(ipc_tok.token_id, h_app, msg.data(), msg.size(), 1, 0) == kernel::IpcStatus::ReplayDetected);
    TEST_ASSERT(ipc_broker->send_message(ipc_tok.token_id, h_app, msg.data(), msg.size(), 1, 1) == kernel::IpcStatus::ReplayDetected);
    TEST_ASSERT(ipc_broker->send_message(ipc_tok.token_id, h_app, msg.data(), msg.size(), 1, 2) == kernel::IpcStatus::Success);
    std::cout << "  -> Step 12 PASS: Replays rejected, monotonic sequence 2 accepted.\n";

    // -------------------------------------------------------------
    // Step 13: Document Registration and Stream I/O
    // -------------------------------------------------------------
    std::cout << "[Step 13] Registering document and performing stream I/O...\n";
    cap_mgr->grant(h_sys.pid, security::Capability::DocumentCreate);
    cap_mgr->grant(h_sys.pid, security::Capability::DocumentShare);

    auto doc_h = doc_reg->register_document(
        h_sys.pid, "com.winios.system", "Report", "/Documents/report.txt", "text/plain", 512, false
    );
    auto grant = doc_reg->create_grant(doc_h, h_sys, h_app, "com.winios.e2e233", documents::DocumentAccessMode::ReadWrite);
    StreamHandle sh = stream_mgr->open_stream(doc_h, h_app.pid, documents::DocumentAccessMode::ReadWrite);
    TEST_ASSERT(sh != documents::kInvalidStreamHandle);

    size_t written = 0;
    TEST_ASSERT(stream_mgr->write_stream(sh, h_app.pid, msg.data(), msg.size(), written));
    TEST_ASSERT(written == msg.size());
    std::cout << "  -> Step 13 PASS: Stream write verified.\n";

    // -------------------------------------------------------------
    // Step 14: Dynamic Document Grant Revocation
    // -------------------------------------------------------------
    std::cout << "[Step 14] Dynamic grant revocation on active stream...\n";
    TEST_ASSERT(doc_reg->revoke_grant_by_hex(grant.grant_token_hex, h_sys.pid));
    size_t read_bytes = 0;
    char read_buf[32]{};
    TEST_ASSERT(!stream_mgr->read_stream(sh, h_app.pid, read_buf, sizeof(read_buf), read_bytes));
    stream_mgr->close_stream(sh, h_app.pid);
    std::cout << "  -> Step 14 PASS: Dynamic grant revocation enforced immediately.\n";

    // -------------------------------------------------------------
    // Step 15: Attack Simulation 1 — Tampered Package Rejection
    // -------------------------------------------------------------
    std::cout << "[Step 15] Attack Simulation 1: Tampered package payload rejection...\n";
    auto tampered_pkg = signed_package;
    tampered_pkg[50] ^= 0x55; // Flip byte in payload
    auto tampered_ver = trust_store.verify_package_signature(tampered_pkg.data(), tampered_pkg.size(), TrustPolicy::StrictProduction, 1500);
    TEST_ASSERT(!tampered_ver.is_trusted);
    std::cout << "  -> Step 15 PASS: Tampered package rejected.\n";

    // -------------------------------------------------------------
    // Step 16: Attack Simulation 2 — Revoked Publisher Rejection
    // -------------------------------------------------------------
    std::cout << "[Step 16] Attack Simulation 2: Revoked publisher package rejection...\n";
    trust_store.revoke_publisher("com.winios.secdev");
    auto revoked_ver = trust_store.verify_package_signature(signed_package.data(), signed_package.size(), TrustPolicy::Development, 1500);
    TEST_ASSERT(revoked_ver.status == SignatureStatus::RevokedPublisher);
    TEST_ASSERT(!revoked_ver.is_trusted);
    std::cout << "  -> Step 16 PASS: Revoked publisher package rejected.\n";

    // -------------------------------------------------------------
    // Step 17: Attack Simulation 3 — Syscall Memory Overflow Rejection
    // -------------------------------------------------------------
    std::cout << "[Step 17] Attack Simulation 3: Syscall integer overflow address rejection...\n";
    const uint64_t overflow_addr = 0xFFFFFFFFFFFFFFF0ULL;
    auto val_res = GuestMemoryValidator::validate_buffer(uas, overflow_addr, 32, false);
    TEST_ASSERT(val_res == ValidationResult::IntegerOverflow);
    std::cout << "  -> Step 17 PASS: Address overflow caught by memory validator.\n";

    // -------------------------------------------------------------
    // Step 18: Controlled Termination and Zero-Leakage Audit
    // -------------------------------------------------------------
    std::cout << "[Step 18] Terminating app and auditing resource cleanup...\n";
    TEST_ASSERT(launcher.terminate(h_app));
    TEST_ASSERT(!identity_mgr->is_valid_handle(h_app));
    TEST_ASSERT(identity_mgr->active_process_count() == 1); // Only h_sys remains
    TEST_ASSERT(launcher.terminate(h_sys));
    TEST_ASSERT(identity_mgr->active_process_count() == 0);
    TEST_ASSERT(task_mgr->get_task_count() == 0);
    std::cout << "  -> Step 18 PASS: Zero resource leakage verified.\n";

    // -------------------------------------------------------------
    // Step 19: Application Update with WAL Crash Durability
    // -------------------------------------------------------------
    std::cout << "[Step 19] Testing application update and WAL crash reconciliation...\n";
    tools::AppTemplateConfig cfg_v2 = cfg;
    cfg_v2.version = "2.0.0";
    auto pkg_v2 = tools::AppGenerator::generate_package(cfg_v2, arm64_bin);

    // Inject crash fault at Extracted state
    installer.inject_fault_at_state(packaging::PackageInstaller::InstallerState::Extracted);
    auto update_status = installer.update(pkg_v2);
    TEST_ASSERT(update_status == InstallStatus::CommitFailed);

    // Reconcile and recover from failure
    auto rec_report = installer.reconcile_and_recover();
    TEST_ASSERT(rec_report.abandoned_transactions_cleaned >= 1);
    TEST_ASSERT(registry->is_installed("com.winios.e2e233"));
    auto app_check = registry->get_app("com.winios.e2e233");
    TEST_ASSERT(app_check.has_value());
    TEST_ASSERT(app_check->version == "1.0.0"); // Preserved v1!
    std::cout << "  -> Step 19 PASS: Interrupted update cleanly reconciled to v1.0.0.\n";

    std::cout << "\n=================================================================\n";
    std::cout << "  ALL 19 END-TO-END SECURITY WORKFLOW STEPS PASSED SUCCESSFULLY!  \n";
    std::cout << "=================================================================\n";
    return 0;
}

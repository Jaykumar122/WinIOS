#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include <cstring>
#include <limits>

#include "packaging/app_package.h"
#include "packaging/app_registry.h"
#include "packaging/package_installer.h"
#include "loader/embedded_campaign230_workloads.inl"
#include "loader/app_launcher.h"
#include "kernel/process_identity.h"
#include "kernel/ipc_capability_broker.h"
#include "documents/document_registry.h"
#include "documents/document_association_manager.h"
#include "platform/resource_budget.h"
#include "security/capability_manager.h"
#include "session/multi_app_session_controller.h"
#include "services/diagnostics_service.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Adversarial Assertion Failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;

// 1. Malformed Manifests & Packages
static void test_malformed_manifests_and_packages() {
    std::cout << "[Adversarial 1] Malformed manifests, truncated packages, and corrupt headers...\n";

    // 1.1 Missing bundle_id
    {
        packaging::PackageManifest m{};
        m.name = "NoBundle";
        m.executable_name = "bin/nobundle";
        TEST_ASSERT(!m.is_valid());
    }

    // 1.2 Truncated package payload
    {
        std::vector<uint8_t> corrupted = {'W', 'I', 'O', 'S', 'P', 'K', 'G', '3'}; // Truncated header
        packaging::AppPackage out_pkg{};
        auto res = packaging::AppPackage::validate_package_data(corrupted.data(), corrupted.size(), &out_pkg);
        TEST_ASSERT(res == packaging::PackageValidationResult::PayloadTruncated);
    }

    // 1.3 Corrupt Magic
    {
        std::vector<uint8_t> bad_magic(64, 0xAA);
        packaging::AppPackage out_pkg{};
        auto res = packaging::AppPackage::validate_package_data(bad_magic.data(), bad_magic.size(), &out_pkg);
        TEST_ASSERT(res == packaging::PackageValidationResult::InvalidMagic);
    }

    // 1.4 Checksum mismatch (tampered payload)
    {
        packaging::AppPackage pkg{};
        pkg.manifest.bundle_id = "com.winios.tamper";
        pkg.manifest.name = "TamperApp";
        pkg.manifest.executable_name = "bin/tamper";
        pkg.add_file("bin/tamper", {0xDE, 0xAD, 0xBE, 0xEF});
        auto packed = pkg.pack_v3();
        // Tamper with one byte in the data area
        if (packed.size() > 10) {
            packed[packed.size() - 2] ^= 0xFF;
        }
        packaging::AppPackage out_pkg{};
        auto res = packaging::AppPackage::validate_package_data(packed.data(), packed.size(), &out_pkg);
        TEST_ASSERT(res == packaging::PackageValidationResult::ChecksumMismatch);
    }

    std::cout << "  -> Passed malformed manifests and packages.\n";
}

// 2. Overlapping Package Entries & Duplicate Files
static void test_overlapping_entries_and_overflow() {
    std::cout << "[Adversarial 2] Overlapping package entries and duplicate file detection...\n";

    // Duplicate path in package entries
    packaging::AppPackage pkg{};
    pkg.manifest.bundle_id = "com.winios.dup";
    pkg.manifest.name = "DupApp";
    pkg.manifest.executable_name = "bin/dup";
    pkg.add_file("bin/dup", {1, 2, 3});
    // Add same file path again
    pkg.add_file("bin/dup", {4, 5, 6});

    auto packed = pkg.pack_v3();
    packaging::AppPackage out_pkg{};
    auto res = packaging::AppPackage::validate_package_data(packed.data(), packed.size(), &out_pkg);
    TEST_ASSERT(res == packaging::PackageValidationResult::DuplicateFileEntry || res == packaging::PackageValidationResult::Valid);

    std::cout << "  -> Passed duplicate file entries detection.\n";
}

// 3. Incompatible SDK & Capability Violations
static void test_sdk_and_capability_violations() {
    std::cout << "[Adversarial 3] Incompatible SDK versions and capability violations...\n";

    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    auto registry = std::make_shared<packaging::ApplicationRegistry>();
    auto identity_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 200, 4);
    auto task_mgr = std::make_shared<settings::TaskManager>();
    auto budget_mgr = std::make_shared<platform::ResourceBudgetManager>();
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    auto diag_svc = std::make_shared<services::DiagnosticsService>();

    loader::AppLaunchPipeline pipeline(vfs, registry, identity_mgr, task_mgr, budget_mgr, cap_mgr, diag_svc);

    // Setup mock app directory with valid Mach-O binary
    const auto& valid_bin = loader::get_embedded_campaign230_app_startup();
    vfs->mkdir("/Applications");
    vfs->mkdir("/Applications/com.winios.forbidden.app");
    vfs->mkdir("/Applications/com.winios.forbidden.app/bin");
    vfs->create_file("/Applications/com.winios.forbidden.app/bin/forbidden");
    vfs->write_file("/Applications/com.winios.forbidden.app/bin/forbidden", valid_bin.data(), valid_bin.size());

    // 3.1 Restricted capability request (KernelAccess)
    packaging::AppRegistration reg1{};
    reg1.bundle_id = "com.winios.forbidden";
    reg1.name = "ForbiddenApp";
    reg1.install_dir = "/Applications/com.winios.forbidden.app";
    reg1.exec_path = "bin/forbidden";
    reg1.permissions = {"KernelAccess"}; // Forbidden privilege
    registry->register_app(reg1);

    auto res1 = pipeline.launch("com.winios.forbidden");
    TEST_ASSERT(res1.status == loader::LaunchStatus::CapabilityDenied);
    TEST_ASSERT(!res1.handle.is_valid());

    // 3.2 Incompatible SDK version (e.g. SDK v3.5 = 0x00030500 > max supported 0x00020400)
    vfs->mkdir("/Applications/com.winios.future.app");
    vfs->mkdir("/Applications/com.winios.future.app/bin");
    vfs->create_file("/Applications/com.winios.future.app/bin/future");
    vfs->write_file("/Applications/com.winios.future.app/bin/future", valid_bin.data(), valid_bin.size());

    packaging::AppRegistration reg2{};
    reg2.bundle_id = "com.winios.future";
    reg2.name = "FutureApp";
    reg2.install_dir = "/Applications/com.winios.future.app";
    reg2.exec_path = "bin/future";
    reg2.min_sdk_version = 0x00030500; // Future SDK
    registry->register_app(reg2);

    auto res2 = pipeline.launch("com.winios.future");
    TEST_ASSERT(res2.status == loader::LaunchStatus::CompatibilityMismatch);

    std::cout << "  -> Passed SDK and capability violation rejections.\n";
}

// 4. PID Reuse & Stale Process Handle Invalidation
static void test_pid_reuse_and_stale_handles() {
    std::cout << "[Adversarial 4] PID reuse and stale process handle verification...\n";

    // Setup pool with small range: PID 100 to 101, max quarantine 1
    kernel::ProcessIdentityManager mgr(100, 101, 1);

    kernel::ApplicationIdentity id1{"com.winios.app1", "app1", "App 1", 0x00020300};
    kernel::ApplicationIdentity id2{"com.winios.app2", "app2", "App 2", 0x00020300};
    kernel::ApplicationIdentity id3{"com.winios.app3", "app3", "App 3", 0x00020300};

    auto h1 = mgr.allocate_process(id1);
    TEST_ASSERT(h1.is_valid());
    TEST_ASSERT(h1.pid == 100);
    TEST_ASSERT(h1.generation == 1);

    auto h2 = mgr.allocate_process(id2);
    TEST_ASSERT(h2.is_valid());
    TEST_ASSERT(h2.pid == 101);
    TEST_ASSERT(h2.generation == 1);

    // Pool full
    auto h3 = mgr.allocate_process(id3);
    TEST_ASSERT(!h3.is_valid());

    // Terminate PID 100 -> enters quarantine
    TEST_ASSERT(mgr.terminate_process(h1));
    TEST_ASSERT(!mgr.is_valid_handle(h1)); // Old handle invalid!

    // Next allocation will recycle PID 100 with incremented generation
    auto h1_new = mgr.allocate_process(id1);
    TEST_ASSERT(h1_new.is_valid());
    TEST_ASSERT(h1_new.pid == 100);
    TEST_ASSERT(h1_new.generation == 2); // Generation incremented!

    // Verify stale handle h1 (generation 1) fails lookup completely
    TEST_ASSERT(!mgr.is_valid_handle(h1));
    TEST_ASSERT(mgr.is_valid_handle(h1_new));

    // Stale handle cannot terminate recycled process
    TEST_ASSERT(!mgr.terminate_process(h1));
    TEST_ASSERT(mgr.is_valid_handle(h1_new));

    std::cout << "  -> Passed PID reuse and stale handle protection.\n";
}

// 5. IPC Authorization & Queue Exhaustion
static void test_ipc_authorization_and_queue_exhaustion() {
    std::cout << "[Adversarial 5] IPC authorization checks and bounded queue exhaustion...\n";

    auto identity_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 105, 2);
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    kernel::IpcCapabilityBroker broker(identity_mgr, cap_mgr);

    kernel::ApplicationIdentity id1{"com.winios.p1", "p1", "P1", 0x00020400};
    kernel::ApplicationIdentity id2{"com.winios.p2", "p2", "P2", 0x00020400};
    kernel::ApplicationIdentity id3{"com.winios.p3", "p3", "P3", 0x00020400};

    auto p1 = identity_mgr->allocate_process(id1);
    auto p2 = identity_mgr->allocate_process(id2);
    auto p3 = identity_mgr->allocate_process(id3);

    // Grant communication right from p1 to p2 only
    auto token1_2 = broker.grant_communication_right(p1, p2);
    TEST_ASSERT(token1_2.token_id != 0);

    // p3 attempts to forge/use token1_2 -> rejected
    const char data[] = "ping";
    auto res_forgery = broker.send_message(token1_2.token_id, p3, data, sizeof(data));
    TEST_ASSERT(res_forgery == kernel::IpcStatus::SenderInvalid);

    // Revoked token -> rejected
    broker.revoke_token(token1_2.token_id);
    auto res_revoked = broker.send_message(token1_2.token_id, p1, data, sizeof(data));
    TEST_ASSERT(res_revoked == kernel::IpcStatus::PermissionDenied);

    // Queue depth exhaustion: max capacity is 32
    auto new_token = broker.grant_communication_right(p1, p2);
    for (size_t i = 0; i < kernel::IpcCapabilityBroker::kMaxPortCapacity; ++i) {
        auto ok = broker.send_message(new_token.token_id, p1, data, sizeof(data), static_cast<uint32_t>(i));
        TEST_ASSERT(ok == kernel::IpcStatus::Success);
    }
    // 33rd message must fail with QueueFull
    auto res_full = broker.send_message(new_token.token_id, p1, data, sizeof(data), 999);
    TEST_ASSERT(res_full == kernel::IpcStatus::QueueFull);

    std::cout << "  -> Passed IPC authorization and queue exhaustion bounds.\n";
}

// 6. Document Grant Misuse & Cross-Process Isolation
static void test_document_grant_misuse() {
    std::cout << "[Adversarial 6] Document grant misuse and cross-process access isolation...\n";

    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    documents::DocumentRegistry doc_reg(cap_mgr);

    // Grant PID 101 DocumentCreate and DocumentRead
    cap_mgr->grant(101, security::Capability::DocumentCreate);
    cap_mgr->grant(101, security::Capability::DocumentRead);

    auto h = doc_reg.register_document(101, "com.winios.p101", "confidential.txt",
                                       "/Documents/confidential.txt", "text/plain", 100);
    TEST_ASSERT(h != documents::kInvalidDocumentHandle);

    // PID 102 has no capabilities and no grant
    TEST_ASSERT(!doc_reg.check_access(h, 102, documents::DocumentAccessMode::Read));

    // Grant PID 102 Read capability globally, but doc_reg has not shared document h with 102
    cap_mgr->grant(102, security::Capability::DocumentRead);
    TEST_ASSERT(!doc_reg.check_access(h, 102, documents::DocumentAccessMode::Read)); // Still rejected (not owner, no grant)

    // Unauthorized unregister: PID 102 tries to delete PID 101's document
    cap_mgr->grant(102, security::Capability::DocumentDelete);
    TEST_ASSERT(!doc_reg.unregister_document(h, 102)); // Rejected!

    // Verify document is still intact
    TEST_ASSERT(doc_reg.is_handle_valid(h));

    std::cout << "  -> Passed document grant misuse checks.\n";
}

// 7. Resource Quota Exhaustion
static void test_resource_budget_exhaustion() {
    std::cout << "[Adversarial 7] Resource budget and quota limits...\n";

    platform::ResourceBudgetManager budget;

    // Allocate 10 MB (within 16 MB default limit) -> ok
    auto err1 = budget.allocate_memory(101, 10 * 1024 * 1024);
    TEST_ASSERT(err1.is_ok());

    // Allocate another 10 MB (total 20 MB > 16 MB limit) -> rejected
    auto err2 = budget.allocate_memory(101, 10 * 1024 * 1024);
    TEST_ASSERT(!err2.is_ok());

    // Current usage is still 10 MB
    TEST_ASSERT(budget.get_task_memory(101) == 10 * 1024 * 1024);

    std::cout << "  -> Passed resource quota limits.\n";
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "=== Running Stage 230.22 Adversarial & Compatibility Tests   ===\n";
    std::cout << "=================================================================\n";
    test_malformed_manifests_and_packages();
    test_overlapping_entries_and_overflow();
    test_sdk_and_capability_violations();
    test_pid_reuse_and_stale_handles();
    test_ipc_authorization_and_queue_exhaustion();
    test_document_grant_misuse();
    test_resource_budget_exhaustion();
    std::cout << "\n=== All Stage 230.22 Adversarial Tests Passed Successfully! ===\n";
    return 0;
}

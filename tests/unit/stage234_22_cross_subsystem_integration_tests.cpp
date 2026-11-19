#include "crypto/ed25519.h"
#include "kernel/ipc_capability_broker.h"
#include "kernel/process_identity.h"
#include "loader/app_launcher.h"
#include "loader/embedded_campaign230_workloads.inl"
#include "memory/guest_memory_validator.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "packaging/app_package.h"
#include "packaging/app_registry.h"
#include "packaging/package_installer.h"
#include "platform/resource_budget.h"
#include "security/capability_manager.h"
#include "security/developer_keystore.h"
#include "security/publisher_trust_store.h"
#include "services/diagnostics_service.h"
#include "settings/task_manager.h"
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
using namespace winios::crypto;
using namespace winios::kernel;
using namespace winios::loader;
using namespace winios::memory;
using namespace winios::packaging;
using namespace winios::platform;
using namespace winios::security;
using namespace winios::services;
using namespace winios::settings;
using namespace winios::vfs;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at line " << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Cross-Subsystem Security Integration Test
// -----------------------------------------------------------------------------
static void test_cross_subsystem_security_chain() {
    std::cout << "[Stage 234.22] Executing End-to-End Cross-Subsystem Security Chain...\n";

    // Subsystem 1: VFS, Registry, and Core Infrastructure
    auto vfs = std::make_shared<VirtualFileSystem>();
    auto registry = std::make_shared<ApplicationRegistry>();
    auto id_mgr = std::make_shared<ProcessIdentityManager>(100, 200, 0);
    auto task_mgr = std::make_shared<TaskManager>();
    auto budget_mgr = std::make_shared<ResourceBudgetManager>();
    auto cap_mgr = std::make_shared<CapabilityManager>();
    auto diag_svc = std::make_shared<DiagnosticsService>();
    auto trust_store = std::make_shared<PublisherTrustStore>();

    // Subsystem 2: Cryptographic Identity Generation & Registration
    std::cout << "  [Step 1] Generating developer Ed25519 identity...\n";
    DeveloperIdentity dev_id = DeveloperKeyStore::generate_identity(
        "com.winios.secureapp", "Secure Software Org", "key-sec-01", PublisherRole::Platform
    );
    PublisherKey dev_pub = DeveloperKeyStore::export_public_key(dev_id);
    TEST_ASSERT(trust_store->register_key(dev_pub), "Register developer public key in trust store");

    // Subsystem 3: Package Assembly & Digital Signing
    std::cout << "  [Step 2] Assembling canonical application package...\n";
    AppPackage pkg;
    pkg.manifest.format_version = "3.0";
    pkg.manifest.bundle_id = "com.winios.secureapp";
    pkg.manifest.name = "SecureApp";
    pkg.manifest.display_name = "Secure Application";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.min_sdk_version_u32 = 0x00020400;
    pkg.manifest.executable_name = "bin/main";
    pkg.manifest.permissions = {"SettingsRead", "DocumentRead"};

    const auto& mach_o_bin = loader::get_embedded_campaign230_app_settings();
    TEST_ASSERT(!mach_o_bin.empty(), "Mach-O binary available");
    pkg.add_file("bin/main", mach_o_bin);
    pkg.add_file("Info.plist", {'<', 'p', 'l', 'i', 's', 't', '>', '<', '/', 'p', 'l', 'i', 's', 't', '>'});

    auto raw_packed = pkg.pack_v3();
    TEST_ASSERT(!raw_packed.empty(), "Package packed into WIOSPKG3");

    std::cout << "  [Step 3] Signing package with Ed25519 signature...\n";
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(raw_packed, dev_id), "Sign package with dev key");

    // Subsystem 4: Signature Verification Prior to Admission
    std::cout << "  [Step 4] Verifying package signature via trust store...\n";
    auto sig_res = trust_store->verify_package_signature(raw_packed.data(), raw_packed.size(), TrustPolicy::StrictProduction);
    TEST_ASSERT(sig_res.is_trusted, "Package signature verified as authentic");
    TEST_ASSERT(sig_res.key_id == "key-sec-01", "Key ID matches signing key");

    // Subsystem 5: Transactional WAL Package Installation
    std::cout << "  [Step 5] Installing package with WAL transaction...\n";
    PackageInstaller installer(vfs, registry);
    auto install_status = installer.install(pkg);
    TEST_ASSERT(install_status == InstallStatus::Success, "Transactional WAL package installation succeeded");
    TEST_ASSERT(registry->is_installed("com.winios.secureapp"), "App registered in ApplicationRegistry");

    // Subsystem 6: 14-Phase Application Launch Transaction
    std::cout << "  [Step 6] Launching application via AppLaunchPipeline...\n";
    AppLaunchPipeline launcher(vfs, registry, id_mgr, task_mgr, budget_mgr, cap_mgr, diag_svc);
    auto launch_res = launcher.launch("com.winios.secureapp");

    TEST_ASSERT(launch_res.status == LaunchStatus::Success, "Launch succeeded");
    TEST_ASSERT(launch_res.final_phase == LaunchTransactionPhase::Committed, "Launch committed");
    TEST_ASSERT(!launch_res.rolled_back, "Not rolled back");
    TEST_ASSERT(launch_res.handle.is_valid(), "Process handle is valid");
    const uint32_t pid = launch_res.handle.pid;
    TEST_ASSERT(pid >= 100, "PID allocated from managed pool");

    // Subsystem 7: Capability Binding Verification
    std::cout << "  [Step 7] Verifying security capability bindings...\n";
    TEST_ASSERT(cap_mgr->check(pid, Capability::SettingsRead), "SettingsRead capability granted");
    TEST_ASSERT(cap_mgr->check(pid, Capability::DocumentRead), "DocumentRead capability granted");
    TEST_ASSERT(!cap_mgr->check(pid, Capability::SettingsWrite), "SettingsWrite correctly denied (not in manifest)");
    TEST_ASSERT(!cap_mgr->check(pid, Capability::TaskManage), "TaskManage correctly denied");

    // Subsystem 8: IPC Capability Broker & Anti-Replay
    std::cout << "  [Step 8] Verifying IPC capability rights & anti-replay...\n";
    IpcCapabilityBroker broker(id_mgr, cap_mgr);
    ApplicationIdentity server_id{"com.winios.server", "1.0.0", "Server", 0x00020400};
    auto h_server = id_mgr->allocate_process(server_id);
    TEST_ASSERT(h_server.is_valid(), "Server process allocated");

    auto token = broker.grant_communication_right(launch_res.handle, h_server, 0x1, true /* strict sequence */);
    TEST_ASSERT(token.token_id != 0, "IPC token granted");

    // Sequence validation
    const char msg_data[] = "SECURE_IPC_PAYLOAD";
    auto v1 = broker.send_message(token.token_id, launch_res.handle, msg_data, sizeof(msg_data), 1, 1);
    TEST_ASSERT(v1 == IpcStatus::Success, "Sequence 1 valid");
    auto v_replay = broker.send_message(token.token_id, launch_res.handle, msg_data, sizeof(msg_data), 1, 1);
    TEST_ASSERT(v_replay == IpcStatus::ReplayDetected, "Replay sequence 1 rejected");
    auto v2 = broker.send_message(token.token_id, launch_res.handle, msg_data, sizeof(msg_data), 1, 2);
    TEST_ASSERT(v2 == IpcStatus::Success, "Sequence 2 valid");

    // Subsystem 9: Guest Memory Validation Boundary
    std::cout << "  [Step 9] Verifying guest memory validation boundary...\n";
    Memory mem(0x1000000ULL); // 16 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "integration_uas");

    const std::uint64_t rw_va = 0x100000000ULL;
    const std::uint64_t ro_va = 0x100004000ULL;
    const std::uint64_t rw_pa = ppm.allocate_page();
    const std::uint64_t ro_pa = ppm.allocate_page();

    TEST_ASSERT(uas.map_user_page(rw_va, rw_pa, PagePermissions::UserReadWrite), "Map RW page");
    TEST_ASSERT(uas.map_user_page(ro_va, ro_pa, PagePermissions::UserRead), "Map RO page");

    // Valid buffer validation
    auto v_ok = GuestMemoryValidator::validate_buffer(uas, rw_va, 512, false);
    TEST_ASSERT(v_ok == ValidationResult::Success, "Valid mapped memory range passes");

    // Hostile boundary checks: NULL pointer dereference
    auto v_null = GuestMemoryValidator::validate_buffer(uas, 0x0ULL, 64, false);
    TEST_ASSERT(v_null == ValidationResult::NullPointer, "NULL pointer dereference rejected");

    // Unmapped address check
    auto v_unmapped = GuestMemoryValidator::validate_buffer(uas, 0x200000000ULL, 64, false);
    TEST_ASSERT(v_unmapped == ValidationResult::UnmappedPage, "Unmapped address rejected");

    // Integer overflow check
    auto v_overflow = GuestMemoryValidator::validate_buffer(uas, 0xFFFFFFFFFFFFFF00ULL, 512, false);
    TEST_ASSERT(v_overflow == ValidationResult::IntegerOverflow, "Overflowing memory range rejected");

    // Write permission on read-only page
    auto v_ro_write = GuestMemoryValidator::validate_buffer(uas, ro_va, 64, true /* require writable */);
    TEST_ASSERT(v_ro_write == ValidationResult::PermissionDenied, "Write to read-only page rejected");

    // Subsystem 10: Graceful Teardown & Lifecycle Verification
    std::cout << "  [Step 10] Terminating process & unwinding state...\n";
    TEST_ASSERT(launcher.terminate(launch_res.handle), "Process terminated cleanly");
    id_mgr->terminate_process(h_server);
    TEST_ASSERT(id_mgr->active_process_count() == 0, "PID slot freed");
    TEST_ASSERT(task_mgr->get_task_count() == 0, "Task unregistered");
    TEST_ASSERT(!cap_mgr->check(pid, Capability::SettingsRead), "Capabilities invalidated after process exit");

    std::cout << "  -> Cross-subsystem security chain integration verified across all 10 subsystems!\n";
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
    std::cout << "  Stage 234.22: Cross-Subsystem Security Integration Suite      \n";
    std::cout << "=================================================================\n";

    test_cross_subsystem_security_chain();

    std::cout << "\n=================================================================\n";
    std::cout << "  ALL CROSS-SUBSYSTEM SECURITY INTEGRATION TESTS PASSED!         \n";
    std::cout << "=================================================================\n";
    return 0;
}
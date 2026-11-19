#include "crypto/ed25519.h"
#include "kernel/ipc_capability_broker.h"
#include "kernel/process_identity.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"
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
#include "documents/document_registry.h"
#include "documents/document_stream_manager.h"
#include "vfs/vfs.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace winios;
using namespace winios::crypto;
using namespace winios::documents;
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

static void test_complete_security_integration_pipeline() {
    std::cout << "[Stage 235.21] End-to-End Cross-Subsystem Security Integration Pipeline...\n";

    // -------------------------------------------------------------------------
    // Step 1: Core Host Infrastructure Setup
    // -------------------------------------------------------------------------
    std::cout << "  [Step 1] Initializing core platform infrastructure...\n";
    auto vfs = std::make_shared<VirtualFileSystem>();
    auto registry = std::make_shared<ApplicationRegistry>();
    auto id_mgr = std::make_shared<ProcessIdentityManager>(100, 200, 0);
    auto task_mgr = std::make_shared<TaskManager>();
    auto budget_mgr = std::make_shared<ResourceBudgetManager>();
    auto cap_mgr = std::make_shared<CapabilityManager>();
    auto diag_svc = std::make_shared<DiagnosticsService>();
    auto trust_store = std::make_shared<PublisherTrustStore>();
    auto doc_registry = std::make_shared<DocumentRegistry>();
    auto doc_stream_mgr = std::make_shared<DocumentStreamManager>(doc_registry, vfs);

    // -------------------------------------------------------------------------
    // Step 2: KeyStore Generation & Trust Registration
    // -------------------------------------------------------------------------
    std::cout << "  [Step 2] KeyStore: Generating and registering developer identity...\n";
    DeveloperIdentity dev_id = DeveloperKeyStore::generate_identity(
        "com.winios.integrated_app", "WinIOS Security Lab", "key-int-01", PublisherRole::Platform
    );
    PublisherKey dev_pub = DeveloperKeyStore::export_public_key(dev_id);
    TEST_ASSERT(trust_store->register_key(dev_pub), "Developer public key registered in trust store");

    // -------------------------------------------------------------------------
    // Step 3: Package Assembly & Ed25519 Signature
    // -------------------------------------------------------------------------
    std::cout << "  [Step 3] Package: Assembling and signing WIOSPKG3 container...\n";
    AppPackage pkg;
    pkg.manifest.format_version = "3.0";
    pkg.manifest.bundle_id = "com.winios.integrated_app";
    pkg.manifest.name = "IntegratedApp";
    pkg.manifest.display_name = "Integrated App";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.min_sdk_version_u32 = 0x00020400;
    pkg.manifest.executable_name = "bin/main";
    pkg.manifest.permissions = {"SettingsRead", "DocumentRead", "DocumentWrite"};

    const auto& mach_o_bin = loader::get_embedded_campaign230_app_settings();
    TEST_ASSERT(!mach_o_bin.empty(), "Mach-O binary available");
    pkg.add_file("bin/main", mach_o_bin);
    pkg.add_file("Info.plist", {'<', 'p', 'l', 'i', 's', 't', '>', '<', '/', 'p', 'l', 'i', 's', 't', '>'});

    auto raw_package = pkg.pack_v3();
    TEST_ASSERT(!raw_package.empty(), "WIOSPKG3 created");
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(raw_package, dev_id), "Signed package with Ed25519");

    auto sig_res = trust_store->verify_package_signature(raw_package.data(), raw_package.size(), TrustPolicy::StrictProduction);
    TEST_ASSERT(sig_res.is_trusted, "Package signature verified as authentic");
    TEST_ASSERT(sig_res.key_id == "key-int-01", "Key ID matches signing key");

    // -------------------------------------------------------------------------
    // Step 4: WAL-Backed Package Installation
    // -------------------------------------------------------------------------
    std::cout << "  [Step 4] Installation: Executing 11-state WAL installer...\n";
    PackageInstaller installer(vfs, registry);
    auto inst_res = installer.install(pkg);
    TEST_ASSERT(inst_res == InstallStatus::Success, "Package installed via WAL");
    TEST_ASSERT(registry->is_installed("com.winios.integrated_app"), "App listed in registry");

    // -------------------------------------------------------------------------
    // Step 5: 14-Phase Application Launch Transaction
    // -------------------------------------------------------------------------
    std::cout << "  [Step 5] Launch: Executing 14-phase launch transaction...\n";
    AppLaunchPipeline launcher(vfs, registry, id_mgr, task_mgr, budget_mgr, cap_mgr, diag_svc);
    auto launch_res = launcher.launch("com.winios.integrated_app");

    TEST_ASSERT(launch_res.status == LaunchStatus::Success, "App launched");
    TEST_ASSERT(!launch_res.rolled_back, "Transaction committed");
    TEST_ASSERT(launch_res.final_phase == LaunchTransactionPhase::Committed, "Phase is Committed");
    TEST_ASSERT(launch_res.handle.is_valid(), "Process handle is valid");
    const uint32_t app_pid = launch_res.handle.pid;

    // Capabilities bound
    TEST_ASSERT(cap_mgr->check(app_pid, Capability::SettingsRead), "Has SettingsRead");
    TEST_ASSERT(cap_mgr->check(app_pid, Capability::DocumentRead), "Has DocumentRead");
    TEST_ASSERT(cap_mgr->check(app_pid, Capability::DocumentWrite), "Has DocumentWrite");
    TEST_ASSERT(!cap_mgr->check(app_pid, Capability::TaskManage), "Lacks TaskManage");

    // -------------------------------------------------------------------------
    // Step 6: Syscall Surface & DarwinStrict ABI Execution
    // -------------------------------------------------------------------------
    std::cout << "  [Step 6] Syscall: Executing user syscalls with DarwinStrict ABI...\n";
    CPU cpu;
    Memory mem(0x1000000ULL);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);
    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();
    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);
    dispatcher.set_capability_manager(cap_mgr.get());

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* proc = proc_mgr.create_process("integrated_proc", app_pid);
    const uint64_t root_l2 = ppm.allocate_page();
    auto uas = std::make_unique<UserAddressSpace>(mem, ppm, root_l2, "int_uas");
    proc->set_address_space(uas.get());

    // Execute getpid (syscall 20)
    cpu.write_register(16, kSysGetPid);
    dispatcher.handle_syscall(cpu, *proc, *uas, sched);
    std::uint64_t ret_pid = cpu.read_register(0);
    TEST_ASSERT(static_cast<std::int32_t>(ret_pid) == proc->pid(), "getpid syscall returned correct PID");

    // Execute getppid (syscall 39)
    cpu.write_register(16, kSysGetPpid);
    dispatcher.handle_syscall(cpu, *proc, *uas, sched);
    std::uint64_t ret_ppid = cpu.read_register(0);
    TEST_ASSERT(static_cast<std::uint32_t>(ret_ppid) == app_pid, "getppid syscall returned parent app PID");

    // -------------------------------------------------------------------------
    // Step 7: IPC Capability Broker & Anti-Replay
    // -------------------------------------------------------------------------
    std::cout << "  [Step 7] IPC: Capability token generation & message exchange...\n";
    IpcCapabilityBroker broker(id_mgr, cap_mgr);
    ApplicationIdentity daemon_id{"com.winios.daemon", "1.0.0", "SystemDaemon", 0x00020400};
    auto h_daemon = id_mgr->allocate_process(daemon_id);
    TEST_ASSERT(h_daemon.is_valid(), "Daemon process allocated");

    auto ipc_token = broker.grant_communication_right(launch_res.handle, h_daemon, 0x1, true);
    TEST_ASSERT(ipc_token.token_id != 0, "IPC right granted");

    const char ipc_msg[] = "INTEGRATED_IPC_PAYLOAD";
    auto s1 = broker.send_message(ipc_token.token_id, launch_res.handle, ipc_msg, sizeof(ipc_msg), 101, 1);
    TEST_ASSERT(s1 == IpcStatus::Success, "IPC message 1 sent successfully");

    // Anti-replay check
    auto s_replay = broker.send_message(ipc_token.token_id, launch_res.handle, ipc_msg, sizeof(ipc_msg), 101, 1);
    TEST_ASSERT(s_replay == IpcStatus::ReplayDetected, "Replay sequence rejected");

    // -------------------------------------------------------------------------
    // Step 8: Document Stream Manager & Dynamic Revocation
    // -------------------------------------------------------------------------
    std::cout << "  [Step 8] Document: Stream lifecycle and dynamic grant revocation...\n";
    vfs->mkdir("/Documents");
    vfs->create_file("/Documents/test_doc.txt");
    const char doc_data[] = "SECURE_DOCUMENT_INITIAL_CONTENT";
    vfs->write_file("/Documents/test_doc.txt", doc_data, sizeof(doc_data));

    auto doc_h = doc_registry->register_document(h_daemon.pid, "com.winios.daemon", "test_doc.txt",
                                                 "/Documents/test_doc.txt", "text/plain", 1024, false);
    TEST_ASSERT(doc_h != kInvalidDocumentHandle, "Document registered");

    auto grant = doc_registry->create_grant(doc_h, h_daemon.pid, app_pid, "com.winios.integrated_app",
                                            DocumentAccessMode::ReadWrite, 0);
    TEST_ASSERT(grant.grant_token != 0, "Document grant created");

    auto stream_h = doc_stream_mgr->open_stream(doc_h, app_pid, DocumentAccessMode::ReadWrite);
    TEST_ASSERT(stream_h != kInvalidStreamHandle, "Stream opened under grant");

    // Read content
    char read_buf[64] = {0};
    std::size_t bytes_read = 0;
    bool r_ok = doc_stream_mgr->read_stream(stream_h, app_pid, read_buf, sizeof(read_buf), bytes_read);
    TEST_ASSERT(r_ok && bytes_read == sizeof(doc_data), "Read successful under active grant");
    TEST_ASSERT(std::strcmp(read_buf, doc_data) == 0, "Read content matches");

    // Revoke grant and verify subsequent I/O is blocked
    bool rev_ok = doc_registry->revoke_grant(grant.grant_token, h_daemon.pid);
    TEST_ASSERT(rev_ok, "Grant revoked");

    std::size_t written = 0;
    bool w_blocked = doc_stream_mgr->write_stream(stream_h, app_pid, "MUTATION", 8, written);
    TEST_ASSERT(!w_blocked, "Write blocked dynamically on revoked grant");

    // -------------------------------------------------------------------------
    // Step 9: Teardown & Invariant Verification
    // -------------------------------------------------------------------------
    std::cout << "  [Step 9] Teardown: Clean termination and resource reclamation...\n";
    doc_stream_mgr->close_stream(stream_h, app_pid);
    TEST_ASSERT(launcher.terminate(launch_res.handle), "Application terminated");
    id_mgr->terminate_process(h_daemon);

    TEST_ASSERT(id_mgr->active_process_count() == 0, "All processes freed");
    TEST_ASSERT(task_mgr->get_task_count() == 0, "All tasks removed");
    TEST_ASSERT(!cap_mgr->check(app_pid, Capability::SettingsRead), "Capabilities revoked");
    std::cout << "  Passed!\n";
}

int main() {
    std::cout << "=== Stage 235.21 End-to-End Security Integration Suite ===" << std::endl;
    test_complete_security_integration_pipeline();
    std::cout << "=== All Stage 235.21 tests PASSED successfully ===" << std::endl;
    return 0;
}

#include "session/multi_app_session_controller.h"
#include "platform/resource_budget.h"
#include "kernel/process_identity.h"
#include "kernel/ipc_capability_broker.h"
#include "documents/document_registry.h"
#include "loader/app_launcher.h"
#include "loader/embedded_campaign230_workloads.inl"
#include "packaging/app_registry.h"
#include "settings/task_manager.h"
#include "security/capability_manager.h"
#include "services/diagnostics_service.h"
#include "vfs/vfs.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <cstdlib>
#if defined(_DEBUG)
#include <crtdbg.h>
#endif
#endif

using namespace winios;
using namespace winios::session;
using namespace winios::platform;

static void register_dummy_app(vfs::VirtualFileSystem& vfs, packaging::ApplicationRegistry& reg,
                               const std::string& bundle_id, const std::string& name,
                               const std::vector<uint8_t>& binary) {
    const std::string app_dir = "/Applications/" + bundle_id + ".app";
    vfs.mkdir("/Applications");
    vfs.mkdir(app_dir);
    vfs.mkdir(app_dir + "/bin");

    const std::string exec = app_dir + "/bin/" + name;
    vfs.create_file(exec);
    vfs.write_file(exec, binary.data(), binary.size());

    packaging::AppRegistration r{};
    r.bundle_id = bundle_id;
    r.name = name;
    r.display_name = name + " App";
    r.version = "1.0.0";
    r.exec_path = "bin/" + name;
    r.install_dir = app_dir;
    r.min_sdk_version = 0x00020400;
    r.permissions = {"SettingsRead"};
    reg.register_app(r);
}

static void test_session_lifecycle_and_lru_eviction() {
    std::cout << "[Test 1] Multi-app session lifecycle, suspension, and LRU eviction...\n";

    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    auto registry = std::make_shared<packaging::ApplicationRegistry>();
    auto identity_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 200, 4);
    auto task_mgr = std::make_shared<settings::TaskManager>();
    auto budget_mgr = std::make_shared<ResourceBudgetManager>();
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    auto diag_svc = std::make_shared<services::DiagnosticsService>();

    auto launcher = std::make_shared<loader::AppLaunchPipeline>(
        vfs, registry, identity_mgr, task_mgr, budget_mgr, cap_mgr, diag_svc);

    // Register 3 test applications
    auto bin_startup = loader::get_embedded_campaign230_app_startup();
    auto bin_settings = loader::get_embedded_campaign230_app_settings();
    auto bin_docs = loader::get_embedded_campaign230_app_docs();

    register_dummy_app(*vfs, *registry, "com.winios.app1", "App1", bin_startup);
    register_dummy_app(*vfs, *registry, "com.winios.app2", "App2", bin_settings);
    register_dummy_app(*vfs, *registry, "com.winios.app3", "App3", bin_docs);

    // Controller configured with max 2 active apps
    MultiAppSessionController ctrl(launcher, identity_mgr, registry, task_mgr, diag_svc, 2);

    // 1. Launch App 1 -> Foreground
    auto res1 = ctrl.launch_app("com.winios.app1");
    assert(res1.status == loader::LaunchStatus::Success);
    uint32_t pid1 = res1.handle.pid;
    assert(ctrl.foreground_pid() == pid1);
    assert(ctrl.get_session_entry(pid1)->state == AppSessionState::Foreground);

    // 2. Launch App 2 -> App 2 becomes Foreground, App 1 moves to Background
    auto res2 = ctrl.launch_app("com.winios.app2");
    assert(res2.status == loader::LaunchStatus::Success);
    uint32_t pid2 = res2.handle.pid;
    (void)pid2;
    assert(ctrl.foreground_pid() == pid2);
    assert(ctrl.get_session_entry(pid2)->state == AppSessionState::Foreground);
    assert(ctrl.get_session_entry(pid1)->state == AppSessionState::Background);

    // 3. Launch App 3 -> Active count was 2 (pid1 background, pid2 foreground).
    // Capacity is 2, so oldest background app (App 1) is evicted to Suspended!
    auto res3 = ctrl.launch_app("com.winios.app3");
    assert(res3.status == loader::LaunchStatus::Success);
    uint32_t pid3 = res3.handle.pid;
    assert(ctrl.foreground_pid() == pid3);
    assert(ctrl.get_session_entry(pid3)->state == AppSessionState::Foreground);
    assert(ctrl.get_session_entry(pid2)->state == AppSessionState::Background);
    assert(ctrl.get_session_entry(pid1)->state == AppSessionState::Suspended);

    // Verify App 1 process handle is still valid in identity_mgr despite suspension
    assert(identity_mgr->is_valid_handle(res1.handle));

    // 4. Switch between active apps (App 2 <-> App 3)
    assert(ctrl.switch_to_app(pid2));
    assert(ctrl.foreground_pid() == pid2);
    assert(ctrl.get_session_entry(pid2)->state == AppSessionState::Foreground);
    assert(ctrl.get_session_entry(pid3)->state == AppSessionState::Background);

    // 5. Resume Suspended App 1 -> App 3 (oldest background) gets suspended to make room
    assert(ctrl.resume_app(pid1));
    assert(ctrl.foreground_pid() == pid1);
    assert(ctrl.get_session_entry(pid1)->state == AppSessionState::Foreground);
    assert(ctrl.get_session_entry(pid2)->state == AppSessionState::Background);
    assert(ctrl.get_session_entry(pid3)->state == AppSessionState::Suspended);

    // 6. Explicit suspend with state snapshot
    std::vector<uint8_t> snap = {'S', 'N', 'A', 'P', '1'};
    assert(ctrl.suspend_app(pid1, snap));
    assert(ctrl.get_session_entry(pid1)->state == AppSessionState::Suspended);
    assert(ctrl.get_session_entry(pid1)->saved_state_snapshot == snap);
    // Automatic foreground promotion: App 2 was MRU background, becomes Foreground!
    assert(ctrl.foreground_pid() == pid2);
    assert(ctrl.get_session_entry(pid2)->state == AppSessionState::Foreground);

    // 7. Terminate foreground app (App 2) -> App 3 was suspended, so foreground becomes 0
    // until an app is resumed
    assert(ctrl.terminate_app(pid2));
    assert(!identity_mgr->is_valid_handle(res2.handle));

    // Cleanup remaining
    ctrl.terminate_app(pid1);
    ctrl.terminate_app(pid3);
    assert(ctrl.list_session_apps().empty());

    std::cout << "  -> Passed.\n";
}

static void test_resource_budget_isolation() {
    std::cout << "[Test 2] ResourceBudgetManager per-process bounds and isolation...\n";

    ResourceBudgetManager mgr;

    uint32_t pid_a = 101;
    uint32_t pid_b = 102;

    // 1. Allocate memory for PID A up to limit (16 MB)
    const uint64_t chunk = 4 * 1024 * 1024; // 4 MB
    assert(mgr.allocate_memory(pid_a, chunk).is_ok());
    assert(mgr.allocate_memory(pid_a, chunk).is_ok());
    assert(mgr.allocate_memory(pid_a, chunk).is_ok());
    assert(mgr.allocate_memory(pid_a, chunk).is_ok());
    assert(mgr.get_task_memory(pid_a) == 16 * 1024 * 1024);

    // 2. Next allocation for PID A exceeds budget
    auto err_a = mgr.allocate_memory(pid_a, chunk);
    assert(!err_a.is_ok());
    assert(err_a.category() == PlatformErrorCategory::ResourceExhausted);

    // 3. Verify PID B is unaffected and can allocate freely (isolation)
    assert(mgr.allocate_memory(pid_b, chunk).is_ok());
    assert(mgr.allocate_memory(pid_b, chunk).is_ok());
    assert(mgr.get_task_memory(pid_b) == 8 * 1024 * 1024);
    assert(mgr.get_task_memory(pid_a) == 16 * 1024 * 1024); // A unchanged

    // 4. Window budget isolation (max 8 per task)
    for (int i = 0; i < 8; ++i) {
        assert(mgr.allocate_window(pid_a).is_ok());
    }
    assert(mgr.get_task_windows(pid_a) == 8);
    auto err_w = mgr.allocate_window(pid_a);
    assert(!err_w.is_ok());
    assert(err_w.category() == PlatformErrorCategory::ResourceExhausted);

    // PID B can allocate window
    assert(mgr.allocate_window(pid_b).is_ok());
    assert(mgr.get_task_windows(pid_b) == 1);

    // 5. Document stream budget isolation (max 16)
    for (int i = 0; i < 16; ++i) {
        assert(mgr.open_document_stream(pid_a).is_ok());
    }
    auto err_s = mgr.open_document_stream(pid_a);
    assert(!err_s.is_ok());
    assert(err_s.category() == PlatformErrorCategory::ResourceExhausted);

    assert(mgr.open_document_stream(pid_b).is_ok());

    // 6. Cleanup PID A resources
    mgr.release_all_task_resources(pid_a);
    assert(mgr.get_task_memory(pid_a) == 0);
    assert(mgr.get_task_windows(pid_a) == 0);

    // PID B resources completely intact
    assert(mgr.get_task_memory(pid_b) == 8 * 1024 * 1024);
    assert(mgr.get_task_windows(pid_b) == 1);

    mgr.release_all_task_resources(pid_b);
    assert(mgr.get_task_memory(pid_b) == 0);

    std::cout << "  -> Passed.\n";
}

static void test_termination_cleanup_ipc_and_documents() {
    std::cout << "[Test 3] Process termination unbinding of IPC capabilities and document grants...\n";

    auto identity_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 200, 4);
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    auto ipc_broker = std::make_shared<kernel::IpcCapabilityBroker>(identity_mgr, cap_mgr);
    auto doc_reg = std::make_shared<documents::DocumentRegistry>(cap_mgr, identity_mgr);

    // Allocate two process handles
    kernel::ApplicationIdentity app1{"com.winios.sender", "1.0.0", "Sender", 0x00020400};
    kernel::ApplicationIdentity app2{"com.winios.receiver", "1.0.0", "Receiver", 0x00020400};

    kernel::ProcessHandle h_sender = identity_mgr->allocate_process(app1);
    kernel::ProcessHandle h_receiver = identity_mgr->allocate_process(app2);

    // Grant capabilities
    cap_mgr->grant(h_sender.pid, security::Capability::DocumentCreate);
    cap_mgr->grant(h_sender.pid, security::Capability::DocumentShare);
    cap_mgr->grant(h_receiver.pid, security::Capability::DocumentRead);

    // 1. Create IPC capability token
    auto token = ipc_broker->grant_communication_right(h_sender, h_receiver, 0x1);
    assert(token.token_id != 0);
    assert(!token.token_hex.empty());

    // Send a message
    std::vector<uint8_t> payload = {'h', 'e', 'l', 'l', 'o'};
    auto send_st = ipc_broker->send_message_by_hex(
        token.token_hex, h_sender, payload.data(), payload.size(), 1, 1);
    assert(send_st == kernel::IpcStatus::Success);
    (void)send_st;

    // 2. Register document and grant access
    auto doc_h = doc_reg->register_document(
        h_sender.pid, "com.winios.sender", "TestDoc", "/Documents/test.txt", "text/plain", 100, false);
    assert(doc_h != documents::kInvalidDocumentHandle);

    auto doc_grant = doc_reg->create_grant(
        doc_h, h_sender, h_receiver, "com.winios.receiver", documents::DocumentAccessMode::Read);
    assert(!doc_grant.grant_token_hex.empty());
    assert(doc_reg->check_access(doc_h, h_receiver, documents::DocumentAccessMode::Read));

    // 3. Terminate sender process
    assert(identity_mgr->terminate_process(h_sender));

    // Sender handle is no longer valid
    assert(!identity_mgr->is_valid_handle(h_sender));

    // Subsequent IPC send using terminated sender handle fails
    auto send_st2 = ipc_broker->send_message_by_hex(
        token.token_hex, h_sender, payload.data(), payload.size(), 2, 2);
    assert(send_st2 == kernel::IpcStatus::SenderInvalid);
    (void)send_st2;

    // 4. Terminate receiver process
    assert(identity_mgr->terminate_process(h_receiver));
    assert(!identity_mgr->is_valid_handle(h_receiver));

    // Document check access with terminated receiver handle fails
    assert(!doc_reg->check_access(doc_h, h_receiver, documents::DocumentAccessMode::Read));

    std::cout << "  -> Passed.\n";
}

int main() {
#if defined(_WIN32)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#if defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
#endif
    std::cout << "=== Stage 231.20: Multi-App Scheduling, Suspension, and Resource Isolation Tests ===\n";
    test_session_lifecycle_and_lru_eviction();
    test_resource_budget_isolation();
    test_termination_cleanup_ipc_and_documents();
    std::cout << "=== All Stage 231.20 Tests PASSED ===\n";
    return 0;
}

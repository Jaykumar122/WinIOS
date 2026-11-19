#include "tools/app_generator.h"
#include "loader/app_launcher.h"
#include "loader/app_inspector.h"
#include "loader/embedded_campaign230_workloads.inl"
#include "packaging/app_package.h"
#include "packaging/app_registry.h"
#include "packaging/package_installer.h"
#include "security/publisher_trust_store.h"
#include "security/capability_manager.h"
#include "kernel/process_identity.h"
#include "kernel/ipc_capability_broker.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/thread.h"
#include "kernel/scheduler.h"
#include "memory/user_address_space.h"
#include "documents/document_registry.h"
#include "settings/task_manager.h"
#include "platform/resource_budget.h"
#include "platform/app_performance_profiler.h"
#include "services/diagnostics_service.h"
#include "vfs/vfs.h"
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "winios_sdk/winios_sdk.h"

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
using namespace winios::kernel;
using namespace winios::memory;

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

    std::cout << "=== Stage 231.21: Full Developer Ecosystem End-to-End Workflow ===\n";

    // -------------------------------------------------------------
    // Step 1: Generate a new guest application template
    // -------------------------------------------------------------
    std::cout << "[Step 1] Generating guest application C source and manifest...\n";
    tools::AppTemplateConfig cfg{};
    cfg.bundle_id = "com.winios.e2eapp";
    cfg.app_name = "E2EApp";
    cfg.display_name = "End-to-End Test Application";
    cfg.version = "1.0.0";
    cfg.permissions = {"SettingsRead", "DiagnosticsWrite", "DocumentRead"};
    cfg.supported_doc_types = {"text/plain"};
    cfg.min_sdk_version = 0x00020400; // v2.4
    cfg.target_abi = "2.4";

    std::string c_src = tools::AppGenerator::generate_c_source(cfg);
    assert(c_src.find("E2EApp") != std::string::npos);
    assert(c_src.find("0x20400") != std::string::npos || c_src.find("0x00020400") != std::string::npos);

    std::string manifest_str = tools::AppGenerator::generate_manifest_ini(cfg);
    assert(manifest_str.find("bundle_id=com.winios.e2eapp") != std::string::npos);
    std::cout << "  -> Step 1 SUCCESS: Generated C source (" << c_src.size() << " bytes) & manifest.\n";

    // -------------------------------------------------------------
    // Step 2: Retrieve compiled genuine ARM64 Mach-O binary
    // -------------------------------------------------------------
    std::cout << "[Step 2] Retrieving genuine compiled ARM64 Mach-O guest binary...\n";
    auto arm64_bin = loader::get_embedded_campaign230_app_startup();
    assert(!arm64_bin.empty());
    std::cout << "  -> Step 2 SUCCESS: Binary retrieved (" << arm64_bin.size() << " bytes).\n";

    // -------------------------------------------------------------
    // Step 3: Inspect its Mach-O structure
    // -------------------------------------------------------------
    std::cout << "[Step 3] Inspecting Mach-O executable format and 16 KB alignment...\n";
    auto report = loader::AppInspector::inspect_binary(arm64_bin.data(), arm64_bin.size());
    assert(report.is_valid);
    assert(report.magic == loader::AppInspector::kMachO64Magic);
    assert(report.cputype == loader::AppInspector::kCpuTypeArm64);
    assert(report.page_aligned_16k);
    assert(report.wx_clean);
    std::cout << "  -> Step 3 SUCCESS: Valid ARM64 Mach-O, 16 KB page-aligned, W^X clean.\n";

    // -------------------------------------------------------------
    // Step 4: Package it
    // -------------------------------------------------------------
    std::cout << "[Step 4] Packaging application into WIOSPKG v3 container...\n";
    auto pkg = tools::AppGenerator::generate_package(cfg, arm64_bin);
    auto raw_package = pkg.pack_v3();
    assert(raw_package.size() > 64);
    std::cout << "  -> Step 4 SUCCESS: Packaged container created (" << raw_package.size() << " bytes).\n";

    // -------------------------------------------------------------
    // Step 5: Sign it using a test publisher key
    // -------------------------------------------------------------
    std::cout << "[Step 5] Signing package with Platform key via HMAC-SHA256...\n";
    security::PublisherTrustStore trust_store;
    security::PublisherKey plat_key{};
    plat_key.key_id = "platform_master_key";
    plat_key.publisher_id = "com.winios.system";
    plat_key.role = security::PublisherRole::Platform;
    plat_key.key_bytes = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
                          0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
                          0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                          0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
    plat_key.valid_from = 0;
    plat_key.valid_until = 0xFFFFFFFF;
    plat_key.is_revoked = false;
    assert(trust_store.register_key(plat_key));

    auto signed_package = raw_package;
    assert(security::PublisherTrustStore::sign_package(signed_package, plat_key));
    assert(signed_package.size() > raw_package.size());
    std::cout << "  -> Step 5 SUCCESS: Signed package with WIOSAUTH trailer (" << signed_package.size() << " bytes).\n";

    // -------------------------------------------------------------
    // Step 6: Verify its signature and trust policy
    // -------------------------------------------------------------
    std::cout << "[Step 6] Verifying signature against PublisherTrustStore...\n";
    auto sig_res = trust_store.verify_package_signature(
        signed_package.data(), signed_package.size(), security::TrustPolicy::Development);
    assert(sig_res.status == security::SignatureStatus::PlatformSigned);
    assert(sig_res.publisher_id == "com.winios.system");
    std::cout << "  -> Step 6 SUCCESS: Verified PlatformSigned signature status.\n";

    // -------------------------------------------------------------
    // Step 7: Install it transactionally
    // -------------------------------------------------------------
    std::cout << "[Step 7] Installing package transactionally through PackageInstaller...\n";
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->mkdir("/Applications");
    vfs->mkdir("/Documents");
    vfs->mkdir("/tmp");

    auto registry = std::make_shared<packaging::ApplicationRegistry>();
    auto installer = std::make_shared<packaging::PackageInstaller>(vfs, registry);

    packaging::AppPackage unpacked{};
    assert(packaging::AppPackage::unpack(signed_package.data(), signed_package.size(), unpacked));
    assert(installer->install(unpacked) == packaging::InstallStatus::Success);
    assert(registry->is_installed("com.winios.e2eapp"));
    std::cout << "  -> Step 7 SUCCESS: Transactional installation committed to VFS and registry.\n";

    // -------------------------------------------------------------
    // Step 8: Query application metadata using Syscall 592
    // -------------------------------------------------------------
    std::cout << "[Step 8] Exercising Syscall 592 (kSysWiniosAppInfo)...\n";
    CPU cpu;
    Memory mem{0x1000000}; // 16 MB guest memory
    PhysicalPageManager ppm{mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL};
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched{thread_mgr, mem};
    sched.initialize();

    VirtualConsole console;
    UserSyscallDispatcher dispatcher{console};
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);
    dispatcher.set_app_registry(registry.get());

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    BsdProcess* proc = proc_mgr.create_process("e2e_proc", 1);
    assert(proc != nullptr);

    const uint64_t root_l2_pa = ppm.allocate_page();
    auto uas = std::make_unique<UserAddressSpace>(mem, ppm, root_l2_pa, "e2e_uas");
    proc->set_address_space(uas.get());

    const uint64_t data_pa = ppm.allocate_page();
    const uint64_t page_va = 0x30000ULL;
    uas->map_user_page(page_va, data_pa, PagePermissions::UserReadWrite);

    // Setup Syscall 592: arg0 = VA of bundle_id string, arg1 = VA of output winios_app_info_t
    const std::string query_bid = "com.winios.e2eapp";
    uas->copyin(page_va, query_bid.c_str(), query_bid.size() + 1);

    const uint64_t info_va = page_va + 0x100;

    cpu.set_program_counter(0x5000);
    cpu.set_nzcv(0);
    cpu.write_register(16, kSysWiniosAppInfo);
    cpu.write_register(0, page_va);
    cpu.write_register(1, info_va);

    dispatcher.handle_syscall(cpu, *proc, *uas, sched);
    // Success: Carry is 0, X0 == 0
    assert((cpu.get_nzcv() & (1ULL << 29)) == 0); // PSTATE.C == 0
    assert(cpu.read_register(0) == 0);

    winios_app_info_t fetched_info{};
    uas->copyout(reinterpret_cast<const void*>(info_va), &fetched_info, sizeof(fetched_info));
    assert(std::string(fetched_info.bundle_id) == "com.winios.e2eapp");
    assert(std::string(fetched_info.display_name) == "End-to-End Test Application");
    assert(std::string(fetched_info.version) == "1.0.0");
    assert(fetched_info.min_sdk_version == 0x00020400);
    std::cout << "  -> Step 8 SUCCESS: Syscall 592 verified bundle metadata successfully.\n";

    // -------------------------------------------------------------
    // Step 9: Enumerate installed applications using Syscall 593
    // -------------------------------------------------------------
    std::cout << "[Step 9] Exercising Syscall 593 (kSysWiniosAppList)...\n";
    const uint64_t req_va = page_va + 0x200;
    const uint64_t buf_va = page_va + 0x300;

    winios_app_list_req_t list_req{};
    list_req.max_entries = 10;
    list_req.active_only = 0;
    list_req.buffer_va = buf_va;
    uas->copyin(req_va, &list_req, sizeof(list_req));

    cpu.set_program_counter(0x5000);
    cpu.set_nzcv(0);
    cpu.write_register(16, kSysWiniosAppList);
    cpu.write_register(0, req_va);

    dispatcher.handle_syscall(cpu, *proc, *uas, sched);
    assert((cpu.get_nzcv() & (1ULL << 29)) == 0); // PSTATE.C == 0
    assert(cpu.read_register(0) >= 1);

    winios_app_list_req_t res_req{};
    uas->copyout(reinterpret_cast<const void*>(req_va), &res_req, sizeof(res_req));
    assert(res_req.returned_entries >= 1);
    std::cout << "  -> Step 9 SUCCESS: Syscall 593 returned " << res_req.returned_entries << " installed application(s).\n";

    // -------------------------------------------------------------
    // Step 10: Launch the guest application
    // -------------------------------------------------------------
    std::cout << "[Step 10] Launching guest application via AppLaunchPipeline...\n";
    auto identity_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 200, 4);
    auto task_mgr = std::make_shared<settings::TaskManager>();
    auto budget_mgr = std::make_shared<platform::ResourceBudgetManager>();
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    auto diag_svc = std::make_shared<services::DiagnosticsService>();
    auto ipc_broker = std::make_shared<kernel::IpcCapabilityBroker>(identity_mgr, cap_mgr);
    auto doc_reg = std::make_shared<documents::DocumentRegistry>(cap_mgr, identity_mgr);
    platform::AppPerformanceProfiler profiler;

    auto launcher = std::make_shared<loader::AppLaunchPipeline>(
        vfs, registry, identity_mgr, task_mgr, budget_mgr, cap_mgr, diag_svc);

    auto launch_res = launcher->launch("com.winios.e2eapp");
    assert(launch_res.status == loader::LaunchStatus::Success);
    assert(launch_res.handle.pid >= 100);
    assert(launch_res.handle.generation >= 1);
    kernel::ProcessHandle h_app = launch_res.handle;
    std::cout << "  -> Step 10 SUCCESS: Launched PID " << h_app.pid << " (generation " << h_app.generation << ").\n";

    // -------------------------------------------------------------
    // Step 11: Exercise authorized IPC
    // -------------------------------------------------------------
    std::cout << "[Step 11] Exercising authorized IPC communication with anti-replay tracking...\n";
    kernel::ApplicationIdentity sys_app{"com.winios.system", "1.0.0", "System", 0x00020400};
    kernel::ProcessHandle h_sys = identity_mgr->allocate_process(sys_app);

    auto ipc_token = ipc_broker->grant_communication_right(h_app, h_sys, 0x1);
    assert(ipc_token.token_id != 0);
    assert(!ipc_token.token_hex.empty());

    std::vector<uint8_t> msg_payload = {'R', 'E', 'Q', '_', 'D', 'A', 'T', 'A'};
    auto send_st1 = ipc_broker->send_message_by_hex(
        ipc_token.token_hex, h_app, msg_payload.data(), msg_payload.size(), 1, 1);
    assert(send_st1 == kernel::IpcStatus::Success);
    (void)send_st1;

    // Replay attack with same sequence number (1) must be rejected
    auto send_replay = ipc_broker->send_message_by_hex(
        ipc_token.token_hex, h_app, msg_payload.data(), msg_payload.size(), 1, 1);
    assert(send_replay == kernel::IpcStatus::ReplayDetected);
    (void)send_replay;

    // Monotonic next sequence number (2) succeeds
    auto send_st2 = ipc_broker->send_message_by_hex(
        ipc_token.token_hex, h_app, msg_payload.data(), msg_payload.size(), 2, 2);
    assert(send_st2 == kernel::IpcStatus::Success);
    (void)send_st2;
    std::cout << "  -> Step 11 SUCCESS: Authorized IPC send delivered; replayed sequence number rejected.\n";

    // -------------------------------------------------------------
    // Step 12: Exercise document handoff
    // -------------------------------------------------------------
    std::cout << "[Step 12] Exercising generational document handoff and access grant...\n";
    cap_mgr->grant(h_sys.pid, security::Capability::DocumentCreate);
    cap_mgr->grant(h_sys.pid, security::Capability::DocumentShare);
    cap_mgr->grant(h_app.pid, security::Capability::DocumentRead);

    auto doc_h = doc_reg->register_document(
        h_sys.pid, "com.winios.system", "ReportDoc", "/Documents/report.txt", "text/plain", 256, false);
    assert(doc_h != documents::kInvalidDocumentHandle);

    auto grant = doc_reg->create_grant(
        doc_h, h_sys, h_app, "com.winios.e2eapp", documents::DocumentAccessMode::Read);
    assert(!grant.grant_token_hex.empty());
    assert(doc_reg->check_access(doc_h, h_app, documents::DocumentAccessMode::Read));
    std::cout << "  -> Step 12 SUCCESS: Document access granted and verified for PID " << h_app.pid << ".\n";

    // -------------------------------------------------------------
    // Step 13: Capture diagnostics and profiler output
    // -------------------------------------------------------------
    std::cout << "[Step 13] Capturing runtime diagnostics and performance profiling metrics...\n";
    profiler.register_process_launch(h_app.pid, "com.winios.e2eapp", 1200);
    profiler.record_process_instructions(h_app.pid, 2048);
    profiler.record_process_syscall(h_app.pid, 592, 10);
    profiler.record_process_syscall(h_app.pid, 593, 15);
    profiler.record_process_memory_delta(h_app.pid, 65536);

    auto m_opt = profiler.get_process_metrics(h_app.pid);
    assert(m_opt.has_value());
    assert(m_opt->instructions_retired == 2048);
    assert(m_opt->syscalls_dispatched == 2);
    assert(m_opt->current_memory_bytes == 65536);

    diag_svc->log("com.winios.e2eapp", h_app.pid, services::LogLevel::Info, "E2E workflow operational");
    auto logs = diag_svc->get_logs(10);
    assert(!logs.empty());
    std::cout << "  -> Step 13 SUCCESS: Captured profiler metrics (2048 instrs, 2 syscalls) and diagnostics.\n";

    // -------------------------------------------------------------
    // Step 14: Trigger controlled application failure
    // -------------------------------------------------------------
    std::cout << "[Step 14] Triggering controlled application termination...\n";
    profiler.mark_process_terminated(h_app.pid);
    assert(launcher->terminate(h_app));
    std::cout << "  -> Step 14 SUCCESS: Process " << h_app.pid << " terminated cleanly.\n";

    // -------------------------------------------------------------
    // Step 15: Verify cleanup and recovery
    // -------------------------------------------------------------
    std::cout << "[Step 15] Verifying cleanup: handle invalidation, IPC rejection, document revocation...\n";
    assert(!identity_mgr->is_valid_handle(h_app));

    // Stale sender handle rejected in IPC
    auto post_term_ipc = ipc_broker->send_message_by_hex(
        ipc_token.token_hex, h_app, msg_payload.data(), msg_payload.size(), 3, 3);
    assert(post_term_ipc == kernel::IpcStatus::SenderInvalid);
    (void)post_term_ipc;

    // Stale document access rejected
    assert(!doc_reg->check_access(doc_h, h_app, documents::DocumentAccessMode::Read));

    // Budget freed
    assert(budget_mgr->get_task_memory(h_app.pid) == 0);
    std::cout << "  -> Step 15 SUCCESS: Zero orphaned resources, stale tokens completely invalidated.\n";

    // -------------------------------------------------------------
    // Step 16: Relaunch and verify deterministic behavior
    // -------------------------------------------------------------
    std::cout << "[Step 16] Relaunching application and verifying fresh generation allocation...\n";
    auto relaunch_res = launcher->launch("com.winios.e2eapp");
    assert(relaunch_res.status == loader::LaunchStatus::Success);
    assert(relaunch_res.handle.pid == h_app.pid || relaunch_res.handle.pid > h_app.pid);
    if (relaunch_res.handle.pid == h_app.pid) {
        assert(relaunch_res.handle.generation > h_app.generation); // Incremented generation!
    }
    assert(identity_mgr->is_valid_handle(relaunch_res.handle));

    // Clean final termination
    assert(launcher->terminate(relaunch_res.handle));
    assert(!identity_mgr->is_valid_handle(relaunch_res.handle));
    std::cout << "  -> Step 16 SUCCESS: Relaunched deterministically with fresh generation; cleaned up.\n";

    std::cout << "=== All 16 Steps of Stage 231.21 PASSED Successfully ===\n";
    return 0;
}

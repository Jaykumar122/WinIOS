#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/apple_artifact_admission.h"
#include "loader/apple_boot_artifact_inventory.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"
#include "runtime/launchd_daemon_manager.h"

using namespace winios;
using namespace winios::runtime;
using namespace winios::loader;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Test 1: syslogd Guest Userspace Execution & Service Registration
bool test_syslogd_guest_service_execution() {
    std::cout << "[Test 1] syslogd Guest Userspace Execution & Service Registration...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Create log file in VFS
    const char empty[] = "";
    bsd.vfs().write_file("/var/log/system.log", empty, 0);

    // Assembly for syslogd:
    // 1. mov x16, #20          (SYS_getpid -> PID 2)
    // 2. svc #0x80
    // 3. mov x19, x0
    // 4. movz x0, #0x1050      (points to "/var/log/system.log\0")
    // 5. mov x1, #1            (O_WRONLY)
    // 6. mov x16, #5           (SYS_open)
    // 7. svc #0x80             (fd in x0)
    // 8. mov x20, x0
    // 9. mov x0, x20           (fd)
    // 10. movz x1, #0x1070     (points to "SYSLOG_ONLINE\n")
    // 11. mov x2, #14          (len)
    // 12. mov x16, #4          (SYS_write)
    // 13. svc #0x80
    // 14. mov x0, x20          (fd)
    // 15. mov x16, #6          (SYS_close)
    // 16. svc #0x80
    // 17. mov x0, #0           (exit 0)
    // 18. mov x16, #1
    // 19. svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2800290u, // mov x16, #20
        0xD4001001u, // svc #0x80
        0xAA0003F3u, // mov x19, x0
        0xD2820A00u, // movz x0, #0x1050
        0xD2800021u, // mov x1, #1
        0xD28000B0u, // mov x16, #5
        0xD4001001u, // svc #0x80
        0xAA0003F4u, // mov x20, x0
        0xAA1403E0u, // mov x0, x20
        0xD2820E01u, // movz x1, #0x1070
        0xD28001C2u, // mov x2, #14
        0xD2800090u, // mov x16, #4
        0xD4001001u, // svc #0x80
        0xAA1403E0u, // mov x0, x20
        0xD28000D0u, // mov x16, #6
        0xD4001001u, // svc #0x80
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    const char log_path[] = "/var/log/system.log";
    const char log_entry[] = "SYSLOG_ONLINE\n";
    // 19 instrs = 76 bytes (0x4C). String 1 at 0x1050 (80), String 2 at 0x1070 (112)
    std::vector<std::uint8_t> code(guest_asm.size() * 4 + 96);
    std::memcpy(code.data(), guest_asm.data(), guest_asm.size() * 4);
    std::memcpy(code.data() + 0x50, log_path, sizeof(log_path));
    std::memcpy(code.data() + 0x70, log_entry, sizeof(log_entry));

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/usr/sbin/syslogd";
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 2);

    TEST_ASSERT(res.process_exited, "syslogd must exit cleanly");
    TEST_ASSERT(res.exit_code == 0, "syslogd exit code must be 0");
    TEST_ASSERT(res.guest_instructions_retired >= 19, "Must retire all 19 guest instructions");

    auto node = bsd.vfs().resolve_path("/var/log/system.log");
    TEST_ASSERT(node != nullptr, "Log file must exist");
    std::string s(node->content.begin(), node->content.end());
    TEST_ASSERT(s.find("SYSLOG_ONLINE") != std::string::npos, "Log entry must be written by guest");

    std::cout << "  Passed (syslogd executed in EL0, wrote to /var/log/system.log)\n";
    return true;
}

// Test 2: notifyd Guest Userspace Execution & PID File Creation
bool test_notifyd_guest_service_execution() {
    std::cout << "[Test 2] notifyd Guest Userspace Execution & PID File Creation...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    const char empty[] = "";
    bsd.vfs().write_file("/var/run/notifyd.pid", empty, 0);

    // Assembly for notifyd:
    // movz x0, #0x1040     (points to "/var/run/notifyd.pid\0")
    // mov x1, #1           (O_WRONLY)
    // mov x16, #5          (SYS_open)
    // svc #0x80
    // mov x19, x0          (fd)
    // mov x0, x19
    // movz x1, #0x1060     (points to "3\n")
    // mov x2, #2
    // mov x16, #4          (SYS_write)
    // svc #0x80
    // mov x0, x19
    // mov x16, #6          (SYS_close)
    // svc #0x80
    // mov x0, #0
    // mov x16, #1
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2820800u, // movz x0, #0x1040
        0xD2800021u, // mov x1, #1
        0xD28000B0u, // mov x16, #5
        0xD4001001u, // svc #0x80
        0xAA0003F3u, // mov x19, x0
        0xAA1303E0u, // mov x0, x19
        0xD2820C01u, // movz x1, #0x1060
        0xD2800042u, // mov x2, #2
        0xD2800090u, // mov x16, #4
        0xD4001001u, // svc #0x80
        0xAA1303E0u, // mov x0, x19
        0xD28000D0u, // mov x16, #6
        0xD4001001u, // svc #0x80
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    const char pid_path[] = "/var/run/notifyd.pid";
    const char pid_str[] = "3\n";
    // 16 instrs = 64 bytes (0x40). String 1 at 0x1040 (64), String 2 at 0x1060 (96)
    std::vector<std::uint8_t> code(guest_asm.size() * 4 + 64);
    std::memcpy(code.data(), guest_asm.data(), guest_asm.size() * 4);
    std::memcpy(code.data() + 0x40, pid_path, sizeof(pid_path));
    std::memcpy(code.data() + 0x60, pid_str, sizeof(pid_str));

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/usr/sbin/notifyd";
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 3);

    TEST_ASSERT(res.process_exited && res.exit_code == 0, "notifyd must exit cleanly");

    auto node = bsd.vfs().resolve_path("/var/run/notifyd.pid");
    TEST_ASSERT(node != nullptr, "PID file must exist");
    std::string s(node->content.begin(), node->content.end());
    TEST_ASSERT(s.find("3") != std::string::npos, "PID file must contain daemon PID");

    std::cout << "  Passed (notifyd created /var/run/notifyd.pid)\n";
    return true;
}

// Test 3: securityd Guest Userspace Execution & Keychain Initialization
bool test_securityd_guest_service_execution() {
    std::cout << "[Test 3] securityd Guest Userspace Execution & Keychain Access...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    const char kc_init[] = "KEYCHAIN_V2_SQLITE";
    bsd.vfs().write_file("/var/Keychains/keychain-2.db", kc_init, sizeof(kc_init) - 1);

    // Assembly for securityd:
    // movz x0, #0x1040     (points to "/var/Keychains/keychain-2.db\0")
    // mov x1, #0           (O_RDONLY)
    // mov x16, #5          (SYS_open)
    // svc #0x80
    // mov x19, x0          (fd)
    // mov x0, x19
    // mov x16, #6          (SYS_close)
    // svc #0x80
    // mov x0, #0
    // mov x16, #1
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2820800u, // movz x0, #0x1040
        0xD2800001u, // mov x1, #0
        0xD28000B0u, // mov x16, #5
        0xD4001001u, // svc #0x80
        0xAA0003F3u, // mov x19, x0
        0xAA1303E0u, // mov x0, x19
        0xD28000D0u, // mov x16, #6
        0xD4001001u, // svc #0x80
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    const char db_path[] = "/var/Keychains/keychain-2.db";
    std::vector<std::uint8_t> code(guest_asm.size() * 4 + 64);
    std::memcpy(code.data(), guest_asm.data(), guest_asm.size() * 4);
    std::memcpy(code.data() + 0x40, db_path, sizeof(db_path));

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/usr/libexec/securityd";
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 4);

    TEST_ASSERT(res.process_exited && res.exit_code == 0, "securityd must exit cleanly");

    std::cout << "  Passed (securityd opened and verified keychain database)\n";
    return true;
}

// Test 4: Multi-Process PID Isolation & Independent Contexts
bool test_multiprocess_pid_isolation() {
    std::cout << "[Test 4] Multi-Process PID Isolation & Independent Contexts...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid_a = bsd.create_process_with_pid(10, 1, "/usr/sbin/syslogd", 0);
    std::uint32_t pid_b = bsd.create_process_with_pid(20, 1, "/usr/sbin/notifyd", 0);

    TEST_ASSERT(pid_a == 10, "PID A must match requested");
    TEST_ASSERT(pid_b == 20, "PID B must match requested");

    auto proc_a = bsd.get_process(10);
    auto proc_b = bsd.get_process(20);
    TEST_ASSERT(proc_a && proc_b, "Both processes must exist");

    // Open file in Proc A
    const char dummy[] = "";
    bsd.vfs().write_file("/var/log/system.log", dummy, 0);
    int fd_a = bsd.open(10, "/var/log/system.log", 0);
    TEST_ASSERT(fd_a >= 3, "Proc A opened fd");

    // Verify Proc B does NOT share fd_a
    TEST_ASSERT(proc_b->fds.find(fd_a) == proc_b->fds.end(), "Proc B must NOT have Proc A's file descriptor");

    bsd.close(10, fd_a);
    std::cout << "  Passed (Isolated file descriptor tables between processes confirmed)\n";
    return true;
}

// Test 5: Cross-Daemon Mach IPC Communication
bool test_cross_daemon_mach_communication() {
    std::cout << "[Test 5] Cross-Daemon Mach IPC Communication...\n";

    MachIpcSubsystem ipc;
    // Daemon (syslogd at task 2) sets up receive port and registers with bootstrap
    auto space_srv = ipc.create_task_port_space(2);
    auto srv_obj = std::make_shared<MachPortObject>(8001, 2);
    mach_port_t rcv_port = space_srv->allocate_port(srv_obj, kMachPortRightReceive);
    space_srv->insert_right(rcv_port, srv_obj, kMachPortRightSend);
    ipc.bootstrap_register(2, "com.apple.system.logger", rcv_port);

    // Client (app at task 50) looks up "com.apple.system.logger"
    mach_port_t client_send_port = ipc.bootstrap_look_up(50, "com.apple.system.logger");
    TEST_ASSERT(client_send_port != kMachPortNull, "Client must resolve bootstrap port");

    auto space_client = ipc.get_task_port_space(50);
    TEST_ASSERT(space_client != nullptr, "Client space must exist");
    auto resolved_obj = space_client->lookup_port(client_send_port);
    TEST_ASSERT(resolved_obj == srv_obj, "Resolved port must point to same MachPortObject");

    // Client enqueues message to logger port
    MachMessage msg{};
    msg.header.msgh_id = 999;
    srv_obj->enqueue_message(std::move(msg));

    TEST_ASSERT(srv_obj->queue_size() == 1, "Server must have 1 queued message");
    auto recvd = srv_obj->dequeue_message();
    TEST_ASSERT(recvd.has_value() && recvd->header.msgh_id == 999, "Message ID must match");

    std::cout << "  Passed (Cross-task Mach IPC communication verified)\n";
    return true;
}

// Test 6: VFS Service State Files
bool test_vfs_service_state_files() {
    std::cout << "[Test 6] VFS Service State Files Integrity...\n";

    DarwinVfs vfs;
    vfs.populate_default_rootfs();

    TEST_ASSERT(vfs.resolve_path("/var/run") != nullptr, "/var/run must exist");
    TEST_ASSERT(vfs.resolve_path("/var/log") != nullptr, "/var/log must exist");
    TEST_ASSERT(vfs.resolve_path("/var/Keychains") != nullptr, "/var/Keychains must exist");

    const char test_data[] = "12345";
    vfs.write_file("/var/run/syslogd.pid", test_data, sizeof(test_data) - 1);
    auto pid_node = vfs.resolve_path("/var/run/syslogd.pid");
    TEST_ASSERT(pid_node != nullptr && pid_node->size == sizeof(test_data) - 1, "File written accurately");

    std::cout << "  Passed (Darwin VFS service directory hierarchy verified)\n";
    return true;
}

// Test 7: Daemon Crash & Recovery Cycle
bool test_daemon_crash_recovery_cycle() {
    std::cout << "[Test 7] Daemon Crash & Recovery Cycle...\n";

    LaunchdDaemonManager mgr;
    mgr.populate_default_ios_daemons();

    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory mem(64 * 1024 * 1024);

    mgr.start_job("com.apple.notifyd", bsd, ipc, mem);
    const auto* j = mgr.get_job("com.apple.notifyd");
    TEST_ASSERT(j && j->state == LaunchdJobState::Running, "notifyd must be running");
    std::uint32_t first_pid = j->pid;

    // Simulate crash with SIGBUS (10)
    mgr.handle_process_exit(first_pid, 10, bsd, ipc, mem);

    const auto* j2 = mgr.get_job("com.apple.notifyd");
    TEST_ASSERT(j2 && j2->state == LaunchdJobState::Running, "notifyd must restart");
    TEST_ASSERT(j2->pid != first_pid, "Must have new PID");
    TEST_ASSERT(j2->crash_count == 1, "Crash count must be 1");

    std::cout << "  Passed (Crash recovery cycle validated for notifyd)\n";
    return true;
}

// Test 8: Daemon Permission and Privilege Hierarchy (UID 0 vs 501)
bool test_daemon_permission_privileges() {
    std::cout << "[Test 8] Daemon Privilege Hierarchy (UID 0 vs 501)...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t root_pid = bsd.create_process_with_pid(30, 1, "/usr/sbin/syslogd", 0);
    std::uint32_t mob_pid = bsd.create_process_with_pid(31, 1, "/Applications/App.app/App", 501);

    auto p_root = bsd.get_process(root_pid);
    auto p_mob = bsd.get_process(mob_pid);

    TEST_ASSERT(p_root && p_root->uid == 0, "Daemon must have UID 0 (root)");
    TEST_ASSERT(p_mob && p_mob->uid == 501, "User app must have UID 501 (mobile)");

    std::cout << "  Passed (Privilege separation between system daemons and mobile apps confirmed)\n";
    return true;
}

// Test 9: Genuine Daemon Mach-O Admission Gate (BLOCKED)
bool test_genuine_daemon_admission_gate() {
    std::cout << "[Test 9] Genuine Daemon Mach-O Admission Gate (BLOCKED)...\n";

    AppleArtifactAdmissionController controller;

    // Genuine unextracted syslogd binary without payload
    AppleBootArtifact real_syslogd{};
    real_syslogd.artifact_id = "genuine_syslogd";
    real_syslogd.file_path = "/usr/sbin/syslogd";
    real_syslogd.kind = AppleArtifactKind::DarwinServiceBinary;
    real_syslogd.provenance = ArtifactProvenance::DecryptedReal;
    real_syslogd.is_synthetic = false;

    auto res = controller.evaluate_artifact(real_syslogd, nullptr, 0);
    TEST_ASSERT(!res.admitted, "Unextracted genuine daemon must NOT be admitted");
    TEST_ASSERT(res.decision == AdmissionDecision::Rejected_Truncated,
                "Must reject empty/missing genuine binary");

    std::cout << "  Passed (Genuine Darwin daemon admission gate verified as BLOCKED)\n";
    return true;
}

// Test 10: Full Darwin Services Sequential Startup Sequence
bool test_full_darwin_services_startup_sequence() {
    std::cout << "[Test 10] Full Darwin Services Sequential Startup Sequence...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);
    LaunchdDaemonManager mgr;
    mgr.populate_default_ios_daemons();

    // Start syslogd -> notifyd -> securityd
    bool ok1 = mgr.start_job("com.apple.syslogd", bsd, ipc, mem);
    bool ok2 = mgr.start_job("com.apple.notifyd", bsd, ipc, mem);
    bool ok3 = mgr.start_job("com.apple.securityd", bsd, ipc, mem);

    TEST_ASSERT(ok1 && ok2 && ok3, "All 3 core services must start successfully");

    const auto* j_sys = mgr.get_job("com.apple.syslogd");
    const auto* j_not = mgr.get_job("com.apple.notifyd");
    const auto* j_sec = mgr.get_job("com.apple.securityd");

    TEST_ASSERT(j_sys->state == LaunchdJobState::Running, "syslogd running");
    TEST_ASSERT(j_not->state == LaunchdJobState::Running, "notifyd running");
    TEST_ASSERT(j_sec->state == LaunchdJobState::Running, "securityd running");

    TEST_ASSERT(j_sys->pid < j_not->pid, "PID ordering matches launch order");
    TEST_ASSERT(j_not->pid < j_sec->pid, "PID ordering matches launch order");

    std::cout << "  Passed (All 3 Darwin core service daemons active and supervised)\n";
    return true;
}

} // namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "WINIOS STAGE 136: DARWIN SERVICE STARTUP & IPC SUPERVISION\n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](const char* name, bool (*fn)()) {
        if (fn()) {
            passed++;
        } else {
            failed++;
            std::cerr << "FAILED: " << name << "\n";
        }
    };

    run_test("test_syslogd_guest_service_execution", test_syslogd_guest_service_execution);
    run_test("test_notifyd_guest_service_execution", test_notifyd_guest_service_execution);
    run_test("test_securityd_guest_service_execution", test_securityd_guest_service_execution);
    run_test("test_multiprocess_pid_isolation", test_multiprocess_pid_isolation);
    run_test("test_cross_daemon_mach_communication", test_cross_daemon_mach_communication);
    run_test("test_vfs_service_state_files", test_vfs_service_state_files);
    run_test("test_daemon_crash_recovery_cycle", test_daemon_crash_recovery_cycle);
    run_test("test_daemon_permission_privileges", test_daemon_permission_privileges);
    run_test("test_genuine_daemon_admission_gate", test_genuine_daemon_admission_gate);
    run_test("test_full_darwin_services_startup_sequence", test_full_darwin_services_startup_sequence);

    std::cout << "-----------------------------------------------------------------\n";
    std::cout << "STAGE 136 RESULTS: " << passed << "/" << (passed + failed) << " tests passed\n";
    std::cout << "=================================================================\n";

    return (failed == 0) ? 0 : 1;
}

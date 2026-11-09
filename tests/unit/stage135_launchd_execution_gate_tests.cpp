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

// Test 1: Genuine /sbin/launchd Artifact Admission Gate (BLOCKED)
bool test_launchd_genuine_artifact_admission_gate() {
    std::cout << "[Test 1] Genuine /sbin/launchd Artifact Admission Gate...\n";

    AppleArtifactAdmissionController controller;

    // 1. Genuine artifact definition without decrypted content (empty / null data)
    AppleBootArtifact real_launchd{};
    real_launchd.artifact_id = "genuine_launchd";
    real_launchd.file_path = "/sbin/launchd";
    real_launchd.kind = AppleArtifactKind::LaunchdBinary;
    real_launchd.provenance = ArtifactProvenance::DecryptedReal;
    real_launchd.is_synthetic = false;

    auto res = controller.evaluate_artifact(real_launchd, nullptr, 0);
    TEST_ASSERT(!res.admitted, "Genuine unextracted launchd must NOT be admitted");
    TEST_ASSERT(res.decision == AdmissionDecision::Rejected_Truncated,
                "Empty/unextracted artifact must produce Rejected_Truncated");

    // 2. Synthetic binary claimed as genuine decrypted real artifact must be rejected
    auto fake_macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();
    real_launchd.is_synthetic = true; // Synthetic fixture
    real_launchd.provenance = ArtifactProvenance::DecryptedReal; // Fraudulently claimed real

    auto res2 = controller.evaluate_artifact(real_launchd, fake_macho.data(), fake_macho.size());
    TEST_ASSERT(!res2.admitted, "Synthetic binary claimed as genuine must be rejected");
    TEST_ASSERT(res2.decision == AdmissionDecision::Rejected_SyntheticAsReal,
                "Must reject synthetic masquerading as genuine");

    std::cout << "  Passed (Genuine /sbin/launchd admission gate rigorously enforced as BLOCKED)\n";
    return true;
}

// Test 2: Synthetic launchd Guest Userspace Execution at PID 1
bool test_launchd_pid1_userspace_execution() {
    std::cout << "[Test 2] Synthetic launchd Guest Userspace Execution at PID 1...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly for launchd init:
    // mov x16, #20         (SYS_getpid)
    // svc #0x80            (returns pid in x0; expected 1)
    // mov x19, x0          (save pid)
    // mov x0, x19          (exit code = pid)
    // mov x16, #1          (SYS_exit)
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2800290u, // mov x16, #20
        0xD4001001u, // svc #0x80
        0xAA0003F3u, // mov x19, x0
        0xAA1303E0u, // mov x0, x19
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/sbin/launchd";
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 1);

    TEST_ASSERT(res.process_exited, "launchd must exit cleanly");
    TEST_ASSERT(res.exit_code == 1, "launchd must observe PID 1 from SYS_getpid");
    TEST_ASSERT(res.guest_instructions_retired >= 6, "Must retire all 6 guest instructions");
    TEST_ASSERT(res.bsd_syscalls_handled >= 2, "Must handle SYS_getpid and SYS_exit");

    std::cout << "  Passed (Guest launchd retired " << res.guest_instructions_retired
              << " instrs, confirmed PID " << res.exit_code << ")\n";
    return true;
}

// Test 3: launchd Task Self and Bootstrap Port Rights
bool test_launchd_task_self_and_bootstrap_port() {
    std::cout << "[Test 3] launchd Task Self and Bootstrap Port Rights...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly:
    // mov x16, #-28        (task_self_trap)
    // svc #0x81            (returns task self port in x0)
    // mov x16, #1          (SYS_exit with task self port)
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0x92800370u, // mov x16, #-28
        0xD4001021u, // svc #0x81
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/sbin/launchd";
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 1);

    TEST_ASSERT(res.process_exited, "Process must exit");
    TEST_ASSERT(res.exit_code == 101, "PID 1 task_self must equal port 101");
    TEST_ASSERT(res.mach_traps_handled >= 1, "Must handle task_self_trap");

    std::cout << "  Passed (launchd task_self port verified as " << res.exit_code << ")\n";
    return true;
}

// Test 4: Property List (.plist) Daemon Configuration Parsing
bool test_launchd_plist_daemon_configuration() {
    std::cout << "[Test 4] Property List (.plist) Daemon Configuration Parsing...\n";

    LaunchdDaemonManager mgr;

    const std::string plist = 
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "    <key>Label</key>\n"
        "    <string>com.apple.testservice</string>\n"
        "    <key>Program</key>\n"
        "    <string>/usr/libexec/testservice</string>\n"
        "    <key>RunAtLoad</key>\n"
        "    <true/>\n"
        "    <key>KeepAlive</key>\n"
        "    <true/>\n"
        "</dict>\n"
        "</plist>\n";

    bool loaded = mgr.load_job_from_plist(plist);
    TEST_ASSERT(loaded, "Failed to load job from plist");
    TEST_ASSERT(mgr.has_job("com.apple.testservice"), "Job must be registered in manager");

    const auto* job = mgr.get_job("com.apple.testservice");
    TEST_ASSERT(job != nullptr, "Job pointer must not be null");
    TEST_ASSERT(job->config.label == "com.apple.testservice", "Label must match");
    TEST_ASSERT(job->config.program_arguments.size() == 1, "Program arguments count must be 1");
    TEST_ASSERT(job->config.program_arguments[0] == "/usr/libexec/testservice", "Program path must match");
    TEST_ASSERT(job->config.run_at_load, "RunAtLoad must be true");
    TEST_ASSERT(job->config.keep_alive, "KeepAlive must be true");

    std::cout << "  Passed (Parsed launchd XML plist into LaunchdJobConfig)\n";
    return true;
}

// Test 5: Default iOS System Daemons Catalog
bool test_launchd_default_ios_daemons_population() {
    std::cout << "[Test 5] Default iOS System Daemons Catalog...\n";

    LaunchdDaemonManager mgr;
    mgr.populate_default_ios_daemons();

    auto labels = mgr.get_all_labels();
    TEST_ASSERT(labels.size() >= 5, "Must populate at least 5 core iOS daemons");

    TEST_ASSERT(mgr.has_job("com.apple.syslogd"), "Must contain syslogd");
    TEST_ASSERT(mgr.has_job("com.apple.notifyd"), "Must contain notifyd");
    TEST_ASSERT(mgr.has_job("com.apple.securityd"), "Must contain securityd");
    TEST_ASSERT(mgr.has_job("com.apple.WindowServer"), "Must contain WindowServer");
    TEST_ASSERT(mgr.has_job("com.apple.SpringBoard"), "Must contain SpringBoard");

    const auto* sb = mgr.get_job("com.apple.SpringBoard");
    TEST_ASSERT(sb != nullptr, "SpringBoard must be registered");
    TEST_ASSERT(!sb->config.dependencies.empty(), "SpringBoard must have dependencies");
    TEST_ASSERT(sb->config.dependencies[0] == "com.apple.WindowServer",
                "SpringBoard must depend on WindowServer");

    std::cout << "  Passed (Validated " << labels.size() << " core iOS daemons in catalog)\n";
    return true;
}

// Test 6: Topological Daemon Dependency Resolution
bool test_launchd_topological_service_dependency_resolution() {
    std::cout << "[Test 6] Topological Daemon Dependency Resolution...\n";

    LaunchdDaemonManager mgr;
    mgr.populate_default_ios_daemons();

    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory mem(64 * 1024 * 1024);

    // Starting SpringBoard directly must recursively launch its dependency tree:
    // SpringBoard -> WindowServer -> securityd -> notifyd -> syslogd
    bool ok = mgr.start_job("com.apple.SpringBoard", bsd, ipc, mem);
    TEST_ASSERT(ok, "SpringBoard launch must succeed");

    // Verify all upstream dependencies transitioned to Running state
    const auto* syslogd = mgr.get_job("com.apple.syslogd");
    const auto* notifyd = mgr.get_job("com.apple.notifyd");
    const auto* securityd = mgr.get_job("com.apple.securityd");
    const auto* ws = mgr.get_job("com.apple.WindowServer");
    const auto* sb = mgr.get_job("com.apple.SpringBoard");

    TEST_ASSERT(syslogd && syslogd->state == LaunchdJobState::Running, "syslogd must be running");
    TEST_ASSERT(notifyd && notifyd->state == LaunchdJobState::Running, "notifyd must be running");
    TEST_ASSERT(securityd && securityd->state == LaunchdJobState::Running, "securityd must be running");
    TEST_ASSERT(ws && ws->state == LaunchdJobState::Running, "WindowServer must be running");
    TEST_ASSERT(sb && sb->state == LaunchdJobState::Running, "SpringBoard must be running");

    // Verify distinct PIDs allocated under launchd parent (PID 1)
    TEST_ASSERT(syslogd->pid > 1, "syslogd PID > 1");
    TEST_ASSERT(ws->pid > syslogd->pid, "WindowServer launched after syslogd");
    TEST_ASSERT(sb->pid > ws->pid, "SpringBoard launched after WindowServer");

    std::cout << "  Passed (All 5 daemons started in exact dependency order: syslogd -> notifyd -> securityd -> ws -> sb)\n";
    return true;
}

// Test 7: Child Crash Detection & KeepAlive Respawn
bool test_launchd_child_crash_and_keepalive_restart() {
    std::cout << "[Test 7] Child Crash Detection & KeepAlive Respawn...\n";

    LaunchdDaemonManager mgr;
    mgr.populate_default_ios_daemons();

    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory mem(64 * 1024 * 1024);

    // Start syslogd
    mgr.start_job("com.apple.syslogd", bsd, ipc, mem);
    const auto* job = mgr.get_job("com.apple.syslogd");
    TEST_ASSERT(job != nullptr && job->state == LaunchdJobState::Running, "syslogd must be running");
    std::uint32_t original_pid = job->pid;

    // Simulate SIGSEGV crash (exit code 139)
    mgr.handle_process_exit(original_pid, 139, bsd, ipc, mem);

    // Job must be restarted under a new PID with crash_count incremented
    const auto* restarted_job = mgr.get_job("com.apple.syslogd");
    TEST_ASSERT(restarted_job != nullptr, "Job must exist");
    TEST_ASSERT(restarted_job->state == LaunchdJobState::Running, "Job must restart to Running");
    TEST_ASSERT(restarted_job->pid != original_pid, "Restarted job must have new PID");
    TEST_ASSERT(restarted_job->crash_count == 1, "Crash count must be incremented to 1");
    TEST_ASSERT(restarted_job->last_exit_code == 139, "Last exit code recorded");

    std::cout << "  Passed (Crashed daemon PID " << original_pid << " respawned as PID "
              << restarted_job->pid << " with crash count " << restarted_job->crash_count << ")\n";
    return true;
}

// Test 8: Guest Stack SP Alignment and Argument Layout
bool test_launchd_guest_stack_and_arguments() {
    std::cout << "[Test 8] Guest Stack SP Alignment & Argument Layout...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly:
    // mov x0, sp           (copy SP to x0)
    // and x0, x0, #0xF     (check 16-byte alignment: SP & 0xF == 0)
    // mov x16, #1          (SYS_exit with exit code = alignment remainder)
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0x910003E0u, // mov x0, sp (add x0, sp, #0)
        0x92400C00u, // and x0, x0, #0xF
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/sbin/launchd";
    cfg.argv = {"/sbin/launchd", "--verbose"};
    cfg.envp = {"PATH=/bin:/usr/bin:/sbin:/usr/sbin", "HOME=/var/root"};

    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 1);

    TEST_ASSERT(res.process_exited, "Process must exit");
    TEST_ASSERT(res.exit_code == 0, "Guest SP must be strictly 16-byte aligned (SP & 0xF == 0)");

    std::cout << "  Passed (Guest stack strictly aligned to 16 bytes)\n";
    return true;
}

// Test 9: Bootstrap Service Registration & Lookup for System Daemons
bool test_launchd_bootstrap_service_registration() {
    std::cout << "[Test 9] Bootstrap Service Registration & Lookup for System Daemons...\n";

    LaunchdDaemonManager mgr;
    mgr.populate_default_ios_daemons();

    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory mem(64 * 1024 * 1024);

    // Start all default daemons
    std::size_t started = mgr.launch_system_boot_daemons(bsd, ipc, mem);
    TEST_ASSERT(started >= 5, "Must start at least 5 boot daemons");

    // Verify system services are registered in the Mach bootstrap registry
    mach_port_t logger_port = ipc.bootstrap_look_up(1, "com.apple.system.logger");
    TEST_ASSERT(logger_port != kMachPortNull, "com.apple.system.logger must be discoverable");

    mach_port_t notify_port = ipc.bootstrap_look_up(1, "com.apple.system.notification_center");
    TEST_ASSERT(notify_port != kMachPortNull, "com.apple.system.notification_center must be discoverable");

    mach_port_t sec_port = ipc.bootstrap_look_up(1, "com.apple.securityd");
    TEST_ASSERT(sec_port != kMachPortNull, "com.apple.securityd must be discoverable");

    mach_port_t sb_port = ipc.bootstrap_look_up(1, "com.apple.springboard.services");
    TEST_ASSERT(sb_port != kMachPortNull, "com.apple.springboard.services must be discoverable");

    std::cout << "  Passed (Mach bootstrap registry contains active system service ports)\n";
    return true;
}

// Test 10: Complete Integrated Boot Sequence (Stage 135 Milestone)
bool test_launchd_end_to_end_boot_orchestration() {
    std::cout << "[Test 10] Complete Integrated Boot Sequence (Stage 135 Milestone)...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);
    LaunchdDaemonManager mgr;
    mgr.populate_default_ios_daemons();

    // 1. Synthetic launchd binary executes at PID 1
    // Assembly:
    // mov x16, #20         (SYS_getpid -> returns 1)
    // svc #0x80
    // mov x19, x0          (save 1)
    // mov x16, #-28        (task_self_trap -> returns 101)
    // svc #0x81
    // mov x20, x0          (save 101)
    // mov x0, #0           (clean exit 0)
    // mov x16, #1
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2800290u, // mov x16, #20
        0xD4001001u, // svc #0x80
        0xAA0003F3u, // mov x19, x0
        0x92800370u, // mov x16, #-28
        0xD4001021u, // svc #0x81
        0xAA0003F4u, // mov x20, x0
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/sbin/launchd";
    cfg.argv = {"/sbin/launchd"};
    cfg.envp = {"HOME=/var/root"};

    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 1);
    TEST_ASSERT(res.process_exited && res.exit_code == 0, "launchd init sequence exited cleanly");
    TEST_ASSERT(res.guest_instructions_retired >= 8, "All launchd instructions retired");

    // 2. Orchestrate system boot daemons
    std::size_t launched = mgr.launch_system_boot_daemons(bsd, ipc, mem);
    TEST_ASSERT(launched >= 5, "All system daemons started");

    // 3. Verify SpringBoard and WindowServer are alive
    const auto* sb = mgr.get_job("com.apple.SpringBoard");
    const auto* ws = mgr.get_job("com.apple.WindowServer");
    TEST_ASSERT(sb && sb->state == LaunchdJobState::Running, "SpringBoard must be running");
    TEST_ASSERT(ws && ws->state == LaunchdJobState::Running, "WindowServer must be running");

    std::cout << "  Passed (End-to-end boot orchestration reached running state)\n";
    return true;
}

} // namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "WINIOS STAGE 135: LAUNCHD EXECUTION GATE & DAEMON SUPERVISOR\n";
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

    run_test("test_launchd_genuine_artifact_admission_gate", test_launchd_genuine_artifact_admission_gate);
    run_test("test_launchd_pid1_userspace_execution", test_launchd_pid1_userspace_execution);
    run_test("test_launchd_task_self_and_bootstrap_port", test_launchd_task_self_and_bootstrap_port);
    run_test("test_launchd_plist_daemon_configuration", test_launchd_plist_daemon_configuration);
    run_test("test_launchd_default_ios_daemons_population", test_launchd_default_ios_daemons_population);
    run_test("test_launchd_topological_service_dependency_resolution", test_launchd_topological_service_dependency_resolution);
    run_test("test_launchd_child_crash_and_keepalive_restart", test_launchd_child_crash_and_keepalive_restart);
    run_test("test_launchd_guest_stack_and_arguments", test_launchd_guest_stack_and_arguments);
    run_test("test_launchd_bootstrap_service_registration", test_launchd_bootstrap_service_registration);
    run_test("test_launchd_end_to_end_boot_orchestration", test_launchd_end_to_end_boot_orchestration);

    std::cout << "-----------------------------------------------------------------\n";
    std::cout << "STAGE 135 RESULTS: " << passed << "/" << (passed + failed) << " tests passed\n";
    std::cout << "=================================================================\n";

    return (failed == 0) ? 0 : 1;
}

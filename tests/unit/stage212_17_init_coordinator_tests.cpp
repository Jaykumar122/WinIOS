#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "kernel/bootstrap_registry.h"
#include "kernel/bsd_process.h"
#include "kernel/init_coordinator.h"
#include "kernel/process_spawner.h"
#include "kernel/thread.h"
#include "loader/macho_user_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;
using namespace winios::loader;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << std::endl; \
            ++g_failures; \
        } else { \
            std::cout << "  [OK] " << (msg) << std::endl; \
        } \
    } while (0)

struct InitTestContext {
    Memory mem{0x800000};
    PhysicalPageManager ppm{mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL};
    KernelThreadManager thread_mgr{};
    BsdProcessManager proc_mgr{};
    BootstrapServiceRegistry bootstrap{};
    std::unique_ptr<InitCoordinator> init;

    InitTestContext() {
        ppm.initialize(false);
        proc_mgr.initialize(); // Creates PID 1 init
        init = std::make_unique<InitCoordinator>(proc_mgr, bootstrap, thread_mgr, ppm, mem);
    }
};

void test_spawn_and_supervision() {
    std::cout << "[Test 1] Supervised Service Spawning & Normal Exit Reaping..." << std::endl;

    InitTestContext ctx;
    const auto bin = MachoUserLoader::get_embedded_init_app();

    // Spawn two services
    const pid_t pid1 = ctx.init->spawn_service("syslogd", bin, {"syslogd"});
    const pid_t pid2 = ctx.init->spawn_service("powerd", bin, {"powerd"});

    TEST_CHECK(pid1 > 1, "syslogd spawned with PID > 1");
    TEST_CHECK(pid2 > 1, "powerd spawned with PID > 1");
    TEST_CHECK(pid1 != pid2, "PIDs are distinct");

    auto* proc1 = ctx.proc_mgr.get_process(pid1);
    auto* proc2 = ctx.proc_mgr.get_process(pid2);
    TEST_CHECK(proc1 && proc1->ppid() == 1, "syslogd parent PID is 1 (Init)");
    TEST_CHECK(proc2 && proc2->ppid() == 1, "powerd parent PID is 1 (Init)");
    TEST_CHECK(ctx.init->active_service_count() == 2, "2 active services monitored");

    // Powerd exits cleanly (code 0)
    proc2->terminate(0);
    const std::size_t events = ctx.init->supervise_step();
    TEST_CHECK(events == 1, "Supervise step reaped powerd");

    const auto* svc2 = ctx.init->get_service("powerd");
    TEST_CHECK(svc2 != nullptr, "powerd service record found");
    TEST_CHECK(svc2->status == SupervisedServiceStatus::Exited, "powerd marked as Exited");
    TEST_CHECK(svc2->exit_code == 0, "powerd exit code is 0");
    TEST_CHECK(ctx.init->active_service_count() == 1, "1 active service remaining");
}

void test_orphan_adoption_and_reaping() {
    std::cout << "[Test 2] Reparented Orphan Zombie Adoption and Reaping..." << std::endl;

    InitTestContext ctx;

    // Create intermediate parent process (PID 10)
    auto* parent = ctx.proc_mgr.create_process("worker_parent", 1);
    TEST_CHECK(parent != nullptr, "Created worker_parent");
    const pid_t parent_pid = parent->pid();

    // Create child of worker_parent (grandchild of Init)
    auto* child = ctx.proc_mgr.create_process("worker_child", parent_pid);
    TEST_CHECK(child != nullptr, "Created worker_child");
    const pid_t child_pid = child->pid();

    TEST_CHECK(child->ppid() == parent_pid, "worker_child initial parent is worker_parent");

    // worker_parent terminates and is reaped -> worker_child reparented to PID 1
    ctx.proc_mgr.terminate_process(parent_pid, 0);
    int status = 0;
    ctx.proc_mgr.wait_process(1, parent_pid, &status, 1 /* WNOHANG */);

    TEST_CHECK(child->ppid() == 1, "worker_child reparented to PID 1 after parent reaped");

    // worker_child terminates
    ctx.proc_mgr.terminate_process(child_pid, 0);

    // Init supervison step reaps the reparented orphan
    const std::size_t reaped = ctx.init->supervise_step();
    TEST_CHECK(reaped >= 1, "InitCoordinator reaped reparented orphan");
    TEST_CHECK(ctx.init->reaped_orphan_count() == 1, "reaped_orphan_count is 1");
    TEST_CHECK(ctx.proc_mgr.get_process(child_pid) == nullptr, "Orphan process removed from process table");
}

void test_service_crash_detection() {
    std::cout << "[Test 3] Supervised Service Crash Detection..." << std::endl;

    InitTestContext ctx;
    const auto bin = MachoUserLoader::get_embedded_init_app();

    const pid_t pid = ctx.init->spawn_service("crasher_daemon", bin, {"crasher_daemon"});
    TEST_CHECK(pid > 1, "Spawned crasher_daemon");

    // Register service in bootstrap registry
    ctx.bootstrap.check_in("com.apple.crasher", 88, pid);
    TEST_CHECK(ctx.bootstrap.is_service_active("com.apple.crasher"), "Service active in bootstrap");

    // Daemon crashes with signal 11 (SIGSEGV, exit code 139)
    auto* proc = ctx.proc_mgr.get_process(pid);
    proc->terminate(139);

    ctx.init->supervise_step();

    const auto* svc = ctx.init->get_service("crasher_daemon");
    TEST_CHECK(svc && svc->status == SupervisedServiceStatus::Crashed, "Service marked as Crashed");
    TEST_CHECK(svc->exit_code == 139, "Reported crash exit code 139");
    TEST_CHECK(!ctx.bootstrap.is_service_active("com.apple.crasher"), "Bootstrap service transitioned to dead on crash");
}

void test_graceful_shutdown() {
    std::cout << "[Test 4] Graceful Shutdown of All Supervised Services..." << std::endl;

    InitTestContext ctx;
    const auto bin = MachoUserLoader::get_embedded_init_app();

    ctx.init->spawn_service("svc_alpha", bin);
    ctx.init->spawn_service("svc_beta", bin);
    ctx.init->spawn_service("svc_gamma", bin);

    TEST_CHECK(ctx.init->active_service_count() == 3, "3 active services before shutdown");

    ctx.init->shutdown_all();

    TEST_CHECK(ctx.init->active_service_count() == 0, "0 active services after shutdown_all");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 212.17 Init Coordinator Tests ===" << std::endl;
    test_spawn_and_supervision();
    test_orphan_adoption_and_reaping();
    test_service_crash_detection();
    test_graceful_shutdown();

    if (g_failures == 0) {
        std::cout << "[PASS] Stage 212.17: All Init Coordinator tests passed." << std::endl;
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 212.17: " << g_failures << " tests failed." << std::endl;
        return 1;
    }
}

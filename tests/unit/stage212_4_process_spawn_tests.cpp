#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "kernel/bsd_process.h"
#include "kernel/process_spawner.h"
#include "kernel/thread.h"
#include "loader/macho_user_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"

using namespace winios::kernel;
using namespace winios::loader;
using namespace winios::memory;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_successful_spawn() {
    std::cout << "[Test 1] Successful Process Spawn with Independent Address Space...\n";

    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* init_proc = proc_mgr.create_process("init", 0); // PID 1

    KernelThreadManager thread_mgr;

    const auto binary = MachoUserLoader::get_embedded_first_app();
    TEST_CHECK(!binary.empty(), "Embedded first app binary retrieved");

    SpawnConfig cfg;
    cfg.process_name = "spawned_child";
    cfg.parent_pid = 1;
    cfg.argv = {"spawned_child", "arg1", "arg2"};
    cfg.envp = {"PATH=/bin", "USER=guest"};

    auto res = ProcessSpawner::spawn(binary, cfg, proc_mgr, thread_mgr, ppm, mem);
    TEST_CHECK(res.success, "Process spawned successfully");
    TEST_CHECK(res.pid == 2, "Spawned process allocated PID 2");
    TEST_CHECK(res.process != nullptr, "Process pointer valid");
    TEST_CHECK(res.process->ppid() == 1, "Parent PID correctly set to 1");
    TEST_CHECK(init_proc->children().size() == 1 && init_proc->children()[0] == 2, "Parent tracks spawned child");

    TEST_CHECK(res.address_space != nullptr, "Independent UserAddressSpace allocated");
    TEST_CHECK(res.address_space->root_table_pa() != 0, "Root L2 table PA is non-zero");

    TEST_CHECK(res.main_thread != nullptr, "Main thread created");
    TEST_CHECK(res.main_thread->tcb().pc != 0, "Entry point PC is initialized");
    TEST_CHECK(res.main_thread->tcb().sp_el0 != 0, "SP_EL0 is initialized");
    TEST_CHECK(res.main_thread->tcb().gpr[0] == 3, "Argc is 3");

    // Verify independent FileDescriptorTable
    TEST_CHECK(res.process->fd_table().open_count() == 3, "Child has independent FDs (0, 1, 2)");
    TEST_CHECK(res.process->fd_table().is_valid_fd(0), "Stdin open");
    TEST_CHECK(res.process->fd_table().is_valid_fd(1), "Stdout open");

    // Clean up
    TEST_CHECK(ProcessSpawner::destroy_process(res.pid, res, proc_mgr, thread_mgr), "Process destroyed cleanly");
    TEST_CHECK(proc_mgr.get_process(2) == nullptr, "PID 2 removed from process manager");
    TEST_CHECK(init_proc->children().empty(), "Parent children list cleared");
}

void test_spawn_failure_rollback() {
    std::cout << "[Test 2] Spawn Failure Rollback (Zero Leaks)...\n";

    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    proc_mgr.create_process("init", 0);

    KernelThreadManager thread_mgr;

    const std::uint64_t initial_free_pages = ppm.get_stats().free_pages;

    // Attempt spawn with corrupted Mach-O
    std::vector<std::uint8_t> corrupt_binary = {0xFE, 0xED, 0xFA, 0xCF, 0x01, 0x00, 0x00, 0x00}; // truncated
    SpawnConfig cfg;
    cfg.process_name = "corrupt_child";
    cfg.parent_pid = 1;

    auto res = ProcessSpawner::spawn(corrupt_binary, cfg, proc_mgr, thread_mgr, ppm, mem);
    TEST_CHECK(!res.success, "Spawn of corrupted binary rejected");
    TEST_CHECK(!res.error_message.empty(), "Informative error message produced");

    // Verify zero page leaks
    const std::uint64_t final_free_pages = ppm.get_stats().free_pages;
    TEST_CHECK(final_free_pages == initial_free_pages, "Free page count exactly preserved (no memory leaked)");

    // Verify no process leaked
    TEST_CHECK(proc_mgr.process_count() == 2, "Process count unchanged (PID 0 and PID 1 only)");
}

void test_multiple_independent_spawns() {
    std::cout << "[Test 3] Multiple Concurrent Independent Spawns...\n";

    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* init_proc = proc_mgr.create_process("init", 0);

    KernelThreadManager thread_mgr;
    const auto binary = MachoUserLoader::get_embedded_first_app();

    SpawnConfig cfg1;
    cfg1.process_name = "child_alpha";
    cfg1.parent_pid = 1;
    auto res1 = ProcessSpawner::spawn(binary, cfg1, proc_mgr, thread_mgr, ppm, mem);

    SpawnConfig cfg2;
    cfg2.process_name = "child_beta";
    cfg2.parent_pid = 1;
    auto res2 = ProcessSpawner::spawn(binary, cfg2, proc_mgr, thread_mgr, ppm, mem);

    TEST_CHECK(res1.success && res2.success, "Both child processes spawned");
    TEST_CHECK(res1.pid != res2.pid, "Processes have distinct PIDs");
    TEST_CHECK(res1.address_space->root_table_pa() != res2.address_space->root_table_pa(),
               "Processes have distinct root L2 page tables");
    TEST_CHECK(init_proc->children().size() == 2, "Parent tracks both children");

    // Clean up
    ProcessSpawner::destroy_process(res1.pid, res1, proc_mgr, thread_mgr);
    ProcessSpawner::destroy_process(res2.pid, res2, proc_mgr, thread_mgr);
    TEST_CHECK(init_proc->children().empty(), "All children reaped and unlinked");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 212.4 Process Spawn Tests ===\n";
    test_successful_spawn();
    test_spawn_failure_rollback();
    test_multiple_independent_spawns();

    if (g_failures == 0) {
        std::cout << "[PASS] Stage 212.4: All process spawn tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 212.4: " << g_failures << " tests failed.\n";
        return 1;
    }
}

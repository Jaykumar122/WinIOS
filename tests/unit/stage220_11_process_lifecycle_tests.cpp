// ============================================================================
// Stage 220.11 Unit Tests: Process Lifecycle, Hierarchy, Reaping & Isolation
// ============================================================================

#include <iostream>
#include <vector>
#include <cstdint>
#include <cassert>
#include <memory>
#include <algorithm>

#include "cpu/cpu.h"
#include "kernel/thread.h"
#include "kernel/bsd_process.h"
#include "kernel/multi_process_scheduler.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/virtual_page_table.h"
#include "memory/user_address_space.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;

static void test_process_hierarchy_and_reparenting() {
    std::cout << "[Test 1] Process hierarchy creation, orphan reparenting to PID 1, and wait4 reaping...\n";
    BsdProcessManager pm;
    TEST_ASSERT(pm.initialize()); // Creates PID 0 (kernel_task)

    // Create PID 1 (launchd / init)
    BsdProcess* init_proc = pm.create_process("launchd", 0);
    TEST_ASSERT(init_proc != nullptr);
    TEST_ASSERT(init_proc->pid() == 1);
    TEST_ASSERT(init_proc->ppid() == 0);

    // Create Parent (PID 2, PPID 1)
    BsdProcess* parent = pm.create_process("parent_service", 1);
    TEST_ASSERT(parent != nullptr);
    TEST_ASSERT(parent->pid() == 2);
    TEST_ASSERT(parent->ppid() == 1);
    TEST_ASSERT(std::find(init_proc->children().begin(), init_proc->children().end(), 2) != init_proc->children().end());

    // Create Children of PID 2 (PID 3 and PID 4)
    BsdProcess* child1 = pm.create_process("worker_1", 2);
    BsdProcess* child2 = pm.create_process("worker_2", 2);
    TEST_ASSERT(child1 != nullptr && child1->pid() == 3);
    TEST_ASSERT(child2 != nullptr && child2->pid() == 4);
    TEST_ASSERT(parent->children().size() == 2);
    TEST_ASSERT(parent->children()[0] == 3);
    TEST_ASSERT(parent->children()[1] == 4);

    // Allocate some file descriptors and threads in parent (starts with 3 standard fds 0, 1, 2)
    int fd3 = parent->fd_table().allocate_fd(FileType::Regular, true, true, "/tmp/p.txt");
    int fd4 = parent->fd_table().allocate_fd(FileType::Console, true, true, "");
    TEST_ASSERT(fd3 == 3 && fd4 == 4);
    TEST_ASSERT(parent->fd_table().open_count() == 5);

    KernelThreadManager th_mgr;
    KernelThread* pt1 = th_mgr.create_thread("pth1", 0x1000, 0, 0x80000, 0x10000, 16, true);
    KernelThread* pt2 = th_mgr.create_thread("pth2", 0x2000, 0, 0x70000, 0x10000, 16, true);
    pt1->set_owner_pid(2);
    pt2->set_owner_pid(2);
    parent->add_thread(pt1);
    parent->add_thread(pt2);
    TEST_ASSERT(parent->thread_count() == 2);

    // Terminate parent (PID 2) with exit code 15
    TEST_ASSERT(pm.terminate_process(2, 15));
    TEST_ASSERT(parent->is_terminated());
    TEST_ASSERT(parent->exit_code() == 15);
    TEST_ASSERT(parent->thread_count() == 2);
    TEST_ASSERT(pt1->state() == ThreadState::Terminated);
    TEST_ASSERT(pt2->state() == ThreadState::Terminated);
    TEST_ASSERT(parent->fd_table().open_count() == 0); // All FDs closed on termination

    // Verify orphan reparenting: PID 3 and PID 4 must now have PPID = 1
    TEST_ASSERT(child1->ppid() == 1);
    TEST_ASSERT(child2->ppid() == 1);
    TEST_ASSERT(parent->children().empty());

    // Init process (PID 1) must now hold PID 3 and PID 4 in its children list
    const auto& init_children = init_proc->children();
    TEST_ASSERT(std::find(init_children.begin(), init_children.end(), 3) != init_children.end());
    TEST_ASSERT(std::find(init_children.begin(), init_children.end(), 4) != init_children.end());

    // Wait and reap parent (PID 2) from init (PPID 1)
    int status = 0;
    int reaped_pid = pm.wait_process(1, 2, &status, 0);
    TEST_ASSERT(reaped_pid == 2);
    TEST_ASSERT(((status >> 8) & 0xFF) == 15);

    // PID 2 is reaped and destroyed
    TEST_ASSERT(pm.get_process(2) == nullptr);
    TEST_ASSERT(std::find(init_proc->children().begin(), init_proc->children().end(), 2) == init_proc->children().end());

    std::cout << "  Passed hierarchy, reparenting, and wait4 reaping tests.\n";
}

static void test_process_memory_and_resource_isolation() {
    std::cout << "[Test 2] Per-process address space, descriptor, and resource isolation...\n";
    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());

    BsdProcessManager pm;
    TEST_ASSERT(pm.initialize());

    const uint64_t root_a = ppm.allocate_page();
    UserAddressSpace uas_a(mem, ppm, root_a, "proc_a");
    uas_a.set_asid(10);
    uas_a.set_owner_pid(100);

    const uint64_t root_b = ppm.allocate_page();
    UserAddressSpace uas_b(mem, ppm, root_b, "proc_b");
    uas_b.set_asid(20);
    uas_b.set_owner_pid(200);

    BsdProcess* proc_a = pm.create_process("proc_a", 1, nullptr, &uas_a);
    BsdProcess* proc_b = pm.create_process("proc_b", 1, nullptr, &uas_b);
    TEST_ASSERT(proc_a != nullptr && proc_b != nullptr);

    // Allocate same FD number in both processes
    int fd_a = proc_a->fd_table().allocate_fd(FileType::Regular, true, true, "/path/proc_a.log");
    int fd_b = proc_b->fd_table().allocate_fd(FileType::Regular, true, true, "/path/proc_b.log");
    TEST_ASSERT(fd_a == fd_b); // Same fd index (e.g. 0)
    TEST_ASSERT(proc_a->fd_table().get_fd(fd_a)->path == "/path/proc_a.log");
    TEST_ASSERT(proc_b->fd_table().get_fd(fd_b)->path == "/path/proc_b.log");

    // Close fd in proc_a: proc_b's fd must remain open and completely unaffected
    TEST_ASSERT(proc_a->fd_table().close_fd(fd_a));
    TEST_ASSERT(!proc_a->fd_table().is_valid_fd(fd_a));
    TEST_ASSERT(proc_b->fd_table().is_valid_fd(fd_b));
    TEST_ASSERT(proc_b->fd_table().get_fd(fd_b)->path == "/path/proc_b.log");

    // Terminate proc_a: proc_b remains runnable and intact
    TEST_ASSERT(pm.terminate_process(proc_a->pid(), 0));
    TEST_ASSERT(proc_a->is_terminated());
    TEST_ASSERT(!proc_b->is_terminated());
    TEST_ASSERT(proc_b->state() == ProcessState::Runnable);
    TEST_ASSERT(proc_b->address_space() == &uas_b);

    std::cout << "  Passed resource isolation tests.\n";
}

int main() {
    std::cout << "=== Stage 220.11 Process Lifecycle and Isolation Tests ===\n";
    test_process_hierarchy_and_reparenting();
    test_process_memory_and_resource_isolation();
    std::cout << "All Stage 220.11 tests passed successfully.\n";
    return 0;
}

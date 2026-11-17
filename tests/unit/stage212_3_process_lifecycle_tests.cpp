#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "kernel/bsd_process.h"

using namespace winios::kernel;

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

void test_parent_child_tracking() {
    std::cout << "[Test 1] Process Hierarchy & Parent/Child Tracking...\n";

    BsdProcessManager mgr;
    TEST_CHECK(mgr.initialize(), "Process manager initialized");

    auto* init_proc = mgr.create_process("init", 0); // PID 1
    TEST_CHECK(init_proc != nullptr && init_proc->pid() == 1, "PID 1 (init) created");

    auto* child1 = mgr.create_process("worker_1", 1); // PID 2
    auto* child2 = mgr.create_process("worker_2", 1); // PID 3
    TEST_CHECK(child1 != nullptr && child1->pid() == 2, "PID 2 created");
    TEST_CHECK(child2 != nullptr && child2->pid() == 3, "PID 3 created");

    const auto& init_children = init_proc->children();
    TEST_CHECK(init_children.size() == 2, "PID 1 tracks 2 children");
    if (init_children.size() >= 2) {
        TEST_CHECK(init_children[0] == 2 && init_children[1] == 3, "Children PIDs are 2 and 3");
    }

    TEST_CHECK(child1->ppid() == 1, "Child 1 ppid is 1");
    TEST_CHECK(child2->ppid() == 1, "Child 2 ppid is 1");
}

void test_orphan_reparenting_to_init() {
    std::cout << "[Test 2] Orphan Reparenting to PID 1 upon Parent Termination...\n";

    BsdProcessManager mgr;
    mgr.initialize();

    auto* init_proc = mgr.create_process("init", 0); // PID 1
    auto* parent = mgr.create_process("parent_service", 1); // PID 2
    auto* orphan1 = mgr.create_process("child_task_1", 2);  // PID 3 (child of PID 2)
    auto* orphan2 = mgr.create_process("child_task_2", 2);  // PID 4 (child of PID 2)

    TEST_CHECK(parent->children().size() == 2, "Parent PID 2 has 2 children");
    TEST_CHECK(orphan1->ppid() == 2, "Orphan 1 ppid is 2");
    TEST_CHECK(orphan2->ppid() == 2, "Orphan 2 ppid is 2");

    // Terminate parent PID 2
    TEST_CHECK(mgr.terminate_process(2, 0), "Terminate PID 2");
    TEST_CHECK(parent->is_terminated(), "PID 2 is Zombie");
    TEST_CHECK(parent->children().empty(), "Terminated parent has no children (reparented)");

    // Verify orphans are now parented by PID 1
    TEST_CHECK(orphan1->ppid() == 1, "Orphan 1 reparented to PID 1");
    TEST_CHECK(orphan2->ppid() == 1, "Orphan 2 reparented to PID 1");

    const auto& init_children = init_proc->children();
    bool has_orphan1 = false;
    bool has_orphan2 = false;
    for (pid_t c : init_children) {
        if (c == 3) has_orphan1 = true;
        if (c == 4) has_orphan2 = true;
    }
    TEST_CHECK(has_orphan1, "PID 1 now tracks orphan 1 (PID 3)");
    TEST_CHECK(has_orphan2, "PID 1 now tracks orphan 2 (PID 4)");
}

void test_process_reaping_and_cleanup() {
    std::cout << "[Test 3] Process Termination, Notification, and Reaping...\n";

    BsdProcessManager mgr;
    mgr.initialize();

    auto* init_proc = mgr.create_process("init", 0); // PID 1
    auto* worker = mgr.create_process("worker", 1);   // PID 2

    TEST_CHECK(!worker->is_parent_notified(), "Parent not notified before termination");

    TEST_CHECK(mgr.terminate_process(2, 77), "Terminate PID 2 with code 77");
    TEST_CHECK(worker->is_terminated(), "Worker is Zombie");
    TEST_CHECK(worker->exit_code() == 77, "Exit code is 77");
    TEST_CHECK(worker->is_parent_notified(), "Parent notified upon worker termination");

    // Cannot reap active PID 1
    TEST_CHECK(!mgr.reap_process(1), "Reaping active PID 1 fails");

    // Reap zombie PID 2
    TEST_CHECK(mgr.reap_process(2), "Reap zombie PID 2 succeeds");
    TEST_CHECK(mgr.get_process(2) == nullptr, "PID 2 no longer exists");

    // Confirm PID 2 unlinked from PID 1's children
    const auto& children = init_proc->children();
    bool found_p2 = false;
    for (pid_t c : children) {
        if (c == 2) found_p2 = true;
    }
    TEST_CHECK(!found_p2, "PID 2 unlinked from PID 1 children list");
}

void test_process_hierarchy_serialization() {
    std::cout << "[Test 4] Process Hierarchy Serialization / Deserialization...\n";

    BsdProcessManager mgr;
    mgr.initialize();
    mgr.create_process("init", 0); // PID 1
    mgr.create_process("sub1", 1); // PID 2
    mgr.create_process("sub2", 1); // PID 3

    std::vector<std::uint8_t> blob;
    mgr.serialize(blob);
    TEST_CHECK(!blob.empty(), "Serialized blob is non-empty");

    BsdProcessManager restored_mgr;
    TEST_CHECK(restored_mgr.deserialize(blob.data(), blob.size()), "Deserialization succeeded");
    TEST_CHECK(restored_mgr.process_count() == 4, "Restored 4 processes (PID 0, 1, 2, 3)");

    const auto* p1 = restored_mgr.get_process(1);
    TEST_CHECK(p1 != nullptr, "Restored PID 1 exists");
    TEST_CHECK(p1->children().size() == 2, "Restored PID 1 tracks 2 children");
    TEST_CHECK(p1->children()[0] == 2 && p1->children()[1] == 3, "Children PIDs are 2 and 3");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 212.3 Process Lifecycle Tests ===\n";
    test_parent_child_tracking();
    test_orphan_reparenting_to_init();
    test_process_reaping_and_cleanup();
    test_process_hierarchy_serialization();

    if (g_failures == 0) {
        std::cout << "[PASS] Stage 212.3: All process lifecycle tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 212.3: " << g_failures << " tests failed.\n";
        return 1;
    }
}

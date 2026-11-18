#include "settings/task_manager.h"
#include <cassert>
#include <iostream>

using namespace winios::settings;

static void test_task_registry_and_enumeration() {
    TaskManager tm;

    assert(tm.get_task_count() == 0);

    TaskInfo t1{};
    t1.pid = 101;
    t1.bundle_id = "com.winios.notes";
    t1.name = "Notes";
    t1.primary_window = 1;
    t1.state = TaskState::Running;
    t1.memory_bytes = 1024 * 1024;
    t1.is_foreground = true;

    TaskInfo t2{};
    t2.pid = 102;
    t2.bundle_id = "com.winios.settings";
    t2.name = "Settings";
    t2.primary_window = 2;
    t2.state = TaskState::Running;
    t2.memory_bytes = 2048 * 1024;
    t2.is_foreground = false;

    assert(tm.register_task(t1));
    assert(tm.register_task(t2));
    assert(tm.get_task_count() == 2);

    auto list = tm.enumerate_tasks();
    assert(list.size() == 2);
    assert(list[0].pid == 101);
    assert(list[1].pid == 102);

    TaskInfo queried{};
    assert(tm.get_task(102, queried));
    assert(queried.name == "Settings");

    assert(tm.update_task_state(101, TaskState::Suspended));
    assert(tm.get_task(101, queried));
    assert(queried.state == TaskState::Suspended);

    std::cout << "[PASS] test_task_registry_and_enumeration\n";
}

static void test_task_switcher_cycling() {
    TaskManager tm;

    TaskInfo t1{};
    t1.pid = 10;
    t1.name = "App1";
    TaskInfo t2{};
    t2.pid = 20;
    t2.name = "App2";
    TaskInfo t3{};
    t3.pid = 30;
    t3.name = "App3";

    tm.register_task(t1);
    tm.register_task(t2);
    tm.register_task(t3);

    assert(!tm.is_switcher_visible());
    tm.show_switcher();
    assert(tm.is_switcher_visible());

    assert(tm.get_selected_index() == 0);
    assert(tm.get_selected_pid() == 10);

    tm.cycle_next();
    assert(tm.get_selected_index() == 1);
    assert(tm.get_selected_pid() == 20);

    tm.cycle_next();
    assert(tm.get_selected_index() == 2);
    assert(tm.get_selected_pid() == 30);

    tm.cycle_next();
    assert(tm.get_selected_index() == 0); // Wrapped around

    tm.cycle_prev();
    assert(tm.get_selected_index() == 2); // Wrapped backwards

    // Activate selected
    assert(tm.activate_selected_task());
    assert(!tm.is_switcher_visible()); // Auto-hid switcher
    TaskInfo active{};
    assert(tm.get_task(30, active));
    assert(active.is_foreground);

    std::cout << "[PASS] test_task_switcher_cycling\n";
}

static void test_task_lifecycle_controls() {
    TaskManager tm;
    bool handler_called = false;
    uint32_t handled_pid = 0;
    bool handled_graceful = false;

    tm.set_lifecycle_handler([&](uint32_t pid, bool graceful) {
        handler_called = true;
        handled_pid = pid;
        handled_graceful = graceful;
        return true;
    });

    TaskInfo t1{};
    t1.pid = 50;
    t1.name = "Worker";
    tm.register_task(t1);

    // Graceful terminate
    assert(tm.terminate_task(50, true));
    assert(handler_called);
    assert(handled_pid == 50);
    assert(handled_graceful == true);

    TaskInfo cur{};
    assert(tm.get_task(50, cur));
    assert(cur.state == TaskState::Suspended);

    // Force kill
    handler_called = false;
    assert(tm.force_kill_task(50));
    assert(handler_called);
    assert(!handled_graceful);
    assert(!tm.get_task(50, cur)); // Removed from task manager

    // Non-existent task
    assert(!tm.terminate_task(999, true));

    std::cout << "[PASS] test_task_lifecycle_controls\n";
}

int main() {
    std::cout << "--- Stage 228.14 Task Manager Tests ---\n";
    test_task_registry_and_enumeration();
    test_task_switcher_cycling();
    test_task_lifecycle_controls();
    std::cout << "All Stage 228.14 Task Manager Tests PASSED.\n";
    return 0;
}

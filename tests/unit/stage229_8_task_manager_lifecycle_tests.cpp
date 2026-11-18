#include "settings/task_manager.h"
#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

#include <iostream>
#include <vector>

using namespace winios::settings;

struct ReclaimedResource {
    uint32_t pid{0};
    uint64_t window{0};
};

static void test_task_registration_and_generation() {
    TaskManager tm;
    TaskInfo t1{};
    t1.pid = 101;
    t1.bundle_id = "com.winios.app1";
    t1.name = "App1";
    t1.primary_window = 0x5001;

    TEST_ASSERT(tm.register_task(t1));
    uint32_t gen1 = tm.get_task_generation(101);
    TEST_ASSERT(gen1 > 0);

    // Re-registering existing PID preserves generation
    t1.name = "App1_Updated";
    TEST_ASSERT(tm.register_task(t1));
    TEST_ASSERT(tm.get_task_generation(101) == gen1);

    // Unregister and register new task with same PID -> gets new generation
    TEST_ASSERT(tm.unregister_task(101));
    TEST_ASSERT(tm.register_task(t1));
    uint32_t gen2 = tm.get_task_generation(101);
    TEST_ASSERT(gen2 > gen1);

    std::cout << "[PASS] test_task_registration_and_generation\n";
}

static void test_graceful_and_force_termination_with_reclamation() {
    TaskManager tm;
    std::vector<ReclaimedResource> reclaimed;
    tm.register_cleanup_callback([&](uint32_t pid, uint64_t win) {
        reclaimed.push_back({pid, win});
    });

    TaskInfo t1{};
    t1.pid = 201;
    t1.bundle_id = "com.winios.app201";
    t1.name = "App201";
    t1.primary_window = 0x9001;
    TEST_ASSERT(tm.register_task(t1));

    // 1. Graceful termination: transitions to Suspended, invokes cleanup
    TEST_ASSERT(tm.terminate_task(201, /*graceful=*/true));
    TEST_ASSERT(reclaimed.size() == 1);
    TEST_ASSERT(reclaimed[0].pid == 201);
    TEST_ASSERT(reclaimed[0].window == 0x9001);

    TaskInfo out{};
    TEST_ASSERT(tm.get_task(201, out));
    TEST_ASSERT(out.state == TaskState::Suspended);

    // 2. Duplicate graceful termination is safe and idempotent
    TEST_ASSERT(tm.terminate_task(201, /*graceful=*/true));

    // 3. Force kill: transitions to Zombie and erases from active tasks
    TEST_ASSERT(tm.force_kill_task(201));
    TEST_ASSERT(!tm.get_task(201, out)); // Erased

    std::cout << "[PASS] test_graceful_and_force_termination_with_reclamation\n";
}

static void test_stale_generation_rejection() {
    TaskManager tm;
    TaskInfo t1{};
    t1.pid = 301;
    t1.name = "App301";
    TEST_ASSERT(tm.register_task(t1));

    uint32_t real_gen = tm.get_task_generation(301);
    TEST_ASSERT(real_gen > 0);

    // Attempt termination with stale generation (e.g. real_gen - 1)
    bool ok = tm.terminate_task_versioned(301, real_gen - 1, /*graceful=*/true);
    TEST_ASSERT(!ok); // Stale handle rejected!

    TaskInfo check{};
    TEST_ASSERT(tm.get_task(301, check));
    TEST_ASSERT(check.state == TaskState::Running); // Remained alive

    // Attempt termination with matching generation -> succeeds
    ok = tm.terminate_task_versioned(301, real_gen, /*graceful=*/true);
    TEST_ASSERT(ok);
    TEST_ASSERT(tm.get_task(301, check));
    TEST_ASSERT(check.state == TaskState::Suspended);

    std::cout << "[PASS] test_stale_generation_rejection\n";
}

static void test_task_switcher_cycle_and_activation() {
    TaskManager tm;
    TaskInfo t1{}; t1.pid = 401; t1.name = "App1";
    TaskInfo t2{}; t2.pid = 402; t2.name = "App2";
    TaskInfo t3{}; t3.pid = 403; t3.name = "App3";

    tm.register_task(t1);
    tm.register_task(t2);
    tm.register_task(t3);

    tm.show_switcher();
    TEST_ASSERT(tm.is_switcher_visible());

    // Cycle circularly next: 0 -> 1 -> 2 -> 0
    TEST_ASSERT(tm.get_selected_index() == 0);
    tm.cycle_next();
    TEST_ASSERT(tm.get_selected_index() == 1);
    TEST_ASSERT(tm.get_selected_pid() == 402);
    tm.cycle_next();
    TEST_ASSERT(tm.get_selected_index() == 2);
    TEST_ASSERT(tm.get_selected_pid() == 403);
    tm.cycle_next();
    TEST_ASSERT(tm.get_selected_index() == 0);

    // Cycle prev: 0 -> 2
    tm.cycle_prev();
    TEST_ASSERT(tm.get_selected_index() == 2);

    // Activate selected (403)
    TEST_ASSERT(tm.activate_selected_task());
    TEST_ASSERT(!tm.is_switcher_visible()); // Switcher dismissed

    TaskInfo check{};
    TEST_ASSERT(tm.get_task(403, check));
    TEST_ASSERT(check.is_foreground);

    std::cout << "[PASS] test_task_switcher_cycle_and_activation\n";
}

int main() {
    std::cout << "=== Running Stage 229 TaskManager Lifecycle Tests ===\n";
    test_task_registration_and_generation();
    test_graceful_and_force_termination_with_reclamation();
    test_stale_generation_rejection();
    test_task_switcher_cycle_and_activation();
    std::cout << "All Stage 229 TaskManager Lifecycle Tests Passed!\n";
    return 0;
}

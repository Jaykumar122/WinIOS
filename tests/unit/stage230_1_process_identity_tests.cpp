#include "kernel/process_identity.h"
#include <iostream>
#include <thread>
#include <vector>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::kernel;

static void test_basic_allocation_and_identity() {
    ProcessIdentityManager mgr(100, 200, 4);

    ApplicationIdentity app1{"com.winios.testapp", "1.0.0", "Test App", 0x00020300};
    ProcessHandle h1 = mgr.allocate_process(app1, 1000);

    TEST_ASSERT(h1.is_valid());
    TEST_ASSERT(h1.pid == 100);
    TEST_ASSERT(h1.generation == 1);
    TEST_ASSERT(mgr.is_valid_handle(h1));
    TEST_ASSERT(mgr.is_valid_pid(100));

    auto inst1 = mgr.get_instance(h1);
    TEST_ASSERT(inst1.has_value());
    TEST_ASSERT(inst1->instance_id == 1);
    TEST_ASSERT(inst1->app.bundle_id == "com.winios.testapp");
    TEST_ASSERT(inst1->start_time_us == 1000);
    TEST_ASSERT(inst1->active);

    ApplicationIdentity app2{"com.winios.secondapp", "2.0.0", "Second App", 0x00020300};
    ProcessHandle h2 = mgr.allocate_process(app2, 2000);
    TEST_ASSERT(h2.pid == 101);
    TEST_ASSERT(h2.generation == 1);
    TEST_ASSERT(mgr.active_process_count() == 2);

    auto inst2 = mgr.get_instance(h2);
    TEST_ASSERT(inst2.has_value());
    TEST_ASSERT(inst2->instance_id == 2);
    TEST_ASSERT(inst2->app.bundle_id == "com.winios.secondapp");

    std::cout << "[PASS] test_basic_allocation_and_identity\n";
}

static void test_stale_handle_rejection() {
    ProcessIdentityManager mgr(100, 105, 2);

    ApplicationIdentity app{"com.winios.test", "1.0", "Test", 0x00020300};
    ProcessHandle h1 = mgr.allocate_process(app, 1000);
    TEST_ASSERT(h1.pid == 100);
    TEST_ASSERT(h1.generation == 1);

    // Save copy of handle
    ProcessHandle stale_h1 = h1;

    // Terminate process
    bool term_ok = mgr.terminate_process(h1);
    TEST_ASSERT(term_ok);
    TEST_ASSERT(!mgr.is_valid_handle(stale_h1));
    TEST_ASSERT(!mgr.is_valid_pid(100));

    // Attempt double termination
    TEST_ASSERT(!mgr.terminate_process(stale_h1));

    std::cout << "[PASS] test_stale_handle_rejection\n";
}

static void test_pid_reuse_and_generation_counter() {
    // Range of 2 PIDs (100 and 101) with quarantine depth 1
    ProcessIdentityManager mgr(100, 101, 1);

    ApplicationIdentity app{"com.winios.test", "1.0", "Test", 0x00020300};
    ProcessHandle h1 = mgr.allocate_process(app);
    TEST_ASSERT(h1.pid == 100);
    TEST_ASSERT(h1.generation == 1);

    ProcessHandle h2 = mgr.allocate_process(app);
    TEST_ASSERT(h2.pid == 101);
    TEST_ASSERT(h2.generation == 1);

    // Terminate 100 -> goes into quarantine
    TEST_ASSERT(mgr.terminate_process(h1));

    // Next allocation will recycle 100 because candidate wraps and pool is small
    ProcessHandle h3 = mgr.allocate_process(app);
    TEST_ASSERT(h3.pid == 100);
    TEST_ASSERT(h3.generation == 2); // Generation incremented!

    // The stale handle h1 (pid=100, gen=1) MUST NOT refer to active process h3 (pid=100, gen=2)!
    TEST_ASSERT(!mgr.is_valid_handle(h1));
    TEST_ASSERT(mgr.is_valid_handle(h3));

    // Terminating via stale handle must fail
    TEST_ASSERT(!mgr.terminate_process(h1));
    TEST_ASSERT(mgr.is_valid_handle(h3));

    // Versioned termination with stale generation must fail
    TEST_ASSERT(!mgr.terminate_process_by_pid(100, 1));
    TEST_ASSERT(mgr.terminate_process_by_pid(100, 2));

    std::cout << "[PASS] test_pid_reuse_and_generation_counter\n";
}

static void test_quarantine_delay_queue() {
    // 5 PIDs (100..104), quarantine depth 3
    ProcessIdentityManager mgr(100, 104, 3);
    ApplicationIdentity app{"com.winios.test", "1.0", "Test", 0x00020300};

    ProcessHandle h1 = mgr.allocate_process(app); // 100
    ProcessHandle h2 = mgr.allocate_process(app); // 101
    TEST_ASSERT(h1.pid == 100);
    TEST_ASSERT(h2.pid == 101);

    // Terminate 100
    mgr.terminate_process(h1);

    // Next allocations must prefer unused PIDs (102, 103, 104) over quarantined 100
    ProcessHandle h3 = mgr.allocate_process(app); // 102
    ProcessHandle h4 = mgr.allocate_process(app); // 103
    ProcessHandle h5 = mgr.allocate_process(app); // 104
    TEST_ASSERT(h3.pid == 102);
    TEST_ASSERT(h4.pid == 103);
    TEST_ASSERT(h5.pid == 104);

    std::cout << "[PASS] test_quarantine_delay_queue\n";
}

static void test_fixed_pid_allocation() {
    ProcessIdentityManager mgr(100, 200, 4);

    ApplicationIdentity sys_app{"com.winios.settings", "1.0", "Settings", 0x00020300};
    ProcessHandle h97 = mgr.allocate_fixed_pid(97, sys_app, 500);
    TEST_ASSERT(h97.is_valid());
    TEST_ASSERT(h97.pid == 97);
    TEST_ASSERT(mgr.is_valid_pid(97));

    // Cannot reallocate same fixed PID while active
    ProcessHandle h97_dup = mgr.allocate_fixed_pid(97, sys_app);
    TEST_ASSERT(!h97_dup.is_valid());

    TEST_ASSERT(mgr.terminate_process(h97));
    TEST_ASSERT(!mgr.is_valid_pid(97));

    std::cout << "[PASS] test_fixed_pid_allocation\n";
}

static void test_concurrent_allocation_termination() {
    ProcessIdentityManager mgr(100, 150, 8);
    ApplicationIdentity app{"com.winios.concurrent", "1.0", "Concurrent", 0x00020300};

    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([&mgr, app]() {
            for (int j = 0; j < 50; ++j) {
                ProcessHandle h = mgr.allocate_process(app);
                if (h.is_valid()) {
                    TEST_ASSERT(mgr.is_valid_handle(h));
                    mgr.terminate_process(h);
                }
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    TEST_ASSERT(mgr.active_process_count() == 0);
    std::cout << "[PASS] test_concurrent_allocation_termination\n";
}

int main() {
    std::cout << "=== Running Stage 230.1 Process Identity Tests ===" << std::endl;
    test_basic_allocation_and_identity();
    test_stale_handle_rejection();
    test_pid_reuse_and_generation_counter();
    test_quarantine_delay_queue();
    test_fixed_pid_allocation();
    test_concurrent_allocation_termination();
    std::cout << "=== All Stage 230.1 Tests Passed Successfully ===" << std::endl;
    return 0;
}

#include "tests/support/test_assert.h"
#include "platform/resource_budget.h"

#include <iostream>

using namespace winios::platform;

namespace {

void test_default_budget_enforcement() {
    std::cout << "[Test 1] Default Resource Budget Limits...\n";
    ResourceBudgetManager mgr;

    uint32_t pid = 100;
    // Memory limit: default is 16 MB
    TEST_ASSERT(mgr.allocate_memory(pid, 10 * 1024 * 1024).is_ok());
    TEST_ASSERT(mgr.get_task_memory(pid) == 10 * 1024 * 1024);

    // Allocating additional 7 MB exceeds 16 MB -> Error
    auto err = mgr.allocate_memory(pid, 7 * 1024 * 1024);
    TEST_ASSERT(!err.is_ok());
    TEST_ASSERT(mgr.get_task_memory(pid) == 10 * 1024 * 1024);

    // Free 5 MB
    mgr.free_memory(pid, 5 * 1024 * 1024);
    TEST_ASSERT(mgr.get_task_memory(pid) == 5 * 1024 * 1024);

    // Now 7 MB allocation fits (5 + 7 = 12 MB <= 16 MB)
    TEST_ASSERT(mgr.allocate_memory(pid, 7 * 1024 * 1024).is_ok());
    TEST_ASSERT(mgr.get_task_memory(pid) == 12 * 1024 * 1024);

    std::cout << "  -> Passed default budget enforcement\n";
}

void test_custom_app_budget_policy() {
    std::cout << "[Test 2] Custom Per-App Budget Policy...\n";
    ResourceBudgetManager mgr;

    uint32_t pid_low = 101;
    AppResourceBudgetConfig low_cfg{};
    low_cfg.max_memory_bytes = 2 * 1024 * 1024; // 2 MB limit
    low_cfg.max_windows = 2;
    low_cfg.max_document_streams = 2;
    mgr.set_task_budget(pid_low, low_cfg);

    // Memory limit: 2 MB
    TEST_ASSERT(mgr.allocate_memory(pid_low, 1 * 1024 * 1024).is_ok());
    TEST_ASSERT(!mgr.allocate_memory(pid_low, 2 * 1024 * 1024).is_ok());

    // Window limit: 2
    TEST_ASSERT(mgr.allocate_window(pid_low).is_ok());
    TEST_ASSERT(mgr.allocate_window(pid_low).is_ok());
    TEST_ASSERT(!mgr.allocate_window(pid_low).is_ok()); // Exceeds 2

    // Document stream limit: 2
    TEST_ASSERT(mgr.open_document_stream(pid_low).is_ok());
    TEST_ASSERT(mgr.open_document_stream(pid_low).is_ok());
    TEST_ASSERT(!mgr.open_document_stream(pid_low).is_ok()); // Exceeds 2

    // Task cleanup releases all budgets
    mgr.release_all_task_resources(pid_low);
    TEST_ASSERT(mgr.get_task_memory(pid_low) == 0);
    TEST_ASSERT(mgr.get_task_windows(pid_low) == 0);

    std::cout << "  -> Passed custom per-app budget policy\n";
}

} // namespace

int main() {
    std::cout << "========================================================\n";
    std::cout << " Stage 239.13: Application Resource Budget Suite       \n";
    std::cout << "========================================================\n";

    test_default_budget_enforcement();
    test_custom_app_budget_policy();

    std::cout << "\nALL STAGE 239.13 TESTS COMPLETED SUCCESSFULLY!\n";
    return 0;
}

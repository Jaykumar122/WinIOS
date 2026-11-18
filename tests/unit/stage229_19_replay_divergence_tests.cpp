#include "replay/replay_diagnostics.h"
#include "platform/resource_budget.h"

#include <cassert>
#include <iostream>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;

static void test_detects_exact_match() {
    replay::ReplayDivergenceChecker checker;

    for (uint64_t i = 1; i <= 5; ++i) {
        replay::ReplayEventRecord rec{i, i * 100, 1, 0x1234 + i, "Step " + std::to_string(i)};
        checker.add_recorded_event(rec);
        checker.add_replayed_event(rec);
    }

    auto div = checker.check_divergence();
    TEST_ASSERT(!div.has_diverged);
    TEST_ASSERT(div.reason == "Replay perfectly deterministic");
    std::cout << "[PASS] test_detects_exact_match\n";
}

static void test_detects_payload_hash_mismatch() {
    replay::ReplayDivergenceChecker checker;

    for (uint64_t i = 1; i <= 3; ++i) {
        replay::ReplayEventRecord rec{i, i * 100, 1, 0x1234 + i, "Step " + std::to_string(i)};
        checker.add_recorded_event(rec);
        checker.add_replayed_event(rec);
    }

    replay::ReplayEventRecord rec4_exp{4, 400, 1, 0xbeef, "Step 4 exp"};
    replay::ReplayEventRecord rec4_act{4, 400, 1, 0xdead, "Step 4 act"};
    checker.add_recorded_event(rec4_exp);
    checker.add_replayed_event(rec4_act);

    auto div = checker.check_divergence();
    TEST_ASSERT(div.has_diverged);
    TEST_ASSERT(div.divergence_step == 3);
    TEST_ASSERT(div.reason.find("Payload hash mismatch") != std::string::npos);
    std::cout << "[PASS] test_detects_payload_hash_mismatch\n";
}

static void test_detects_timeline_length_mismatch() {
    replay::ReplayDivergenceChecker checker;

    replay::ReplayEventRecord rec1{1, 100, 1, 0x1111, "Step 1"};
    checker.add_recorded_event(rec1);
    checker.add_replayed_event(rec1);

    replay::ReplayEventRecord rec2{2, 200, 1, 0x2222, "Step 2"};
    checker.add_recorded_event(rec2);

    auto div = checker.check_divergence();
    TEST_ASSERT(div.has_diverged);
    TEST_ASSERT(div.divergence_step == 1);
    TEST_ASSERT(div.reason.find("Timeline length mismatch") != std::string::npos);
    std::cout << "[PASS] test_detects_timeline_length_mismatch\n";
}

static void test_resource_budget() {
    platform::ResourceBudgetManager budget;

    auto err1 = budget.allocate_memory(10, 10 * 1024 * 1024);
    TEST_ASSERT(err1.is_ok());
    TEST_ASSERT(budget.get_task_memory(10) == 10 * 1024 * 1024);

    auto err2 = budget.allocate_memory(10, 7 * 1024 * 1024);
    TEST_ASSERT(!err2.is_ok());
    TEST_ASSERT(err2.category() == platform::PlatformErrorCategory::ResourceExhausted);

    budget.free_memory(10, 5 * 1024 * 1024);
    TEST_ASSERT(budget.get_task_memory(10) == 5 * 1024 * 1024);

    for (uint32_t w = 0; w < 8; ++w) {
        TEST_ASSERT(budget.allocate_window(10).is_ok());
    }
    TEST_ASSERT(budget.get_task_windows(10) == 8);

    auto err_win = budget.allocate_window(10);
    TEST_ASSERT(!err_win.is_ok());
    TEST_ASSERT(err_win.category() == platform::PlatformErrorCategory::ResourceExhausted);

    TEST_ASSERT(budget.validate_payload_size(1024).is_ok());
    TEST_ASSERT(!budget.validate_payload_size(64 * 1024).is_ok());

    budget.release_all_task_resources(10);
    TEST_ASSERT(budget.get_task_memory(10) == 0);
    TEST_ASSERT(budget.get_task_windows(10) == 0);
    std::cout << "[PASS] test_resource_budget\n";
}

int main() {
    std::cout << "=== Running Stage 229 Replay Divergence & Budget Tests ===\n";
    test_detects_exact_match();
    test_detects_payload_hash_mismatch();
    test_detects_timeline_length_mismatch();
    test_resource_budget();
    std::cout << "All Stage 229 Replay Divergence & Budget Tests Passed!\n";
    return 0;
}

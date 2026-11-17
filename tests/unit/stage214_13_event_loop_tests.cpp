// ============================================================================
// Stage 214.13: Unified UI Event Loop Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <vector>

#include "runtime/ui/event_loop.h"

using namespace winios::runtime::ui;

namespace {

int g_failures = 0;

#define LOOP_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[LOOP_FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [LOOP_OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_event_loop_queue_and_ordering() {
    std::cout << "[Stage 214.13] Verifying Event Loop Queue and Ordering...\n";

    UiEventLoop loop(4); // capacity 4
    LOOP_CHECK(loop.pending_event_count() == 0, "Loop starts empty");

    std::vector<int> execution_order;

    loop.post_task([&]() { execution_order.push_back(1); }, 100);
    loop.post_task([&]() { execution_order.push_back(2); }, 200);
    loop.post_task([&]() { execution_order.push_back(3); }, 300);
    loop.post_task([&]() { execution_order.push_back(4); }, 400);

    LOOP_CHECK(loop.pending_event_count() == 4, "Loop holds 4 events");

    // Attempting to post 5th event exceeds bounded queue capacity of 4
    bool fifth_posted = loop.post_task([&]() { execution_order.push_back(5); });
    LOOP_CHECK(!fifth_posted, "5th event rejected due to bounded capacity");
    LOOP_CHECK(loop.total_dropped_events() == 1, "Total dropped events == 1");

    // Process all events
    std::size_t processed = loop.run_until_idle();
    LOOP_CHECK(processed == 4, "Processed 4 events");
    LOOP_CHECK(loop.pending_event_count() == 0, "Loop queue drained to 0");

    LOOP_CHECK(execution_order.size() == 4, "4 tasks executed");
    LOOP_CHECK(execution_order[0] == 1 && execution_order[1] == 2 &&
               execution_order[2] == 3 && execution_order[3] == 4,
               "Deterministic FIFO task execution order verified");
}

void test_event_loop_shutdown() {
    std::cout << "[Stage 214.13] Verifying Event Loop Shutdown Request...\n";

    UiEventLoop loop(16);
    int task_counter = 0;

    loop.post_task([&]() { ++task_counter; });
    UiEvent shutdown_evt{};
    shutdown_evt.type = UiEventType::Shutdown;
    loop.post_event(shutdown_evt);
    loop.post_task([&]() { ++task_counter; }); // Should not run after shutdown

    loop.run_until_idle();
    LOOP_CHECK(!loop.is_running(), "Loop stopped running");
    LOOP_CHECK(task_counter == 1, "Only first task executed before shutdown");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 214.13: Event Loop Tests              \n";
    std::cout << "====================================================\n";

    test_event_loop_queue_and_ordering();
    test_event_loop_shutdown();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL EVENT LOOP TESTS PASSED" : "LOOP FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

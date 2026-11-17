// ============================================================================
// Stage 213.14: Touch Event Queue Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <vector>

#include "input/touch_event_queue.h"

using namespace winios::input;

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

void test_fifo_ordering_and_peek() {
    std::cout << "[Test 1] Verify FIFO Event Ordering and Non-Destructive Peeking...\n";

    TouchEventQueue q(16);
    TEST_CHECK(q.empty(), "New queue is empty");
    TEST_CHECK(q.size() == 0, "New queue size is 0");

    InputEvent e1{};
    e1.type = InputEventType::TouchBegin;
    e1.touch.contact_id = 0;
    e1.touch.x = 10;

    InputEvent e2{};
    e2.type = InputEventType::TouchMove;
    e2.touch.contact_id = 0;
    e2.touch.x = 20;

    InputEvent e3{};
    e3.type = InputEventType::TouchEnd;
    e3.touch.contact_id = 0;
    e3.touch.x = 30;

    TEST_CHECK(q.push(e1), "Push e1 succeeded");
    TEST_CHECK(q.push(e2), "Push e2 succeeded");
    TEST_CHECK(q.push(e3), "Push e3 succeeded");
    TEST_CHECK(q.size() == 3, "Queue size is 3");

    // Peek at front
    InputEvent peeked{};
    TEST_CHECK(q.peek(peeked), "peek succeeded");
    TEST_CHECK(peeked.type == InputEventType::TouchBegin && peeked.touch.x == 10, "peeked event is e1");
    TEST_CHECK(q.size() == 3, "Queue size remains 3 after peek");

    // Pop in FIFO order
    InputEvent out{};
    TEST_CHECK(q.pop(out) && out.type == InputEventType::TouchBegin && out.touch.x == 10, "First pop is e1");
    TEST_CHECK(q.pop(out) && out.type == InputEventType::TouchMove && out.touch.x == 20, "Second pop is e2");
    TEST_CHECK(q.pop(out) && out.type == InputEventType::TouchEnd && out.touch.x == 30, "Third pop is e3");

    TEST_CHECK(q.empty(), "Queue is empty after popping all elements");
    TEST_CHECK(!q.pop(out), "Pop from empty queue returns false");
}

void test_capacity_and_overflow_handling() {
    std::cout << "[Test 2] Verify Bounded Capacity and Oldest-Event Overwrite Upon Overflow...\n";

    constexpr std::size_t kCapacity = 4;
    TouchEventQueue q(kCapacity);

    // Push 6 events into a capacity-4 queue
    for (std::uint32_t i = 1; i <= 6; ++i) {
        InputEvent evt{};
        evt.touch.x = static_cast<std::int32_t>(i * 100);
        q.push(evt);
    }

    TEST_CHECK(q.full(), "Queue is full");
    TEST_CHECK(q.size() == kCapacity, "Queue size capped at capacity 4");
    TEST_CHECK(q.overflow_count() == 2, "Overflow count is 2 (2 oldest events dropped)");
    TEST_CHECK(q.total_pushed() == 6, "Total pushed is 6");

    // Popping should return the 4 newest events: 3, 4, 5, 6
    InputEvent out{};
    TEST_CHECK(q.pop(out) && out.touch.x == 300, "First pop is event 3 (300)");
    TEST_CHECK(q.pop(out) && out.touch.x == 400, "Second pop is event 4 (400)");
    TEST_CHECK(q.pop(out) && out.touch.x == 500, "Third pop is event 5 (500)");
    TEST_CHECK(q.pop(out) && out.touch.x == 600, "Fourth pop is event 6 (600)");
    TEST_CHECK(q.empty(), "Queue is now empty");
    TEST_CHECK(q.total_popped() == 4, "Total popped is 4");
}

void test_clear() {
    std::cout << "[Test 3] Verify Queue Clear Operation...\n";

    TouchEventQueue q(8);
    for (int i = 0; i < 5; ++i) {
        InputEvent e{};
        q.push(e);
    }
    TEST_CHECK(q.size() == 5, "Size is 5");

    q.clear();
    TEST_CHECK(q.empty(), "Queue empty after clear");
    TEST_CHECK(q.size() == 0, "Size is 0 after clear");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 213.14: Touch Event Queue Tests       \n";
    std::cout << "====================================================\n";

    test_fifo_ordering_and_peek();
    test_capacity_and_overflow_handling();
    test_clear();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL TESTS PASSED" : "FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

// ============================================================================
// Stage 214.14: Gesture Recognition Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <vector>

#include "runtime/ui/gesture_recognizer.h"

using namespace winios::runtime::ui;

namespace {

int g_failures = 0;

#define GEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[GEST_FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [GEST_OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_tap_and_double_tap() {
    std::cout << "[Stage 214.14] Verifying Tap and Double Tap Gestures...\n";

    GestureRecognizer recognizer;
    std::vector<GestureType> detected;

    recognizer.set_gesture_callback([&](const GestureEvent& ev) {
        detected.push_back(ev.type);
    });

    // 1. Single Tap at (100, 100)
    recognizer.on_touch_begin(0, 100, 100, 10000);
    recognizer.on_touch_end(0, 102, 101, 50000); // 40ms duration, < 10px move

    GEST_CHECK(detected.size() == 1, "First tap emitted");
    GEST_CHECK(detected[0] == GestureType::Tap, "Gesture is Tap");

    // 2. Second Tap at (102, 101) within 150ms -> Double Tap
    recognizer.on_touch_begin(0, 102, 101, 150000);
    recognizer.on_touch_end(0, 102, 101, 190000);

    GEST_CHECK(detected.size() == 2, "Second gesture emitted");
    GEST_CHECK(detected[1] == GestureType::DoubleTap, "Gesture is DoubleTap");
}

void test_long_press_and_pan() {
    std::cout << "[Stage 214.14] Verifying Long Press and Pan Gestures...\n";

    GestureRecognizer recognizer;
    std::vector<GestureType> detected;

    recognizer.set_gesture_callback([&](const GestureEvent& ev) {
        detected.push_back(ev.type);
    });

    // Long press: hold for 600ms at (200, 200)
    recognizer.on_touch_begin(0, 200, 200, 10000);
    recognizer.on_touch_move(0, 201, 201, 620000); // 610ms elapsed
    recognizer.on_touch_end(0, 201, 201, 650000);

    GEST_CHECK(!detected.empty() && detected[0] == GestureType::LongPress, "LongPress gesture detected");

    // Pan: move 40px
    detected.clear();
    recognizer.on_touch_begin(0, 100, 100, 700000);
    recognizer.on_touch_move(0, 130, 100, 750000); // moved 30px > 10px threshold
    recognizer.on_touch_end(0, 130, 100, 800000);

    GEST_CHECK(!detected.empty() && detected[0] == GestureType::Pan, "Pan gesture detected");
}

void test_swipe_and_cancel() {
    std::cout << "[Stage 214.14] Verifying Swipe and Cancellation Gestures...\n";

    GestureRecognizer recognizer;
    SwipeDirection swipe_dir = SwipeDirection::None;
    GestureType last_type = GestureType::Tap;

    recognizer.set_gesture_callback([&](const GestureEvent& ev) {
        last_type = ev.type;
        swipe_dir = ev.swipe_dir;
    });

    // Swipe Right: from 100 to 200 (> 40px)
    recognizer.on_touch_begin(0, 100, 100, 100000);
    recognizer.on_touch_end(0, 200, 100, 200000);

    GEST_CHECK(last_type == GestureType::Swipe, "Swipe gesture detected");
    GEST_CHECK(swipe_dir == SwipeDirection::Right, "Swipe direction is Right");

    // Cancellation:
    recognizer.on_touch_begin(0, 100, 100, 300000);
    recognizer.on_touch_cancel(0, 350000);

    GEST_CHECK(last_type == GestureType::Cancel, "Cancel gesture received, no tap emitted");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 214.14: Gesture Recognition Tests     \n";
    std::cout << "====================================================\n";

    test_tap_and_double_tap();
    test_long_press_and_pan();
    test_swipe_and_cancel();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL GESTURE TESTS PASSED" : "GESTURE FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

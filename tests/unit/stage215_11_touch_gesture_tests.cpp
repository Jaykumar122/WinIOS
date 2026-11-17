#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include "input/coordinate_normalizer.h"
#include "input/gesture_recognizer.h"
#include "input/host_input_queue.h"

using namespace winios::input;

void test_bounded_input_queue() {
    // 1. Queue with DropOldest policy
    HostInputQueue q(4, QueueDropPolicy::DropOldest);
    assert(q.empty());
    assert(q.capacity() == 4);

    InputEvent e1{}, e2{}, e3{}, e4{}, e5{};
    e1.touch.x = 10; e1.timestamp_us = 1000;
    e2.touch.x = 20; e2.timestamp_us = 2000;
    e3.touch.x = 30; e3.timestamp_us = 3000;
    e4.touch.x = 40; e4.timestamp_us = 4000;
    e5.touch.x = 50; e5.timestamp_us = 5000;

    assert(q.push(e1));
    assert(q.push(e2));
    assert(q.push(e3));
    assert(q.push(e4));
    assert(q.full());
    assert(q.size() == 4);

    // Push 5th event -> should drop e1 (x=10)
    assert(q.push(e5));
    assert(q.dropped_count() == 1);
    assert(q.size() == 4);

    InputEvent popped{};
    assert(q.pop(popped));
    assert(popped.touch.x == 20); // e2 was next after e1 was dropped!

    // 2. Queue with DropNewest policy
    HostInputQueue q2(2, QueueDropPolicy::DropNewest);
    assert(q2.push(e1));
    assert(q2.push(e2));
    assert(q2.full());
    assert(!q2.push(e3)); // Rejected!
    assert(q2.dropped_count() == 1);

    InputEvent p1{};
    assert(q2.pop(p1));
    assert(p1.touch.x == 10); // e1 preserved

    std::cout << "[PASS] test_bounded_input_queue passed\n";
}

void test_coordinate_normalizer() {
    // Stretch mode: window 400x600, panel 800x1200 (scale factor 2.0)
    auto p1 = CoordinateNormalizer::normalize(200, 300, 400, 600, 800, 1200, ScalingMode::Stretch);
    assert(p1.panel_x == 400);
    assert(p1.panel_y == 600);
    assert(p1.in_bounds);

    // Aspect ratio preservation with Pillarbox:
    // Window: 1000x1200 (aspect 0.833) vs Panel: 800x1200 (aspect 0.667)
    // Pillarbox adds letterbox padding on left/right:
    // Active render width = 1200 * (800/1200) = 800. Offset X = (1000 - 800)/2 = 100.
    auto p_center = CoordinateNormalizer::normalize(500, 600, 1000, 1200, 800, 1200, ScalingMode::PreserveAspectRatio);
    assert(p_center.in_bounds);
    assert(p_center.panel_x == 400); // exactly center!
    assert(p_center.panel_y == 600);

    // Click inside left pillarbox bar (x = 50 < 100)
    auto p_bar = CoordinateNormalizer::normalize(50, 600, 1000, 1200, 800, 1200, ScalingMode::PreserveAspectRatio);
    assert(!p_bar.in_bounds); // Outside active panel bounds
    assert(p_bar.panel_x == 0); // clamped to left edge

    std::cout << "[PASS] test_coordinate_normalizer passed\n";
}

void test_gesture_recognizer() {
    GestureRecognizer gr;
    std::vector<GestureEvent> recognized;
    gr.set_gesture_callback([&](const GestureEvent& ge) {
        recognized.push_back(ge);
    });

    // 1. Single Tap
    InputEvent down{}, up{};
    down.type = InputEventType::TouchBegin;
    down.touch.x = 100; down.touch.y = 200;
    down.timestamp_us = 100000;
    gr.process_input_event(down);

    up.type = InputEventType::TouchEnd;
    up.touch.x = 102; up.touch.y = 201; // < 15px
    up.timestamp_us = 150000; // 50ms duration
    gr.process_input_event(up);

    assert(recognized.size() == 1);
    assert(recognized[0].type == GestureType::Tap);
    assert(recognized[0].x == 102);

    // 2. Pan / Drag
    recognized.clear();
    gr.process_input_event(down);

    InputEvent move{};
    move.type = InputEventType::TouchMove;
    move.touch.x = 150; move.touch.y = 250; // dist > 10px
    move.timestamp_us = 120000;
    gr.process_input_event(move);

    assert(!recognized.empty());
    assert(recognized[0].type == GestureType::Pan);
    assert(recognized[0].delta_x == 50);
    assert(recognized[0].delta_y == 50);

    gr.process_input_event(up);

    std::cout << "[PASS] test_gesture_recognizer passed\n";
}

void test_focus_navigator() {
    FocusNavigator nav({101, 102, 103});
    assert(nav.current_focus() == 101);

    // Tab -> 102
    assert(nav.focus_next() == 102);
    // Tab -> 103
    assert(nav.focus_next() == 103);
    // Wrap Tab -> 101
    assert(nav.focus_next() == 101);

    // Shift+Tab -> 103
    assert(nav.focus_previous() == 103);
    // Shift+Tab -> 102
    assert(nav.focus_previous() == 102);

    // Direct jump
    nav.set_focus(103);
    assert(nav.current_focus() == 103);

    std::cout << "[PASS] test_focus_navigator passed\n";
}

int main() {
    std::cout << "Starting Stage 215.11 Touch and Gesture Validation Tests...\n";
    test_bounded_input_queue();
    test_coordinate_normalizer();
    test_gesture_recognizer();
    test_focus_navigator();
    std::cout << "All Stage 215.11 Touch & Gesture tests PASSED!\n";
    return 0;
}

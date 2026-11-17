#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

#include "ui/display_window.h"

using namespace winios::ui;

void test_lifecycle_states_and_transitions() {
    DisplayWindowConfig cfg;
    cfg.headless = true;
    cfg.native_width = 800;
    cfg.native_height = 1200;

    DisplayWindow win(cfg);
    assert(win.lifecycle_state() == DisplayLifecycleState::Uninitialized);

    // Create
    assert(win.create());
    assert(win.lifecycle_state() == DisplayLifecycleState::Visible);
    assert(win.is_open());

    // Hide
    win.hide();
    assert(win.lifecycle_state() == DisplayLifecycleState::Hidden);

    // Show
    win.show();
    assert(win.lifecycle_state() == DisplayLifecycleState::Visible);

    // Minimize
    win.set_minimized(true);
    assert(win.lifecycle_state() == DisplayLifecycleState::Minimized);

    // Restore
    win.set_minimized(false);
    assert(win.lifecycle_state() == DisplayLifecycleState::Visible);

    // Resize
    win.handle_resize(500, 800);
    assert(win.window_width() == 500);
    assert(win.window_height() == 800);

    // Close
    win.close();
    assert(win.lifecycle_state() == DisplayLifecycleState::Closed);
    assert(!win.is_open());

    std::cout << "[PASS] test_lifecycle_states_and_transitions passed\n";
}

void test_presentation_statistics() {
    DisplayWindowConfig cfg;
    cfg.headless = true;
    cfg.native_width = 800;
    cfg.native_height = 1200;

    DisplayWindow win(cfg);
    assert(win.create());

    // 1. Drop a null frame
    assert(!win.present_frame(nullptr, 800, 1200));
    auto stats1 = win.presentation_stats();
    assert(stats1.total_frames_dropped == 1);
    assert(stats1.total_frames_presented == 0);

    // 2. Present 3 valid frames with tiny delay
    std::vector<std::uint8_t> frame(800 * 1200 * 4, 0x55);
    for (int i = 0; i < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        assert(win.present_frame(frame.data(), 800, 1200));
    }

    auto stats2 = win.presentation_stats();
    assert(stats2.total_frames_presented == 3);
    assert(stats2.total_frames_dropped == 1);
    assert(stats2.last_presentation_time_ns > 0);
    assert(stats2.average_frame_interval_ms > 0.0);
    assert(stats2.client_width == win.window_width());
    assert(stats2.client_height == win.window_height());
    assert(stats2.lifecycle_state == DisplayLifecycleState::Visible);

    win.close();
    std::cout << "[PASS] test_presentation_statistics passed\n";
}

int main() {
    std::cout << "Starting Stage 215.6 Display Lifecycle & Synchronization Tests...\n";
    test_lifecycle_states_and_transitions();
    test_presentation_statistics();
    std::cout << "All Stage 215.6 Display Lifecycle tests PASSED!\n";
    return 0;
}

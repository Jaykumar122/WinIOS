// ============================================================================
// Stage 213.10: Frame Scheduler and Refresh Model Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <vector>

#include "graphics/frame_scheduler.h"

using namespace winios::graphics;

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

void test_target_fps_and_timing() {
    std::cout << "[Test 1] Verify Target FPS Configuration and Frame Intervals...\n";

    FrameScheduler sched60(60);
    TEST_CHECK(sched60.target_fps() == 60, "Target FPS is 60");
    TEST_CHECK(sched60.frame_interval_us() == 16666ULL, "60 FPS frame interval is 16666 us (~16.6 ms)");

    FrameScheduler sched120(120);
    TEST_CHECK(sched120.target_fps() == 120, "Target FPS is 120 (ProMotion)");
    TEST_CHECK(sched120.frame_interval_us() == 8333ULL, "120 FPS frame interval is 8333 us (~8.3 ms)");
}

void test_deterministic_vsync_simulation() {
    std::cout << "[Test 2] Verify Deterministic Virtual Clock Step & Vsync Boundaries...\n";

    FrameScheduler sched(60);
    std::vector<std::pair<std::uint64_t, std::uint64_t>> vsync_events;

    sched.set_vsync_callback([&](std::uint64_t seq, std::uint64_t ts) {
        vsync_events.emplace_back(seq, ts);
    });

    // Advance by 10,000 us (below 16,666 us boundary)
    bool triggered = sched.advance_time(10000);
    TEST_CHECK(!triggered, "No vsync triggered for 10ms (< 16.6ms)");
    TEST_CHECK(vsync_events.empty(), "Zero vsync callbacks invoked");

    // Advance by another 10,000 us (total 20,000 us >= 16,666 us)
    triggered = sched.advance_time(10000);
    TEST_CHECK(triggered, "Vsync boundary crossed at 20ms");
    TEST_CHECK(vsync_events.size() == 1, "Exactly 1 vsync callback invoked");
    TEST_CHECK(vsync_events[0].first == 1, "First vsync sequence is 1");
    TEST_CHECK(vsync_events[0].second == 16666ULL, "First vsync timestamp is 16666 us");

    // Advance by 50,000 us (3 more vsyncs: 33332, 49998, 66664)
    sched.advance_time(50000);
    TEST_CHECK(vsync_events.size() == 4, "Total 4 vsync events after 70ms");
    TEST_CHECK(vsync_events.back().first == 4, "Last vsync sequence is 4");
}

void test_frame_completion_and_metrics() {
    std::cout << "[Test 3] Verify Frame Rendering Lifecycle and Metrics Accounting...\n";

    FrameScheduler sched(60);
    std::uint64_t completed_seq = 0;
    std::uint64_t completed_duration = 0;

    sched.set_frame_complete_callback([&](std::uint64_t seq, std::uint64_t dur) {
        completed_seq = seq;
        completed_duration = dur;
    });

    sched.trigger_vsync();
    sched.notify_frame_begin();
    sched.advance_time(5000); // Frame took 5ms to render
    sched.notify_frame_rendered();

    TEST_CHECK(sched.stats().rendered_frames == 1, "Rendered frames count is 1");
    TEST_CHECK(completed_seq == 1, "Completed frame sequence matches 1");
    TEST_CHECK(completed_duration == 5000, "Render duration recorded as 5000 us");

    sched.notify_frame_dropped();
    TEST_CHECK(sched.stats().dropped_frames == 1, "Dropped frames count recorded as 1");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 213.10: Frame Scheduler Tests         \n";
    std::cout << "====================================================\n";

    test_target_fps_and_timing();
    test_deterministic_vsync_simulation();
    test_frame_completion_and_metrics();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL TESTS PASSED" : "FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

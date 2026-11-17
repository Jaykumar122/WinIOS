// ============================================================================
// Stage 213.9: Damage Tracking and Partial Redraw Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "graphics/compositor.h"

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

void test_damage_tracker_bounding_box_aggregation() {
    std::cout << "[Test 1] Verify DamageTracker Dirty Rect Aggregation and Bounding Box Union...\n";

    DamageTracker dt;
    TEST_CHECK(!dt.has_damage(), "New DamageTracker has no damage");

    // Add first rect [10, 20, 30, 40]
    dt.add_dirty_rect(Rect{10, 20, 30, 40});
    TEST_CHECK(dt.has_damage(), "has_damage returns true after adding rect");
    TEST_CHECK(dt.bounding_box().x == 10 && dt.bounding_box().y == 20, "Bounding box origin matches [10, 20]");
    TEST_CHECK(dt.bounding_box().width == 30 && dt.bounding_box().height == 40, "Bounding box size matches [30, 40]");

    // Add second disjoint rect [60, 80, 20, 20]
    // Union should span [10, 20] to [80, 100] -> width = 70, height = 80
    dt.add_dirty_rect(Rect{60, 80, 20, 20});
    TEST_CHECK(dt.bounding_box().x == 10 && dt.bounding_box().y == 20, "Union origin matches min x/y [10, 20]");
    TEST_CHECK(dt.bounding_box().width == 70, "Union width is 70 (80 - 10)");
    TEST_CHECK(dt.bounding_box().height == 80, "Union height is 80 (100 - 20)");

    // Clear damage
    dt.clear();
    TEST_CHECK(!dt.has_damage(), "clear() resets damage flag");
    TEST_CHECK(dt.bounding_box().empty(), "clear() resets bounding box");
}

void test_partial_vs_full_recomposition() {
    std::cout << "[Test 2] Verify Independent Partial Redraw vs Full Redraw Performance & Counting...\n";

    DisplayConfiguration cfg{};
    cfg.width = 100;
    cfg.height = 100;
    cfg.row_stride = 100 * 4;

    auto disp = std::make_shared<VirtualDisplay>(cfg);
    Compositor compositor(disp);
    compositor.set_background_color(0xFF000000u);

    // Initial full composite
    compositor.composite(true);
    TEST_CHECK(compositor.stats().full_redraw_frames == 1, "Initial frame was full redraw");
    TEST_CHECK(compositor.stats().partial_redraw_frames == 0, "Zero partial redraws so far");

    // Attempt second composite with no damage -> must skip
    compositor.composite(false);
    TEST_CHECK(compositor.stats().skipped_frames == 1, "Undamaged frame was skipped");

    // Add a surface with a tiny damaged region
    auto surf = std::make_shared<DisplaySurface>(1, 1, 50, 50);
    surf->set_position(10, 10);
    surf->fill_rect(1, Rect{0, 0, 50, 50}, 0xFF00FF00u); // Green
    compositor.add_surface(surf);

    // Mark damage only on the small surface
    compositor.composite(false);
    TEST_CHECK(compositor.stats().partial_redraw_frames == 1, "Partial redraw triggered for surface damage");
    TEST_CHECK(compositor.stats().full_redraw_frames == 1, "Full redraw count unchanged");

    // Pixel at (20, 20) inside surface is green
    TEST_CHECK(disp->read_pixel(20, 20) == 0xFF00FF00u, "Damaged region updated to green");

    // Pixel outside surface remains black
    TEST_CHECK(disp->read_pixel(90, 90) == 0xFF000000u, "Undamaged region remained black");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 213.9: Damage Tracking Tests          \n";
    std::cout << "====================================================\n";

    test_damage_tracker_bounding_box_aggregation();
    test_partial_vs_full_recomposition();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL TESTS PASSED" : "FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

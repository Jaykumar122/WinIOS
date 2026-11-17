// ============================================================================
// Stage 213.15: Windows Input Translation Tests
// ============================================================================
#include <cmath>
#include <cstdint>
#include <iostream>

#include "graphics/virtual_display.h"
#include "input/input_router.h"

using namespace winios::graphics;
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

void test_coordinate_scaling_portrait() {
    std::cout << "[Test 1] Verify Host-to-Virtual Coordinate Scaling in Portrait Mode...\n";

    DisplayConfiguration cfg{};
    cfg.width = 1179;
    cfg.height = 2556;

    // Desktop host window footprint: 400 x 800
    constexpr std::uint32_t kHostW = 400;
    constexpr std::uint32_t kHostH = 800;

    // Center click at (200, 400)
    TouchPoint tp = InputRouter::translate_host_coordinate(200, 400, kHostW, kHostH, cfg);
    TEST_CHECK(tp.is_active, "Translated point is active");
    TEST_CHECK(tp.x >= 588 && tp.x <= 590, "X coordinate scaled to center (~589)");
    TEST_CHECK(tp.y >= 1277 && tp.y <= 1279, "Y coordinate scaled to center (~1278)");

    // Origin click at (0, 0)
    tp = InputRouter::translate_host_coordinate(0, 0, kHostW, kHostH, cfg);
    TEST_CHECK(tp.x == 0 && tp.y == 0, "Origin click maps to (0, 0)");

    // Max corner click at (400, 800) -> clamped to (1178, 2555)
    tp = InputRouter::translate_host_coordinate(400, 800, kHostW, kHostH, cfg);
    TEST_CHECK(tp.x == 1178 && tp.y == 2555, "Max corner maps to display max edge [1178, 2555]");
}

void test_clamping_and_out_of_bounds() {
    std::cout << "[Test 2] Verify Out-of-Bounds Clamping Protection...\n";

    DisplayConfiguration cfg{};
    cfg.width = 1000;
    cfg.height = 2000;

    // Negative coordinates
    TouchPoint tp = InputRouter::translate_host_coordinate(-50, -100, 500, 1000, cfg);
    TEST_CHECK(tp.x == 0 && tp.y == 0, "Negative coordinates clamped to (0, 0)");

    // Huge coordinates outside host window
    tp = InputRouter::translate_host_coordinate(9999, 9999, 500, 1000, cfg);
    TEST_CHECK(tp.x == 999 && tp.y == 1999, "Huge coordinates clamped to (999, 1999)");
}

void test_landscape_orientation_translation() {
    std::cout << "[Test 3] Verify Coordinate Translation in Landscape Orientation...\n";

    DisplayConfiguration cfg{};
    cfg.set_orientation(DisplayOrientation::LandscapeLeft); // 2556 x 1179

    constexpr std::uint32_t kHostW = 800;
    constexpr std::uint32_t kHostH = 400;

    TouchPoint tp = InputRouter::translate_host_coordinate(400, 200, kHostW, kHostH, cfg);
    TEST_CHECK(tp.x >= 1277 && tp.x <= 1279, "Landscape X coordinate scaled to ~1278");
    TEST_CHECK(tp.y >= 588 && tp.y <= 590, "Landscape Y coordinate scaled to ~589");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 213.15: Input Translation Tests       \n";
    std::cout << "====================================================\n";

    test_coordinate_scaling_portrait();
    test_clamping_and_out_of_bounds();
    test_landscape_orientation_translation();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL TESTS PASSED" : "FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

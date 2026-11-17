// ============================================================================
// Stage 213.8: Compositor Architecture Tests
// ============================================================================
#include <cmath>
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

void test_compositor_multi_surface_layering() {
    std::cout << "[Test 1] Verify Multi-Surface Z-Ordered Compositing and Overlap...\n";

    DisplayConfiguration cfg{};
    cfg.width = 200;
    cfg.height = 200;
    cfg.row_stride = 200 * 4;

    auto disp = std::make_shared<VirtualDisplay>(cfg);
    Compositor compositor(disp);
    compositor.set_background_color(0xFF000000u); // Opaque Black

    // Surface 1: [0, 0, 100, 100], Z=1, Solid Blue (0xFF0000FF in BGRA -> B=255, G=0, R=0, A=255)
    auto surf1 = std::make_shared<DisplaySurface>(1, 10, 100, 100, PixelFormat::B8G8R8A8_UNORM);
    surf1->set_position(0, 0);
    surf1->set_z_order(1);
    surf1->fill_rect(10, Rect{0, 0, 100, 100}, 0xFF0000FFu);

    // Surface 2: [50, 50, 100, 100], Z=2, Solid Red (0xFFFF0000 in BGRA -> B=0, G=0, R=255, A=255)
    auto surf2 = std::make_shared<DisplaySurface>(2, 20, 100, 100, PixelFormat::B8G8R8A8_UNORM);
    surf2->set_position(50, 50);
    surf2->set_z_order(2);
    surf2->fill_rect(20, Rect{0, 0, 100, 100}, 0xFFFF0000u);

    compositor.add_surface(surf1);
    compositor.add_surface(surf2);

    TEST_CHECK(compositor.composite(true), "Composite pass succeeded");

    // Check pixel at (25, 25): Only inside surf1 -> Solid Blue
    std::uint32_t px_surf1 = disp->read_pixel(25, 25);
    TEST_CHECK(px_surf1 == 0xFF0000FFu, "Pixel (25, 25) in surf1 is solid blue");

    // Check pixel at (75, 75): Inside BOTH surf1 and surf2 -> surf2 is on top (Z=2) -> Solid Red
    std::uint32_t px_overlap = disp->read_pixel(75, 75);
    TEST_CHECK(px_overlap == 0xFFFF0000u, "Pixel (75, 75) in overlap region shows top surface surf2 (red)");

    // Check pixel at (125, 125): Only inside surf2 -> Solid Red
    std::uint32_t px_surf2 = disp->read_pixel(125, 125);
    TEST_CHECK(px_surf2 == 0xFFFF0000u, "Pixel (125, 125) in surf2 is solid red");

    // Check pixel at (180, 180): Outside both surfaces -> Background Black
    std::uint32_t px_bg = disp->read_pixel(180, 180);
    TEST_CHECK(px_bg == 0xFF000000u, "Pixel (180, 180) outside surfaces is background black");
}

void test_alpha_compositing_pipeline() {
    std::cout << "[Test 2] Verify Alpha Compositing with Opacity Between Surfaces...\n";

    DisplayConfiguration cfg{};
    cfg.width = 100;
    cfg.height = 100;
    cfg.row_stride = 100 * 4;

    auto disp = std::make_shared<VirtualDisplay>(cfg);
    Compositor compositor(disp);

    // Surface 1: Background [0, 0, 100, 100], Z=1, Solid Blue
    auto surf1 = std::make_shared<DisplaySurface>(1, 1, 100, 100);
    surf1->set_z_order(1);
    surf1->fill_rect(1, Rect{0, 0, 100, 100}, 0xFF0000FFu);

    // Surface 2: Foreground [0, 0, 100, 100], Z=2, Solid Red with 50% opacity
    auto surf2 = std::make_shared<DisplaySurface>(2, 2, 100, 100);
    surf2->set_z_order(2);
    surf2->set_opacity(0.5f);
    surf2->fill_rect(2, Rect{0, 0, 100, 100}, 0xFFFF0000u);

    compositor.add_surface(surf1);
    compositor.add_surface(surf2);

    TEST_CHECK(compositor.composite(true), "Composite pass succeeded");

    // Inspect blended pixel at (50, 50)
    std::uint32_t px = disp->read_pixel(50, 50);
    std::uint8_t b = px & 0xFF;
    std::uint8_t g = (px >> 8) & 0xFF;
    std::uint8_t r = (px >> 16) & 0xFF;
    std::uint8_t a = (px >> 24) & 0xFF;

    TEST_CHECK(a == 255, "Blended pixel alpha is 255");
    TEST_CHECK(g == 0, "Blended pixel green is 0");
    TEST_CHECK(r >= 126 && r <= 129, "Blended red channel is ~128");
    TEST_CHECK(b >= 126 && b <= 129, "Blended blue channel is ~127");
}

void test_hidden_and_zero_opacity_surfaces() {
    std::cout << "[Test 3] Verify Hidden and Zero-Opacity Surfaces are Skipped...\n";

    DisplayConfiguration cfg{};
    cfg.width = 100;
    cfg.height = 100;
    cfg.row_stride = 100 * 4;

    auto disp = std::make_shared<VirtualDisplay>(cfg);
    Compositor compositor(disp);
    compositor.set_background_color(0xFF000000u);

    auto surf = std::make_shared<DisplaySurface>(1, 1, 100, 100);
    surf->fill_rect(1, Rect{0, 0, 100, 100}, 0xFF00FF00u); // Green
    surf->set_visible(false); // Hidden!

    compositor.add_surface(surf);
    compositor.composite(true);

    TEST_CHECK(disp->read_pixel(50, 50) == 0xFF000000u, "Hidden surface was skipped (remains black)");

    // Now make visible but zero opacity
    surf->set_visible(true);
    surf->set_opacity(0.0f);
    compositor.composite(true);
    TEST_CHECK(disp->read_pixel(50, 50) == 0xFF000000u, "Zero-opacity surface was skipped (remains black)");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 213.8: Compositor Architecture Tests  \n";
    std::cout << "====================================================\n";

    test_compositor_multi_surface_layering();
    test_alpha_compositing_pipeline();
    test_hidden_and_zero_opacity_surfaces();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL TESTS PASSED" : "FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

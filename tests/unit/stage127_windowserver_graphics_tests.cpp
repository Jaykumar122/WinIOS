#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include "graphics/window_server.h"

using namespace winios::graphics;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

bool test_display_geometry_and_resolution() {
    std::cout << "[Test 1] iPhone 15 Super Retina XDR Geometry & Dimensions...\n";

    WindowServerCompositor comp;
    TEST_ASSERT(comp.width() == 1179, "Width must match iPhone 15 (1179px)");
    TEST_ASSERT(comp.height() == 2556, "Height must match iPhone 15 (2556px)");

    std::size_t expected_fb_bytes = 1179ULL * 2556ULL * 4ULL;
    TEST_ASSERT(comp.framebuffer_size() == expected_fb_bytes, "Framebuffer size must be ~11.5 MB");

    std::cout << "  Passed (1179 x 2556 @ 32bpp, " << expected_fb_bytes << " bytes)\n";
    return true;
}

bool test_iosurface_creation_and_pixel_access() {
    std::cout << "[Test 2] IOSurface Allocation & Pixel Access...\n";

    WindowServerCompositor comp;
    auto surf = comp.create_surface(100, 100);
    TEST_ASSERT(surf != nullptr, "create_surface must succeed");
    TEST_ASSERT(surf->width() == 100, "Surface width match");
    TEST_ASSERT(surf->height() == 100, "Surface height match");

    std::uint32_t test_color = 0xFFAABBCCu;
    surf->write_pixel(15, 25, test_color);

    std::uint32_t read_back = surf->read_pixel(15, 25);
    TEST_ASSERT(read_back == test_color, "Pixel readback match");

    std::cout << "  Passed (Surface 100x100 pixel read/write verified)\n";
    return true;
}

bool test_iosurface_rect_fill() {
    std::cout << "[Test 3] IOSurface Rectangle Fill Operation...\n";

    WindowServerCompositor comp;
    auto surf = comp.create_surface(200, 200);

    Rect r{10, 10, 50, 50};
    std::uint32_t red = 0xFFFF0000u;
    surf->fill_rect(r, red);

    TEST_ASSERT(surf->read_pixel(25, 25) == red, "Pixel inside rect must be Red");
    TEST_ASSERT(surf->read_pixel(5, 5) == 0, "Pixel outside rect must be 0");
    TEST_ASSERT(surf->read_pixel(70, 70) == 0, "Pixel outside rect must be 0");

    std::cout << "  Passed (Filled 50x50 rect with 0xFFFF0000)\n";
    return true;
}

bool test_damage_tracker_bounding_box() {
    std::cout << "[Test 4] Damage Tracker Dirty Rect Aggregation...\n";

    DamageTracker tracker;
    TEST_ASSERT(!tracker.has_damage(), "Initially clean");

    tracker.add_dirty_rect(Rect{10, 20, 30, 40}); // [10, 40), [20, 60)
    TEST_ASSERT(tracker.has_damage(), "Must flag damage");
    TEST_ASSERT(tracker.bounding_box().x == 10, "BBox x");
    TEST_ASSERT(tracker.bounding_box().y == 20, "BBox y");

    tracker.add_dirty_rect(Rect{100, 150, 50, 50}); // [100, 150), [150, 200)
    const auto& bbox = tracker.bounding_box();
    TEST_ASSERT(bbox.x == 10, "Merged BBox x");
    TEST_ASSERT(bbox.y == 20, "Merged BBox y");
    TEST_ASSERT(bbox.width == 140, "Merged BBox width");  // 150 - 10 = 140
    TEST_ASSERT(bbox.height == 180, "Merged BBox height"); // 200 - 20 = 180

    tracker.clear();
    TEST_ASSERT(!tracker.has_damage(), "Cleared damage");

    std::cout << "  Passed (Aggregated dirty rectangles into bounding box)\n";
    return true;
}

bool test_compositor_layer_addition_and_removal() {
    std::cout << "[Test 5] Compositor Layer Hierarchy Management...\n";

    WindowServerCompositor comp(400, 800);
    auto surf = comp.create_surface(400, 50);

    WindowServerLayer layer;
    layer.layer_id = 1;
    layer.surface = surf;
    layer.frame = Rect{0, 0, 400, 50};
    layer.z_order = 10;

    bool ok_add = comp.add_layer(layer);
    TEST_ASSERT(ok_add, "add_layer must succeed");
    TEST_ASSERT(comp.damage_tracker().has_damage(), "Adding layer marks damage");

    bool ok_rem = comp.remove_layer(1);
    TEST_ASSERT(ok_rem, "remove_layer must succeed");

    std::cout << "  Passed (Added and removed status bar layer)\n";
    return true;
}

bool test_z_order_compositing() {
    std::cout << "[Test 6] Z-Order Compositing (Back-to-Front)...\n";

    WindowServerCompositor comp(200, 200);

    // Layer 1: Red background at z=0, covering (0,0,100,100)
    auto surf1 = comp.create_surface(100, 100);
    surf1->fill_rect(Rect{0, 0, 100, 100}, 0xFFFF0000u); // Red
    WindowServerLayer l1;
    l1.layer_id = 1;
    l1.surface = surf1;
    l1.frame = Rect{0, 0, 100, 100};
    l1.z_order = 0;
    comp.add_layer(l1);

    // Layer 2: Blue foreground at z=1, covering (50,50,100,100)
    auto surf2 = comp.create_surface(100, 100);
    surf2->fill_rect(Rect{0, 0, 100, 100}, 0xFF0000FFu); // Blue
    WindowServerLayer l2;
    l2.layer_id = 2;
    l2.surface = surf2;
    l2.frame = Rect{50, 50, 100, 100};
    l2.z_order = 1;
    comp.add_layer(l2);

    std::vector<std::uint8_t> fb(comp.framebuffer_size(), 0);
    bool ok_comp = comp.composite(fb.data(), fb.size());
    TEST_ASSERT(ok_comp, "composite must succeed");

    auto read_fb = [&](std::uint32_t x, std::uint32_t y) -> std::uint32_t {
        std::size_t off = (static_cast<std::size_t>(y) * 200 * 4) + (x * 4);
        std::uint32_t c = 0;
        std::memcpy(&c, fb.data() + off, 4);
        return c;
    };

    TEST_ASSERT(read_fb(25, 25) == 0xFFFF0000u, "(25, 25) must be Red (Layer 1 only)");
    TEST_ASSERT(read_fb(125, 125) == 0xFF0000FFu, "(125, 125) must be Blue (Layer 2 only)");
    TEST_ASSERT(read_fb(75, 75) == 0xFF0000FFu, "(75, 75) overlap must be Blue (z_order 1 on top)");

    std::cout << "  Passed (Overlap resolved cleanly by z-order)\n";
    return true;
}

bool test_alpha_blending() {
    std::cout << "[Test 7] Alpha Blending & Layer Opacity...\n";

    WindowServerCompositor comp(100, 100);

    // Bottom layer: Solid White (0xFFFFFFFF)
    auto surf_white = comp.create_surface(100, 100);
    surf_white->fill_rect(Rect{0, 0, 100, 100}, 0xFFFFFFFFu);
    WindowServerLayer l1;
    l1.layer_id = 1;
    l1.surface = surf_white;
    l1.frame = Rect{0, 0, 100, 100};
    l1.z_order = 0;
    comp.add_layer(l1);

    // Top layer: Solid Black with 50% opacity
    auto surf_black = comp.create_surface(100, 100);
    surf_black->fill_rect(Rect{0, 0, 100, 100}, 0xFF000000u);
    WindowServerLayer l2;
    l2.layer_id = 2;
    l2.surface = surf_black;
    l2.frame = Rect{0, 0, 100, 100};
    l2.z_order = 1;
    l2.opacity = 0.5f; // 50% opacity
    comp.add_layer(l2);

    std::vector<std::uint8_t> fb(comp.framebuffer_size(), 0);
    comp.composite(fb.data(), fb.size());

    // Blended pixel should be ~0x7F or 0x80 (mid-gray)
    std::uint32_t blended = 0;
    std::memcpy(&blended, fb.data(), 4);
    std::uint8_t r = (blended >> 16) & 0xFF;
    std::uint8_t g = (blended >> 8) & 0xFF;
    std::uint8_t b = blended & 0xFF;

    TEST_ASSERT(r >= 120 && r <= 135, "Blended Red must be ~127");
    TEST_ASSERT(g >= 120 && g <= 135, "Blended Green must be ~127");
    TEST_ASSERT(b >= 120 && b <= 135, "Blended Blue must be ~127");

    std::cout << "  Passed (Alpha blend result: RGB=" << (int)r << "," << (int)g << "," << (int)b << ")\n";
    return true;
}

bool test_directx_host_presentation_staging() {
    std::cout << "[Test 8] DirectX Host Presentation Staging Bridge...\n";

    WindowServerCompositor comp(200, 400);
    auto surf = comp.create_surface(200, 400);
    surf->fill_rect(Rect{0, 0, 200, 400}, 0xFF00FF00u); // Green

    WindowServerLayer l;
    l.layer_id = 1;
    l.surface = surf;
    l.frame = Rect{0, 0, 200, 400};
    comp.add_layer(l);

    std::vector<std::uint8_t> fb(comp.framebuffer_size(), 0);
    comp.composite(fb.data(), fb.size());

    bool ok_pres = comp.present_to_host(fb.data(), fb.size());
    TEST_ASSERT(ok_pres, "present_to_host must succeed");

    const auto& stats = comp.presentation_adapter().stats();
    TEST_ASSERT(stats.presented_frames == 1, "presented_frames count must be 1");
    TEST_ASSERT(stats.dropped_frames == 0, "dropped_frames count must be 0");

    // Check host staging buffer
    const auto& staging = comp.presentation_adapter().host_staging_buffer();
    TEST_ASSERT(staging.size() == comp.framebuffer_size(), "Staging buffer size match");
    std::uint32_t staged_pixel = 0;
    std::memcpy(&staged_pixel, staging.data(), 4);
    TEST_ASSERT(staged_pixel == 0xFF00FF00u, "Staged pixel must match composited Green");

    std::cout << "  Passed (Presented frame to host DirectX staging surface)\n";
    return true;
}

bool test_dirty_rect_optimized_presentation() {
    std::cout << "[Test 9] Damage-Optimized Partial Presentation...\n";

    WindowServerCompositor comp(200, 200);
    std::vector<std::uint8_t> fb(comp.framebuffer_size(), 0);

    // Initial full frame
    comp.present_to_host(fb.data(), fb.size());

    // Modify a 10x10 icon at (50, 50)
    std::uint32_t new_color = 0xFF112233u;
    std::size_t offset = (50 * 200 * 4) + (50 * 4);
    std::memcpy(fb.data() + offset, &new_color, 4);

    comp.mark_damage(Rect{50, 50, 10, 10});
    TEST_ASSERT(comp.damage_tracker().has_damage(), "Must track damage");

    bool ok = comp.present_to_host(fb.data(), fb.size());
    TEST_ASSERT(ok, "Partial present must succeed");
    TEST_ASSERT(!comp.damage_tracker().has_damage(), "Damage cleared after present");

    const auto& staging = comp.presentation_adapter().host_staging_buffer();
    std::uint32_t staged = 0;
    std::memcpy(&staged, staging.data() + offset, 4);
    TEST_ASSERT(staged == new_color, "Updated pixel copied to staging");

    std::cout << "  Passed (Optimized dirty rect blitted)\n";
    return true;
}

bool test_springboard_full_screen_ui_composition() {
    std::cout << "[Test 10] SpringBoard Full Screen UI Scene Composition...\n";

    WindowServerCompositor comp; // 1179 x 2556
    std::vector<std::uint8_t> fb(comp.framebuffer_size(), 0);

    // 1. Wallpaper Layer (z=0)
    auto wallpaper = comp.create_surface(1179, 2556);
    wallpaper->fill_rect(Rect{0, 0, 1179, 2556}, 0xFF1C1C1Eu); // Dark Apple wallpaper
    WindowServerLayer l_wall{1, 100, wallpaper, Rect{0, 0, 1179, 2556}, 1.0f, 0, false};
    comp.add_layer(l_wall);

    // 2. Status Bar Layer (z=10)
    auto statusbar = comp.create_surface(1179, 120);
    statusbar->fill_rect(Rect{0, 0, 1179, 120}, 0xFF000000u); // Dynamic island region
    WindowServerLayer l_stat{2, 100, statusbar, Rect{0, 0, 1179, 120}, 1.0f, 10, false};
    comp.add_layer(l_stat);

    // 3. Dock Layer (z=5)
    auto dock = comp.create_surface(1000, 220);
    dock->fill_rect(Rect{0, 0, 1000, 220}, 0x80FFFFFFu); // Translucent frosted dock
    WindowServerLayer l_dock{3, 100, dock, Rect{89, 2250, 1000, 220}, 0.8f, 5, false};
    comp.add_layer(l_dock);

    // 4. App Icon Layer (z=2)
    auto icon = comp.create_surface(180, 180);
    icon->fill_rect(Rect{0, 0, 180, 180}, 0xFF007AFFu); // iOS Blue Icon
    WindowServerLayer l_icon{4, 100, icon, Rect{120, 300, 180, 180}, 1.0f, 2, false};
    comp.add_layer(l_icon);

    // Perform multi-layer compositing
    bool ok_comp = comp.composite(fb.data(), fb.size());
    TEST_ASSERT(ok_comp, "Full scene composite must succeed");

    bool ok_pres = comp.present_to_host(fb.data(), fb.size());
    TEST_ASSERT(ok_pres, "Full scene present must succeed");

    const auto& stats = comp.presentation_adapter().stats();
    TEST_ASSERT(stats.presented_frames >= 1, "Must register presentation");

    std::cout << "  Passed (Composited Wallpaper + Status Bar + Dock + App Icon: 1179x2556)\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  WINIOS STAGE 127: WINDOWSERVER & GRAPHICS INTEGRATION TESTS    \n";
    std::cout << "  Platform: iPhone 15 / Apple A16 Bionic (T8120) / ARM64e        \n";
    std::cout << "  Display: 1179 x 2556 Super Retina XDR -> Windows 11 DirectX    \n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int total = 10;

    if (test_display_geometry_and_resolution()) passed++;
    if (test_iosurface_creation_and_pixel_access()) passed++;
    if (test_iosurface_rect_fill()) passed++;
    if (test_damage_tracker_bounding_box()) passed++;
    if (test_compositor_layer_addition_and_removal()) passed++;
    if (test_z_order_compositing()) passed++;
    if (test_alpha_blending()) passed++;
    if (test_directx_host_presentation_staging()) passed++;
    if (test_dirty_rect_optimized_presentation()) passed++;
    if (test_springboard_full_screen_ui_composition()) passed++;

    std::cout << "-----------------------------------------------------------------\n";
    std::cout << "Stage 127 Tests Complete: " << passed << " / " << total << " passed\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}

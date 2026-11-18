#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

#include "graphics/virtual_display.h"
#include "graphics/display_surface.h"
#include "graphics/compositor.h"
#include "renderer/presentation_pacer.h"
#include "runtime/interactive_runtime_contract.h"

using namespace winios;
using namespace winios::graphics;
using namespace winios::renderer;

void test_compositor_occlusion_culling() {
    std::cout << "[Test 1] Compositor occlusion culling...\n";
    DisplayConfiguration cfg{};
    cfg.width = 400;
    cfg.height = 400;
    cfg.row_stride = 400 * 4;
    auto display = std::make_shared<VirtualDisplay>(cfg);

    Compositor comp(display);

    // Surface 1: bottom window, 100x100 at (50, 50), red, z=1
    auto surf_bottom = std::make_shared<DisplaySurface>(1, 10, 100, 100);
    surf_bottom->set_position(50, 50);
    surf_bottom->set_z_order(1);
    surf_bottom->set_opacity(1.0f);
    surf_bottom->fill_rect(10, Rect{0, 0, 100, 100}, 0xFFFF0000u); // Red

    // Surface 2: top window, 200x200 at (0, 0), blue, z=2, fully covers Surface 1
    auto surf_top = std::make_shared<DisplaySurface>(2, 20, 200, 200);
    surf_top->set_position(0, 0);
    surf_top->set_z_order(2);
    surf_top->set_opacity(1.0f);
    surf_top->fill_rect(20, Rect{0, 0, 200, 200}, 0xFF0000FFu); // Blue

    comp.add_surface(surf_bottom);
    comp.add_surface(surf_top);

    bool ok = comp.composite(true);
    assert(ok);
    (void)ok;

    const auto& stats = comp.stats();
    // surf_bottom must be completely culled
    assert(stats.occluded_surfaces_culled == 1);
    assert(stats.occluded_pixels_saved == 100 * 100);
    (void)stats;

    // Verify display pixel at (60, 60): should be Blue (0xFF0000FF), NOT Red
    uint32_t px = display->read_pixel(60, 60);
    assert(px == 0xFF0000FFu);
    (void)px;

    std::cout << "[PASS] Occlusion culling verified.\n";
}

void test_compositor_partial_overlap() {
    std::cout << "[Test 2] Partial overlap non-culling...\n";
    DisplayConfiguration cfg{};
    cfg.width = 400;
    cfg.height = 400;
    cfg.row_stride = 400 * 4;
    auto display = std::make_shared<VirtualDisplay>(cfg);

    Compositor comp(display);

    // Surface 1: 100x100 at (50, 50), red, z=1
    auto surf1 = std::make_shared<DisplaySurface>(1, 10, 100, 100);
    surf1->set_position(50, 50);
    surf1->set_z_order(1);
    surf1->set_opacity(1.0f);
    surf1->fill_rect(10, Rect{0, 0, 100, 100}, 0xFFFF0000u);

    // Surface 2: 100x100 at (100, 50), green, z=2 (partially overlaps 50 pixels)
    auto surf2 = std::make_shared<DisplaySurface>(2, 20, 100, 100);
    surf2->set_position(100, 50);
    surf2->set_z_order(2);
    surf2->set_opacity(1.0f);
    surf2->fill_rect(20, Rect{0, 0, 100, 100}, 0xFF00FF00u);

    comp.add_surface(surf1);
    comp.add_surface(surf2);

    bool ok = comp.composite(true);
    assert(ok);
    (void)ok;

    const auto& stats = comp.stats();
    assert(stats.occluded_surfaces_culled == 0); // None fully occluded
    (void)stats;

    // At (60, 60), surface 1 is visible (Red)
    assert(display->read_pixel(60, 60) == 0xFFFF0000u);
    // At (120, 60), surface 2 is visible (Green)
    assert(display->read_pixel(120, 60) == 0xFF00FF00u);

    std::cout << "[PASS] Partial overlap rendering verified.\n";
}

void test_damage_aggregation() {
    std::cout << "[Test 3] Damage aggregation and partial frame redraw...\n";
    DisplayConfiguration cfg{};
    cfg.width = 500;
    cfg.height = 500;
    cfg.row_stride = 500 * 4;
    auto display = std::make_shared<VirtualDisplay>(cfg);

    Compositor comp(display);

    auto surf = std::make_shared<DisplaySurface>(1, 10, 200, 200);
    surf->set_position(100, 100);
    surf->fill_rect(10, Rect{0, 0, 200, 200}, 0xFF112233u);
    comp.add_surface(surf);

    comp.composite(true); // Full initial frame
    assert(comp.stats().full_redraw_frames == 1);

    // Now update only a small 20x20 rect on surface
    surf->fill_rect(10, Rect{10, 10, 20, 20}, 0xFFAABBCCu);
    comp.composite(false); // Damage-driven composite

    assert(comp.stats().partial_redraw_frames == 1);
    assert(display->read_pixel(115, 115) == 0xFFAABBCCu);

    // When no damage, frame is skipped
    comp.composite(false);
    assert(comp.stats().skipped_frames == 1);

    std::cout << "[PASS] Damage aggregation and skipped frames verified.\n";
}

void test_presentation_pacer_and_stats() {
    std::cout << "[Test 4] Presentation pacer metrics and contract struct...\n";
    PresentationPacer pacer;

    // Simulate 10 frames at 16,666,666 ns intervals (~60 FPS)
    uint64_t t = 1'000'000'000ULL;
    for (int i = 0; i < 10; ++i) {
        pacer.on_frame_presented(t);
        t += 16'666'666ULL;
    }
    pacer.on_frame_dropped();

    auto diag = pacer.diagnostics();
    assert(diag.total_frames == 10);
    assert(diag.dropped_frames == 1);
    assert(std::abs(diag.fps - 60.0) < 1.0);

    auto stats = pacer.presentation_stats(60);
    assert(stats.total_frames_presented == 10);
    assert(stats.total_frames_dropped == 1);
    assert(stats.refresh_rate_hz == 60);
    assert(stats.average_frame_time_us >= 16000 && stats.average_frame_time_us <= 17000);

    std::cout << "[PASS] Presentation pacer verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 222.11 & 222.12: COMPOSITOR & PACING TESTS\n";
    std::cout << "========================================\n";

    test_compositor_occlusion_culling();
    test_compositor_partial_overlap();
    test_damage_aggregation();
    test_presentation_pacer_and_stats();

    std::cout << "\nALL STAGE 222.11 & 222.12 TESTS PASSED (100%).\n";
    return 0;
}

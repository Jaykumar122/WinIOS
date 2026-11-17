// ============================================================================
// Stage 214.11: Rendering and Invalidation Optimization Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <vector>

#include "runtime/ui/render_context.h"
#include "runtime/ui/controls.h"

using namespace winios::runtime::ui;

namespace {

int g_failures = 0;

#define INVAL_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[INVAL_FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [INVAL_OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_invalidation_and_damage_tracking() {
    std::cout << "[Stage 214.11] Verifying Dirty Rect Unions and Invalidation...\n";

    std::vector<std::uint8_t> buffer(400 * 400 * 4, 0);
    RenderContext ctx(buffer.data(), 400, 400, 400 * 4);

    INVAL_CHECK(!ctx.has_damage(), "Context starts without damage");

    // Invalidate rect 1: [10, 10, 50, 50]
    ctx.invalidate({10, 10, 50, 50});
    INVAL_CHECK(ctx.has_damage(), "Context has damage");
    Rect d1 = ctx.dirty_bounds();
    INVAL_CHECK(d1.x == 10 && d1.y == 10 && d1.width == 50 && d1.height == 50, "Dirty bounds matches rect 1");

    // Invalidate rect 2: [100, 100, 50, 50]
    // Union should be from (10, 10) to (150, 150) -> width=140, height=140
    ctx.invalidate({100, 100, 50, 50});
    Rect d2 = ctx.dirty_bounds();
    INVAL_CHECK(d2.x == 10 && d2.y == 10 && d2.width == 140 && d2.height == 140, "Coalesced dirty union bounds [10,10,140,140]");

    ctx.clear_damage();
    INVAL_CHECK(!ctx.has_damage(), "Damage cleared cleanly");
}

void test_partial_redraw_equivalence() {
    std::cout << "[Stage 214.11] Verifying Partial Redraw Bit-for-Bit Equivalence...\n";

    const std::uint32_t w = 200;
    const std::uint32_t h = 200;
    const std::size_t buf_size = w * h * 4;

    std::vector<std::uint8_t> full_buffer(buf_size, 0);
    std::vector<std::uint8_t> partial_buffer(buf_size, 0);

    RenderContext full_ctx(full_buffer.data(), w, h, w * 4);
    RenderContext partial_ctx(partial_buffer.data(), w, h, w * 4);

    // Initial draw on both: background + button
    full_ctx.fill_rect({0, 0, 200, 200}, 0xFF000000);
    partial_ctx.fill_rect({0, 0, 200, 200}, 0xFF000000);

    Button btn("Tap Me", 1, 1);
    btn.set_frame({20, 20, 100, 40});
    btn.draw(full_ctx);
    btn.draw(partial_ctx);

    // Check both initial buffers match
    INVAL_CHECK(full_buffer == partial_buffer, "Initial buffers bit-for-bit identical");

    // Now button state changes: pressed!
    btn.set_pressed(true);

    // Full redraw: clear whole screen and draw
    full_ctx.fill_rect({0, 0, 200, 200}, 0xFF000000);
    btn.draw(full_ctx);

    // Partial redraw: clear only button's frame and draw with clip
    partial_ctx.push_clip(btn.frame());
    partial_ctx.fill_rect(btn.frame(), 0xFF000000);
    btn.draw(partial_ctx);
    partial_ctx.pop_clip();

    // Verify bit-for-bit equivalence between full and partial redraw
    INVAL_CHECK(full_buffer == partial_buffer,
                "Partial redraw produces identical pixel buffer to full redraw");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 214.11: Rendering Invalidation Tests  \n";
    std::cout << "====================================================\n";

    test_invalidation_and_damage_tracking();
    test_partial_redraw_equivalence();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL INVAL TESTS PASSED" : "INVAL FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

// ============================================================================
// Stage 238.9: UI Rendering Correctness and Geometry Validation Tests
// ============================================================================
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <cstring>

#include "tests/support/test_assert.h"

extern "C" {
#include "winios/ui/toolkit.h"

int32_t winios_surface_fill(winios_wid_t wid, uint32_t x, uint32_t y, uint32_t width, uint32_t height, winios_color_t color) {
    (void)wid; (void)x; (void)y; (void)width; (void)height; (void)color;
    return 0;
}
int32_t winios_surface_update(winios_wid_t wid, const void* pixels, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    (void)wid; (void)pixels; (void)x; (void)y; (void)width; (void)height;
    return 0;
}
}

namespace {

void test_checkbox_pixel_conformance() {
    std::cout << "[Test 1] Checkbox Pixel Conformance & Centered Fill...\n";
    constexpr uint32_t kStride = 200;
    constexpr uint32_t kHeight = 200;
    std::vector<uint32_t> fb(kStride * kHeight, 0);

    winios_checkbox_t cb;
    winios_checkbox_init(&cb, 10, 10, 100, 20, "TestCheck", 0);
    cb.box_color = 0xFFCCCCCCU;
    cb.check_color = 0xFF007AFFU;

    // 1. Unchecked rendering
    winios_checkbox_render(fb.data(), kStride, &cb);

    // Box geometry: x in [10, 23], y in [13, 26] (since height=20, box_sz=14, by = 10 + (20-14)/2 = 13)
    // Box border should be non-zero
    TEST_ASSERT(fb[13 * kStride + 10] == cb.box_color, "Top-left of box outline must match box_color");
    TEST_ASSERT(fb[26 * kStride + 23] == cb.box_color, "Bottom-right of box outline must match box_color");
    // Center pixel (15, 15) must be 0 when unchecked
    TEST_ASSERT(fb[15 * kStride + 15] == 0, "Center pixel must be empty when unchecked");

    // 2. Checked rendering
    std::fill(fb.begin(), fb.end(), 0);
    cb.checked = 1;
    winios_checkbox_render(fb.data(), kStride, &cb);

    // Center pixel (15, 15) MUST be non-zero (filled with check_color)
    TEST_ASSERT(fb[15 * kStride + 15] == cb.check_color, "Center pixel (15, 15) must match check_color");
    // Inner fill covers x in [12, 21], y in [15, 24]
    TEST_ASSERT(fb[15 * kStride + 12] == cb.check_color, "Inner fill start (12, 15) must match check_color");
    TEST_ASSERT(fb[24 * kStride + 21] == cb.check_color, "Inner fill end (21, 24) must match check_color");
    // Pixel outside box (e.g. 5, 5 or 30, 30) must be 0
    TEST_ASSERT(fb[5 * kStride + 5] == 0);

    std::cout << "  -> Passed checkbox pixel conformance test\n";
}

void test_small_and_extreme_control_bounds() {
    std::cout << "[Test 2] Small Controls & Extreme Dimension Bounds...\n";
    constexpr uint32_t kStride = 100;
    std::vector<uint32_t> fb(kStride * 100, 0);

    winios_checkbox_t small_cb;
    // Control smaller than box_sz (width=6, height=8 vs box_sz=14)
    winios_checkbox_init(&small_cb, 5, 5, 6, 8, "Tiny", 1);

    // Must not crash or underflow in (bounds.height - box_sz)
    winios_checkbox_render(fb.data(), kStride, &small_cb);
    TEST_ASSERT(fb[7 * kStride + 7] != 0, "Rendered into tiny control without crash");

    // Zero bounds
    winios_checkbox_t zero_cb;
    winios_checkbox_init(&zero_cb, 0, 0, 0, 0, "Zero", 1);
    winios_checkbox_render(fb.data(), kStride, &zero_cb);

    // Null safety
    winios_checkbox_render(nullptr, kStride, &small_cb);
    winios_checkbox_render(fb.data(), kStride, nullptr);

    std::cout << "  -> Passed small and extreme control bounds\n";
}

void test_deterministic_rendering() {
    std::cout << "[Test 3] Deterministic Framebuffer Rendering Invariance...\n";
    constexpr uint32_t kStride = 120;
    std::vector<uint32_t> fb1(kStride * 80, 0);
    std::vector<uint32_t> fb2(kStride * 80, 0);

    winios_checkbox_t cb;
    winios_checkbox_init(&cb, 20, 15, 80, 25, "Option A", 1);

    winios_checkbox_render(fb1.data(), kStride, &cb);
    winios_checkbox_render(fb2.data(), kStride, &cb);

    TEST_ASSERT(std::memcmp(fb1.data(), fb2.data(), fb1.size() * sizeof(uint32_t)) == 0,
                "Rendering with identical parameters must be bit-for-bit identical");
    std::cout << "  -> Passed deterministic rendering invariance\n";
}

} // namespace

int main() {
    std::cout << "========================================================\n";
    std::cout << " Stage 238.9: UI Rendering Correctness Suite            \n";
    std::cout << "========================================================\n";

    test_checkbox_pixel_conformance();
    test_small_and_extreme_control_bounds();
    test_deterministic_rendering();

    std::cout << "\nALL STAGE 238.9 TESTS COMPLETED SUCCESSFULLY!\n";
    return 0;
}

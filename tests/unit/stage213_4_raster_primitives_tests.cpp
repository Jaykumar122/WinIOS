// ============================================================================
// Stage 213.4: Pixel Formats and Raster Primitives Golden Tests
// ============================================================================
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "graphics/virtual_display.h"

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

void test_golden_alpha_blending() {
    std::cout << "[Test 1] Verify Porter-Duff Source-Over Alpha Blending Golden Values...\n";

    // Format: BGRA8888 -> [B, G, R, A]
    // 1. Fully opaque source replaces destination
    std::uint8_t dst1[4] = {255, 0, 0, 255}; // Solid blue
    std::uint8_t src1[4] = {0, 0, 255, 255}; // Solid red
    RasterPrimitives::blend_pixel(dst1, src1, PixelFormat::B8G8R8A8_UNORM);
    TEST_CHECK(dst1[0] == 0 && dst1[1] == 0 && dst1[2] == 255 && dst1[3] == 255,
               "Opaque red over opaque blue produces pure opaque red");

    // 2. Fully transparent source leaves destination untouched
    std::uint8_t dst2[4] = {100, 150, 200, 255};
    std::uint8_t src2[4] = {0, 0, 0, 0};
    RasterPrimitives::blend_pixel(dst2, src2, PixelFormat::B8G8R8A8_UNORM);
    TEST_CHECK(dst2[0] == 100 && dst2[1] == 150 && dst2[2] == 200 && dst2[3] == 255,
               "Transparent source leaves destination completely untouched");

    // 3. Exact 50% alpha blending golden test:
    // Destination: Solid Blue [255, 0, 0, 255]
    // Source: 50% Red [0, 0, 255, 128]
    // Expected:
    // B = (0 * 128 + 255 * 127 + 127) / 255 = 127
    // G = 0
    // R = (255 * 128 + 0 * 127 + 127) / 255 = 128
    // A = 128 + (255 * 127 + 127) / 255 = 255
    std::uint8_t dst3[4] = {255, 0, 0, 255};
    std::uint8_t src3[4] = {0, 0, 255, 128};
    RasterPrimitives::blend_pixel(dst3, src3, PixelFormat::B8G8R8A8_UNORM);
    TEST_CHECK(dst3[0] == 127, "Golden Blue channel matches 127");
    TEST_CHECK(dst3[1] == 0, "Golden Green channel matches 0");
    TEST_CHECK(dst3[2] == 128, "Golden Red channel matches 128");
    TEST_CHECK(dst3[3] == 255, "Golden Alpha channel matches 255");

    // 4. Opacity scaling
    std::uint8_t dst4[4] = {255, 0, 0, 255};
    std::uint8_t src4[4] = {0, 0, 255, 255}; // 100% opaque red, blended at 0.5f opacity
    RasterPrimitives::blend_pixel(dst4, src4, PixelFormat::B8G8R8A8_UNORM, 0.5f);
    TEST_CHECK(dst4[2] >= 126 && dst4[2] <= 129, "Red channel scaled by 0.5 opacity is ~128");
    TEST_CHECK(dst4[0] >= 126 && dst4[0] <= 129, "Blue channel scaled by 0.5 opacity is ~127");
}

void test_clipping_primitives() {
    std::cout << "[Test 2] Verify Geometric Clipping Primitives...\n";

    Rect bounds{10, 10, 100, 100};

    // Target completely inside
    Rect r1{20, 20, 30, 30};
    Rect c1 = RasterPrimitives::clip_rect(r1, bounds);
    TEST_CHECK(c1.x == 20 && c1.y == 20 && c1.width == 30 && c1.height == 30, "Inside rect unchanged by clip");

    // Target overlapping top-left
    Rect r2{0, 0, 50, 50};
    Rect c2 = RasterPrimitives::clip_rect(r2, bounds);
    TEST_CHECK(c2.x == 10 && c2.y == 10 && c2.width == 40 && c2.height == 40, "Overlapping top-left clipped to [10,10,40,40]");

    // Target overlapping bottom-right
    Rect r3{80, 80, 50, 50};
    Rect c3 = RasterPrimitives::clip_rect(r3, bounds);
    TEST_CHECK(c3.x == 80 && c3.y == 80 && c3.width == 30 && c3.height == 30, "Overlapping bottom-right clipped to [80,80,30,30]");

    // Target completely outside
    Rect r4{200, 200, 50, 50};
    Rect c4 = RasterPrimitives::clip_rect(r4, bounds);
    TEST_CHECK(c4.empty(), "Completely disjoint rect clipped to empty");
}

void test_golden_blit_and_image_copy() {
    std::cout << "[Test 3] Verify Golden Blit Image Copy with Alpha and Sub-Region Clipping...\n";

    constexpr std::uint32_t kW = 20;
    constexpr std::uint32_t kH = 20;
    std::vector<std::uint8_t> dst(kW * kH * 4, 0);

    // Fill destination with black (0xFF000000 in BGRA -> B=0, G=0, R=0, A=255)
    for (std::size_t i = 0; i < kW * kH; ++i) {
        dst[i * 4 + 3] = 255;
    }

    // Create 10x10 source pattern: solid green (B=0, G=255, R=0, A=255)
    constexpr std::uint32_t kSrcW = 10;
    constexpr std::uint32_t kSrcH = 10;
    std::vector<std::uint8_t> src(kSrcW * kSrcH * 4, 0);
    for (std::size_t i = 0; i < kSrcW * kSrcH; ++i) {
        src[i * 4 + 1] = 255; // Green
        src[i * 4 + 3] = 255; // Alpha
    }

    // Blit source at dst offset (5, 5) with source rect (0, 0, 10, 10)
    RasterPrimitives::blit(
        dst.data(), kW, kH, kW * 4, PixelFormat::B8G8R8A8_UNORM,
        src.data(), kSrcW, kSrcH, kSrcW * 4, PixelFormat::B8G8R8A8_UNORM,
        5, 5, Rect{0, 0, kSrcW, kSrcH});

    // Verify blitted region (5, 5) through (14, 14) has green pixels
    std::size_t px_offset = (5 * kW * 4) + (5 * 4);
    TEST_CHECK(dst[px_offset + 1] == 255, "Destination (5,5) Green channel is 255");
    TEST_CHECK(dst[px_offset + 0] == 0, "Destination (5,5) Blue channel is 0");
    TEST_CHECK(dst[px_offset + 2] == 0, "Destination (5,5) Red channel is 0");

    // Verify outside region (4, 4) remains black
    std::size_t outside_offset = (4 * kW * 4) + (4 * 4);
    TEST_CHECK(dst[outside_offset + 1] == 0, "Destination (4,4) outside blit area remains untouched");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 213.4: Pixel Formats & Raster Tests   \n";
    std::cout << "====================================================\n";

    test_golden_alpha_blending();
    test_clipping_primitives();
    test_golden_blit_and_image_copy();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL TESTS PASSED" : "FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

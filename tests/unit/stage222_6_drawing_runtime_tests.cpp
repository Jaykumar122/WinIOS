#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include "winios/ui/graphics.h"
#include "winios/ui/font8x8.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

// Test software frame buffer simulating a 200x200 ARGB surface
static uint32_t s_test_fb[200 * 200];
static int s_fill_count = 0;
static int s_update_count = 0;

// Host emulation hooks for guest drawing runtime unit tests
extern "C" {
int32_t winios_surface_fill(winios_wid_t wid, uint32_t x, uint32_t y, uint32_t width, uint32_t height, winios_color_t color) {
    (void)wid;
    ++s_fill_count;
    for (uint32_t row = 0; row < height; ++row) {
        uint32_t cy = y + row;
        if (cy >= 200) break;
        for (uint32_t col = 0; col < width; ++col) {
            uint32_t cx = x + col;
            if (cx >= 200) break;
            s_test_fb[cy * 200 + cx] = color;
        }
    }
    return 0;
}

int32_t winios_surface_update(winios_wid_t wid, const void* pixels, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    (void)wid;
    ++s_update_count;
    const uint32_t* src = (const uint32_t*)pixels;
    for (uint32_t row = 0; row < height; ++row) {
        uint32_t cy = y + row;
        if (cy >= 200) break;
        for (uint32_t col = 0; col < width; ++col) {
            uint32_t cx = x + col;
            if (cx >= 200) break;
            s_test_fb[cy * 200 + cx] = src[row * width + col];
        }
    }
    return 0;
}
}

void test_font8x8_bitmap_structure() {
    std::cout << "[Test 1] 8x8 font bitmap glyph verification...\n";
    // Verify space is blank
    const uint8_t* space_glyph = winios_font8x8_data[' ' - 32];
    for (int i = 0; i < 8; ++i) {
        TEST_ASSERT(space_glyph[i] == 0x00);
    }

    // Verify '!' has top dot and bottom point
    const uint8_t* excl_glyph = winios_font8x8_data['!' - 32];
    TEST_ASSERT(excl_glyph[0] == 0x18);
    TEST_ASSERT(excl_glyph[5] == 0x00);
    TEST_ASSERT(excl_glyph[6] == 0x18);

    // Verify 'A' top bar and crossbar
    const uint8_t* a_glyph = winios_font8x8_data['A' - 32];
    TEST_ASSERT(a_glyph[0] == 0x18);
    TEST_ASSERT(a_glyph[4] == 0x7E); // crossbar

    std::cout << "[PASS] Font bitmap verified.\n";
}

void test_line_drawing() {
    std::cout << "[Test 2] Bresenham and fast run line drawing...\n";
    std::memset(s_test_fb, 0, sizeof(s_test_fb));

    // Horizontal line
    winios_draw_line(1, 10, 20, 50, 20, WINIOS_COLOR_BLUE);
    TEST_ASSERT(s_test_fb[20 * 200 + 10] == WINIOS_COLOR_BLUE);
    TEST_ASSERT(s_test_fb[20 * 200 + 30] == WINIOS_COLOR_BLUE);
    TEST_ASSERT(s_test_fb[20 * 200 + 50] == WINIOS_COLOR_BLUE);
    TEST_ASSERT(s_test_fb[20 * 200 + 51] == 0); // Not drawn past end

    // Vertical line
    winios_draw_line(1, 40, 50, 40, 80, WINIOS_COLOR_RED);
    TEST_ASSERT(s_test_fb[50 * 200 + 40] == WINIOS_COLOR_RED);
    TEST_ASSERT(s_test_fb[65 * 200 + 40] == WINIOS_COLOR_RED);
    TEST_ASSERT(s_test_fb[80 * 200 + 40] == WINIOS_COLOR_RED);

    // Diagonal line
    winios_draw_line(1, 100, 100, 110, 110, WINIOS_COLOR_GREEN);
    TEST_ASSERT(s_test_fb[100 * 200 + 100] == WINIOS_COLOR_GREEN);
    TEST_ASSERT(s_test_fb[105 * 200 + 105] == WINIOS_COLOR_GREEN);
    TEST_ASSERT(s_test_fb[110 * 200 + 110] == WINIOS_COLOR_GREEN);

    std::cout << "[PASS] Line drawing verified.\n";
}

void test_rect_and_text_drawing() {
    std::cout << "[Test 3] Rectangle and text rendering...\n";
    std::memset(s_test_fb, 0, sizeof(s_test_fb));

    // Filled rectangle
    winios_draw_rect(1, 10, 10, 30, 20, WINIOS_COLOR_ORANGE, 1);
    TEST_ASSERT(s_test_fb[10 * 200 + 10] == WINIOS_COLOR_ORANGE);
    TEST_ASSERT(s_test_fb[20 * 200 + 25] == WINIOS_COLOR_ORANGE);
    TEST_ASSERT(s_test_fb[29 * 200 + 39] == WINIOS_COLOR_ORANGE);
    TEST_ASSERT(s_test_fb[30 * 200 + 40] == 0); // Outside

    // Outline rectangle
    winios_draw_rect(1, 50, 50, 20, 20, WINIOS_COLOR_WHITE, 0);
    TEST_ASSERT(s_test_fb[50 * 200 + 50] == WINIOS_COLOR_WHITE); // top-left
    TEST_ASSERT(s_test_fb[50 * 200 + 69] == WINIOS_COLOR_WHITE); // top-right
    TEST_ASSERT(s_test_fb[69 * 200 + 50] == WINIOS_COLOR_WHITE); // bottom-left
    TEST_ASSERT(s_test_fb[69 * 200 + 69] == WINIOS_COLOR_WHITE); // bottom-right
    TEST_ASSERT(s_test_fb[60 * 200 + 60] == 0); // Interior is empty

    // Text rendering: "A"
    winios_draw_text(1, 100, 50, "A", WINIOS_COLOR_RED);
    // Row 4 of 'A' has crossbar 0x7E: (100+1 to 100+6)
    TEST_ASSERT(s_test_fb[(50 + 4) * 200 + (100 + 3)] == WINIOS_COLOR_RED);

    // Blit test
    std::vector<uint32_t> sprite(16 * 16, WINIOS_COLOR_GREEN);
    winios_blit(1, sprite.data(), 150, 150, 16, 16, 16 * 4);
    TEST_ASSERT(s_test_fb[150 * 200 + 150] == WINIOS_COLOR_GREEN);
    TEST_ASSERT(s_test_fb[165 * 200 + 165] == WINIOS_COLOR_GREEN);

    std::cout << "[PASS] Rect and text drawing verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 222.6: 2D DRAWING RUNTIME TESTS\n";
    std::cout << "========================================\n";

    test_font8x8_bitmap_structure();
    test_line_drawing();
    test_rect_and_text_drawing();

    std::cout << "\nALL STAGE 222.6 TESTS PASSED (100%).\n";
    return 0;
}

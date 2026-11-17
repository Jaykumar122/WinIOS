// ============================================================================
// Stage 213.3: Framebuffer Memory & Safe Access Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <memory>
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

void test_bounds_checked_pixel_access() {
    std::cout << "[Test 1] Verify Bounds-Checked Pixel Reads and Writes...\n";

    DisplayConfiguration cfg{};
    cfg.width = 400;
    cfg.height = 300;
    cfg.row_stride = 400 * 4;

    VirtualDisplay disp(cfg);

    // Write within bounds
    TEST_CHECK(disp.write_pixel(50, 50, 0xAABBCCDDu), "write_pixel(50, 50) within bounds succeeds");
    TEST_CHECK(disp.read_pixel(50, 50) == 0xAABBCCDDu, "read_pixel(50, 50) returns written color 0xAABBCCDD");

    // Write at boundary
    TEST_CHECK(disp.write_pixel(399, 299, 0x11223344u), "write_pixel(399, 299) at max edge succeeds");
    TEST_CHECK(disp.read_pixel(399, 299) == 0x11223344u, "read_pixel(399, 299) returns 0x11223344");

    // Out-of-bounds writes must fail safely without memory corruption
    TEST_CHECK(!disp.write_pixel(400, 50, 0xFF00FF00u), "write_pixel(400, 50) out of bounds rejected");
    TEST_CHECK(!disp.write_pixel(50, 300, 0xFF00FF00u), "write_pixel(50, 300) out of bounds rejected");
    TEST_CHECK(!disp.write_pixel(9999, 9999, 0xFF00FF00u), "write_pixel(9999, 9999) out of bounds rejected");

    // Out-of-bounds reads return 0
    TEST_CHECK(disp.read_pixel(400, 50) == 0, "read_pixel(400, 50) out of bounds returns 0");
    TEST_CHECK(disp.read_pixel(50, 300) == 0, "read_pixel(50, 300) out of bounds returns 0");
}

void test_clear_and_fill_rect() {
    std::cout << "[Test 2] Verify Clear & Fill Rect Operations...\n";

    DisplayConfiguration cfg{};
    cfg.width = 100;
    cfg.height = 100;
    cfg.row_stride = 100 * 4;

    VirtualDisplay disp(cfg);

    // Clear with solid blue
    disp.clear(0xFF0000FFu);
    TEST_CHECK(disp.read_pixel(0, 0) == 0xFF0000FFu, "Pixel (0,0) has cleared blue color");
    TEST_CHECK(disp.read_pixel(50, 50) == 0xFF0000FFu, "Pixel (50,50) has cleared blue color");
    TEST_CHECK(disp.read_pixel(99, 99) == 0xFF0000FFu, "Pixel (99,99) has cleared blue color");

    // Fill centered rectangle (20, 20, 60, 60) with solid red
    disp.fill_rect(Rect{20, 20, 60, 60}, 0xFFFF0000u);

    // Inside rect: red
    TEST_CHECK(disp.read_pixel(20, 20) == 0xFFFF0000u, "Pixel (20,20) inside fill rect is red");
    TEST_CHECK(disp.read_pixel(50, 50) == 0xFFFF0000u, "Pixel (50,50) inside fill rect is red");
    TEST_CHECK(disp.read_pixel(79, 79) == 0xFFFF0000u, "Pixel (79,79) inside fill rect is red");

    // Outside rect: remains blue
    TEST_CHECK(disp.read_pixel(10, 10) == 0xFF0000FFu, "Pixel (10,10) outside fill rect remains blue");
    TEST_CHECK(disp.read_pixel(85, 85) == 0xFF0000FFu, "Pixel (85,85) outside fill rect remains blue");
}

void test_safe_buffer_copying() {
    std::cout << "[Test 3] Verify Safe Buffer Copying, Bounds Checking & Stride Handling...\n";

    DisplayConfiguration cfg{};
    cfg.width = 50;
    cfg.height = 50;
    cfg.row_stride = 50 * 4;

    VirtualDisplay disp(cfg);
    disp.clear(0xAA55AA55u);

    // Attempt copy_to with buffer that is too small -> must fail
    std::vector<std::uint8_t> small_buf(100);
    TEST_CHECK(!disp.copy_to(small_buf.data(), small_buf.size()), "copy_to with undersized buffer rejected");

    // Attempt copy_from with buffer that is too small -> must fail
    TEST_CHECK(!disp.copy_from(small_buf.data(), small_buf.size()), "copy_from with undersized buffer rejected");

    // Full buffer copy_to
    std::vector<std::uint8_t> full_buf(disp.framebuffer_size());
    TEST_CHECK(disp.copy_to(full_buf.data(), full_buf.size()), "copy_to with matching size succeeds");

    std::uint32_t first_px = 0;
    std::memcpy(&first_px, full_buf.data(), 4);
    TEST_CHECK(first_px == 0xAA55AA55u, "First pixel in copied buffer matches framebuffer");

    // Modify buffer and copy back
    std::uint32_t new_px = 0x12345678u;
    std::memcpy(full_buf.data(), &new_px, 4);
    TEST_CHECK(disp.copy_from(full_buf.data(), full_buf.size()), "copy_from with matching size succeeds");
    TEST_CHECK(disp.read_pixel(0, 0) == 0x12345678u, "Pixel (0,0) updated from copy_from");
}

void test_pixel_format_conversion() {
    std::cout << "[Test 4] Verify BGRA <-> RGBA and RGB565 Format Conversions...\n";

    // RGBA: R=0xFF, G=0x80, B=0x20, A=0xFF
    std::uint8_t src_rgba[4] = {0xFF, 0x80, 0x20, 0xFF};
    std::uint8_t dst_bgra[4] = {0, 0, 0, 0};

    bool conv_res = VirtualDisplay::convert_format(
        src_rgba, PixelFormat::R8G8B8A8_UNORM,
        dst_bgra, PixelFormat::B8G8R8A8_UNORM,
        1);
    TEST_CHECK(conv_res, "convert_format RGBA -> BGRA succeeds");
    TEST_CHECK(dst_bgra[0] == 0x20, "Blue channel mapped to index 0");
    TEST_CHECK(dst_bgra[1] == 0x80, "Green channel mapped to index 1");
    TEST_CHECK(dst_bgra[2] == 0xFF, "Red channel mapped to index 2");
    TEST_CHECK(dst_bgra[3] == 0xFF, "Alpha channel mapped to index 3");

    // BGRA -> RGB565
    std::uint8_t dst_rgb565[2] = {0, 0};
    conv_res = VirtualDisplay::convert_format(
        dst_bgra, PixelFormat::B8G8R8A8_UNORM,
        dst_rgb565, PixelFormat::R5G6B5_UNORM,
        1);
    TEST_CHECK(conv_res, "convert_format BGRA -> RGB565 succeeds");

    std::uint16_t raw_565 = 0;
    std::memcpy(&raw_565, dst_rgb565, 2);
    TEST_CHECK(raw_565 != 0, "RGB565 value is non-zero");

    // RGB565 -> BGRA
    std::uint8_t restored_bgra[4] = {0, 0, 0, 0};
    conv_res = VirtualDisplay::convert_format(
        dst_rgb565, PixelFormat::R5G6B5_UNORM,
        restored_bgra, PixelFormat::B8G8R8A8_UNORM,
        1);
    TEST_CHECK(conv_res, "convert_format RGB565 -> BGRA succeeds");
    // Verify restored values within 5-bit/6-bit precision tolerance (+- 8)
    TEST_CHECK(std::abs(restored_bgra[2] - 0xFF) <= 8, "Restored Red channel matches original within tolerance");
    TEST_CHECK(std::abs(restored_bgra[1] - 0x80) <= 8, "Restored Green channel matches original within tolerance");
    TEST_CHECK(std::abs(restored_bgra[0] - 0x20) <= 8, "Restored Blue channel matches original within tolerance");
}

void test_guest_memory_ownership() {
    std::cout << "[Test 5] Verify Guest Physical Address Mapping & Ownership...\n";

    DisplayConfiguration cfg{};
    VirtualDisplay disp(cfg);

    TEST_CHECK(disp.guest_physical_address() == 0x240000000ULL, "Default guest physical address is 0x240000000");

    disp.set_guest_physical_address(0x300000000ULL);
    TEST_CHECK(disp.guest_physical_address() == 0x300000000ULL, "Guest physical address updated to 0x300000000");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 213.3: Framebuffer Memory Tests       \n";
    std::cout << "====================================================\n";

    test_bounds_checked_pixel_access();
    test_clear_and_fill_rect();
    test_safe_buffer_copying();
    test_pixel_format_conversion();
    test_guest_memory_ownership();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL TESTS PASSED" : "FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

// ============================================================================
// Stage 213.2: Virtual Display Architecture Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>

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

void test_default_iphone15_configuration() {
    std::cout << "[Test 1] Verify Default iPhone 15 Portrait Profile Geometry & Defaults...\n";

    DisplayConfiguration config{};
    TEST_CHECK(config.validate(), "Default configuration validates successfully");
    TEST_CHECK(config.validate_error() == DisplayError::Success, "validate_error returns Success");

    TEST_CHECK(config.width == 1179u, "Default width is 1179");
    TEST_CHECK(config.height == 2556u, "Default height is 2556");
    TEST_CHECK(config.orientation == DisplayOrientation::Portrait, "Default orientation is Portrait");
    TEST_CHECK(!is_landscape(config.orientation), "Default orientation is not landscape");
    TEST_CHECK(config.pixel_format == PixelFormat::B8G8R8A8_UNORM, "Default format is BGRA8888");
    TEST_CHECK(config.bytes_per_pixel == 4u, "Default bytes_per_pixel is 4");
    TEST_CHECK(config.row_stride == 1179u * 4u, "Default row_stride is 1179 * 4 = 4716");
    TEST_CHECK(config.refresh_interval_us == 16666u, "Default refresh is ~60Hz (16666 us)");
    TEST_CHECK(config.display_scale == 3.0f, "Default display_scale is 3.0 (Retina)");

    std::uint64_t expected_bytes = static_cast<std::uint64_t>(config.row_stride) * config.height;
    TEST_CHECK(config.total_framebuffer_bytes() == expected_bytes, "Framebuffer total bytes matches row_stride * height");
    TEST_CHECK(expected_bytes == 12054096ULL, "Total bytes is 12,054,096 bytes (~12 MB)");
}

void test_orientation_switching() {
    std::cout << "[Test 2] Verify Portrait <-> Landscape Dynamic Geometry Swapping...\n";

    DisplayConfiguration config{};
    config.set_orientation(DisplayOrientation::LandscapeRight);

    TEST_CHECK(config.orientation == DisplayOrientation::LandscapeRight, "Orientation set to LandscapeRight");
    TEST_CHECK(is_landscape(config.orientation), "Orientation recognized as landscape");
    TEST_CHECK(config.width == 2556u, "Width swapped to 2556 in landscape");
    TEST_CHECK(config.height == 1179u, "Height swapped to 1179 in landscape");
    TEST_CHECK(config.row_stride == 2556u * 4u, "Row stride updated to 2556 * 4");
    TEST_CHECK(config.validate(), "Landscape configuration remains valid");

    // Swap back to Portrait
    config.set_orientation(DisplayOrientation::Portrait);
    TEST_CHECK(config.width == 1179u, "Width swapped back to 1179 in portrait");
    TEST_CHECK(config.height == 2556u, "Height swapped back to 2556 in portrait");
    TEST_CHECK(config.row_stride == 1179u * 4u, "Row stride restored to 4716");
    TEST_CHECK(config.validate(), "Portrait configuration remains valid");
}

void test_geometry_validation_and_bounds() {
    std::cout << "[Test 3] Verify Bounds Checking, Zero Dimensions and Invalid Configurations...\n";

    // Zero width
    DisplayConfiguration zero_w{};
    zero_w.width = 0;
    TEST_CHECK(!zero_w.validate(), "Zero width rejected");
    TEST_CHECK(zero_w.validate_error() == DisplayError::InvalidDimensions, "Zero width error is InvalidDimensions");

    // Zero height
    DisplayConfiguration zero_h{};
    zero_h.height = 0;
    TEST_CHECK(!zero_h.validate(), "Zero height rejected");
    TEST_CHECK(zero_h.validate_error() == DisplayError::InvalidDimensions, "Zero height error is InvalidDimensions");

    // Extreme dimensions exceeding 8192 max
    DisplayConfiguration huge{};
    huge.width = 16384;
    TEST_CHECK(!huge.validate(), "16384 width rejected");
    TEST_CHECK(huge.validate_error() == DisplayError::InvalidDimensions, "Excessive dimension error is InvalidDimensions");

    // Invalid row stride smaller than width * bpp
    DisplayConfiguration bad_stride{};
    bad_stride.width = 1000;
    bad_stride.bytes_per_pixel = 4;
    bad_stride.row_stride = 2000; // should be at least 4000
    TEST_CHECK(!bad_stride.validate(), "Stride smaller than width * bpp rejected");
    TEST_CHECK(bad_stride.validate_error() == DisplayError::InvalidStride, "Error code is InvalidStride");

    // Allocation exceeding budget limit
    DisplayConfiguration oom_config{};
    oom_config.width = 8000;
    oom_config.height = 8000;
    oom_config.row_stride = 8000 * 4;
    oom_config.max_allocation_size = 64 * 1024 * 1024; // 64 MB
    // 8000 * 8000 * 4 = 256 MB > 64 MB
    TEST_CHECK(!oom_config.validate(), "256 MB allocation rejected against 64 MB limit");
    TEST_CHECK(oom_config.validate_error() == DisplayError::AllocationExceeded, "Error is AllocationExceeded");
}

void test_virtual_display_lifecycle() {
    std::cout << "[Test 4] Verify VirtualDisplay Instantiation, Reconfiguration and Querying...\n";

    DisplayConfiguration initial{};
    initial.width = 800;
    initial.height = 600;
    initial.row_stride = 800 * 4;

    VirtualDisplay disp(initial);
    TEST_CHECK(disp.width() == 800, "VirtualDisplay width initialized to 800");
    TEST_CHECK(disp.height() == 600, "VirtualDisplay height initialized to 600");
    TEST_CHECK(disp.stride() == 3200, "VirtualDisplay stride initialized to 3200");
    TEST_CHECK(disp.framebuffer_size() == 800 * 600 * 4, "Framebuffer size matches 800*600*4");
    TEST_CHECK(disp.raw_data() != nullptr, "Framebuffer backing buffer is allocated and non-null");

    // Valid reconfiguration
    DisplayConfiguration reconfig{};
    reconfig.width = 1179;
    reconfig.height = 2556;
    reconfig.row_stride = 1179 * 4;
    TEST_CHECK(disp.reconfigure(reconfig), "Valid reconfigure returns true");
    TEST_CHECK(disp.width() == 1179, "VirtualDisplay width updated to 1179");
    TEST_CHECK(disp.height() == 2556, "VirtualDisplay height updated to 2556");

    // Invalid reconfiguration rejected and previous state preserved
    DisplayConfiguration invalid_cfg{};
    invalid_cfg.width = 0;
    TEST_CHECK(!disp.reconfigure(invalid_cfg), "Invalid reconfigure returns false");
    TEST_CHECK(disp.width() == 1179, "VirtualDisplay preserved width 1179");

    // Orientation change
    TEST_CHECK(disp.set_orientation(DisplayOrientation::LandscapeLeft), "set_orientation to LandscapeLeft succeeds");
    TEST_CHECK(disp.width() == 2556, "Width updated to 2556 after landscape switch");
    TEST_CHECK(disp.height() == 1179, "Height updated to 1179 after landscape switch");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 213.2: Virtual Display Architecture   \n";
    std::cout << "====================================================\n";

    test_default_iphone15_configuration();
    test_orientation_switching();
    test_geometry_validation_and_bounds();
    test_virtual_display_lifecycle();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL TESTS PASSED" : "FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

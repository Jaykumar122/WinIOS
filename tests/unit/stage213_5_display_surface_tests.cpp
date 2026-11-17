// ============================================================================
// Stage 213.5: Display Surface Architecture and Access Control Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "graphics/display_surface.h"

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

void test_surface_creation_and_geometry() {
    std::cout << "[Test 1] Verify DisplaySurface Geometry and Initial State...\n";

    constexpr std::uint32_t kOwnerPid = 101;
    DisplaySurface surf(1, kOwnerPid, 300, 200, PixelFormat::B8G8R8A8_UNORM);

    TEST_CHECK(surf.id() == 1, "Surface ID matches 1");
    TEST_CHECK(surf.owner_pid() == kOwnerPid, "Owner PID matches 101");
    TEST_CHECK(surf.width() == 300, "Width is 300");
    TEST_CHECK(surf.height() == 200, "Height is 200");
    TEST_CHECK(surf.stride() == 300 * 4, "Stride is 1200");
    TEST_CHECK(surf.size_bytes() == 300 * 200 * 4, "Size bytes is 240,000");
    TEST_CHECK(surf.is_visible(), "Surface visible by default");
    TEST_CHECK(surf.opacity() == 1.0f, "Opacity is 1.0 by default");
    TEST_CHECK(surf.damage_tracker().has_damage(), "New surface has initial full damage");
}

void test_process_access_control_and_isolation() {
    std::cout << "[Test 2] Verify Cross-Process Access Isolation and Authorization...\n";

    constexpr std::uint32_t kOwnerPid = 42;
    constexpr std::uint32_t kUnauthorizedPid = 99;
    constexpr std::uint32_t kKernelPid = 0;

    DisplaySurface surf(10, kOwnerPid, 100, 100, PixelFormat::B8G8R8A8_UNORM);

    // 1. Unauthorized process (PID 99) write must be blocked
    auto res = surf.write_pixel(kUnauthorizedPid, 10, 10, 0xFFFFFFFF);
    TEST_CHECK(res == SurfaceResultCode::AccessDenied, "PID 99 write_pixel blocked with AccessDenied");

    // 2. Unauthorized process read must be blocked
    std::uint32_t read_color = 0;
    res = surf.read_pixel(kUnauthorizedPid, 10, 10, read_color);
    TEST_CHECK(res == SurfaceResultCode::AccessDenied, "PID 99 read_pixel blocked with AccessDenied");

    // 3. Unauthorized fill_rect blocked
    res = surf.fill_rect(kUnauthorizedPid, Rect{0, 0, 50, 50}, 0xFF00FF00);
    TEST_CHECK(res == SurfaceResultCode::AccessDenied, "PID 99 fill_rect blocked with AccessDenied");

    // 4. Owner process (PID 42) write succeeds
    res = surf.write_pixel(kOwnerPid, 10, 10, 0x12345678);
    TEST_CHECK(res == SurfaceResultCode::Success, "Owner PID 42 write_pixel succeeds");

    // 5. Owner read succeeds
    res = surf.read_pixel(kOwnerPid, 10, 10, read_color);
    TEST_CHECK(res == SurfaceResultCode::Success, "Owner PID 42 read_pixel succeeds");
    TEST_CHECK(read_color == 0x12345678, "Read color matches written color 0x12345678");

    // 6. Kernel / Compositor (PID 0) has access
    res = surf.read_pixel(kKernelPid, 10, 10, read_color);
    TEST_CHECK(res == SurfaceResultCode::Success, "Kernel PID 0 read_pixel succeeds");
    TEST_CHECK(read_color == 0x12345678, "Kernel read matches written color");
}

void test_bounds_protection() {
    std::cout << "[Test 3] Verify Out-of-Bounds Memory Protection...\n";

    constexpr std::uint32_t kOwnerPid = 10;
    DisplaySurface surf(2, kOwnerPid, 50, 50);

    // Out-of-bounds write
    auto res = surf.write_pixel(kOwnerPid, 50, 20, 0xFF00FF00);
    TEST_CHECK(res == SurfaceResultCode::OutOfBounds, "Writing at x=50 (width=50) rejected with OutOfBounds");

    res = surf.write_pixel(kOwnerPid, 20, 50, 0xFF00FF00);
    TEST_CHECK(res == SurfaceResultCode::OutOfBounds, "Writing at y=50 (height=50) rejected with OutOfBounds");

    // Out-of-bounds read
    std::uint32_t col = 0;
    res = surf.read_pixel(kOwnerPid, 100, 100, col);
    TEST_CHECK(res == SurfaceResultCode::OutOfBounds, "Reading at (100, 100) rejected with OutOfBounds");
}

void test_position_and_layering() {
    std::cout << "[Test 4] Verify Surface Position, Z-Order and Damage Propagation...\n";

    DisplaySurface surf(3, 1, 100, 100);
    surf.damage_tracker().clear();

    surf.set_position(50, 100);
    TEST_CHECK(surf.pos_x() == 50, "Position X updated to 50");
    TEST_CHECK(surf.pos_y() == 100, "Position Y updated to 100");
    TEST_CHECK(surf.damage_tracker().has_damage(), "Moving surface marks damage");

    Rect b = surf.bounds();
    TEST_CHECK(b.x == 50 && b.y == 100 && b.width == 100 && b.height == 100, "Bounds rectangle matches [50, 100, 100, 100]");

    surf.set_z_order(5);
    TEST_CHECK(surf.z_order() == 5, "Z-order set to 5");

    surf.set_opacity(0.75f);
    TEST_CHECK(std::abs(surf.opacity() - 0.75f) < 0.001f, "Opacity set to 0.75");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 213.5: Display Surface Tests          \n";
    std::cout << "====================================================\n";

    test_surface_creation_and_geometry();
    test_process_access_control_and_isolation();
    test_bounds_protection();
    test_position_and_layering();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL TESTS PASSED" : "FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

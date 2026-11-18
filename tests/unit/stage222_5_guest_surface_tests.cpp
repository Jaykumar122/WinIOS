#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#include "graphics/display_surface.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::graphics;

void test_surface_geometry_and_stride() {
    std::cout << "[Test 1] Surface stride and framebuffer geometry...\n";
    DisplaySurface surf(1, 100, 320, 240, PixelFormat::B8G8R8A8_UNORM);

    TEST_ASSERT(surf.width() == 320);
    TEST_ASSERT(surf.height() == 240);
    TEST_ASSERT(surf.stride() == 320 * 4);
    TEST_ASSERT(surf.size_bytes() == 320 * 240 * 4);
    TEST_ASSERT(surf.format() == PixelFormat::B8G8R8A8_UNORM);

    // Dynamic resize
    surf.resize(640, 480);
    TEST_ASSERT(surf.width() == 640);
    TEST_ASSERT(surf.height() == 480);
    TEST_ASSERT(surf.stride() == 640 * 4);
    TEST_ASSERT(surf.size_bytes() == 640 * 480 * 4);

    std::cout << "[PASS] Geometry and stride verified.\n";
}

void test_surface_damage_tracking() {
    std::cout << "[Test 2] Surface damage tracking...\n";
    DisplaySurface surf(2, 100, 400, 300);

    // Initial surface has full damage
    TEST_ASSERT(surf.damage_tracker().has_damage());
    surf.clear_damage();
    TEST_ASSERT(!surf.damage_tracker().has_damage());

    // Add damage
    surf.damage_tracker().add_dirty_rect(Rect{10, 10, 50, 50});
    TEST_ASSERT(surf.damage_tracker().has_damage());
    auto dmg = surf.damage_tracker().bounding_box();
    TEST_ASSERT(dmg.x <= 10 && dmg.y <= 10);
    TEST_ASSERT(dmg.width >= 50 && dmg.height >= 50);

    // Add more damage and verify union
    surf.damage_tracker().add_dirty_rect(Rect{100, 100, 20, 20});
    auto union_dmg = surf.damage_tracker().bounding_box();
    TEST_ASSERT(union_dmg.x <= 10 && union_dmg.y <= 10);
    TEST_ASSERT(union_dmg.x + union_dmg.width >= 120);
    TEST_ASSERT(union_dmg.y + union_dmg.height >= 120);

    surf.clear_damage();
    TEST_ASSERT(!surf.damage_tracker().has_damage());

    std::cout << "[PASS] Damage tracking verified.\n";
}

void test_surface_access_control_and_pixel_ops() {
    std::cout << "[Test 3] Surface access control and pixel operations...\n";
    DisplaySurface surf(3, 100, 200, 200);

    // Owner PID (100) fill rect
    auto res_owner = surf.fill_rect(100, Rect{0, 0, 50, 50}, 0xFFFF0000); // Red
    TEST_ASSERT(res_owner == SurfaceResultCode::Success);

    // Foreign PID (200) fill rect rejected
    auto res_foreign = surf.fill_rect(200, Rect{0, 0, 50, 50}, 0xFF00FF00);
    TEST_ASSERT(res_foreign == SurfaceResultCode::AccessDenied);

    // Out of bounds fill rect (completely outside surface bounds)
    auto res_oob = surf.fill_rect(100, Rect{250, 250, 50, 50}, 0xFF0000FF);
    TEST_ASSERT(res_oob == SurfaceResultCode::OutOfBounds);

    // Copy full pixels
    std::vector<uint8_t> test_pixels(surf.size_bytes(), 0xFF);
    auto res_copy_owner = surf.copy_pixels(100, test_pixels.data(), test_pixels.size());
    TEST_ASSERT(res_copy_owner == SurfaceResultCode::Success);

    auto res_copy_foreign = surf.copy_pixels(200, test_pixels.data(), test_pixels.size());
    TEST_ASSERT(res_copy_foreign == SurfaceResultCode::AccessDenied);

    // Direct pixel read back
    uint32_t val = 0;
    TEST_ASSERT(surf.read_pixel(100, 5, 5, val) == SurfaceResultCode::Success);
    TEST_ASSERT(val == 0xFFFFFFFF);

    std::cout << "[PASS] Access control and pixel ops verified.\n";
}

void test_surface_resource_manager_quotas_and_cleanup() {
    std::cout << "[Test 4] SurfaceResourceManager quotas and lifecycle cleanup...\n";
    SurfaceResourceManager srm;

    // Reject 0 dimension
    std::shared_ptr<DisplaySurface> surf;
    auto res_zero = srm.create_surface(50, 0, 100, PixelFormat::B8G8R8A8_UNORM, surf);
    TEST_ASSERT(res_zero == SurfaceResultCode::InvalidDimensions);

    // Allocate up to max quota (16 surfaces)
    std::vector<std::shared_ptr<DisplaySurface>> allocated;
    for (uint32_t i = 0; i < SurfaceResourceManager::kMaxSurfacesPerProcess; ++i) {
        std::shared_ptr<DisplaySurface> s;
        auto res = srm.create_surface(50, 64, 64, PixelFormat::B8G8R8A8_UNORM, s);
        TEST_ASSERT(res == SurfaceResultCode::Success);
        TEST_ASSERT(s != nullptr);
        allocated.push_back(s);
    }

    auto acct = srm.get_process_accounting(50);
    TEST_ASSERT(acct.surface_count == SurfaceResourceManager::kMaxSurfacesPerProcess);
    TEST_ASSERT(acct.allocated_bytes == SurfaceResourceManager::kMaxSurfacesPerProcess * (64 * 64 * 4));

    // 17th allocation must be rejected with SurfaceLimitExceeded
    std::shared_ptr<DisplaySurface> extra_surf;
    auto res_exceeded = srm.create_surface(50, 64, 64, PixelFormat::B8G8R8A8_UNORM, extra_surf);
    TEST_ASSERT(res_exceeded == SurfaceResultCode::SurfaceLimitExceeded);

    // Stale handle lookup after destruction
    uint32_t first_id = allocated[0]->id();
    TEST_ASSERT(srm.destroy_surface(50, first_id) == SurfaceResultCode::Success);
    std::shared_ptr<DisplaySurface> lookup;
    TEST_ASSERT(srm.get_surface(50, first_id, lookup) == SurfaceResultCode::NotFound);

    // Process termination cleanup
    srm.cleanup_process_surfaces(50);
    auto acct_after = srm.get_process_accounting(50);
    TEST_ASSERT(acct_after.surface_count == 0);
    TEST_ASSERT(acct_after.allocated_bytes == 0);

    std::cout << "[PASS] Quotas and lifecycle cleanup verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 222.5: GUEST SURFACE CONTRACT TESTS\n";
    std::cout << "========================================\n";

    test_surface_geometry_and_stride();
    test_surface_damage_tracking();
    test_surface_access_control_and_pixel_ops();
    test_surface_resource_manager_quotas_and_cleanup();

    std::cout << "\nALL STAGE 222.5 TESTS PASSED (100%).\n";
    return 0;
}

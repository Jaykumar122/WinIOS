// ============================================================================
// Stage 213.6: Surface Memory and Resource Accounting Tests
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

void test_per_process_quota_and_rollback() {
    std::cout << "[Test 1] Verify Per-Process Surface Quotas and Rollback on Exceeded Limits...\n";

    SurfaceResourceManager mgr;
    constexpr std::uint32_t kPid = 5;

    std::vector<std::shared_ptr<DisplaySurface>> created;
    // Create up to kMaxSurfacesPerProcess (16)
    for (std::uint32_t i = 0; i < SurfaceResourceManager::kMaxSurfacesPerProcess; ++i) {
        std::shared_ptr<DisplaySurface> surf;
        auto res = mgr.create_surface(kPid, 100, 100, PixelFormat::B8G8R8A8_UNORM, surf);
        TEST_CHECK(res == SurfaceResultCode::Success, "Surface creation succeeded within quota");
        TEST_CHECK(surf != nullptr, "Surface pointer is valid");
        created.push_back(surf);
    }

    TEST_CHECK(mgr.get_process_accounting(kPid).surface_count == 16, "Process surface count is 16");

    // 17th surface must be rejected with SurfaceLimitExceeded
    std::shared_ptr<DisplaySurface> overflow_surf;
    auto res = mgr.create_surface(kPid, 100, 100, PixelFormat::B8G8R8A8_UNORM, overflow_surf);
    TEST_CHECK(res == SurfaceResultCode::SurfaceLimitExceeded, "17th surface rejected with SurfaceLimitExceeded");
    TEST_CHECK(overflow_surf == nullptr, "Overflow surface pointer is null (atomic rollback)");
    TEST_CHECK(mgr.get_process_accounting(kPid).surface_count == 16, "Process surface count unchanged at 16");
}

void test_per_surface_and_budget_limits() {
    std::cout << "[Test 2] Verify Per-Surface Max Bytes and Total Display Budget Limits...\n";

    SurfaceResourceManager mgr;
    constexpr std::uint32_t kPid = 8;

    // Single surface exceeding 32MB (e.g. 4000x3000x4 = 48MB)
    std::shared_ptr<DisplaySurface> huge_surf;
    auto res = mgr.create_surface(kPid, 4000, 3000, PixelFormat::B8G8R8A8_UNORM, huge_surf);
    TEST_CHECK(res == SurfaceResultCode::PerSurfaceLimitExceeded, "48MB surface rejected with PerSurfaceLimitExceeded");
    TEST_CHECK(huge_surf == nullptr, "Rejected surface pointer is null");

    // Check total display memory budget (128 MB max)
    // Create 4 surfaces of 30MB each = 120MB
    std::vector<std::shared_ptr<DisplaySurface>> batch;
    for (std::uint32_t i = 0; i < 4; ++i) {
        std::shared_ptr<DisplaySurface> surf;
        // 2500 x 3000 x 4 = 30,000,000 bytes (~28.6 MB)
        res = mgr.create_surface(10 + i, 2500, 3000, PixelFormat::B8G8R8A8_UNORM, surf);
        TEST_CHECK(res == SurfaceResultCode::Success, "Large surface within budget succeeds");
        batch.push_back(surf);
    }

    TEST_CHECK(mgr.total_allocated_bytes() >= 114000000ULL, "Total allocated memory tracked correctly");

    // 5th large surface would exceed 128MB budget
    std::shared_ptr<DisplaySurface> budget_fail_surf;
    res = mgr.create_surface(20, 2500, 3000, PixelFormat::B8G8R8A8_UNORM, budget_fail_surf);
    TEST_CHECK(res == SurfaceResultCode::MemoryBudgetExceeded, "Surface exceeding total budget rejected with MemoryBudgetExceeded");
}

void test_stale_handle_detection_and_cleanup() {
    std::cout << "[Test 3] Verify Destruction Cleanup and Stale Handle Detection...\n";

    SurfaceResourceManager mgr;
    constexpr std::uint32_t kPid = 15;

    std::shared_ptr<DisplaySurface> surf;
    auto res = mgr.create_surface(kPid, 200, 200, PixelFormat::B8G8R8A8_UNORM, surf);
    TEST_CHECK(res == SurfaceResultCode::Success, "Surface created");
    std::uint32_t sid = surf->id();

    // Query valid surface
    std::shared_ptr<DisplaySurface> queried;
    res = mgr.get_surface(kPid, sid, queried);
    TEST_CHECK(res == SurfaceResultCode::Success && queried != nullptr, "get_surface succeeds for active surface");

    // Destroy surface
    res = mgr.destroy_surface(kPid, sid);
    TEST_CHECK(res == SurfaceResultCode::Success, "destroy_surface succeeds");

    // Query stale handle
    res = mgr.get_surface(kPid, sid, queried);
    TEST_CHECK(res == SurfaceResultCode::NotFound, "get_surface on destroyed handle returns NotFound (stale handle)");
    TEST_CHECK(queried == nullptr, "Queried handle is null");

    // Total memory returned to 0
    TEST_CHECK(mgr.total_allocated_bytes() == 0, "All memory reclaimed after destruction");
    TEST_CHECK(mgr.total_surface_count() == 0, "Total surface count is 0");
}

void test_process_exit_cleanup() {
    std::cout << "[Test 4] Verify Automatic Process Exit Cleanup...\n";

    SurfaceResourceManager mgr;
    constexpr std::uint32_t kAppPid = 77;

    for (int i = 0; i < 5; ++i) {
        std::shared_ptr<DisplaySurface> surf;
        mgr.create_surface(kAppPid, 100, 100, PixelFormat::B8G8R8A8_UNORM, surf);
    }

    TEST_CHECK(mgr.total_surface_count() == 5, "5 surfaces allocated for PID 77");
    TEST_CHECK(mgr.get_process_accounting(kAppPid).surface_count == 5, "Process accounting shows 5 surfaces");

    // Simulate process termination
    mgr.cleanup_process_surfaces(kAppPid);

    TEST_CHECK(mgr.total_surface_count() == 0, "All surfaces cleaned up upon process exit");
    TEST_CHECK(mgr.total_allocated_bytes() == 0, "All display memory reclaimed upon process exit");
    TEST_CHECK(mgr.get_process_accounting(kAppPid).surface_count == 0, "PID 77 accounting removed");
}

void test_leak_free_create_destroy_loop() {
    std::cout << "[Test 5] Verify Leak-Free Repeated Create/Destroy Cycles (100 Iterations)...\n";

    SurfaceResourceManager mgr;
    constexpr std::uint32_t kPid = 99;

    for (int cycle = 0; cycle < 100; ++cycle) {
        std::shared_ptr<DisplaySurface> surf;
        auto res = mgr.create_surface(kPid, 150, 150, PixelFormat::B8G8R8A8_UNORM, surf);
        if (res != SurfaceResultCode::Success || !surf) {
            TEST_CHECK(false, "Surface creation failed during cycle");
            break;
        }

        surf->write_pixel(kPid, 0, 0, 0xFFFFFFFF);
        mgr.destroy_surface(kPid, surf->id());
    }

    TEST_CHECK(mgr.total_allocated_bytes() == 0, "Memory completely free after 100 cycles (zero leaks)");
    TEST_CHECK(mgr.total_surface_count() == 0, "Surface count is 0 after 100 cycles");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 213.6: Resource Accounting Tests     \n";
    std::cout << "====================================================\n";

    test_per_process_quota_and_rollback();
    test_per_surface_and_budget_limits();
    test_stale_handle_detection_and_cleanup();
    test_process_exit_cleanup();
    test_leak_free_create_destroy_loop();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL TESTS PASSED" : "FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

// ============================================================================
// Stage 213.22: Snapshot, Replay & 25-Vector Adversarial Security Suite
// Zero Memory Corruption, Zero Privilege Escapes, Zero Leaks
// ============================================================================
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

#include "graphics/compositor.h"
#include "graphics/display_snapshot.h"
#include "graphics/display_surface.h"
#include "graphics/virtual_display.h"
#include "graphics/window_manager.h"
#include "input/input_router.h"
#include "input/touch_event_queue.h"
#include "input/virtual_input_device.h"

using namespace winios::graphics;
using namespace winios::input;

namespace {

int g_failures = 0;

#define SEC_TEST(num, name, cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "[SEC_FAIL] Vector " << (num) << " (" << (name) << ") FAILED at line " << __LINE__ << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [SEC_PASS] Vector " << (num) << " (" << (name) << ")\n"; \
        } \
    } while (0)

void run_25_vector_security_suite() {
    std::cout << "[Security Suite] Executing 25 Adversarial Stress & Vulnerability Vectors...\n";

    // Vector 1: Framebuffer bounds overflow
    DisplayConfiguration cfg{};
    cfg.width = 100; cfg.height = 100; cfg.row_stride = 100 * 4;
    VirtualDisplay disp(cfg);
    SEC_TEST(1, "Framebuffer bounds overflow", !disp.write_pixel(100, 50, 0xFFFFFFFF) && !disp.write_pixel(50, 100, 0xFFFFFFFF));

    // Vector 2: Invalid dimensions (0 width, 0 height)
    DisplayConfiguration zero_dim{};
    zero_dim.width = 0; zero_dim.height = 100;
    SEC_TEST(2, "Invalid zero dimensions", zero_dim.validate_error() == DisplayError::InvalidDimensions);

    // Vector 3: Excessive dimensions (> 8192)
    DisplayConfiguration huge_dim{};
    huge_dim.width = 10000; huge_dim.height = 100;
    SEC_TEST(3, "Excessive dimensions (>8192)", huge_dim.validate_error() == DisplayError::InvalidDimensions);

    // Vector 4: Integer multiplication overflow on stride
    DisplayConfiguration stride_ovf{};
    stride_ovf.width = 100; stride_ovf.bytes_per_pixel = 4;
    stride_ovf.row_stride = 100; // smaller than 400
    SEC_TEST(4, "Invalid row stride", stride_ovf.validate_error() == DisplayError::InvalidStride);

    // Vector 5: Excessive allocation size exceeding 64MB display limit
    DisplayConfiguration oom_alloc{};
    oom_alloc.width = 5000; oom_alloc.height = 5000; oom_alloc.row_stride = 5000 * 4; // 100MB > 64MB
    SEC_TEST(5, "Excessive allocation limit (>64MB)", oom_alloc.validate_error() == DisplayError::AllocationExceeded);

    // Vector 6: Per-surface allocation limit (32MB)
    SurfaceResourceManager srm;
    std::shared_ptr<DisplaySurface> surf_out;
    auto res6 = srm.create_surface(1, 4000, 3000, PixelFormat::B8G8R8A8_UNORM, surf_out); // 48MB
    SEC_TEST(6, "Per-surface memory limit (32MB)", res6 == SurfaceResultCode::PerSurfaceLimitExceeded && surf_out == nullptr);

    // Vector 7: Per-process surface quota (16 surfaces max)
    for (int i = 0; i < 16; ++i) {
        srm.create_surface(10, 10, 10, PixelFormat::B8G8R8A8_UNORM, surf_out);
    }
    auto res7 = srm.create_surface(10, 10, 10, PixelFormat::B8G8R8A8_UNORM, surf_out);
    SEC_TEST(7, "Per-process surface quota (16 surfaces)", res7 == SurfaceResultCode::SurfaceLimitExceeded);

    // Vector 8: Total display memory budget limit (128MB)
    SurfaceResourceManager srm_budget;
    std::vector<std::shared_ptr<DisplaySurface>> batch;
    for (int i = 0; i < 4; ++i) {
        srm_budget.create_surface(100 + i, 2500, 3000, PixelFormat::B8G8R8A8_UNORM, surf_out);
        batch.push_back(surf_out);
    }
    auto res8 = srm_budget.create_surface(200, 2500, 3000, PixelFormat::B8G8R8A8_UNORM, surf_out);
    SEC_TEST(8, "Total display budget limit (128MB)", res8 == SurfaceResultCode::MemoryBudgetExceeded);

    // Vector 9: Invalid surface handle lookup
    auto res9 = srm.get_surface(1, 9999, surf_out);
    SEC_TEST(9, "Invalid surface handle lookup", res9 == SurfaceResultCode::NotFound && surf_out == nullptr);

    // Vector 10: Stale surface handle lookup after destruction
    srm.create_surface(5, 50, 50, PixelFormat::B8G8R8A8_UNORM, surf_out);
    std::uint32_t sid = surf_out->id();
    srm.destroy_surface(5, sid);
    auto res10 = srm.get_surface(5, sid, surf_out);
    SEC_TEST(10, "Stale destroyed surface handle lookup", res10 == SurfaceResultCode::NotFound);

    // Vector 11: Cross-process surface write access denied
    srm.create_surface(42, 50, 50, PixelFormat::B8G8R8A8_UNORM, surf_out);
    auto res11 = surf_out->write_pixel(99, 10, 10, 0xFFFFFFFF); // PID 99 != PID 42
    SEC_TEST(11, "Cross-process surface write denied", res11 == SurfaceResultCode::AccessDenied);

    // Vector 12: Cross-process surface read access denied
    std::uint32_t col = 0;
    auto res12 = surf_out->read_pixel(99, 10, 10, col);
    SEC_TEST(12, "Cross-process surface read denied", res12 == SurfaceResultCode::AccessDenied);

    // Vector 13: Cross-process fill_rect access denied
    auto res13 = surf_out->fill_rect(99, Rect{0, 0, 20, 20}, 0xFF00FF00);
    SEC_TEST(13, "Cross-process fill_rect denied", res13 == SurfaceResultCode::AccessDenied);

    // Vector 14: Stale window handles (destroying nonexistent window)
    WindowManager wm;
    bool ok14 = wm.destroy_window(1, 8888);
    SEC_TEST(14, "Destroying nonexistent window handle", !ok14);

    // Vector 15: Cross-process window tampering (PID 2 modifying PID 1's window)
    std::uint32_t wid = wm.create_window(1, "App 1", Rect{0, 0, 100, 100}, surf_out);
    bool ok15 = wm.set_window_frame(2, wid, Rect{50, 50, 200, 200});
    SEC_TEST(15, "Cross-process window tampering blocked", !ok15);

    // Vector 16: Malformed pixel format check
    DisplayConfiguration bad_fmt{};
    bad_fmt.pixel_format = static_cast<PixelFormat>(999);
    SEC_TEST(16, "Malformed pixel format rejected", bad_fmt.validate_error() == DisplayError::UnsupportedFormat);

    // Vector 17: Out-of-bounds pixel read
    auto res17 = surf_out->read_pixel(42, 100, 100, col);
    SEC_TEST(17, "Out-of-bounds pixel read rejected", res17 == SurfaceResultCode::OutOfBounds);

    // Vector 18: Out-of-bounds pixel write
    auto res18 = surf_out->write_pixel(42, 100, 100, 0xFF00FF00);
    SEC_TEST(18, "Out-of-bounds pixel write rejected", res18 == SurfaceResultCode::OutOfBounds);

    // Vector 19: Damage rectangle clipping outside buffer
    Rect clipped19 = RasterPrimitives::clip_rect(Rect{200, 200, 50, 50}, Rect{0, 0, 100, 100});
    SEC_TEST(19, "Damage rect clipping outside buffer is empty", clipped19.empty());

    // Vector 20: Presentation buffer undersized
    DirectXPresentationAdapter presenter(100, 100);
    std::vector<std::uint8_t> undersized(50);
    bool ok20 = presenter.present(undersized.data(), undersized.size(), Rect{0, 0, 100, 100});
    SEC_TEST(20, "Presentation buffer undersized rejected", !ok20 && presenter.stats().dropped_frames == 1);

    // Vector 21: Touch queue exhaustion and bounded capacity
    TouchEventQueue tq(10);
    for (int i = 0; i < 20; ++i) {
        InputEvent e{};
        tq.push(e);
    }
    SEC_TEST(21, "Touch queue capped at capacity (overflow tracked)", tq.size() == 10 && tq.overflow_count() == 10);

    // Vector 22: Out-of-bounds touch contact identifier (contact ID 15)
    VirtualInputDevice vid(1);
    bool ok22 = vid.inject_touch_begin(15, 100, 100, 100ULL);
    SEC_TEST(22, "Invalid touch contact identifier (>=10) rejected", !ok22);

    // Vector 23: Host coordinate extreme overflow clamping
    DisplayConfiguration coord_cfg{};
    coord_cfg.width = 1000; coord_cfg.height = 2000;
    TouchPoint tp23 = InputRouter::translate_host_coordinate(999999, 999999, 500, 1000, coord_cfg);
    SEC_TEST(23, "Host coordinate extreme overflow clamped to screen bounds", tp23.x == 999 && tp23.y == 1999);

    // Vector 24: Unauthorized input event delivery (PID 2 cannot poll PID 1's events)
    InputRouter router(std::make_shared<WindowManager>());
    InputEvent secret_evt{};
    secret_evt.type = InputEventType::TouchBegin;
    secret_evt.touch.x = 50;
    router.route_event(secret_evt); // routed to PID 1
    InputEvent stolen{};
    bool ok24 = router.poll_event(2, stolen); // PID 2 polling
    SEC_TEST(24, "Unauthorized input event polling blocked", !ok24);

    // Vector 25: Window destruction while touch active (synthesizes TouchCancel)
    auto wm25 = std::make_shared<WindowManager>();
    InputRouter router25(wm25);
    auto surf25 = std::make_shared<DisplaySurface>(1, 70, 200, 200);
    std::uint32_t wid25 = wm25->create_window(70, "Active App", Rect{0, 0, 200, 200}, surf25);

    InputEvent begin_evt{};
    begin_evt.type = InputEventType::TouchBegin;
    begin_evt.touch.contact_id = 0;
    begin_evt.touch.x = 50;
    begin_evt.touch.y = 50;
    router25.route_event(begin_evt);

    // Window destroyed before touch ends
    router25.on_window_destroyed(wid25);
    InputEvent cancel_out{};
    router25.poll_event(70, cancel_out); // consume begin
    bool got_cancel = router25.poll_event(70, cancel_out);
    SEC_TEST(25, "Window destruction synthesized TouchCancel to active process", got_cancel && cancel_out.type == InputEventType::TouchCancel);
}

void test_snapshot_replay_and_truncation() {
    std::cout << "[Test 2] Verify Snapshot Replay Roundtrip & Truncation Protection...\n";

    DisplayConfiguration cfg{};
    cfg.width = 400; cfg.height = 600; cfg.row_stride = 400 * 4;
    VirtualDisplay disp(cfg);
    disp.write_pixel(50, 50, 0xAABBCCDDu);

    SurfaceResourceManager srm;
    std::shared_ptr<DisplaySurface> surf;
    srm.create_surface(10, 200, 200, PixelFormat::B8G8R8A8_UNORM, surf);
    surf->write_pixel(10, 10, 10, 0x11223344u);

    WindowManager wm;
    wm.create_window(10, "Snapshot Window", Rect{10, 20, 200, 200}, surf);

    // Serialize
    std::vector<std::uint8_t> snapshot_data;
    DisplaySnapshot::serialize(disp, srm, wm, snapshot_data);
    SEC_TEST(26, "Display snapshot serialization produces non-empty bytes", !snapshot_data.empty());

    // Deserialize into fresh instances
    VirtualDisplay disp2(cfg);
    SurfaceResourceManager srm2;
    WindowManager wm2;

    const std::uint8_t* p_src = snapshot_data.data();
    std::size_t remaining = snapshot_data.size();
    bool des_ok = DisplaySnapshot::deserialize(p_src, remaining, disp2, srm2, wm2);
    SEC_TEST(27, "Display snapshot deserialization succeeds", des_ok && remaining == 0);
    SEC_TEST(28, "Restored framebuffer content matches original", disp2.read_pixel(50, 50) == 0xAABBCCDDu);

    // Truncated buffer rejection test
    std::vector<std::uint8_t> truncated(snapshot_data.begin(), snapshot_data.begin() + 16);
    const std::uint8_t* p_trunc = truncated.data();
    std::size_t rem_trunc = truncated.size();
    bool trunc_ok = DisplaySnapshot::deserialize(p_trunc, rem_trunc, disp2, srm2, wm2);
    SEC_TEST(29, "Truncated snapshot buffer safely rejected", !trunc_ok);
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 213.22: Adversarial Security Suite    \n";
    std::cout << "====================================================\n";

    run_25_vector_security_suite();
    test_snapshot_replay_and_truncation();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL 25+ SECURITY TESTS PASSED" : "SECURITY VULNERABILITIES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

#include <iostream>
#include <cstdint>
#include <memory>
#include <vector>

#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "graphics/virtual_display.h"
#include "graphics/multi_window_compositor.h"
#include "graphics/desktop_controller.h"
#include "devices/input_device_service.h"
#include "devices/display_service.h"
#include "devices/notification_service.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::graphics;
using namespace winios::devices;

static uint64_t compute_display_checksum(const VirtualDisplay& disp) {
    const uint8_t* raw = disp.raw_data();
    size_t size = static_cast<size_t>(disp.height()) * disp.stride();
    uint64_t hash = 14695981039346656037ULL; // FNV-1a 64-bit
    for (size_t i = 0; i < size; ++i) {
        hash ^= raw[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint64_t run_simulation_session() {
    DisplayConfiguration disp_cfg;
    disp_cfg.width = 800;
    disp_cfg.height = 600;
    auto disp = std::make_shared<VirtualDisplay>(disp_cfg);

    auto wm = std::make_shared<WindowManager>();
    auto comp = std::make_shared<MultiWindowCompositor>(wm, disp);
    comp->set_background_color(0xFF1E1E2E);

    auto ptr_dev = std::make_shared<VirtualPointerDevice>();
    auto kbd_dev = std::make_shared<VirtualKeyboardDevice>();
    auto notif_svc = std::make_shared<VirtualNotificationService>();
    notif_svc->initialize();

    DesktopController controller(wm, comp, disp, nullptr, ptr_dev, kbd_dev, notif_svc);

    // Create 2 test windows
    auto s1 = std::make_shared<DisplaySurface>(1, 101, 280, 180);
    s1->fill_rect(101, Rect{0, 0, 280, 180}, 0xFF313244);
    uint64_t h1 = wm->create_window_v2(101, "Window Alpha", Rect{80, 80, 280, 180}, s1);

    auto s2 = std::make_shared<DisplaySurface>(2, 102, 260, 160);
    s2->fill_rect(102, Rect{0, 0, 260, 160}, 0xFF45475A);
    uint64_t h2 = wm->create_window_v2(102, "Window Beta", Rect{200, 150, 260, 160}, s2);

    (void)h1; (void)h2;

    // Fixed deterministic input script
    uint64_t t = 1000000;
    for (int i = 0; i < 50; ++i) {
        controller.handle_pointer_move(100 + i * 4, 100 + (i % 10) * 8, t);
        t += 16666;
    }
    // Click to drag Window Beta
    controller.handle_pointer_button(1, true, 250, 160, t);
    t += 16666;
    for (int i = 0; i < 20; ++i) {
        controller.handle_pointer_move(250 + i * 2, 160 + i * 2, t);
        t += 16666;
    }
    controller.handle_pointer_button(1, false, 290, 200, t);
    t += 16666;

    // Keyboard inputs
    controller.handle_key_event(65, true, 0, t);
    t += 16666;
    controller.handle_key_event(65, false, 0, t);
    t += 16666;
    controller.handle_text_input("Deterministic Replay Test", t);
    t += 16666;

    // Notification post
    notif_svc->post_notification(101, "Replay Event", "Phase 1 Complete", "system", 5000);

    // Step controller
    controller.step();

    return compute_display_checksum(*disp);
}

int main() {
    std::cout << "[Test Stage 226.21] Deterministic Input Replay & Visual Checksum...\n" << std::flush;

    uint64_t checksum1 = run_simulation_session();
    uint64_t checksum2 = run_simulation_session();

    TEST_ASSERT(checksum1 != 0);
    TEST_ASSERT(checksum1 == checksum2);

    std::cout << "[Test Stage 226.21] PASSED with matching checksum 0x"
              << std::hex << checksum1 << std::dec << "\n" << std::flush;
    return 0;
}

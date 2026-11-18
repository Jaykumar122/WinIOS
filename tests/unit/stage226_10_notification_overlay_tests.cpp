#include <iostream>
#include <cstdint>
#include <memory>
#include <vector>

#include "graphics/window_manager.h"
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

int main() {
    std::cout << "[Test Stage 226.10] Notification Overlay & Desktop Integration...\n" << std::flush;

    DisplayConfiguration disp_cfg;
    disp_cfg.width = 1024;
    disp_cfg.height = 768;
    auto disp = std::make_shared<VirtualDisplay>(disp_cfg);

    auto wm = std::make_shared<WindowManager>();
    auto comp = std::make_shared<MultiWindowCompositor>(wm, disp);
    comp->set_background_color(0xFF000000); // Black background
    auto ptr_dev = std::make_shared<VirtualPointerDevice>();
    auto kbd_dev = std::make_shared<VirtualKeyboardDevice>();
    auto notif_svc = std::make_shared<VirtualNotificationService>();
    notif_svc->initialize();

    DesktopController controller(wm, comp, disp, nullptr, ptr_dev, kbd_dev, notif_svc);

    // 1. Initial composite with no notifications
    TEST_ASSERT(controller.step());
    std::uint8_t* raw = disp->raw_data();
    // Top right pixel (1000, 40) should be background (0xFF000000)
    std::uint32_t sample = *reinterpret_cast<std::uint32_t*>(raw + (40 * disp->stride()) + (1000 * 4));
    TEST_ASSERT(sample == 0xFF000000);

    // 2. Post a notification
    uint32_t n1 = notif_svc->post_notification(101, "Low Battery", "15% remaining", "alert", 5000);
    TEST_ASSERT(n1 > 0);
    TEST_ASSERT(notif_svc->active_count() == 1);

    // Step desktop controller (composites windows + renders notification overlay)
    TEST_ASSERT(controller.step());

    // Top right pixel (card is width 320, x = 1024 - 320 - 20 = 684, y = 35)
    // Sample inside notification card at (700, 50)
    sample = *reinterpret_cast<std::uint32_t*>(raw + (50 * disp->stride()) + (700 * 4));
    // Card background is 0xFF24242A (opaque blit)
    TEST_ASSERT((sample & 0x00FFFFFF) == 0x0024242A);

    // 3. Post 3 more notifications (total 4)
    notif_svc->post_notification(101, "Message 1", "Hello there", "chat", 5000);
    notif_svc->post_notification(101, "Message 2", "Another ping", "chat", 5000);
    notif_svc->post_notification(101, "Message 3", "Third ping", "chat", 5000);
    TEST_ASSERT(notif_svc->active_count() == 4);

    // Desktop overlay caps at rendering top 3
    TEST_ASSERT(controller.step());

    // Card 3 is at card_y = 35 + 2 * (65 + 10) = 185
    // Sample inside card 3 at (700, 195)
    std::uint32_t sample_card3 = *reinterpret_cast<std::uint32_t*>(raw + (195 * disp->stride()) + (700 * 4));
    TEST_ASSERT((sample_card3 & 0x00FFFFFF) == 0x0024242A);

    // Below card 3, card 4 is NOT rendered: at y = 35 + 3 * 75 = 260
    std::uint32_t sample_below = *reinterpret_cast<std::uint32_t*>(raw + (270 * disp->stride()) + (700 * 4));
    TEST_ASSERT(sample_below == 0xFF000000);

    // 4. Clear all notifications
    notif_svc->clear_all();
    TEST_ASSERT(notif_svc->active_count() == 0);
    TEST_ASSERT(controller.step());

    // Card area should now be restored to background
    sample = *reinterpret_cast<std::uint32_t*>(raw + (50 * disp->stride()) + (700 * 4));
    TEST_ASSERT(sample == 0xFF000000);

    std::cout << "[Test Stage 226.10] PASSED successfully.\n" << std::flush;
    return 0;
}

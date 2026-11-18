#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#include "graphics/window_decorations.h"
#include "graphics/window_manager.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace winios::graphics;

void test_non_client_hit_testing_zones() {
    WindowManager wm;
    auto s = std::make_shared<DisplaySurface>(1, 10, 400, 300);
    // Window at (100, 100), size (400, 300)
    std::uint32_t w = wm.create_window(10, "Test Window", Rect{100, 100, 400, 300}, s);
    TEST_ASSERT(w > 0);

    // 1. Outside window
    TEST_ASSERT(wm.hit_test_non_client(w, 50, 50) == NonClientZone::None);
    TEST_ASSERT(wm.hit_test_non_client(w, 550, 450) == NonClientZone::None);

    // 2. Title Bar area (y in [100, 127])
    // Center of title bar (x = 200, y = 114)
    TEST_ASSERT(wm.hit_test_non_client(w, 200, 114) == NonClientZone::TitleBar);

    // 3. Close Button:
    // Window width = 400, close_x = 100 + 400 - 6 - 18 = 476. Close rect is [476..494], y in [105..123]
    TEST_ASSERT(wm.hit_test_non_client(w, 480, 114) == NonClientZone::CloseButton);

    // 4. Maximize Button:
    // max_x = 476 - 6 - 18 = 452. Max rect is [452..470]
    TEST_ASSERT(wm.hit_test_non_client(w, 460, 114) == NonClientZone::MaximizeButton);

    // 5. Minimize Button:
    // min_x = 452 - 6 - 18 = 428. Min rect is [428..446]
    TEST_ASSERT(wm.hit_test_non_client(w, 435, 114) == NonClientZone::MinimizeButton);

    // 6. Client Area:
    // x = 200, y = 200 (well within client frame)
    TEST_ASSERT(wm.hit_test_non_client(w, 200, 200) == NonClientZone::Client);

    // 7. Border:
    // x = 102 (within 4px of left edge), y = 200
    TEST_ASSERT(wm.hit_test_non_client(w, 102, 200) == NonClientZone::Border);

    std::cout << "[PASS] test_non_client_hit_testing_zones passed\n";
}

void test_decorations_rendering() {
    constexpr std::uint32_t kW = 400;
    constexpr std::uint32_t kH = 300;
    constexpr std::uint32_t kStride = kW * 4;
    std::vector<std::uint8_t> buffer(kStride * kH, 0);

    WindowManager wm;
    auto s = std::make_shared<DisplaySurface>(1, 10, 200, 150);
    std::uint32_t wid = wm.create_window(10, "Active Window", Rect{50, 50, 200, 150}, s);
    auto win = wm.get_window(wid);
    TEST_ASSERT(win != nullptr);

    win->is_focused = true;
    WindowDecorations::render(buffer.data(), kW, kH, kStride, *win);

    // Check title bar pixel color at (60, 60) -> should match kActiveTitleBarColor
    std::size_t offset = (60 * kStride) + (60 * 4);
    std::uint32_t color = static_cast<std::uint32_t>(buffer[offset + 0]) |
                         (static_cast<std::uint32_t>(buffer[offset + 1]) << 8) |
                         (static_cast<std::uint32_t>(buffer[offset + 2]) << 16) |
                         (static_cast<std::uint32_t>(buffer[offset + 3]) << 24);
    TEST_ASSERT(color == WindowDecorations::kActiveTitleBarColor);

    std::cout << "[PASS] test_decorations_rendering passed\n";
}

int main() {
    std::cout << "=== Running Stage 216.8 Window Decorations Tests ===" << std::endl;
    test_non_client_hit_testing_zones();
    test_decorations_rendering();
    std::cout << "=== All Stage 216.8 Window Decorations Tests PASSED ===" << std::endl;
    return 0;
}

#include <iostream>
#include <cstdint>
#include <memory>

#include "devices/display_service.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::devices;

int main() {
    std::cout << "[Test Stage 225.6] Virtual Display Service & Output Negotiation...\n" << std::flush;

    VirtualDisplayService disp;
    TEST_ASSERT(disp.initialize());
    TEST_ASSERT(disp.start());

    // 1. Initial config
    auto cfg = disp.get_config();
    TEST_ASSERT(cfg.width == 1179);
    TEST_ASSERT(cfg.height == 2556);
    TEST_ASSERT(cfg.refresh_rate_hz == 60);

    // 2. Set valid config
    DisplayConfigData new_cfg = cfg;
    new_cfg.width = 1290;
    new_cfg.height = 2796;
    new_cfg.scale = 3.0f;
    TEST_ASSERT(disp.set_config(new_cfg));

    auto updated = disp.get_config();
    TEST_ASSERT(updated.width == 1290);
    TEST_ASSERT(updated.height == 2796);
    TEST_ASSERT(disp.resize_count() == 1);

    // 3. Reject invalid configs
    DisplayConfigData bad_cfg = cfg;
    bad_cfg.width = 100; // Too small (min 320)
    TEST_ASSERT(!disp.set_config(bad_cfg));

    bad_cfg = cfg;
    bad_cfg.scale = 0.1f; // Out of bounds (min 0.5)
    TEST_ASSERT(!disp.set_config(bad_cfg));

    // 4. Resize callback notification
    uint32_t cb_w = 0, cb_h = 0;
    disp.set_resize_callback([&](uint32_t w, uint32_t h) {
        cb_w = w;
        cb_h = h;
    });

    new_cfg.width = 800;
    new_cfg.height = 600;
    TEST_ASSERT(disp.set_config(new_cfg));
    TEST_ASSERT(cb_w == 800);
    TEST_ASSERT(cb_h == 600);

    std::cout << "[Test Stage 225.6] PASS: Display Service verified.\n";
    return 0;
}

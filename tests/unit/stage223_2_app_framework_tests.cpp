#include <cassert>
#include <cstring>
#include <iostream>

#include "winios/winios.h"
#include "winios/app/app.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

// Host test stubs for window & event syscalls
static winios_event_v2_t g_pending_events[8];
static int g_event_head = 0;
static int g_event_tail = 0;
static int g_window_created = 0;
static int g_window_destroyed = 0;
static int g_surface_presented = 0;

extern "C" {
    winios_wid_t winios_window_create(const char* title, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
        (void)title; (void)x; (void)y; (void)width; (void)height;
        g_window_created++;
        return 42; // Simulated valid window ID
    }

    int32_t winios_window_destroy(winios_wid_t wid) {
        (void)wid;
        g_window_destroyed++;
        return 0;
    }

    int32_t winios_surface_present(winios_wid_t wid) {
        (void)wid;
        g_surface_presented++;
        return 0;
    }

    int32_t winios_poll_event_v2(winios_event_v2_t* out_event) {
        if (!out_event || g_event_head == g_event_tail) return 0;
        *out_event = g_pending_events[g_event_head++];
        return 1;
    }

    void winios_sched_yield(void) {
        // Yield stub
    }
}

static int s_init_called = 0;
static int s_update_called = 0;
static int s_render_called = 0;
static int s_event_called = 0;
static int s_shutdown_called = 0;

static void on_app_init(winios_app_t* app, void* user_data) {
    (void)app; (void)user_data;
    s_init_called++;
}

static void on_app_update(winios_app_t* app, uint64_t delta_us, void* user_data) {
    (void)app; (void)delta_us; (void)user_data;
    s_update_called++;
    if (s_update_called >= 3) {
        winios_app_request_exit(app, 99);
    }
}

static void on_app_render(winios_app_t* app, uint32_t* fb, uint32_t stride, void* user_data) {
    (void)app; (void)fb; (void)stride; (void)user_data;
    s_render_called++;
}

static int on_app_event(winios_app_t* app, const winios_event_v2_t* evt, void* user_data) {
    (void)app; (void)evt; (void)user_data;
    s_event_called++;
    return 1; // triggers redraw
}

static void on_app_shutdown(winios_app_t* app, void* user_data) {
    (void)app; (void)user_data;
    s_shutdown_called++;
}

void test_app_framework_lifecycle() {
    std::cout << "[Test 1] Application framework lifecycle & main loop...\n";

    winios_app_config_t cfg{};
    cfg.app_id = "com.apple.testapp";
    cfg.title = "Test Lifecycle App";
    cfg.window_width = 500;
    cfg.window_height = 400;
    cfg.on_init = on_app_init;
    cfg.on_update = on_app_update;
    cfg.on_render = on_app_render;
    cfg.on_event = on_app_event;
    cfg.on_shutdown = on_app_shutdown;

    winios_app_t app{};
    TEST_ASSERT(winios_app_init(&app, &cfg) == 0);
    TEST_ASSERT(app.width == 500);
    TEST_ASSERT(app.height == 400);

    // Queue 2 events
    winios_event_v2_t ev1{};
    ev1.type = WINIOS_EVENT_V2_MOUSE_DOWN;
    ev1.x = 100; ev1.y = 100;
    g_pending_events[g_event_tail++] = ev1;

    winios_event_v2_t ev2{};
    ev2.type = WINIOS_EVENT_V2_MOUSE_UP;
    ev2.x = 100; ev2.y = 100;
    g_pending_events[g_event_tail++] = ev2;

    int exit_code = winios_app_run(&app);
    TEST_ASSERT(exit_code == 99);
    TEST_ASSERT(s_init_called == 1);
    TEST_ASSERT(s_update_called >= 3);
    TEST_ASSERT(s_render_called >= 1);
    TEST_ASSERT(s_event_called == 2);
    TEST_ASSERT(s_shutdown_called == 1);

    // Window was created and cleanly destroyed
    TEST_ASSERT(g_window_created == 1);
    TEST_ASSERT(g_window_destroyed == 1);
    TEST_ASSERT(g_surface_presented >= 1);

    std::cout << "[PASS] Application framework lifecycle verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 223.2 & 223.3: GUEST APP FRAMEWORK TESTS\n";
    std::cout << "========================================\n";

    test_app_framework_lifecycle();

    std::cout << "\nALL STAGE 223.2 & 223.3 TESTS PASSED (100%).\n";
    return 0;
}

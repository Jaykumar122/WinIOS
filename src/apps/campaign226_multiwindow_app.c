#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    winios_log_info("MWAPP", "Campaign 226 Multi-Window Guest Application starting...");

    // 1. Create Primary Window (360x240 at 50, 50) via ABI v2.0
    uint64_t h_primary = winios_window_create_v2("MultiWindow Primary", 50, 50, 360, 240, 0);
    if (h_primary == 0) {
        winios_log_error("MWAPP", "Failed to create primary window");
        winios_exit(1);
        return 1;
    }

    // 2. Create Secondary Tool Window (240x180 at 440, 50) via ABI v2.0
    uint64_t h_tool = winios_window_create_v2("MultiWindow Tool", 440, 50, 240, 180, 0);
    if (h_tool == 0) {
        winios_log_error("MWAPP", "Failed to create tool window");
        winios_exit(2);
        return 2;
    }

    // 3. Invalidate Damage Rectangles on both windows
    winios_rect_t dmg1 = {10, 10, 100, 50};
    winios_window_invalidate_rect(h_primary, &dmg1);

    winios_rect_t dmg2 = {0, 0, 240, 180};
    winios_window_invalidate_rect(h_tool, &dmg2);

    // 4. Poll Event Queue for Primary Window
    winios_desktop_event_t ev;
    winios_memset(&ev, 0, sizeof(ev));
    int32_t poll_res = winios_event_poll_v2(h_primary, &ev);
    (void)poll_res;

    // 5. Query Window State
    winios_window_state_data_t state_data;
    winios_memset(&state_data, 0, sizeof(state_data));
    int32_t st_res = winios_window_get_state_v2(h_primary, &state_data);
    (void)st_res;

    // 6. Set Desktop Clipboard Text via ABI v2.0
    const char* clip_msg = "Campaign 226 Multi-Window Workspace Active";
    winios_clipboard_set_v2(clip_msg, 42);

    // 7. Destroy Secondary Tool Window
    int32_t dest_res = winios_window_destroy_v2(h_tool);
    (void)dest_res;

    winios_log_info("MWAPP", "Campaign 226 Multi-Window App completed operations, exiting 93.");
    winios_exit(93);
    return 93;
}

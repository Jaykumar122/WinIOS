#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    winios_log_info("RESBRW", "Campaign 223 Resource Browser starting...");

    // 1. Create Resource Browser Window (400x320)
    winios_wid_t wid = winios_window_create("Resource Browser", 120, 120, 400, 320);
    if (wid > 0) {
        // Light macOS-style background
        winios_surface_fill(wid, 0, 0, 400, 320, 0xFFF2F2F7);

        // Header toolbar
        winios_surface_fill(wid, 0, 0, 400, 40, 0xFFFFFFFF);
        winios_draw_text(wid, 16, 14, "Resource Explorer", 0xFF1C1C1E);

        // 2. Zoom Level Slider (50% to 200%, default 100%)
        winios_slider_t slider;
        winios_slider_init(&slider, 20, 55, 180, 24, 50, 200, 100);
        winios_draw_text(wid, 20, 85, "Zoom: 100%", 0xFF3C3C43);

        // Simulate slider drag: drag thumb rightwards to ~150%
        winios_event_v2_t drag_down;
        winios_memset(&drag_down, 0, sizeof(drag_down));
        drag_down.type = WINIOS_EVENT_V2_MOUSE_DOWN;
        drag_down.x = 140; drag_down.y = 67;
        winios_slider_handle_event(&slider, &drag_down);

        winios_event_v2_t drag_up;
        winios_memset(&drag_up, 0, sizeof(drag_up));
        drag_up.type = WINIOS_EVENT_V2_MOUSE_UP;
        drag_up.x = 140; drag_up.y = 67;
        winios_slider_handle_event(&slider, &drag_up);

        winios_log_info("RESBRW", "Zoom slider adjusted via pointer interaction.");

        // 3. Action Menu
        winios_menu_t menu;
        winios_menu_init(&menu, 230, 55, 150);
        winios_menu_add_item(&menu, "Inspect Asset", 101);
        winios_menu_add_item(&menu, "Export Raw", 102);
        winios_menu_add_item(&menu, "Refresh List", 103);

        // Simulate menu item click: item 0 at y = 55 + 2 + 12 = 69
        winios_event_v2_t click_menu;
        winios_memset(&click_menu, 0, sizeof(click_menu));
        click_menu.type = WINIOS_EVENT_V2_MOUSE_UP;
        click_menu.x = 260; click_menu.y = 69;
        if (winios_menu_handle_event(&menu, &click_menu)) {
            winios_log_info("RESBRW", "Menu item selected: Inspect Asset (ID 101)");
        }

        // 4. Resource Item List Frame
        winios_draw_rect(wid, 20, 120, 360, 180, 0xFFD1D1D6, 0);
        winios_surface_fill(wid, 21, 121, 358, 178, 0xFFFFFFFF);

        winios_draw_text(wid, 30, 135, "[1] font8x8_basic.bin (2048 B)", 0xFF007AFF);
        winios_draw_text(wid, 30, 160, "[2] app_icon_60pt@3x.png (14280 B)", 0xFF1C1C1E);
        winios_draw_text(wid, 30, 185, "[3] bundle_manifest.json (512 B)", 0xFF1C1C1E);

        // Copy selected asset path to clipboard
        winios_clipboard_set("font8x8_basic.bin", 17);
        winios_log_info("RESBRW", "Asset path copied to system clipboard");

        winios_surface_present(wid);
    }

    winios_log_info("RESBRW", "Resource Browser completed successfully with exit code 63.");
    winios_exit(63);
    return 63;
}

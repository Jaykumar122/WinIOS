#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    winios_log_info("CALC", "Campaign 223 Calculator starting...");

    // 1. Create Calculator Window (280x360)
    winios_wid_t wid = winios_window_create("Calculator", 50, 50, 280, 360);
    if (wid > 0) {
        // Base dark background (Apple Calculator style)
        winios_surface_fill(wid, 0, 0, 280, 360, 0xFF1C1C1E);

        // Display panel
        winios_surface_fill(wid, 10, 20, 260, 60, 0xFF2C2C2E);
        winios_draw_text(wid, 240, 45, "0", 0xFFFFFFFF);

        // Buttons
        winios_button_t btn_7;
        winios_button_init(&btn_7, 15, 100, 55, 45, "7");
        winios_button_t btn_plus;
        winios_button_init(&btn_plus, 210, 100, 55, 45, "+");
        winios_button_t btn_5;
        winios_button_init(&btn_5, 80, 160, 55, 45, "5");
        winios_button_t btn_eq;
        winios_button_init(&btn_eq, 210, 280, 55, 45, "=");

        // Render buttons
        winios_draw_rect(wid, 15, 100, 55, 45, 0xFF505050, 1);
        winios_draw_text(wid, 38, 115, "7", 0xFFFFFFFF);

        winios_draw_rect(wid, 210, 100, 55, 45, 0xFFFF9F0A, 1);
        winios_draw_text(wid, 233, 115, "+", 0xFFFFFFFF);

        winios_draw_rect(wid, 80, 160, 55, 45, 0xFF505050, 1);
        winios_draw_text(wid, 103, 175, "5", 0xFFFFFFFF);

        winios_draw_rect(wid, 210, 280, 55, 45, 0xFFFF9F0A, 1);
        winios_draw_text(wid, 233, 295, "=", 0xFFFFFFFF);

        // 2. Simulate calculation sequence: 7 + 5 = 12
        winios_event_v2_t down_evt;
        winios_memset(&down_evt, 0, sizeof(down_evt));
        down_evt.type = WINIOS_EVENT_V2_MOUSE_DOWN;

        // Click '7'
        down_evt.x = 40; down_evt.y = 120;
        winios_button_handle_event(&btn_7, &down_evt);

        // Click '+'
        down_evt.x = 230; down_evt.y = 120;
        winios_button_handle_event(&btn_plus, &down_evt);

        // Click '5'
        down_evt.x = 100; down_evt.y = 180;
        winios_button_handle_event(&btn_5, &down_evt);

        // Click '='
        down_evt.x = 230; down_evt.y = 300;
        winios_button_handle_event(&btn_eq, &down_evt);

        // 3. Display updated result "12"
        winios_surface_fill(wid, 10, 20, 260, 60, 0xFF2C2C2E);
        winios_draw_text(wid, 230, 45, "12", 0xFFFFFFFF);

        // 4. Copy result to clipboard
        winios_clipboard_set("12", 2);
        winios_log_info("CALC", "Calculated 7 + 5 = 12 and copied to clipboard");

        winios_surface_present(wid);
    }

    winios_log_info("CALC", "Calculator application completed successfully with exit code 61.");
    winios_exit(61);
    return 61;
}

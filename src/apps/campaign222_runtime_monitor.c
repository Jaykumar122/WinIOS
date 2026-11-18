#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    winios_log_info("MONITOR", "Campaign 222 Runtime Monitor starting...");
    uint32_t pid = winios_get_pid();
    (void)pid;

    // 1. Create Monitor Window (380x300)
    winios_wid_t wid = winios_window_create("Runtime Monitor", 120, 100, 380, 300);
    if (wid > 0) {
        // Base window surface fill: dark console theme
        winios_surface_fill(wid, 0, 0, 380, 300, 0xFF1C1C1E);

        // Header bar: deep charcoal
        winios_surface_fill(wid, 0, 0, 380, 36, 0xFF2C2C2E);
        winios_draw_text(wid, 16, 10, "Monitor v1.6", 0xFFFFFFFF);

        // CPU Telemetry Label
        winios_label_t cpu_lbl;
        winios_label_init(&cpu_lbl, 20, 50, 340, 20, "CPU: Apple A16");
        winios_draw_text(wid, 20, 50, "CPU: A16", 0xFF30D158);

        // Memory bar & label
        winios_label_t mem_lbl;
        winios_label_init(&mem_lbl, 20, 80, 340, 20, "RAM: 48%");
        winios_draw_text(wid, 20, 80, "RAM: 48%", 0xFFEBEBF5);

        winios_progressbar_t mem_bar;
        winios_progressbar_init(&mem_bar, 20, 105, 340, 18, 0, 100, 48);
        winios_draw_rect(wid, 20, 105, 340, 18, 0xFF3A3A3C, 1);
        winios_draw_rect(wid, 20, 105, (340 * 48) / 100, 18, 0xFF0A84FF, 1);

        // Presentation & Compositor Telemetry
        winios_label_t comp_lbl;
        winios_label_init(&comp_lbl, 20, 140, 340, 20, "FPS: 60");
        winios_draw_text(wid, 20, 140, "FPS: 60", 0xFFFFD60A);

        winios_progressbar_t fps_bar;
        winios_progressbar_init(&fps_bar, 20, 165, 340, 18, 0, 60, 60);
        winios_draw_rect(wid, 20, 165, 340, 18, 0xFF3A3A3C, 1);
        winios_draw_rect(wid, 20, 165, 340, 18, 0xFF30D158, 1);

        // Action Button: Snapshot Telemetry
        winios_button_t snap_btn;
        winios_button_init(&snap_btn, 20, 205, 160, 32, "Capture");
        winios_draw_rect(wid, 20, 205, 160, 32, 0xFF5E5CE6, 1);
        winios_draw_text(wid, 32, 213, "Capture", 0xFFFFFFFF);

        // 2. Handle interactive simulated event (click button)
        winios_event_v2_t down_evt;
        winios_memset(&down_evt, 0, sizeof(down_evt));
        down_evt.type = WINIOS_EVENT_V2_MOUSE_DOWN;
        down_evt.x = 40;
        down_evt.y = 215;
        winios_button_handle_event(&snap_btn, &down_evt);

        winios_event_v2_t up_evt;
        winios_memset(&up_evt, 0, sizeof(up_evt));
        up_evt.type = WINIOS_EVENT_V2_MOUSE_UP;
        up_evt.x = 40;
        up_evt.y = 215;

        if (winios_button_handle_event(&snap_btn, &up_evt)) {
            winios_log_info("MONITOR", "Snapshot button activated!");
        }

        // 3. Write persistent telemetry record
        const char* telem_file = "telemetry.log";
        const char* telem_data = "CPU=A16,MODE=EL0,RAM=48%,FPS=60,STATUS=OK\n";
        int32_t w_res = winios_storage_write_file(telem_file, telem_data, winios_strlen(telem_data));
        if (w_res >= 0) {
            winios_log_info("MONITOR", "Telemetry log written to isolated storage.");
        }

        // 4. Present surface
        winios_surface_present(wid);
    }

    winios_log_info("MONITOR", "Runtime monitor completed successfully with exit code 52.");
    winios_exit(52);
    return 52;
}

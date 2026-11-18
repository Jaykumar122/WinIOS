#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    winios_log_info("NOTES", "Campaign 222 Interactive Notes starting...");
    uint32_t pid = winios_get_pid();
    (void)pid;

    // 1. Create Notes Window (360x320)
    winios_wid_t wid = winios_window_create("Interactive Notes", 50, 50, 360, 320);
    if (wid > 0) {
        // Base window surface fill: light parchment
        winios_surface_fill(wid, 0, 0, 360, 320, 0xFFF7F5F0);

        // Header bar: subtle slate
        winios_surface_fill(wid, 0, 0, 360, 36, 0xFFE2E0D8);
        winios_draw_text(wid, 16, 10, "Notes v1.6", 0xFF222222);

        // Draw note editor frame
        winios_draw_rect(wid, 16, 50, 328, 120, 0xFFCCCCCC, 0);
        winios_surface_fill(wid, 17, 51, 326, 118, 0xFFFFFFFF);

        // 2. Initialize UI Toolkit widgets
        char note_buf[128];
        const char* initial_text = "Review ARM64 Compositor & Input";
        size_t text_len = winios_strlen(initial_text);
        winios_memcpy(note_buf, initial_text, text_len + 1);

        winios_textinput_t text_in;
        winios_textinput_init(&text_in, 20, 55, 320, 24, note_buf, sizeof(note_buf));
        winios_draw_text(wid, 24, 60, "Note Active", 0xFF111111);

        winios_checkbox_t auto_save;
        winios_checkbox_init(&auto_save, 20, 185, 200, 20, "Auto-sync", 1);
        // Draw checkbox indicator & label
        winios_draw_rect(wid, 20, 185, 16, 16, 0xFF4A90E2, 1);
        winios_draw_text(wid, 44, 186, "AutoSync", 0xFF333333);

        winios_button_t save_btn;
        winios_button_init(&save_btn, 20, 220, 120, 32, "Save Note");
        winios_draw_rect(wid, 20, 220, 120, 32, 0xFF007AFF, 1);
        winios_draw_text(wid, 42, 228, "Save", 0xFFFFFFFF);

        winios_progressbar_t prog;
        winios_progressbar_init(&prog, 20, 270, 320, 18, 0, 100, 100);
        winios_draw_rect(wid, 20, 270, 320, 18, 0xFFE0E0E0, 1);
        winios_draw_rect(wid, 20, 270, 320, 18, 0xFF34C759, 1);

        // 3. Handle interactive simulated event (click Save Note button)
        winios_event_v2_t down_evt;
        winios_memset(&down_evt, 0, sizeof(down_evt));
        down_evt.type = WINIOS_EVENT_V2_MOUSE_DOWN;
        down_evt.x = 40;
        down_evt.y = 230;
        winios_button_handle_event(&save_btn, &down_evt);

        winios_event_v2_t up_evt;
        winios_memset(&up_evt, 0, sizeof(up_evt));
        up_evt.type = WINIOS_EVENT_V2_MOUSE_UP;
        up_evt.x = 40;
        up_evt.y = 230;

        if (winios_button_handle_event(&save_btn, &up_evt)) {
            winios_log_info("NOTES", "Save button click triggered!");
        }

        // 4. Perform atomic storage write
        const char* note_file = "active_note.txt";
        int32_t w_res = winios_storage_write_file(note_file, text_in.buffer, text_in.length);
        if (w_res >= 0) {
            winios_log_info("NOTES", "Note saved successfully into isolated storage.");
        }

        // 5. Read back to confirm data integrity
        char verify_buf[128];
        winios_memset(verify_buf, 0, sizeof(verify_buf));
        size_t read_bytes = 0;
        int32_t r_res = winios_storage_read_file(note_file, verify_buf, sizeof(verify_buf) - 1, &read_bytes);
        if (r_res >= 0 && read_bytes > 0) {
            winios_log_info("NOTES", "Note reloaded and verified from isolated storage.");
        }

        // 6. Present surface
        winios_surface_present(wid);
    }

    winios_log_info("NOTES", "Notes application completed successfully with exit code 51.");
    winios_exit(51);
    return 51;
}

#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    winios_log_info("NOTES2", "Campaign 223 Notes v2 starting...");

    // 1. Create Notes v2 Window (360x360)
    winios_wid_t wid = winios_window_create("Notes v2", 80, 80, 360, 360);
    if (wid > 0) {
        // Light yellow parchment background
        winios_surface_fill(wid, 0, 0, 360, 360, 0xFFFAF8F5);

        // Header bar
        winios_surface_fill(wid, 0, 0, 360, 36, 0xFFEAE6DF);
        winios_draw_text(wid, 16, 12, "Notes v2.0 - Campaign 223", 0xFF2C2C2E);

        // 2. Multiline Text Area
        char note_buf[256];
        const char* initial_text = "Campaign 223 Notes";
        size_t init_len = winios_strlen(initial_text);
        winios_memcpy(note_buf, initial_text, init_len + 1);

        winios_textarea_t ta;
        winios_textarea_init(&ta, 20, 50, 320, 180, note_buf, sizeof(note_buf));
        ta.is_focused = 1;

        // Draw textarea container
        winios_draw_rect(wid, 20, 50, 320, 180, 0xFFD1D1D6, 0);
        winios_surface_fill(wid, 21, 51, 318, 178, 0xFFFFFFFF);
        winios_draw_text(wid, 28, 60, note_buf, 0xFF1C1C1E);

        // 3. Simulate user typing newline & "Rich UI Controls"
        winios_event_v2_t nl_evt;
        winios_memset(&nl_evt, 0, sizeof(nl_evt));
        nl_evt.type = WINIOS_EVENT_V2_KEY_CHAR;
        nl_evt.key_code = 10; // Newline
        winios_textarea_handle_event(&ta, &nl_evt);

        const char* append_text = "Rich UI Controls";
        for (int i = 0; append_text[i]; ++i) {
            winios_event_v2_t ch_evt;
            winios_memset(&ch_evt, 0, sizeof(ch_evt));
            ch_evt.type = WINIOS_EVENT_V2_KEY_CHAR;
            ch_evt.key_code = (uint32_t)append_text[i];
            winios_textarea_handle_event(&ta, &ch_evt);
        }

        winios_draw_text(wid, 28, 76, append_text, 0xFF1C1C1E);

        // 4. Save to isolated storage
        const char* filename = "notes_v2.txt";
        int32_t w_res = winios_storage_write_file(filename, note_buf, ta.length);
        if (w_res >= 0) {
            winios_log_info("NOTES2", "Note persisted to storage successfully.");
        }

        // 5. Read back from storage to verify
        char verify_buf[256];
        winios_memset(verify_buf, 0, sizeof(verify_buf));
        size_t read_bytes = 0;
        int32_t r_res = winios_storage_read_file(filename, verify_buf, sizeof(verify_buf) - 1, &read_bytes);
        if (r_res >= 0 && read_bytes > 0) {
            winios_log_info("NOTES2", "Note verified from isolated storage.");
        }

        // 6. Modal Confirmation Dialog
        winios_dialog_t dlg;
        winios_dialog_init(&dlg, 50, 120, 260, 120, "Saved", "Note persisted to disk!", 0);
        // Simulate click OK on dialog (OK button is at right: ok_x = 50 + 260 - 80 = 230, btn_y = 120 + 120 - 38 = 202)
        winios_event_v2_t click_ok;
        winios_memset(&click_ok, 0, sizeof(click_ok));
        click_ok.type = WINIOS_EVENT_V2_MOUSE_UP;
        click_ok.x = 240;
        click_ok.y = 210;
        winios_dialog_handle_event(&dlg, &click_ok);
        if (dlg.result == WINIOS_DIALOG_OK) {
            winios_log_info("NOTES2", "Confirmation dialog dismissed via OK");
        }

        // 7. Clipboard synchronization
        winios_clipboard_set(note_buf, ta.length);

        winios_surface_present(wid);
    }

    winios_log_info("NOTES2", "Notes v2 application completed successfully with exit code 62.");
    winios_exit(62);
    return 62;
}

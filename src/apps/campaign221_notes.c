#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    winios_log_info("NOTES", "Notes application starting...");
    uint32_t pid = winios_get_pid();

    // 1. Create Notes Window
    winios_wid_t wid = winios_window_create("Notes", 80, 80, 400, 350);
    if (wid > 0) {
        // Background: Orange/Paper tone
        winios_surface_fill(wid, 0, 0, 400, 350, WINIOS_COLOR_ORANGE);
        // Editor area: White
        winios_surface_fill(wid, 15, 45, 370, 280, WINIOS_COLOR_WHITE);
        // Header bar: Gray
        winios_surface_fill(wid, 15, 10, 370, 30, WINIOS_COLOR_GRAY);
        winios_surface_present(wid);
    }

    // 2. Storage operations: Write a note
    const char* note_path = "quick_note.txt";
    const char* note_content = "Buy milk\nMeeting with design team\nReview WinIOS Campaign 221\n";
    int32_t w_res = winios_storage_write_file(note_path, note_content, winios_strlen(note_content));
    if (w_res >= 0) {
        winios_log_info("NOTES", "Note saved successfully.");
    }

    // 3. Read back note
    char note_buf[128];
    winios_memset(note_buf, 0, sizeof(note_buf));
    size_t read_bytes = 0;
    int32_t r_res = winios_storage_read_file(note_path, note_buf, sizeof(note_buf) - 1, &read_bytes);
    if (r_res >= 0 && read_bytes > 0) {
        winios_log_info("NOTES", "Note reloaded successfully.");
    }

    // 4. Poll events
    winios_event_t ev;
    winios_poll_event(&ev);

    winios_log_info("NOTES", "Notes application exiting cleanly.");
    return 43; // Distinctive exit code
}

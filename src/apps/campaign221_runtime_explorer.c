#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    // 1. Structured logging
    winios_log_info("EXPLORER", "Runtime Explorer starting...");

    // 2. Query PID and ArgBlock
    uint32_t pid = winios_get_pid();
    const winios_app_arg_block_t* args = winios_get_arg_block();

    char msg[128];
    winios_strcpy(msg, "Process running with PID: ");
    char pid_str[16];
    winios_itoa((int32_t)pid, pid_str, 10);
    winios_strcpy(msg + winios_strlen(msg), pid_str);
    winios_log_info("EXPLORER", msg);

    // 3. Create display window
    winios_wid_t wid = winios_window_create("Runtime Explorer", 50, 50, 480, 320);
    if (wid > 0) {
        // Draw background: Dark Blue
        winios_surface_fill(wid, 0, 0, 480, 320, WINIOS_COLOR_BLUE);
        // Draw inner status panel: Gray
        winios_surface_fill(wid, 20, 40, 440, 240, WINIOS_COLOR_GRAY);
        // Draw header bar: White
        winios_surface_fill(wid, 20, 40, 440, 30, WINIOS_COLOR_WHITE);
        winios_surface_present(wid);
    }

    // 4. Test Persistent Storage (Read/Write Config)
    const char* cfg_file = "explorer.cfg";
    const char* sample_cfg = "theme=dark\nrefresh_rate=60\nautosave=1\n";
    int32_t w_res = winios_storage_write_file(cfg_file, sample_cfg, winios_strlen(sample_cfg));
    if (w_res >= 0) {
        winios_log_info("EXPLORER", "Config successfully saved to private storage.");
    }

    char read_buf[64];
    winios_memset(read_buf, 0, sizeof(read_buf));
    size_t bytes_read = 0;
    int32_t r_res = winios_storage_read_file(cfg_file, read_buf, sizeof(read_buf) - 1, &bytes_read);
    if (r_res >= 0 && bytes_read > 0) {
        winios_log_info("EXPLORER", "Config successfully read from private storage.");
    }

    // 5. Poll event loop (non-blocking)
    winios_event_t ev;
    for (int i = 0; i < 5; ++i) {
        if (winios_poll_event(&ev) == 0 && ev.type != WINIOS_EVENT_NONE) {
            winios_log_debug("EXPLORER", "Received event in guest loop.");
        }
    }

    winios_log_info("EXPLORER", "Runtime Explorer exiting cleanly.");
    return 42; // Distinctive exit code for test assertion
}

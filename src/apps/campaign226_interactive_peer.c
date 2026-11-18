#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    winios_log_info("PEER", "Campaign 226 Interactive Peer Guest Application starting...");

    // 1. Create Peer Window (420x220 at 100, 320) via ABI v2.0
    uint64_t h_peer = winios_window_create_v2("Interactive Peer", 100, 320, 420, 220, 0);
    if (h_peer == 0) {
        winios_log_error("PEER", "Failed to create peer window");
        winios_exit(1);
        return 1;
    }

    // 2. Submit Text Input Event to Own Window
    const char* greeting = "Peer Active Ready";
    winios_text_input_submit(h_peer, greeting, 17);

    // 3. Read Desktop Clipboard set by primary app
    char clip_buf[128];
    winios_memset(clip_buf, 0, sizeof(clip_buf));
    size_t actual_len = 0;
    int64_t clip_res = winios_clipboard_get_v2(clip_buf, sizeof(clip_buf) - 1, &actual_len);
    (void)clip_res;

    // 4. Post Desktop Notification via Virtual Notification Service
    winios_notification_post(
        "Interactive Peer",
        "Multi-window environment synchronized",
        "system",
        4000
    );

    // 5. Minimize and Restore Peer Window
    winios_window_set_state_v2(h_peer, WINIOS_WINDOW_STATE_MINIMIZED);
    winios_window_set_state_v2(h_peer, WINIOS_WINDOW_STATE_VISIBLE);

    // 6. Poll for the submitted text event
    winios_desktop_event_t ev;
    winios_memset(&ev, 0, sizeof(ev));
    winios_event_poll_v2(h_peer, &ev);

    winios_log_info("PEER", "Campaign 226 Interactive Peer completed, exiting 94.");
    winios_exit(94);
    return 94;
}

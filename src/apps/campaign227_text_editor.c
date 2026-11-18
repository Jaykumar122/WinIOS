#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    winios_log_info("EDITOR", "WinIOS Text Editor starting...");

    // 1. Create Editor Window (480x320 at 60, 60) via ABI v2.0
    uint64_t win_h = winios_window_create_v2("WinIOS Text Editor", 60, 60, 480, 320, 0);
    if (win_h == 0) {
        winios_log_error("EDITOR", "Failed to create editor window");
        winios_exit(1);
        return 1;
    }

    // 2. Open document stream for notes.txt (doc handle 0x100000001, mode 3 = ReadWrite)
    uint64_t doc_h = 0x100000001ULL;
    uint64_t stream_h = 0;
    int st_res = winios_doc_stream_open(doc_h, 3 /* ReadWrite */, &stream_h);
    if (st_res == 0 && stream_h != 0) {
        // 3. Write document contents
        const char content[] = "Campaign 227 Document Editor Active\nText Buffer Verified.\n";
        uint64_t written = 0;
        winios_doc_stream_write(stream_h, content, sizeof(content) - 1, &written);

        // 4. Close stream to commit atomic save
        winios_doc_stream_close(stream_h);
    }

    // 5. Grant read access to Document Viewer (PID 96)
    uint64_t grant_token = 0;
    winios_doc_grant_access(doc_h, 96, 1 /* Read */, &grant_token);

    // 6. Set Desktop Clipboard Banner
    const char* clip_msg = "Campaign 227 Text Editor Active";
    winios_clipboard_set_v2(clip_msg, 31);

    // 7. Destroy window cleanly
    winios_window_destroy_v2(win_h);

    winios_log_info("EDITOR", "Text editor completed operations, exiting 95.");
    winios_exit(95);
    return 95;
}

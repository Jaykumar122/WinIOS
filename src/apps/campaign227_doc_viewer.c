#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    winios_log_info("VIEWER", "WinIOS Document Viewer starting...");

    // 1. Create Viewer Window (400x300 at 560, 60) via ABI v2.0
    uint64_t win_h = winios_window_create_v2("WinIOS Document Viewer", 560, 60, 400, 300, 0);
    if (win_h == 0) {
        winios_log_error("VIEWER", "Failed to create viewer window");
        winios_exit(1);
        return 1;
    }

    // 2. Query document metadata for notes.txt (doc handle 0x100000001)
    uint64_t doc_h = 0x100000001ULL;
    winios_doc_metadata_t meta;
    winios_memset(&meta, 0, sizeof(meta));
    int meta_res = winios_doc_get_metadata(doc_h, &meta);
    (void)meta_res;

    // 3. Open document stream in Read mode (1 = Read)
    uint64_t stream_h = 0;
    int st_res = winios_doc_stream_open(doc_h, 1 /* Read */, &stream_h);
    if (st_res == 0 && stream_h != 0) {
        // 4. Read document contents
        char read_buf[256];
        winios_memset(read_buf, 0, sizeof(read_buf));
        uint64_t bytes_read = 0;
        winios_doc_stream_read(stream_h, read_buf, sizeof(read_buf) - 1, &bytes_read);

        // 5. Close stream
        winios_doc_stream_close(stream_h);
    }

    // 6. Read Desktop Clipboard set by editor
    char clip_buf[64];
    winios_memset(clip_buf, 0, sizeof(clip_buf));
    size_t clip_len = 0;
    winios_clipboard_get_v2(clip_buf, sizeof(clip_buf) - 1, &clip_len);

    // 7. Destroy window cleanly
    winios_window_destroy_v2(win_h);

    winios_log_info("VIEWER", "Document viewer completed operations, exiting 96.");
    winios_exit(96);
    return 96;
}

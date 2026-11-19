#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    winios_log_info("DOCS_APP", "WinIOS App Docs workload running...");

    // 1. Create document window (480x350 at 100, 100)
    uint64_t win_h = winios_window_create_v2("App Docs", 100, 100, 480, 350, 0);
    if (win_h == 0) {
        winios_log_error("DOCS_APP", "Failed to create docs window");
        winios_exit(1);
        return 1;
    }

    // 2. Query recent documents via Category C ABI v2.1
    winios_recent_doc_t recents[4];
    winios_memset(recents, 0, sizeof(recents));
    uint32_t count = 0;
    int res = winios_doc_get_recent(recents, 4, &count);
    (void)res;

    // 3. Open document stream if recent exists, or trigger picker
    uint64_t stream_h = 0;
    if (count > 0 && recents[0].doc_handle != 0) {
        winios_doc_stream_open(recents[0].doc_handle, 1 /* Read */, &stream_h);
    } else {
        uint32_t req_id = 0;
        winios_doc_open_picker(win_h, ".txt", &req_id);
    }

    // 4. Render document viewer interface
    winios_draw_text(win_h, 30, 40, "WinIOS Document Viewer v2.4", 0xFFFFFFFF);
    winios_draw_text(win_h, 30, 70, count > 0 ? recents[0].display_name : "No Recent Document", 0xFF34C759);
    winios_surface_present(win_h);

    if (stream_h != 0) {
        winios_doc_stream_close(stream_h);
    }

    winios_log_info("DOCS_APP", "Docs workload completed successfully, exiting 103.");
    winios_exit(103);
    return 103;
}

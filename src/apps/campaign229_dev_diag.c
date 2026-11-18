#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    winios_log_info("DEVDIAG", "WinIOS Developer Diagnostics workload starting (PID 95)...");

    // 1. Create Developer Diagnostics Window (500x380 at 60, 60) via ABI v2.0
    uint64_t win_h = winios_window_create_v2("Developer Diagnostics", 60, 60, 500, 380, 0);
    if (win_h == 0) {
        winios_log_error("DEVDIAG", "Failed to create devdiag window");
        winios_exit(1);
        return 1;
    }

    // 2. Query initial settings snapshot via Category C ABI v2.2 (Syscall 581)
    winios_settings_snapshot_t snap;
    winios_memset(&snap, 0, sizeof(snap));
    int res = winios_settings_get(&snap);
    if (res != 0) {
        winios_log_error("DEVDIAG", "Failed to query system settings");
        winios_exit(2);
        return 2;
    }

    // 3. Enumerate tasks via Syscall 586
    winios_task_entry_t tasks[16];
    winios_memset(tasks, 0, sizeof(tasks));
    uint32_t task_count = 0;
    int enum_res = winios_task_enumerate(tasks, 16, &task_count);
    (void)enum_res;

    // 4. Query Diagnostic Event Stream via Category C ABI v2.3 (Syscall 591)
    winios_diag_event_record_t records[16];
    winios_memset(records, 0, sizeof(records));
    winios_diag_query_req_t req;
    winios_memset(&req, 0, sizeof(req));
    req.after_sequence = 0;
    req.max_entries = 16;
    req.min_level = 0;
    req.category_filter = 0;

    int qres = winios_diag_query_events(&req, records);
    if (qres < 0) {
        winios_log_error("DEVDIAG", "Diagnostic query failed");
        winios_exit(3);
        return 3;
    }

    // 5. Test sequence-based pagination
    if (req.returned_entries > 0) {
        uint64_t last_seq = records[req.returned_entries - 1].sequence;
        winios_diag_query_req_t page_req;
        winios_memset(&page_req, 0, sizeof(page_req));
        page_req.after_sequence = last_seq;
        page_req.max_entries = 8;
        winios_diag_event_record_t page_records[8];
        winios_memset(page_records, 0, sizeof(page_records));
        int pres = winios_diag_query_events(&page_req, page_records);
        (void)pres;
    }

    // 6. Cleanly destroy window
    winios_window_destroy_v2(win_h);

    winios_log_info("DEVDIAG", "Developer diagnostics workload completed successfully, exiting 95.");
    winios_exit(95);
    return 95;
}

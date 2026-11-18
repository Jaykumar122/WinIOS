#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    winios_log_info("PROCMON", "Process Monitor initializing...");
    uint32_t my_pid = winios_get_pid();

    // 1. Create Process Monitor Window
    winios_wid_t wid = winios_window_create("Process Monitor", 120, 120, 500, 300);
    if (wid > 0) {
        // Console style: Black background
        winios_surface_fill(wid, 0, 0, 500, 300, WINIOS_COLOR_BLACK);
        // Green status bar
        winios_surface_fill(wid, 10, 10, 480, 25, WINIOS_COLOR_GREEN);
        // Gray process list background
        winios_surface_fill(wid, 10, 45, 480, 240, WINIOS_COLOR_GRAY);
        winios_surface_present(wid);
    }

    // 2. Query process status
    winios_process_info_t info;
    winios_memset(&info, 0, sizeof(info));
    int64_t res = winios_syscall2(WINIOS_SYS_GET_PROCESS_STATUS, (uint64_t)my_pid, (uint64_t)&info);
    if (res == 0) {
        winios_log_info("PROCMON", "Queried process status successfully.");
    }

    // 3. Poll events
    winios_event_t ev;
    winios_poll_event(&ev);

    winios_log_info("PROCMON", "Process Monitor terminating.");
    return 44; // Distinctive exit code
}

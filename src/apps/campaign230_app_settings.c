#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    winios_log_info("SETTINGS_APP", "WinIOS App Settings workload running...");

    // 1. Create settings window (450x320 at 80, 80)
    uint64_t win_h = winios_window_create_v2("App Settings", 80, 80, 450, 320, 0);
    if (win_h == 0) {
        winios_log_error("SETTINGS_APP", "Failed to create settings window");
        winios_exit(1);
        return 1;
    }

    // 2. Query initial settings snapshot via Category C ABI v2.2 (Syscall 581)
    winios_settings_snapshot_t snap;
    winios_memset(&snap, 0, sizeof(snap));
    int res = winios_settings_get(&snap);
    if (res != 0) {
        winios_log_error("SETTINGS_APP", "Failed to read system settings");
        winios_exit(2);
        return 2;
    }

    // 3. Render settings status
    winios_draw_text(win_h, 30, 40, "WinIOS Settings Service Client", 0xFFFFFFFF);
    winios_draw_text(win_h, 30, 70, snap.theme_mode == 1 ? "Theme: Dark" : "Theme: Light", 0xFF007AFF);
    winios_surface_present(win_h);

    winios_log_info("SETTINGS_APP", "Settings workload completed successfully, exiting 102.");
    winios_exit(102);
    return 102;
}

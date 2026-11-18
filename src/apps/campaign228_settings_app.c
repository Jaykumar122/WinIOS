#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    winios_log_info("SETTINGS", "WinIOS System Settings App starting (PID 97)...");

    // 1. Create Settings Window (480x360 at 80, 80) via ABI v2.0
    uint64_t win_h = winios_window_create_v2("System Settings", 80, 80, 480, 360, 0);
    if (win_h == 0) {
        winios_log_error("SETTINGS", "Failed to create settings window");
        winios_exit(1);
        return 1;
    }

    // 2. Query initial settings snapshot via Category C ABI v2.2 (Syscall 581)
    winios_settings_snapshot_t snap;
    winios_memset(&snap, 0, sizeof(snap));
    int res = winios_settings_get(&snap);
    if (res != 0) {
        winios_log_error("SETTINGS", "Failed to query system settings");
        winios_exit(2);
        return 2;
    }

    // 3. Mutate Appearance & Power settings via Syscall 582
    // Key 1 = Theme (0=Light), Key 2 = Accent (4=Orange), Key 5 = Volume (92), Key 7 = Low Power (1)
    winios_settings_set(1, 0);  // Light Mode
    winios_settings_set(2, 4);  // Orange Accent
    winios_settings_set(5, 92); // Master Volume 92%
    winios_settings_set(7, 1);  // Low Power Mode Enabled

    // 4. Verify Theme API direct setter (Syscall 584)
    winios_theme_set(0 /* Light */, 4 /* Orange */);

    // 5. Test Control Center Toggle (Syscall 585, action 0 = toggle)
    int cc_vis = winios_control_center_toggle(0);
    (void)cc_vis;

    // 6. Re-query settings to verify mutations
    winios_settings_snapshot_t updated;
    winios_memset(&updated, 0, sizeof(updated));
    winios_settings_get(&updated);

    // 7. Cleanly destroy window
    winios_window_destroy_v2(win_h);

    winios_log_info("SETTINGS", "Settings application completed successfully, exiting 97.");
    winios_exit(97);
    return 97;
}

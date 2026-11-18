#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    winios_log_info("LIFECYCLE", "WinIOS Lifecycle & Settings Demo starting (PID 96)...");

    // 1. Create Lifecycle Demo Window (460x340 at 120, 120) via ABI v2.0
    uint64_t win_h = winios_window_create_v2("Lifecycle & Settings Demo", 120, 120, 460, 340, 0);
    if (win_h == 0) {
        winios_log_error("LIFECYCLE", "Failed to create demo window");
        winios_exit(1);
        return 1;
    }

    // 2. Mutate theme to Dark Mode (1) and Blue Accent (0) via Syscall 584
    int theme_res = winios_theme_set(1 /* Dark */, 0 /* Blue */);
    if (theme_res != 0) {
        winios_log_error("LIFECYCLE", "Failed to set theme");
        winios_exit(2);
        return 2;
    }

    // 3. Mutate accessibility settings via Syscall 590
    // text_scale = 125%, high_contrast = 0
    int a11y_set_res = winios_accessibility_set(125, 0);
    if (a11y_set_res != 0) {
        winios_log_error("LIFECYCLE", "Failed to set accessibility");
        winios_exit(3);
        return 3;
    }

    // 4. Query accessibility to verify
    winios_accessibility_info_t a11y;
    winios_memset(&a11y, 0, sizeof(a11y));
    int a11y_get_res = winios_accessibility_get(&a11y);
    if (a11y_get_res != 0) {
        winios_log_error("LIFECYCLE", "Failed to get accessibility");
        winios_exit(4);
        return 4;
    }

    // 5. Test Control Center Toggle (Syscall 585, action 0 = toggle)
    int cc_res = winios_control_center_toggle(0);
    (void)cc_res;

    // 6. Query updated settings snapshot via Syscall 581
    winios_settings_snapshot_t snap;
    winios_memset(&snap, 0, sizeof(snap));
    int snap_res = winios_settings_get(&snap);
    if (snap_res != 0) {
        winios_log_error("LIFECYCLE", "Failed to get settings snapshot");
        winios_exit(5);
        return 5;
    }

    // 7. Cleanly destroy window
    winios_window_destroy_v2(win_h);

    winios_log_info("LIFECYCLE", "Lifecycle demo workload completed successfully, exiting 96.");
    winios_exit(96);
    return 96;
}

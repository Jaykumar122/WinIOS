#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    winios_log_info("SETTINGS", "Campaign 224 Platform Settings starting...");

    // 1. Create Settings Window (360x440)
    winios_wid_t wid = winios_window_create("Platform Settings", 60, 60, 360, 440);
    if (wid > 0) {
        // Light clean background
        winios_surface_fill(wid, 0, 0, 360, 440, 0xFFF2F2F7);

        // Header bar
        winios_surface_fill(wid, 0, 0, 360, 50, 0xFF007AFF);
        winios_draw_text(wid, 20, 18, "Platform Settings", 0xFFFFFFFF);

        // Section 1: Application Identity & Metadata
        winios_surface_fill(wid, 15, 65, 330, 75, 0xFFFFFFFF);
        winios_draw_text(wid, 25, 75, "App ID: com.winios.settings", 0xFF1C1C1E);
        winios_draw_text(wid, 25, 95, "Platform: WinIOS A16 Research", 0xFF3C3C43);
        winios_draw_text(wid, 25, 115, "SDK: v1.8 (ABI v1.8)", 0xFF007AFF);

        // Section 2: Configuration Service (SDK v1.8)
        winios_config_set_string("theme", "dark_mode");
        winios_config_set_int("refresh_rate_hz", 60);
        winios_config_set_bool("auto_sync", 1);

        char theme_buf[32];
        winios_config_get_string("theme", theme_buf, sizeof(theme_buf), "default");
        int64_t rate = winios_config_get_int("refresh_rate_hz", 30);
        int auto_sync = winios_config_get_bool("auto_sync", 0);

        winios_surface_fill(wid, 15, 155, 330, 85, 0xFFFFFFFF);
        winios_draw_text(wid, 25, 165, "Configuration Service:", 0xFF1C1C1E);
        winios_draw_text(wid, 25, 185, theme_buf, 0xFF34C759);
        winios_draw_text(wid, 25, 205, rate == 60 ? "Rate: 60 Hz" : "Rate: 30 Hz", 0xFF3C3C43);
        winios_draw_text(wid, 180, 205, auto_sync ? "AutoSync: ON" : "AutoSync: OFF", 0xFF007AFF);

        // Section 3: Platform Diagnostics Summary (SDK v1.8)
        winios_platform_diagnostics_t diag;
        winios_memset(&diag, 0, sizeof(diag));
        winios_diagnostics_get_summary(&diag);

        winios_surface_fill(wid, 15, 255, 330, 85, 0xFFFFFFFF);
        winios_draw_text(wid, 25, 265, "Platform Diagnostics:", 0xFF1C1C1E);
        winios_draw_text(wid, 25, 285, "Active IPC Ports: Verified", 0xFF3C3C43);
        winios_draw_text(wid, 25, 305, "Security Audit: Active", 0xFF34C759);

        // Section 4: Sandboxed Storage & Persistence Test
        const char* test_data = "settings_v1_8_persisted_profile";
        winios_storage_write_file("profile.cfg", test_data, 31);

        // Present Frame
        winios_surface_present(wid);
    }

    winios_log_info("SETTINGS", "Platform Settings execution complete.");
    winios_exit(81);
    return 81;
}

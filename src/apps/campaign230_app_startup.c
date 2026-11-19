#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    winios_log_info("STARTUP", "WinIOS App Startup workload running...");

    // 1. Create main application window (400x300 at 50, 50)
    uint64_t win_h = winios_window_create_v2("App Startup", 50, 50, 400, 300, 0);
    if (win_h == 0) {
        winios_log_error("STARTUP", "Failed to create startup window");
        winios_exit(1);
        return 1;
    }

    // 2. Query application info via Category C ABI v2.4 (Syscall 592)
    winios_app_info_t info;
    winios_memset(&info, 0, sizeof(info));
    int res = winios_app_get_info("com.winios.appstartup", &info);
    (void)res;

    // 3. Render greeting on surface
    winios_draw_text(win_h, 30, 40, "WinIOS SDK v2.4 Startup Demo", 0xFFFFFFFF);
    winios_surface_present(win_h);

    winios_log_info("STARTUP", "Startup workload completed successfully, exiting 101.");
    winios_exit(101);
    return 101;
}

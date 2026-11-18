#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    winios_log_info("MONITOR", "WinIOS Task Monitor starting (PID 98)...");

    // 1. Create Task Monitor Window (400x300 at 520, 80) via ABI v2.0
    uint64_t win_h = winios_window_create_v2("Task & Accessibility Monitor", 520, 80, 400, 300, 0);
    if (win_h == 0) {
        winios_log_error("MONITOR", "Failed to create task monitor window");
        winios_exit(1);
        return 1;
    }

    // 2. Enumerate running tasks via Category C ABI v2.2 (Syscall 586)
    winios_task_entry_t tasks[16];
    winios_memset(tasks, 0, sizeof(tasks));
    uint32_t task_count = 0;
    int enum_res = winios_task_enumerate(tasks, 16, &task_count);
    (void)enum_res;

    // 3. Test Task Switching (Syscall 587) - cycle or target self/peer
    winios_task_switch(0); // Cycle next in task switcher

    // 4. Configure Accessibility settings via Syscall 590
    // text_scale = 140%, high_contrast = 1
    winios_accessibility_set(140, 1);

    // 5. Query Accessibility settings via Syscall 589
    winios_accessibility_info_t a11y;
    winios_memset(&a11y, 0, sizeof(a11y));
    winios_accessibility_get(&a11y);

    // 6. Cleanly destroy window
    winios_window_destroy_v2(win_h);

    winios_log_info("MONITOR", "Task monitor completed successfully, exiting 98.");
    winios_exit(98);
    return 98;
}

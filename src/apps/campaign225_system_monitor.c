#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    winios_log_info("SYSMON", "Campaign 225 System Monitor starting...");

    // 1. Create System Monitor Window (400x480)
    winios_wid_t wid = winios_window_create("System Monitor", 80, 80, 400, 480);
    if (wid > 0) {
        // Dark theme background
        winios_surface_fill(wid, 0, 0, 400, 480, 0xFF1C1C1E);

        // Header bar
        winios_surface_fill(wid, 0, 0, 400, 50, 0xFF2C2C2E);
        winios_draw_text(wid, 20, 18, "System Monitor", 0xFF0A84FF);

        // Section 1: Monotonic & Wall-Clock Timing
        winios_timespec_t ts;
        winios_memset(&ts, 0, sizeof(ts));
        int32_t ret_time = winios_time_get(&ts);

        winios_surface_fill(wid, 15, 65, 370, 75, 0xFF2C2C2E);
        if (ret_time == 0) {
            winios_draw_text(wid, 25, 95, "Clock: Active", 0xFF30D158);
        }

        // Section 2: Virtual Network Loopback Test
        const char net_payload[] = "WINIOS_NET_ECHO";
        int32_t ret_send = winios_net_send(net_payload, sizeof(net_payload));
        char recv_buf[32];
        winios_memset(recv_buf, 0, sizeof(recv_buf));
        size_t recvd_len = 0;
        int64_t ret_recv = winios_net_recv(recv_buf, sizeof(recv_buf), &recvd_len);

        winios_surface_fill(wid, 15, 155, 370, 75, 0xFF2C2C2E);
        if (ret_send == 0 && ret_recv > 0) {
            winios_draw_text(wid, 25, 185, "Network: Echo OK", 0xFF30D158);
        }

        // Section 3: Notification Service Integration
        uint32_t nid = winios_notification_post("Alert", "Services operational", "system", 4000);

        winios_surface_fill(wid, 15, 245, 370, 75, 0xFF2C2C2E);
        if (nid > 0) {
            winios_draw_text(wid, 25, 275, "Alert: Sent", 0xFF0A84FF);
        }

        // Section 4: Device Diagnostics Summary
        winios_device_diag_t diag;
        winios_memset(&diag, 0, sizeof(diag));
        int32_t ret_diag = winios_device_diag_summary(&diag);

        winios_surface_fill(wid, 15, 335, 370, 85, 0xFF2C2C2E);
        if (ret_diag == 0) {
            winios_draw_text(wid, 25, 365, "Diag: OK", 0xFF30D158);
        }

        // Present Frame
        winios_surface_present(wid);
    }

    winios_log_info("SYSMON", "System Monitor operations complete, exiting 92.");
    winios_exit(92);
    return 92;
}

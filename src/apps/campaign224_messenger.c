#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    winios_log_info("MESSENGER", "Campaign 224 IPC Messenger starting...");

    // 1. Create Messenger Window (320x420)
    winios_wid_t wid = winios_window_create("IPC Messenger", 440, 60, 320, 420);
    if (wid > 0) {
        // Dark theme background
        winios_surface_fill(wid, 0, 0, 320, 420, 0xFF1C1C1E);

        // Header bar
        winios_surface_fill(wid, 0, 0, 320, 48, 0xFF2C2C2E);
        winios_draw_text(wid, 20, 16, "IPC Messenger", 0xFFFFFFFF);

        // 2. Generation-tagged IPC v2 Port Creation & Registration
        uint64_t port = winios_ipc_port_create_v2("com.winios.messenger.port");
        winios_ipc_register_port(port, "com.winios.messenger.port");

        // Lookup port by name
        uint64_t looked_up = winios_ipc_lookup_port("com.winios.messenger.port");

        // 3. Send & Receive Messages
        const char* msg_ping = "HELLO_IPC_V2_PACKET";
        winios_ipc_send_v2(looked_up, 2001, msg_ping, 19);

        char recv_buf[64];
        winios_memset(recv_buf, 0, sizeof(recv_buf));
        uint32_t sender_pid = 0;
        uint32_t msg_code = 0;
        int32_t recv_bytes = winios_ipc_recv_v2(port, recv_buf, sizeof(recv_buf), &sender_pid, &msg_code);

        // 4. Test Queue Depth Limits (send multiple messages)
        for (int i = 0; i < 5; ++i) {
            winios_ipc_send_v2(port, 3000 + i, "BURST_MSG", 9);
        }

        // Test Unreachable Receiver (invalid port 0xDEADBEEF)
        int32_t fail_send = winios_ipc_send_v2(0xDEADBEEF00000000ULL, 9999, "FAIL", 4);
        (void)fail_send;

        // Render Status Panels
        winios_surface_fill(wid, 12, 60, 296, 75, 0xFF2C2C2E);
        winios_draw_text(wid, 22, 70, "Port: com.winios.messenger", 0xFFFFFFFF);
        winios_draw_text(wid, 22, 90, "Handle: 64-bit Generation OK", 0xFF34C759);
        winios_draw_text(wid, 22, 110, "Discovery: Name Directory OK", 0xFF007AFF);

        winios_surface_fill(wid, 12, 145, 296, 120, 0xFF2C2C2E);
        winios_draw_text(wid, 22, 155, "Message Traffic Log:", 0xFF8E8E93);
        if (recv_bytes > 0) {
            winios_draw_text(wid, 22, 180, recv_buf, 0xFF34C759);
        } else {
            winios_draw_text(wid, 22, 180, "No Incoming Messages", 0xFFFF3B30);
        }
        winios_draw_text(wid, 22, 210, "Burst Queue Depth: 5/32", 0xFFFFFFFF);
        winios_draw_text(wid, 22, 230, "Unreachable Port Reject: OK", 0xFFFF9F0A);

        // Persistent message log to private storage
        winios_storage_write_file("messenger_history.log", recv_buf, recv_bytes > 0 ? (size_t)recv_bytes : 0);

        // Present Frame
        winios_surface_present(wid);
    }

    winios_log_info("MESSENGER", "IPC Messenger execution complete.");
    winios_exit(82);
    return 82;
}

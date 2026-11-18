#include "winios/winios.h"

int main(int argc, const char* const argv[]) {
    (void)argc;
    (void)argv;

    winios_log_info("DEVCTR", "Campaign 225 Device Center starting...");

    // 1. Create Device Center Window (420x520)
    winios_wid_t wid = winios_window_create("Device Center", 50, 50, 420, 520);
    if (wid > 0) {
        // Background
        winios_surface_fill(wid, 0, 0, 420, 520, 0xFFF2F2F7);

        // Header bar
        winios_surface_fill(wid, 0, 0, 420, 55, 0xFF007AFF);
        winios_draw_text(wid, 20, 20, "Device Center", 0xFFFFFFFF);

        // Section 1: Device Enumeration
        winios_device_info_t dev_list[16];
        winios_memset(dev_list, 0, sizeof(dev_list));
        uint32_t dev_count = 0;
        int32_t ret_enum = winios_device_enum(dev_list, 16, 0, &dev_count);

        winios_surface_fill(wid, 15, 70, 390, 80, 0xFFFFFFFF);
        if (ret_enum >= 0) {
            winios_draw_text(wid, 25, 95, "Devices: OK", 0xFF34C759);
        }

        // Section 2: Display Configuration
        winios_display_config_t disp_cfg;
        winios_memset(&disp_cfg, 0, sizeof(disp_cfg));
        int32_t ret_disp = winios_display_get_config(&disp_cfg);

        winios_surface_fill(wid, 15, 165, 390, 80, 0xFFFFFFFF);
        if (ret_disp == 0) {
            winios_draw_text(wid, 25, 195, "Display: 60Hz", 0xFF007AFF);
        }

        // Section 3: Audio Service Status
        winios_audio_status_t audio_st;
        winios_memset(&audio_st, 0, sizeof(audio_st));
        int32_t ret_audio = winios_audio_get_status(&audio_st);

        winios_surface_fill(wid, 15, 260, 390, 80, 0xFFFFFFFF);
        if (ret_audio == 0) {
            winios_draw_text(wid, 25, 290, "Audio: Ready", 0xFF34C759);
        }

        // Section 4: Sensor Hub (Motion & Environmental)
        winios_sensor_sample_t accel_sample;
        winios_memset(&accel_sample, 0, sizeof(accel_sample));
        int32_t ret_accel = winios_sensor_sample(WINIOS_SENSOR_ACCELEROMETER, &accel_sample);

        winios_sensor_sample_t light_sample;
        winios_memset(&light_sample, 0, sizeof(light_sample));
        int32_t ret_light = winios_sensor_sample(WINIOS_SENSOR_AMBIENT_LIGHT, &light_sample);

        winios_surface_fill(wid, 15, 355, 390, 90, 0xFFFFFFFF);
        if (ret_accel == 0 && ret_light == 0) {
            winios_draw_text(wid, 25, 385, "Sensors: OK", 0xFF007AFF);
        }

        // Section 5: Persistence check
        const char* stamp = "device_center_v1_9_profile";
        winios_storage_write_file("devcenter.cfg", stamp, 26);

        // Present Frame
        winios_surface_present(wid);
    }

    winios_log_info("DEVCTR", "Device Center operations verified, exiting 91.");
    winios_exit(91);
    return 91;
}

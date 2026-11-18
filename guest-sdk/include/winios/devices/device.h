#ifndef WINIOS_GUEST_SDK_DEVICES_DEVICE_H
#define WINIOS_GUEST_SDK_DEVICES_DEVICE_H

#include "winios/winios_types.h"
#include "winios/system/syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

// Device Classes
#define WINIOS_DEV_CLASS_UNKNOWN       0
#define WINIOS_DEV_CLASS_DISPLAY       1
#define WINIOS_DEV_CLASS_POINTER       2
#define WINIOS_DEV_CLASS_KEYBOARD      3
#define WINIOS_DEV_CLASS_AUDIO_OUT     4
#define WINIOS_DEV_CLASS_AUDIO_IN      5
#define WINIOS_DEV_CLASS_SENSOR        6
#define WINIOS_DEV_CLASS_NETWORK       7
#define WINIOS_DEV_CLASS_CLOCK         8
#define WINIOS_DEV_CLASS_NOTIFICATION  9

// Device States
#define WINIOS_DEV_STATE_UNINIT        0
#define WINIOS_DEV_STATE_READY         1
#define WINIOS_DEV_STATE_RUNNING       2
#define WINIOS_DEV_STATE_SUSPENDED     3
#define WINIOS_DEV_STATE_STOPPED       4
#define WINIOS_DEV_STATE_ERROR         5
#define WINIOS_DEV_STATE_REMOVED       6

// Sensor Types
#define WINIOS_SENSOR_ACCELEROMETER    1
#define WINIOS_SENSOR_GYROSCOPE        2
#define WINIOS_SENSOR_AMBIENT_LIGHT    3

#pragma pack(push, 8)

typedef struct {
    uint32_t device_id;
    uint32_t generation;
    uint32_t device_class;
    uint32_t state;
    char     name[64];
    char     provider[32];
    uint32_t version;
    uint32_t required_capability;
    uint32_t flags;
    uint64_t handle;
} winios_device_info_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    float    scale;
    uint32_t refresh_rate_hz;
    uint32_t bits_per_pixel;
    uint32_t orientation;
    uint32_t flags;
} winios_display_config_t;

typedef struct {
    uint32_t state;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t queued_bytes;
    uint32_t buffer_capacity;
    uint64_t total_frames_submitted;
    uint64_t total_bytes_submitted;
    uint64_t total_underruns;
    float    volume;
    uint8_t  is_muted;
    uint8_t  pad[3];
} winios_audio_status_t;

typedef struct {
    uint32_t sensor_type;
    uint32_t sequence;
    uint64_t timestamp_us;
    float    values[4];
    uint32_t status;
    uint32_t pad;
} winios_sensor_sample_t;

typedef struct {
    uint64_t monotonic_us;
    uint64_t wall_time_us;
    int32_t  timezone_offset_minutes;
    uint32_t timer_frequency_hz;
} winios_timespec_t;

typedef struct {
    size_t   total_devices;
    size_t   active_devices;
    uint64_t audio_bytes_submitted;
    uint64_t audio_underruns;
    uint64_t sensor_samples_generated;
    uint64_t network_packets_sent;
    uint64_t network_packets_received;
    uint64_t network_packets_dropped;
    uint64_t notifications_posted;
    size_t   notifications_active;
} winios_device_diag_t;

#pragma pack(pop)

// Syscall C API Prototypes
int32_t  winios_device_enum(winios_device_info_t* buf, uint32_t max_count, uint32_t class_filter, uint32_t* out_count);
int32_t  winios_device_get_info(uint64_t handle_or_id, winios_device_info_t* out_info);
uint64_t winios_device_open(uint32_t device_id, uint32_t flags);
int32_t  winios_device_close(uint64_t handle);

int32_t  winios_display_get_config(winios_display_config_t* out_cfg);
int32_t  winios_display_set_config(const winios_display_config_t* in_cfg);

int32_t  winios_audio_submit(const void* pcm_data, size_t size_bytes);
int32_t  winios_audio_get_status(winios_audio_status_t* out_status);

int32_t  winios_sensor_sample(uint32_t sensor_type, winios_sensor_sample_t* out_sample);
int32_t  winios_sensor_set_enabled(uint32_t sensor_type, int32_t enabled);

int32_t  winios_net_send(const void* data, size_t length);
int64_t  winios_net_recv(void* buf, size_t max_len, size_t* out_len);

int32_t  winios_time_get(winios_timespec_t* out_spec);
uint32_t winios_notification_post(const char* title, const char* body, const char* category, uint32_t duration_ms);
int32_t  winios_device_diag_summary(winios_device_diag_t* out_diag);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_GUEST_SDK_DEVICES_DEVICE_H

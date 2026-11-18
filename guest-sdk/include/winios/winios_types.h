#ifndef WINIOS_GUEST_SDK_TYPES_H
#define WINIOS_GUEST_SDK_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WINIOS_SDK_VERSION_MAJOR 2
#define WINIOS_SDK_VERSION_MINOR 3
#define WINIOS_SDK_VERSION_PATCH 0
#define WINIOS_SDK_VERSION       0x00020300

#define WINIOS_CATEGORY_C_ABI_VERSION_2_3 0x00020300
#define WINIOS_CATEGORY_C_ABI_VERSION_2_2 0x00020200

#define WINIOS_ABI_CATEGORY_A_DARWIN_BSD 0x01
#define WINIOS_ABI_CATEGORY_B_MSG_TRAP   0x02
#define WINIOS_ABI_CATEGORY_C_ACCEL      0x03

// Common error codes
typedef enum {
    WINIOS_SUCCESS            = 0,
    WINIOS_ERR_INVALID_PARAM  = -1,
    WINIOS_ERR_NO_MEMORY      = -2,
    WINIOS_ERR_NOT_FOUND      = -3,
    WINIOS_ERR_ACCESS_DENIED  = -4,
    WINIOS_ERR_IO             = -5,
    WINIOS_ERR_BUSY           = -6,
    WINIOS_ERR_TIMEOUT        = -7,
    WINIOS_ERR_UNSUPPORTED    = -8,
    WINIOS_ERR_OVERFLOW       = -9
} winios_status_t;

// Standard BSD syscall numbers (Category A)
#define WINIOS_SYS_EXIT          1
#define WINIOS_SYS_FORK          2
#define WINIOS_SYS_READ          3
#define WINIOS_SYS_WRITE         4
#define WINIOS_SYS_OPEN          5
#define WINIOS_SYS_CLOSE         6
#define WINIOS_SYS_WAIT4         7
#define WINIOS_SYS_GETPID        20
#define WINIOS_SYS_PIPE          42
#define WINIOS_SYS_SCHED_YIELD   334

// WinIOS Accelerated Syscalls (Category C)
#define WINIOS_SYS_WINDOW_CREATE       500
#define WINIOS_SYS_WINDOW_DESTROY      501
#define WINIOS_SYS_SURFACE_UPDATE      502
#define WINIOS_SYS_SURFACE_PRESENT     503
#define WINIOS_SYS_POLL_EVENT          504
#define WINIOS_SYS_SURFACE_FILL        505
#define WINIOS_SYS_SET_WINDOW_TITLE    506
#define WINIOS_SYS_GET_ABI_VERSION     507
#define WINIOS_SYS_WINDOW_GET_GEOMETRY 508
#define WINIOS_SYS_WINDOW_SET_GEOMETRY 509
#define WINIOS_SYS_WINDOW_SET_FOCUS    510
#define WINIOS_SYS_WINDOW_LIST         511
#define WINIOS_SYS_CREATE_TIMER        512
// Category C ABI v1.2 Extensions
#define WINIOS_SYS_ALLOC_HEAP          513
#define WINIOS_SYS_FREE_HEAP           514
#define WINIOS_SYS_GET_RUNTIME_INFO    515
#define WINIOS_SYS_STORAGE_READ        516
#define WINIOS_SYS_STORAGE_WRITE       517
#define WINIOS_SYS_IPC_PORT_CREATE     518
#define WINIOS_SYS_IPC_SEND            519
#define WINIOS_SYS_IPC_RECV            520
#define WINIOS_SYS_PREFS_GET_SET       521
// Category C ABI v1.3 Extensions
#define WINIOS_SYS_MUTEX_LOCK          522
#define WINIOS_SYS_MUTEX_UNLOCK        523
#define WINIOS_SYS_THREAD_CREATE       524
#define WINIOS_SYS_THREAD_EXIT         525
#define WINIOS_SYS_THREAD_JOIN         526
#define WINIOS_SYS_MSG_TRAP            527
// Category C ABI v1.5 Extensions (Campaign 221)
#define WINIOS_SYS_LOG                 528
#define WINIOS_SYS_GET_APP_INFO        529
#define WINIOS_SYS_SPAWN_PROCESS       530
#define WINIOS_SYS_GET_PROCESS_STATUS  531
#define WINIOS_SYS_RESOURCE_OPEN       532
// Category C ABI v1.6 Extensions (Campaign 222)
#define WINIOS_SYS_WINDOW_SET_VISIBILITY 533
#define WINIOS_SYS_CLIPBOARD_GET         534
#define WINIOS_SYS_CLIPBOARD_SET         535
#define WINIOS_SYS_APP_SUPERVISOR_NOTIFY 536
// Category C ABI v1.8 Extensions (Campaign 224)
#define WINIOS_SYS_CONFIG_GET            537
#define WINIOS_SYS_CONFIG_SET            538
#define WINIOS_SYS_LOG_LEVELED           539
#define WINIOS_SYS_DIAG_SUMMARY          540
#define WINIOS_SYS_IPC_PORT_CREATE_V2    541
#define WINIOS_SYS_IPC_SEND_V2           542
#define WINIOS_SYS_IPC_RECV_V2           543
#define WINIOS_SYS_IPC_LOOKUP_PORT       544
#define WINIOS_SYS_IPC_REGISTER_PORT     545
// Category C ABI v1.9 Extensions (Campaign 225)
#define WINIOS_SYS_DEVICE_ENUM           546
#define WINIOS_SYS_DEVICE_GET_INFO       547
#define WINIOS_SYS_DEVICE_OPEN           548
#define WINIOS_SYS_DEVICE_CLOSE          549
#define WINIOS_SYS_DISPLAY_GET_CONFIG    550
#define WINIOS_SYS_DISPLAY_SET_CONFIG    551
#define WINIOS_SYS_AUDIO_SUBMIT          552
#define WINIOS_SYS_AUDIO_GET_STATUS      553
#define WINIOS_SYS_SENSOR_SAMPLE         554
#define WINIOS_SYS_SENSOR_SET_ENABLED    555
#define WINIOS_SYS_NET_SEND              556
#define WINIOS_SYS_NET_RECV              557
#define WINIOS_SYS_TIME_GET              558
#define WINIOS_SYS_NOTIFICATION_POST     559
#define WINIOS_SYS_DEVICE_DIAG_SUMMARY   560
// Category C ABI v2.0 Extensions (Campaign 226)
#define WINIOS_SYS_WINDOW_CREATE_V2      561
#define WINIOS_SYS_WINDOW_DESTROY_V2     562
#define WINIOS_SYS_WINDOW_SET_STATE      563
#define WINIOS_SYS_WINDOW_GET_STATE      564
#define WINIOS_SYS_WINDOW_INVALIDATE_RECT 565
#define WINIOS_SYS_EVENT_POLL_V2         566
#define WINIOS_SYS_EVENT_WAIT            567
#define WINIOS_SYS_TEXT_INPUT_SUBMIT     568
#define WINIOS_SYS_CLIPBOARD_GET_V2      569
#define WINIOS_SYS_CLIPBOARD_SET_V2      570
// Category C ABI v2.1 Extensions (Campaign 227)
#define WINIOS_SYS_DOC_OPEN_PICKER       571
#define WINIOS_SYS_DOC_SAVE_PICKER       572
#define WINIOS_SYS_DOC_GET_METADATA      573
#define WINIOS_SYS_DOC_STREAM_OPEN       574
#define WINIOS_SYS_DOC_STREAM_READ       575
#define WINIOS_SYS_DOC_STREAM_WRITE      576
#define WINIOS_SYS_DOC_STREAM_SEEK       577
#define WINIOS_SYS_DOC_STREAM_CLOSE      578
#define WINIOS_SYS_DOC_GRANT_ACCESS      579
#define WINIOS_SYS_DOC_GET_RECENT        580

#pragma pack(push, 4)
typedef struct {
    uint64_t doc_handle;
    uint32_t owner_pid;
    uint32_t is_read_only;
    uint64_t file_size;
    uint64_t created_time_us;
    uint64_t modified_time_us;
    char     display_name[64];
    char     canonical_path[128];
    char     mime_type[32];
} winios_doc_metadata_t;

typedef struct {
    uint64_t stream_handle;
    uint64_t doc_handle;
    uint32_t mode;
    uint32_t reserved;
    uint64_t current_cursor;
    uint64_t stream_size;
} winios_doc_stream_info_t;

typedef struct {
    uint64_t doc_handle;
    uint32_t grantee_pid;
    uint32_t mode;
    uint64_t expiry_us;
    uint64_t out_grant_token;
} winios_doc_grant_req_t;

typedef struct {
    uint64_t doc_handle;
    uint64_t last_opened_time_us;
    char     display_name[64];
    char     canonical_path[128];
} winios_recent_doc_t;

// ABI v2.2 Settings Snapshot (48 bytes)
typedef struct {
    uint32_t theme_mode;          // 0=Light, 1=Dark, 2=System
    uint32_t accent_color;        // 0=Blue..7=Graphite
    uint32_t display_width;
    uint32_t display_height;
    uint32_t display_scale;       // 100, 125, 150, 200
    uint32_t display_orientation; // 0=Landscape, 1=Portrait
    uint32_t master_volume;       // 0..100
    uint32_t is_muted;            // 0 or 1
    uint32_t low_power_mode;      // 0 or 1
    uint32_t text_scale_percent;  // 100..200
    uint32_t high_contrast;       // 0 or 1
    uint32_t network_enabled;     // 0 or 1
} winios_settings_snapshot_t;

// ABI v2.2 Task Descriptor (120 bytes)
typedef struct {
    uint32_t pid;
    uint32_t state;               // 0=Running, 1=Suspended, 2=Zombie, 3=Crashed
    uint32_t is_foreground;       // 0 or 1
    uint32_t pad;
    uint64_t primary_window;
    char     name[32];
    char     bundle_id[64];
} winios_task_entry_t;

// ABI v2.2 Accessibility Descriptor (32 bytes)
typedef struct {
    uint32_t text_scale_percent;
    uint32_t high_contrast;
    uint32_t reduce_motion;
    uint32_t focus_indicator_visible;
    uint32_t screen_reader_active;
    uint32_t pad[3];
} winios_accessibility_info_t;

#define WINIOS_SYS_SETTINGS_GET           581
#define WINIOS_SYS_SETTINGS_SET           582
#define WINIOS_SYS_THEME_GET              583
#define WINIOS_SYS_THEME_SET              584
#define WINIOS_SYS_CONTROL_CENTER_TOGGLE  585
#define WINIOS_SYS_TASK_ENUMERATE         586
#define WINIOS_SYS_TASK_SWITCH            587
#define WINIOS_SYS_TASK_TERMINATE         588
#define WINIOS_SYS_ACCESSIBILITY_GET      589
#define WINIOS_SYS_ACCESSIBILITY_SET      590
#define WINIOS_SYS_DIAG_EVENT_QUERY       591

// ABI v2.3 Diagnostic Event Record (48 bytes)
typedef struct {
    uint64_t sequence;
    uint64_t timestamp_us;
    uint32_t pid;
    uint32_t level;
    uint32_t category;
    int32_t  error_code;
    char     tag[16];
} winios_diag_event_record_t;

// ABI v2.3 Diagnostic Query Request (32 bytes)
typedef struct {
    uint64_t after_sequence;
    uint32_t category_filter;
    uint32_t min_level;
    uint32_t max_entries;
    uint32_t returned_entries;
    uint64_t buffer_va;
} winios_diag_query_req_t;

#pragma pack(pop)

// Alignment helper
#define WINIOS_ALIGNED(n) __attribute__((aligned(n)))

#ifdef __cplusplus
}
#endif

#endif // WINIOS_GUEST_SDK_TYPES_H
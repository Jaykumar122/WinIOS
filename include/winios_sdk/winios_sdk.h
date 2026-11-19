#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// WinIOS Category C ABI Constants
// -----------------------------------------------------------------------------
#define WINIOS_CATEGORY_C_ABI_VERSION_1_0 0x00010000
#define WINIOS_CATEGORY_C_ABI_VERSION_1_1 0x00010100
#define WINIOS_CATEGORY_C_ABI_VERSION_1_2 0x00010200
#define WINIOS_CATEGORY_C_ABI_VERSION_1_5 0x00010500
#define WINIOS_CATEGORY_C_ABI_VERSION_1_6 0x00010600
#define WINIOS_CATEGORY_C_ABI_VERSION_1_7 0x00010700
#define WINIOS_CATEGORY_C_ABI_VERSION_1_8 0x00010800
#define WINIOS_CATEGORY_C_ABI_VERSION_1_9 0x00010900
#define WINIOS_CATEGORY_C_ABI_VERSION     0x00010900
#define WINIOS_SDK_VERSION_1_2            0x00010200
#define WINIOS_SDK_VERSION_1_5            0x00010500
#define WINIOS_SDK_VERSION_1_6            0x00010600
#define WINIOS_SDK_VERSION_1_7            0x00010700
#define WINIOS_SDK_VERSION_1_8            0x00010800
#define WINIOS_SDK_VERSION_1_9            0x00010900
#define WINIOS_SDK_VERSION                0x00010900

// Syscalls 500..512 (ABI v1.0 & v1.1)
#define SYS_WINIOS_WINDOW_CREATE          500
#define SYS_WINIOS_WINDOW_DESTROY         501
#define SYS_WINIOS_SURFACE_UPDATE         502
#define SYS_WINIOS_SURFACE_PRESENT        503
#define SYS_WINIOS_POLL_EVENT             504
#define SYS_WINIOS_SURFACE_FILL           505
#define SYS_WINIOS_SET_WINDOW_TITLE       506
#define SYS_WINIOS_GET_ABI_VERSION        507
#define SYS_WINIOS_WINDOW_GET_GEOMETRY    508
#define SYS_WINIOS_WINDOW_SET_GEOMETRY    509
#define SYS_WINIOS_WINDOW_SET_FOCUS       510
#define SYS_WINIOS_WINDOW_LIST            511
#define SYS_WINIOS_CREATE_TIMER           512

// Syscalls 513..521 (ABI v1.2 Extensions)
#define SYS_WINIOS_STORAGE_OPEN           513
#define SYS_WINIOS_STORAGE_READ           514
#define SYS_WINIOS_STORAGE_WRITE          515
#define SYS_WINIOS_STORAGE_CLOSE          516
#define SYS_WINIOS_IPC_PORT_CREATE        517
#define SYS_WINIOS_IPC_SEND               518
#define SYS_WINIOS_IPC_RECV               519
#define SYS_WINIOS_PREFS_GET              520
#define SYS_WINIOS_PREFS_SET              521

// Syscalls 530..536 (ABI v1.6 Extensions)
#define SYS_WINIOS_WINDOW_GET_PROPERTIES  530
#define SYS_WINIOS_WINDOW_SET_GEOMETRY_V2 531
#define SYS_WINIOS_WINDOW_RAISE           532
#define SYS_WINIOS_WINDOW_SET_VISIBILITY  533
#define SYS_WINIOS_CLIPBOARD_GET          534
#define SYS_WINIOS_CLIPBOARD_SET          535
#define SYS_WINIOS_APP_SUPERVISOR_NOTIFY  536

// Syscalls 537..545 (ABI v1.8 Platform Extensions)
#define SYS_WINIOS_CONFIG_GET             537
#define SYS_WINIOS_CONFIG_SET             538
#define SYS_WINIOS_LOG_LEVELED            539
#define SYS_WINIOS_DIAG_SUMMARY           540
#define SYS_WINIOS_IPC_PORT_CREATE_V2     541
#define SYS_WINIOS_IPC_SEND_V2            542
#define SYS_WINIOS_IPC_RECV_V2            543
#define SYS_WINIOS_IPC_LOOKUP_PORT        544
#define SYS_WINIOS_IPC_REGISTER_PORT      545

// Syscalls 546..560 (ABI v1.9 Virtual Devices & System Services)
#define SYS_WINIOS_DEVICE_ENUM            546
#define SYS_WINIOS_DEVICE_GET_INFO        547
#define SYS_WINIOS_DEVICE_OPEN            548
#define SYS_WINIOS_DEVICE_CLOSE           549
#define SYS_WINIOS_DISPLAY_GET_CONFIG     550
#define SYS_WINIOS_DISPLAY_SET_CONFIG     551
#define SYS_WINIOS_AUDIO_SUBMIT           552
#define SYS_WINIOS_AUDIO_GET_STATUS       553
#define SYS_WINIOS_SENSOR_SAMPLE          554
#define SYS_WINIOS_SENSOR_SET_ENABLED     555
#define SYS_WINIOS_NET_SEND               556
#define SYS_WINIOS_NET_RECV               557
#define SYS_WINIOS_TIME_GET               558
#define SYS_WINIOS_NOTIFICATION_POST      559
#define SYS_WINIOS_DEVICE_DIAG_SUMMARY    560

// Syscalls 561..570 (ABI v2.0 Interactive Desktop Compositor & Window Events)
#define SYS_WINIOS_WINDOW_CREATE_V2       561
#define SYS_WINIOS_WINDOW_DESTROY_V2      562
#define SYS_WINIOS_WINDOW_SET_STATE       563
#define SYS_WINIOS_WINDOW_GET_STATE       564
#define SYS_WINIOS_WINDOW_INVALIDATE_RECT 565
#define SYS_WINIOS_EVENT_POLL_V2          566
#define SYS_WINIOS_EVENT_WAIT             567
#define SYS_WINIOS_TEXT_INPUT_SUBMIT      568
#define SYS_WINIOS_CLIPBOARD_GET_V2       569
#define SYS_WINIOS_CLIPBOARD_SET_V2       570

// Category C ABI v2.1 Document Service Extensions (Syscalls 571..580)
#define SYS_WINIOS_DOC_OPEN_PICKER        571
#define SYS_WINIOS_DOC_SAVE_PICKER        572
#define SYS_WINIOS_DOC_GET_METADATA       573
#define SYS_WINIOS_DOC_STREAM_OPEN        574
#define SYS_WINIOS_DOC_STREAM_READ        575
#define SYS_WINIOS_DOC_STREAM_WRITE       576
#define SYS_WINIOS_DOC_STREAM_SEEK        577
#define SYS_WINIOS_DOC_STREAM_CLOSE       578
#define SYS_WINIOS_DOC_GRANT_ACCESS       579
#define SYS_WINIOS_DOC_GET_RECENT         580

// Category C ABI v2.2 Settings, Multitasking & Accessibility Extensions (Syscalls 581..590)
#define SYS_WINIOS_SETTINGS_GET           581
#define SYS_WINIOS_SETTINGS_SET           582
#define SYS_WINIOS_THEME_GET              583
#define SYS_WINIOS_THEME_SET              584
#define SYS_WINIOS_CONTROL_CENTER_TOGGLE  585
#define SYS_WINIOS_TASK_ENUMERATE         586
#define SYS_WINIOS_TASK_SWITCH            587
#define SYS_WINIOS_TASK_TERMINATE         588
#define SYS_WINIOS_ACCESSIBILITY_GET      589
#define SYS_WINIOS_ACCESSIBILITY_SET      590
#define SYS_WINIOS_DIAG_EVENT_QUERY       591
#define SYS_WINIOS_APP_INFO               592
#define SYS_WINIOS_APP_LIST               593

#define WINIOS_CATEGORY_C_ABI_VERSION_2_4 0x00020400
#define WINIOS_CATEGORY_C_ABI_VERSION_2_3 0x00020300
#define WINIOS_CATEGORY_C_ABI_VERSION_2_2 0x00020200
#define WINIOS_CATEGORY_C_ABI_VERSION_2_1 0x00020100
#define WINIOS_CATEGORY_C_ABI_VERSION_2_0 0x00020000
#define WINIOS_SDK_VERSION_2_4            0x00020400
#define WINIOS_SDK_VERSION_2_3            0x00020300

// -----------------------------------------------------------------------------
// Core UI & Window Types
// -----------------------------------------------------------------------------
#ifndef WINIOS_WINDOW_TYPES_DEFINED
#define WINIOS_WINDOW_TYPES_DEFINED

typedef uint32_t winios_wid_t;
typedef uint32_t winios_color_t;

#pragma pack(push, 8)

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} winios_rect_t;

typedef struct {
    uint32_t version;         // ABI version (0x00020000)
    uint32_t size;            // sizeof(winios_desktop_event_t) = 128
    uint32_t event_type;      // DesktopEventType
    uint32_t flags;           // Flags (bit 0: synthetic, bit 1: repeat)
    uint64_t timestamp_us;    // Monotonic timestamp
    uint64_t window_handle;   // 64-bit generation-tagged handle
    int32_t  x;               // Client-relative X
    int32_t  y;               // Client-relative Y
    int32_t  screen_x;        // Screen X
    int32_t  screen_y;        // Screen Y
    uint32_t button_or_key;   // Mouse button or keycode
    int32_t  wheel_delta;     // Wheel delta
    uint32_t modifiers;       // Modifier mask (Shift=1, Ctrl=2, Alt=4, Meta=8)
    uint32_t payload_len;     // Payload byte length
    uint8_t  payload[64];     // Inline payload (e.g. UTF-8 text)
} winios_desktop_event_t;

typedef struct {
    uint64_t handle;
    uint32_t owner_pid;
    uint32_t state;           // 0: Normal, 1: Minimized, 2: Hidden, 3: Closed, 4: Maximized
    int32_t  z_order;
    uint32_t is_focused;
    winios_rect_t frame;
    winios_rect_t client_frame;
    uint32_t reserved[4];
} winios_window_state_data_t;

typedef enum {
    WINIOS_EVENT_NONE                 = 0,
    WINIOS_EVENT_TOUCH_BEGIN          = 1,
    WINIOS_EVENT_TOUCH_MOVE           = 2,
    WINIOS_EVENT_TOUCH_END            = 3,
    WINIOS_EVENT_TOUCH_CANCEL         = 4,
    WINIOS_EVENT_KEY_DOWN             = 5,
    WINIOS_EVENT_KEY_UP               = 6,
    WINIOS_EVENT_KEY_CHAR             = 7,
    WINIOS_EVENT_TIMER                = 8,
    WINIOS_EVENT_WINDOW_FOCUS         = 9,
    WINIOS_EVENT_WINDOW_DEFOCUS       = 10,
    WINIOS_EVENT_WINDOW_CLOSE_REQUEST = 11,
    WINIOS_EVENT_IPC_INCOMING         = 12
} winios_event_type_t;

typedef struct {
    uint32_t type;         // winios_event_type_t
    uint32_t contact_id;   // 0..9 or timer_id / port_id
    int32_t  x;            // Coordinate relative to window frame
    int32_t  y;            // Coordinate relative to window frame
    uint32_t key_code;     // Virtual key code or ASCII
    uint32_t window_id;    // Target window ID
    uint64_t timestamp_us; // Event timestamp in microseconds
} winios_event_t;

typedef enum {
    WINIOS_WINDOW_STATE_HIDDEN    = 0,
    WINIOS_WINDOW_STATE_VISIBLE   = 1,
    WINIOS_WINDOW_STATE_MINIMIZED = 2,
    WINIOS_WINDOW_STATE_MAXIMIZED = 3,
    WINIOS_WINDOW_STATE_CLOSED    = 4
} winios_window_state_t;

typedef struct {
    winios_rect_t frame;
    int32_t       z_order;
    uint32_t      state;          // winios_window_state_t
    uint32_t      is_focused;     // 1 if focused, 0 otherwise
    uint32_t      always_on_top;  // 1 if always on top, 0 otherwise
} winios_window_geometry_t;

#endif // WINIOS_WINDOW_TYPES_DEFINED

typedef struct {
    uint32_t count;
    uint32_t max_count;
    uint32_t window_ids[32];
} winios_window_list_t;

// -----------------------------------------------------------------------------
// ABI Storage, IPC, Preference and Platform Types
// -----------------------------------------------------------------------------
typedef struct {
    uint64_t path_ptr;     // Pointer to UTF-8 path string
    uint32_t path_len;     // Length of path
    uint32_t flags;        // File open mode flags
} winios_storage_req_t;

typedef struct {
    uint32_t sender_pid;   // Origin process ID
    uint32_t target_port;  // Target port identifier
    uint64_t data_ptr;     // Pointer to payload
    uint32_t data_size;    // Size in bytes
    uint32_t message_id;   // Application-defined message ID
} winios_ipc_msg_desc_t;

typedef struct {
    uint64_t key_ptr;      // Pointer to UTF-8 key string
    uint64_t val_ptr;      // Pointer to buffer
    uint32_t key_len;      // Key string length
    uint32_t val_size;     // Buffer size in bytes
    uint32_t val_type;     // 1=String, 2=Int, 3=Bool
    uint32_t reserved;     // Padding
} winios_pref_entry_t;

typedef struct {
    uint32_t abi_version;
    uint32_t process_id;
    uint64_t heap_used_bytes;
    uint64_t total_instructions_retired;
} winios_runtime_info_t;

typedef struct {
    uint32_t msg_size;     // Size of header + payload in bytes
    uint32_t msg_id;       // User message ID or function code
    uint32_t src_port;     // Sending port capability
    uint32_t dst_port;     // Destination port capability
    uint32_t flags;        // Message flags
    uint32_t reserved;     // Alignment padding
} winios_msg_header_t;

// ABI v1.8 Typed Configuration Request
typedef struct {
    uint64_t key_ptr;      // Pointer to UTF-8 key string
    uint32_t key_len;      // Key length
    uint32_t val_type;     // 0=String, 1=Int64, 2=Double, 3=Bool
    uint64_t val_buf_ptr;  // Pointer to buffer
    uint32_t val_buf_size; // Buffer size
    uint32_t out_len;      // Output data length
} winios_config_req_t;

// ABI v1.8 64-bit Generation-Tagged IPC Message
typedef struct {
    uint64_t target_port;  // 64-bit generation handle
    uint64_t data_ptr;     // Pointer to message payload
    uint32_t data_size;    // Payload length
    uint32_t message_id;   // Application message code
    uint32_t sender_pid;   // Authenticated sender PID
    uint32_t reserved;     // Padding
} winios_ipc_msg_v2_t;

// ABI v1.8 Platform Diagnostics Summary
typedef struct {
    uint32_t active_processes;
    uint32_t installed_apps;
    uint32_t open_windows;
    uint32_t ipc_ports;
    uint32_t total_crashes;
    uint32_t total_security_checks;
    uint32_t total_security_denials;
    uint32_t reserved;
} winios_diag_summary_t;

// ABI v2.1 Document Metadata (264 bytes)
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

// ABI v2.1 Document Stream Description (40 bytes)
typedef struct {
    uint64_t stream_handle;
    uint64_t doc_handle;
    uint32_t mode; // 1=Read, 2=Write, 3=ReadWrite
    uint32_t reserved;
    uint64_t current_cursor;
    uint64_t stream_size;
} winios_doc_stream_info_t;

// ABI v2.1 Document Access Grant Request (32 bytes)
typedef struct {
    uint64_t doc_handle;
    uint32_t grantee_pid;
    uint32_t mode; // 1=Read, 2=Write, 3=ReadWrite
    uint64_t expiry_us;
    uint64_t out_grant_token;
} winios_doc_grant_req_t;

// ABI v2.1 Recent Document Descriptor (208 bytes)
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

// ABI v2.4 Application Info Record (128 bytes)
typedef struct {
    char     bundle_id[64];
    char     display_name[32];
    char     version[16];
    uint32_t min_sdk_version;
    uint32_t state;
    uint64_t storage_quota_bytes;
} winios_app_info_t;

// ABI v2.4 Application List Request (24 bytes)
typedef struct {
    uint32_t max_entries;
    uint32_t returned_entries;
    uint32_t active_only;
    uint32_t reserved;
    uint64_t buffer_va;
} winios_app_list_req_t;

#pragma pack(pop)

#ifdef __cplusplus
static_assert(sizeof(winios_rect_t) == 16, "winios_rect_t must be 16 bytes");
static_assert(sizeof(winios_event_t) == 32, "winios_event_t must be 32 bytes");
static_assert(sizeof(winios_window_geometry_t) == 32, "winios_window_geometry_t must be 32 bytes");
static_assert(sizeof(winios_window_list_t) == 136, "winios_window_list_t must be 136 bytes");
static_assert(sizeof(winios_storage_req_t) == 16, "winios_storage_req_t must be 16 bytes");
static_assert(sizeof(winios_ipc_msg_desc_t) == 24, "winios_ipc_msg_desc_t must be 24 bytes");
static_assert(sizeof(winios_pref_entry_t) == 32, "winios_pref_entry_t must be 32 bytes");
static_assert(sizeof(winios_runtime_info_t) == 24, "winios_runtime_info_t must be 24 bytes");
static_assert(sizeof(winios_msg_header_t) == 24, "winios_msg_header_t must be 24 bytes");
static_assert(sizeof(winios_config_req_t) == 32, "winios_config_req_t must be 32 bytes");
static_assert(sizeof(winios_ipc_msg_v2_t) == 32, "winios_ipc_msg_v2_t must be 32 bytes");
static_assert(sizeof(winios_diag_summary_t) == 32, "winios_diag_summary_t must be 32 bytes");
static_assert(sizeof(winios_doc_metadata_t) == 264, "winios_doc_metadata_t must be 264 bytes");
static_assert(sizeof(winios_doc_stream_info_t) == 40, "winios_doc_stream_info_t must be 40 bytes");
static_assert(sizeof(winios_doc_grant_req_t) == 32, "winios_doc_grant_req_t must be 32 bytes");
static_assert(sizeof(winios_recent_doc_t) == 208, "winios_recent_doc_t must be 208 bytes");
static_assert(sizeof(winios_settings_snapshot_t) == 48, "winios_settings_snapshot_t must be 48 bytes");
static_assert(sizeof(winios_task_entry_t) == 120, "winios_task_entry_t must be 120 bytes");
static_assert(sizeof(winios_accessibility_info_t) == 32, "winios_accessibility_info_t must be 32 bytes");
static_assert(sizeof(winios_diag_event_record_t) == 48, "winios_diag_event_record_t must be 48 bytes");
static_assert(sizeof(winios_diag_query_req_t) == 32, "winios_diag_query_req_t must be 32 bytes");
static_assert(sizeof(winios_app_info_t) == 128, "winios_app_info_t must be 128 bytes");
static_assert(sizeof(winios_app_list_req_t) == 24, "winios_app_list_req_t must be 24 bytes");
#endif

// -----------------------------------------------------------------------------
// Color Helper Macros (ARGB 32-bit format)
// -----------------------------------------------------------------------------
#define WINIOS_COLOR_ARGB(a, r, g, b) \
    (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

#define WINIOS_COLOR_RGB(r, g, b) WINIOS_COLOR_ARGB(0xFF, (r), (g), (b))

#define WINIOS_COLOR_BLACK   WINIOS_COLOR_RGB(0, 0, 0)
#define WINIOS_COLOR_WHITE   WINIOS_COLOR_RGB(255, 255, 255)
#define WINIOS_COLOR_GRAY    WINIOS_COLOR_RGB(128, 128, 128)
#define WINIOS_COLOR_BLUE    WINIOS_COLOR_RGB(0, 122, 255)
#define WINIOS_COLOR_GREEN   WINIOS_COLOR_RGB(52, 199, 89)
#define WINIOS_COLOR_ORANGE  WINIOS_COLOR_RGB(255, 149, 0)
#define WINIOS_COLOR_RED     WINIOS_COLOR_RGB(255, 59, 48)

#ifdef __cplusplus
}
#endif

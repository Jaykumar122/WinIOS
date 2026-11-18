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
#define WINIOS_CATEGORY_C_ABI_VERSION     0x00010100
#define WINIOS_CATEGORY_C_ABI_VERSION_1_2 0x00010200
#define WINIOS_SDK_VERSION                0x00010200

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

// -----------------------------------------------------------------------------
// Core UI & Window Types
// -----------------------------------------------------------------------------
typedef uint32_t winios_wid_t;
typedef uint32_t winios_color_t;

#pragma pack(push, 8)

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} winios_rect_t;

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
    WINIOS_WINDOW_STATE_NORMAL    = 0,
    WINIOS_WINDOW_STATE_MINIMIZED = 1,
    WINIOS_WINDOW_STATE_HIDDEN    = 2,
    WINIOS_WINDOW_STATE_CLOSED    = 3
} winios_window_state_t;

typedef struct {
    winios_rect_t frame;
    int32_t       z_order;
    uint32_t      state;          // winios_window_state_t
    uint32_t      is_focused;     // 1 if focused, 0 otherwise
    uint32_t      always_on_top;  // 1 if always on top, 0 otherwise
} winios_window_geometry_t;

typedef struct {
    uint32_t count;
    uint32_t max_count;
    uint32_t window_ids[32];
} winios_window_list_t;

// -----------------------------------------------------------------------------
// ABI v1.2 Storage, IPC, and Preference Types
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

#pragma once

#include <cstdint>
#include <cstddef>

#ifndef WINIOS_WINDOW_TYPES_DEFINED
#define WINIOS_WINDOW_TYPES_DEFINED

typedef uint32_t winios_wid_t;
typedef uint32_t winios_color_t;

typedef enum {
    WINIOS_WINDOW_STATE_HIDDEN    = 0,
    WINIOS_WINDOW_STATE_VISIBLE   = 1,
    WINIOS_WINDOW_STATE_MINIMIZED = 2,
    WINIOS_WINDOW_STATE_MAXIMIZED = 3,
    WINIOS_WINDOW_STATE_CLOSED    = 4
} winios_window_state_t;

#pragma pack(push, 8)
typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} winios_rect_t;

typedef struct {
    winios_rect_t frame;
    int32_t       z_order;
    uint32_t      state;          // winios_window_state_t
    uint32_t      is_focused;     // 1 if active, 0 otherwise
    uint32_t      always_on_top;  // 1 if pinned, 0 otherwise
} winios_window_geometry_t;
#pragma pack(pop)

#endif // WINIOS_WINDOW_TYPES_DEFINED

#ifndef WINIOS_EVENT_V2_TYPES_DEFINED
#define WINIOS_EVENT_V2_TYPES_DEFINED

// Input Provenance tracking record (24 bytes)
#pragma pack(push, 8)
typedef struct {
    uint32_t device_type;      // 1 = mouse, 2 = keyboard, 3 = touch
    uint32_t device_id;        // Host device identifier
    uint64_t host_timestamp;   // Microseconds from host high-resolution timer
    uint32_t sequence_number;  // Monotonic sequence
    uint32_t flags;            // Provenance flags (1 = physical, 2 = simulated)
} winios_input_provenance_t;

// Enhanced Event V2
typedef enum {
    WINIOS_EVENT_V2_NONE                 = 0,
    WINIOS_EVENT_V2_TOUCH_BEGIN          = 1,
    WINIOS_EVENT_V2_TOUCH_MOVE           = 2,
    WINIOS_EVENT_V2_TOUCH_END            = 3,
    WINIOS_EVENT_V2_TOUCH_CANCEL         = 4,
    WINIOS_EVENT_V2_KEY_DOWN             = 5,
    WINIOS_EVENT_V2_KEY_UP               = 6,
    WINIOS_EVENT_V2_KEY_CHAR             = 7,
    WINIOS_EVENT_V2_TIMER                = 8,
    WINIOS_EVENT_V2_WINDOW_FOCUS         = 9,
    WINIOS_EVENT_V2_WINDOW_DEFOCUS       = 10,
    WINIOS_EVENT_V2_WINDOW_CLOSE_REQUEST = 11,
    WINIOS_EVENT_V2_IPC_INCOMING         = 12,
    WINIOS_EVENT_V2_MOUSE_MOVE           = 13,
    WINIOS_EVENT_V2_MOUSE_DOWN           = 14,
    WINIOS_EVENT_V2_MOUSE_UP             = 15,
    WINIOS_EVENT_V2_MOUSE_WHEEL          = 16,
    WINIOS_EVENT_V2_WINDOW_RESIZE        = 17
} winios_event_v2_type_t;

// Key Modifiers bitmask
#define WINIOS_KEYMOD_NONE     0x00000000
#define WINIOS_KEYMOD_SHIFT    0x00000001
#define WINIOS_KEYMOD_CTRL     0x00000002
#define WINIOS_KEYMOD_ALT      0x00000004
#define WINIOS_KEYMOD_META     0x00000008
#define WINIOS_KEYMOD_CAPSLOCK 0x00000010

// Event V2 structure (64 bytes, 8-byte aligned)
typedef struct {
    uint32_t type;             // winios_event_v2_type_t
    uint32_t contact_id;       // touch ID or button (1=Left, 2=Right, 3=Middle)
    int32_t  x;                // Window-relative X coordinate
    int32_t  y;                // Window-relative Y coordinate
    uint32_t key_code;         // Virtual key or character code
    uint32_t modifiers;        // Key modifier flags
    int32_t  wheel_delta;      // Mouse wheel delta (e.g. +/- 120)
    uint32_t window_id;        // Destination window ID
    uint64_t timestamp_us;     // Timestamp in microseconds
    winios_input_provenance_t provenance; // Provenance audit metadata
} winios_event_v2_t;
#pragma pack(pop)

#endif // WINIOS_EVENT_V2_TYPES_DEFINED

#ifdef __cplusplus
namespace winios::runtime {
extern "C" {
#endif

#pragma pack(push, 8)

// Surface descriptor (32 bytes)
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
    uint32_t pixel_format;        // 0 = ARGB8888
    uint64_t buffer_guest_va;
    uint64_t buffer_size_bytes;
} winios_surface_desc_t;

// Dirty Rectangle for damage tracking (16 bytes)
typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} winios_dirty_rect_t;

// Presentation Statistics (48 bytes)
typedef struct {
    uint64_t total_frames_presented;
    uint64_t total_frames_dropped;
    uint64_t total_dirty_rects_processed;
    uint64_t total_occluded_rects_culled;
    uint64_t last_presentation_timestamp_us;
    uint32_t average_frame_time_us;
    uint32_t refresh_rate_hz;
} winios_presentation_stats_t;

// Clipboard request descriptor (16 bytes)
typedef struct {
    uint32_t format;           // 1 = UTF-8 Plaintext
    uint32_t buffer_len;       // In/Out byte size
    uint64_t buffer_va;        // Guest virtual address
} winios_clipboard_req_t;

// Application lifecycle state
typedef enum {
    WINIOS_APP_STATE_UNINITIALIZED = 0,
    WINIOS_APP_STATE_STARTING      = 1,
    WINIOS_APP_STATE_RUNNING       = 2,
    WINIOS_APP_STATE_SUSPENDED     = 3,
    WINIOS_APP_STATE_TERMINATED    = 4,
    WINIOS_APP_STATE_FAULTED       = 5
} winios_app_state_t;

#pragma pack(pop)

#ifdef __cplusplus
} // extern "C"

using ::winios_event_v2_t;
using ::winios_input_provenance_t;
using ::winios_event_v2_type_t;

} // namespace winios::runtime
#endif

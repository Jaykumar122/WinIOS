#ifndef WINIOS_GUEST_SDK_EVENT_H
#define WINIOS_GUEST_SDK_EVENT_H

#include "winios/winios_types.h"
#include "winios/system/syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#pragma pack(push, 8)
typedef struct {
    uint32_t type;         // winios_event_type_t
    uint32_t contact_id;   // touch ID or timer_id
    int32_t  x;            // Coordinate relative to window frame
    int32_t  y;            // Coordinate relative to window frame
    uint32_t key_code;     // Virtual key code or ASCII
    uint32_t window_id;    // Target window ID
    uint64_t timestamp_us; // Event timestamp in microseconds
} winios_event_t;
#pragma pack(pop)

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

// Event loop APIs
int32_t  winios_poll_event(winios_event_t* out_event);
int32_t  winios_poll_event_v2(winios_event_v2_t* out_event);
int32_t  winios_wait_event(winios_event_t* out_event, uint32_t timeout_ms);
int32_t  winios_wait_event_v2(winios_event_v2_t* out_event, uint32_t timeout_ms);
uint32_t winios_create_timer(uint32_t interval_ms);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_GUEST_SDK_EVENT_H

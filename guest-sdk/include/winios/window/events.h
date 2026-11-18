#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Desktop Event Types (ABI v2.0)
// -----------------------------------------------------------------------------
typedef enum {
    WINIOS_DESKTOP_EVENT_NONE                = 0,
    WINIOS_DESKTOP_EVENT_POINTER_MOVE        = 1,
    WINIOS_DESKTOP_EVENT_POINTER_DOWN        = 2,
    WINIOS_DESKTOP_EVENT_POINTER_UP          = 3,
    WINIOS_DESKTOP_EVENT_POINTER_WHEEL       = 4,
    WINIOS_DESKTOP_EVENT_KEY_DOWN            = 5,
    WINIOS_DESKTOP_EVENT_KEY_UP              = 6,
    WINIOS_DESKTOP_EVENT_TEXT_INPUT          = 7,
    WINIOS_DESKTOP_EVENT_FOCUS_GAINED        = 8,
    WINIOS_DESKTOP_EVENT_FOCUS_LOST          = 9,
    WINIOS_DESKTOP_EVENT_WINDOW_MOVE         = 10,
    WINIOS_DESKTOP_EVENT_WINDOW_RESIZE       = 11,
    WINIOS_DESKTOP_EVENT_WINDOW_CLOSE_REQ    = 12,
    WINIOS_DESKTOP_EVENT_WINDOW_STATE_CHANGE = 13,
    WINIOS_DESKTOP_EVENT_DISPLAY_RECONFIG    = 14
} winios_desktop_event_type_t;

#pragma pack(push, 8)

typedef struct {
    uint32_t version;         // ABI version: WINIOS_CATEGORY_C_ABI_VERSION_2_0 (0x00020000)
    uint32_t size;            // sizeof(winios_desktop_event_t) = 128
    uint32_t event_type;      // winios_desktop_event_type_t
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

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

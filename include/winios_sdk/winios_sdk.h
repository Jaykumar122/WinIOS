#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// WinIOS Category C ABI v1.0 Constants
// -----------------------------------------------------------------------------
#define WINIOS_CATEGORY_C_ABI_VERSION 0x00010000

#define SYS_WINIOS_WINDOW_CREATE     500
#define SYS_WINIOS_WINDOW_DESTROY    501
#define SYS_WINIOS_SURFACE_UPDATE    502
#define SYS_WINIOS_SURFACE_PRESENT   503
#define SYS_WINIOS_POLL_EVENT        504
#define SYS_WINIOS_SURFACE_FILL      505
#define SYS_WINIOS_SET_WINDOW_TITLE  506
#define SYS_WINIOS_GET_ABI_VERSION   507

// -----------------------------------------------------------------------------
// Core UI Types
// -----------------------------------------------------------------------------
typedef uint32_t winios_wid_t;
typedef uint32_t winios_color_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} winios_rect_t;

typedef enum {
    WINIOS_EVENT_NONE        = 0,
    WINIOS_EVENT_TOUCH_BEGIN = 1,
    WINIOS_EVENT_TOUCH_MOVE  = 2,
    WINIOS_EVENT_TOUCH_END   = 3,
    WINIOS_EVENT_TOUCH_CANCEL= 4,
    WINIOS_EVENT_KEY_DOWN    = 5,
    WINIOS_EVENT_KEY_UP      = 6,
    WINIOS_EVENT_KEY_CHAR    = 7
} winios_event_type_t;

typedef struct {
    uint32_t type;         // winios_event_type_t
    uint32_t contact_id;   // 0..9
    int32_t  x;            // Coordinate relative to window frame
    int32_t  y;            // Coordinate relative to window frame
    uint32_t key_code;     // Virtual key code or ASCII
    uint32_t window_id;    // Target window ID
    uint64_t timestamp_us; // Event timestamp in microseconds
} winios_event_t;

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

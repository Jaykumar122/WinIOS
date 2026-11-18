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

// Event loop APIs
int32_t winios_poll_event(winios_event_t* out_event);
int32_t winios_wait_event(winios_event_t* out_event, uint32_t timeout_ms);
uint32_t winios_create_timer(uint32_t interval_ms);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_GUEST_SDK_EVENT_H
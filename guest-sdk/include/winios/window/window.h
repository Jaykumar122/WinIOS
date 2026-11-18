#pragma once

#include <stdint.h>
#include <stddef.h>
#include "winios/winios_types.h"
#include "winios/ui/window.h"
#include "winios/window/events.h"

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Window Lifecycle States
// -----------------------------------------------------------------------------
typedef enum {
    WINIOS_WINDOW_STATE_V2_NORMAL    = 0,
    WINIOS_WINDOW_STATE_V2_MINIMIZED = 1,
    WINIOS_WINDOW_STATE_V2_HIDDEN    = 2,
    WINIOS_WINDOW_STATE_V2_CLOSED    = 3,
    WINIOS_WINDOW_STATE_V2_MAXIMIZED = 4
} winios_window_state_v2_t;

#define WINIOS_HANDLE_ID(h)  ((uint32_t)((h) & 0xFFFFFFFFULL))
#define WINIOS_HANDLE_GEN(h) ((uint32_t)((h) >> 32))

#pragma pack(push, 8)

typedef struct {
    uint64_t handle;
    uint32_t owner_pid;
    uint32_t state;
    int32_t  z_order;
    uint32_t is_focused;
    winios_rect_t frame;
    winios_rect_t client_frame;
    uint32_t reserved[4];
} winios_window_state_data_t;

#pragma pack(pop)

// -----------------------------------------------------------------------------
// Guest Window Management & Event APIs (ABI v2.0)
// -----------------------------------------------------------------------------
uint64_t winios_window_create_v2(const char* title, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t flags);
int32_t  winios_window_destroy_v2(uint64_t handle);
int32_t  winios_window_set_state_v2(uint64_t handle, uint32_t state);
int32_t  winios_window_get_state_v2(uint64_t handle, winios_window_state_data_t* out_state);
int32_t  winios_window_invalidate_rect(uint64_t handle, const winios_rect_t* rect);

int32_t  winios_event_poll_v2(uint64_t handle, winios_desktop_event_t* out_event);
int32_t  winios_event_wait(uint64_t handle, winios_desktop_event_t* out_event, uint64_t timeout_us);
int32_t  winios_text_input_submit(uint64_t handle, const char* utf8_text, size_t len);

int64_t  winios_clipboard_get_v2(char* out_buf, size_t max_len, size_t* actual_len);
int32_t  winios_clipboard_set_v2(const char* text, size_t len);

#ifdef __cplusplus
}
#endif

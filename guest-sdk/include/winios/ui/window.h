#ifndef WINIOS_GUEST_SDK_WINDOW_H
#define WINIOS_GUEST_SDK_WINDOW_H

#include "winios/winios_types.h"
#include "winios/system/syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

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
    winios_rect_t frame;
    int32_t       z_order;
    uint32_t      state;
    uint32_t      is_focused;
    uint32_t      always_on_top;
} winios_window_geometry_t;

typedef enum {
    WINIOS_WINDOW_STATE_HIDDEN    = 0,
    WINIOS_WINDOW_STATE_VISIBLE   = 1,
    WINIOS_WINDOW_STATE_MINIMIZED = 2,
    WINIOS_WINDOW_STATE_MAXIMIZED = 3,
    WINIOS_WINDOW_STATE_CLOSED    = 4
} winios_window_state_t;
#pragma pack(pop)

#endif // WINIOS_WINDOW_TYPES_DEFINED

// Window operations
winios_wid_t winios_window_create(const char* title, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
int32_t      winios_window_destroy(winios_wid_t wid);
int32_t      winios_window_set_title(winios_wid_t wid, const char* title);
int32_t      winios_window_get_geometry(winios_wid_t wid, winios_window_geometry_t* geom);
int32_t      winios_window_set_geometry(winios_wid_t wid, const winios_window_geometry_t* geom);
int32_t      winios_window_set_focus(winios_wid_t wid);
int32_t      winios_window_request_focus(winios_wid_t wid);
int32_t      winios_window_set_visibility(winios_wid_t wid, uint32_t visible);
int32_t      winios_window_get_state(winios_wid_t wid, uint32_t* out_state);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_GUEST_SDK_WINDOW_H
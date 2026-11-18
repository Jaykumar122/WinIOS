#ifndef WINIOS_GUEST_SDK_WINDOW_H
#define WINIOS_GUEST_SDK_WINDOW_H

#include "winios/winios_types.h"
#include "winios/system/syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

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
#pragma pack(pop)

// Window operations
winios_wid_t winios_window_create(const char* title, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
int32_t      winios_window_destroy(winios_wid_t wid);
int32_t      winios_window_set_title(winios_wid_t wid, const char* title);
int32_t      winios_window_get_geometry(winios_wid_t wid, winios_window_geometry_t* geom);
int32_t      winios_window_set_focus(winios_wid_t wid);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_GUEST_SDK_WINDOW_H
#ifndef WINIOS_GUEST_SDK_GRAPHICS_H
#define WINIOS_GUEST_SDK_GRAPHICS_H

#include "winios/winios_types.h"
#include "winios/system/syscall.h"
#include "winios/ui/window.h"

#ifdef __cplusplus
extern "C" {
#endif

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

// Surface and rendering operations
int32_t winios_surface_fill(winios_wid_t wid, uint32_t x, uint32_t y, uint32_t width, uint32_t height, winios_color_t color);
int32_t winios_surface_update(winios_wid_t wid, const void* pixels, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
int32_t winios_surface_present(winios_wid_t wid);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_GUEST_SDK_GRAPHICS_H
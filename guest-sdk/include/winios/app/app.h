#ifndef WINIOS_APP_FRAMEWORK_H
#define WINIOS_APP_FRAMEWORK_H

#include <stdint.h>
#include <stddef.h>
#include "winios/winios_types.h"
#include "winios/ui/window.h"
#include "winios/ui/event.h"
#include "winios/ui/graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// WinIOS Guest Application Framework v2
// -----------------------------------------------------------------------------

typedef struct winios_app winios_app_t;

typedef void (*winios_app_init_fn)(winios_app_t* app, void* user_data);
typedef void (*winios_app_update_fn)(winios_app_t* app, uint64_t delta_us, void* user_data);
typedef void (*winios_app_render_fn)(winios_app_t* app, uint32_t* fb, uint32_t stride, void* user_data);
typedef int  (*winios_app_event_fn)(winios_app_t* app, const winios_event_v2_t* event, void* user_data);
typedef void (*winios_app_shutdown_fn)(winios_app_t* app, void* user_data);

typedef struct {
    const char*            app_id;
    const char*            title;
    uint32_t               window_x;
    uint32_t               window_y;
    uint32_t               window_width;
    uint32_t               window_height;
    winios_app_init_fn     on_init;
    winios_app_update_fn   on_update;
    winios_app_render_fn   on_render;
    winios_app_event_fn    on_event;
    winios_app_shutdown_fn on_shutdown;
    void*                  user_data;
} winios_app_config_t;

struct winios_app {
    winios_app_config_t config;
    winios_wid_t        main_window_id;
    uint32_t            width;
    uint32_t            height;
    uint8_t             is_running;
    uint8_t             is_foreground;
    uint8_t             needs_redraw;
    uint8_t             is_dispatching; // Reentrancy guard
    int32_t             exit_code;
    uint64_t            last_tick_us;
    void*               user_context;
};

// Application Lifecycle API
int  winios_app_init(winios_app_t* app, const winios_app_config_t* config);
int  winios_app_run(winios_app_t* app);
void winios_app_request_exit(winios_app_t* app, int32_t exit_code);
void winios_app_invalidate(winios_app_t* app);
winios_wid_t winios_app_get_window_id(const winios_app_t* app);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_APP_FRAMEWORK_H

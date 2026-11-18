#ifndef WINIOS_UI_TOOLKIT_H
#define WINIOS_UI_TOOLKIT_H

#include <stdint.h>
#include <stddef.h>
#include "winios/ui/window.h"
#include "winios/ui/event.h"
#include "winios/ui/graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// UI Button Component
// -----------------------------------------------------------------------------
typedef struct {
    winios_rect_t bounds;
    const char*   text;
    uint32_t      bg_color;
    uint32_t      hover_bg_color;
    uint32_t      pressed_bg_color;
    uint32_t      text_color;
    uint32_t      border_color;
    uint8_t       is_pressed;
    uint8_t       is_hovered;
} winios_button_t;

void winios_button_init(winios_button_t* btn, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* text);
// Returns 1 if button was clicked (mouse/touch up inside bounds), 0 otherwise
int  winios_button_handle_event(winios_button_t* btn, const winios_event_v2_t* evt);
void winios_button_render(uint32_t* fb, uint32_t stride, const winios_button_t* btn);

// -----------------------------------------------------------------------------
// UI Label Component
// -----------------------------------------------------------------------------
typedef struct {
    winios_rect_t bounds;
    const char*   text;
    uint32_t      text_color;
    uint32_t      bg_color;
} winios_label_t;

void winios_label_init(winios_label_t* lbl, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* text);
void winios_label_render(uint32_t* fb, uint32_t stride, const winios_label_t* lbl);

// -----------------------------------------------------------------------------
// UI TextInput Component (Single-line interactive editor with caret)
// -----------------------------------------------------------------------------
typedef struct {
    winios_rect_t bounds;
    char*         buffer;
    size_t        capacity;
    size_t        length;
    size_t        cursor_pos;
    uint8_t       is_focused;
    uint32_t      bg_color;
    uint32_t      text_color;
    uint32_t      border_color;
    uint32_t      focus_border_color;
} winios_textinput_t;

void winios_textinput_init(winios_textinput_t* ti, uint32_t x, uint32_t y, uint32_t w, uint32_t h, char* buffer, size_t capacity);
// Returns 1 if text changed, 0 otherwise
int  winios_textinput_handle_event(winios_textinput_t* ti, const winios_event_v2_t* evt);
void winios_textinput_render(uint32_t* fb, uint32_t stride, const winios_textinput_t* ti);

// -----------------------------------------------------------------------------
// UI Checkbox Component
// -----------------------------------------------------------------------------
typedef struct {
    winios_rect_t bounds;
    const char*   label;
    uint8_t       checked;
    uint32_t      text_color;
    uint32_t      box_color;
    uint32_t      check_color;
} winios_checkbox_t;

void winios_checkbox_init(winios_checkbox_t* cb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* label, uint8_t initial_state);
// Returns 1 if state toggled, 0 otherwise
int  winios_checkbox_handle_event(winios_checkbox_t* cb, const winios_event_v2_t* evt);
void winios_checkbox_render(uint32_t* fb, uint32_t stride, const winios_checkbox_t* cb);

// -----------------------------------------------------------------------------
// UI ProgressBar Component
// -----------------------------------------------------------------------------
typedef struct {
    winios_rect_t bounds;
    uint32_t      min_val;
    uint32_t      max_val;
    uint32_t      current_val;
    uint32_t      fill_color;
    uint32_t      bg_color;
    uint32_t      border_color;
} winios_progressbar_t;

void winios_progressbar_init(winios_progressbar_t* pb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t min_val, uint32_t max_val, uint32_t cur_val);
void winios_progressbar_set_value(winios_progressbar_t* pb, uint32_t val);
void winios_progressbar_render(uint32_t* fb, uint32_t stride, const winios_progressbar_t* pb);

// -----------------------------------------------------------------------------
// System Clipboard API
// -----------------------------------------------------------------------------
int winios_clipboard_get(char* buffer, size_t max_len);
int winios_clipboard_set(const char* text, size_t len);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_UI_TOOLKIT_H

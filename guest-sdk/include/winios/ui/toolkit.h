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
// UI TextArea Component (Multiline editor with selection, cursor and word wrap)
// -----------------------------------------------------------------------------
typedef struct {
    winios_rect_t bounds;
    char*         buffer;
    size_t        capacity;
    size_t        length;
    size_t        cursor_pos;
    size_t        sel_start;
    size_t        sel_end;
    int32_t       scroll_y;
    uint8_t       is_focused;
    uint32_t      bg_color;
    uint32_t      text_color;
    uint32_t      border_color;
    uint32_t      sel_color;
} winios_textarea_t;

void winios_textarea_init(winios_textarea_t* ta, uint32_t x, uint32_t y, uint32_t w, uint32_t h, char* buffer, size_t capacity);
int  winios_textarea_handle_event(winios_textarea_t* ta, const winios_event_v2_t* evt);
void winios_textarea_render(uint32_t* fb, uint32_t stride, const winios_textarea_t* ta);

// -----------------------------------------------------------------------------
// UI Slider Component (Continuous/stepped horizontal value bar)
// -----------------------------------------------------------------------------
typedef struct {
    winios_rect_t bounds;
    int32_t       min_val;
    int32_t       max_val;
    int32_t       current_val;
    uint8_t       is_dragging;
    uint32_t      track_color;
    uint32_t      fill_color;
    uint32_t      thumb_color;
} winios_slider_t;

void winios_slider_init(winios_slider_t* sl, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int32_t min_v, int32_t max_v, int32_t cur_v);
int  winios_slider_handle_event(winios_slider_t* sl, const winios_event_v2_t* evt);
void winios_slider_render(uint32_t* fb, uint32_t stride, const winios_slider_t* sl);

// -----------------------------------------------------------------------------
// UI Dialog Component (Modal / Confirmation dialog overlay)
// -----------------------------------------------------------------------------
typedef enum {
    WINIOS_DIALOG_NONE   = 0,
    WINIOS_DIALOG_OK     = 1,
    WINIOS_DIALOG_CANCEL = 2
} winios_dialog_result_t;

typedef struct {
    winios_rect_t          bounds;
    const char*            title;
    const char*            message;
    const char*            ok_label;
    const char*            cancel_label;
    uint8_t                is_visible;
    uint8_t                has_cancel;
    winios_dialog_result_t result;
} winios_dialog_t;

void winios_dialog_init(winios_dialog_t* dlg, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* title, const char* msg, int has_cancel);
int  winios_dialog_handle_event(winios_dialog_t* dlg, const winios_event_v2_t* evt);
void winios_dialog_render(uint32_t* fb, uint32_t stride, const winios_dialog_t* dlg);

// -----------------------------------------------------------------------------
// UI Menu Component (Dropdown / Context menu)
// -----------------------------------------------------------------------------
#define WINIOS_MENU_MAX_ITEMS 8

typedef struct {
    const char* label;
    int32_t     action_id;
    uint8_t     is_enabled;
} winios_menu_item_t;

typedef struct {
    winios_rect_t      bounds;
    uint32_t           item_count;
    winios_menu_item_t items[WINIOS_MENU_MAX_ITEMS];
    int32_t            selected_action;
    int32_t            hover_index;
    uint8_t            is_open;
} winios_menu_t;

void winios_menu_init(winios_menu_t* menu, uint32_t x, uint32_t y, uint32_t w);
int  winios_menu_add_item(winios_menu_t* menu, const char* label, int32_t action_id);
int  winios_menu_handle_event(winios_menu_t* menu, const winios_event_v2_t* evt);
void winios_menu_render(uint32_t* fb, uint32_t stride, const winios_menu_t* menu);

// -----------------------------------------------------------------------------
// UI Focus Manager (Keyboard tab & navigation)
// -----------------------------------------------------------------------------
#define WINIOS_FOCUS_MAX_ITEMS 32

typedef struct {
    uint32_t count;
    uint32_t active_index;
    void*    controls[WINIOS_FOCUS_MAX_ITEMS];
    uint8_t  types[WINIOS_FOCUS_MAX_ITEMS]; // 1=Button, 2=TextInput, 3=TextArea, 4=Checkbox, 5=Slider
} winios_focus_manager_t;

void winios_focus_init(winios_focus_manager_t* fm);
int  winios_focus_register(winios_focus_manager_t* fm, void* control, uint8_t type);
void winios_focus_next(winios_focus_manager_t* fm);
void winios_focus_prev(winios_focus_manager_t* fm);
void* winios_focus_get_active(const winios_focus_manager_t* fm);

// -----------------------------------------------------------------------------
// System Clipboard API
// -----------------------------------------------------------------------------
int winios_clipboard_get(char* buffer, size_t max_len);
int winios_clipboard_set(const char* text, size_t len);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_UI_TOOLKIT_H

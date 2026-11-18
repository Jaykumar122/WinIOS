#include <cassert>
#include <cstring>
#include <iostream>

#include "winios/winios.h"
#include "winios/ui/toolkit.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

void test_textarea_multiline_editing() {
    std::cout << "[Test 1] Multiline TextArea typing, caret navigation & backspace...\n";
    char buffer[256] = "Hello";
    winios_textarea_t ta;
    winios_textarea_init(&ta, 10, 10, 300, 200, buffer, sizeof(buffer));

    TEST_ASSERT(ta.length == 5);
    TEST_ASSERT(ta.cursor_pos == 5);

    // Click to focus
    winios_event_v2_t click{};
    click.type = WINIOS_EVENT_V2_MOUSE_DOWN;
    click.x = 20; click.y = 20;
    TEST_ASSERT(winios_textarea_handle_event(&ta, &click));
    TEST_ASSERT(ta.is_focused == 1);

    // Type newline (Enter)
    winios_event_v2_t enter{};
    enter.type = WINIOS_EVENT_V2_KEY_CHAR;
    enter.key_code = 10;
    TEST_ASSERT(winios_textarea_handle_event(&ta, &enter));
    TEST_ASSERT(ta.length == 6);
    TEST_ASSERT(buffer[5] == '\n');

    // Type 'W' 'o' 'r' 'l' 'd'
    const char* str = "World";
    for (int i = 0; str[i]; ++i) {
        winios_event_v2_t ch_ev{};
        ch_ev.type = WINIOS_EVENT_V2_KEY_CHAR;
        ch_ev.key_code = (uint32_t)str[i];
        TEST_ASSERT(winios_textarea_handle_event(&ta, &ch_ev));
    }
    TEST_ASSERT(ta.length == 11);
    TEST_ASSERT(std::strcmp(buffer, "Hello\nWorld") == 0);

    // Backspace: delete 'd'
    winios_event_v2_t bs{};
    bs.type = WINIOS_EVENT_V2_KEY_CHAR;
    bs.key_code = 8;
    TEST_ASSERT(winios_textarea_handle_event(&ta, &bs));
    TEST_ASSERT(ta.length == 10);
    TEST_ASSERT(std::strcmp(buffer, "Hello\nWorl") == 0);

    // Left Arrow
    winios_event_v2_t left{};
    left.type = WINIOS_EVENT_V2_KEY_DOWN;
    left.key_code = 37;
    TEST_ASSERT(winios_textarea_handle_event(&ta, &left));
    TEST_ASSERT(ta.cursor_pos == 9);

    // Home key
    winios_event_v2_t home{};
    home.type = WINIOS_EVENT_V2_KEY_DOWN;
    home.key_code = 36;
    TEST_ASSERT(winios_textarea_handle_event(&ta, &home));
    TEST_ASSERT(ta.cursor_pos == 0);

    std::cout << "[PASS] TextArea multiline editing verified.\n";
}

void test_slider_component() {
    std::cout << "[Test 2] Continuous slider interaction and clamping...\n";
    winios_slider_t sl;
    winios_slider_init(&sl, 50, 50, 200, 24, 0, 100, 25);
    TEST_ASSERT(sl.current_val == 25);

    // Click at middle of slider (rel_x = 100 on 200 width -> 50%)
    winios_event_v2_t down{};
    down.type = WINIOS_EVENT_V2_MOUSE_DOWN;
    down.x = 150; down.y = 60; // 50 + 100
    TEST_ASSERT(winios_slider_handle_event(&sl, &down));
    TEST_ASSERT(sl.is_dragging == 1);
    TEST_ASSERT(sl.current_val == 50);

    // Drag to 75% (x = 200 -> rel_x = 150)
    winios_event_v2_t drag{};
    drag.type = WINIOS_EVENT_V2_MOUSE_MOVE;
    drag.x = 200; drag.y = 60;
    TEST_ASSERT(winios_slider_handle_event(&sl, &drag));
    TEST_ASSERT(sl.current_val == 75);

    // Release
    winios_event_v2_t up{};
    up.type = WINIOS_EVENT_V2_MOUSE_UP;
    up.x = 200; up.y = 60;
    TEST_ASSERT(winios_slider_handle_event(&sl, &up));
    TEST_ASSERT(sl.is_dragging == 0);
    TEST_ASSERT(sl.current_val == 75);

    std::cout << "[PASS] Slider interaction verified.\n";
}

void test_dialog_and_menu() {
    std::cout << "[Test 3] Modal confirmation dialog and dropdown menu...\n";

    // 1. Dialog test
    winios_dialog_t dlg;
    winios_dialog_init(&dlg, 100, 100, 300, 180, "Confirm Save", "Save changes to disk?", 1);
    TEST_ASSERT(dlg.is_visible == 1);
    TEST_ASSERT(dlg.result == WINIOS_DIALOG_NONE);

    // Click OK button (at right side: 100 + 300 - 80 + 35 = 355, 100 + 180 - 38 + 14 = 256)
    winios_event_v2_t ok_click{};
    ok_click.type = WINIOS_EVENT_V2_MOUSE_UP;
    ok_click.x = 350; ok_click.y = 255;
    TEST_ASSERT(winios_dialog_handle_event(&dlg, &ok_click));
    TEST_ASSERT(dlg.result == WINIOS_DIALOG_OK);
    TEST_ASSERT(dlg.is_visible == 0);

    // 2. Menu test
    winios_menu_t menu;
    winios_menu_init(&menu, 50, 50, 160);
    TEST_ASSERT(winios_menu_add_item(&menu, "Open...", 101));
    TEST_ASSERT(winios_menu_add_item(&menu, "Save", 102));
    TEST_ASSERT(winios_menu_add_item(&menu, "Exit", 103));
    TEST_ASSERT(menu.item_count == 3);

    // Hover second item (idx 1)
    winios_event_v2_t hover{};
    hover.type = WINIOS_EVENT_V2_MOUSE_MOVE;
    hover.x = 70; hover.y = 50 + 2 + 24 + 10; // y within item 1
    winios_menu_handle_event(&menu, &hover);
    TEST_ASSERT(menu.hover_index == 1);

    // Click second item
    winios_event_v2_t menu_click{};
    menu_click.type = WINIOS_EVENT_V2_MOUSE_UP;
    menu_click.x = 70; menu_click.y = 50 + 2 + 24 + 10;
    TEST_ASSERT(winios_menu_handle_event(&menu, &menu_click));
    TEST_ASSERT(menu.selected_action == 102);
    TEST_ASSERT(menu.is_open == 0);

    std::cout << "[PASS] Dialog and Menu interactions verified.\n";
}

void test_focus_manager() {
    std::cout << "[Test 4] Keyboard focus cycling and Tab navigation...\n";
    winios_focus_manager_t fm;
    winios_focus_init(&fm);

    int btn1 = 1, btn2 = 2, txt1 = 3;
    TEST_ASSERT(winios_focus_register(&fm, &btn1, 1));
    TEST_ASSERT(winios_focus_register(&fm, &txt1, 2));
    TEST_ASSERT(winios_focus_register(&fm, &btn2, 1));
    TEST_ASSERT(fm.count == 3);

    TEST_ASSERT(winios_focus_get_active(&fm) == &btn1);

    // Tab forward
    winios_focus_next(&fm);
    TEST_ASSERT(winios_focus_get_active(&fm) == &txt1);

    winios_focus_next(&fm);
    TEST_ASSERT(winios_focus_get_active(&fm) == &btn2);

    // Wrap around
    winios_focus_next(&fm);
    TEST_ASSERT(winios_focus_get_active(&fm) == &btn1);

    // Shift-Tab backward
    winios_focus_prev(&fm);
    TEST_ASSERT(winios_focus_get_active(&fm) == &btn2);

    std::cout << "[PASS] Focus manager navigation verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 223.7, 223.8, 223.9, 223.13: UI COMPONENT TESTS\n";
    std::cout << "========================================\n";

    test_textarea_multiline_editing();
    test_slider_component();
    test_dialog_and_menu();
    test_focus_manager();

    std::cout << "\nALL STAGE 223.7 COMPONENT TESTS PASSED (100%).\n";
    return 0;
}

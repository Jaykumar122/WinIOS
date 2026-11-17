// ============================================================================
// Stage 214.10: Reusable UI Controls Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "runtime/ui/controls.h"
#include "runtime/ui/render_context.h"

using namespace winios::runtime::ui;

namespace {

int g_failures = 0;

#define CTRL_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[CTRL_FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [CTRL_OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_controls_suite() {
    std::cout << "[Stage 214.10] Verifying Reusable UI Controls...\n";

    std::vector<std::uint8_t> buffer(400 * 600 * 4, 0);
    RenderContext ctx(buffer.data(), 400, 600, 400 * 4);

    // 1. Label
    Label lbl("Counter: 0", 1, 1);
    lbl.set_frame({10, 10, 200, 30});
    lbl.draw(ctx);
    CTRL_CHECK(lbl.text() == "Counter: 0", "Label text initialized");
    lbl.set_text("Counter: 1");
    CTRL_CHECK(lbl.text() == "Counter: 1", "Label text updated");

    // 2. Button
    Button btn("Increment", 2, 1);
    btn.set_frame({10, 50, 120, 44});
    bool clicked = false;
    btn.set_on_click([&](View&) {
        clicked = true;
    });
    btn.perform_click();
    CTRL_CHECK(clicked, "Button on_click callback fired");

    btn.set_pressed(true);
    CTRL_CHECK(btn.is_pressed(), "Button pressed state is true");
    btn.draw(ctx);

    // 3. Checkbox
    Checkbox chk("Done item", false, 3, 1);
    chk.set_frame({10, 110, 200, 30});
    bool checked_event = false;
    chk.set_on_checked_changed([&](Checkbox&, bool is_chk) {
        checked_event = is_chk;
    });
    CTRL_CHECK(!chk.is_checked(), "Checkbox starts unchecked");
    chk.perform_click(); // toggles checkbox
    CTRL_CHECK(chk.is_checked(), "Checkbox is now checked");
    CTRL_CHECK(checked_event, "Checkbox callback received true");
    chk.draw(ctx);

    // 4. Toggle
    Toggle tog(false, 4, 1);
    tog.set_frame({10, 150, 60, 35});
    bool toggle_event = false;
    tog.set_on_toggle_changed([&](Toggle&, bool on) {
        toggle_event = on;
    });
    tog.perform_click();
    CTRL_CHECK(tog.is_on(), "Toggle switched ON");
    CTRL_CHECK(toggle_event, "Toggle callback received true");
    tog.draw(ctx);

    // 5. TextInput
    TextInput input("Enter notes...", 5, 1);
    input.set_frame({10, 200, 250, 40});
    input.set_focused(true);
    input.set_text("Meeting at 3pm");
    CTRL_CHECK(input.text() == "Meeting at 3pm", "TextInput text updated");
    CTRL_CHECK(input.is_focused(), "TextInput is focused");
    input.draw(ctx);

    // 6. ProgressBar
    ProgressBar prog(0.5f, 6, 1);
    prog.set_frame({10, 260, 200, 10});
    CTRL_CHECK(prog.progress() == 0.5f, "ProgressBar progress == 0.5");
    prog.set_progress(1.5f); // Should clamp to 1.0
    CTRL_CHECK(prog.progress() == 1.0f, "ProgressBar progress clamped to 1.0");
    prog.draw(ctx);

    // 7. ScrollView
    ScrollView scroll(7, 1);
    scroll.set_frame({0, 300, 400, 200});
    auto scroll_child = std::make_shared<Button>("Scroll Child", 71, 1);
    scroll_child->set_frame({10, 50, 150, 40});
    scroll.add_child(scroll_child);

    // Hit test at local y=55 before scroll
    View* h1 = scroll.hit_test(20, 355);
    CTRL_CHECK(h1 == scroll_child.get(), "Hit test found child before scrolling");

    // Scroll by 100 px downwards
    scroll.scroll_by(100);
    CTRL_CHECK(scroll.scroll_offset_y() == 100, "Scroll offset is 100");

    // After scrolling down by 100, child is visually at y = 50 - 100 = -50 (out of bounds)
    // Hit test at old screen y=355 should now hit scrollview container, not child
    View* h2 = scroll.hit_test(20, 355);
    CTRL_CHECK(h2 == &scroll, "Hit test at old position misses scrolled child");

    // Hit test at child's new shifted position y = 50 + 100 = 150 (screen 300 + 50 = 350? Wait: local_y = py - frame.y + scroll_offset_y)
    // When py = 300 (top of scroll), local_y = 100.
    // Child is at y = 50, so local_y needs to be 50 -> py - 300 + 100 = 50 -> py = 250 (which is above scrollview frame).
    scroll.draw(ctx);
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 214.10: Reusable UI Controls Tests    \n";
    std::cout << "====================================================\n";

    test_controls_suite();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL CONTROLS TESTS PASSED" : "CONTROLS FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

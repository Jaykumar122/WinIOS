// ============================================================================
// Stage 214.15: Focus, Capture, and Event Propagation Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <memory>

#include "runtime/ui/event_dispatcher.h"
#include "runtime/ui/controls.h"

using namespace winios::runtime::ui;

namespace {

int g_failures = 0;

#define DISP_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[DISP_FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [DISP_OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_focus_and_capture() {
    std::cout << "[Stage 214.15] Verifying Focus, Capture, and Dispatcher...\n";

    auto root = std::make_shared<ViewGroup>(1, 1);
    root->set_frame({0, 0, 800, 1200});

    auto btn1 = std::make_shared<Button>("Button 1", 101, 1);
    btn1->set_frame({50, 50, 150, 50});
    bool btn1_clicked = false;
    btn1->set_on_click([&](View&) { btn1_clicked = true; });
    root->add_child(btn1);

    auto btn2 = std::make_shared<Button>("Button 2", 102, 1);
    btn2->set_frame({50, 120, 150, 50});
    btn2->set_enabled(false); // DISABLED
    root->add_child(btn2);

    EventDispatcher dispatcher(root.get());

    // 1. Focus acquisition on btn1
    DISP_CHECK(dispatcher.request_focus(btn1.get()), "Focus granted to btn1");
    DISP_CHECK(dispatcher.focused_view() == btn1.get(), "btn1 is focused");
    DISP_CHECK(btn1->is_focused(), "btn1 reports focused");

    // 2. Disabled view rejects focus
    DISP_CHECK(!dispatcher.request_focus(btn2.get()), "Disabled btn2 rejected focus");
    DISP_CHECK(dispatcher.focused_view() == btn1.get(), "btn1 remains focused");

    // 3. Touch dispatch and capture
    // Touch down on btn1 at (60, 60)
    DISP_CHECK(dispatcher.dispatch_touch_down(60, 60), "Touch down dispatched to btn1");
    DISP_CHECK(dispatcher.captured_view() == btn1.get(), "btn1 captured touch");
    DISP_CHECK(btn1->is_pressed(), "btn1 is pressed");

    // Touch move slightly
    DISP_CHECK(dispatcher.dispatch_touch_move(70, 70), "Touch move dispatched to captured btn1");

    // Touch up inside btn1 -> triggers click!
    DISP_CHECK(dispatcher.dispatch_touch_up(70, 70), "Touch up dispatched");
    DISP_CHECK(btn1_clicked, "btn1 on_click fired successfully");
    DISP_CHECK(dispatcher.captured_view() == nullptr, "Capture released after touch up");
    DISP_CHECK(!btn1->is_pressed(), "btn1 no longer pressed");

    // 4. Touch down on disabled view btn2 -> filtered out
    DISP_CHECK(!dispatcher.dispatch_touch_down(60, 130), "Touch down on disabled view rejected");

    // 5. Cleanup on view destruction
    dispatcher.on_view_destroyed(btn1.get());
    DISP_CHECK(dispatcher.focused_view() == nullptr, "Focus cleared on view destruction");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 214.15: Focus & Capture Tests         \n";
    std::cout << "====================================================\n";

    test_focus_and_capture();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL DISPATCHER TESTS PASSED" : "DISPATCHER FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

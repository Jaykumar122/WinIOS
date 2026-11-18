#include "settings/accessibility_service.h"
#include <cassert>
#include <iostream>

using namespace winios::settings;

static void test_node_tree_and_focus() {
    SystemStateManager mgr;
    AccessibilityService a11y(&mgr);

    assert(a11y.get_node_count() == 0);

    AccessibilityNode n1{};
    n1.id = 1;
    n1.role = AccessibilityRole::Button;
    n1.label = "OK";
    n1.x = 10;
    n1.y = 10;
    n1.width = 80;
    n1.height = 30;

    AccessibilityNode n2{};
    n2.id = 2;
    n2.role = AccessibilityRole::Button;
    n2.label = "Cancel";
    n2.x = 100;
    n2.y = 10;
    n2.width = 80;
    n2.height = 30;

    AccessibilityNode n3{};
    n3.id = 3;
    n3.role = AccessibilityRole::StaticText;
    n3.label = "Status Message";
    n3.x = 10;
    n3.y = 60;
    n3.width = 200;
    n3.height = 20;

    assert(a11y.register_node(n1));
    assert(a11y.register_node(n2));
    assert(a11y.register_node(n3));
    assert(a11y.get_node_count() == 3);

    // Initial focus on first node
    assert(a11y.get_focused_id() == 1);

    // Focus navigation
    assert(a11y.focus_next());
    assert(a11y.get_focused_id() == 2);

    assert(a11y.focus_next());
    assert(a11y.get_focused_id() == 3);

    assert(a11y.focus_next());
    assert(a11y.get_focused_id() == 1); // Wrap around

    assert(a11y.focus_prev());
    assert(a11y.get_focused_id() == 3); // Wrap backwards

    // Explicit focus
    assert(a11y.set_focus(2));
    assert(a11y.get_focused_id() == 2);

    std::cout << "[PASS] test_node_tree_and_focus\n";
}

static void test_action_invocation() {
    SystemStateManager mgr;
    AccessibilityService a11y(&mgr);

    uint64_t invoked_node_id = 0;
    std::string invoked_action;
    a11y.set_action_handler([&](uint64_t id, const std::string& act) {
        invoked_node_id = id;
        invoked_action = act;
    });

    AccessibilityNode btn{};
    btn.id = 42;
    btn.role = AccessibilityRole::Button;
    btn.label = "Submit";
    btn.is_enabled = true;
    a11y.register_node(btn);

    assert(a11y.activate_focused());
    assert(invoked_node_id == 42);
    assert(invoked_action == "activate");

    std::cout << "[PASS] test_action_invocation\n";
}

static void test_preferences_integration() {
    SystemStateManager mgr;
    AccessibilityService a11y(&mgr);

    assert(a11y.get_text_scale_percent() == 100);
    assert(a11y.get_text_scale_factor() == 1.0f);
    assert(!a11y.is_high_contrast());

    assert(a11y.set_text_scale(150));
    assert(a11y.get_text_scale_percent() == 150);
    assert(a11y.get_text_scale_factor() == 1.5f);
    assert(mgr.get_accessibility().text_scale_percent == 150);

    assert(a11y.set_high_contrast(true));
    assert(a11y.is_high_contrast());
    assert(mgr.get_accessibility().high_contrast);

    std::cout << "[PASS] test_preferences_integration\n";
}

int main() {
    std::cout << "--- Stage 228.17 Accessibility Service Tests ---\n";
    test_node_tree_and_focus();
    test_action_invocation();
    test_preferences_integration();
    std::cout << "All Stage 228.17 Accessibility Service Tests PASSED.\n";
    return 0;
}

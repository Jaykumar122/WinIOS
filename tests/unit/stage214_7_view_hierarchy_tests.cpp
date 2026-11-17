// ============================================================================
// Stage 214.7: View Hierarchy and Containers Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "runtime/ui/view.h"
#include "runtime/ui/view_group.h"

using namespace winios::runtime::ui;

namespace {

int g_failures = 0;

#define HIER_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[HIER_FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [HIER_OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_view_hierarchy() {
    std::cout << "[Stage 214.7] Verifying View Hierarchy and Cycle Rejection...\n";

    auto root = std::make_shared<ViewGroup>(100, 1);
    root->set_frame({0, 0, 800, 1200});

    auto panel = std::make_shared<ViewGroup>(200, 1);
    panel->set_frame({50, 100, 700, 1000});

    auto child1 = std::make_shared<View>(301, 1);
    child1->set_frame({10, 20, 100, 50});
    child1->set_tag("button_1");

    auto child2 = std::make_shared<View>(302, 1);
    child2->set_frame({10, 80, 100, 50});
    child2->set_tag("button_2");

    HIER_CHECK(root->add_child(panel), "Added panel to root");
    HIER_CHECK(panel->add_child(child1), "Added child1 to panel");
    HIER_CHECK(panel->add_child(child2), "Added child2 to panel");

    HIER_CHECK(root->child_count() == 1, "Root has 1 child");
    HIER_CHECK(panel->child_count() == 2, "Panel has 2 children");

    // Cycle detection: Cannot add root as child of panel
    HIER_CHECK(!panel->add_child(root), "Cycle prevented: cannot add root to panel");

    // Cycle detection: Cannot add child to itself
    HIER_CHECK(!panel->add_child(panel), "Cycle prevented: cannot add panel to itself");

    // Absolute frame computation
    Rect abs1 = child1->absolute_frame();
    HIER_CHECK(abs1.x == 60 && abs1.y == 120, "Child1 absolute coordinates: 50+10=60, 100+20=120");

    // Descendant verification
    HIER_CHECK(child1->is_descendant_of(panel.get()), "Child1 is descendant of panel");
    HIER_CHECK(child1->is_descendant_of(root.get()), "Child1 is descendant of root");
    HIER_CHECK(!root->is_descendant_of(child1.get()), "Root is NOT descendant of child1");

    // View lookup by ID and Tag
    HIER_CHECK(root->find_view_by_id(301) == child1.get(), "Found view by ID 301");
    HIER_CHECK(root->find_view_by_tag("button_2") == child2.get(), "Found view by tag button_2");

    // Hit testing (child-first, local coordinates)
    View* hit = root->hit_test(65, 125);
    HIER_CHECK(hit == child1.get(), "Hit test routed to child1");

    // Child removal
    HIER_CHECK(panel->remove_child(301), "Removed child1");
    HIER_CHECK(panel->child_count() == 1, "Panel now has 1 child");
    HIER_CHECK(child1->parent() == nullptr, "Child1 parent detached");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 214.7: View Hierarchy Tests           \n";
    std::cout << "====================================================\n";

    test_view_hierarchy();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL HIERARCHY TESTS PASSED" : "HIERARCHY FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

#include <cassert>
#include <cstring>
#include <iostream>

#include "winios/winios.h"
#include "winios/ui/layout.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

void test_column_layout() {
    std::cout << "[Test 1] Vertical column layout with spacing & flex...\n";

    winios_layout_node_t root;
    winios_layout_init_node(&root, WINIOS_LAYOUT_COLUMN);
    root.spacing = 10;
    winios_layout_set_padding(&root, 10, 10, 15, 15);

    // Child 1: Fixed header (height 30)
    winios_layout_node_t header;
    winios_layout_init_node(&header, WINIOS_LAYOUT_ABSOLUTE);
    header.min_height = 30;

    // Child 2: Flex content (flex 1)
    winios_layout_node_t content;
    winios_layout_init_node(&content, WINIOS_LAYOUT_ABSOLUTE);
    content.flex_weight = 1;

    // Child 3: Fixed footer (height 40)
    winios_layout_node_t footer;
    winios_layout_init_node(&footer, WINIOS_LAYOUT_ABSOLUTE);
    footer.min_height = 40;

    TEST_ASSERT(winios_layout_add_child(&root, &header));
    TEST_ASSERT(winios_layout_add_child(&root, &content));
    TEST_ASSERT(winios_layout_add_child(&root, &footer));

    // Calculate layout in 400x300 window
    winios_rect_t window_bounds{ 0, 0, 400, 300 };
    winios_layout_calculate(&root, window_bounds);

    // Verify root bounds
    TEST_ASSERT(root.bounds.width == 400 && root.bounds.height == 300);

    // Inner width = 400 - 30 = 370
    // Inner height = 300 - 20 = 280
    // Total spacing = 2 * 10 = 20
    // Fixed height = 30 + 40 = 70
    // Remaining flex height = 280 - 70 - 20 = 190
    TEST_ASSERT(header.bounds.x == 15 && header.bounds.y == 10);
    TEST_ASSERT(header.bounds.height == 30 && header.bounds.width == 370);

    TEST_ASSERT(content.bounds.x == 15 && content.bounds.y == 50); // 10 + 30 + 10
    TEST_ASSERT(content.bounds.height == 190 && content.bounds.width == 370);

    TEST_ASSERT(footer.bounds.x == 15 && footer.bounds.y == 250); // 50 + 190 + 10
    TEST_ASSERT(footer.bounds.height == 40 && footer.bounds.width == 370);

    std::cout << "[PASS] Column layout calculated correctly.\n";
}

void test_row_layout_and_resizing() {
    std::cout << "[Test 2] Row layout with flex ratios & window resizing reflow...\n";

    winios_layout_node_t root;
    winios_layout_init_node(&root, WINIOS_LAYOUT_ROW);
    root.spacing = 8;
    root.align_items = WINIOS_ALIGN_CENTER;

    // Left child: fixed width 100
    winios_layout_node_t left;
    winios_layout_init_node(&left, WINIOS_LAYOUT_ABSOLUTE);
    left.bounds.width = 100;
    left.bounds.height = 40;

    // Right child: flex 1
    winios_layout_node_t right;
    winios_layout_init_node(&right, WINIOS_LAYOUT_ABSOLUTE);
    right.flex_weight = 1;

    TEST_ASSERT(winios_layout_add_child(&root, &left));
    TEST_ASSERT(winios_layout_add_child(&root, &right));

    // First size: 500x200
    winios_rect_t size1{ 0, 0, 500, 200 };
    winios_layout_calculate(&root, size1);

    // Total width = 500. Left = 100, spacing = 8. Right = 500 - 100 - 8 = 392.
    TEST_ASSERT(left.bounds.x == 0 && left.bounds.width == 100);
    TEST_ASSERT(left.bounds.y == (200 - 40) / 2); // Center aligned vertically!
    TEST_ASSERT(right.bounds.x == 108 && right.bounds.width == 392);

    // Responsive Window Resize: expand to 800x400
    winios_rect_t size2{ 0, 0, 800, 400 };
    winios_layout_calculate(&root, size2);

    // Right child automatically reflows to 800 - 100 - 8 = 692!
    TEST_ASSERT(left.bounds.width == 100);
    TEST_ASSERT(left.bounds.y == (400 - 40) / 2);
    TEST_ASSERT(right.bounds.x == 108 && right.bounds.width == 692);

    std::cout << "[PASS] Row layout and responsive resize reflow verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 223.4 & 223.5: LAYOUT ENGINE TESTS\n";
    std::cout << "========================================\n";

    test_column_layout();
    test_row_layout_and_resizing();

    std::cout << "\nALL STAGE 223.4 & 223.5 TESTS PASSED (100%).\n";
    return 0;
}

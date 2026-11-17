// ============================================================================
// Stage 214.8: Layout Engine Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "runtime/ui/layout_engine.h"

using namespace winios::runtime::ui;

namespace {

int g_failures = 0;

#define LAYOUT_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[LAYOUT_FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [LAYOUT_OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_vbox_layout() {
    std::cout << "[Stage 214.8] Verifying VBox Vertical Layout Engine...\n";

    auto vbox = std::make_shared<VBox>(10, 1, 1);
    vbox->set_frame({0, 0, 400, 600});
    LayoutParams vbox_lp{};
    vbox_lp.padding = {10, 20, 10, 20}; // left=10, top=20, right=10, bottom=20
    vbox->set_layout_params(vbox_lp);

    auto item1 = std::make_shared<View>(101, 1);
    LayoutParams lp1{};
    lp1.width = LayoutDimension::make_fixed(100);
    lp1.height = LayoutDimension::make_fixed(50);
    lp1.horizontal_alignment = Alignment::Start;
    item1->set_layout_params(lp1);
    vbox->add_child(item1);

    auto item2 = std::make_shared<View>(102, 1);
    LayoutParams lp2{};
    lp2.width = LayoutDimension::make_match_parent(); // should be 400 - 10 - 10 = 380
    lp2.height = LayoutDimension::make_fixed(60);
    item2->set_layout_params(lp2);
    vbox->add_child(item2);

    auto item3 = std::make_shared<View>(103, 1);
    LayoutParams lp3{};
    lp3.width = LayoutDimension::make_fixed(80);
    lp3.height = LayoutDimension::make_fixed(40);
    lp3.horizontal_alignment = Alignment::Center; // centered in (400 - 20) = 380 -> x = 10 + (380-80)/2 = 160
    item3->set_layout_params(lp3);
    vbox->add_child(item3);

    vbox->layout();

    // Check item1: x=10, y=20, w=100, h=50
    LAYOUT_CHECK(item1->frame().x == 10 && item1->frame().y == 20, "Item1 position (10, 20)");
    LAYOUT_CHECK(item1->frame().width == 100 && item1->frame().height == 50, "Item1 size (100, 50)");

    // Check item2: y = 20 + 50 + 10 = 80, x=10, w=380, h=60
    LAYOUT_CHECK(item2->frame().x == 10 && item2->frame().y == 80, "Item2 position (10, 80)");
    LAYOUT_CHECK(item2->frame().width == 380 && item2->frame().height == 60, "Item2 MatchParent width == 380");

    // Check item3: y = 80 + 60 + 10 = 150, x = 10 + (380 - 80)/2 = 160
    LAYOUT_CHECK(item3->frame().x == 160 && item3->frame().y == 150, "Item3 Center aligned x=160, y=150");
    LAYOUT_CHECK(item3->frame().width == 80 && item3->frame().height == 40, "Item3 size (80, 40)");
}

void test_hbox_layout() {
    std::cout << "[Stage 214.8] Verifying HBox Horizontal Layout Engine...\n";

    auto hbox = std::make_shared<HBox>(15, 2, 1);
    hbox->set_frame({0, 0, 500, 200});
    LayoutParams hbox_lp{};
    hbox_lp.padding = {20, 10, 20, 10};
    hbox->set_layout_params(hbox_lp);

    auto btn1 = std::make_shared<View>(201, 1);
    LayoutParams lp1{};
    lp1.width = LayoutDimension::make_fixed(120);
    lp1.height = LayoutDimension::make_match_parent(); // 200 - 10 - 10 = 180
    btn1->set_layout_params(lp1);
    hbox->add_child(btn1);

    auto btn2 = std::make_shared<View>(202, 1);
    LayoutParams lp2{};
    lp2.width = LayoutDimension::make_fixed(150);
    lp2.height = LayoutDimension::make_fixed(60);
    lp2.vertical_alignment = Alignment::Center; // centered vertically in 180: y = 10 + (180-60)/2 = 70
    btn2->set_layout_params(lp2);
    hbox->add_child(btn2);

    hbox->layout();

    // Check btn1: x=20, y=10, w=120, h=180
    LAYOUT_CHECK(btn1->frame().x == 20 && btn1->frame().y == 10, "Btn1 position (20, 10)");
    LAYOUT_CHECK(btn1->frame().width == 120 && btn1->frame().height == 180, "Btn1 MatchParent height == 180");

    // Check btn2: x = 20 + 120 + 15 = 155, y = 10 + (180-60)/2 = 70
    LAYOUT_CHECK(btn2->frame().x == 155 && btn2->frame().y == 70, "Btn2 Center aligned x=155, y=70");
    LAYOUT_CHECK(btn2->frame().width == 150 && btn2->frame().height == 60, "Btn2 size (150, 60)");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 214.8: Layout Engine Tests            \n";
    std::cout << "====================================================\n";

    test_vbox_layout();
    test_hbox_layout();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL LAYOUT TESTS PASSED" : "LAYOUT FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

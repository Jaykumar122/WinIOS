#include "runtime/ui/counter_app_view.h"

namespace winios::runtime::ui {

CounterAppView::CounterAppView(std::uint32_t view_id, std::uint32_t pid)
    : ViewGroup(view_id, pid) {

    // Load persisted count if exists
    auto saved = kernel::AppPreferencesManager::instance().get_int("com.winios.counter", "count");
    if (saved.has_value()) {
        count_ = *saved;
    }

    set_frame({0, 0, 700, 900});

    main_layout_ = std::make_shared<VBox>(24, view_id + 1, pid);
    main_layout_->set_frame({0, 0, 700, 900});
    LayoutParams root_lp{};
    root_lp.padding = {40, 60, 40, 40};
    main_layout_->set_layout_params(root_lp);

    // 1. Title Label
    title_label_ = std::make_shared<Label>("Counter Application", view_id + 2, pid);
    title_label_->set_font_scale(2);
    title_label_->set_text_color(0xFFFFFFFF);
    title_label_->set_alignment(TextAlignment::Center);
    LayoutParams title_lp{};
    title_lp.width = LayoutDimension::make_match_parent();
    title_lp.height = LayoutDimension::make_fixed(40);
    title_label_->set_layout_params(title_lp);
    main_layout_->add_child(title_label_);

    // 2. Count Label
    count_label_ = std::make_shared<Label>("Count: " + std::to_string(count_), view_id + 3, pid);
    count_label_->set_font_scale(3);
    count_label_->set_text_color(0xFF34C759); // Green
    count_label_->set_alignment(TextAlignment::Center);
    LayoutParams count_lp{};
    count_lp.width = LayoutDimension::make_match_parent();
    count_lp.height = LayoutDimension::make_fixed(60);
    count_label_->set_layout_params(count_lp);
    main_layout_->add_child(count_label_);

    // 3. Button Bar
    button_bar_ = std::make_shared<HBox>(16, view_id + 4, pid);
    LayoutParams bar_lp{};
    bar_lp.width = LayoutDimension::make_match_parent();
    bar_lp.height = LayoutDimension::make_fixed(50);
    button_bar_->set_layout_params(bar_lp);

    inc_btn_ = std::make_shared<Button>("+1 Increment", view_id + 5, pid);
    inc_btn_->set_bg_color(0xFF007AFF); // Blue
    LayoutParams inc_lp{};
    inc_lp.width = LayoutDimension::make_fixed(180);
    inc_lp.height = LayoutDimension::make_fixed(50);
    inc_btn_->set_layout_params(inc_lp);
    inc_btn_->set_on_click([this](View&) {
        increment();
    });
    button_bar_->add_child(inc_btn_);

    reset_btn_ = std::make_shared<Button>("Reset", view_id + 6, pid);
    reset_btn_->set_bg_color(0xFFFF9500); // Orange
    LayoutParams reset_lp{};
    reset_lp.width = LayoutDimension::make_fixed(120);
    reset_lp.height = LayoutDimension::make_fixed(50);
    reset_btn_->set_layout_params(reset_lp);
    reset_btn_->set_on_click([this](View&) {
        reset_count();
    });
    button_bar_->add_child(reset_btn_);

    exit_btn_ = std::make_shared<Button>("Exit App", view_id + 7, pid);
    exit_btn_->set_bg_color(0xFFFF3B30); // Red
    LayoutParams exit_lp{};
    exit_lp.width = LayoutDimension::make_fixed(120);
    exit_lp.height = LayoutDimension::make_fixed(50);
    exit_btn_->set_layout_params(exit_lp);
    exit_btn_->set_on_click([this](View&) {
        if (on_exit_) on_exit_();
    });
    button_bar_->add_child(exit_btn_);

    main_layout_->add_child(button_bar_);
    add_child(main_layout_);

    main_layout_->layout();
}

void CounterAppView::increment() {
    ++count_;
    count_label_->set_text("Count: " + std::to_string(count_));
    kernel::AppPreferencesManager::instance().set_int("com.winios.counter", "count", count_);
    invalidate();
}

void CounterAppView::reset_count() {
    count_ = 0;
    count_label_->set_text("Count: 0");
    kernel::AppPreferencesManager::instance().set_int("com.winios.counter", "count", 0);
    invalidate();
}

void CounterAppView::render_to_surface(RenderContext& ctx) {
    // Fill background with dark surface color
    ctx.fill_rect(frame_, 0xFF1C1C1E);
    draw(ctx);
}

} // namespace winios::runtime::ui

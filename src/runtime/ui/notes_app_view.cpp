#include "runtime/ui/notes_app_view.h"

namespace winios::runtime::ui {

NotesAppView::NotesAppView(std::uint32_t view_id, std::uint32_t pid)
    : ViewGroup(view_id, pid) {

    set_frame({0, 0, 700, 900});

    main_layout_ = std::make_shared<VBox>(16, view_id + 1, pid);
    main_layout_->set_frame({0, 0, 700, 900});
    LayoutParams root_lp{};
    root_lp.padding = {30, 40, 30, 30};
    main_layout_->set_layout_params(root_lp);

    // Title
    title_label_ = std::make_shared<Label>("Notes & Tasks", view_id + 2, pid);
    title_label_->set_font_scale(2);
    title_label_->set_text_color(0xFFFFFFFF);
    LayoutParams title_lp{};
    title_lp.width = LayoutDimension::make_match_parent();
    title_lp.height = LayoutDimension::make_fixed(35);
    title_label_->set_layout_params(title_lp);
    main_layout_->add_child(title_label_);

    // Text Input field
    input_ = std::make_shared<TextInput>("Add a new item...", view_id + 3, pid);
    input_->set_text("Review WinIOS Sprint Deliverables");
    LayoutParams input_lp{};
    input_lp.width = LayoutDimension::make_match_parent();
    input_lp.height = LayoutDimension::make_fixed(44);
    input_->set_layout_params(input_lp);
    main_layout_->add_child(input_);

    // ScrollView for Checklist
    scroll_view_ = std::make_shared<ScrollView>(view_id + 10, pid);
    LayoutParams scroll_lp{};
    scroll_lp.width = LayoutDimension::make_match_parent();
    scroll_lp.height = LayoutDimension::make_fixed(500);
    scroll_view_->set_layout_params(scroll_lp);
    scroll_view_->set_frame({0, 0, 640, 500});

    scroll_content_ = std::make_shared<VBox>(12, view_id + 20, pid);
    scroll_content_->set_frame({0, 0, 640, 500});
    LayoutParams content_lp{};
    content_lp.padding = {10, 10, 10, 10};
    scroll_content_->set_layout_params(content_lp);

    // Create 5 checklist items
    const char* tasks[5] = {
        "1. Complete Baseline & Artifact Audit",
        "2. Build Reusable Guest UI Framework",
        "3. Implement Gesture Recognizers & Event Loop",
        "4. Enforce App Lifecycle & Persist State",
        "5. Execute 15-Step Interactive Verification"
    };

    auto& pm = kernel::AppPreferencesManager::instance();

    for (std::size_t i = 0; i < 5; ++i) {
        std::string key = "item_" + std::to_string(i);
        bool initial_chk = (i < 2); // items 0, 1 default checked
        auto saved = pm.get_bool("com.winios.notes", key);
        if (saved.has_value()) {
            initial_chk = *saved;
        }

        auto chk = std::make_shared<Checkbox>(tasks[i], initial_chk, static_cast<std::uint32_t>(view_id + 30 + i), pid);
        LayoutParams chk_lp{};
        chk_lp.width = LayoutDimension::make_match_parent();
        chk_lp.height = LayoutDimension::make_fixed(40);
        chk->set_layout_params(chk_lp);

        chk->set_on_checked_changed([key](Checkbox&, bool is_checked) {
            kernel::AppPreferencesManager::instance().set_bool("com.winios.notes", key, is_checked);
        });

        checklist_items_.push_back(chk);
        scroll_content_->add_child(chk);
    }

    scroll_content_->layout();
    scroll_view_->add_child(scroll_content_);
    main_layout_->add_child(scroll_view_);

    // Close Button
    close_btn_ = std::make_shared<Button>("Close Notes", view_id + 40, pid);
    close_btn_->set_bg_color(0xFF3A3A3C);
    LayoutParams btn_lp{};
    btn_lp.width = LayoutDimension::make_match_parent();
    btn_lp.height = LayoutDimension::make_fixed(50);
    close_btn_->set_layout_params(btn_lp);
    close_btn_->set_on_click([this](View&) {
        if (on_close_) on_close_();
    });
    main_layout_->add_child(close_btn_);

    add_child(main_layout_);
    main_layout_->layout();
}

void NotesAppView::toggle_item(std::size_t index) {
    if (index < checklist_items_.size()) {
        checklist_items_[index]->toggle();
    }
}

bool NotesAppView::is_item_checked(std::size_t index) const {
    if (index < checklist_items_.size()) {
        return checklist_items_[index]->is_checked();
    }
    return false;
}

void NotesAppView::render_to_surface(RenderContext& ctx) {
    ctx.fill_rect(frame_, 0xFF000000); // Black grouped background
    draw(ctx);
}

} // namespace winios::runtime::ui

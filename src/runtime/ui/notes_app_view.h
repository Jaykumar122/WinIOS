#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "runtime/ui/view_group.h"
#include "runtime/ui/layout_engine.h"
#include "runtime/ui/controls.h"
#include "kernel/app_preferences.h"

namespace winios::runtime::ui {

class NotesAppView : public ViewGroup {
public:
    explicit NotesAppView(std::uint32_t view_id = 50, std::uint32_t pid = 3);

    void toggle_item(std::size_t index);
    [[nodiscard]] bool is_item_checked(std::size_t index) const;

    [[nodiscard]] std::shared_ptr<ScrollView> scroll_view() const noexcept { return scroll_view_; }
    [[nodiscard]] std::shared_ptr<TextInput> text_input() const noexcept { return input_; }
    [[nodiscard]] std::shared_ptr<Button> close_button() const noexcept { return close_btn_; }

    void set_on_close(std::function<void()> cb) { on_close_ = std::move(cb); }

    void render_to_surface(RenderContext& ctx);

private:
    std::shared_ptr<VBox> main_layout_;
    std::shared_ptr<Label> title_label_;
    std::shared_ptr<TextInput> input_;
    std::shared_ptr<ScrollView> scroll_view_;
    std::shared_ptr<VBox> scroll_content_;
    std::vector<std::shared_ptr<Checkbox>> checklist_items_;
    std::shared_ptr<Button> close_btn_;
    std::function<void()> on_close_;
};

} // namespace winios::runtime::ui

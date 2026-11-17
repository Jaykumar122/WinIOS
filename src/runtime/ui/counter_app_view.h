#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "runtime/ui/view_group.h"
#include "runtime/ui/layout_engine.h"
#include "runtime/ui/controls.h"
#include "kernel/app_preferences.h"

namespace winios::runtime::ui {

class CounterAppView : public ViewGroup {
public:
    explicit CounterAppView(std::uint32_t view_id = 10, std::uint32_t pid = 2);

    void increment();
    void reset_count();

    [[nodiscard]] std::int64_t current_count() const noexcept { return count_; }

    [[nodiscard]] std::shared_ptr<Label> count_label() const noexcept { return count_label_; }
    [[nodiscard]] std::shared_ptr<Button> inc_button() const noexcept { return inc_btn_; }
    [[nodiscard]] std::shared_ptr<Button> reset_button() const noexcept { return reset_btn_; }
    [[nodiscard]] std::shared_ptr<Button> exit_button() const noexcept { return exit_btn_; }

    void set_on_exit(std::function<void()> cb) { on_exit_ = std::move(cb); }

    void render_to_surface(RenderContext& ctx);

private:
    std::int64_t count_{0};
    std::shared_ptr<VBox> main_layout_;
    std::shared_ptr<Label> title_label_;
    std::shared_ptr<Label> count_label_;
    std::shared_ptr<HBox> button_bar_;
    std::shared_ptr<Button> inc_btn_;
    std::shared_ptr<Button> reset_btn_;
    std::shared_ptr<Button> exit_btn_;
    std::function<void()> on_exit_;
};

} // namespace winios::runtime::ui

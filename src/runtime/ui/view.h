#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "runtime/ui/ui_types.h"
#include "runtime/ui/render_context.h"

namespace winios::runtime::ui {

class ViewGroup;

class View : public std::enable_shared_from_this<View> {
public:
    using ClickCallback = std::function<void(View&)>;

    explicit View(std::uint32_t view_id = 0, std::uint32_t pid = 0);
    virtual ~View() = default;

    [[nodiscard]] std::uint32_t id() const noexcept { return id_; }
    void set_id(std::uint32_t id) noexcept { id_ = id; }

    [[nodiscard]] std::uint32_t pid() const noexcept { return pid_; }
    void set_pid(std::uint32_t pid) noexcept { pid_ = pid; }

    [[nodiscard]] const std::string& tag() const noexcept { return tag_; }
    void set_tag(const std::string& tag) { tag_ = tag; }

    [[nodiscard]] const Rect& frame() const noexcept { return frame_; }
    void set_frame(const Rect& f) noexcept;
    void set_position(std::int32_t x, std::int32_t y) noexcept;
    void set_size(std::int32_t w, std::int32_t h) noexcept;

    [[nodiscard]] Rect absolute_frame() const noexcept;

    [[nodiscard]] const LayoutParams& layout_params() const noexcept { return layout_params_; }
    void set_layout_params(const LayoutParams& lp) noexcept { layout_params_ = lp; }

    [[nodiscard]] Visibility visibility() const noexcept { return visibility_; }
    void set_visibility(Visibility v) noexcept;

    [[nodiscard]] bool is_enabled() const noexcept { return is_enabled_; }
    void set_enabled(bool e) noexcept { is_enabled_ = e; invalidate(); }

    [[nodiscard]] bool is_focused() const noexcept { return is_focused_; }
    void set_focused(bool f) noexcept { is_focused_ = f; invalidate(); }

    [[nodiscard]] bool is_pressed() const noexcept { return is_pressed_; }
    void set_pressed(bool p) noexcept { is_pressed_ = p; invalidate(); }

    [[nodiscard]] bool is_selected() const noexcept { return is_selected_; }
    void set_selected(bool s) noexcept { is_selected_ = s; invalidate(); }

    [[nodiscard]] ViewGroup* parent() const noexcept { return parent_; }
    void set_parent(ViewGroup* p) noexcept { parent_ = p; }

    [[nodiscard]] bool is_descendant_of(const View* ancestor) const noexcept;

    virtual void invalidate();
    virtual void request_layout();

    virtual View* hit_test(std::int32_t px, std::int32_t py);

    virtual void draw(RenderContext& ctx);

    void set_on_click(ClickCallback cb) { on_click_ = std::move(cb); }
    void perform_click();

protected:
    std::uint32_t id_{0};
    std::uint32_t pid_{0};
    std::string tag_;
    Rect frame_{0, 0, 0, 0};
    LayoutParams layout_params_{};
    Visibility visibility_{Visibility::Visible};
    bool is_enabled_{true};
    bool is_focused_{false};
    bool is_pressed_{false};
    bool is_selected_{false};
    ViewGroup* parent_{nullptr};
    ClickCallback on_click_;
};

} // namespace winios::runtime::ui

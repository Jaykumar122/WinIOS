#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <memory>

#include "runtime/ui/view.h"
#include "runtime/ui/view_group.h"
#include "runtime/ui/text_renderer.h"

namespace winios::runtime::ui {

// -----------------------------------------------------------------------------
// Label: Deterministic Text Display Control
// -----------------------------------------------------------------------------
class Label : public View {
public:
    explicit Label(const std::string& text = "", std::uint32_t view_id = 0, std::uint32_t pid = 0);

    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    void set_text(const std::string& text);

    [[nodiscard]] std::uint32_t text_color() const noexcept { return text_color_; }
    void set_text_color(std::uint32_t color) noexcept { text_color_ = color; invalidate(); }

    [[nodiscard]] std::uint32_t font_scale() const noexcept { return font_scale_; }
    void set_font_scale(std::uint32_t scale) noexcept { font_scale_ = scale; invalidate(); request_layout(); }

    [[nodiscard]] TextAlignment alignment() const noexcept { return alignment_; }
    void set_alignment(TextAlignment align) noexcept { alignment_ = align; invalidate(); }

    void draw(RenderContext& ctx) override;

private:
    std::string text_;
    std::uint32_t text_color_{0xFFFFFFFF};
    std::uint32_t font_scale_{1};
    TextAlignment alignment_{TextAlignment::Left};
};

// -----------------------------------------------------------------------------
// Button: Interactive Rounded Button with Click and Press State
// -----------------------------------------------------------------------------
class Button : public View {
public:
    explicit Button(const std::string& label = "", std::uint32_t view_id = 0, std::uint32_t pid = 0);

    [[nodiscard]] const std::string& label() const noexcept { return label_; }
    void set_label(const std::string& label);

    [[nodiscard]] std::uint32_t bg_color() const noexcept { return bg_color_; }
    void set_bg_color(std::uint32_t c) noexcept { bg_color_ = c; invalidate(); }

    [[nodiscard]] std::uint32_t text_color() const noexcept { return text_color_; }
    void set_text_color(std::uint32_t c) noexcept { text_color_ = c; invalidate(); }

    [[nodiscard]] std::int32_t corner_radius() const noexcept { return corner_radius_; }
    void set_corner_radius(std::int32_t r) noexcept { corner_radius_ = r; invalidate(); }

    void draw(RenderContext& ctx) override;

private:
    std::string label_;
    std::uint32_t bg_color_{0xFF007AFF};
    std::uint32_t text_color_{0xFFFFFFFF};
    std::int32_t corner_radius_{10};
};

// -----------------------------------------------------------------------------
// Checkbox: Toggleable Checkbox with Label
// -----------------------------------------------------------------------------
class Checkbox : public View {
public:
    using CheckedCallback = std::function<void(Checkbox&, bool)>;

    explicit Checkbox(const std::string& label = "", bool checked = false, std::uint32_t view_id = 0, std::uint32_t pid = 0);

    [[nodiscard]] bool is_checked() const noexcept { return is_checked_; }
    void set_checked(bool checked);

    void toggle();

    void set_on_checked_changed(CheckedCallback cb) { on_checked_ = std::move(cb); }

    void draw(RenderContext& ctx) override;

private:
    std::string label_;
    bool is_checked_{false};
    CheckedCallback on_checked_;
};

// -----------------------------------------------------------------------------
// Toggle: iOS-style Pill Switch
// -----------------------------------------------------------------------------
class Toggle : public View {
public:
    using ToggleCallback = std::function<void(Toggle&, bool)>;

    explicit Toggle(bool on = false, std::uint32_t view_id = 0, std::uint32_t pid = 0);

    [[nodiscard]] bool is_on() const noexcept { return is_on_; }
    void set_on(bool on);

    void toggle();

    void set_on_toggle_changed(ToggleCallback cb) { on_toggle_ = std::move(cb); }

    void draw(RenderContext& ctx) override;

private:
    bool is_on_{false};
    ToggleCallback on_toggle_;
};

// -----------------------------------------------------------------------------
// TextInput: Text Field Foundation with Placeholder
// -----------------------------------------------------------------------------
class TextInput : public View {
public:
    explicit TextInput(const std::string& placeholder = "", std::uint32_t view_id = 0, std::uint32_t pid = 0);

    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    void set_text(const std::string& text);

    [[nodiscard]] const std::string& placeholder() const noexcept { return placeholder_; }
    void set_placeholder(const std::string& p);

    void draw(RenderContext& ctx) override;

private:
    std::string text_;
    std::string placeholder_;
};

// -----------------------------------------------------------------------------
// ProgressBar: Deterministic Progress Indicator
// -----------------------------------------------------------------------------
class ProgressBar : public View {
public:
    explicit ProgressBar(float progress = 0.0f, std::uint32_t view_id = 0, std::uint32_t pid = 0);

    [[nodiscard]] float progress() const noexcept { return progress_; }
    void set_progress(float p);

    void draw(RenderContext& ctx) override;

private:
    float progress_{0.0f};
};

// -----------------------------------------------------------------------------
// ScrollView: Vertical Scroll Container
// -----------------------------------------------------------------------------
class ScrollView : public ViewGroup {
public:
    explicit ScrollView(std::uint32_t view_id = 0, std::uint32_t pid = 0);

    [[nodiscard]] std::int32_t scroll_offset_y() const noexcept { return scroll_offset_y_; }
    void set_scroll_offset_y(std::int32_t offset);
    void scroll_by(std::int32_t delta_y);

    View* hit_test(std::int32_t px, std::int32_t py) override;
    void draw(RenderContext& ctx) override;

private:
    std::int32_t scroll_offset_y_{0};
    std::int32_t content_height_{0};
};

} // namespace winios::runtime::ui

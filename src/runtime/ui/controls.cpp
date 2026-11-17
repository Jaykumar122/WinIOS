#include "runtime/ui/controls.h"
#include <algorithm>

namespace winios::runtime::ui {

// -----------------------------------------------------------------------------
// Label
// -----------------------------------------------------------------------------
Label::Label(const std::string& text, std::uint32_t view_id, std::uint32_t pid)
    : View(view_id, pid), text_(text) {}

void Label::set_text(const std::string& text) {
    if (text_ != text) {
        text_ = text;
        invalidate();
        request_layout();
    }
}

void Label::draw(RenderContext& ctx) {
    if (visibility_ != Visibility::Visible || text_.empty()) return;
    Rect abs_f = absolute_frame();
    TextRenderer::render_text(ctx, abs_f, text_, text_color_, font_scale_, alignment_, true);
}

// -----------------------------------------------------------------------------
// Button
// -----------------------------------------------------------------------------
Button::Button(const std::string& label, std::uint32_t view_id, std::uint32_t pid)
    : View(view_id, pid), label_(label) {}

void Button::set_label(const std::string& label) {
    if (label_ != label) {
        label_ = label;
        invalidate();
    }
}

void Button::draw(RenderContext& ctx) {
    if (visibility_ != Visibility::Visible) return;
    Rect abs_f = absolute_frame();

    std::uint32_t fill = bg_color_;
    if (!is_enabled_) {
        fill = 0xFF48484A; // Disabled gray
    } else if (is_pressed_) {
        // Darken button by reducing RGB components
        std::uint32_t r = ((fill >> 16u) & 0xFFu) * 3 / 4;
        std::uint32_t g = ((fill >> 8u) & 0xFFu) * 3 / 4;
        std::uint32_t b = (fill & 0xFFu) * 3 / 4;
        fill = 0xFF000000u | (r << 16u) | (g << 8u) | b;
    }

    ctx.draw_rounded_rect(abs_f, corner_radius_, fill);

    if (!label_.empty()) {
        auto m = TextRenderer::metrics(1);
        std::int32_t text_w = static_cast<std::int32_t>(label_.size()) * m.glyph_width;
        std::int32_t tx = abs_f.x + std::max(0, (abs_f.width - text_w) / 2);
        std::int32_t ty = abs_f.y + std::max(0, (abs_f.height - m.glyph_height) / 2);
        ctx.draw_string(tx, ty, label_.c_str(), text_color_, 1);
    }
}

// -----------------------------------------------------------------------------
// Checkbox
// -----------------------------------------------------------------------------
Checkbox::Checkbox(const std::string& label, bool checked, std::uint32_t view_id, std::uint32_t pid)
    : View(view_id, pid), label_(label), is_checked_(checked) {
    set_on_click([this](View&) {
        toggle();
    });
}

void Checkbox::set_checked(bool checked) {
    if (is_checked_ != checked) {
        is_checked_ = checked;
        invalidate();
        if (on_checked_) {
            on_checked_(*this, is_checked_);
        }
    }
}

void Checkbox::toggle() {
    set_checked(!is_checked_);
}

void Checkbox::draw(RenderContext& ctx) {
    if (visibility_ != Visibility::Visible) return;
    Rect abs_f = absolute_frame();

    std::int32_t box_size = std::min(abs_f.height - 4, 24);
    if (box_size < 12) box_size = 12;
    std::int32_t box_y = abs_f.y + (abs_f.height - box_size) / 2;

    Rect box_rect{abs_f.x, box_y, box_size, box_size};

    if (is_checked_) {
        ctx.draw_rounded_rect(box_rect, 4, 0xFF34C759); // iOS Green fill
        // Draw white checkmark symbol slash and backslash
        ctx.draw_line(box_rect.x + 4, box_rect.y + box_size / 2, box_rect.x + box_size / 2 - 1, box_rect.y + box_size - 6, 0xFFFFFFFF);
        ctx.draw_line(box_rect.x + box_size / 2 - 1, box_rect.y + box_size - 6, box_rect.x + box_size - 5, box_rect.y + 5, 0xFFFFFFFF);
    } else {
        ctx.draw_rounded_rect(box_rect, 4, 0xFF2C2C2E); // Dark surface
        ctx.draw_rect_outline(box_rect, 0xFF8E8E93, 2); // Gray border
    }

    if (!label_.empty()) {
        auto m = TextRenderer::metrics(1);
        std::int32_t tx = abs_f.x + box_size + 10;
        std::int32_t ty = abs_f.y + (abs_f.height - m.glyph_height) / 2;
        ctx.draw_string(tx, ty, label_.c_str(), 0xFFFFFFFF, 1);
    }
}

// -----------------------------------------------------------------------------
// Toggle
// -----------------------------------------------------------------------------
Toggle::Toggle(bool on, std::uint32_t view_id, std::uint32_t pid)
    : View(view_id, pid), is_on_(on) {
    set_on_click([this](View&) {
        toggle();
    });
}

void Toggle::set_on(bool on) {
    if (is_on_ != on) {
        is_on_ = on;
        invalidate();
        if (on_toggle_) {
            on_toggle_(*this, is_on_);
        }
    }
}

void Toggle::toggle() {
    set_on(!is_on_);
}

void Toggle::draw(RenderContext& ctx) {
    if (visibility_ != Visibility::Visible) return;
    Rect abs_f = absolute_frame();

    std::int32_t tw = 50;
    std::int32_t th = 30;
    std::int32_t tx = abs_f.x;
    std::int32_t ty = abs_f.y + (abs_f.height - th) / 2;
    Rect pill_rect{tx, ty, tw, th};

    std::uint32_t pill_color = is_on_ ? 0xFF34C759 : 0xFF3A3A3C; // Green vs Dark Gray
    ctx.draw_rounded_rect(pill_rect, 15, pill_color);

    // Knob
    std::int32_t knob_size = 26;
    std::int32_t knob_x = is_on_ ? (tx + tw - knob_size - 2) : (tx + 2);
    std::int32_t knob_y = ty + 2;
    Rect knob_rect{knob_x, knob_y, knob_size, knob_size};
    ctx.draw_rounded_rect(knob_rect, 13, 0xFFFFFFFF);
}

// -----------------------------------------------------------------------------
// TextInput
// -----------------------------------------------------------------------------
TextInput::TextInput(const std::string& placeholder, std::uint32_t view_id, std::uint32_t pid)
    : View(view_id, pid), placeholder_(placeholder) {}

void TextInput::set_text(const std::string& text) {
    if (text_ != text) {
        text_ = text;
        invalidate();
    }
}

void TextInput::set_placeholder(const std::string& p) {
    if (placeholder_ != p) {
        placeholder_ = p;
        invalidate();
    }
}

void TextInput::draw(RenderContext& ctx) {
    if (visibility_ != Visibility::Visible) return;
    Rect abs_f = absolute_frame();

    // Background & border
    ctx.draw_rounded_rect(abs_f, 8, 0xFF1C1C1E);
    std::uint32_t border_col = is_focused_ ? 0xFF007AFF : 0xFF38383A;
    ctx.draw_rect_outline(abs_f, border_col, is_focused_ ? 2 : 1);

    auto m = TextRenderer::metrics(1);
    std::int32_t tx = abs_f.x + 8;
    std::int32_t ty = abs_f.y + (abs_f.height - m.glyph_height) / 2;

    if (!text_.empty()) {
        ctx.draw_string(tx, ty, text_.c_str(), 0xFFFFFFFF, 1);
        if (is_focused_) {
            std::int32_t cursor_x = tx + static_cast<std::int32_t>(text_.size()) * m.glyph_width + 1;
            ctx.draw_line(cursor_x, ty, cursor_x, ty + m.glyph_height, 0xFF007AFF);
        }
    } else if (!placeholder_.empty()) {
        ctx.draw_string(tx, ty, placeholder_.c_str(), 0xFF8E8E93, 1);
    }
}

// -----------------------------------------------------------------------------
// ProgressBar
// -----------------------------------------------------------------------------
ProgressBar::ProgressBar(float progress, std::uint32_t view_id, std::uint32_t pid)
    : View(view_id, pid), progress_(std::clamp(progress, 0.0f, 1.0f)) {}

void ProgressBar::set_progress(float p) {
    float clamped = std::clamp(p, 0.0f, 1.0f);
    if (progress_ != clamped) {
        progress_ = clamped;
        invalidate();
    }
}

void ProgressBar::draw(RenderContext& ctx) {
    if (visibility_ != Visibility::Visible) return;
    Rect abs_f = absolute_frame();

    // Track
    ctx.draw_rounded_rect(abs_f, abs_f.height / 2, 0xFF3A3A3C);

    // Indicator
    std::int32_t fill_w = static_cast<std::int32_t>(abs_f.width * progress_);
    if (fill_w > 0) {
        Rect fill_rect{abs_f.x, abs_f.y, fill_w, abs_f.height};
        ctx.draw_rounded_rect(fill_rect, abs_f.height / 2, 0xFF007AFF);
    }
}

// -----------------------------------------------------------------------------
// ScrollView
// -----------------------------------------------------------------------------
ScrollView::ScrollView(std::uint32_t view_id, std::uint32_t pid)
    : ViewGroup(view_id, pid) {
    clip_children_ = true;
}

void ScrollView::set_scroll_offset_y(std::int32_t offset) {
    std::int32_t new_offset = std::max(0, offset);
    if (scroll_offset_y_ != new_offset) {
        scroll_offset_y_ = new_offset;
        invalidate();
    }
}

void ScrollView::scroll_by(std::int32_t delta_y) {
    set_scroll_offset_y(scroll_offset_y_ + delta_y);
}

View* ScrollView::hit_test(std::int32_t px, std::int32_t py) {
    if (visibility_ != Visibility::Visible || !is_enabled_) return nullptr;
    if (!frame_.contains(px, py)) return nullptr;

    std::int32_t local_x = px - frame_.x;
    std::int32_t local_y = py - frame_.y + scroll_offset_y_;

    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        auto* target = (*it)->hit_test(local_x, local_y);
        if (target) {
            return target;
        }
    }
    return this;
}

void ScrollView::draw(RenderContext& ctx) {
    if (visibility_ != Visibility::Visible) return;

    Rect abs_f = absolute_frame();
    ctx.push_clip(abs_f);

    // Render children shifted by -scroll_offset_y
    for (auto& child : children_) {
        if (!child || child->visibility() == Visibility::Gone) continue;
        Rect orig = child->frame();
        child->set_position(orig.x, orig.y - scroll_offset_y_);
        child->draw(ctx);
        child->set_position(orig.x, orig.y); // restore position
    }

    // Draw scroll indicator on the right edge if content exceeds view
    if (scroll_offset_y_ > 0) {
        std::int32_t bar_w = 4;
        std::int32_t bar_h = std::max(20, abs_f.height / 4);
        std::int32_t bar_x = abs_f.x + abs_f.width - bar_w - 2;
        std::int32_t bar_y = abs_f.y + std::min(abs_f.height - bar_h, scroll_offset_y_ / 4);
        ctx.draw_rounded_rect({bar_x, bar_y, bar_w, bar_h}, 2, 0x80FFFFFF);
    }

    ctx.pop_clip();
}

} // namespace winios::runtime::ui

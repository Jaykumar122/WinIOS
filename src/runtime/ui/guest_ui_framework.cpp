#include "runtime/ui/guest_ui_framework.h"
#include "runtime/ui/font_engine.h"
#include <algorithm>

namespace winios::runtime::ui {

GuestWidget::GuestWidget(WidgetType type) : type_(type) {}

void GuestWidget::SetFrame(int32_t x, int32_t y, uint32_t w, uint32_t h) {
    frame_.x = static_cast<uint32_t>(std::max(0, x));
    frame_.y = static_cast<uint32_t>(std::max(0, y));
    frame_.width = w;
    frame_.height = h;
    Invalidate();
}

bool GuestWidget::HitTest(int32_t x, int32_t y) const {
    if (!is_visible_) return false;
    return (x >= static_cast<int32_t>(frame_.x) &&
            x < static_cast<int32_t>(frame_.x + frame_.width) &&
            y >= static_cast<int32_t>(frame_.y) &&
            y < static_cast<int32_t>(frame_.y + frame_.height));
}

void GuestWidget::Render(uint32_t* surface_pixels, uint32_t surface_w, uint32_t surface_h) {
    if (!is_visible_ || !surface_pixels) return;
    for (uint32_t y = frame_.y; y < frame_.y + frame_.height && y < surface_h; ++y) {
        for (uint32_t x = frame_.x; x < frame_.x + frame_.width && x < surface_w; ++x) {
            surface_pixels[y * surface_w + x] = bg_color_;
        }
    }
    Validate();
}

bool GuestWidget::HandleEvent(const winios_event_t& ev) {
    (void)ev;
    return false;
}

// -----------------------------------------------------------------------------
// GuestLabel
// -----------------------------------------------------------------------------
GuestLabel::GuestLabel(const std::string& text)
    : GuestWidget(WidgetType::kLabel), text_(text) {
    bg_color_ = WINIOS_COLOR_WHITE;
}

void GuestLabel::SetText(const std::string& text) {
    text_ = text;
    Invalidate();
}

void GuestLabel::Render(uint32_t* surface_pixels, uint32_t surface_w, uint32_t surface_h) {
    if (!is_visible_ || !surface_pixels) return;
    GuestWidget::Render(surface_pixels, surface_w, surface_h);
    FontEngine::Get().RenderText(text_,
                                 static_cast<int32_t>(frame_.x + 4),
                                 static_cast<int32_t>(frame_.y + 2),
                                 text_color_,
                                 surface_pixels, surface_w, surface_h,
                                 frame_.x, frame_.y, frame_.width, frame_.height);
    Validate();
}

// -----------------------------------------------------------------------------
// GuestButton
// -----------------------------------------------------------------------------
GuestButton::GuestButton(const std::string& label)
    : GuestWidget(WidgetType::kButton), label_(label) {
    bg_color_ = WINIOS_COLOR_BLUE;
}

void GuestButton::SetLabel(const std::string& label) {
    label_ = label;
    Invalidate();
}

void GuestButton::Render(uint32_t* surface_pixels, uint32_t surface_w, uint32_t surface_h) {
    if (!is_visible_ || !surface_pixels) return;
    uint32_t draw_bg = is_pressed_ ? WINIOS_COLOR_GRAY : bg_color_;
    for (uint32_t y = frame_.y; y < frame_.y + frame_.height && y < surface_h; ++y) {
        for (uint32_t x = frame_.x; x < frame_.x + frame_.width && x < surface_w; ++x) {
            surface_pixels[y * surface_w + x] = draw_bg;
        }
    }
    FontEngine::Get().RenderText(label_,
                                 static_cast<int32_t>(frame_.x + 8),
                                 static_cast<int32_t>(frame_.y + (frame_.height > 16 ? (frame_.height - 16) / 2 : 2)),
                                 WINIOS_COLOR_WHITE,
                                 surface_pixels, surface_w, surface_h,
                                 frame_.x, frame_.y, frame_.width, frame_.height);
    Validate();
}

bool GuestButton::HandleEvent(const winios_event_t& ev) {
    if (!is_visible_ || !is_enabled_) return false;
    if (ev.type == WINIOS_EVENT_TOUCH_BEGIN) {
        if (HitTest(ev.x, ev.y)) {
            is_pressed_ = true;
            Invalidate();
            return true;
        }
    } else if (ev.type == WINIOS_EVENT_TOUCH_END) {
        if (is_pressed_) {
            is_pressed_ = false;
            Invalidate();
            if (HitTest(ev.x, ev.y) && on_click_) {
                on_click_();
            }
            return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// GuestTextInput
// -----------------------------------------------------------------------------
GuestTextInput::GuestTextInput(const std::string& placeholder)
    : GuestWidget(WidgetType::kTextInput), placeholder_(placeholder) {
    bg_color_ = WINIOS_COLOR_WHITE;
}

void GuestTextInput::SetText(const std::string& text) {
    text_ = text;
    Invalidate();
}

void GuestTextInput::Render(uint32_t* surface_pixels, uint32_t surface_w, uint32_t surface_h) {
    if (!is_visible_ || !surface_pixels) return;
    GuestWidget::Render(surface_pixels, surface_w, surface_h);

    const std::string& display = text_.empty() ? placeholder_ : text_;
    uint32_t color = text_.empty() ? WINIOS_COLOR_GRAY : WINIOS_COLOR_BLACK;

    FontEngine::Get().RenderText(display,
                                 static_cast<int32_t>(frame_.x + 4),
                                 static_cast<int32_t>(frame_.y + 4),
                                 color,
                                 surface_pixels, surface_w, surface_h,
                                 frame_.x, frame_.y, frame_.width, frame_.height);
    Validate();
}

bool GuestTextInput::HandleEvent(const winios_event_t& ev) {
    if (!is_visible_ || !is_enabled_) return false;
    if (ev.type == WINIOS_EVENT_TOUCH_BEGIN) {
        if (HitTest(ev.x, ev.y)) {
            SetFocused(true);
            return true;
        } else {
            SetFocused(false);
        }
    } else if (ev.type == WINIOS_EVENT_KEY_CHAR && is_focused_) {
        if (ev.key_code == 8) { // Backspace
            if (!text_.empty()) {
                text_.pop_back();
                Invalidate();
            }
        } else if (ev.key_code >= 32 && ev.key_code <= 126) {
            text_.push_back(static_cast<char>(ev.key_code));
            Invalidate();
        }
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// GuestImageView
// -----------------------------------------------------------------------------
void GuestImageView::Render(uint32_t* surface_pixels, uint32_t surface_w, uint32_t surface_h) {
    if (!is_visible_ || !surface_pixels || !image_.is_valid) return;
    for (uint32_t y = 0; y < image_.height && (frame_.y + y) < surface_h && y < frame_.height; ++y) {
        for (uint32_t x = 0; x < image_.width && (frame_.x + x) < surface_w && x < frame_.width; ++x) {
            surface_pixels[(frame_.y + y) * surface_w + (frame_.x + x)] = image_.pixels_argb[y * image_.width + x];
        }
    }
    Validate();
}

// -----------------------------------------------------------------------------
// GuestContainer
// -----------------------------------------------------------------------------
void GuestContainer::AddChild(std::shared_ptr<GuestWidget> child) {
    if (child) {
        children_.push_back(std::move(child));
        Invalidate();
    }
}

void GuestContainer::RemoveChild(const std::string& id) {
    for (auto it = children_.begin(); it != children_.end(); ++it) {
        if ((*it)->GetId() == id) {
            children_.erase(it);
            Invalidate();
            break;
        }
    }
}

void GuestContainer::Render(uint32_t* surface_pixels, uint32_t surface_w, uint32_t surface_h) {
    if (!is_visible_ || !surface_pixels) return;
    GuestWidget::Render(surface_pixels, surface_w, surface_h);
    for (auto& child : children_) {
        if (child && child->IsVisible()) {
            child->Render(surface_pixels, surface_w, surface_h);
        }
    }
    Validate();
}

bool GuestContainer::HandleEvent(const winios_event_t& ev) {
    if (!is_visible_ || !is_enabled_) return false;
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if ((*it) && (*it)->IsVisible() && (*it)->HandleEvent(ev)) {
            return true;
        }
    }
    return false;
}

} // namespace winios::runtime::ui

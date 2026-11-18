#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>

#include "winios_sdk/winios_sdk.h"
#include "resources/resource_manager.h"

namespace winios::runtime::ui {

enum class WidgetType : uint32_t {
    kView = 0,
    kContainer = 1,
    kLabel = 2,
    kButton = 3,
    kTextInput = 4,
    kImageView = 5
};

class GuestWidget {
public:
    explicit GuestWidget(WidgetType type);
    virtual ~GuestWidget() = default;

    WidgetType GetType() const { return type_; }
    void SetId(const std::string& id) { id_ = id; }
    const std::string& GetId() const { return id_; }

    void SetFrame(int32_t x, int32_t y, uint32_t w, uint32_t h);
    winios_rect_t GetFrame() const { return frame_; }

    void SetVisible(bool v) { is_visible_ = v; Invalidate(); }
    bool IsVisible() const { return is_visible_; }

    void SetEnabled(bool e) { is_enabled_ = e; Invalidate(); }
    bool IsEnabled() const { return is_enabled_; }

    void SetBackgroundColor(uint32_t color) { bg_color_ = color; Invalidate(); }
    uint32_t GetBackgroundColor() const { return bg_color_; }

    virtual void Render(uint32_t* surface_pixels, uint32_t surface_w, uint32_t surface_h);
    virtual bool HandleEvent(const winios_event_t& ev);

    bool IsInvalidated() const { return is_invalidated_; }
    void Validate() { is_invalidated_ = false; }
    void Invalidate() { is_invalidated_ = true; }

    bool HitTest(int32_t x, int32_t y) const;

protected:
    WidgetType type_;
    std::string id_;
    winios_rect_t frame_{0, 0, 0, 0};
    bool is_visible_{true};
    bool is_enabled_{true};
    bool is_invalidated_{true};
    uint32_t bg_color_{WINIOS_COLOR_WHITE};
};

class GuestLabel : public GuestWidget {
public:
    explicit GuestLabel(const std::string& text = "");
    void SetText(const std::string& text);
    const std::string& GetText() const { return text_; }
    void SetTextColor(uint32_t color) { text_color_ = color; Invalidate(); }

    void Render(uint32_t* surface_pixels, uint32_t surface_w, uint32_t surface_h) override;

private:
    std::string text_;
    uint32_t text_color_{WINIOS_COLOR_BLACK};
};

class GuestButton : public GuestWidget {
public:
    explicit GuestButton(const std::string& label = "");
    void SetLabel(const std::string& label);
    const std::string& GetLabel() const { return label_; }
    void SetOnClick(std::function<void()> cb) { on_click_ = std::move(cb); }

    void Render(uint32_t* surface_pixels, uint32_t surface_w, uint32_t surface_h) override;
    bool HandleEvent(const winios_event_t& ev) override;

private:
    std::string label_;
    bool is_pressed_{false};
    std::function<void()> on_click_;
};

class GuestTextInput : public GuestWidget {
public:
    explicit GuestTextInput(const std::string& placeholder = "");
    void SetText(const std::string& text);
    const std::string& GetText() const { return text_; }
    void SetFocused(bool f) { is_focused_ = f; Invalidate(); }
    bool IsFocused() const { return is_focused_; }

    void Render(uint32_t* surface_pixels, uint32_t surface_w, uint32_t surface_h) override;
    bool HandleEvent(const winios_event_t& ev) override;

private:
    std::string text_;
    std::string placeholder_;
    bool is_focused_{false};
};

class GuestImageView : public GuestWidget {
public:
    GuestImageView() : GuestWidget(WidgetType::kImageView) {}
    void SetImage(const resources::DecodedImage& img) { image_ = img; Invalidate(); }

    void Render(uint32_t* surface_pixels, uint32_t surface_w, uint32_t surface_h) override;

private:
    resources::DecodedImage image_;
};

class GuestContainer : public GuestWidget {
public:
    GuestContainer() : GuestWidget(WidgetType::kContainer) {}
    void AddChild(std::shared_ptr<GuestWidget> child);
    void RemoveChild(const std::string& id);
    const std::vector<std::shared_ptr<GuestWidget>>& GetChildren() const { return children_; }

    void Render(uint32_t* surface_pixels, uint32_t surface_w, uint32_t surface_h) override;
    bool HandleEvent(const winios_event_t& ev) override;

private:
    std::vector<std::shared_ptr<GuestWidget>> children_;
};

} // namespace winios::runtime::ui

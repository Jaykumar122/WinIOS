#include "runtime/ui/view.h"
#include "runtime/ui/view_group.h"

namespace winios::runtime::ui {

View::View(std::uint32_t view_id, std::uint32_t pid)
    : id_(view_id), pid_(pid) {}

void View::set_frame(const Rect& f) noexcept {
    if (frame_.x != f.x || frame_.y != f.y || frame_.width != f.width || frame_.height != f.height) {
        invalidate(); // invalidate old area
        frame_ = f;
        invalidate(); // invalidate new area
        request_layout();
    }
}

void View::set_position(std::int32_t x, std::int32_t y) noexcept {
    set_frame({x, y, frame_.width, frame_.height});
}

void View::set_size(std::int32_t w, std::int32_t h) noexcept {
    set_frame({frame_.x, frame_.y, w, h});
}

Rect View::absolute_frame() const noexcept {
    std::int32_t ax = frame_.x;
    std::int32_t ay = frame_.y;
    const ViewGroup* p = parent_;
    while (p) {
        ax += p->frame().x;
        ay += p->frame().y;
        p = p->parent();
    }
    return {ax, ay, frame_.width, frame_.height};
}

void View::set_visibility(Visibility v) noexcept {
    if (visibility_ != v) {
        visibility_ = v;
        invalidate();
        request_layout();
    }
}

bool View::is_descendant_of(const View* ancestor) const noexcept {
    if (!ancestor) return false;
    const ViewGroup* p = parent_;
    while (p) {
        if (p == ancestor) return true;
        p = p->parent();
    }
    return false;
}

void View::invalidate() {
    if (parent_) {
        parent_->invalidate();
    }
}

void View::request_layout() {
    if (parent_) {
        parent_->request_layout();
    }
}

View* View::hit_test(std::int32_t px, std::int32_t py) {
    if (visibility_ != Visibility::Visible) return nullptr;
    if (frame_.contains(px, py)) {
        return this;
    }
    return nullptr;
}

void View::draw(RenderContext& /*ctx*/) {
    // Default base view has no drawing
}

void View::perform_click() {
    if (is_enabled_ && on_click_) {
        on_click_(*this);
    }
}

} // namespace winios::runtime::ui

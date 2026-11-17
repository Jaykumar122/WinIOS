#include "runtime/ui/view_group.h"
#include <algorithm>

namespace winios::runtime::ui {

ViewGroup::ViewGroup(std::uint32_t view_id, std::uint32_t pid)
    : View(view_id, pid) {}

ViewGroup::~ViewGroup() {
    clear_children();
}

bool ViewGroup::add_child(std::shared_ptr<View> child) {
    if (!child) return false;
    if (child.get() == this) return false; // Cannot add self
    if (this->is_descendant_of(child.get())) return false; // Cycle prevention!

    // If already in another parent, detach
    if (child->parent()) {
        child->parent()->remove_child(child->id());
    }

    child->set_parent(this);
    children_.push_back(child);
    invalidate();
    request_layout();
    return true;
}

bool ViewGroup::remove_child(std::uint32_t view_id) {
    auto it = std::find_if(children_.begin(), children_.end(),
                           [view_id](const std::shared_ptr<View>& c) { return c->id() == view_id; });
    if (it != children_.end()) {
        (*it)->set_parent(nullptr);
        children_.erase(it);
        invalidate();
        request_layout();
        return true;
    }
    return false;
}

void ViewGroup::clear_children() {
    for (auto& c : children_) {
        if (c) {
            c->set_parent(nullptr);
        }
    }
    children_.clear();
    invalidate();
    request_layout();
}

std::shared_ptr<View> ViewGroup::child_at(std::size_t index) const {
    if (index < children_.size()) {
        return children_[index];
    }
    return nullptr;
}

View* ViewGroup::find_view_by_id(std::uint32_t id) {
    if (id_ == id) return this;
    for (auto& c : children_) {
        if (c->id() == id) return c.get();
        auto* vg = dynamic_cast<ViewGroup*>(c.get());
        if (vg) {
            auto* found = vg->find_view_by_id(id);
            if (found) return found;
        }
    }
    return nullptr;
}

View* ViewGroup::find_view_by_tag(const std::string& tag) {
    if (tag_ == tag) return this;
    for (auto& c : children_) {
        if (c->tag() == tag) return c.get();
        auto* vg = dynamic_cast<ViewGroup*>(c.get());
        if (vg) {
            auto* found = vg->find_view_by_tag(tag);
            if (found) return found;
        }
    }
    return nullptr;
}

View* ViewGroup::hit_test(std::int32_t px, std::int32_t py) {
    if (visibility_ != Visibility::Visible || !is_enabled_) return nullptr;
    if (!frame_.contains(px, py)) return nullptr;

    std::int32_t local_x = px - frame_.x;
    std::int32_t local_y = py - frame_.y;

    // Check children in front-to-back order (reverse iteration)
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        auto* target = (*it)->hit_test(local_x, local_y);
        if (target) {
            return target;
        }
    }

    return this; // Hit container itself
}

void ViewGroup::draw(RenderContext& ctx) {
    if (visibility_ != Visibility::Visible) return;

    Rect abs_f = absolute_frame();
    if (clip_children_) {
        ctx.push_clip(abs_f);
    }

    for (auto& child : children_) {
        if (child && child->visibility() != Visibility::Gone) {
            child->draw(ctx);
        }
    }

    if (clip_children_) {
        ctx.pop_clip();
    }
}

} // namespace winios::runtime::ui

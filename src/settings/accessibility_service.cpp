#include "settings/accessibility_service.h"
#include <algorithm>

namespace winios::settings {

AccessibilityService::AccessibilityService(SystemStateManager* state_mgr)
    : state_mgr_(state_mgr) {}

bool AccessibilityService::register_node(const AccessibilityNode& node) {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (node.id == 0) return false;

    for (auto& n : nodes_) {
        if (n.id == node.id) {
            n = node;
            return true;
        }
    }

    nodes_.push_back(node);
    if (nodes_.size() == 1) {
        nodes_[0].is_focused = true;
        focused_index_ = 0;
    }
    return true;
}

bool AccessibilityService::unregister_node(uint64_t id) {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    auto it = std::find_if(nodes_.begin(), nodes_.end(), [id](const AccessibilityNode& n) {
        return n.id == id;
    });

    if (it == nodes_.end()) {
        return false;
    }

    nodes_.erase(it);
    if (focused_index_ >= nodes_.size() && !nodes_.empty()) {
        focused_index_ = nodes_.size() - 1;
        nodes_[focused_index_].is_focused = true;
    }
    return true;
}

void AccessibilityService::clear_nodes() {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    nodes_.clear();
    focused_index_ = 0;
}

bool AccessibilityService::get_node(uint64_t id, AccessibilityNode& out_node) const {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    for (const auto& n : nodes_) {
        if (n.id == id) {
            out_node = n;
            return true;
        }
    }
    return false;
}

std::vector<AccessibilityNode> AccessibilityService::get_all_nodes() const {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    return nodes_;
}

size_t AccessibilityService::get_node_count() const {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    return nodes_.size();
}

bool AccessibilityService::focus_next() {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (nodes_.empty()) return false;

    nodes_[focused_index_].is_focused = false;
    focused_index_ = (focused_index_ + 1) % nodes_.size();
    nodes_[focused_index_].is_focused = true;
    return true;
}

bool AccessibilityService::focus_prev() {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (nodes_.empty()) return false;

    nodes_[focused_index_].is_focused = false;
    if (focused_index_ == 0) {
        focused_index_ = nodes_.size() - 1;
    } else {
        focused_index_--;
    }
    nodes_[focused_index_].is_focused = true;
    return true;
}

bool AccessibilityService::set_focus(uint64_t id) {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    bool found = false;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].id == id) {
            nodes_[i].is_focused = true;
            focused_index_ = i;
            found = true;
        } else {
            nodes_[i].is_focused = false;
        }
    }
    return found;
}

bool AccessibilityService::get_focused_node(AccessibilityNode& out_node) const {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (nodes_.empty() || focused_index_ >= nodes_.size()) {
        return false;
    }
    out_node = nodes_[focused_index_];
    return true;
}

uint64_t AccessibilityService::get_focused_id() const {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (nodes_.empty() || focused_index_ >= nodes_.size()) {
        return 0;
    }
    return nodes_[focused_index_].id;
}

bool AccessibilityService::activate_focused() {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (nodes_.empty() || focused_index_ >= nodes_.size()) {
        return false;
    }

    if (!nodes_[focused_index_].is_enabled) {
        return false;
    }

    if (action_handler_) {
        action_handler_(nodes_[focused_index_].id, "activate");
    }
    return true;
}

uint32_t AccessibilityService::get_text_scale_percent() const {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (state_mgr_) {
        return state_mgr_->get_accessibility().text_scale_percent;
    }
    return 100;
}

float AccessibilityService::get_text_scale_factor() const {
    uint32_t pct = get_text_scale_percent();
    return static_cast<float>(pct) / 100.0f;
}

bool AccessibilityService::is_high_contrast() const {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (state_mgr_) {
        return state_mgr_->get_accessibility().high_contrast;
    }
    return false;
}

bool AccessibilityService::is_reduce_motion() const {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (state_mgr_) {
        return state_mgr_->get_accessibility().reduce_motion;
    }
    return false;
}

bool AccessibilityService::is_focus_indicator_visible() const {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (state_mgr_) {
        return state_mgr_->get_accessibility().focus_indicator_visible;
    }
    return true;
}

bool AccessibilityService::set_text_scale(uint32_t percent) {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (state_mgr_) {
        return state_mgr_->set_text_scale(percent);
    }
    return false;
}

bool AccessibilityService::set_high_contrast(bool enable) {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (state_mgr_) {
        return state_mgr_->set_high_contrast(enable);
    }
    return false;
}

} // namespace winios::settings

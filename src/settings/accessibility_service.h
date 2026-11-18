#pragma once

#include "settings/settings_types.h"
#include "settings/system_state_manager.h"
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace winios::settings {

enum class AccessibilityRole : uint32_t {
    Button     = 0,
    StaticText = 1,
    Slider     = 2,
    Checkbox   = 3,
    Window     = 4,
    Dialog     = 5,
    Menu       = 6,
    ListItem   = 7
};

struct AccessibilityNode {
    uint64_t          id{0};
    AccessibilityRole role{AccessibilityRole::Button};
    std::string       label;
    std::string       value;
    int32_t           x{0};
    int32_t           y{0};
    int32_t           width{0};
    int32_t           height{0};
    bool              is_focused{false};
    bool              is_enabled{true};
};

class AccessibilityService {
public:
    using ActionHandler = std::function<void(uint64_t node_id, const std::string& action)>;

    explicit AccessibilityService(SystemStateManager* state_mgr = nullptr);
    ~AccessibilityService() = default;

    AccessibilityService(const AccessibilityService&) = delete;
    AccessibilityService& operator=(const AccessibilityService&) = delete;

    // Node Tree & Registry
    bool register_node(const AccessibilityNode& node);
    bool unregister_node(uint64_t id);
    void clear_nodes();
    [[nodiscard]] bool get_node(uint64_t id, AccessibilityNode& out_node) const;
    [[nodiscard]] std::vector<AccessibilityNode> get_all_nodes() const;
    [[nodiscard]] size_t get_node_count() const;

    // Focus Navigation
    bool focus_next();
    bool focus_prev();
    bool set_focus(uint64_t id);
    [[nodiscard]] bool get_focused_node(AccessibilityNode& out_node) const;
    [[nodiscard]] uint64_t get_focused_id() const;

    // Actions
    bool activate_focused();
    void set_action_handler(ActionHandler handler) {
        std::lock_guard<std::recursive_mutex> lock(lock_);
        action_handler_ = std::move(handler);
    }

    // Preferences & Queries (delegated to SystemStateManager)
    [[nodiscard]] uint32_t get_text_scale_percent() const;
    [[nodiscard]] float get_text_scale_factor() const;
    [[nodiscard]] bool is_high_contrast() const;
    [[nodiscard]] bool is_reduce_motion() const;
    [[nodiscard]] bool is_focus_indicator_visible() const;

    bool set_text_scale(uint32_t percent);
    bool set_high_contrast(bool enable);

    void set_state_manager(SystemStateManager* sm) noexcept { state_mgr_ = sm; }

private:
    mutable std::recursive_mutex   lock_;
    SystemStateManager*            state_mgr_{nullptr};
    std::vector<AccessibilityNode> nodes_;
    size_t                         focused_index_{0};
    ActionHandler                  action_handler_;
};

} // namespace winios::settings

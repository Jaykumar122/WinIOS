#include "runtime/ui/theme.h"

namespace winios::runtime::ui {

ThemeManager& ThemeManager::instance() noexcept {
    static ThemeManager s_instance;
    return s_instance;
}

ThemeManager::ThemeManager() {
    set_dark_mode(true);
}

void ThemeManager::set_dark_mode(bool dark) noexcept {
    is_dark_ = dark;
    if (dark) {
        palette_.primary = 0xFF007AFF;
        palette_.secondary = 0xFF5856D6;
        palette_.success = 0xFF34C759;
        palette_.warning = 0xFFFF9500;
        palette_.danger = 0xFFFF3B30;
        palette_.background = 0xFF000000;
        palette_.surface = 0xFF1C1C1E;
        palette_.text_primary = 0xFFFFFFFF;
        palette_.text_secondary = 0xFF8E8E93;
        palette_.border = 0xFF38383A;
        palette_.control_bg = 0xFF2C2C2E;
    } else {
        palette_.primary = 0xFF007AFF;
        palette_.secondary = 0xFF5856D6;
        palette_.success = 0xFF34C759;
        palette_.warning = 0xFFFF9500;
        palette_.danger = 0xFFFF3B30;
        palette_.background = 0xFFFFFFFF;
        palette_.surface = 0xFFF2F2F7;
        palette_.text_primary = 0xFF000000;
        palette_.text_secondary = 0xFF8E8E93;
        palette_.border = 0xFFC6C6C8;
        palette_.control_bg = 0xFFE5E5EA;
    }
}

void ThemeManager::track_resource(std::uint32_t pid, std::uint32_t resource_id) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    pid_resources_[pid].insert(resource_id);
}

void ThemeManager::release_resources_for_pid(std::uint32_t pid) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    pid_resources_.erase(pid);
}

std::size_t ThemeManager::active_resource_count_for_pid(std::uint32_t pid) const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    auto it = pid_resources_.find(pid);
    if (it != pid_resources_.end()) {
        return it->second.size();
    }
    return 0;
}

} // namespace winios::runtime::ui

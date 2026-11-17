#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

namespace winios::runtime::ui {

struct ColorPalette {
    std::uint32_t primary{0xFF007AFF};       // iOS Blue
    std::uint32_t secondary{0xFF5856D6};     // iOS Purple
    std::uint32_t success{0xFF34C759};       // iOS Green
    std::uint32_t warning{0xFFFF9500};       // iOS Orange
    std::uint32_t danger{0xFFFF3B30};        // iOS Red
    std::uint32_t background{0xFF000000};    // Dark background
    std::uint32_t surface{0xFF1C1C1E};       // Dark card / surface
    std::uint32_t text_primary{0xFFFFFFFF};  // White text
    std::uint32_t text_secondary{0xFF8E8E93};// Gray text
    std::uint32_t border{0xFF38383A};        // Border line
    std::uint32_t control_bg{0xFF2C2C2E};    // Control fill
};

struct UiMetrics {
    std::int32_t spacing_xs{4};
    std::int32_t spacing_sm{8};
    std::int32_t spacing_md{16};
    std::int32_t spacing_lg{24};
    std::int32_t corner_radius{10};
    std::int32_t button_height{44};
    std::int32_t toggle_width{51};
    std::int32_t toggle_height{31};
    std::int32_t checkbox_size{24};
    std::uint32_t default_font_scale{1};
};

class ThemeManager {
public:
    static ThemeManager& instance() noexcept;

    void set_dark_mode(bool dark) noexcept;
    [[nodiscard]] bool is_dark_mode() const noexcept { return is_dark_; }

    [[nodiscard]] const ColorPalette& palette() const noexcept { return palette_; }
    void set_palette(const ColorPalette& p) noexcept { palette_ = p; }

    [[nodiscard]] const UiMetrics& metrics() const noexcept { return metrics_; }
    void set_metrics(const UiMetrics& m) noexcept { metrics_ = m; }

    // Per-process resource accounting
    void track_resource(std::uint32_t pid, std::uint32_t resource_id);
    void release_resources_for_pid(std::uint32_t pid);
    [[nodiscard]] std::size_t active_resource_count_for_pid(std::uint32_t pid) const;

private:
    ThemeManager();

    bool is_dark_{true};
    ColorPalette palette_{};
    UiMetrics metrics_{};

    mutable std::mutex resource_mutex_;
    std::unordered_map<std::uint32_t, std::unordered_set<std::uint32_t>> pid_resources_;
};

} // namespace winios::runtime::ui

#include "input/coordinate_normalizer.h"

#include <algorithm>

namespace winios::input {

NormalizedPoint CoordinateNormalizer::normalize(
    std::int32_t client_x,
    std::int32_t client_y,
    std::uint32_t client_width,
    std::uint32_t client_height,
    std::uint32_t panel_width,
    std::uint32_t panel_height,
    ScalingMode mode) noexcept {

    NormalizedPoint pt{};
    if (client_width == 0 || client_height == 0 || panel_width == 0 || panel_height == 0) {
        pt.in_bounds = false;
        return pt;
    }

    if (mode == ScalingMode::Stretch) {
        const double sx = static_cast<double>(client_x) * static_cast<double>(panel_width) / static_cast<double>(client_width);
        const double sy = static_cast<double>(client_y) * static_cast<double>(panel_height) / static_cast<double>(client_height);

        pt.panel_x = std::clamp(static_cast<std::int32_t>(sx), 0, static_cast<std::int32_t>(panel_width - 1));
        pt.panel_y = std::clamp(static_cast<std::int32_t>(sy), 0, static_cast<std::int32_t>(panel_height - 1));
        pt.in_bounds = (client_x >= 0 && client_x < static_cast<std::int32_t>(client_width) &&
                        client_y >= 0 && client_y < static_cast<std::int32_t>(client_height));
        return pt;
    }

    // PreserveAspectRatio (Fit with Letterbox or Pillarbox)
    const double panel_ratio = static_cast<double>(panel_width) / static_cast<double>(panel_height);
    const double client_ratio = static_cast<double>(client_width) / static_cast<double>(client_height);

    double render_w = 0.0;
    double render_h = 0.0;
    double offset_x = 0.0;
    double offset_y = 0.0;

    if (client_ratio > panel_ratio) {
        // Window is wider than panel -> Pillarbox (bars on left and right)
        render_h = static_cast<double>(client_height);
        render_w = render_h * panel_ratio;
        offset_x = (static_cast<double>(client_width) - render_w) / 2.0;
        offset_y = 0.0;
    } else {
        // Window is taller than panel -> Letterbox (bars on top and bottom)
        render_w = static_cast<double>(client_width);
        render_h = render_w / panel_ratio;
        offset_x = 0.0;
        offset_y = (static_cast<double>(client_height) - render_h) / 2.0;
    }

    const double active_x = static_cast<double>(client_x) - offset_x;
    const double active_y = static_cast<double>(client_y) - offset_y;

    if (active_x < 0.0 || active_x >= render_w || active_y < 0.0 || active_y >= render_h) {
        pt.in_bounds = false;
    } else {
        pt.in_bounds = true;
    }

    const double norm_x = active_x * static_cast<double>(panel_width) / render_w;
    const double norm_y = active_y * static_cast<double>(panel_height) / render_h;

    pt.panel_x = std::clamp(static_cast<std::int32_t>(norm_x), 0, static_cast<std::int32_t>(panel_width - 1));
    pt.panel_y = std::clamp(static_cast<std::int32_t>(norm_y), 0, static_cast<std::int32_t>(panel_height - 1));

    return pt;
}

} // namespace winios::input

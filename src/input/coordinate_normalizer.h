#pragma once

#include <cstdint>

namespace winios::input {

enum class ScalingMode : std::uint8_t {
    Stretch = 0,
    PreserveAspectRatio = 1
};

struct NormalizedPoint {
    std::int32_t panel_x{0};
    std::int32_t panel_y{0};
    bool in_bounds{true};
};

class CoordinateNormalizer {
public:
    static NormalizedPoint normalize(
        std::int32_t client_x,
        std::int32_t client_y,
        std::uint32_t client_width,
        std::uint32_t client_height,
        std::uint32_t panel_width,
        std::uint32_t panel_height,
        ScalingMode mode = ScalingMode::PreserveAspectRatio) noexcept;
};

} // namespace winios::input

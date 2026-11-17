#pragma once

#include <cstdint>
#include <algorithm>

namespace winios::runtime::ui {

struct Point {
    std::int32_t x{0};
    std::int32_t y{0};
};

struct Size {
    std::int32_t width{0};
    std::int32_t height{0};
};

struct Rect {
    std::int32_t x{0};
    std::int32_t y{0};
    std::int32_t width{0};
    std::int32_t height{0};

    [[nodiscard]] constexpr bool contains(std::int32_t px, std::int32_t py) const noexcept {
        return px >= x && px < (x + width) && py >= y && py < (y + height);
    }

    [[nodiscard]] constexpr bool intersects(const Rect& o) const noexcept {
        return !(x + width <= o.x || o.x + o.width <= x ||
                 y + height <= o.y || o.y + o.height <= y);
    }

    [[nodiscard]] Rect intersect_with(const Rect& o) const noexcept {
        std::int32_t nx = std::max(x, o.x);
        std::int32_t ny = std::max(y, o.y);
        std::int32_t nr = std::min(x + width, o.x + o.width);
        std::int32_t nb = std::min(y + height, o.y + o.height);
        if (nr < nx || nb < ny) {
            return {0, 0, 0, 0};
        }
        return {nx, ny, nr - nx, nb - ny};
    }

    [[nodiscard]] Rect union_with(const Rect& o) const noexcept {
        if (width <= 0 || height <= 0) return o;
        if (o.width <= 0 || o.height <= 0) return *this;
        std::int32_t nx = std::min(x, o.x);
        std::int32_t ny = std::min(y, o.y);
        std::int32_t nr = std::max(x + width, o.x + o.width);
        std::int32_t nb = std::max(y + height, o.y + o.height);
        return {nx, ny, nr - nx, nb - ny};
    }
};

struct EdgeInsets {
    std::int32_t left{0};
    std::int32_t top{0};
    std::int32_t right{0};
    std::int32_t bottom{0};
};

enum class Visibility : std::uint8_t {
    Visible,
    Invisible, // Retains space in layout but not drawn
    Gone       // Excluded from layout and drawing
};

enum class Alignment : std::uint8_t {
    Start,
    Center,
    End,
    Stretch
};

enum class DimensionMode : std::uint8_t {
    Fixed,
    WrapContent,
    MatchParent
};

struct LayoutDimension {
    DimensionMode mode{DimensionMode::WrapContent};
    std::int32_t fixed_value{0};

    static LayoutDimension make_fixed(std::int32_t val) noexcept {
        return {DimensionMode::Fixed, std::max(0, val)};
    }
    static LayoutDimension make_wrap() noexcept {
        return {DimensionMode::WrapContent, 0};
    }
    static LayoutDimension make_match_parent() noexcept {
        return {DimensionMode::MatchParent, 0};
    }
};

struct LayoutParams {
    LayoutDimension width{LayoutDimension::make_wrap()};
    LayoutDimension height{LayoutDimension::make_wrap()};
    EdgeInsets margin{};
    EdgeInsets padding{};
    Alignment horizontal_alignment{Alignment::Start};
    Alignment vertical_alignment{Alignment::Start};
    std::int32_t min_width{0};
    std::int32_t min_height{0};
    std::int32_t max_width{100000};
    std::int32_t max_height{100000};
};

} // namespace winios::runtime::ui

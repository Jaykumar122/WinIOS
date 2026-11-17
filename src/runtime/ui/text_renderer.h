#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "runtime/ui/ui_types.h"
#include "runtime/ui/render_context.h"

namespace winios::runtime::ui {

enum class TextAlignment : std::uint8_t {
    Left,
    Center,
    Right
};

struct TextMetrics {
    std::int32_t glyph_width{8};
    std::int32_t glyph_height{8};
    std::int32_t baseline{7};
    std::int32_t line_spacing{2};
};

class TextRenderer {
public:
    static TextMetrics metrics(std::uint32_t scale = 1) noexcept;

    static Size measure_text(const std::string& text, std::uint32_t scale = 1) noexcept;

    static std::vector<std::string> wrap_text(const std::string& text, std::int32_t max_width, std::uint32_t scale = 1);

    static void render_text(
        RenderContext& ctx,
        const Rect& bounds,
        const std::string& text,
        std::uint32_t color_bgra,
        std::uint32_t scale = 1,
        TextAlignment align = TextAlignment::Left,
        bool word_wrap = true);
};

} // namespace winios::runtime::ui

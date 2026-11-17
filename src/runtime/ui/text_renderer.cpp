#include "runtime/ui/text_renderer.h"

#include <sstream>

namespace winios::runtime::ui {

TextMetrics TextRenderer::metrics(std::uint32_t scale) noexcept {
    if (scale == 0) scale = 1;
    return TextMetrics{
        static_cast<std::int32_t>(8 * scale),
        static_cast<std::int32_t>(8 * scale),
        static_cast<std::int32_t>(7 * scale),
        static_cast<std::int32_t>(2 * scale)
    };
}

Size TextRenderer::measure_text(const std::string& text, std::uint32_t scale) noexcept {
    if (scale == 0) scale = 1;
    auto m = metrics(scale);
    std::int32_t max_line_w = 0;
    std::int32_t cur_line_w = 0;
    std::int32_t line_count = 1;

    for (char c : text) {
        if (c == '\n') {
            max_line_w = std::max(max_line_w, cur_line_w);
            cur_line_w = 0;
            line_count++;
        } else {
            cur_line_w += m.glyph_width;
        }
    }
    max_line_w = std::max(max_line_w, cur_line_w);

    std::int32_t total_h = line_count * m.glyph_height + (line_count - 1) * m.line_spacing;
    return {max_line_w, total_h};
}

std::vector<std::string> TextRenderer::wrap_text(const std::string& text, std::int32_t max_width, std::uint32_t scale) {
    if (scale == 0) scale = 1;
    auto m = metrics(scale);
    std::vector<std::string> lines;
    if (text.empty() || max_width <= 0) {
        lines.push_back("");
        return lines;
    }

    std::istringstream stream(text);
    std::string paragraph;
    while (std::getline(stream, paragraph, '\n')) {
        std::istringstream words(paragraph);
        std::string word;
        std::string current_line;

        while (words >> word) {
            std::string test_line = current_line.empty() ? word : (current_line + " " + word);
            std::int32_t test_w = static_cast<std::int32_t>(test_line.size()) * m.glyph_width;

            if (test_w <= max_width) {
                current_line = test_line;
            } else {
                if (!current_line.empty()) {
                    lines.push_back(current_line);
                }
                // If single word exceeds max_width, break it character by character
                if (static_cast<std::int32_t>(word.size()) * m.glyph_width > max_width) {
                    std::string chunk;
                    for (char c : word) {
                        if (static_cast<std::int32_t>(chunk.size() + 1) * m.glyph_width <= max_width) {
                            chunk.push_back(c);
                        } else {
                            lines.push_back(chunk);
                            chunk = std::string(1, c);
                        }
                    }
                    current_line = chunk;
                } else {
                    current_line = word;
                }
            }
        }
        lines.push_back(current_line);
    }

    if (lines.empty()) {
        lines.push_back("");
    }
    return lines;
}

void TextRenderer::render_text(
    RenderContext& ctx,
    const Rect& bounds,
    const std::string& text,
    std::uint32_t color_bgra,
    std::uint32_t scale,
    TextAlignment align,
    bool word_wrap) {

    if (text.empty() || bounds.width <= 0 || bounds.height <= 0) return;
    if (scale == 0) scale = 1;

    ctx.push_clip(bounds);
    auto m = metrics(scale);

    std::vector<std::string> lines;
    if (word_wrap) {
        lines = wrap_text(text, bounds.width, scale);
    } else {
        std::istringstream ss(text);
        std::string l;
        while (std::getline(ss, l, '\n')) {
            lines.push_back(l);
        }
    }

    std::int32_t line_height = m.glyph_height + m.line_spacing;
    std::int32_t cur_y = bounds.y;

    for (const auto& line : lines) {
        if (cur_y + m.glyph_height > bounds.y + bounds.height) break; // Exceeded bounds

        std::int32_t line_w = static_cast<std::int32_t>(line.size()) * m.glyph_width;
        std::int32_t cur_x = bounds.x;

        if (align == TextAlignment::Center) {
            cur_x = bounds.x + std::max(0, (bounds.width - line_w) / 2);
        } else if (align == TextAlignment::Right) {
            cur_x = bounds.x + std::max(0, bounds.width - line_w);
        }

        ctx.draw_string(cur_x, cur_y, line.c_str(), color_bgra, scale);
        cur_y += line_height;
    }

    ctx.pop_clip();
}

} // namespace winios::runtime::ui

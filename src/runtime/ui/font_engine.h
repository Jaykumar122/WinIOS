#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace winios::runtime::ui {

struct TextMetrics {
    uint32_t width{0};
    uint32_t height{0};
    uint32_t line_height{16};
    uint32_t baseline{13};
};

class FontEngine {
public:
    static FontEngine& Get();

    TextMetrics MeasureText(const std::string& text, float font_size = 14.0f) const;

    void RenderText(const std::string& text,
                    int32_t x, int32_t y,
                    uint32_t color,
                    uint32_t* surface_pixels,
                    uint32_t surface_width,
                    uint32_t surface_height,
                    int32_t clip_x = 0,
                    int32_t clip_y = 0,
                    int32_t clip_w = -1,
                    int32_t clip_h = -1) const;

private:
    FontEngine();
    ~FontEngine() = default;

    // Built-in 8x16 ASCII glyph table (128 characters)
    uint8_t glyph_table_[128][16];
};

} // namespace winios::runtime::ui

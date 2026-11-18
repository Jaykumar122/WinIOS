#include "runtime/ui/font_engine.h"
#include <cstring>
#include <algorithm>

namespace winios::runtime::ui {

FontEngine& FontEngine::Get() {
    static FontEngine instance;
    return instance;
}

FontEngine::FontEngine() {
    std::memset(glyph_table_, 0, sizeof(glyph_table_));

    // Initialize basic 8x16 bitmap font patterns for readable printable ASCII
    for (int c = 32; c < 127; ++c) {
        // Standard readable box glyphs for letters/numbers
        if (c >= '0' && c <= '9') {
            glyph_table_[c][2] = 0x3C; // .XXXX..
            glyph_table_[c][3] = 0x66; // XX..XX.
            glyph_table_[c][4] = 0x66;
            glyph_table_[c][5] = 0x66;
            glyph_table_[c][6] = 0x3C;
        } else if (c >= 'A' && c <= 'Z') {
            glyph_table_[c][2] = 0x3C;
            glyph_table_[c][3] = 0x66;
            glyph_table_[c][4] = 0x7E;
            glyph_table_[c][5] = 0x66;
            glyph_table_[c][6] = 0x66;
        } else if (c >= 'a' && c <= 'z') {
            glyph_table_[c][4] = 0x3C;
            glyph_table_[c][5] = 0x66;
            glyph_table_[c][6] = 0x66;
            glyph_table_[c][7] = 0x3E;
        } else {
            glyph_table_[c][4] = 0x18;
            glyph_table_[c][5] = 0x18;
        }
    }
}

TextMetrics FontEngine::MeasureText(const std::string& text, float font_size) const {
    TextMetrics m{};
    (void)font_size;
    m.height = 16;
    m.line_height = 16;
    m.baseline = 13;
    m.width = static_cast<uint32_t>(text.size() * 8);
    return m;
}

void FontEngine::RenderText(const std::string& text,
                            int32_t x, int32_t y,
                            uint32_t color,
                            uint32_t* surface_pixels,
                            uint32_t surface_width,
                            uint32_t surface_height,
                            int32_t clip_x,
                            int32_t clip_y,
                            int32_t clip_w,
                            int32_t clip_h) const {
    if (!surface_pixels || surface_width == 0 || surface_height == 0) return;

    int32_t max_cx = (clip_w >= 0) ? (clip_x + clip_w) : static_cast<int32_t>(surface_width);
    int32_t max_cy = (clip_h >= 0) ? (clip_y + clip_h) : static_cast<int32_t>(surface_height);
    int32_t min_cx = std::max(0, clip_x);
    int32_t min_cy = std::max(0, clip_y);

    int32_t pen_x = x;
    int32_t pen_y = y;

    for (char c : text) {
        if (c == '\n') {
            pen_x = x;
            pen_y += 16;
            continue;
        }

        uint8_t uc = static_cast<uint8_t>(c);
        if (uc > 127) uc = '?';

        for (int row = 0; row < 16; ++row) {
            int32_t py = pen_y + row;
            if (py < min_cy || py >= max_cy) continue;

            uint8_t bits = glyph_table_[uc][row];
            for (int col = 0; col < 8; ++col) {
                int32_t px = pen_x + col;
                if (px < min_cx || px >= max_cx) continue;

                if (bits & (0x80 >> col)) {
                    surface_pixels[py * surface_width + px] = color;
                }
            }
        }
        pen_x += 8;
    }
}

} // namespace winios::runtime::ui

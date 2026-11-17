// ============================================================================
// Stage 214.9: Text Rendering and Font Infrastructure Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <vector>

#include "runtime/ui/render_context.h"
#include "runtime/ui/text_renderer.h"

using namespace winios::runtime::ui;

namespace {

int g_failures = 0;

#define TEXT_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[TEXT_FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [TEXT_OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_text_metrics_and_measurement() {
    std::cout << "[Stage 214.9] Verifying Font Metrics and Measurement...\n";

    auto m1 = TextRenderer::metrics(1);
    TEXT_CHECK(m1.glyph_width == 8 && m1.glyph_height == 8, "Scale 1 metrics: 8x8");
    TEXT_CHECK(m1.baseline == 7 && m1.line_spacing == 2, "Scale 1 baseline 7, spacing 2");

    auto m2 = TextRenderer::metrics(2);
    TEXT_CHECK(m2.glyph_width == 16 && m2.glyph_height == 16, "Scale 2 metrics: 16x16");

    // Measure single line "Hello" (5 chars * 8 = 40w, 8h)
    Size s1 = TextRenderer::measure_text("Hello", 1);
    TEXT_CHECK(s1.width == 40 && s1.height == 8, "Single line measurement: 40x8");

    // Measure two lines "Hello\nWorld" (5 chars * 8 = 40w, 2*8 + 2 = 18h)
    Size s2 = TextRenderer::measure_text("Hello\nWorld", 1);
    TEXT_CHECK(s2.width == 40 && s2.height == 18, "Multi-line measurement: 40x18");
}

void test_word_wrapping() {
    std::cout << "[Stage 214.9] Verifying Word Wrapping...\n";

    // "The quick brown fox" -> 19 chars
    // Max width 80 px = 10 chars per line (10 * 8 = 80)
    // Line 1: "The quick" (9 chars = 72 px)
    // Line 2: "brown fox" (9 chars = 72 px)
    auto lines = TextRenderer::wrap_text("The quick brown fox", 80, 1);
    TEXT_CHECK(lines.size() == 2, "Wrapped into 2 lines");
    TEXT_CHECK(lines[0] == "The quick", "Line 0 == 'The quick'");
    TEXT_CHECK(lines[1] == "brown fox", "Line 1 == 'brown fox'");
}

void test_glyph_rendering_golden() {
    std::cout << "[Stage 214.9] Verifying Glyph Pixel Golden Rendering...\n";

    std::vector<std::uint8_t> buffer(32 * 32 * 4, 0); // 32x32 black surface
    RenderContext ctx(buffer.data(), 32, 32, 32 * 4);

    // Render character 'A' at (0, 0) in white (0xFFFFFFFF)
    ctx.draw_char(0, 0, 'A', 0xFFFFFFFF, 1);

    auto* pixels = reinterpret_cast<const std::uint32_t*>(buffer.data());

    // 'A' glyph in kFont8x8:
    // row 0: 0x18 -> ..XX.... -> cols 3, 4 are white
    // row 3: 0x7E -> .XXXXXX. -> cols 1..6 are white
    TEXT_CHECK(pixels[0 * 32 + 3] == 0xFFFFFFFF, "Row 0, col 3 is white");
    TEXT_CHECK(pixels[0 * 32 + 4] == 0xFFFFFFFF, "Row 0, col 4 is white");
    TEXT_CHECK(pixels[0 * 32 + 0] == 0, "Row 0, col 0 is background black");

    TEXT_CHECK(pixels[3 * 32 + 1] == 0xFFFFFFFF, "Row 3, col 1 is white (horizontal bar)");
    TEXT_CHECK(pixels[3 * 32 + 6] == 0xFFFFFFFF, "Row 3, col 6 is white (horizontal bar)");
    TEXT_CHECK(pixels[3 * 32 + 0] == 0, "Row 3, col 0 is black");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 214.9: Text Rendering Tests           \n";
    std::cout << "====================================================\n";

    test_text_metrics_and_measurement();
    test_word_wrapping();
    test_glyph_rendering_golden();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL TEXT TESTS PASSED" : "TEXT FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

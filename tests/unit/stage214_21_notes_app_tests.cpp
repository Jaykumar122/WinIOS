// ============================================================================
// Stage 214.21: Interactive Notes Application Migration Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <vector>

#include "runtime/ui/notes_app_view.h"
#include "runtime/ui/render_context.h"
#include "loader/linux_image_loader.h"
#include "kernel/app_preferences.h"

using namespace winios::runtime::ui;
using namespace winios::loader;
using namespace winios::kernel;

namespace {

int g_failures = 0;

#define NOTES_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[NOTES_FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [NOTES_OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_notes_app_migration() {
    std::cout << "[Stage 214.21] Verifying Migrated Notes Application...\n";

    winios::kernel::AppPreferencesManager::instance().reset();

    const std::uint32_t w = 700;
    const std::uint32_t h = 900;

    // 1. Initial Launch
    NotesAppView notes_view(50, 3);
    NOTES_CHECK(notes_view.is_item_checked(0), "Item 0 starts checked");
    NOTES_CHECK(!notes_view.is_item_checked(3), "Item 3 starts unchecked");

    std::vector<std::uint8_t> buf0(w * h * 4, 0);
    RenderContext ctx0(buf0.data(), w, h, w * 4);
    notes_view.render_to_surface(ctx0);
    std::string hash0 = LinuxImageLoader::compute_sha256(buf0.data(), buf0.size());
    NOTES_CHECK(!hash0.empty(), "Computed initial notes render hash");

    // 2. Toggle Item 3 (from unchecked to checked)
    notes_view.toggle_item(3);
    NOTES_CHECK(notes_view.is_item_checked(3), "Item 3 is now checked");

    auto saved_item3 = winios::kernel::AppPreferencesManager::instance().get_bool("com.winios.notes", "item_3");
    NOTES_CHECK(saved_item3.has_value() && *saved_item3 == true, "Persisted item_3 == true");

    // Re-render
    std::vector<std::uint8_t> buf1(w * h * 4, 0);
    RenderContext ctx1(buf1.data(), w, h, w * 4);
    notes_view.render_to_surface(ctx1);
    std::string hash1 = LinuxImageLoader::compute_sha256(buf1.data(), buf1.size());
    NOTES_CHECK(hash0 != hash1, "CRITICAL: Checkbox toggle produced distinct framebuffer hash (hash0 != hash1)");

    std::uint32_t diff_pixels = 0;
    const auto* p0 = reinterpret_cast<const std::uint32_t*>(buf0.data());
    const auto* p1 = reinterpret_cast<const std::uint32_t*>(buf1.data());
    for (std::size_t i = 0; i < w * h; ++i) {
        if (p0[i] != p1[i]) {
            ++diff_pixels;
        }
    }
    NOTES_CHECK(diff_pixels > 0, "Proved non-zero pixel mutations on checkbox toggle");
    std::cout << "  [INFO] Mutated pixels on checkbox toggle: " << diff_pixels << " pixels\n";

    // 3. Scroll content
    notes_view.scroll_view()->scroll_by(60);
    std::vector<std::uint8_t> buf_scroll(w * h * 4, 0);
    RenderContext ctx_scroll(buf_scroll.data(), w, h, w * 4);
    notes_view.render_to_surface(ctx_scroll);
    std::string hash_scroll = LinuxImageLoader::compute_sha256(buf_scroll.data(), buf_scroll.size());
    NOTES_CHECK(hash_scroll != hash1, "Scrolling content produced distinct framebuffer hash");

    // 4. Close Notes
    bool closed = false;
    notes_view.set_on_close([&]() {
        closed = true;
    });
    notes_view.close_button()->perform_click();
    NOTES_CHECK(closed, "Close notes button fired callback");

    // 5. Relaunch Notes Application: Verify persistence
    NotesAppView relaunched_notes(50, 3);
    NOTES_CHECK(relaunched_notes.is_item_checked(3),
                "Relaunched notes restored checked state for Item 3 from preferences");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 214.21: Notes App Migration Tests     \n";
    std::cout << "====================================================\n";

    test_notes_app_migration();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL NOTES APP TESTS PASSED" : "NOTES APP FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

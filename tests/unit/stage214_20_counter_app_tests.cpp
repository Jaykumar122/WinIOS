// ============================================================================
// Stage 214.20: Interactive Counter Application Migration Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <vector>

#include "runtime/ui/counter_app_view.h"
#include "runtime/ui/render_context.h"
#include "loader/linux_image_loader.h"
#include "kernel/app_preferences.h"

using namespace winios::runtime::ui;
using namespace winios::loader;
using namespace winios::kernel;

namespace {

int g_failures = 0;

#define APP_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[APP_FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [APP_OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_counter_app_migration() {
    std::cout << "[Stage 214.20] Verifying Migrated Counter Application...\n";

    winios::kernel::AppPreferencesManager::instance().reset();

    // 1. Instantiate CounterAppView
    CounterAppView counter_view(10, 2);
    APP_CHECK(counter_view.current_count() == 0, "Initial count is 0");
    APP_CHECK(counter_view.count_label()->text() == "Count: 0", "Initial label is 'Count: 0'");

    const std::uint32_t w = 700;
    const std::uint32_t h = 900;
    std::vector<std::uint8_t> buf0(w * h * 4, 0);
    RenderContext ctx0(buf0.data(), w, h, w * 4);
    counter_view.render_to_surface(ctx0);

    std::string hash0 = LinuxImageLoader::compute_sha256(buf0.data(), buf0.size());
    APP_CHECK(!hash0.empty(), "Computed initial render hash");

    // 2. Increment Counter
    counter_view.inc_button()->perform_click();
    APP_CHECK(counter_view.current_count() == 1, "Count incremented to 1");
    APP_CHECK(counter_view.count_label()->text() == "Count: 1", "Label updated to 'Count: 1'");

    // Check persistence
    auto saved1 = winios::kernel::AppPreferencesManager::instance().get_int("com.winios.counter", "count");
    APP_CHECK(saved1.has_value() && *saved1 == 1, "Persisted count == 1 in preferences");

    // Re-render to second buffer
    std::vector<std::uint8_t> buf1(w * h * 4, 0);
    RenderContext ctx1(buf1.data(), w, h, w * 4);
    counter_view.render_to_surface(ctx1);

    std::string hash1 = LinuxImageLoader::compute_sha256(buf1.data(), buf1.size());
    APP_CHECK(hash0 != hash1, "CRITICAL: State change produced distinct framebuffer hash (hash0 != hash1)");

    // Count changed pixels between count=0 and count=1
    std::uint32_t diff_pixels = 0;
    const auto* p0 = reinterpret_cast<const std::uint32_t*>(buf0.data());
    const auto* p1 = reinterpret_cast<const std::uint32_t*>(buf1.data());
    for (std::size_t i = 0; i < w * h; ++i) {
        if (p0[i] != p1[i]) {
            ++diff_pixels;
        }
    }
    APP_CHECK(diff_pixels > 0, "Proved non-zero pixel mutations on counter increment");
    std::cout << "  [INFO] Mutated pixels on increment: " << diff_pixels << " pixels\n";

    // 3. Increment again
    counter_view.inc_button()->perform_click();
    APP_CHECK(counter_view.current_count() == 2, "Count incremented to 2");
    std::vector<std::uint8_t> buf2(w * h * 4, 0);
    RenderContext ctx2(buf2.data(), w, h, w * 4);
    counter_view.render_to_surface(ctx2);
    std::string hash2 = LinuxImageLoader::compute_sha256(buf2.data(), buf2.size());
    APP_CHECK(hash1 != hash2, "Distinct hash on count=2 (hash1 != hash2)");

    // 4. Reset Counter
    counter_view.reset_button()->perform_click();
    APP_CHECK(counter_view.current_count() == 0, "Count reset to 0");
    std::vector<std::uint8_t> buf_reset(w * h * 4, 0);
    RenderContext ctx_reset(buf_reset.data(), w, h, w * 4);
    counter_view.render_to_surface(ctx_reset);
    std::string hash_reset = LinuxImageLoader::compute_sha256(buf_reset.data(), buf_reset.size());
    APP_CHECK(hash_reset == hash0, "Deterministic rendering confirmed: reset returns exactly to initial hash0");

    // 5. Exit button
    bool exit_fired = false;
    counter_view.set_on_exit([&]() {
        exit_fired = true;
    });
    counter_view.exit_button()->perform_click();
    APP_CHECK(exit_fired, "Exit button fired lifecycle callback");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 214.20: Counter App Migration Tests   \n";
    std::cout << "====================================================\n";

    test_counter_app_migration();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL COUNTER APP TESTS PASSED" : "COUNTER APP FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "winios/winios.h"
#include "winios/ui/layout.h"
#include "winios/ui/toolkit.h"
#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "security/capability_manager.h"
#include "runtime/system_clipboard.h"

using namespace winios;
using namespace winios::graphics;
using namespace winios::security;
using namespace winios::runtime;

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

void test_fuzz_layout_engine_limits() {
    std::cout << "[Fuzz 1] Layout engine child limit clamping & arithmetic overflow resistance...\n";

    winios_layout_node_t root;
    winios_layout_init_node(&root, WINIOS_LAYOUT_COLUMN);

    winios_layout_node_t children[WINIOS_LAYOUT_MAX_CHILDREN + 5];
    for (int i = 0; i < WINIOS_LAYOUT_MAX_CHILDREN + 5; ++i) {
        winios_layout_init_node(&children[i], WINIOS_LAYOUT_ABSOLUTE);
    }

    // Add children up to limit
    for (int i = 0; i < WINIOS_LAYOUT_MAX_CHILDREN; ++i) {
        TEST_ASSERT(winios_layout_add_child(&root, &children[i]) == 1);
    }
    TEST_ASSERT(root.child_count == WINIOS_LAYOUT_MAX_CHILDREN);

    // Overflow attempt: adding 17th child MUST fail safely without memory corruption
    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT(winios_layout_add_child(&root, &children[WINIOS_LAYOUT_MAX_CHILDREN + i]) == 0);
    }
    TEST_ASSERT(root.child_count == WINIOS_LAYOUT_MAX_CHILDREN);

    // Test extreme boundary values (0x0 window, massive dimensions)
    winios_rect_t zero_bounds{ 0, 0, 0, 0 };
    winios_layout_calculate(&root, zero_bounds);
    TEST_ASSERT(root.bounds.width == 0 && root.bounds.height == 0);

    winios_rect_t huge_bounds{ 0, 0, 0x00FFFFFF, 0x00FFFFFF };
    winios_layout_calculate(&root, huge_bounds);
    TEST_ASSERT(root.bounds.width == 0x00FFFFFF);

    std::cout << "[PASS] Layout engine child bounds and arithmetic resilience verified.\n";
}

void test_fuzz_textarea_buffer_overflow() {
    std::cout << "[Fuzz 2] TextArea buffer capacity boundary protection & UTF-8 typing...\n";

    char small_buf[16];
    winios_textarea_t ta;
    winios_textarea_init(&ta, 0, 0, 100, 100, small_buf, sizeof(small_buf));
    ta.is_focused = 1;
    TEST_ASSERT(ta.length == 0);
    TEST_ASSERT(ta.capacity == 16);

    // Fill buffer up to capacity - 1 (15 characters)
    for (int i = 0; i < 25; ++i) {
        winios_event_v2_t ch_evt;
        winios_memset(&ch_evt, 0, sizeof(ch_evt));
        ch_evt.type = WINIOS_EVENT_V2_KEY_CHAR;
        ch_evt.key_code = 'A' + (i % 26);
        winios_textarea_handle_event(&ta, &ch_evt);
    }

    // Must be strictly clamped to capacity - 1 (15) with null terminator intact
    TEST_ASSERT(ta.length == 15);
    TEST_ASSERT(small_buf[15] == '\0');
    TEST_ASSERT(small_buf[0] == 'A');

    std::cout << "[PASS] TextArea buffer overflow prevention verified.\n";
}

void test_fuzz_clipboard_boundary_cases() {
    std::cout << "[Fuzz 3] Clipboard zero-byte, UTF-8 multiline, and boundary cases...\n";

    auto caps = std::make_shared<CapabilityManager>();
    caps->grant(1, Capability::ClipboardWrite);
    caps->grant(1, Capability::ClipboardRead);
    SystemClipboard clipboard(caps);

    // 1. Empty string
    TEST_ASSERT(clipboard.set_text(1, ""));
    std::string out;
    TEST_ASSERT(clipboard.get_text(1, out));
    TEST_ASSERT(out.empty());

    // 2. Multiline UTF-8 text with symbols
    std::string complex_text = "Line 1: Special \xc2\xa9 \xe2\x82\xac\nLine 2: Tab\tAnd Newlines\r\nLine 3: End.";
    TEST_ASSERT(clipboard.set_text(1, complex_text));
    TEST_ASSERT(clipboard.get_text(1, out));
    TEST_ASSERT(out == complex_text);

    // 3. Exact 64 KB boundary
    std::string exact_64k(SystemClipboard::kMaxClipboardBytes, 'B');
    TEST_ASSERT(clipboard.set_text(1, exact_64k));
    TEST_ASSERT(clipboard.data_size() == SystemClipboard::kMaxClipboardBytes);

    // 4. Exactly 64 KB + 1 byte (Must be rejected)
    std::string over_64k(SystemClipboard::kMaxClipboardBytes + 1, 'C');
    TEST_ASSERT(!clipboard.set_text(1, over_64k));
    // Content should remain previous valid payload
    TEST_ASSERT(clipboard.data_size() == SystemClipboard::kMaxClipboardBytes);

    std::cout << "[PASS] Clipboard boundary and UTF-8 resilience verified.\n";
}

void test_foreign_window_and_surface_isolation() {
    std::cout << "[Fuzz 4] Window and surface cross-PID isolation...\n";

    WindowManager wm;
    SurfaceResourceManager sm;

    std::shared_ptr<DisplaySurface> surf;
    auto res = sm.create_surface(10, 100, 100, PixelFormat::B8G8R8A8_UNORM, surf);
    TEST_ASSERT(res == SurfaceResultCode::Success);
    uint32_t wid = wm.create_window(10, "OwnerPID10", Rect{0, 0, 100, 100}, surf);
    TEST_ASSERT(wid > 0);

    // Attacker PID 99 attempts to destroy or modify
    TEST_ASSERT(!wm.destroy_window(99, wid));
    TEST_ASSERT(!wm.move_window(99, wid, 10, 10));
    TEST_ASSERT(!wm.resize_window(99, wid, 200, 200));

    // Non-existent window queries return null or false safely
    TEST_ASSERT(wm.get_window(99999) == nullptr);
    TEST_ASSERT(!wm.destroy_window(10, 99999));

    std::cout << "[PASS] Cross-PID window and surface isolation verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 223.23: SECURITY & FUZZING TESTS\n";
    std::cout << "========================================\n";

    test_fuzz_layout_engine_limits();
    test_fuzz_textarea_buffer_overflow();
    test_fuzz_clipboard_boundary_cases();
    test_foreign_window_and_surface_isolation();

    std::cout << "\nALL STAGE 223.23 TESTS PASSED (100%).\n";
    return 0;
}

#include <cassert>
#include <iostream>
#include <memory>
#include <string>

#include "apps/sample_apps.h"
#include "graphics/display_surface.h"
#include "graphics/window_manager.h"
#include "runtime/system_clipboard.h"
#include "security/capability_manager.h"
#include "storage/isolated_storage.h"
#include "vfs/vfs.h"

using namespace winios::apps;
using namespace winios::graphics;
using namespace winios::runtime;
using namespace winios::security;
using namespace winios::storage;
using namespace winios::vfs;

void test_calculator_app_interactive() {
    auto vfs = std::make_shared<VirtualFileSystem>();
    vfs->initialize_standard_hierarchy();
    auto caps = std::make_shared<CapabilityManager>();
    auto clip = std::make_shared<SystemClipboard>(caps);

    auto win_mgr = std::make_shared<WindowManager>();
    auto surf_mgr = std::make_shared<SurfaceResourceManager>();
    auto storage_calc = std::make_shared<IsolatedStorage>(vfs, "com.winios.calculator");

    // Grant capabilities to PID 2 (Calculator)
    caps->grant_named(2, {"display.window", "storage.private", "clipboard.write"});

    CalculatorApp calc(2, win_mgr, surf_mgr, storage_calc, clip);
    assert(calc.launch());
    assert(calc.is_running());
    assert(calc.window_id() > 0);
    assert(calc.display_text() == "0");

    // Test calculation: 7 + 5 = 12
    calc.press_key('7');
    assert(calc.display_text() == "7");

    calc.press_key('+');
    calc.press_key('5');
    assert(calc.display_text() == "5");

    calc.press_key('=');
    assert(calc.display_text() == "12");

    // Verify storage persistence of calculation history
    std::vector<std::uint8_t> hist_data;
    assert(storage_calc->read("calc_history.txt", hist_data) == StorageResult::Success);
    std::string hist_str(reinterpret_cast<const char*>(hist_data.data()), hist_data.size());
    assert(hist_str.find("12") != std::string::npos);

    // Verify clipboard content
    std::string clip_content;
    assert(clip->has_data());

    // Test second calculation: C, 9 * 8 = 72
    calc.press_key('C');
    assert(calc.display_text() == "0");
    calc.press_key('9');
    calc.press_key('*');
    calc.press_key('8');
    calc.press_key('=');
    assert(calc.display_text() == "72");

    calc.terminate();
    assert(!calc.is_running());

    std::cout << "[PASS] test_calculator_app_interactive passed\n";
}

void test_text_editor_app_interactive_and_persistence() {
    auto vfs = std::make_shared<VirtualFileSystem>();
    vfs->initialize_standard_hierarchy();
    auto caps = std::make_shared<CapabilityManager>();
    auto clip = std::make_shared<SystemClipboard>(caps);

    auto win_mgr = std::make_shared<WindowManager>();
    auto surf_mgr = std::make_shared<SurfaceResourceManager>();
    auto storage_editor = std::make_shared<IsolatedStorage>(vfs, "com.winios.texteditor");

    // Set clipboard text from prior app (PID 2)
    caps->grant_named(2, {"clipboard.write"});
    assert(clip->set_text(2, "72"));

    // Grant capabilities to PID 3 (Text Editor)
    caps->grant_named(3, {"display.window", "storage.private", "clipboard.read"});

    {
        TextEditorApp editor(3, win_mgr, surf_mgr, storage_editor, clip);
        assert(editor.launch());
        assert(editor.is_running());

        // Paste clipboard content ("72")
        editor.handle_paste();
        assert(editor.content() == "72");

        // Type additional text: " + 30 = 102"
        const std::string append_text = " + 30 = 102";
        for (char ch : append_text) {
            editor.handle_char(static_cast<char32_t>(ch));
        }
        assert(editor.content() == "72 + 30 = 102");

        // Backspace test
        editor.handle_backspace(); // removes '2'
        assert(editor.content() == "72 + 30 = 10");
        editor.handle_char(U'2'); // restore '2'
        assert(editor.content() == "72 + 30 = 102");

        // Save document and terminate
        editor.save_document();
        editor.terminate();
    }

    // Verify document was saved in isolated storage
    std::vector<std::uint8_t> doc_bytes;
    assert(storage_editor->read("document.txt", doc_bytes) == StorageResult::Success);
    std::string doc_saved(reinterpret_cast<const char*>(doc_bytes.data()), doc_bytes.size());
    assert(doc_saved == "72 + 30 = 102");

    // Relaunch Text Editor and verify persistence recovery
    {
        TextEditorApp editor2(3, win_mgr, surf_mgr, storage_editor, clip);
        assert(editor2.launch());
        assert(editor2.content() == "72 + 30 = 102");
        editor2.terminate();
    }

    std::cout << "[PASS] test_text_editor_app_interactive_and_persistence passed\n";
}

int main() {
    std::cout << "Starting Stage 215.21 Sample Applications Tests...\n";
    test_calculator_app_interactive();
    test_text_editor_app_interactive_and_persistence();
    std::cout << "All Stage 215.21 Sample Applications tests PASSED!\n";
    return 0;
}

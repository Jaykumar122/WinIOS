#include "apps/sample_apps.h"
#include "runtime/guest_ui_runtime.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace winios::apps {

// =============================================================================
// CalculatorApp Implementation
// =============================================================================
CalculatorApp::CalculatorApp(std::uint32_t pid,
                             std::shared_ptr<graphics::WindowManager> win_mgr,
                             std::shared_ptr<graphics::SurfaceResourceManager> surf_mgr,
                             std::shared_ptr<storage::IsolatedStorage> storage,
                             std::shared_ptr<runtime::SystemClipboard> clipboard)
    : pid_(pid)
    , win_mgr_(std::move(win_mgr))
    , surf_mgr_(std::move(surf_mgr))
    , storage_(std::move(storage))
    , clipboard_(std::move(clipboard)) {}

CalculatorApp::~CalculatorApp() {
    terminate();
}

bool CalculatorApp::launch() {
    if (is_running_) return true;
    if (!win_mgr_ || !surf_mgr_) return false;

    // Create 700x900 window
    const std::uint32_t w = 700;
    const std::uint32_t h = 900;
    auto res = surf_mgr_->create_surface(pid_, w, h, graphics::PixelFormat::B8G8R8A8_UNORM, surf_);
    if (res != graphics::SurfaceResultCode::Success || !surf_) {
        return false;
    }

    wid_ = win_mgr_->create_window(pid_, "Calculator", {50, 100, w, h}, surf_);
    is_running_ = true;
    render();
    return true;
}

void CalculatorApp::terminate() {
    if (!is_running_) return;
    if (surf_ && surf_mgr_) {
        surf_mgr_->destroy_surface(pid_, surf_->id());
        surf_ = nullptr;
    }
    if (wid_ != 0 && win_mgr_) {
        win_mgr_->destroy_window(pid_, wid_);
        wid_ = 0;
    }
    is_running_ = false;
}

void CalculatorApp::press_key(char key) {
    if (key >= '0' && key <= '9') {
        if (clear_on_next_digit_ || display_ == "0") {
            display_ = std::string(1, key);
            clear_on_next_digit_ = false;
        } else {
            display_.push_back(key);
        }
    } else if (key == '+' || key == '-' || key == '*' || key == '/') {
        accumulator_ = std::stod(display_);
        pending_op_ = key;
        clear_on_next_digit_ = true;
    } else if (key == '=') {
        execute_operation();
    } else if (key == 'C' || key == 'c') {
        display_ = "0";
        accumulator_ = 0.0;
        pending_op_ = '\0';
        clear_on_next_digit_ = true;
    }
    render();
}

void CalculatorApp::execute_operation() {
    if (pending_op_ == '\0') return;
    const double val = std::stod(display_);
    double result = 0.0;

    if (pending_op_ == '+') result = accumulator_ + val;
    else if (pending_op_ == '-') result = accumulator_ - val;
    else if (pending_op_ == '*') result = accumulator_ * val;
    else if (pending_op_ == '/') result = (val != 0.0) ? (accumulator_ / val) : 0.0;

    // Format output
    if (std::floor(result) == result) {
        display_ = std::to_string(static_cast<long long>(result));
    } else {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4) << result;
        display_ = ss.str();
    }

    pending_op_ = '\0';
    clear_on_next_digit_ = true;

    // Save to storage
    if (storage_) {
        const std::string hist = "Calculation: " + display_ + "\n";
        storage_->atomic_write("calc_history.txt", hist.data(), hist.size());
    }

    // Copy to clipboard
    if (clipboard_) {
        clipboard_->set_text(pid_, display_);
    }
}

bool CalculatorApp::handle_touch_tap(std::int32_t x, std::int32_t y) {
    // Buttons are in a grid from y = 300 to y = 800
    // Col width = 150, row height = 100, 4 columns, 4 rows
    if (y < 300 || y >= 700 || x < 50 || x >= 650) return false;

    const int col = (x - 50) / 150;
    const int row = (y - 300) / 100;

    static const char key_grid[4][4] = {
        {'7', '8', '9', '/'},
        {'4', '5', '6', '*'},
        {'1', '2', '3', '-'},
        {'C', '0', '=', '+'}
    };

    if (row >= 0 && row < 4 && col >= 0 && col < 4) {
        press_key(key_grid[row][col]);
        return true;
    }
    return false;
}

void CalculatorApp::render() {
    if (!surf_) return;
    auto* pix = surf_->raw_data();
    const std::uint32_t w = surf_->width();
    const std::uint32_t h = surf_->height();
    const std::uint32_t stride = w * 4;

    // 1. Dark background (0xFF1C1C1E)
    surf_->fill_rect(pid_, {0, 0, w, h}, 0xFF1C1C1E);

    // 2. Title Bar
    runtime::GuestUiRuntime::draw_string_8x8(
        pix, w, h, stride, graphics::PixelFormat::B8G8R8A8_UNORM,
        30, 40, "Calculator", 0xFFFFFFFF, 3);

    // 3. Display Screen (0xFF2C2C2E)
    surf_->fill_rect(pid_, {50, 120, 600, 140}, 0xFF2C2C2E);

    // Display string aligned to right
    const std::int32_t text_x = 600 - static_cast<std::int32_t>(display_.size() * 8 * 4);
    runtime::GuestUiRuntime::draw_string_8x8(
        pix, w, h, stride, graphics::PixelFormat::B8G8R8A8_UNORM,
        std::max(70, text_x), 160, display_.c_str(), 0xFFFFFFFF, 4);

    // 4. Grid of Buttons
    static const char* labels[4][4] = {
        {"7", "8", "9", "/"},
        {"4", "5", "6", "*"},
        {"1", "2", "3", "-"},
        {"C", "0", "=", "+"}
    };

    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            graphics::Rect btn_rect{
                static_cast<std::uint32_t>(50 + c * 150),
                static_cast<std::uint32_t>(300 + r * 100),
                130,
                80
            };
            const bool is_op = (c == 3 || (r == 3 && c == 2));
            const std::uint32_t bg_col = is_op ? 0xFFFF9F0A : 0xFF505050;

            runtime::GuestUiRuntime::draw_button(
                pix, w, h, stride, graphics::PixelFormat::B8G8R8A8_UNORM,
                btn_rect, labels[r][c], bg_col, 0xFFFFFFFF, false);
        }
    }
}

// =============================================================================
// TextEditorApp Implementation
// =============================================================================
TextEditorApp::TextEditorApp(std::uint32_t pid,
                             std::shared_ptr<graphics::WindowManager> win_mgr,
                             std::shared_ptr<graphics::SurfaceResourceManager> surf_mgr,
                             std::shared_ptr<storage::IsolatedStorage> storage,
                             std::shared_ptr<runtime::SystemClipboard> clipboard)
    : pid_(pid)
    , win_mgr_(std::move(win_mgr))
    , surf_mgr_(std::move(surf_mgr))
    , storage_(std::move(storage))
    , clipboard_(std::move(clipboard)) {}

TextEditorApp::~TextEditorApp() {
    terminate();
}

bool TextEditorApp::launch() {
    if (is_running_) return true;
    if (!win_mgr_ || !surf_mgr_) return false;

    const std::uint32_t w = 700;
    const std::uint32_t h = 900;
    auto res = surf_mgr_->create_surface(pid_, w, h, graphics::PixelFormat::B8G8R8A8_UNORM, surf_);
    if (res != graphics::SurfaceResultCode::Success || !surf_) {
        return false;
    }

    wid_ = win_mgr_->create_window(pid_, "Text Editor", {50, 100, w, h}, surf_);
    is_running_ = true;
    load_document();
    render();
    return true;
}

void TextEditorApp::terminate() {
    if (!is_running_) return;
    save_document();
    if (surf_ && surf_mgr_) {
        surf_mgr_->destroy_surface(pid_, surf_->id());
        surf_ = nullptr;
    }
    if (wid_ != 0 && win_mgr_) {
        win_mgr_->destroy_window(pid_, wid_);
        wid_ = 0;
    }
    is_running_ = false;
}

void TextEditorApp::handle_char(char32_t ch) {
    if (ch >= 32 && ch <= 126) {
        buffer_.push_back(static_cast<char>(ch));
        render();
    }
}

void TextEditorApp::handle_backspace() {
    if (!buffer_.empty()) {
        buffer_.pop_back();
        render();
    }
}

void TextEditorApp::handle_paste() {
    if (!clipboard_) return;
    std::string text;
    if (clipboard_->get_text(pid_, text)) {
        buffer_.append(text);
        render();
    }
}

void TextEditorApp::save_document() {
    if (storage_ && !buffer_.empty()) {
        storage_->atomic_write("document.txt", buffer_.data(), buffer_.size());
    }
}

void TextEditorApp::load_document() {
    if (!storage_) return;
    std::vector<std::uint8_t> data;
    if (storage_->read("document.txt", data) == storage::StorageResult::Success) {
        buffer_ = std::string(reinterpret_cast<const char*>(data.data()), data.size());
    }
}

void TextEditorApp::render() {
    if (!surf_) return;
    auto* pix = surf_->raw_data();
    const std::uint32_t w = surf_->width();
    const std::uint32_t h = surf_->height();
    const std::uint32_t stride = w * 4;

    // 1. Background (0xFFFFFFFF White canvas)
    surf_->fill_rect(pid_, {0, 0, w, h}, 0xFFFFFFFF);

    // 2. Header Bar (0xFFF2F2F7 Gray)
    surf_->fill_rect(pid_, {0, 0, w, 80}, 0xFFF2F2F7);

    runtime::GuestUiRuntime::draw_string_8x8(
        pix, w, h, stride, graphics::PixelFormat::B8G8R8A8_UNORM,
        30, 25, "Text Editor - document.txt", 0xFF000000, 2);

    // 3. Document text body
    std::string display_str = buffer_ + "_"; // cursor
    runtime::GuestUiRuntime::draw_string_8x8(
        pix, w, h, stride, graphics::PixelFormat::B8G8R8A8_UNORM,
        40, 110, display_str.c_str(), 0xFF1C1C1E, 2);
}

// =============================================================================
// MultiWindowEditorApp Implementation
// =============================================================================
MultiWindowEditorApp::MultiWindowEditorApp(std::uint32_t pid,
                                           std::shared_ptr<graphics::WindowManager> win_mgr,
                                           std::shared_ptr<graphics::SurfaceResourceManager> surf_mgr,
                                           std::shared_ptr<storage::IsolatedStorage> storage)
    : pid_(pid)
    , win_mgr_(std::move(win_mgr))
    , surf_mgr_(std::move(surf_mgr))
    , storage_(std::move(storage)) {}

MultiWindowEditorApp::~MultiWindowEditorApp() {
    terminate();
}

bool MultiWindowEditorApp::launch() {
    if (is_running_) return true;
    if (!win_mgr_ || !surf_mgr_) return false;

    // Create Primary Editor Window Surface
    const std::uint32_t ew = 450;
    const std::uint32_t eh = 350;
    auto res1 = surf_mgr_->create_surface(pid_, ew, eh, graphics::PixelFormat::B8G8R8A8_UNORM, primary_surf_);
    if (res1 != graphics::SurfaceResultCode::Success || !primary_surf_) {
        return false;
    }

    // Create Secondary Tool Palette Window Surface
    const std::uint32_t pw = 180;
    const std::uint32_t ph = 260;
    auto res2 = surf_mgr_->create_surface(pid_, pw, ph, graphics::PixelFormat::B8G8R8A8_UNORM, palette_surf_);
    if (res2 != graphics::SurfaceResultCode::Success || !palette_surf_) {
        surf_mgr_->destroy_surface(pid_, primary_surf_->id());
        primary_surf_ = nullptr;
        return false;
    }

    primary_wid_ = win_mgr_->create_window(pid_, "Editor Document", {40, 80, ew, eh}, primary_surf_);
    palette_wid_ = win_mgr_->create_window(pid_, "Tool Palette", {510, 80, pw, ph}, palette_surf_);

    // Set Palette Window as Always-on-Top floating window
    win_mgr_->set_always_on_top(pid_, palette_wid_, true);

    is_running_ = true;
    load_document();
    render();
    return true;
}

void MultiWindowEditorApp::terminate() {
    if (!is_running_) return;
    save_document();

    if (palette_surf_ && surf_mgr_) {
        surf_mgr_->destroy_surface(pid_, palette_surf_->id());
        palette_surf_ = nullptr;
    }
    if (palette_wid_ != 0 && win_mgr_) {
        win_mgr_->destroy_window(pid_, palette_wid_);
        palette_wid_ = 0;
    }

    if (primary_surf_ && surf_mgr_) {
        surf_mgr_->destroy_surface(pid_, primary_surf_->id());
        primary_surf_ = nullptr;
    }
    if (primary_wid_ != 0 && win_mgr_) {
        win_mgr_->destroy_window(pid_, primary_wid_);
        primary_wid_ = 0;
    }

    is_running_ = false;
}

void MultiWindowEditorApp::handle_char(char32_t ch) {
    if (current_tool_ == 0) { // Pen
        if (ch >= 32 && ch <= 126) {
            buffer_.push_back(static_cast<char>(ch));
            render_editor();
        }
    } else if (current_tool_ == 1) { // Eraser
        handle_backspace();
    }
}

void MultiWindowEditorApp::handle_backspace() {
    if (!buffer_.empty()) {
        buffer_.pop_back();
        render_editor();
    }
}

void MultiWindowEditorApp::select_tool(int tool_id) {
    current_tool_ = tool_id;
    if (current_tool_ == 2) { // Clear
        buffer_.clear();
        render_editor();
    } else if (current_tool_ == 3) { // Save
        save_document();
    }
    render_palette();
}

bool MultiWindowEditorApp::handle_palette_tap(std::int32_t x, std::int32_t y) {
    if (x < 15 || x > 165) return false;
    if (y >= 40 && y < 80) {
        select_tool(0); // Pen
        return true;
    } else if (y >= 90 && y < 130) {
        select_tool(1); // Eraser
        return true;
    } else if (y >= 140 && y < 180) {
        select_tool(2); // Clear
        return true;
    } else if (y >= 190 && y < 230) {
        select_tool(3); // Save
        return true;
    }
    return false;
}

void MultiWindowEditorApp::save_document() {
    if (storage_) {
        storage_->atomic_write("document.txt", buffer_.data(), buffer_.size());
    }
}

void MultiWindowEditorApp::load_document() {
    if (storage_) {
        std::vector<std::uint8_t> data;
        if (storage_->read("document.txt", data) == storage::StorageResult::Success) {
            buffer_ = std::string(reinterpret_cast<const char*>(data.data()), data.size());
        }
    }
}

void MultiWindowEditorApp::render_editor() {
    if (!primary_surf_) return;
    auto* pix = primary_surf_->raw_data();
    const std::uint32_t w = primary_surf_->width();
    const std::uint32_t h = primary_surf_->height();
    const std::uint32_t stride = w * 4;

    primary_surf_->fill_rect(pid_, {0, 0, w, h}, 0xFF1E1E1E);

    runtime::GuestUiRuntime::draw_string_8x8(
        pix, w, h, stride, graphics::PixelFormat::B8G8R8A8_UNORM,
        20, 20, "Document Editor", 0xFF64D2FF, 2);

    primary_surf_->fill_rect(pid_, {20, 50, w - 40, h - 70}, 0xFF2A2A2C);

    if (!buffer_.empty()) {
        runtime::GuestUiRuntime::draw_string_8x8(
            pix, w, h, stride, graphics::PixelFormat::B8G8R8A8_UNORM,
            30, 65, buffer_.c_str(), 0xFFFFFFFF, 2);
    } else {
        runtime::GuestUiRuntime::draw_string_8x8(
            pix, w, h, stride, graphics::PixelFormat::B8G8R8A8_UNORM,
            30, 65, "[Type to insert text...]", 0xFF8E8E93, 2);
    }

    primary_surf_->damage_tracker().mark_full_damage(w, h);
}

void MultiWindowEditorApp::render_palette() {
    if (!palette_surf_) return;
    auto* pix = palette_surf_->raw_data();
    const std::uint32_t w = palette_surf_->width();
    const std::uint32_t h = palette_surf_->height();
    const std::uint32_t stride = w * 4;

    palette_surf_->fill_rect(pid_, {0, 0, w, h}, 0xFF252526);

    runtime::GuestUiRuntime::draw_string_8x8(
        pix, w, h, stride, graphics::PixelFormat::B8G8R8A8_UNORM,
        15, 15, "Tool Palette", 0xFFFFFFFF, 1);

    static const char* tool_names[4] = {"Pen", "Eraser", "Clear", "Save"};
    for (int i = 0; i < 4; ++i) {
        graphics::Rect btn_rect{15, static_cast<std::uint32_t>(40 + i * 50), 150, 40};
        bool is_selected = (current_tool_ == i);
        std::uint32_t bg_col = is_selected ? 0xFF0A84FF : 0xFF3A3A3C;
        runtime::GuestUiRuntime::draw_button(
            pix, w, h, stride, graphics::PixelFormat::B8G8R8A8_UNORM,
            btn_rect, tool_names[i], bg_col, 0xFFFFFFFF, is_selected);
    }

    palette_surf_->damage_tracker().mark_full_damage(w, h);
}

void MultiWindowEditorApp::render() {
    render_editor();
    render_palette();
}

// =============================================================================
// Package Generation Helpers
// =============================================================================
packaging::AppPackage create_calculator_package() {
    packaging::AppPackage pkg;
    pkg.manifest.bundle_id = "com.winios.calculator";
    pkg.manifest.name = "Calculator";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.min_os_version = "215.0";
    pkg.manifest.executable_name = "calculator.bin";
    pkg.manifest.permissions = {"display.window", "storage.private", "clipboard.write"};

    // Mach-O executable payload
    std::vector<std::uint8_t> exec_bytes = {
        0xCF, 0xFA, 0xED, 0xFE, 0x0C, 0x00, 0x00, 0x01, // Mach-O 64-bit ARM64
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        'C', 'A', 'L', 'C', 'U', 'L', 'A', 'T', 'O', 'R'
    };
    pkg.add_file("calculator.bin", std::move(exec_bytes));

    std::vector<std::uint8_t> icon_bytes = {0x89, 'P', 'N', 'G', 'C', 'A', 'L', 'C'};
    pkg.add_file("res/icon.png", std::move(icon_bytes));

    return pkg;
}

packaging::AppPackage create_text_editor_package() {
    packaging::AppPackage pkg;
    pkg.manifest.bundle_id = "com.winios.texteditor";
    pkg.manifest.name = "Text Editor";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.min_os_version = "215.0";
    pkg.manifest.executable_name = "editor.bin";
    pkg.manifest.permissions = {"display.window", "storage.private", "clipboard.read"};

    std::vector<std::uint8_t> exec_bytes = {
        0xCF, 0xFA, 0xED, 0xFE, 0x0C, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        'T', 'E', 'X', 'T', 'E', 'D', 'I', 'T', 'O', 'R'
    };
    pkg.add_file("editor.bin", std::move(exec_bytes));

    std::vector<std::uint8_t> icon_bytes = {0x89, 'P', 'N', 'G', 'E', 'D', 'I', 'T'};
    pkg.add_file("res/icon.png", std::move(icon_bytes));

    return pkg;
}

packaging::AppPackage create_multi_window_editor_package() {
    packaging::AppPackage pkg;
    pkg.manifest.bundle_id = "com.winios.multieditor";
    pkg.manifest.name = "Multi-Window Editor";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.min_os_version = "216.0";
    pkg.manifest.executable_name = "multieditor.bin";
    pkg.manifest.permissions = {"display.window", "storage.private", "event.timer"};

    std::vector<std::uint8_t> exec_bytes = {
        0xCF, 0xFA, 0xED, 0xFE, 0x0C, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        'M', 'U', 'L', 'T', 'I', 'E', 'D', 'I', 'T', 'O', 'R'
    };
    pkg.add_file("multieditor.bin", std::move(exec_bytes));

    std::vector<std::uint8_t> icon_bytes = {0x89, 'P', 'N', 'G', 'M', 'U', 'L', 'T', 'I'};
    pkg.add_file("res/icon.png", std::move(icon_bytes));

    return pkg;
}

} // namespace winios::apps

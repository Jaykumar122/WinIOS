#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "graphics/display_surface.h"
#include "graphics/window_manager.h"
#include "input/virtual_input_device.h"
#include "packaging/app_package.h"
#include "runtime/system_clipboard.h"
#include "storage/isolated_storage.h"

namespace winios::apps {

// -----------------------------------------------------------------------------
// Interactive Calculator Application (com.winios.calculator)
// -----------------------------------------------------------------------------
class CalculatorApp {
public:
    CalculatorApp(std::uint32_t pid,
                  std::shared_ptr<graphics::WindowManager> win_mgr,
                  std::shared_ptr<graphics::SurfaceResourceManager> surf_mgr,
                  std::shared_ptr<storage::IsolatedStorage> storage,
                  std::shared_ptr<runtime::SystemClipboard> clipboard);
    ~CalculatorApp();

    bool launch();
    void terminate();

    // Process touch tap at window-relative coordinates
    bool handle_touch_tap(std::int32_t x, std::int32_t y);

    // Direct button press simulation
    void press_key(char key);

    // Redraw interface
    void render();

    [[nodiscard]] std::string display_text() const { return display_; }
    [[nodiscard]] std::uint32_t window_id() const noexcept { return wid_; }
    [[nodiscard]] bool is_running() const noexcept { return is_running_; }
    [[nodiscard]] std::shared_ptr<graphics::DisplaySurface> surface() const noexcept { return surf_; }

private:
    void execute_operation();

    std::uint32_t                                     pid_{2};
    std::shared_ptr<graphics::WindowManager>          win_mgr_;
    std::shared_ptr<graphics::SurfaceResourceManager> surf_mgr_;
    std::shared_ptr<storage::IsolatedStorage>         storage_;
    std::shared_ptr<runtime::SystemClipboard>         clipboard_;

    std::uint32_t                                     wid_{0};
    std::shared_ptr<graphics::DisplaySurface>         surf_{nullptr};
    bool                                              is_running_{false};

    std::string display_{"0"};
    double accumulator_{0.0};
    char pending_op_{'\0'};
    bool clear_on_next_digit_{true};
};

// -----------------------------------------------------------------------------
// Interactive Text Editor Application (com.winios.texteditor)
// -----------------------------------------------------------------------------
class TextEditorApp {
public:
    TextEditorApp(std::uint32_t pid,
                  std::shared_ptr<graphics::WindowManager> win_mgr,
                  std::shared_ptr<graphics::SurfaceResourceManager> surf_mgr,
                  std::shared_ptr<storage::IsolatedStorage> storage,
                  std::shared_ptr<runtime::SystemClipboard> clipboard);
    ~TextEditorApp();

    bool launch();
    void terminate();

    // Input handlers
    void handle_char(char32_t ch);
    void handle_backspace();
    void handle_paste();
    void save_document();
    void load_document();

    // Redraw interface
    void render();

    [[nodiscard]] std::string content() const { return buffer_; }
    [[nodiscard]] std::uint32_t window_id() const noexcept { return wid_; }
    [[nodiscard]] bool is_running() const noexcept { return is_running_; }
    [[nodiscard]] std::shared_ptr<graphics::DisplaySurface> surface() const noexcept { return surf_; }

private:
    std::uint32_t                                     pid_{3};
    std::shared_ptr<graphics::WindowManager>          win_mgr_;
    std::shared_ptr<graphics::SurfaceResourceManager> surf_mgr_;
    std::shared_ptr<storage::IsolatedStorage>         storage_;
    std::shared_ptr<runtime::SystemClipboard>         clipboard_;

    std::uint32_t                                     wid_{0};
    std::shared_ptr<graphics::DisplaySurface>         surf_{nullptr};
    bool                                              is_running_{false};

    std::string buffer_;
};

// Package generation utilities
packaging::AppPackage create_calculator_package();
packaging::AppPackage create_text_editor_package();

} // namespace winios::apps

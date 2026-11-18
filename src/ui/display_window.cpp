#include "ui/display_window.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>

#include "input/input_provenance.h"
#include "kernel/guest_trace_logger.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#endif

namespace winios::ui {

#ifdef _WIN32
namespace {

constexpr const char* kWindowClassName = "WinIOS_iPhone15_Surface";
bool g_class_registered = false;

LRESULT CALLBACK DisplayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* win = reinterpret_cast<DisplayWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (win) {
            win->blit_to_dc(hdc);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE: {
        if (win) {
            if (wParam == SIZE_MINIMIZED) {
                win->set_minimized(true);
            } else if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED) {
                win->set_minimized(false);
                RECT rc;
                GetClientRect(hwnd, &rc);
                win->handle_resize(static_cast<std::uint32_t>(rc.right - rc.left),
                                   static_cast<std::uint32_t>(rc.bottom - rc.top));
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_SETFOCUS: {
        if (win) {
            win->set_focused(true);
        }
        return 0;
    }

    case WM_KILLFOCUS: {
        if (win) {
            win->set_focused(false);
        }
        return 0;
    }

    case WM_KEYDOWN:
    case WM_KEYUP: {
        if (win) {
            const bool is_injected = (InSendMessage() != 0);
            win->on_win32_key(static_cast<std::uint64_t>(wParam), static_cast<std::uint64_t>(lParam), is_injected);
        }
        return 0;
    }

    case WM_CHAR: {
        if (win) {
            const bool is_injected = (InSendMessage() != 0);
            win->on_win32_char(static_cast<std::uint64_t>(wParam), static_cast<std::uint64_t>(lParam), is_injected);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (win) {
            const int cx = GET_X_LPARAM(lParam);
            const int cy = GET_Y_LPARAM(lParam);
            POINT pt = { cx, cy };
            ClientToScreen(hwnd, &pt);
            const bool is_injected = (InSendMessage() != 0);

            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);

            win->on_win32_mouse_move(cx, cy, pt.x, pt.y, static_cast<std::uint64_t>(wParam), is_injected);
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP: {
        if (win) {
            const int cx = GET_X_LPARAM(lParam);
            const int cy = GET_Y_LPARAM(lParam);
            POINT pt = { cx, cy };
            ClientToScreen(hwnd, &pt);
            const bool is_injected = (InSendMessage() != 0);

            input::MouseButton btn = input::MouseButton::Left;
            bool is_down = false;
            if (msg == WM_LBUTTONDOWN) {
                btn = input::MouseButton::Left;
                is_down = true;
                SetCapture(hwnd);
                win->set_mouse_capture(true);
            } else if (msg == WM_LBUTTONUP) {
                btn = input::MouseButton::Left;
                is_down = false;
                ReleaseCapture();
                win->set_mouse_capture(false);
            } else if (msg == WM_RBUTTONDOWN) {
                btn = input::MouseButton::Right;
                is_down = true;
            } else if (msg == WM_RBUTTONUP) {
                btn = input::MouseButton::Right;
                is_down = false;
            } else if (msg == WM_MBUTTONDOWN) {
                btn = input::MouseButton::Middle;
                is_down = true;
            } else if (msg == WM_MBUTTONUP) {
                btn = input::MouseButton::Middle;
                is_down = false;
            }

            win->on_win32_mouse_button(btn, is_down, cx, cy, pt.x, pt.y, static_cast<std::uint64_t>(wParam), is_injected);
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (win) {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            const int sx = GET_X_LPARAM(lParam);
            const int sy = GET_Y_LPARAM(lParam);
            POINT pt = { sx, sy };
            ScreenToClient(hwnd, &pt);
            const bool is_injected = (InSendMessage() != 0);

            win->on_win32_mouse_wheel(delta, pt.x, pt.y, sx, sy, static_cast<std::uint64_t>(wParam), is_injected);
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        if (win) {
            win->on_win32_mouse_leave();
        }
        return 0;
    }

    case WM_CLOSE:
        if (win) {
            win->close();
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

} // anonymous namespace
#endif

DisplayWindow::DisplayWindow(const DisplayWindowConfig& config)
    : config_(config) {
    update_window_geometry();
    const std::size_t fb_size = static_cast<std::size_t>(config_.native_width) * config_.native_height * 4;
    backbuffer_.assign(fb_size, 0);
}

DisplayWindow::~DisplayWindow() {
    close();
}

void DisplayWindow::update_window_geometry() {
    window_width_ = static_cast<std::uint32_t>(static_cast<float>(config_.native_width) * config_.scale);
    window_height_ = static_cast<std::uint32_t>(static_cast<float>(config_.native_height) * config_.scale);
    if (window_width_ < 200) window_width_ = 200;
    if (window_height_ < 400) window_height_ = 400;
}

bool DisplayWindow::create() {
    if (is_open_) return true;

    if (config_.headless) {
        is_open_ = true;
        lifecycle_state_ = DisplayLifecycleState::Visible;
        return true;
    }

#ifdef _WIN32
    HINSTANCE hinst = GetModuleHandleA(nullptr);

    if (!g_class_registered) {
        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(WNDCLASSEXA);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = DisplayWndProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = kWindowClassName;

        if (!RegisterClassExA(&wc)) {
            // Could already be registered
        }
        g_class_registered = true;
    }

    RECT rc = { 0, 0, static_cast<LONG>(window_width_), static_cast<LONG>(window_height_) };
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rc, style, FALSE);

    const int total_w = rc.right - rc.left;
    const int total_h = rc.bottom - rc.top;

    HWND hwnd = CreateWindowExA(
        0,
        kWindowClassName,
        config_.title.c_str(),
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        total_w, total_h,
        nullptr, nullptr,
        hinst, nullptr);

    if (!hwnd) {
        // Fall back to headless if GUI creation is unavailable (e.g. CI / non-interactive session)
        config_.headless = true;
        is_open_ = true;
        lifecycle_state_ = DisplayLifecycleState::Visible;
        return true;
    }

    SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    hwnd_ = hwnd;
    hdc_ = GetDC(hwnd);
    is_open_ = true;
    lifecycle_state_ = DisplayLifecycleState::Visible;
    return true;
#else
    config_.headless = true;
    is_open_ = true;
    lifecycle_state_ = DisplayLifecycleState::Visible;
    return true;
#endif
}

void DisplayWindow::close() {
    std::lock_guard<std::mutex> lock(lock_);
    if (!is_open_) return;

#ifdef _WIN32
    if (hwnd_) {
        HWND hwnd = reinterpret_cast<HWND>(hwnd_);
        HDC hdc = reinterpret_cast<HDC>(hdc_);
        if (hdc) ReleaseDC(hwnd, hdc);
        DestroyWindow(hwnd);
        hwnd_ = nullptr;
        hdc_ = nullptr;
    }
#endif

    is_open_ = false;
    lifecycle_state_ = DisplayLifecycleState::Closed;
}

void DisplayWindow::show() {
    std::lock_guard<std::mutex> lock(lock_);
    if (!is_open_) return;
#ifdef _WIN32
    if (hwnd_) {
        ShowWindow(reinterpret_cast<HWND>(hwnd_), SW_SHOW);
    }
#endif
    lifecycle_state_ = DisplayLifecycleState::Visible;
}

void DisplayWindow::hide() {
    std::lock_guard<std::mutex> lock(lock_);
    if (!is_open_) return;
#ifdef _WIN32
    if (hwnd_) {
        ShowWindow(reinterpret_cast<HWND>(hwnd_), SW_HIDE);
    }
#endif
    lifecycle_state_ = DisplayLifecycleState::Hidden;
}

void DisplayWindow::set_minimized(bool min) {
    std::lock_guard<std::mutex> lock(lock_);
    lifecycle_state_ = min ? DisplayLifecycleState::Minimized : DisplayLifecycleState::Visible;
}

void DisplayWindow::handle_resize(std::uint32_t new_width, std::uint32_t new_height) {
    std::lock_guard<std::mutex> lock(lock_);
    if (new_width > 0 && new_height > 0) {
        window_width_ = new_width;
        window_height_ = new_height;
    }
}

DisplayLifecycleState DisplayWindow::lifecycle_state() const noexcept {
    std::lock_guard<std::mutex> lock(lock_);
    return lifecycle_state_;
}

PresentationStats DisplayWindow::presentation_stats() const noexcept {
    std::lock_guard<std::mutex> lock(lock_);
    PresentationStats stats{};
    stats.total_frames_presented = total_frames_;
    stats.total_frames_dropped = total_dropped_frames_;
    stats.last_presentation_time_ns = last_presentation_time_ns_;
    stats.average_frame_interval_ms = average_frame_interval_ms_;
    stats.client_width = window_width_;
    stats.client_height = window_height_;
    stats.lifecycle_state = lifecycle_state_;
    return stats;
}

bool DisplayWindow::pump_events() {
    if (!is_open_) return false;
    if (config_.headless) return true;

#ifdef _WIN32
    if (!hwnd_) return false;
    HWND hwnd = reinterpret_cast<HWND>(hwnd_);
    MSG msg;

    while (PeekMessageA(&msg, hwnd, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            is_open_ = false;
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return is_open_;
#else
    return true;
#endif
}

void DisplayWindow::blit_to_dc(void* hdc_ptr) {
#ifdef _WIN32
    if (!hdc_ptr || !hwnd_) return;
    HDC hdc = reinterpret_cast<HDC>(hdc_ptr);
    HWND hwnd = reinterpret_cast<HWND>(hwnd_);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int client_w = rc.right - rc.left;
    int client_h = rc.bottom - rc.top;
    if (client_w <= 0 || client_h <= 0) return;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = static_cast<LONG>(config_.native_width);
    bi.bmiHeader.biHeight = -static_cast<LONG>(config_.native_height); // Top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    SetStretchBltMode(hdc, HALFTONE);

    std::lock_guard<std::mutex> lock(lock_);
    if (!backbuffer_.empty()) {
        StretchDIBits(hdc,
                      0, 0, client_w, client_h,
                      0, 0, config_.native_width, config_.native_height,
                      backbuffer_.data(),
                      &bi, DIB_RGB_COLORS, SRCCOPY);
    }
#else
    (void)hdc_ptr;
#endif
}

bool DisplayWindow::present_frame(const device::AppleDisplay& display) {
    return present_frame(display.raw_framebuffer_data(), display.width(), display.height());
}

bool DisplayWindow::present_frame(const std::uint8_t* raw_argb, std::uint32_t width, std::uint32_t height) {
    if (!raw_argb || width == 0 || height == 0) {
        std::lock_guard<std::mutex> lock(lock_);
        total_dropped_frames_++;
        return false;
    }
    if (!is_open_) {
        if (!create()) return false;
    }

    const auto now_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    {
        std::lock_guard<std::mutex> lock(lock_);
        const std::size_t incoming_bytes = static_cast<std::size_t>(width) * height * 4;
        if (backbuffer_.size() != incoming_bytes) {
            backbuffer_.resize(incoming_bytes);
        }
        std::memcpy(backbuffer_.data(), raw_argb, incoming_bytes);
        total_frames_++;

        if (last_presentation_time_ns_ > 0 && now_ns > last_presentation_time_ns_) {
            const double delta_ms = static_cast<double>(now_ns - last_presentation_time_ns_) / 1'000'000.0;
            if (average_frame_interval_ms_ == 0.0) {
                average_frame_interval_ms_ = delta_ms;
            } else {
                average_frame_interval_ms_ = (average_frame_interval_ms_ * 0.9) + (delta_ms * 0.1);
            }
        }
        last_presentation_time_ns_ = now_ns;
    }

#ifdef _WIN32
    if (!config_.headless && hwnd_ && hdc_) {
        blit_to_dc(hdc_);
        pump_events();
    }
#endif

    return true;
}

TouchEvent DisplayWindow::last_touch() const noexcept {
    std::lock_guard<std::mutex> lock(lock_);
    return last_touch_;
}

void DisplayWindow::inject_touch(std::uint32_t x, std::uint32_t y, bool is_down) {
    TouchEvent ev;
    ev.x = std::min(x, config_.native_width - 1);
    ev.y = std::min(y, config_.native_height - 1);
    ev.is_down = is_down;

    TouchCallback cb;
    {
        std::lock_guard<std::mutex> lock(lock_);
        last_touch_ = ev;
        cb = touch_callback_;
    }

    if (cb) {
        cb(ev);
    }
}

void DisplayWindow::inject_key(std::uint32_t key_code, bool is_down) {
    KeyCallback cb;
    {
        std::lock_guard<std::mutex> lock(lock_);
        cb = key_callback_;
    }
    if (cb) {
        cb(key_code, is_down);
    }
}

void DisplayWindow::inject_char(char32_t ch) {
    CharCallback cb;
    {
        std::lock_guard<std::mutex> lock(lock_);
        cb = char_callback_;
    }
    if (cb) {
        cb(ch);
    }
}

void DisplayWindow::set_focused(bool focused) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (is_focused_ != focused) {
            is_focused_ = focused;
            changed = true;
        }
    }
    if (changed) {
        input::InputEvent ev{};
        ev.type = focused ? input::InputEventType::WindowFocus : input::InputEventType::WindowBlur;
        ev.timestamp_us = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        ev.provenance = input::InputProvenance::SyntheticHostEvent;
        ev.win32_msg_id = focused ? 0x0007 : 0x0008; // WM_SETFOCUS : WM_KILLFOCUS
        if (input_queue_) {
            input_queue_->push(ev);
        }
        if (event_callback_) {
            event_callback_(ev);
        }
    }
}

void DisplayWindow::set_mouse_capture(bool capture) {
    std::lock_guard<std::mutex> lock(lock_);
    has_mouse_capture_ = capture;
}

void DisplayWindow::inject_mouse_move(std::int32_t x, std::int32_t y, input::InputProvenance provenance) {
    input::InputEvent ev{};
    ev.type = input::InputEventType::MouseMove;
    ev.timestamp_us = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    ev.provenance = provenance;
    ev.win32_msg_id = 0x0200; // WM_MOUSEMOVE
    ev.mouse.x = x;
    ev.mouse.y = y;
    ev.mouse.client_x = x;
    ev.mouse.client_y = y;

    kernel::GuestTraceLogger::instance().log_input_provenance(ev, input::InputDeliveryStatus::Delivered);

    if (input_queue_) {
        input_queue_->push(ev);
    }
    if (event_callback_) {
        event_callback_(ev);
    }
}

void DisplayWindow::inject_mouse_button(input::MouseButton btn, bool is_down, std::int32_t x, std::int32_t y, input::InputProvenance provenance) {
    input::InputEvent ev{};
    ev.type = is_down ? input::InputEventType::MouseDown : input::InputEventType::MouseUp;
    ev.timestamp_us = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    ev.provenance = provenance;
    ev.win32_msg_id = is_down ? (btn == input::MouseButton::Left ? 0x0201 : 0x0204)
                              : (btn == input::MouseButton::Left ? 0x0202 : 0x0205);
    ev.mouse.button = btn;
    ev.mouse.x = x;
    ev.mouse.y = y;
    ev.mouse.client_x = x;
    ev.mouse.client_y = y;

    kernel::GuestTraceLogger::instance().log_input_provenance(ev, input::InputDeliveryStatus::Delivered);

    if (input_queue_) {
        input_queue_->push(ev);
    }
    if (event_callback_) {
        event_callback_(ev);
    }

    if (btn == input::MouseButton::Left) {
        inject_touch(static_cast<std::uint32_t>(std::max(0, x)), static_cast<std::uint32_t>(std::max(0, y)), is_down);
    }
}

void DisplayWindow::inject_mouse_wheel(std::int32_t delta, std::int32_t x, std::int32_t y, input::InputProvenance provenance) {
    input::InputEvent ev{};
    ev.type = input::InputEventType::MouseWheel;
    ev.timestamp_us = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    ev.provenance = provenance;
    ev.win32_msg_id = 0x020A; // WM_MOUSEWHEEL
    ev.mouse.wheel_delta = delta;
    ev.mouse.x = x;
    ev.mouse.y = y;
    ev.mouse.client_x = x;
    ev.mouse.client_y = y;

    kernel::GuestTraceLogger::instance().log_input_provenance(ev, input::InputDeliveryStatus::Delivered);

    if (input_queue_) {
        input_queue_->push(ev);
    }
    if (event_callback_) {
        event_callback_(ev);
    }
}

void DisplayWindow::inject_key_event(std::uint32_t key_code, bool is_down, bool is_repeat, std::uint32_t modifiers, input::InputProvenance provenance) {
    input::InputEvent ev{};
    ev.type = is_down ? input::InputEventType::KeyPress : input::InputEventType::KeyRelease;
    ev.timestamp_us = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    ev.provenance = provenance;
    ev.win32_msg_id = is_down ? 0x0100 : 0x0101; // WM_KEYDOWN : WM_KEYUP
    ev.keyboard.virtual_key = key_code;
    ev.keyboard.is_down = is_down;
    ev.keyboard.is_repeat = is_repeat;
    ev.keyboard.modifiers = modifiers;

    kernel::GuestTraceLogger::instance().log_input_provenance(ev, input::InputDeliveryStatus::Delivered);

    if (input_queue_) {
        input_queue_->push(ev);
    }
    if (event_callback_) {
        event_callback_(ev);
    }

    inject_key(key_code, is_down);
}

void DisplayWindow::inject_char_event(char32_t ch, input::InputProvenance provenance) {
    input::InputEvent ev{};
    ev.type = input::InputEventType::CharInput;
    ev.timestamp_us = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    ev.provenance = provenance;
    ev.win32_msg_id = 0x0102; // WM_CHAR
    ev.keyboard.char_code = ch;

    kernel::GuestTraceLogger::instance().log_input_provenance(ev, input::InputDeliveryStatus::Delivered);

    if (input_queue_) {
        input_queue_->push(ev);
    }
    if (event_callback_) {
        event_callback_(ev);
    }

    inject_char(ch);
}

void DisplayWindow::on_win32_mouse_move(int cx, int cy, int sx, int sy, std::uint64_t wparam, bool is_injected) {
    (void)wparam;
    const std::uint32_t cw = std::max(1u, window_width_);
    const std::uint32_t ch = std::max(1u, window_height_);
    const auto gx = static_cast<std::int32_t>(std::clamp(cx * static_cast<int>(config_.native_width) / static_cast<int>(cw),
                                                          0, static_cast<int>(config_.native_width - 1)));
    const auto gy = static_cast<std::int32_t>(std::clamp(cy * static_cast<int>(config_.native_height) / static_cast<int>(ch),
                                                          0, static_cast<int>(config_.native_height - 1)));

    input::InputEvent ev{};
    ev.type = input::InputEventType::MouseMove;
    ev.timestamp_us = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    ev.provenance = is_injected ? input::InputProvenance::WindowsMessageInjected : input::InputProvenance::PhysicalWindowsInput;
    ev.win32_msg_id = 0x0200; // WM_MOUSEMOVE
    ev.mouse.x = gx;
    ev.mouse.y = gy;
    ev.mouse.screen_x = sx;
    ev.mouse.screen_y = sy;
    ev.mouse.client_x = cx;
    ev.mouse.client_y = cy;
    ev.mouse.button = input::MouseButton::None;

    kernel::GuestTraceLogger::instance().log_input_provenance(ev, input::InputDeliveryStatus::Delivered);

    if (input_queue_) {
        input_queue_->push(ev);
    }
    if (event_callback_) {
        event_callback_(ev);
    }
}

void DisplayWindow::on_win32_mouse_button(input::MouseButton btn, bool is_down, int cx, int cy, int sx, int sy, std::uint64_t wparam, bool is_injected) {
    (void)wparam;
    const std::uint32_t cw = std::max(1u, window_width_);
    const std::uint32_t ch = std::max(1u, window_height_);
    const auto gx = static_cast<std::int32_t>(std::clamp(cx * static_cast<int>(config_.native_width) / static_cast<int>(cw),
                                                          0, static_cast<int>(config_.native_width - 1)));
    const auto gy = static_cast<std::int32_t>(std::clamp(cy * static_cast<int>(config_.native_height) / static_cast<int>(ch),
                                                          0, static_cast<int>(config_.native_height - 1)));

    input::InputEvent ev{};
    ev.type = is_down ? input::InputEventType::MouseDown : input::InputEventType::MouseUp;
    ev.timestamp_us = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    ev.provenance = is_injected ? input::InputProvenance::WindowsMessageInjected : input::InputProvenance::PhysicalWindowsInput;
    ev.win32_msg_id = is_down ? (btn == input::MouseButton::Left ? 0x0201 : (btn == input::MouseButton::Right ? 0x0204 : 0x0207))
                              : (btn == input::MouseButton::Left ? 0x0202 : (btn == input::MouseButton::Right ? 0x0205 : 0x0208));
    ev.mouse.x = gx;
    ev.mouse.y = gy;
    ev.mouse.screen_x = sx;
    ev.mouse.screen_y = sy;
    ev.mouse.client_x = cx;
    ev.mouse.client_y = cy;
    ev.mouse.button = btn;

    kernel::GuestTraceLogger::instance().log_input_provenance(ev, input::InputDeliveryStatus::Delivered);

    if (input_queue_) {
        input_queue_->push(ev);
    }
    if (event_callback_) {
        event_callback_(ev);
    }

    if (btn == input::MouseButton::Left) {
        inject_touch(static_cast<std::uint32_t>(std::max(0, gx)), static_cast<std::uint32_t>(std::max(0, gy)), is_down);
    }
}

void DisplayWindow::on_win32_mouse_wheel(int delta, int cx, int cy, int sx, int sy, std::uint64_t wparam, bool is_injected) {
    (void)wparam;
    const std::uint32_t cw = std::max(1u, window_width_);
    const std::uint32_t ch = std::max(1u, window_height_);
    const auto gx = static_cast<std::int32_t>(std::clamp(cx * static_cast<int>(config_.native_width) / static_cast<int>(cw),
                                                          0, static_cast<int>(config_.native_width - 1)));
    const auto gy = static_cast<std::int32_t>(std::clamp(cy * static_cast<int>(config_.native_height) / static_cast<int>(ch),
                                                          0, static_cast<int>(config_.native_height - 1)));

    input::InputEvent ev{};
    ev.type = input::InputEventType::MouseWheel;
    ev.timestamp_us = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    ev.provenance = is_injected ? input::InputProvenance::WindowsMessageInjected : input::InputProvenance::PhysicalWindowsInput;
    ev.win32_msg_id = 0x020A; // WM_MOUSEWHEEL
    ev.mouse.x = gx;
    ev.mouse.y = gy;
    ev.mouse.screen_x = sx;
    ev.mouse.screen_y = sy;
    ev.mouse.client_x = cx;
    ev.mouse.client_y = cy;
    ev.mouse.wheel_delta = delta;

    kernel::GuestTraceLogger::instance().log_input_provenance(ev, input::InputDeliveryStatus::Delivered);

    if (input_queue_) {
        input_queue_->push(ev);
    }
    if (event_callback_) {
        event_callback_(ev);
    }
}

void DisplayWindow::on_win32_mouse_leave() {
    input::InputEvent ev{};
    ev.type = input::InputEventType::WindowBlur;
    ev.timestamp_us = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    ev.provenance = input::InputProvenance::SyntheticHostEvent;
    ev.win32_msg_id = 0x02A3; // WM_MOUSELEAVE
    if (input_queue_) {
        input_queue_->push(ev);
    }
    if (event_callback_) {
        event_callback_(ev);
    }
}

void DisplayWindow::on_win32_key(std::uint64_t wparam, std::uint64_t lparam, bool is_injected) {
    const bool is_down = ((lparam & (1ULL << 31)) == 0);
    const bool is_repeat = is_down && ((lparam & (1ULL << 30)) != 0);
    std::uint32_t modifiers = 0;
#ifdef _WIN32
    if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= 1;
    if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= 2;
    if (GetKeyState(VK_MENU) & 0x8000) modifiers |= 4;
#endif

    input::InputEvent ev{};
    ev.type = is_down ? input::InputEventType::KeyPress : input::InputEventType::KeyRelease;
    ev.timestamp_us = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    ev.provenance = is_injected ? input::InputProvenance::WindowsMessageInjected : input::InputProvenance::PhysicalWindowsInput;
    ev.win32_msg_id = is_down ? 0x0100 : 0x0101; // WM_KEYDOWN : WM_KEYUP
    ev.keyboard.virtual_key = static_cast<std::uint32_t>(wparam);
    ev.keyboard.is_down = is_down;
    ev.keyboard.is_repeat = is_repeat;
    ev.keyboard.modifiers = modifiers;

    kernel::GuestTraceLogger::instance().log_input_provenance(ev, input::InputDeliveryStatus::Delivered);

    if (input_queue_) {
        input_queue_->push(ev);
    }
    if (event_callback_) {
        event_callback_(ev);
    }

    inject_key(static_cast<std::uint32_t>(wparam), is_down);
}

void DisplayWindow::on_win32_char(std::uint64_t wparam, std::uint64_t lparam, bool is_injected) {
    (void)lparam;
    const auto ch = static_cast<char32_t>(wparam);

    input::InputEvent ev{};
    ev.type = input::InputEventType::CharInput;
    ev.timestamp_us = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    ev.provenance = is_injected ? input::InputProvenance::WindowsMessageInjected : input::InputProvenance::PhysicalWindowsInput;
    ev.win32_msg_id = 0x0102; // WM_CHAR
    ev.keyboard.char_code = ch;

    kernel::GuestTraceLogger::instance().log_input_provenance(ev, input::InputDeliveryStatus::Delivered);

    if (input_queue_) {
        input_queue_->push(ev);
    }
    if (event_callback_) {
        event_callback_(ev);
    }

    inject_char(ch);
}

bool DisplayWindow::capture_client_area_bmp(const std::string& path) const {
#ifdef _WIN32
    if (!config_.headless && hwnd_) {
        HWND hwnd = reinterpret_cast<HWND>(hwnd_);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int client_w = rc.right - rc.left;
        int client_h = rc.bottom - rc.top;
        if (client_w > 0 && client_h > 0) {
            HDC window_dc = GetDC(hwnd);
            if (window_dc) {
                HDC mem_dc = CreateCompatibleDC(window_dc);
                if (mem_dc) {
                    HBITMAP mem_bmp = CreateCompatibleBitmap(window_dc, client_w, client_h);
                    if (mem_bmp) {
                        HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);
                        BitBlt(mem_dc, 0, 0, client_w, client_h, window_dc, 0, 0, SRCCOPY);

                        BITMAPINFO bi{};
                        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                        bi.bmiHeader.biWidth = client_w;
                        bi.bmiHeader.biHeight = client_h; // bottom-up
                        bi.bmiHeader.biPlanes = 1;
                        bi.bmiHeader.biBitCount = 32;
                        bi.bmiHeader.biCompression = BI_RGB;

                        const std::uint32_t row_stride = static_cast<std::uint32_t>(client_w * 4);
                        const std::uint32_t img_size = row_stride * static_cast<std::uint32_t>(client_h);
                        std::vector<std::uint8_t> pixels(img_size, 0);

                        GetDIBits(mem_dc, mem_bmp, 0, client_h, pixels.data(), &bi, DIB_RGB_COLORS);

                        SelectObject(mem_dc, old_bmp);
                        DeleteObject(mem_bmp);
                        DeleteDC(mem_dc);
                        ReleaseDC(hwnd, window_dc);

                        std::ofstream out(path, std::ios::binary);
                        if (out) {
                            const std::uint32_t file_size = 14 + 40 + img_size;
                            std::uint8_t file_header[14] = {0};
                            file_header[0] = 'B';
                            file_header[1] = 'M';
                            std::memcpy(&file_header[2], &file_size, 4);
                            std::uint32_t offset = 54;
                            std::memcpy(&file_header[10], &offset, 4);
                            out.write(reinterpret_cast<const char*>(file_header), 14);

                            std::uint8_t info_header[40] = {0};
                            std::uint32_t bi_size = 40;
                            std::memcpy(&info_header[0], &bi_size, 4);
                            std::memcpy(&info_header[4], &client_w, 4);
                            std::memcpy(&info_header[8], &client_h, 4);
                            std::uint16_t planes = 1;
                            std::memcpy(&info_header[12], &planes, 2);
                            std::uint16_t bpp = 32;
                            std::memcpy(&info_header[14], &bpp, 2);
                            std::memcpy(&info_header[20], &img_size, 4);
                            out.write(reinterpret_cast<const char*>(info_header), 40);

                            out.write(reinterpret_cast<const char*>(pixels.data()), img_size);
                            return true;
                        }
                    } else {
                        DeleteDC(mem_dc);
                        ReleaseDC(hwnd, window_dc);
                    }
                } else {
                    ReleaseDC(hwnd, window_dc);
                }
            }
        }
    }
#endif

    // Headless fallback: produce scaled client-area BMP from backbuffer
    std::lock_guard<std::mutex> lock(lock_);
    if (backbuffer_.empty()) return false;

    const std::uint32_t cw = window_width_;
    const std::uint32_t ch = window_height_;
    if (cw == 0 || ch == 0) return false;

    const std::uint32_t row_stride = cw * 4;
    const std::uint32_t img_size = row_stride * ch;
    std::vector<std::uint8_t> scaled_pixels(img_size, 0);

    const std::uint32_t nw = config_.native_width;
    const std::uint32_t nh = config_.native_height;

    for (std::uint32_t cy = 0; cy < ch; ++cy) {
        std::uint32_t sy = ((ch - 1 - cy) * nh) / ch;
        if (sy >= nh) sy = nh - 1;
        for (std::uint32_t cx = 0; cx < cw; ++cx) {
            std::uint32_t sx = (cx * nw) / cw;
            if (sx >= nw) sx = nw - 1;

            const std::size_t src_idx = (static_cast<std::size_t>(sy) * nw + sx) * 4;
            const std::size_t dst_idx = (static_cast<std::size_t>(cy) * cw + cx) * 4;

            scaled_pixels[dst_idx + 0] = backbuffer_[src_idx + 0];
            scaled_pixels[dst_idx + 1] = backbuffer_[src_idx + 1];
            scaled_pixels[dst_idx + 2] = backbuffer_[src_idx + 2];
            scaled_pixels[dst_idx + 3] = backbuffer_[src_idx + 3];
        }
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    const std::uint32_t file_size = 14 + 40 + img_size;
    std::uint8_t file_header[14] = {0};
    file_header[0] = 'B';
    file_header[1] = 'M';
    std::memcpy(&file_header[2], &file_size, 4);
    std::uint32_t offset = 54;
    std::memcpy(&file_header[10], &offset, 4);
    out.write(reinterpret_cast<const char*>(file_header), 14);

    std::uint8_t info_header[40] = {0};
    std::uint32_t bi_size = 40;
    std::memcpy(&info_header[0], &bi_size, 4);
    std::memcpy(&info_header[4], &cw, 4);
    std::memcpy(&info_header[8], &ch, 4);
    std::uint16_t planes = 1;
    std::memcpy(&info_header[12], &planes, 2);
    std::uint16_t bpp = 32;
    std::memcpy(&info_header[14], &bpp, 2);
    std::memcpy(&info_header[20], &img_size, 4);
    out.write(reinterpret_cast<const char*>(info_header), 40);

    out.write(reinterpret_cast<const char*>(scaled_pixels.data()), img_size);
    return true;
}

bool DisplayWindow::save_screenshot_bmp(const std::string& path) const {
    std::lock_guard<std::mutex> lock(lock_);
    if (backbuffer_.empty()) return false;

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    const std::uint32_t w = config_.native_width;
    const std::uint32_t h = config_.native_height;
    const std::uint32_t row_stride = w * 4;
    const std::uint32_t image_size = row_stride * h;
    const std::uint32_t file_size = 14 + 40 + image_size;

    // 1. BITMAPFILEHEADER (14 bytes)
    std::uint8_t file_header[14] = {0};
    file_header[0] = 'B';
    file_header[1] = 'M';
    std::memcpy(&file_header[2], &file_size, 4);
    std::uint32_t offset = 54;
    std::memcpy(&file_header[10], &offset, 4);
    out.write(reinterpret_cast<const char*>(file_header), 14);

    // 2. BITMAPINFOHEADER (40 bytes)
    std::uint8_t info_header[40] = {0};
    std::uint32_t bi_size = 40;
    std::memcpy(&info_header[0], &bi_size, 4);
    std::memcpy(&info_header[4], &w, 4);
    std::memcpy(&info_header[8], &h, 4); // Bottom-up
    std::uint16_t planes = 1;
    std::memcpy(&info_header[12], &planes, 2);
    std::uint16_t bpp = 32;
    std::memcpy(&info_header[14], &bpp, 2);
    std::memcpy(&info_header[20], &image_size, 4);
    out.write(reinterpret_cast<const char*>(info_header), 40);

    // 3. Pixels (BMP bottom-up order)
    for (int y = static_cast<int>(h) - 1; y >= 0; --y) {
        const auto* row = backbuffer_.data() + static_cast<std::size_t>(y) * row_stride;
        out.write(reinterpret_cast<const char*>(row), row_stride);
    }

    return true;
}

} // namespace winios::ui

#include "ui/display_window.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>

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
            // Forward to blit
            RECT rc;
            GetClientRect(hwnd, &rc);
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = static_cast<LONG>(win->config().native_width);
            bi.bmiHeader.biHeight = -static_cast<LONG>(win->config().native_height); // Top-down
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;

            SetStretchBltMode(hdc, HALFTONE);
            StretchDIBits(hdc,
                          0, 0, rc.right - rc.left, rc.bottom - rc.top,
                          0, 0, win->config().native_width, win->config().native_height,
                          win->last_touch().is_down ? win->config().title.data() : nullptr, // checked in lock
                          &bi, DIB_RGB_COLORS, SRCCOPY);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_MOUSEMOVE:
    case WM_LBUTTONUP: {
        if (win) {
            const bool is_down = (msg != WM_LBUTTONUP) && ((wParam & MK_LBUTTON) != 0);
            if (msg == WM_MOUSEMOVE && !is_down) break;

            const int cx = GET_X_LPARAM(lParam);
            const int cy = GET_Y_LPARAM(lParam);

            RECT rc;
            GetClientRect(hwnd, &rc);
            const int cw = std::max(1, static_cast<int>(rc.right - rc.left));
            const int ch = std::max(1, static_cast<int>(rc.bottom - rc.top));

            const auto tx = static_cast<std::uint32_t>(
                std::clamp(cx * static_cast<int>(win->config().native_width) / cw,
                           0, static_cast<int>(win->config().native_width - 1)));
            const auto ty = static_cast<std::uint32_t>(
                std::clamp(cy * static_cast<int>(win->config().native_height) / ch,
                           0, static_cast<int>(win->config().native_height - 1)));

            win->inject_touch(tx, ty, is_down);
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
        return true;
    }

    SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    hwnd_ = hwnd;
    hdc_ = GetDC(hwnd);
    is_open_ = true;
    return true;
#else
    config_.headless = true;
    is_open_ = true;
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

bool DisplayWindow::present_frame(const device::AppleDisplay& display) {
    return present_frame(display.raw_framebuffer_data(), display.width(), display.height());
}

bool DisplayWindow::present_frame(const std::uint8_t* raw_argb, std::uint32_t width, std::uint32_t height) {
    if (!raw_argb || width == 0 || height == 0) return false;
    if (!is_open_) {
        if (!create()) return false;
    }

    {
        std::lock_guard<std::mutex> lock(lock_);
        const std::size_t incoming_bytes = static_cast<std::size_t>(width) * height * 4;
        if (backbuffer_.size() != incoming_bytes) {
            backbuffer_.resize(incoming_bytes);
        }
        std::memcpy(backbuffer_.data(), raw_argb, incoming_bytes);
        total_frames_++;
    }

#ifdef _WIN32
    if (!config_.headless && hwnd_ && hdc_) {
        HWND hwnd = reinterpret_cast<HWND>(hwnd_);
        HDC hdc = reinterpret_cast<HDC>(hdc_);

        RECT rc;
        GetClientRect(hwnd, &rc);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = static_cast<LONG>(width);
        bi.bmiHeader.biHeight = -static_cast<LONG>(height); // Top-down DIB
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        SetStretchBltMode(hdc, HALFTONE);
        std::lock_guard<std::mutex> lock(lock_);
        StretchDIBits(hdc,
                      0, 0, rc.right - rc.left, rc.bottom - rc.top,
                      0, 0, width, height,
                      backbuffer_.data(),
                      &bi, DIB_RGB_COLORS, SRCCOPY);

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

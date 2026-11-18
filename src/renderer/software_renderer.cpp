#include "renderer/software_renderer.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace winios::renderer {

SoftwareRenderer::~SoftwareRenderer() {
    shutdown();
}

bool SoftwareRenderer::initialize(void* native_window_handle, std::uint32_t width, std::uint32_t height) {
    std::lock_guard<std::mutex> guard(lock_);
    hwnd_ = native_window_handle;
    width_ = width;
    height_ = height;
    backbuffer_.assign(static_cast<std::size_t>(width_) * height_ * 4, 0);

    diag_.backend_name = "SoftwareReference";
    diag_.is_hardware_accelerated = false;
    diag_.width = width_;
    diag_.height = height_;
    diag_.total_frames_submitted = 0;
    diag_.total_frames_presented = 0;
    diag_.dropped_frames = 0;
    diag_.device_loss_events = 0;
    diag_.recovery_events = 0;

    return true;
}

void SoftwareRenderer::shutdown() {
    std::lock_guard<std::mutex> guard(lock_);
    hwnd_ = nullptr;
    width_ = 0;
    height_ = 0;
    backbuffer_.clear();
}

bool SoftwareRenderer::resize(std::uint32_t width, std::uint32_t height) {
    std::lock_guard<std::mutex> guard(lock_);
    if (width == 0 || height == 0) return false;
    width_ = width;
    height_ = height;
    backbuffer_.assign(static_cast<std::size_t>(width_) * height_ * 4, 0);
    diag_.width = width_;
    diag_.height = height_;
    return true;
}

bool SoftwareRenderer::begin_frame() {
    std::lock_guard<std::mutex> guard(lock_);
    if (simulated_loss_) {
        diag_.dropped_frames++;
        return false;
    }
    in_frame_ = true;
    return true;
}

bool SoftwareRenderer::submit_frame(const std::uint8_t* argb_data, std::uint32_t width, std::uint32_t height, std::uint32_t stride) {
    std::lock_guard<std::mutex> guard(lock_);
    if (!in_frame_ || simulated_loss_ || !argb_data || width == 0 || height == 0) {
        diag_.dropped_frames++;
        return false;
    }

    diag_.total_frames_submitted++;

    // Nearest-neighbor / linear copy into backbuffer
    const std::uint32_t dst_w = width_;
    const std::uint32_t dst_h = height_;
    if (dst_w == 0 || dst_h == 0) return false;

    if (dst_w == width && dst_h == height && stride == (width * 4)) {
        std::memcpy(backbuffer_.data(), argb_data, backbuffer_.size());
    } else {
        // Stretch blit
        for (std::uint32_t dy = 0; dy < dst_h; ++dy) {
            std::uint32_t sy = (dy * height) / dst_h;
            if (sy >= height) sy = height - 1;

            const std::uint8_t* src_row = argb_data + (static_cast<std::size_t>(sy) * stride);
            std::uint8_t* dst_row = backbuffer_.data() + (static_cast<std::size_t>(dy) * dst_w * 4);

            for (std::uint32_t dx = 0; dx < dst_w; ++dx) {
                std::uint32_t sx = (dx * width) / dst_w;
                if (sx >= width) sx = width - 1;

                const std::uint8_t* sp = src_row + (static_cast<std::size_t>(sx) * 4);
                std::uint8_t* dp = dst_row + (static_cast<std::size_t>(dx) * 4);
                dp[0] = sp[0];
                dp[1] = sp[1];
                dp[2] = sp[2];
                dp[3] = sp[3];
            }
        }
    }

    return true;
}

bool SoftwareRenderer::end_frame(bool vsync) {
    (void)vsync;
    auto t0 = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> guard(lock_);
        if (!in_frame_ || simulated_loss_) {
            in_frame_ = false;
            return false;
        }
        in_frame_ = false;

#ifdef _WIN32
        if (hwnd_) {
            HWND hwnd = reinterpret_cast<HWND>(hwnd_);
            HDC hdc = GetDC(hwnd);
            if (hdc) {
                BITMAPINFO bi{};
                bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bi.bmiHeader.biWidth = static_cast<LONG>(width_);
                bi.bmiHeader.biHeight = -static_cast<LONG>(height_);
                bi.bmiHeader.biPlanes = 1;
                bi.bmiHeader.biBitCount = 32;
                bi.bmiHeader.biCompression = BI_RGB;

                RECT rc;
                GetClientRect(hwnd, &rc);
                int cw = rc.right - rc.left;
                int ch = rc.bottom - rc.top;

                StretchDIBits(hdc, 0, 0, cw, ch,
                              0, 0, width_, height_,
                              backbuffer_.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
                ReleaseDC(hwnd, hdc);
            }
        }
#endif

        diag_.total_frames_presented++;
    }

    auto t1 = std::chrono::steady_clock::now();
    double elapsed_us = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

    std::lock_guard<std::mutex> guard(lock_);
    if (diag_.total_frames_presented == 1) {
        diag_.average_present_time_us = elapsed_us;
    } else {
        diag_.average_present_time_us = (diag_.average_present_time_us * 0.9) + (elapsed_us * 0.1);
    }
    if (elapsed_us > diag_.max_present_time_us) {
        diag_.max_present_time_us = elapsed_us;
    }

    return true;
}

RendererDiagnostics SoftwareRenderer::diagnostics() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return diag_;
}

void SoftwareRenderer::simulate_device_loss() {
    std::lock_guard<std::mutex> guard(lock_);
    simulated_loss_ = true;
    diag_.device_loss_events++;
}

bool SoftwareRenderer::recover_from_device_loss() {
    std::lock_guard<std::mutex> guard(lock_);
    simulated_loss_ = false;
    diag_.recovery_events++;
    return true;
}

} // namespace winios::renderer

#include "renderer/d3d11_renderer.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#endif

namespace winios::renderer {

D3D11Renderer::D3D11Renderer() {
    diag_.backend_name = "Direct3D11";
    diag_.is_hardware_accelerated = false;
}

D3D11Renderer::~D3D11Renderer() {
    shutdown();
}

bool D3D11Renderer::init_d3d11_pipeline() {
#ifdef _WIN32
    if (!hwnd_) {
        return false;
    }

    HWND hwnd = reinterpret_cast<HWND>(hwnd_);
    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = width_;
    scd.BufferDesc.Height = height_;
    scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL obtained_level;

    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain* sc = nullptr;

    UINT create_flags = 0;
#ifdef _DEBUG
    create_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        create_flags,
        feature_levels,
        static_cast<UINT>(std::size(feature_levels)),
        D3D11_SDK_VERSION,
        &scd,
        &sc,
        &dev,
        &obtained_level,
        &ctx);

    if (FAILED(hr)) {
        // Fall back to WARP driver before pure software
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0,
            feature_levels,
            static_cast<UINT>(std::size(feature_levels)),
            D3D11_SDK_VERSION,
            &scd,
            &sc,
            &dev,
            &obtained_level,
            &ctx);
    }

    if (FAILED(hr) || !dev || !ctx || !sc) {
        if (dev) dev->Release();
        if (ctx) ctx->Release();
        if (sc) sc->Release();
        return false;
    }

    d3d_device_ = dev;
    d3d_context_ = ctx;
    dxgi_swapchain_ = sc;

    // Create Backbuffer Render Target View
    ID3D11Texture2D* backbuffer_tex = nullptr;
    hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backbuffer_tex));
    if (SUCCEEDED(hr) && backbuffer_tex) {
        ID3D11RenderTargetView* rtv = nullptr;
        hr = dev->CreateRenderTargetView(backbuffer_tex, nullptr, &rtv);
        backbuffer_tex->Release();
        if (SUCCEEDED(hr)) {
            render_target_view_ = rtv;
        }
    }

    // Create dynamic/staging texture for frame upload
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width_;
    td.Height = height_;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DYNAMIC;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    ID3D11Texture2D* staging = nullptr;
    hr = dev->CreateTexture2D(&td, nullptr, &staging);
    if (SUCCEEDED(hr)) {
        staging_texture_ = staging;
    }

    hardware_active_ = true;
    diag_.is_hardware_accelerated = true;
    diag_.backend_name = "Direct3D11";
    return true;
#else
    return false;
#endif
}

void D3D11Renderer::release_d3d11_resources() {
#ifdef _WIN32
    if (render_target_view_) {
        reinterpret_cast<ID3D11RenderTargetView*>(render_target_view_)->Release();
        render_target_view_ = nullptr;
    }
    if (staging_texture_) {
        reinterpret_cast<ID3D11Texture2D*>(staging_texture_)->Release();
        staging_texture_ = nullptr;
    }
    if (dxgi_swapchain_) {
        reinterpret_cast<IDXGISwapChain*>(dxgi_swapchain_)->Release();
        dxgi_swapchain_ = nullptr;
    }
    if (d3d_context_) {
        reinterpret_cast<ID3D11DeviceContext*>(d3d_context_)->Release();
        d3d_context_ = nullptr;
    }
    if (d3d_device_) {
        reinterpret_cast<ID3D11Device*>(d3d_device_)->Release();
        d3d_device_ = nullptr;
    }
#endif
    hardware_active_ = false;
}

bool D3D11Renderer::initialize(void* native_window_handle, std::uint32_t width, std::uint32_t height) {
    std::lock_guard<std::mutex> guard(lock_);
    hwnd_ = native_window_handle;
    width_ = width;
    height_ = height;
    device_lost_ = false;

    diag_.width = width_;
    diag_.height = height_;
    diag_.total_frames_submitted = 0;
    diag_.total_frames_presented = 0;
    diag_.dropped_frames = 0;
    diag_.device_loss_events = 0;
    diag_.recovery_events = 0;

    bool d3d_ok = init_d3d11_pipeline();
    if (!d3d_ok) {
        // Fall back to software renderer gracefully
        fallback_renderer_ = std::make_unique<SoftwareRenderer>();
        fallback_renderer_->initialize(native_window_handle, width, height);
        diag_.backend_name = "Direct3D11 (SoftwareFallback)";
        diag_.is_hardware_accelerated = false;
    }

    return true;
}

void D3D11Renderer::shutdown() {
    std::lock_guard<std::mutex> guard(lock_);
    release_d3d11_resources();
    if (fallback_renderer_) {
        fallback_renderer_->shutdown();
        fallback_renderer_.reset();
    }
    hwnd_ = nullptr;
    width_ = 0;
    height_ = 0;
}

bool D3D11Renderer::resize(std::uint32_t width, std::uint32_t height) {
    std::lock_guard<std::mutex> guard(lock_);
    if (width == 0 || height == 0) return false;
    width_ = width;
    height_ = height;
    diag_.width = width_;
    diag_.height = height_;

    if (hardware_active_) {
        release_d3d11_resources();
        init_d3d11_pipeline();
    }
    if (fallback_renderer_) {
        fallback_renderer_->resize(width, height);
    }
    return true;
}

bool D3D11Renderer::begin_frame() {
    std::lock_guard<std::mutex> guard(lock_);
    if (device_lost_) {
        diag_.dropped_frames++;
        return false;
    }
    if (fallback_renderer_) {
        return fallback_renderer_->begin_frame();
    }
    in_frame_ = true;
    return true;
}

bool D3D11Renderer::submit_frame(const std::uint8_t* argb_data, std::uint32_t width, std::uint32_t height, std::uint32_t stride) {
    std::lock_guard<std::mutex> guard(lock_);
    if (device_lost_ || !argb_data || width == 0 || height == 0) {
        diag_.dropped_frames++;
        return false;
    }

    if (fallback_renderer_) {
        return fallback_renderer_->submit_frame(argb_data, width, height, stride);
    }

#ifdef _WIN32
    if (!hardware_active_ || !d3d_context_ || !staging_texture_) {
        diag_.dropped_frames++;
        return false;
    }

    auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(d3d_context_);
    auto* tex = reinterpret_cast<ID3D11Texture2D*>(staging_texture_);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = ctx->Map(tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        std::uint32_t copy_h = std::min(height, height_);
        std::uint32_t copy_w_bytes = std::min(width, width_) * 4;

        for (std::uint32_t y = 0; y < copy_h; ++y) {
            const std::uint8_t* src_row = argb_data + (static_cast<std::size_t>(y) * stride);
            std::uint8_t* dst_row = reinterpret_cast<std::uint8_t*>(mapped.pData) + (static_cast<std::size_t>(y) * mapped.RowPitch);
            std::memcpy(dst_row, src_row, copy_w_bytes);
        }
        ctx->Unmap(tex, 0);
        diag_.total_frames_submitted++;
        return true;
    }
#endif

    diag_.dropped_frames++;
    return false;
}

bool D3D11Renderer::end_frame(bool vsync) {
    auto t0 = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> guard(lock_);
        if (device_lost_) {
            in_frame_ = false;
            return false;
        }

        if (fallback_renderer_) {
            bool ok = fallback_renderer_->end_frame(vsync);
            diag_.total_frames_presented = fallback_renderer_->diagnostics().total_frames_presented;
            return ok;
        }

        in_frame_ = false;

#ifdef _WIN32
        if (hardware_active_ && dxgi_swapchain_) {
            auto* sc = reinterpret_cast<IDXGISwapChain*>(dxgi_swapchain_);
            HRESULT hr = sc->Present(vsync ? 1 : 0, 0);
            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
                simulate_device_loss();
                return false;
            }
            diag_.total_frames_presented++;
        }
#endif
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

bool D3D11Renderer::is_hardware_accelerated() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return hardware_active_;
}

std::string D3D11Renderer::backend_name() const {
    std::lock_guard<std::mutex> guard(lock_);
    return diag_.backend_name;
}

RendererDiagnostics D3D11Renderer::diagnostics() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    if (fallback_renderer_) {
        auto d = fallback_renderer_->diagnostics();
        d.backend_name = diag_.backend_name;
        d.device_loss_events = diag_.device_loss_events;
        d.recovery_events = diag_.recovery_events;
        return d;
    }
    return diag_;
}

void D3D11Renderer::simulate_device_loss() {
    std::lock_guard<std::mutex> guard(lock_);
    device_lost_ = true;
    diag_.device_loss_events++;
    release_d3d11_resources();
    if (fallback_renderer_) {
        fallback_renderer_->simulate_device_loss();
    }
}

bool D3D11Renderer::recover_from_device_loss() {
    std::lock_guard<std::mutex> guard(lock_);
    device_lost_ = false;

    bool d3d_ok = init_d3d11_pipeline();
    if (d3d_ok) {
        diag_.recovery_events++;
        if (fallback_renderer_) {
            fallback_renderer_.reset();
        }
        return true;
    }

    // Direct3D 11 reinitialization failed: activate software fallback seamlessly
    if (!fallback_renderer_) {
        fallback_renderer_ = std::make_unique<SoftwareRenderer>();
        fallback_renderer_->initialize(hwnd_, width_, height_);
    } else {
        fallback_renderer_->recover_from_device_loss();
    }
    diag_.recovery_events++;
    diag_.backend_name = "Direct3D11 (SoftwareFallback)";
    diag_.is_hardware_accelerated = false;
    return true;
}

bool D3D11Renderer::is_using_software_fallback() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return fallback_renderer_ != nullptr;
}

} // namespace winios::renderer

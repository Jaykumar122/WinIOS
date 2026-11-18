#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "renderer/renderer.h"
#include "renderer/software_renderer.h"

namespace winios::renderer {

class D3D11Renderer : public IRenderer {
public:
    D3D11Renderer();
    ~D3D11Renderer() override;

    bool initialize(void* native_window_handle, std::uint32_t width, std::uint32_t height) override;
    void shutdown() override;
    bool resize(std::uint32_t width, std::uint32_t height) override;

    bool begin_frame() override;
    bool submit_frame(const std::uint8_t* argb_data, std::uint32_t width, std::uint32_t height, std::uint32_t stride) override;
    bool end_frame(bool vsync = true) override;

    [[nodiscard]] bool is_hardware_accelerated() const noexcept override;
    [[nodiscard]] std::string backend_name() const override;
    [[nodiscard]] RendererDiagnostics diagnostics() const noexcept override;

    void simulate_device_loss() override;
    bool recover_from_device_loss() override;

    [[nodiscard]] bool is_using_software_fallback() const noexcept;

private:
    bool init_d3d11_pipeline();
    void release_d3d11_resources();

    void*                       hwnd_{nullptr};
    std::uint32_t               width_{0};
    std::uint32_t               height_{0};
    bool                        hardware_active_{false};
    bool                        in_frame_{false};
    bool                        device_lost_{false};

    // D3D11 interfaces stored as opaque pointers to avoid leaking d3d11.h across headers
    void*                       d3d_device_{nullptr};
    void*                       d3d_context_{nullptr};
    void*                       dxgi_swapchain_{nullptr};
    void*                       render_target_view_{nullptr};
    void*                       staging_texture_{nullptr};

    std::unique_ptr<SoftwareRenderer> fallback_renderer_;
    mutable std::mutex          lock_;
    RendererDiagnostics         diag_{};
};

} // namespace winios::renderer

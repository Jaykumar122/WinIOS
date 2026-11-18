#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "renderer/renderer.h"

namespace winios::renderer {

class SoftwareRenderer : public IRenderer {
public:
    SoftwareRenderer() = default;
    ~SoftwareRenderer() override;

    bool initialize(void* native_window_handle, std::uint32_t width, std::uint32_t height) override;
    void shutdown() override;
    bool resize(std::uint32_t width, std::uint32_t height) override;

    bool begin_frame() override;
    bool submit_frame(const std::uint8_t* argb_data, std::uint32_t width, std::uint32_t height, std::uint32_t stride) override;
    bool end_frame(bool vsync = true) override;

    [[nodiscard]] bool is_hardware_accelerated() const noexcept override { return false; }
    [[nodiscard]] std::string backend_name() const override { return "SoftwareReference"; }
    [[nodiscard]] RendererDiagnostics diagnostics() const noexcept override;

    void simulate_device_loss() override;
    bool recover_from_device_loss() override;

    [[nodiscard]] const std::vector<std::uint8_t>& backbuffer() const noexcept { return backbuffer_; }

private:
    void*                     hwnd_{nullptr};
    std::uint32_t             width_{0};
    std::uint32_t             height_{0};
    bool                      in_frame_{false};
    bool                      simulated_loss_{false};
    mutable std::mutex        lock_;
    std::vector<std::uint8_t> backbuffer_;
    RendererDiagnostics       diag_{};
};

} // namespace winios::renderer

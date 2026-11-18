#pragma once

#include <cstdint>
#include <string>

namespace winios::renderer {

struct RendererDiagnostics {
    std::string   backend_name{"SoftwareReference"};
    bool          is_hardware_accelerated{false};
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint64_t total_frames_submitted{0};
    std::uint64_t total_frames_presented{0};
    std::uint64_t dropped_frames{0};
    std::uint64_t device_loss_events{0};
    std::uint64_t recovery_events{0};
    double        average_present_time_us{0.0};
    double        max_present_time_us{0.0};
};

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual bool initialize(void* native_window_handle, std::uint32_t width, std::uint32_t height) = 0;
    virtual void shutdown() = 0;
    virtual bool resize(std::uint32_t width, std::uint32_t height) = 0;

    virtual bool begin_frame() = 0;
    virtual bool submit_frame(const std::uint8_t* argb_data, std::uint32_t width, std::uint32_t height, std::uint32_t stride) = 0;
    virtual bool end_frame(bool vsync = true) = 0;

    [[nodiscard]] virtual bool is_hardware_accelerated() const noexcept = 0;
    [[nodiscard]] virtual std::string backend_name() const = 0;
    [[nodiscard]] virtual RendererDiagnostics diagnostics() const noexcept = 0;

    // Device loss simulation & recovery testing
    virtual void simulate_device_loss() = 0;
    virtual bool recover_from_device_loss() = 0;
};

} // namespace winios::renderer

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "device/virtual_device.h"

namespace winios::device {

// -----------------------------------------------------------------------------
// AppleDisplay: iPhone 15 Super Retina XDR Framebuffer Controller
// Emulates the display controller for iPhone 15 (iPhone15,4):
// - Resolution: 1179 x 2556 pixels, 32-bit ARGB
// - RowBytes: 4716
// - Total Framebuffer Size: ~11.5 MB (12,058,656 bytes)
// Provides linear framebuffer management, test pattern generation, pixel access,
// and export for Windows DirectX / GDI surface presentation.
// -----------------------------------------------------------------------------
class AppleDisplay : public VirtualDevice {
public:
    static constexpr std::uint64_t kDefaultDisplayBase = 0x0B000000ULL;
    static constexpr std::uint64_t kDefaultDisplaySize = 0x00010000ULL; // 64 KB MMIO window
    static constexpr std::uint32_t kDefaultWidth       = 1179u;
    static constexpr std::uint32_t kDefaultHeight      = 2556u;
    static constexpr std::uint32_t kDefaultDepth       = 32u;
    static constexpr std::uint32_t kDefaultRowBytes    = 1179u * 4u;
    static constexpr std::size_t   kFramebufferBytes   = static_cast<std::size_t>(kDefaultWidth) * kDefaultHeight * 4;

    // MMIO Register Offsets
    static constexpr std::uint64_t kRegWidth        = 0x0000u; // Width in pixels
    static constexpr std::uint64_t kRegHeight       = 0x0004u; // Height in pixels
    static constexpr std::uint64_t kRegDepth        = 0x0008u; // Color depth (32-bit)
    static constexpr std::uint64_t kRegRowBytes     = 0x000Cu; // Bytes per scanline
    static constexpr std::uint64_t kRegFbBaseLow    = 0x0010u; // Physical Framebuffer Base [31:0]
    static constexpr std::uint64_t kRegFbBaseHigh   = 0x0014u; // Physical Framebuffer Base [63:32]
    static constexpr std::uint64_t kRegStatus       = 0x0018u; // Display Controller Status (1 = Active)
    static constexpr std::uint64_t kRegControl      = 0x0020u; // Enable / Blank Control

    explicit AppleDisplay(std::uint32_t device_id = 0x44495350u, // 'DISP'
                          std::uint64_t mmio_base = kDefaultDisplayBase,
                          std::uint64_t mmio_size = kDefaultDisplaySize,
                          std::uint64_t fb_phys_base = 0x7C000000ULL);

    ~AppleDisplay() override = default;

    // VirtualDevice Interface
    std::uint64_t read_register(std::uint64_t offset, unsigned width) override;
    void write_register(std::uint64_t offset, std::uint64_t value, unsigned width) override;
    void reset() noexcept override;

    std::vector<std::uint8_t> serialize_state() const override;
    bool deserialize_state(const std::uint8_t* data, std::size_t size) override;

    // Display Geometry
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] std::uint32_t depth() const noexcept { return depth_; }
    [[nodiscard]] std::uint32_t row_bytes() const noexcept { return row_bytes_; }
    [[nodiscard]] std::size_t framebuffer_size() const noexcept { return fb_memory_.size(); }
    [[nodiscard]] std::uint64_t fb_phys_base() const noexcept { return fb_phys_base_; }
    void set_fb_phys_base(std::uint64_t base) noexcept { fb_phys_base_ = base; }

    // Pixel Buffer Access
    [[nodiscard]] const std::uint8_t* raw_framebuffer_data() const noexcept {
        return fb_memory_.data();
    }
    [[nodiscard]] std::uint8_t* raw_framebuffer_data() noexcept {
        return fb_memory_.data();
    }

    void set_pixel(std::uint32_t x, std::uint32_t y, std::uint32_t argb) noexcept;
    [[nodiscard]] std::uint32_t get_pixel(std::uint32_t x, std::uint32_t y) const noexcept;
    void clear(std::uint32_t argb = 0x00000000u) noexcept;

    // Test Pattern & Boot Visuals
    void render_boot_splash();

    // Export Helpers
    [[nodiscard]] std::vector<std::uint8_t> export_ppm() const;

private:
    mutable std::mutex lock_;

    std::uint32_t width_{kDefaultWidth};
    std::uint32_t height_{kDefaultHeight};
    std::uint32_t depth_{kDefaultDepth};
    std::uint32_t row_bytes_{kDefaultRowBytes};
    std::uint64_t fb_phys_base_{0x7C000000ULL};
    bool display_enabled_{true};

    std::vector<std::uint8_t> fb_memory_;
};

} // namespace winios::device

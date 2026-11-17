#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include "graphics/window_server.h"

namespace winios::graphics {

// -----------------------------------------------------------------------------
// Display Orientation
// -----------------------------------------------------------------------------
enum class DisplayOrientation : std::uint8_t {
    Portrait = 0,
    LandscapeLeft = 1,
    PortraitUpsideDown = 2,
    LandscapeRight = 3
};

[[nodiscard]] constexpr bool is_landscape(DisplayOrientation orient) noexcept {
    return orient == DisplayOrientation::LandscapeLeft || orient == DisplayOrientation::LandscapeRight;
}

// -----------------------------------------------------------------------------
// Display Error Codes
// -----------------------------------------------------------------------------
enum class DisplayError : std::uint32_t {
    Success = 0,
    InvalidDimensions = 1,
    IntegerOverflow = 2,
    AllocationExceeded = 3,
    UnsupportedFormat = 4,
    InvalidStride = 5,
    OutOfBoundsAccess = 6,
    BufferTooSmall = 7,
    InvalidPointer = 8
};

// -----------------------------------------------------------------------------
// DisplayConfiguration: Complete Geometry & Timing Description
// -----------------------------------------------------------------------------
struct DisplayConfiguration {
    static constexpr std::uint32_t kDefaultWidth = 1179u;
    static constexpr std::uint32_t kDefaultHeight = 2556u;
    static constexpr std::uint32_t kDefaultRefreshIntervalUs = 16666u; // ~60 Hz (16.666 ms)
    static constexpr float kDefaultScale = 3.0f; // iPhone 15 Super Retina XDR
    static constexpr std::uint64_t kDefaultMaxAllocation = 64ULL * 1024ULL * 1024ULL; // 64 MB
    static constexpr std::uint64_t kDefaultGuestPhysAddr = 0x240000000ULL;

    std::uint32_t width{kDefaultWidth};
    std::uint32_t height{kDefaultHeight};
    DisplayOrientation orientation{DisplayOrientation::Portrait};
    PixelFormat pixel_format{PixelFormat::B8G8R8A8_UNORM};
    std::uint32_t bytes_per_pixel{4u};
    std::uint32_t row_stride{kDefaultWidth * 4u};
    std::uint32_t refresh_interval_us{kDefaultRefreshIntervalUs};
    float display_scale{kDefaultScale};
    std::uint64_t max_allocation_size{kDefaultMaxAllocation};
    std::uint64_t guest_physical_address{kDefaultGuestPhysAddr};

    [[nodiscard]] bool validate() const noexcept {
        return validate_error() == DisplayError::Success;
    }

    [[nodiscard]] DisplayError validate_error() const noexcept;
    [[nodiscard]] std::uint64_t total_framebuffer_bytes() const noexcept;

    // Swap geometry to match orientation
    void set_orientation(DisplayOrientation orient) noexcept;
};

// -----------------------------------------------------------------------------
// RasterPrimitives: Channel-safe Blending, Clipping & Format Conversions
// -----------------------------------------------------------------------------
class RasterPrimitives {
public:
    // RGB565 packing/unpacking
    [[nodiscard]] static constexpr std::uint16_t pack_rgb565(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
        return static_cast<std::uint16_t>(((static_cast<std::uint16_t>(r) & 0xF8u) << 8) |
                                          ((static_cast<std::uint16_t>(g) & 0xFCu) << 3) |
                                          ((static_cast<std::uint16_t>(b) & 0xF8u) >> 3));
    }

    static constexpr void unpack_rgb565(std::uint16_t rgb, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) noexcept {
        r = static_cast<std::uint8_t>((rgb >> 8) & 0xF8u);
        g = static_cast<std::uint8_t>((rgb >> 3) & 0xFCu);
        b = static_cast<std::uint8_t>((rgb << 3) & 0xF8u);
        // Expand 5/6 bits to 8 bits cleanly
        r |= static_cast<std::uint8_t>(r >> 5);
        g |= static_cast<std::uint8_t>(g >> 6);
        b |= static_cast<std::uint8_t>(b >> 5);
    }

    // Alpha blending: Porter-Duff Source-Over
    // Cout = (Csrc * Asrc + Cdst * Adst * (255 - Asrc) / 255) / Aout
    // Aout = Asrc + Adst * (255 - Asrc) / 255
    static void blend_pixel(std::uint8_t* dst, const std::uint8_t* src, PixelFormat fmt, float opacity = 1.0f) noexcept;

    // Clipping helper
    [[nodiscard]] static Rect clip_rect(const Rect& target, const Rect& bounds) noexcept;

    // Safe rectangle fill with optional clip rect
    static void fill_rect(
        std::uint8_t* buffer,
        std::uint32_t buf_w,
        std::uint32_t buf_h,
        std::uint32_t stride,
        PixelFormat fmt,
        const Rect& rect,
        std::uint32_t color,
        const Rect* clip = nullptr) noexcept;

    // Safe image blit with clipping, opacity and format conversion
    static void blit(
        std::uint8_t* dst,
        std::uint32_t dst_w,
        std::uint32_t dst_h,
        std::uint32_t dst_stride,
        PixelFormat dst_fmt,
        const std::uint8_t* src,
        std::uint32_t src_w,
        std::uint32_t src_h,
        std::uint32_t src_stride,
        PixelFormat src_fmt,
        std::int32_t dst_x,
        std::int32_t dst_y,
        const Rect& src_rect,
        float opacity = 1.0f,
        const Rect* clip = nullptr) noexcept;
};

// -----------------------------------------------------------------------------
// VirtualDisplay: Complete Configurable Display and Framebuffer
// -----------------------------------------------------------------------------
class VirtualDisplay {
public:
    explicit VirtualDisplay(const DisplayConfiguration& config = {});
    ~VirtualDisplay() = default;

    VirtualDisplay(const VirtualDisplay&) = delete;
    VirtualDisplay& operator=(const VirtualDisplay&) = delete;

    [[nodiscard]] const DisplayConfiguration& config() const noexcept { return config_; }
    [[nodiscard]] std::uint32_t width() const noexcept { return config_.width; }
    [[nodiscard]] std::uint32_t height() const noexcept { return config_.height; }
    [[nodiscard]] std::uint32_t stride() const noexcept { return config_.row_stride; }
    [[nodiscard]] PixelFormat pixel_format() const noexcept { return config_.pixel_format; }
    [[nodiscard]] std::size_t framebuffer_size() const noexcept { return framebuffer_.size(); }
    [[nodiscard]] std::uint64_t guest_physical_address() const noexcept { return config_.guest_physical_address; }

    [[nodiscard]] const std::uint8_t* raw_data() const noexcept { return framebuffer_.data(); }
    [[nodiscard]] std::uint8_t* raw_data() noexcept { return framebuffer_.data(); }

    // Reconfiguration and orientation changes
    bool reconfigure(const DisplayConfiguration& new_config);
    bool set_orientation(DisplayOrientation orient);
    void set_guest_physical_address(std::uint64_t addr) noexcept {
        std::lock_guard<std::mutex> guard(lock_);
        config_.guest_physical_address = addr;
    }

    // Bounds-checked pixel access
    bool write_pixel(std::uint32_t x, std::uint32_t y, std::uint32_t color) noexcept;
    [[nodiscard]] std::uint32_t read_pixel(std::uint32_t x, std::uint32_t y) const noexcept;

    // Buffer clearing and filling
    void clear(std::uint32_t color = 0) noexcept;
    void fill_rect(const Rect& rect, std::uint32_t color) noexcept;

    // Safe memory copies with bounds checking
    bool copy_from(const std::uint8_t* src, std::size_t src_size, std::uint32_t src_stride = 0) noexcept;
    bool copy_to(std::uint8_t* dst, std::size_t dst_size, std::uint32_t dst_stride = 0) const noexcept;

    // Format conversion utility
    static bool convert_format(
        const std::uint8_t* src,
        PixelFormat src_fmt,
        std::uint8_t* dst,
        PixelFormat dst_fmt,
        std::size_t pixel_count) noexcept;

private:
    mutable std::mutex        lock_;
    DisplayConfiguration      config_;
    std::vector<std::uint8_t> framebuffer_;
};

} // namespace winios::graphics

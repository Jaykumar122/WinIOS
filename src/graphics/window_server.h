#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace winios::graphics {

// -----------------------------------------------------------------------------
// Pixel Format & Geometry Primitives
// -----------------------------------------------------------------------------
enum class PixelFormat {
    B8G8R8A8_UNORM, // Standard 32-bit BGRA (Little-Endian ARGB/BGRA on Windows)
    R8G8B8A8_UNORM  // Standard 32-bit RGBA
};

struct Rect {
    std::uint32_t x{0};
    std::uint32_t y{0};
    std::uint32_t width{0};
    std::uint32_t height{0};

    [[nodiscard]] constexpr bool empty() const noexcept {
        return width == 0 || height == 0;
    }

    [[nodiscard]] bool contains(std::uint32_t px, std::uint32_t py) const noexcept {
        return px >= x && px < (x + width) && py >= y && py < (y + height);
    }
};

// -----------------------------------------------------------------------------
// DamageTracker: Dirty Rectangle Aggregation
// -----------------------------------------------------------------------------
class DamageTracker {
public:
    DamageTracker() = default;

    void add_dirty_rect(const Rect& r) noexcept;
    void mark_full_damage(std::uint32_t width, std::uint32_t height) noexcept;
    void clear() noexcept;

    [[nodiscard]] bool has_damage() const noexcept { return has_damage_; }
    [[nodiscard]] const Rect& bounding_box() const noexcept { return bounding_box_; }
    [[nodiscard]] const std::vector<Rect>& dirty_rects() const noexcept { return rects_; }

private:
    bool              has_damage_{false};
    Rect              bounding_box_{0, 0, 0, 0};
    std::vector<Rect> rects_;
};

// -----------------------------------------------------------------------------
// IOSurface: Shared Offscreen / Window Backing Buffer
// -----------------------------------------------------------------------------
class IOSurface {
public:
    IOSurface(
        std::uint32_t id,
        std::uint32_t width,
        std::uint32_t height,
        PixelFormat format = PixelFormat::B8G8R8A8_UNORM);

    ~IOSurface() = default;

    [[nodiscard]] std::uint32_t id() const noexcept { return id_; }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] std::uint32_t stride() const noexcept { return stride_; }
    [[nodiscard]] std::size_t size_bytes() const noexcept { return buffer_.size(); }
    [[nodiscard]] PixelFormat format() const noexcept { return format_; }

    [[nodiscard]] const std::uint8_t* raw_data() const noexcept { return buffer_.data(); }
    [[nodiscard]] std::uint8_t* raw_data() noexcept { return buffer_.data(); }

    void write_pixel(std::uint32_t x, std::uint32_t y, std::uint32_t color) noexcept;
    [[nodiscard]] std::uint32_t read_pixel(std::uint32_t x, std::uint32_t y) const noexcept;
    void fill_rect(const Rect& r, std::uint32_t color) noexcept;

private:
    std::uint32_t             id_{0};
    std::uint32_t             width_{0};
    std::uint32_t             height_{0};
    std::uint32_t             stride_{0};
    PixelFormat               format_{PixelFormat::B8G8R8A8_UNORM};
    std::vector<std::uint8_t> buffer_;
};

// -----------------------------------------------------------------------------
// WindowServerLayer: Composited UI Surface
// -----------------------------------------------------------------------------
struct WindowServerLayer {
    std::uint32_t              layer_id{0};
    std::uint32_t              client_task_id{0};
    std::shared_ptr<IOSurface> surface;
    Rect                       frame{0, 0, 0, 0};
    float                      opacity{1.0f};
    int                        z_order{0};
    bool                       is_hidden{false};
};

// -----------------------------------------------------------------------------
// DirectXPresentationAdapter: Windows 11 Host Display Bridge
// -----------------------------------------------------------------------------
struct PresentationStats {
    std::uint64_t total_frames{0};
    std::uint64_t presented_frames{0};
    std::uint64_t dropped_frames{0};
    double        average_fps{60.0};
    std::uint64_t last_present_us{0};
};

class DirectXPresentationAdapter {
public:
    DirectXPresentationAdapter(std::uint32_t width, std::uint32_t height);
    ~DirectXPresentationAdapter() = default;

    // Present guest framebuffer to host DirectX staging surface
    bool present(const void* fb_data, std::size_t size, const Rect& damage);

    [[nodiscard]] const PresentationStats& stats() const noexcept { return stats_; }
    [[nodiscard]] const std::vector<std::uint8_t>& host_staging_buffer() const noexcept {
        return host_staging_buffer_;
    }

private:
    std::uint32_t             width_{0};
    std::uint32_t             height_{0};
    PresentationStats         stats_{};
    std::vector<std::uint8_t> host_staging_buffer_;
};

// -----------------------------------------------------------------------------
// WindowServerCompositor: CoreGraphics & WindowServer Compositing Engine
// -----------------------------------------------------------------------------
class WindowServerCompositor {
public:
    static constexpr std::uint32_t kIPhone15Width  = 1179u;
    static constexpr std::uint32_t kIPhone15Height = 2556u;

    WindowServerCompositor(
        std::uint32_t width = kIPhone15Width,
        std::uint32_t height = kIPhone15Height);

    ~WindowServerCompositor() = default;

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] std::size_t framebuffer_size() const noexcept {
        return static_cast<std::size_t>(width_) * height_ * 4;
    }

    // Surface Management
    std::shared_ptr<IOSurface> create_surface(
        std::uint32_t width,
        std::uint32_t height,
        PixelFormat format = PixelFormat::B8G8R8A8_UNORM);

    // Layer Management
    bool add_layer(WindowServerLayer layer);
    bool remove_layer(std::uint32_t layer_id);
    bool update_layer_frame(std::uint32_t layer_id, const Rect& frame);

    // Damage Tracking
    void mark_damage(const Rect& r);
    [[nodiscard]] DamageTracker& damage_tracker() noexcept { return damage_; }

    // Compositing Pipeline
    bool composite(void* dest_framebuffer, std::size_t fb_size);

    // Host Display Presentation
    bool present_to_host(const void* dest_framebuffer, std::size_t fb_size);

    [[nodiscard]] DirectXPresentationAdapter& presentation_adapter() noexcept {
        return presenter_;
    }

private:
    std::uint32_t                    width_{kIPhone15Width};
    std::uint32_t                    height_{kIPhone15Height};
    std::uint32_t                    next_surface_id_{1};
    mutable std::mutex               lock_;
    DamageTracker                    damage_;
    std::vector<WindowServerLayer>   layers_;
    DirectXPresentationAdapter       presenter_;
};

} // namespace winios::graphics

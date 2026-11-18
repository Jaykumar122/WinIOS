#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "graphics/virtual_display.h"

namespace winios::graphics {

// -----------------------------------------------------------------------------
// Surface Result Codes
// -----------------------------------------------------------------------------
enum class SurfaceResultCode : std::uint32_t {
    Success = 0,
    InvalidDimensions = 1,
    UnsupportedFormat = 2,
    SurfaceLimitExceeded = 3,       // Per-process quota exceeded
    MemoryBudgetExceeded = 4,       // Total display memory budget exceeded
    PerSurfaceLimitExceeded = 5,    // Max bytes per surface exceeded
    AccessDenied = 6,               // Unauthorized process attempting access
    NotFound = 7,                   // Stale or invalid surface handle
    OutOfBounds = 8,                // Out-of-bounds pixel/copy coordinates
    InvalidParameter = 9
};

// -----------------------------------------------------------------------------
// DisplaySurface: Process-owned UI surface with Access Validation
// -----------------------------------------------------------------------------
class DisplaySurface {
public:
    DisplaySurface(
        std::uint32_t surface_id,
        std::uint32_t owner_pid,
        std::uint32_t width,
        std::uint32_t height,
        PixelFormat format = PixelFormat::B8G8R8A8_UNORM);

    ~DisplaySurface() = default;

    DisplaySurface(const DisplaySurface&) = delete;
    DisplaySurface& operator=(const DisplaySurface&) = delete;

    [[nodiscard]] std::uint32_t id() const noexcept { return id_; }
    [[nodiscard]] std::uint32_t owner_pid() const noexcept { return owner_pid_; }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] std::uint32_t stride() const noexcept { return stride_; }
    [[nodiscard]] PixelFormat format() const noexcept { return format_; }
    [[nodiscard]] std::size_t size_bytes() const noexcept { return buffer_.size(); }

    // Position, Bounds & Layering
    [[nodiscard]] std::int32_t pos_x() const noexcept { return pos_x_; }
    [[nodiscard]] std::int32_t pos_y() const noexcept { return pos_y_; }
    [[nodiscard]] std::int32_t z_order() const noexcept { return z_order_; }
    [[nodiscard]] float opacity() const noexcept { return opacity_; }
    [[nodiscard]] bool is_visible() const noexcept { return is_visible_; }
    [[nodiscard]] Rect bounds() const noexcept {
        return Rect{
            static_cast<std::uint32_t>(std::max(0, pos_x_)),
            static_cast<std::uint32_t>(std::max(0, pos_y_)),
            width_,
            height_
        };
    }

    void set_position(std::int32_t x, std::int32_t y) noexcept;
    void set_z_order(std::int32_t z) noexcept { z_order_ = z; }
    void set_opacity(float alpha) noexcept { opacity_ = std::clamp(alpha, 0.0f, 1.0f); }
    void set_visible(bool visible) noexcept { is_visible_ = visible; }
    void clear_damage() noexcept { damage_.clear(); }
    void resize(std::uint32_t new_width, std::uint32_t new_height) {
        if (new_width == 0 || new_height == 0) return;
        width_ = new_width;
        height_ = new_height;
        stride_ = width_ * 4;
        buffer_.assign(static_cast<std::size_t>(stride_) * height_, 0);
        damage_.mark_full_damage(width_, height_);
    }

    // Access Control Validation: owner_pid == caller_pid, or caller_pid == 0 (kernel/compositor)
    [[nodiscard]] bool can_access(std::uint32_t caller_pid) const noexcept {
        return caller_pid == 0 || caller_pid == owner_pid_;
    }

    // Process-validated pixel operations
    SurfaceResultCode write_pixel(std::uint32_t caller_pid, std::uint32_t x, std::uint32_t y, std::uint32_t color) noexcept;
    SurfaceResultCode read_pixel(std::uint32_t caller_pid, std::uint32_t x, std::uint32_t y, std::uint32_t& out_color) const noexcept;
    SurfaceResultCode fill_rect(std::uint32_t caller_pid, const Rect& r, std::uint32_t color) noexcept;
    SurfaceResultCode copy_pixels(std::uint32_t caller_pid, const std::uint8_t* src, std::size_t size) noexcept;

    // Buffer direct access for compositor (caller_pid = 0)
    [[nodiscard]] const std::uint8_t* raw_data() const noexcept { return buffer_.data(); }
    [[nodiscard]] std::uint8_t* raw_data() noexcept { return buffer_.data(); }

    // Damage tracking
    [[nodiscard]] DamageTracker& damage_tracker() noexcept { return damage_; }
    [[nodiscard]] const DamageTracker& damage_tracker() const noexcept { return damage_; }

private:
    std::uint32_t             id_{0};
    std::uint32_t             owner_pid_{0};
    std::uint32_t             width_{0};
    std::uint32_t             height_{0};
    std::uint32_t             stride_{0};
    PixelFormat               format_{PixelFormat::B8G8R8A8_UNORM};
    std::int32_t              pos_x_{0};
    std::int32_t              pos_y_{0};
    std::int32_t              z_order_{0};
    float                     opacity_{1.0f};
    bool                      is_visible_{true};
    DamageTracker             damage_;
    std::vector<std::uint8_t> buffer_;
};

// -----------------------------------------------------------------------------
// SurfaceResourceManager: Quotas, Accounting, Stale Handle Detection & Lifecycles
// -----------------------------------------------------------------------------
struct ProcessAccounting {
    std::uint32_t surface_count{0};
    std::size_t   allocated_bytes{0};
};

class SurfaceResourceManager {
public:
    static constexpr std::size_t   kMaxSurfaceBytes = 32ULL * 1024ULL * 1024ULL;      // 32 MB per surface
    static constexpr std::uint32_t kMaxSurfacesPerProcess = 16u;                      // 16 surfaces per PID
    static constexpr std::size_t   kMaxTotalDisplayBudget = 128ULL * 1024ULL * 1024ULL; // 128 MB total budget

    SurfaceResourceManager() = default;
    ~SurfaceResourceManager() = default;

    SurfaceResourceManager(const SurfaceResourceManager&) = delete;
    SurfaceResourceManager& operator=(const SurfaceResourceManager&) = delete;

    // Creation with atomic quota verification and rollback on failure
    SurfaceResultCode create_surface(
        std::uint32_t caller_pid,
        std::uint32_t width,
        std::uint32_t height,
        PixelFormat format,
        std::shared_ptr<DisplaySurface>& out_surface);

    // Destruction with cleanup and quota reclamation
    SurfaceResultCode destroy_surface(std::uint32_t caller_pid, std::uint32_t surface_id);

    // Retrieval with stale handle and authorization verification
    SurfaceResultCode get_surface(
        std::uint32_t caller_pid,
        std::uint32_t surface_id,
        std::shared_ptr<DisplaySurface>& out_surface) const;

    // Process termination cleanup
    void cleanup_process_surfaces(std::uint32_t pid);

    // Diagnostics & Accounting Queries
    [[nodiscard]] std::size_t total_allocated_bytes() const noexcept;
    [[nodiscard]] std::uint32_t total_surface_count() const noexcept;
    [[nodiscard]] ProcessAccounting get_process_accounting(std::uint32_t pid) const noexcept;
    [[nodiscard]] std::vector<std::shared_ptr<DisplaySurface>> get_all_visible_surfaces() const;

private:
    mutable std::mutex lock_;
    std::uint32_t next_surface_id_{1};
    std::size_t total_allocated_memory_{0};
    std::map<std::uint32_t, ProcessAccounting> process_accounting_;
    std::map<std::uint32_t, std::shared_ptr<DisplaySurface>> surfaces_;
    std::map<std::uint32_t, std::uint32_t> surface_generations_; // Stale-handle detection
};

} // namespace winios::graphics

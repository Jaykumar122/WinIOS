#include "graphics/display_surface.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace winios::graphics {

// -----------------------------------------------------------------------------
// DisplaySurface Implementation
// -----------------------------------------------------------------------------
DisplaySurface::DisplaySurface(
    std::uint32_t surface_id,
    std::uint32_t owner_pid,
    std::uint32_t width,
    std::uint32_t height,
    PixelFormat format)
    : id_(surface_id),
      owner_pid_(owner_pid),
      width_(width),
      height_(height),
      format_(format) {

    std::uint32_t bpp = bytes_per_pixel(format_);
    stride_ = width_ * bpp;
    std::size_t total_size = static_cast<std::size_t>(stride_) * height_;
    buffer_.assign(total_size, 0);

    damage_.mark_full_damage(width_, height_);
}

void DisplaySurface::set_position(std::int32_t x, std::int32_t y) noexcept {
    if (pos_x_ != x || pos_y_ != y) {
        pos_x_ = x;
        pos_y_ = y;
        damage_.mark_full_damage(width_, height_);
    }
}

SurfaceResultCode DisplaySurface::write_pixel(
    std::uint32_t caller_pid,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t color) noexcept {

    if (!can_access(caller_pid)) {
        return SurfaceResultCode::AccessDenied;
    }

    if (x >= width_ || y >= height_) {
        return SurfaceResultCode::OutOfBounds;
    }

    std::uint32_t bpp = bytes_per_pixel(format_);
    std::size_t offset = (static_cast<std::size_t>(y) * stride_) + (x * bpp);
    if (offset + bpp > buffer_.size()) {
        return SurfaceResultCode::OutOfBounds;
    }

    std::memcpy(buffer_.data() + offset, &color, bpp);
    damage_.add_dirty_rect(Rect{x, y, 1, 1});
    return SurfaceResultCode::Success;
}

SurfaceResultCode DisplaySurface::read_pixel(
    std::uint32_t caller_pid,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t& out_color) const noexcept {

    if (!can_access(caller_pid)) {
        return SurfaceResultCode::AccessDenied;
    }

    if (x >= width_ || y >= height_) {
        return SurfaceResultCode::OutOfBounds;
    }

    std::uint32_t bpp = bytes_per_pixel(format_);
    std::size_t offset = (static_cast<std::size_t>(y) * stride_) + (x * bpp);
    if (offset + bpp > buffer_.size()) {
        return SurfaceResultCode::OutOfBounds;
    }

    out_color = 0;
    std::memcpy(&out_color, buffer_.data() + offset, bpp);
    return SurfaceResultCode::Success;
}

SurfaceResultCode DisplaySurface::fill_rect(
    std::uint32_t caller_pid,
    const Rect& r,
    std::uint32_t color) noexcept {

    if (!can_access(caller_pid)) {
        return SurfaceResultCode::AccessDenied;
    }

    if (r.empty()) {
        return SurfaceResultCode::Success;
    }

    Rect clipped = RasterPrimitives::clip_rect(r, Rect{0, 0, width_, height_});
    if (clipped.empty()) {
        return SurfaceResultCode::OutOfBounds;
    }

    RasterPrimitives::fill_rect(
        buffer_.data(),
        width_,
        height_,
        stride_,
        format_,
        clipped,
        color);

    damage_.add_dirty_rect(clipped);
    return SurfaceResultCode::Success;
}

SurfaceResultCode DisplaySurface::copy_pixels(
    std::uint32_t caller_pid,
    const std::uint8_t* src,
    std::size_t size) noexcept {

    if (!can_access(caller_pid)) {
        return SurfaceResultCode::AccessDenied;
    }

    if (!src) {
        return SurfaceResultCode::InvalidParameter;
    }

    if (size < buffer_.size()) {
        return SurfaceResultCode::OutOfBounds;
    }

    std::memcpy(buffer_.data(), src, buffer_.size());
    damage_.mark_full_damage(width_, height_);
    return SurfaceResultCode::Success;
}

// -----------------------------------------------------------------------------
// SurfaceResourceManager Implementation
// -----------------------------------------------------------------------------
SurfaceResultCode SurfaceResourceManager::create_surface(
    std::uint32_t caller_pid,
    std::uint32_t width,
    std::uint32_t height,
    PixelFormat format,
    std::shared_ptr<DisplaySurface>& out_surface) {

    out_surface.reset();

    if (width == 0 || height == 0 || width > 8192u || height > 8192u) {
        return SurfaceResultCode::InvalidDimensions;
    }

    std::uint32_t bpp = bytes_per_pixel(format);
    if (bpp == 0 || (bpp != 2 && bpp != 4)) {
        return SurfaceResultCode::UnsupportedFormat;
    }

    std::uint64_t surface_bytes_64 = static_cast<std::uint64_t>(width) * bpp * height;
    if (surface_bytes_64 > kMaxSurfaceBytes) {
        return SurfaceResultCode::PerSurfaceLimitExceeded;
    }
    std::size_t surface_bytes = static_cast<std::size_t>(surface_bytes_64);

    std::lock_guard<std::mutex> guard(lock_);

    // Check per-process quota
    auto& proc_acc = process_accounting_[caller_pid];
    if (proc_acc.surface_count >= kMaxSurfacesPerProcess) {
        return SurfaceResultCode::SurfaceLimitExceeded;
    }

    // Check total display memory budget
    if (total_allocated_memory_ + surface_bytes > kMaxTotalDisplayBudget) {
        return SurfaceResultCode::MemoryBudgetExceeded;
    }

    // Allocate surface ID & object
    std::uint32_t sid = next_surface_id_++;
    auto surface = std::make_shared<DisplaySurface>(sid, caller_pid, width, height, format);

    // Commit accounting
    proc_acc.surface_count++;
    proc_acc.allocated_bytes += surface_bytes;
    total_allocated_memory_ += surface_bytes;

    surfaces_[sid] = surface;
    surface_generations_[sid] = 1;

    out_surface = surface;
    return SurfaceResultCode::Success;
}

SurfaceResultCode SurfaceResourceManager::destroy_surface(
    std::uint32_t caller_pid,
    std::uint32_t surface_id) {

    std::lock_guard<std::mutex> guard(lock_);
    auto it = surfaces_.find(surface_id);
    if (it == surfaces_.end()) {
        return SurfaceResultCode::NotFound;
    }

    const auto& surface = it->second;
    if (!surface->can_access(caller_pid)) {
        return SurfaceResultCode::AccessDenied;
    }

    std::size_t surface_bytes = surface->size_bytes();
    std::uint32_t owner = surface->owner_pid();

    // Reclaim accounting
    auto acc_it = process_accounting_.find(owner);
    if (acc_it != process_accounting_.end()) {
        if (acc_it->second.surface_count > 0) {
            acc_it->second.surface_count--;
        }
        if (acc_it->second.allocated_bytes >= surface_bytes) {
            acc_it->second.allocated_bytes -= surface_bytes;
        } else {
            acc_it->second.allocated_bytes = 0;
        }
        if (acc_it->second.surface_count == 0 && acc_it->second.allocated_bytes == 0) {
            process_accounting_.erase(acc_it);
        }
    }

    if (total_allocated_memory_ >= surface_bytes) {
        total_allocated_memory_ -= surface_bytes;
    } else {
        total_allocated_memory_ = 0;
    }

    surfaces_.erase(it);
    surface_generations_.erase(surface_id);

    return SurfaceResultCode::Success;
}

SurfaceResultCode SurfaceResourceManager::get_surface(
    std::uint32_t caller_pid,
    std::uint32_t surface_id,
    std::shared_ptr<DisplaySurface>& out_surface) const {

    out_surface.reset();
    std::lock_guard<std::mutex> guard(lock_);
    auto it = surfaces_.find(surface_id);
    if (it == surfaces_.end()) {
        return SurfaceResultCode::NotFound;
    }

    if (!it->second->can_access(caller_pid)) {
        return SurfaceResultCode::AccessDenied;
    }

    out_surface = it->second;
    return SurfaceResultCode::Success;
}

void SurfaceResourceManager::cleanup_process_surfaces(std::uint32_t pid) {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<std::uint32_t> to_remove;
    for (const auto& [sid, surf] : surfaces_) {
        if (surf->owner_pid() == pid) {
            to_remove.push_back(sid);
        }
    }

    for (std::uint32_t sid : to_remove) {
        auto it = surfaces_.find(sid);
        if (it != surfaces_.end()) {
            std::size_t bytes = it->second->size_bytes();
            if (total_allocated_memory_ >= bytes) {
                total_allocated_memory_ -= bytes;
            } else {
                total_allocated_memory_ = 0;
            }
            surfaces_.erase(it);
            surface_generations_.erase(sid);
        }
    }

    process_accounting_.erase(pid);
}

std::size_t SurfaceResourceManager::total_allocated_bytes() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return total_allocated_memory_;
}

std::uint32_t SurfaceResourceManager::total_surface_count() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return static_cast<std::uint32_t>(surfaces_.size());
}

ProcessAccounting SurfaceResourceManager::get_process_accounting(std::uint32_t pid) const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = process_accounting_.find(pid);
    if (it != process_accounting_.end()) {
        return it->second;
    }
    return ProcessAccounting{0, 0};
}

std::vector<std::shared_ptr<DisplaySurface>> SurfaceResourceManager::get_all_visible_surfaces() const {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<std::shared_ptr<DisplaySurface>> result;
    for (const auto& [sid, surf] : surfaces_) {
        if (surf->is_visible() && surf->opacity() > 0.0f) {
            result.push_back(surf);
        }
    }
    // Sort by z-order ascending
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a->z_order() < b->z_order();
    });
    return result;
}

} // namespace winios::graphics

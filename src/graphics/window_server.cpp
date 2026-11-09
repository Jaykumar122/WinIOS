#include "graphics/window_server.h"

#include <algorithm>
#include <cstring>

namespace winios::graphics {

// -----------------------------------------------------------------------------
// DamageTracker Implementation
// -----------------------------------------------------------------------------
void DamageTracker::add_dirty_rect(const Rect& r) noexcept {
    if (r.empty()) return;

    if (!has_damage_) {
        bounding_box_ = r;
        has_damage_ = true;
        rects_.clear();
        rects_.push_back(r);
        return;
    }

    std::uint32_t x1 = std::min(bounding_box_.x, r.x);
    std::uint32_t y1 = std::min(bounding_box_.y, r.y);
    std::uint32_t x2 = std::max(bounding_box_.x + bounding_box_.width, r.x + r.width);
    std::uint32_t y2 = std::max(bounding_box_.y + bounding_box_.height, r.y + r.height);

    bounding_box_ = Rect{x1, y1, x2 - x1, y2 - y1};
    rects_.push_back(r);
}

void DamageTracker::mark_full_damage(std::uint32_t width, std::uint32_t height) noexcept {
    has_damage_ = true;
    bounding_box_ = Rect{0, 0, width, height};
    rects_.clear();
    rects_.push_back(bounding_box_);
}

void DamageTracker::clear() noexcept {
    has_damage_ = false;
    bounding_box_ = Rect{0, 0, 0, 0};
    rects_.clear();
}

// -----------------------------------------------------------------------------
// IOSurface Implementation
// -----------------------------------------------------------------------------
IOSurface::IOSurface(
    std::uint32_t id,
    std::uint32_t width,
    std::uint32_t height,
    PixelFormat format)
    : id_(id), width_(width), height_(height), format_(format) {

    stride_ = width_ * 4u;
    buffer_.assign(static_cast<std::size_t>(stride_) * height_, 0);
}

void IOSurface::write_pixel(std::uint32_t x, std::uint32_t y, std::uint32_t color) noexcept {
    if (x >= width_ || y >= height_) return;

    std::size_t offset = (static_cast<std::size_t>(y) * stride_) + (x * 4u);
    std::memcpy(buffer_.data() + offset, &color, sizeof(color));
}

std::uint32_t IOSurface::read_pixel(std::uint32_t x, std::uint32_t y) const noexcept {
    if (x >= width_ || y >= height_) return 0;

    std::size_t offset = (static_cast<std::size_t>(y) * stride_) + (x * 4u);
    std::uint32_t color = 0;
    std::memcpy(&color, buffer_.data() + offset, sizeof(color));
    return color;
}

void IOSurface::fill_rect(const Rect& r, std::uint32_t color) noexcept {
    std::uint32_t x_end = std::min(r.x + r.width, width_);
    std::uint32_t y_end = std::min(r.y + r.height, height_);

    for (std::uint32_t py = r.y; py < y_end; ++py) {
        std::size_t row_start = (static_cast<std::size_t>(py) * stride_);
        for (std::uint32_t px = r.x; px < x_end; ++px) {
            std::size_t offset = row_start + (px * 4u);
            std::memcpy(buffer_.data() + offset, &color, sizeof(color));
        }
    }
}

// -----------------------------------------------------------------------------
// DirectXPresentationAdapter Implementation
// -----------------------------------------------------------------------------
DirectXPresentationAdapter::DirectXPresentationAdapter(std::uint32_t width, std::uint32_t height)
    : width_(width), height_(height) {

    host_staging_buffer_.assign(static_cast<std::size_t>(width_) * height_ * 4, 0);
}

bool DirectXPresentationAdapter::present(const void* fb_data, std::size_t size, const Rect& damage) {
    stats_.total_frames++;

    std::size_t expected_size = static_cast<std::size_t>(width_) * height_ * 4;
    if (!fb_data || size < expected_size) {
        stats_.dropped_frames++;
        return false;
    }

    const auto* src = static_cast<const std::uint8_t*>(fb_data);
    auto* dst = host_staging_buffer_.data();

    // Damage-optimized row copy
    if (damage.empty() || (damage.width == width_ && damage.height == height_)) {
        std::memcpy(dst, src, expected_size);
    } else {
        std::uint32_t y_end = std::min(damage.y + damage.height, height_);
        std::uint32_t x_end = std::min(damage.x + damage.width, width_);
        std::size_t copy_bytes = static_cast<std::size_t>(x_end - damage.x) * 4;

        for (std::uint32_t y = damage.y; y < y_end; ++y) {
            std::size_t row_offset = (static_cast<std::size_t>(y) * width_ * 4) + (damage.x * 4);
            std::memcpy(dst + row_offset, src + row_offset, copy_bytes);
        }
    }

    stats_.presented_frames++;
    return true;
}

// -----------------------------------------------------------------------------
// WindowServerCompositor Implementation
// -----------------------------------------------------------------------------
WindowServerCompositor::WindowServerCompositor(std::uint32_t width, std::uint32_t height)
    : width_(width), height_(height), presenter_(width, height) {

    damage_.mark_full_damage(width_, height_);
}

std::shared_ptr<IOSurface> WindowServerCompositor::create_surface(
    std::uint32_t width,
    std::uint32_t height,
    PixelFormat format) {

    std::lock_guard<std::mutex> guard(lock_);
    return std::make_shared<IOSurface>(next_surface_id_++, width, height, format);
}

bool WindowServerCompositor::add_layer(WindowServerLayer layer) {
    std::lock_guard<std::mutex> guard(lock_);
    damage_.add_dirty_rect(layer.frame);
    layers_.push_back(std::move(layer));
    return true;
}

bool WindowServerCompositor::remove_layer(std::uint32_t layer_id) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = std::find_if(layers_.begin(), layers_.end(), [&](const WindowServerLayer& l) {
        return l.layer_id == layer_id;
    });

    if (it != layers_.end()) {
        damage_.add_dirty_rect(it->frame);
        layers_.erase(it);
        return true;
    }
    return false;
}

bool WindowServerCompositor::update_layer_frame(std::uint32_t layer_id, const Rect& frame) {
    std::lock_guard<std::mutex> guard(lock_);
    for (auto& l : layers_) {
        if (l.layer_id == layer_id) {
            damage_.add_dirty_rect(l.frame); // Old frame
            l.frame = frame;
            damage_.add_dirty_rect(l.frame); // New frame
            return true;
        }
    }
    return false;
}

void WindowServerCompositor::mark_damage(const Rect& r) {
    std::lock_guard<std::mutex> guard(lock_);
    damage_.add_dirty_rect(r);
}

bool WindowServerCompositor::composite(void* dest_framebuffer, std::size_t fb_size) {
    std::lock_guard<std::mutex> guard(lock_);
    std::size_t expected_size = framebuffer_size();
    if (!dest_framebuffer || fb_size < expected_size) {
        return false;
    }

    auto* dst = static_cast<std::uint8_t*>(dest_framebuffer);

    // Sort layers by z_order ascending (back to front)
    std::vector<WindowServerLayer*> sorted_layers;
    for (auto& l : layers_) {
        if (!l.is_hidden && l.opacity > 0.0f && l.surface) {
            sorted_layers.push_back(&l);
        }
    }
    std::sort(sorted_layers.begin(), sorted_layers.end(), [](const WindowServerLayer* a, const WindowServerLayer* b) {
        return a->z_order < b->z_order;
    });

    // Clear background to solid black (or dark wallpaper)
    std::memset(dst, 0, expected_size);

    // Composite layers
    for (const auto* layer : sorted_layers) {
        const auto& surf = layer->surface;
        std::uint32_t dst_x_end = std::min(layer->frame.x + layer->frame.width, width_);
        std::uint32_t dst_y_end = std::min(layer->frame.y + layer->frame.height, height_);

        for (std::uint32_t dy = layer->frame.y; dy < dst_y_end; ++dy) {
            std::uint32_t sy = dy - layer->frame.y;
            if (sy >= surf->height()) break;

            for (std::uint32_t dx = layer->frame.x; dx < dst_x_end; ++dx) {
                std::uint32_t sx = dx - layer->frame.x;
                if (sx >= surf->width()) break;

                std::uint32_t src_color = surf->read_pixel(sx, sy);
                std::size_t dst_offset = (static_cast<std::size_t>(dy) * width_ * 4) + (dx * 4);

                if (layer->opacity >= 1.0f) {
                    std::memcpy(dst + dst_offset, &src_color, 4);
                } else {
                    // Alpha blend with opacity
                    std::uint32_t dst_color = 0;
                    std::memcpy(&dst_color, dst + dst_offset, 4);

                    std::uint8_t sa = static_cast<std::uint8_t>((src_color >> 24) * layer->opacity);
                    std::uint8_t sr = static_cast<std::uint8_t>((src_color >> 16) & 0xFF);
                    std::uint8_t sg = static_cast<std::uint8_t>((src_color >> 8) & 0xFF);
                    std::uint8_t sb = static_cast<std::uint8_t>(src_color & 0xFF);

                    std::uint8_t dr = static_cast<std::uint8_t>((dst_color >> 16) & 0xFF);
                    std::uint8_t dg = static_cast<std::uint8_t>((dst_color >> 8) & 0xFF);
                    std::uint8_t db = static_cast<std::uint8_t>(dst_color & 0xFF);

                    std::uint8_t out_r = static_cast<std::uint8_t>((sr * sa + dr * (255 - sa)) / 255);
                    std::uint8_t out_g = static_cast<std::uint8_t>((sg * sa + dg * (255 - sa)) / 255);
                    std::uint8_t out_b = static_cast<std::uint8_t>((sb * sa + db * (255 - sa)) / 255);

                    std::uint32_t out_color = (0xFFu << 24) | (out_r << 16) | (out_g << 8) | out_b;
                    std::memcpy(dst + dst_offset, &out_color, 4);
                }
            }
        }
    }

    return true;
}

bool WindowServerCompositor::present_to_host(const void* dest_framebuffer, std::size_t fb_size) {
    std::lock_guard<std::mutex> guard(lock_);
    bool ok = presenter_.present(dest_framebuffer, fb_size, damage_.bounding_box());
    if (ok) {
        damage_.clear();
    }
    return ok;
}

} // namespace winios::graphics

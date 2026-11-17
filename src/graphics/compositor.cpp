#include "graphics/compositor.h"

#include <algorithm>

namespace winios::graphics {

Compositor::Compositor(std::shared_ptr<VirtualDisplay> display)
    : display_(std::move(display)) {

    if (display_) {
        damage_.mark_full_damage(display_->width(), display_->height());
    }
}

bool Compositor::add_surface(std::shared_ptr<DisplaySurface> surface) {
    if (!surface) return false;

    std::lock_guard<std::mutex> guard(lock_);
    auto it = std::find_if(surfaces_.begin(), surfaces_.end(), [&](const auto& s) {
        return s->id() == surface->id();
    });

    if (it != surfaces_.end()) {
        return false;
    }

    damage_.add_dirty_rect(surface->bounds());
    surfaces_.push_back(std::move(surface));
    return true;
}

bool Compositor::remove_surface(std::uint32_t surface_id) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = std::find_if(surfaces_.begin(), surfaces_.end(), [&](const auto& s) {
        return s->id() == surface_id;
    });

    if (it != surfaces_.end()) {
        damage_.add_dirty_rect((*it)->bounds());
        surfaces_.erase(it);
        return true;
    }
    return false;
}

void Compositor::clear_surfaces() {
    std::lock_guard<std::mutex> guard(lock_);
    surfaces_.clear();
    if (display_) {
        damage_.mark_full_damage(display_->width(), display_->height());
    }
}

void Compositor::invalidate_full_frame() noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    if (display_) {
        damage_.mark_full_damage(display_->width(), display_->height());
    }
}

void Compositor::invalidate_rect(const Rect& rect) noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    damage_.add_dirty_rect(rect);
}

bool Compositor::composite(bool force_full) {
    std::lock_guard<std::mutex> guard(lock_);
    if (!display_) {
        return false;
    }

    std::uint32_t disp_w = display_->width();
    std::uint32_t disp_h = display_->height();
    std::uint32_t disp_stride = display_->stride();
    PixelFormat disp_fmt = display_->pixel_format();
    std::uint8_t* disp_buf = display_->raw_data();

    if (!disp_buf || disp_w == 0 || disp_h == 0) {
        return false;
    }

    // Collect damage from registered surfaces
    for (const auto& surf : surfaces_) {
        if (surf && surf->is_visible() && surf->damage_tracker().has_damage()) {
            const Rect& sb = surf->bounds();
            damage_.add_dirty_rect(sb);
        }
    }

    if (force_full) {
        damage_.mark_full_damage(disp_w, disp_h);
    }

    if (!damage_.has_damage()) {
        stats_.skipped_frames++;
        return true;
    }

    stats_.total_frames++;
    Rect damage_box = damage_.bounding_box();
    damage_box = RasterPrimitives::clip_rect(damage_box, Rect{0, 0, disp_w, disp_h});

    bool is_full = (damage_box.x == 0 && damage_box.y == 0 &&
                    damage_box.width == disp_w && damage_box.height == disp_h);

    if (is_full) {
        stats_.full_redraw_frames++;
        // Clear entire display buffer with background color
        display_->clear(background_color_);
    } else {
        stats_.partial_redraw_frames++;
        // Fill only damage rect with background color
        display_->fill_rect(damage_box, background_color_);
    }

    // Collect and sort visible surfaces by Z-order ascending
    std::vector<std::shared_ptr<DisplaySurface>> sorted_surfaces;
    for (const auto& surf : surfaces_) {
        if (surf && surf->is_visible() && surf->opacity() > 0.0f) {
            sorted_surfaces.push_back(surf);
        }
    }

    std::sort(sorted_surfaces.begin(), sorted_surfaces.end(), [](const auto& a, const auto& b) {
        return a->z_order() < b->z_order();
    });

    // Composite each surface inside the damage box
    for (const auto& surf : sorted_surfaces) {
        Rect src_rect{0, 0, surf->width(), surf->height()};

        RasterPrimitives::blit(
            disp_buf,
            disp_w,
            disp_h,
            disp_stride,
            disp_fmt,
            surf->raw_data(),
            surf->width(),
            surf->height(),
            surf->stride(),
            surf->format(),
            surf->pos_x(),
            surf->pos_y(),
            src_rect,
            surf->opacity(),
            &damage_box);

        stats_.total_pixels_composited += (surf->width() * surf->height());
        surf->damage_tracker().clear();
    }

    damage_.clear();
    return true;
}

} // namespace winios::graphics

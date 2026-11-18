#include "graphics/multi_window_compositor.h"

#include <algorithm>
#include <cstring>

namespace winios::graphics {

MultiWindowCompositor::MultiWindowCompositor(
    std::shared_ptr<WindowManager> window_mgr,
    std::shared_ptr<VirtualDisplay> display)
    : window_mgr_(std::move(window_mgr))
    , display_(std::move(display)) {

    if (display_) {
        desktop_damage_.mark_full_damage(display_->width(), display_->height());
    }
}

void MultiWindowCompositor::invalidate_full_desktop() noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    if (display_) {
        desktop_damage_.mark_full_damage(display_->width(), display_->height());
    }
}

void MultiWindowCompositor::invalidate_rect(const Rect& rect) noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    desktop_damage_.add_dirty_rect(rect);
}

void MultiWindowCompositor::clear_damaged_background(const Rect& damage) {
    if (!display_) return;
    std::uint8_t* dst = display_->raw_data();
    std::uint32_t dst_w = display_->width();
    std::uint32_t dst_h = display_->height();
    std::uint32_t dst_stride = display_->stride();

    std::uint32_t x0 = std::min(damage.x, dst_w);
    std::uint32_t y0 = std::min(damage.y, dst_h);
    std::uint32_t x1 = std::min(damage.x + damage.width, dst_w);
    std::uint32_t y1 = std::min(damage.y + damage.height, dst_h);

    if (x0 >= x1 || y0 >= y1) return;

    const std::uint8_t b = static_cast<std::uint8_t>(background_color_ & 0xFF);
    const std::uint8_t g = static_cast<std::uint8_t>((background_color_ >> 8) & 0xFF);
    const std::uint8_t r = static_cast<std::uint8_t>((background_color_ >> 16) & 0xFF);
    const std::uint8_t a = static_cast<std::uint8_t>((background_color_ >> 24) & 0xFF);

    for (std::uint32_t y = y0; y < y1; ++y) {
        std::uint8_t* row = dst + (static_cast<std::size_t>(y) * dst_stride) + (static_cast<std::size_t>(x0) * 4);
        for (std::uint32_t x = x0; x < x1; ++x) {
            row[0] = b;
            row[1] = g;
            row[2] = r;
            row[3] = a;
            row += 4;
        }
    }
}

void MultiWindowCompositor::blit_client_surface(const Window& window, const Rect& damage) {
    if (!display_ || !window.surface || !window.surface->is_visible()) return;

    const Rect client_rc = window.client_frame();
    Rect blit_rc = client_rc.intersect_with(damage);
    if (blit_rc.width == 0 || blit_rc.height == 0) return;

    std::uint8_t* dst = display_->raw_data();
    std::uint32_t dst_w = display_->width();
    std::uint32_t dst_h = display_->height();
    std::uint32_t dst_stride = display_->stride();

    const std::uint8_t* src = window.surface->raw_data();
    std::uint32_t src_w = window.surface->width();
    std::uint32_t src_h = window.surface->height();
    std::uint32_t src_stride = window.surface->stride();
    if (!src || src_w == 0 || src_h == 0) return;

    std::uint32_t x0 = std::min(blit_rc.x, dst_w);
    std::uint32_t y0 = std::min(blit_rc.y, dst_h);
    std::uint32_t x1 = std::min(blit_rc.x + blit_rc.width, dst_w);
    std::uint32_t y1 = std::min(blit_rc.y + blit_rc.height, dst_h);

    for (std::uint32_t y = y0; y < y1; ++y) {
        std::uint32_t sy = y - client_rc.y;
        if (sy >= src_h) break;

        std::uint8_t* dst_row = dst + (static_cast<std::size_t>(y) * dst_stride) + (static_cast<std::size_t>(x0) * 4);
        const std::uint8_t* src_row = src + (static_cast<std::size_t>(sy) * src_stride);

        for (std::uint32_t x = x0; x < x1; ++x) {
            std::uint32_t sx = x - client_rc.x;
            if (sx >= src_w) break;

            const std::uint8_t* sp = src_row + (static_cast<std::size_t>(sx) * 4);
            const std::uint8_t sa = sp[3];

            if (sa == 255) {
                dst_row[0] = sp[0];
                dst_row[1] = sp[1];
                dst_row[2] = sp[2];
                dst_row[3] = 255;
            } else if (sa > 0) {
                // Alpha blend
                const std::uint32_t inv_a = 255 - sa;
                dst_row[0] = static_cast<std::uint8_t>((sp[0] * sa + dst_row[0] * inv_a) / 255);
                dst_row[1] = static_cast<std::uint8_t>((sp[1] * sa + dst_row[1] * inv_a) / 255);
                dst_row[2] = static_cast<std::uint8_t>((sp[2] * sa + dst_row[2] * inv_a) / 255);
                dst_row[3] = 255;
            }
            dst_row += 4;
            stats_.total_pixels_composited++;
        }
    }
}

bool MultiWindowCompositor::composite(bool force_full) {
    std::lock_guard<std::mutex> guard(lock_);
    if (!display_ || !window_mgr_) return false;

    std::uint32_t disp_w = display_->width();
    std::uint32_t disp_h = display_->height();
    std::uint8_t* disp_buf = display_->raw_data();
    if (!disp_buf || disp_w == 0 || disp_h == 0) return false;

    // Collect damage from windows
    auto windows = window_mgr_->get_visible_windows_z_ordered();
    for (const auto& w : windows) {
        if (w->is_dirty) {
            desktop_damage_.add_dirty_rect(w->frame);
        }
        if (w->surface && w->surface->damage_tracker().has_damage()) {
            desktop_damage_.add_dirty_rect(w->frame);
        }
    }

    if (force_full) {
        desktop_damage_.mark_full_damage(disp_w, disp_h);
    }

    if (!desktop_damage_.has_damage()) {
        stats_.skipped_frames++;
        return true;
    }

    stats_.total_frames++;
    Rect damage_box = desktop_damage_.bounding_box();
    if (damage_box.width == disp_w && damage_box.height == disp_h) {
        stats_.full_redraw_frames++;
    } else {
        stats_.partial_redraw_frames++;
    }

    // 1. Clear background for damaged region
    clear_damaged_background(damage_box);

    // 2. Composite visible windows in z-order back to front
    for (std::size_t i = 0; i < windows.size(); ++i) {
        const auto& w = windows[i];
        if (!w || !w->is_visible() || w->state == WindowState::Closed) continue;

        // Stage 216.12 Occlusion check: Is window completely covered by a higher opaque window?
        bool is_occluded = false;
        for (std::size_t j = i + 1; j < windows.size(); ++j) {
            const auto& higher = windows[j];
            if (higher->is_visible() && higher->frame.contains(w->frame.x, w->frame.y) &&
                higher->frame.contains(w->frame.x + w->frame.width - 1, w->frame.y + w->frame.height - 1)) {
                is_occluded = true;
                break;
            }
        }

        if (is_occluded) {
            stats_.occluded_windows_skipped++;
            continue;
        }

        // Render non-client decorations (title bar, buttons, border)
        WindowDecorations::render(disp_buf, disp_w, disp_h, display_->stride(), *w);

        // Blit client surface
        blit_client_surface(*w, damage_box);

        window_mgr_->clear_window_dirty(w->id);
        if (w->surface) {
            w->surface->clear_damage();
        }
    }

    desktop_damage_.clear();
    return true;
}

} // namespace winios::graphics

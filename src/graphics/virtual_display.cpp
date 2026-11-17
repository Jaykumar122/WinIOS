#include "graphics/virtual_display.h"

#include <cmath>
#include <limits>

namespace winios::graphics {

// -----------------------------------------------------------------------------
// DisplayConfiguration Implementation
// -----------------------------------------------------------------------------
DisplayError DisplayConfiguration::validate_error() const noexcept {
    std::uint32_t bpp = winios::graphics::bytes_per_pixel(pixel_format);
    if (bpp == 0 || (bpp != 2 && bpp != 4) ||
        (pixel_format != PixelFormat::B8G8R8A8_UNORM &&
         pixel_format != PixelFormat::R8G8B8A8_UNORM &&
         pixel_format != PixelFormat::R5G6B5_UNORM)) {
        return DisplayError::UnsupportedFormat;
    }

    if (width == 0 || height == 0 || width > 8192u || height > 8192u) {
        return DisplayError::InvalidDimensions;
    }

    // Check multiplication overflow for width * bpp
    std::uint64_t min_stride = static_cast<std::uint64_t>(width) * bpp;
    if (min_stride > std::numeric_limits<std::uint32_t>::max()) {
        return DisplayError::IntegerOverflow;
    }

    if (row_stride < static_cast<std::uint32_t>(min_stride)) {
        return DisplayError::InvalidStride;
    }

    // Check total allocation overflow
    std::uint64_t total_bytes = static_cast<std::uint64_t>(row_stride) * height;
    if (total_bytes > max_allocation_size) {
        return DisplayError::AllocationExceeded;
    }

    if (refresh_interval_us < 1000u || refresh_interval_us > 1000000u) {
        return DisplayError::InvalidDimensions; // Refresh rate out of sensible bounds
    }

    if (display_scale <= 0.0f || display_scale > 10.0f) {
        return DisplayError::InvalidDimensions;
    }

    return DisplayError::Success;
}

std::uint64_t DisplayConfiguration::total_framebuffer_bytes() const noexcept {
    return static_cast<std::uint64_t>(row_stride) * height;
}

void DisplayConfiguration::set_orientation(DisplayOrientation orient) noexcept {
    if (orientation == orient) {
        return;
    }

    bool was_landscape = is_landscape(orientation);
    bool now_landscape = is_landscape(orient);

    orientation = orient;

    if (was_landscape != now_landscape) {
        std::swap(width, height);
        std::uint32_t bpp = winios::graphics::bytes_per_pixel(pixel_format);
        row_stride = width * bpp;
    }
}

// -----------------------------------------------------------------------------
// RasterPrimitives Implementation
// -----------------------------------------------------------------------------
void RasterPrimitives::blend_pixel(
    std::uint8_t* dst,
    const std::uint8_t* src,
    PixelFormat fmt,
    float opacity) noexcept {

    if (!dst || !src) return;

    if (fmt == PixelFormat::R5G6B5_UNORM) {
        // RGB565 has no alpha channel, direct overwrite
        std::memcpy(dst, src, 2);
        return;
    }

    // Determine alpha channel index (3 for both BGRA and RGBA)
    std::uint32_t src_a = src[3];
    if (opacity < 1.0f) {
        float clamped_op = std::clamp(opacity, 0.0f, 1.0f);
        src_a = static_cast<std::uint32_t>(std::round(src_a * clamped_op));
    }

    if (src_a == 0) {
        return; // Fully transparent, nothing to do
    }

    if (src_a >= 255) {
        // Fully opaque, direct copy
        std::memcpy(dst, src, 4);
        return;
    }

    std::uint32_t inv_a = 255u - src_a;
    std::uint32_t dst_a = dst[3];

    // Standard alpha blending:
    // C_out = (C_src * A_src + C_dst * (255 - A_src)) / 255
    for (std::size_t i = 0; i < 3; ++i) {
        std::uint32_t blended = (static_cast<std::uint32_t>(src[i]) * src_a) +
                                (static_cast<std::uint32_t>(dst[i]) * inv_a);
        dst[i] = static_cast<std::uint8_t>((blended + 127u) / 255u);
    }

    // A_out = A_src + (A_dst * (255 - A_src)) / 255
    std::uint32_t out_a = src_a + ((dst_a * inv_a + 127u) / 255u);
    dst[3] = static_cast<std::uint8_t>(std::min(out_a, 255u));
}

Rect RasterPrimitives::clip_rect(const Rect& target, const Rect& bounds) noexcept {
    if (target.empty() || bounds.empty()) {
        return Rect{0, 0, 0, 0};
    }

    std::uint32_t x1 = std::max(target.x, bounds.x);
    std::uint32_t y1 = std::max(target.y, bounds.y);
    std::uint32_t x2 = std::min(target.x + target.width, bounds.x + bounds.width);
    std::uint32_t y2 = std::min(target.y + target.height, bounds.y + bounds.height);

    if (x2 <= x1 || y2 <= y1) {
        return Rect{0, 0, 0, 0};
    }

    return Rect{x1, y1, x2 - x1, y2 - y1};
}

void RasterPrimitives::fill_rect(
    std::uint8_t* buffer,
    std::uint32_t buf_w,
    std::uint32_t buf_h,
    std::uint32_t stride,
    PixelFormat fmt,
    const Rect& rect,
    std::uint32_t color,
    const Rect* clip) noexcept {

    if (!buffer || buf_w == 0 || buf_h == 0 || rect.empty()) return;

    Rect effective = rect;
    if (clip) {
        effective = clip_rect(effective, *clip);
    }
    effective = clip_rect(effective, Rect{0, 0, buf_w, buf_h});

    if (effective.empty()) return;

    std::uint32_t bpp = bytes_per_pixel(fmt);
    std::uint8_t color_bytes[4];
    std::memcpy(color_bytes, &color, sizeof(color));

    for (std::uint32_t y = effective.y; y < effective.y + effective.height; ++y) {
        std::uint8_t* row = buffer + (static_cast<std::size_t>(y) * stride) + (effective.x * bpp);
        for (std::uint32_t x = 0; x < effective.width; ++x) {
            std::memcpy(row + (x * bpp), color_bytes, bpp);
        }
    }
}

void RasterPrimitives::blit(
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
    float opacity,
    const Rect* clip) noexcept {

    if (!dst || !src || dst_w == 0 || dst_h == 0 || src_w == 0 || src_h == 0) return;

    // Clip src_rect to source buffer dimensions
    Rect clipped_src = clip_rect(src_rect, Rect{0, 0, src_w, src_h});
    if (clipped_src.empty()) return;

    // Compute destination bounding box
    std::int32_t render_x = dst_x;
    std::int32_t render_y = dst_y;
    std::int32_t render_w = static_cast<std::int32_t>(clipped_src.width);
    std::int32_t render_h = static_cast<std::int32_t>(clipped_src.height);

    // Apply destination clip bounds
    std::int32_t clip_x1 = 0;
    std::int32_t clip_y1 = 0;
    std::int32_t clip_x2 = static_cast<std::int32_t>(dst_w);
    std::int32_t clip_y2 = static_cast<std::int32_t>(dst_h);

    if (clip) {
        clip_x1 = std::max(clip_x1, static_cast<std::int32_t>(clip->x));
        clip_y1 = std::max(clip_y1, static_cast<std::int32_t>(clip->y));
        clip_x2 = std::min(clip_x2, static_cast<std::int32_t>(clip->x + clip->width));
        clip_y2 = std::min(clip_y2, static_cast<std::int32_t>(clip->y + clip->height));
    }

    std::int32_t vis_x1 = std::max(render_x, clip_x1);
    std::int32_t vis_y1 = std::max(render_y, clip_y1);
    std::int32_t vis_x2 = std::min(render_x + render_w, clip_x2);
    std::int32_t vis_y2 = std::min(render_y + render_h, clip_y2);

    if (vis_x2 <= vis_x1 || vis_y2 <= vis_y1) return;

    std::uint32_t src_bpp = bytes_per_pixel(src_fmt);
    std::uint32_t dst_bpp = bytes_per_pixel(dst_fmt);

    for (std::int32_t dy = vis_y1; dy < vis_y2; ++dy) {
        std::uint32_t sy = clipped_src.y + static_cast<std::uint32_t>(dy - render_y);
        const std::uint8_t* src_row = src + (static_cast<std::size_t>(sy) * src_stride);
        std::uint8_t* dst_row = dst + (static_cast<std::size_t>(dy) * dst_stride);

        for (std::int32_t dx = vis_x1; dx < vis_x2; ++dx) {
            std::uint32_t sx = clipped_src.x + static_cast<std::uint32_t>(dx - render_x);
            const std::uint8_t* s_px = src_row + (sx * src_bpp);
            std::uint8_t* d_px = dst_row + (dx * dst_bpp);

            std::uint8_t temp_src[4];
            if (src_fmt == dst_fmt) {
                blend_pixel(d_px, s_px, dst_fmt, opacity);
            } else {
                // Convert src pixel format to dst pixel format first
                if (src_fmt == PixelFormat::B8G8R8A8_UNORM && dst_fmt == PixelFormat::R8G8B8A8_UNORM) {
                    temp_src[0] = s_px[2]; // R
                    temp_src[1] = s_px[1]; // G
                    temp_src[2] = s_px[0]; // B
                    temp_src[3] = s_px[3]; // A
                    blend_pixel(d_px, temp_src, dst_fmt, opacity);
                } else if (src_fmt == PixelFormat::R8G8B8A8_UNORM && dst_fmt == PixelFormat::B8G8R8A8_UNORM) {
                    temp_src[0] = s_px[2]; // B
                    temp_src[1] = s_px[1]; // G
                    temp_src[2] = s_px[0]; // R
                    temp_src[3] = s_px[3]; // A
                    blend_pixel(d_px, temp_src, dst_fmt, opacity);
                } else if (src_fmt == PixelFormat::R5G6B5_UNORM && dst_fmt == PixelFormat::B8G8R8A8_UNORM) {
                    std::uint16_t raw_val = 0;
                    std::memcpy(&raw_val, s_px, 2);
                    std::uint8_t r = 0, g = 0, b = 0;
                    unpack_rgb565(raw_val, r, g, b);
                    temp_src[0] = b;
                    temp_src[1] = g;
                    temp_src[2] = r;
                    temp_src[3] = 255;
                    blend_pixel(d_px, temp_src, dst_fmt, opacity);
                } else {
                    // Fallback direct copy
                    std::memcpy(d_px, s_px, std::min(dst_bpp, src_bpp));
                }
            }
        }
    }
}

// -----------------------------------------------------------------------------
// VirtualDisplay Implementation
// -----------------------------------------------------------------------------
VirtualDisplay::VirtualDisplay(const DisplayConfiguration& config)
    : config_(config) {

    if (!config_.validate()) {
        // Fallback to valid defaults
        config_ = DisplayConfiguration{};
    }

    std::size_t total_bytes = static_cast<std::size_t>(config_.total_framebuffer_bytes());
    framebuffer_.assign(total_bytes, 0);
}

bool VirtualDisplay::reconfigure(const DisplayConfiguration& new_config) {
    if (!new_config.validate()) {
        return false;
    }

    std::lock_guard<std::mutex> guard(lock_);
    config_ = new_config;
    std::size_t total_bytes = static_cast<std::size_t>(config_.total_framebuffer_bytes());
    framebuffer_.assign(total_bytes, 0);
    return true;
}

bool VirtualDisplay::set_orientation(DisplayOrientation orient) {
    std::lock_guard<std::mutex> guard(lock_);
    config_.set_orientation(orient);
    if (!config_.validate()) {
        return false;
    }

    std::size_t total_bytes = static_cast<std::size_t>(config_.total_framebuffer_bytes());
    framebuffer_.assign(total_bytes, 0);
    return true;
}

bool VirtualDisplay::write_pixel(std::uint32_t x, std::uint32_t y, std::uint32_t color) noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    if (x >= config_.width || y >= config_.height) {
        return false;
    }

    std::uint32_t bpp = bytes_per_pixel(config_.pixel_format);
    std::size_t offset = (static_cast<std::size_t>(y) * config_.row_stride) + (x * bpp);
    if (offset + bpp > framebuffer_.size()) {
        return false;
    }

    std::memcpy(framebuffer_.data() + offset, &color, bpp);
    return true;
}

std::uint32_t VirtualDisplay::read_pixel(std::uint32_t x, std::uint32_t y) const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    if (x >= config_.width || y >= config_.height) {
        return 0;
    }

    std::uint32_t bpp = bytes_per_pixel(config_.pixel_format);
    std::size_t offset = (static_cast<std::size_t>(y) * config_.row_stride) + (x * bpp);
    if (offset + bpp > framebuffer_.size()) {
        return 0;
    }

    std::uint32_t val = 0;
    std::memcpy(&val, framebuffer_.data() + offset, bpp);
    return val;
}

void VirtualDisplay::clear(std::uint32_t color) noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    std::uint32_t bpp = bytes_per_pixel(config_.pixel_format);
    if (color == 0 || (bpp == 4 && color == 0xFFFFFFFF)) {
        std::memset(framebuffer_.data(), color == 0 ? 0 : 0xFF, framebuffer_.size());
        return;
    }

    RasterPrimitives::fill_rect(
        framebuffer_.data(),
        config_.width,
        config_.height,
        config_.row_stride,
        config_.pixel_format,
        Rect{0, 0, config_.width, config_.height},
        color);
}

void VirtualDisplay::fill_rect(const Rect& rect, std::uint32_t color) noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    RasterPrimitives::fill_rect(
        framebuffer_.data(),
        config_.width,
        config_.height,
        config_.row_stride,
        config_.pixel_format,
        rect,
        color);
}

bool VirtualDisplay::copy_from(const std::uint8_t* src, std::size_t src_size, std::uint32_t src_stride) noexcept {
    if (!src) return false;

    std::lock_guard<std::mutex> guard(lock_);
    std::size_t target_size = framebuffer_.size();
    if (src_stride == 0 || src_stride == config_.row_stride) {
        if (src_size < target_size) return false;
        std::memcpy(framebuffer_.data(), src, target_size);
        return true;
    }

    // Row-by-row copy with stride conversion
    std::uint32_t row_bytes = std::min(src_stride, config_.row_stride);
    if (src_size < static_cast<std::size_t>(src_stride) * config_.height) {
        return false;
    }

    for (std::uint32_t y = 0; y < config_.height; ++y) {
        const std::uint8_t* s_row = src + (static_cast<std::size_t>(y) * src_stride);
        std::uint8_t* d_row = framebuffer_.data() + (static_cast<std::size_t>(y) * config_.row_stride);
        std::memcpy(d_row, s_row, row_bytes);
    }
    return true;
}

bool VirtualDisplay::copy_to(std::uint8_t* dst, std::size_t dst_size, std::uint32_t dst_stride) const noexcept {
    if (!dst) return false;

    std::lock_guard<std::mutex> guard(lock_);
    std::size_t target_size = framebuffer_.size();
    if (dst_stride == 0 || dst_stride == config_.row_stride) {
        if (dst_size < target_size) return false;
        std::memcpy(dst, framebuffer_.data(), target_size);
        return true;
    }

    // Row-by-row copy with stride conversion
    std::uint32_t row_bytes = std::min(dst_stride, config_.row_stride);
    if (dst_size < static_cast<std::size_t>(dst_stride) * config_.height) {
        return false;
    }

    for (std::uint32_t y = 0; y < config_.height; ++y) {
        const std::uint8_t* s_row = framebuffer_.data() + (static_cast<std::size_t>(y) * config_.row_stride);
        std::uint8_t* d_row = dst + (static_cast<std::size_t>(y) * dst_stride);
        std::memcpy(d_row, s_row, row_bytes);
    }
    return true;
}

bool VirtualDisplay::convert_format(
    const std::uint8_t* src,
    PixelFormat src_fmt,
    std::uint8_t* dst,
    PixelFormat dst_fmt,
    std::size_t pixel_count) noexcept {

    if (!src || !dst || pixel_count == 0) return false;

    if (src_fmt == dst_fmt) {
        std::uint32_t bpp = bytes_per_pixel(src_fmt);
        std::memcpy(dst, src, pixel_count * bpp);
        return true;
    }

    for (std::size_t i = 0; i < pixel_count; ++i) {
        if (src_fmt == PixelFormat::B8G8R8A8_UNORM && dst_fmt == PixelFormat::R8G8B8A8_UNORM) {
            const std::uint8_t* s = src + (i * 4);
            std::uint8_t* d = dst + (i * 4);
            d[0] = s[2]; // R
            d[1] = s[1]; // G
            d[2] = s[0]; // B
            d[3] = s[3]; // A
        } else if (src_fmt == PixelFormat::R8G8B8A8_UNORM && dst_fmt == PixelFormat::B8G8R8A8_UNORM) {
            const std::uint8_t* s = src + (i * 4);
            std::uint8_t* d = dst + (i * 4);
            d[0] = s[2]; // B
            d[1] = s[1]; // G
            d[2] = s[0]; // R
            d[3] = s[3]; // A
        } else if (src_fmt == PixelFormat::B8G8R8A8_UNORM && dst_fmt == PixelFormat::R5G6B5_UNORM) {
            const std::uint8_t* s = src + (i * 4);
            std::uint16_t rgb = RasterPrimitives::pack_rgb565(s[2], s[1], s[0]);
            std::memcpy(dst + (i * 2), &rgb, 2);
        } else if (src_fmt == PixelFormat::R8G8B8A8_UNORM && dst_fmt == PixelFormat::R5G6B5_UNORM) {
            const std::uint8_t* s = src + (i * 4);
            std::uint16_t rgb = RasterPrimitives::pack_rgb565(s[0], s[1], s[2]);
            std::memcpy(dst + (i * 2), &rgb, 2);
        } else if (src_fmt == PixelFormat::R5G6B5_UNORM && dst_fmt == PixelFormat::B8G8R8A8_UNORM) {
            std::uint16_t rgb = 0;
            std::memcpy(&rgb, src + (i * 2), 2);
            std::uint8_t r = 0, g = 0, b = 0;
            RasterPrimitives::unpack_rgb565(rgb, r, g, b);
            std::uint8_t* d = dst + (i * 4);
            d[0] = b;
            d[1] = g;
            d[2] = r;
            d[3] = 255;
        } else if (src_fmt == PixelFormat::R5G6B5_UNORM && dst_fmt == PixelFormat::R8G8B8A8_UNORM) {
            std::uint16_t rgb = 0;
            std::memcpy(&rgb, src + (i * 2), 2);
            std::uint8_t r = 0, g = 0, b = 0;
            RasterPrimitives::unpack_rgb565(rgb, r, g, b);
            std::uint8_t* d = dst + (i * 4);
            d[0] = r;
            d[1] = g;
            d[2] = b;
            d[3] = 255;
        } else {
            return false;
        }
    }
    return true;
}

} // namespace winios::graphics

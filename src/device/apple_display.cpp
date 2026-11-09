#include "device/apple_display.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace winios::device {

AppleDisplay::AppleDisplay(std::uint32_t device_id,
                           std::uint64_t mmio_base,
                           std::uint64_t mmio_size,
                           std::uint64_t fb_phys_base)
    : VirtualDevice(device_id, "AppleDisplay", VirtualDeviceType::GenericMmio, mmio_base, mmio_size),
      fb_phys_base_(fb_phys_base),
      fb_memory_(kFramebufferBytes, 0) {
    reset();
}

void AppleDisplay::reset() noexcept {
    std::lock_guard<std::mutex> lock(lock_);
    width_ = kDefaultWidth;
    height_ = kDefaultHeight;
    depth_ = kDefaultDepth;
    row_bytes_ = kDefaultRowBytes;
    display_enabled_ = true;
    std::fill(fb_memory_.begin(), fb_memory_.end(), std::uint8_t{0});
}

void AppleDisplay::set_pixel(std::uint32_t x, std::uint32_t y, std::uint32_t argb) noexcept {
    if (x >= width_ || y >= height_) return;
    std::lock_guard<std::mutex> lock(lock_);

    const std::size_t offset = static_cast<std::size_t>(y) * row_bytes_ + static_cast<std::size_t>(x) * 4;
    if (offset + 4 <= fb_memory_.size()) {
        std::memcpy(&fb_memory_[offset], &argb, 4);
    }
}

std::uint32_t AppleDisplay::get_pixel(std::uint32_t x, std::uint32_t y) const noexcept {
    if (x >= width_ || y >= height_) return 0u;
    std::lock_guard<std::mutex> lock(lock_);

    const std::size_t offset = static_cast<std::size_t>(y) * row_bytes_ + static_cast<std::size_t>(x) * 4;
    if (offset + 4 <= fb_memory_.size()) {
        std::uint32_t argb = 0;
        std::memcpy(&argb, &fb_memory_[offset], 4);
        return argb;
    }
    return 0u;
}

void AppleDisplay::clear(std::uint32_t argb) noexcept {
    std::lock_guard<std::mutex> lock(lock_);
    const std::size_t total_pixels = static_cast<std::size_t>(width_) * height_;
    auto* p = reinterpret_cast<std::uint32_t*>(fb_memory_.data());
    for (std::size_t i = 0; i < total_pixels; ++i) {
        p[i] = argb;
    }
}

void AppleDisplay::render_boot_splash() {
    std::lock_guard<std::mutex> lock(lock_);
    // Black background
    std::fill(fb_memory_.begin(), fb_memory_.end(), std::uint8_t{0});

    // Draw centered silver/white Apple boot glyph / indicator (100x100 square)
    const std::uint32_t cx = width_ / 2;
    const std::uint32_t cy = height_ / 2;
    const std::uint32_t glyph_color = 0x00FFFFFFu; // White ARGB

    for (std::uint32_t y = cy - 50; y < cy + 50; ++y) {
        for (std::uint32_t x = cx - 50; x < cx + 50; ++x) {
            const std::size_t off = static_cast<std::size_t>(y) * row_bytes_ + static_cast<std::size_t>(x) * 4;
            if (off + 4 <= fb_memory_.size()) {
                std::memcpy(&fb_memory_[off], &glyph_color, 4);
            }
        }
    }
}

std::vector<std::uint8_t> AppleDisplay::export_ppm() const {
    std::lock_guard<std::mutex> lock(lock_);
    std::string header = "P6\n" + std::to_string(width_) + " " + std::to_string(height_) + "\n255\n";

    std::vector<std::uint8_t> ppm(header.begin(), header.end());
    ppm.reserve(ppm.size() + static_cast<std::size_t>(width_) * height_ * 3);

    for (std::uint32_t y = 0; y < height_; ++y) {
        const std::size_t row_off = static_cast<std::size_t>(y) * row_bytes_;
        for (std::uint32_t x = 0; x < width_; ++x) {
            const std::size_t off = row_off + static_cast<std::size_t>(x) * 4;
            std::uint32_t argb = 0;
            if (off + 4 <= fb_memory_.size()) {
                std::memcpy(&argb, &fb_memory_[off], 4);
            }
            const auto r = static_cast<std::uint8_t>((argb >> 16) & 0xFFu);
            const auto g = static_cast<std::uint8_t>((argb >> 8) & 0xFFu);
            const auto b = static_cast<std::uint8_t>(argb & 0xFFu);
            ppm.push_back(r);
            ppm.push_back(g);
            ppm.push_back(b);
        }
    }

    return ppm;
}

std::uint64_t AppleDisplay::read_register(std::uint64_t offset, unsigned width) {
    std::lock_guard<std::mutex> lock(lock_);
    (void)width;

    switch (offset) {
    case kRegWidth:
        return static_cast<std::uint64_t>(width_);
    case kRegHeight:
        return static_cast<std::uint64_t>(height_);
    case kRegDepth:
        return static_cast<std::uint64_t>(depth_);
    case kRegRowBytes:
        return static_cast<std::uint64_t>(row_bytes_);
    case kRegFbBaseLow:
        return static_cast<std::uint64_t>(fb_phys_base_ & 0xFFFFFFFFu);
    case kRegFbBaseHigh:
        return static_cast<std::uint64_t>((fb_phys_base_ >> 32) & 0xFFFFFFFFu);
    case kRegStatus:
        return display_enabled_ ? 1ULL : 0ULL;
    case kRegControl:
        return display_enabled_ ? 1ULL : 0ULL;
    default:
        return 0ULL;
    }
}

void AppleDisplay::write_register(std::uint64_t offset, std::uint64_t value, unsigned width) {
    std::lock_guard<std::mutex> lock(lock_);
    (void)width;
    const auto val32 = static_cast<std::uint32_t>(value);

    switch (offset) {
    case kRegControl:
        display_enabled_ = (val32 & 0x01u) != 0;
        break;
    case kRegFbBaseLow:
        fb_phys_base_ = (fb_phys_base_ & ~0xFFFFFFFFULL) | static_cast<std::uint64_t>(val32);
        break;
    case kRegFbBaseHigh:
        fb_phys_base_ = (fb_phys_base_ & 0xFFFFFFFFULL) | (static_cast<std::uint64_t>(val32) << 32);
        break;
    default:
        break;
    }
}

std::vector<std::uint8_t> AppleDisplay::serialize_state() const {
    std::lock_guard<std::mutex> lock(lock_);
    std::vector<std::uint8_t> out;
    out.reserve(64);

    const std::uint32_t magic = 0x44495350u; // 'DISP'
    const std::uint32_t version = 1u;

    auto append_u32 = [&](std::uint32_t v) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
        out.insert(out.end(), p, p + 4);
    };
    auto append_u64 = [&](std::uint64_t v) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
        out.insert(out.end(), p, p + 8);
    };

    append_u32(magic);
    append_u32(version);
    append_u32(width_);
    append_u32(height_);
    append_u32(depth_);
    append_u32(row_bytes_);
    append_u64(fb_phys_base_);
    append_u32(display_enabled_ ? 1u : 0u);

    return out;
}

bool AppleDisplay::deserialize_state(const std::uint8_t* data, std::size_t size) {
    if (!data || size < 36) return false;
    std::lock_guard<std::mutex> lock(lock_);

    const auto* p = data;
    auto read_u32 = [&]() -> std::uint32_t {
        std::uint32_t v = 0;
        std::memcpy(&v, p, 4);
        p += 4;
        return v;
    };
    auto read_u64 = [&]() -> std::uint64_t {
        std::uint64_t v = 0;
        std::memcpy(&v, p, 8);
        p += 8;
        return v;
    };

    const std::uint32_t magic = read_u32();
    const std::uint32_t version = read_u32();
    if (magic != 0x44495350u || version != 1u) return false;

    width_ = read_u32();
    height_ = read_u32();
    depth_ = read_u32();
    row_bytes_ = read_u32();
    fb_phys_base_ = read_u64();
    display_enabled_ = (read_u32() != 0);

    return true;
}

} // namespace winios::device

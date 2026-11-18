#include "resources/resource_manager.h"
#include <cstring>

namespace winios::resources {

ResourceManager::ResourceManager(std::shared_ptr<ResourceBundle> bundle, uint32_t process_id)
    : bundle_(std::move(bundle)), process_id_(process_id) {}

ResourceManager::~ResourceManager() {
    ClearCache();
}

bool ResourceManager::LoadText(const std::string& res_id, std::string* text_out, const std::string& locale) {
    if (!text_out || !bundle_) return false;
    std::lock_guard<std::mutex> lock(mutex_);

    std::string key = locale + ":" + res_id;
    auto it = text_cache_.find(key);
    if (it != text_cache_.end()) {
        *text_out = it->second;
        return true;
    }

    const auto* item = bundle_->GetResource(res_id, locale);
    if (!item) return false;

    std::string text(reinterpret_cast<const char*>(item->data.data()), item->data.size());
    if (cache_bytes_ + text.size() <= kMaxCacheBytes) {
        cache_bytes_ += text.size();
        text_cache_[key] = text;
    }
    *text_out = std::move(text);
    return true;
}

bool ResourceManager::LoadImage(const std::string& res_id, DecodedImage* img_out, const std::string& locale) {
    if (!img_out || !bundle_) return false;
    std::lock_guard<std::mutex> lock(mutex_);

    std::string key = locale + ":" + res_id;
    auto it = image_cache_.find(key);
    if (it != image_cache_.end()) {
        *img_out = it->second;
        return true;
    }

    const auto* item = bundle_->GetResource(res_id, locale);
    if (!item) return false;

    DecodedImage img{};
    if (!DecodeBmp(item->data.data(), item->data.size(), &img)) {
        return false;
    }

    size_t img_bytes = img.pixels_argb.size() * sizeof(uint32_t);
    if (cache_bytes_ + img_bytes <= kMaxCacheBytes) {
        cache_bytes_ += img_bytes;
        image_cache_[key] = img;
    }
    *img_out = std::move(img);
    return true;
}

bool ResourceManager::LoadRawData(const std::string& res_id, std::vector<uint8_t>* data_out, const std::string& locale) {
    if (!data_out || !bundle_) return false;
    const auto* item = bundle_->GetResource(res_id, locale);
    if (!item) return false;
    *data_out = item->data;
    return true;
}

void ResourceManager::ClearCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    image_cache_.clear();
    text_cache_.clear();
    cache_bytes_ = 0;
}

size_t ResourceManager::GetCacheUsageBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_bytes_;
}

bool ResourceManager::DecodeBmp(const uint8_t* data, size_t size, DecodedImage* img_out) {
    if (!data || size < 54 || !img_out) return false;

    // Check BMP magic 'BM' (0x42, 0x4D)
    if (data[0] != 0x42 || data[1] != 0x4D) return false;

    uint32_t data_offset = *reinterpret_cast<const uint32_t*>(data + 10);
    uint32_t header_size = *reinterpret_cast<const uint32_t*>(data + 14);
    int32_t width = *reinterpret_cast<const int32_t*>(data + 18);
    int32_t height = *reinterpret_cast<const int32_t*>(data + 22);
    uint16_t bpp = *reinterpret_cast<const uint16_t*>(data + 28);
    uint32_t compression = *reinterpret_cast<const uint32_t*>(data + 30);

    if (header_size < 40 || width <= 0 || height == 0 || (bpp != 24 && bpp != 32) || compression != 0) {
        return false; // Only uncompressed 24-bit and 32-bit BMP supported
    }

    bool top_down = false;
    uint32_t uheight = static_cast<uint32_t>(height);
    if (height < 0) {
        top_down = true;
        uheight = static_cast<uint32_t>(-height);
    }
    uint32_t uwidth = static_cast<uint32_t>(width);

    if (data_offset >= size) return false;

    img_out->width = uwidth;
    img_out->height = uheight;
    img_out->pixels_argb.resize(uwidth * uheight);

    const size_t row_bytes = (uwidth * (bpp / 8) + 3) & ~3;
    const uint8_t* pixel_src = data + data_offset;

    for (uint32_t y = 0; y < uheight; ++y) {
        uint32_t target_y = top_down ? y : (uheight - 1 - y);
        const uint8_t* row = pixel_src + y * row_bytes;
        if (row + row_bytes > data + size) return false;

        for (uint32_t x = 0; x < uwidth; ++x) {
            uint32_t argb = 0xFF000000;
            if (bpp == 32) {
                uint8_t b = row[x * 4 + 0];
                uint8_t g = row[x * 4 + 1];
                uint8_t r = row[x * 4 + 2];
                uint8_t a = row[x * 4 + 3];
                argb = (static_cast<uint32_t>(a) << 24) |
                       (static_cast<uint32_t>(r) << 16) |
                       (static_cast<uint32_t>(g) << 8)  |
                       static_cast<uint32_t>(b);
            } else {
                uint8_t b = row[x * 3 + 0];
                uint8_t g = row[x * 3 + 1];
                uint8_t r = row[x * 3 + 2];
                argb = (0xFFu << 24) |
                       (static_cast<uint32_t>(r) << 16) |
                       (static_cast<uint32_t>(g) << 8)  |
                       static_cast<uint32_t>(b);
            }
            img_out->pixels_argb[target_y * uwidth + x] = argb;
        }
    }

    img_out->is_valid = true;
    return true;
}

} // namespace winios::resources

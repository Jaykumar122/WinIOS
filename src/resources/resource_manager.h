#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

#include "resources/resource_bundle.h"

namespace winios::resources {

struct DecodedImage {
    uint32_t width{0};
    uint32_t height{0};
    std::vector<uint32_t> pixels_argb;
    bool is_valid{false};
};

class ResourceManager {
public:
    static constexpr size_t kMaxCacheBytes = 8 * 1024 * 1024; // 8 MB

    explicit ResourceManager(std::shared_ptr<ResourceBundle> bundle, uint32_t process_id);
    ~ResourceManager();

    bool LoadText(const std::string& res_id, std::string* text_out, const std::string& locale = "default");
    bool LoadImage(const std::string& res_id, DecodedImage* img_out, const std::string& locale = "default");
    bool LoadRawData(const std::string& res_id, std::vector<uint8_t>* data_out, const std::string& locale = "default");

    void ClearCache();
    size_t GetCacheUsageBytes() const;
    uint32_t GetProcessId() const { return process_id_; }

    static bool DecodeBmp(const uint8_t* data, size_t size, DecodedImage* img_out);

private:
    std::shared_ptr<ResourceBundle> bundle_;
    uint32_t process_id_{0};
    mutable std::mutex mutex_;
    std::unordered_map<std::string, DecodedImage> image_cache_;
    std::unordered_map<std::string, std::string> text_cache_;
    size_t cache_bytes_{0};
};

} // namespace winios::resources

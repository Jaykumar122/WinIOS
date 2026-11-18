#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace winios::resources {

enum class ResourceType : uint32_t {
    kUnknown = 0,
    kImage = 1,
    kIcon = 2,
    kText = 3,
    kConfig = 4,
    kBinary = 5
};

struct ResourceItem {
    std::string id;
    std::string path;
    ResourceType type{ResourceType::kUnknown};
    std::string locale{"default"};
    uint64_t size{0};
    std::string sha256_hex;
    std::vector<uint8_t> data;
};

class ResourceBundle {
public:
    static constexpr size_t kMaxBundleSize = 16 * 1024 * 1024; // 16 MB
    static constexpr size_t kMaxResourceSize = 4 * 1024 * 1024; // 4 MB

    ResourceBundle() = default;
    ~ResourceBundle() = default;

    bool AddResource(const ResourceItem& item);
    bool HasResource(const std::string& id, const std::string& locale = "default") const;
    const ResourceItem* GetResource(const std::string& id, const std::string& locale = "default") const;

    size_t GetResourceCount() const { return items_.size(); }
    size_t GetTotalSize() const;
    void   Clear() { items_.clear(); index_.clear(); }

private:
    std::string MakeKey(const std::string& id, const std::string& locale) const;

    std::vector<ResourceItem> items_;
    std::unordered_map<std::string, size_t> index_;
};

} // namespace winios::resources

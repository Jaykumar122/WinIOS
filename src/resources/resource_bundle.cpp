#include "resources/resource_bundle.h"

namespace winios::resources {

std::string ResourceBundle::MakeKey(const std::string& id, const std::string& locale) const {
    return locale + ":" + id;
}

bool ResourceBundle::AddResource(const ResourceItem& item) {
    if (item.id.empty() || item.data.empty()) return false;
    if (item.data.size() > kMaxResourceSize) return false;
    if (GetTotalSize() + item.data.size() > kMaxBundleSize) return false;

    std::string key = MakeKey(item.id, item.locale);
    if (index_.find(key) != index_.end()) {
        return false; // Duplicate
    }

    size_t idx = items_.size();
    items_.push_back(item);
    items_.back().size = item.data.size();
    index_[key] = idx;
    return true;
}

bool ResourceBundle::HasResource(const std::string& id, const std::string& locale) const {
    if (index_.find(MakeKey(id, locale)) != index_.end()) return true;
    if (locale != "default" && index_.find(MakeKey(id, "default")) != index_.end()) return true;
    return false;
}

const ResourceItem* ResourceBundle::GetResource(const std::string& id, const std::string& locale) const {
    auto it = index_.find(MakeKey(id, locale));
    if (it != index_.end()) return &items_[it->second];

    if (locale != "default") {
        it = index_.find(MakeKey(id, "default"));
        if (it != index_.end()) return &items_[it->second];
    }
    return nullptr;
}

size_t ResourceBundle::GetTotalSize() const {
    size_t total = 0;
    for (const auto& item : items_) {
        total += item.data.size();
    }
    return total;
}

} // namespace winios::resources

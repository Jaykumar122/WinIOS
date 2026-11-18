#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>

#include "storage/isolated_storage.h"

namespace winios::runtime {

enum class PrefType : uint32_t {
    kString = 1,
    kInt = 2,
    kBool = 3
};

struct PrefValue {
    PrefType type{PrefType::kString};
    std::string str_val;
    int32_t int_val{0};
    bool bool_val{false};
};

class AppPreferences {
public:
    AppPreferences(std::shared_ptr<storage::IsolatedStorage> storage, const std::string& bundle_id);
    ~AppPreferences() = default;

    void SetString(const std::string& key, const std::string& value);
    std::string GetString(const std::string& key, const std::string& default_val = "") const;

    void SetInt(const std::string& key, int32_t value);
    int32_t GetInt(const std::string& key, int32_t default_val = 0) const;

    void SetBool(const std::string& key, bool value);
    bool GetBool(const std::string& key, bool default_val = false) const;

    bool HasKey(const std::string& key) const;
    void RemoveKey(const std::string& key);
    void Clear();

    bool Save();
    bool Load();

    const std::string& GetBundleId() const { return bundle_id_; }

private:
    std::shared_ptr<storage::IsolatedStorage> storage_;
    std::string bundle_id_;
    std::string pref_file_path_{"preferences.cfg"};
    mutable std::mutex mutex_;
    std::unordered_map<std::string, PrefValue> values_;
};

} // namespace winios::runtime

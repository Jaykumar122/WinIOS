#include "runtime/app_preferences.h"
#include <sstream>

namespace winios::runtime {

AppPreferences::AppPreferences(std::shared_ptr<storage::IsolatedStorage> storage, const std::string& bundle_id)
    : storage_(std::move(storage)), bundle_id_(bundle_id) {}

void AppPreferences::SetString(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    PrefValue pv;
    pv.type = PrefType::kString;
    pv.str_val = value;
    values_[key] = pv;
}

std::string AppPreferences::GetString(const std::string& key, const std::string& default_val) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = values_.find(key);
    if (it != values_.end() && it->second.type == PrefType::kString) {
        return it->second.str_val;
    }
    return default_val;
}

void AppPreferences::SetInt(const std::string& key, int32_t value) {
    std::lock_guard<std::mutex> lock(mutex_);
    PrefValue pv;
    pv.type = PrefType::kInt;
    pv.int_val = value;
    values_[key] = pv;
}

int32_t AppPreferences::GetInt(const std::string& key, int32_t default_val) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = values_.find(key);
    if (it != values_.end() && it->second.type == PrefType::kInt) {
        return it->second.int_val;
    }
    return default_val;
}

void AppPreferences::SetBool(const std::string& key, bool value) {
    std::lock_guard<std::mutex> lock(mutex_);
    PrefValue pv;
    pv.type = PrefType::kBool;
    pv.bool_val = value;
    values_[key] = pv;
}

bool AppPreferences::GetBool(const std::string& key, bool default_val) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = values_.find(key);
    if (it != values_.end() && it->second.type == PrefType::kBool) {
        return it->second.bool_val;
    }
    return default_val;
}

bool AppPreferences::HasKey(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return values_.find(key) != values_.end();
}

void AppPreferences::RemoveKey(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    values_.erase(key);
}

void AppPreferences::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    values_.clear();
}

bool AppPreferences::Save() {
    if (!storage_) return false;
    std::lock_guard<std::mutex> lock(mutex_);

    std::ostringstream ss;
    ss << "# WINIOS PREFERENCES v1\n";
    for (const auto& [k, v] : values_) {
        if (v.type == PrefType::kString) {
            ss << "S:" << k << "=" << v.str_val << "\n";
        } else if (v.type == PrefType::kInt) {
            ss << "I:" << k << "=" << v.int_val << "\n";
        } else if (v.type == PrefType::kBool) {
            ss << "B:" << k << "=" << (v.bool_val ? "1" : "0") << "\n";
        }
    }
    std::string serialized = ss.str();
    return storage_->atomic_write(pref_file_path_, serialized.data(), serialized.size(), true) == storage::StorageResult::Success;
}

bool AppPreferences::Load() {
    if (!storage_) return false;
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<uint8_t> buf;
    if (storage_->read(pref_file_path_, buf) != storage::StorageResult::Success) {
        return false;
    }

    std::string text(reinterpret_cast<const char*>(buf.data()), buf.size());
    std::istringstream ss(text);
    std::string line;
    values_.clear();

    while (std::getline(ss, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line.size() < 4) continue;

        char type_char = line[0];
        if (line[1] != ':') continue;

        size_t eq_pos = line.find('=', 2);
        if (eq_pos == std::string::npos) continue;

        std::string key = line.substr(2, eq_pos - 2);
        std::string val = line.substr(eq_pos + 1);

        PrefValue pv;
        if (type_char == 'S') {
            pv.type = PrefType::kString;
            pv.str_val = val;
            values_[key] = pv;
        } else if (type_char == 'I') {
            pv.type = PrefType::kInt;
            pv.int_val = std::stoi(val);
            values_[key] = pv;
        } else if (type_char == 'B') {
            pv.type = PrefType::kBool;
            pv.bool_val = (val == "1" || val == "true");
            values_[key] = pv;
        }
    }
    return true;
}

} // namespace winios::runtime

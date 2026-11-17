#include "kernel/app_preferences.h"
#include <cstring>

namespace winios::kernel {

AppPreferencesManager& AppPreferencesManager::instance() noexcept {
    static AppPreferencesManager s_instance;
    return s_instance;
}

void AppPreferencesManager::reset() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    store_.clear();
}

bool AppPreferencesManager::set_int(const std::string& app_id, const std::string& key, std::int64_t val) {
    if (app_id.empty() || key.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    store_[app_id][key] = PrefValue::make_int(val);
    return true;
}

bool AppPreferencesManager::set_float(const std::string& app_id, const std::string& key, double val) {
    if (app_id.empty() || key.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    store_[app_id][key] = PrefValue::make_float(val);
    return true;
}

bool AppPreferencesManager::set_string(const std::string& app_id, const std::string& key, const std::string& val) {
    if (app_id.empty() || key.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    store_[app_id][key] = PrefValue::make_string(val);
    return true;
}

bool AppPreferencesManager::set_bool(const std::string& app_id, const std::string& key, bool val) {
    if (app_id.empty() || key.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    store_[app_id][key] = PrefValue::make_bool(val);
    return true;
}

bool AppPreferencesManager::set_bytes(const std::string& app_id, const std::string& key, const std::vector<std::uint8_t>& val) {
    if (app_id.empty() || key.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    store_[app_id][key] = PrefValue::make_bytes(val);
    return true;
}

std::optional<std::int64_t> AppPreferencesManager::get_int(const std::string& app_id, const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it_app = store_.find(app_id);
    if (it_app == store_.end()) return std::nullopt;
    auto it_val = it_app->second.find(key);
    if (it_val == it_app->second.end() || it_val->second.type != PrefType::Int) return std::nullopt;
    return std::get<std::int64_t>(it_val->second.value);
}

std::optional<double> AppPreferencesManager::get_float(const std::string& app_id, const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it_app = store_.find(app_id);
    if (it_app == store_.end()) return std::nullopt;
    auto it_val = it_app->second.find(key);
    if (it_val == it_app->second.end() || it_val->second.type != PrefType::Float) return std::nullopt;
    return std::get<double>(it_val->second.value);
}

std::optional<std::string> AppPreferencesManager::get_string(const std::string& app_id, const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it_app = store_.find(app_id);
    if (it_app == store_.end()) return std::nullopt;
    auto it_val = it_app->second.find(key);
    if (it_val == it_app->second.end() || it_val->second.type != PrefType::String) return std::nullopt;
    return std::get<std::string>(it_val->second.value);
}

std::optional<bool> AppPreferencesManager::get_bool(const std::string& app_id, const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it_app = store_.find(app_id);
    if (it_app == store_.end()) return std::nullopt;
    auto it_val = it_app->second.find(key);
    if (it_val == it_app->second.end() || it_val->second.type != PrefType::Bool) return std::nullopt;
    return std::get<bool>(it_val->second.value);
}

std::optional<std::vector<std::uint8_t>> AppPreferencesManager::get_bytes(const std::string& app_id, const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it_app = store_.find(app_id);
    if (it_app == store_.end()) return std::nullopt;
    auto it_val = it_app->second.find(key);
    if (it_val == it_app->second.end() || it_val->second.type != PrefType::Bytes) return std::nullopt;
    return std::get<std::vector<std::uint8_t>>(it_val->second.value);
}

bool AppPreferencesManager::has_key(const std::string& app_id, const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it_app = store_.find(app_id);
    if (it_app == store_.end()) return false;
    return it_app->second.find(key) != it_app->second.end();
}

bool AppPreferencesManager::remove_key(const std::string& app_id, const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it_app = store_.find(app_id);
    if (it_app == store_.end()) return false;
    return it_app->second.erase(key) > 0;
}

void AppPreferencesManager::clear_app_preferences(const std::string& app_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    store_.erase(app_id);
}

std::size_t AppPreferencesManager::app_key_count(const std::string& app_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it_app = store_.find(app_id);
    if (it_app == store_.end()) return 0;
    return it_app->second.size();
}

void AppPreferencesManager::serialize(std::vector<std::uint8_t>& dst) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint32_t magic = 0x50524546; // 'PREF'
    const std::uint32_t version = 1;

    auto append_u32 = [&](std::uint32_t val) {
        dst.insert(dst.end(), reinterpret_cast<const std::uint8_t*>(&val), reinterpret_cast<const std::uint8_t*>(&val) + 4);
    };
    auto append_u64 = [&](std::uint64_t val) {
        dst.insert(dst.end(), reinterpret_cast<const std::uint8_t*>(&val), reinterpret_cast<const std::uint8_t*>(&val) + 8);
    };
    auto append_str = [&](const std::string& s) {
        append_u32(static_cast<std::uint32_t>(s.size()));
        dst.insert(dst.end(), s.begin(), s.end());
    };

    append_u32(magic);
    append_u32(version);
    append_u32(static_cast<std::uint32_t>(store_.size()));

    for (const auto& app_pair : store_) {
        append_str(app_pair.first);
        append_u32(static_cast<std::uint32_t>(app_pair.second.size()));
        for (const auto& kv : app_pair.second) {
            append_str(kv.first);
            dst.push_back(static_cast<std::uint8_t>(kv.second.type));
            switch (kv.second.type) {
            case PrefType::Int:
                append_u64(static_cast<std::uint64_t>(std::get<std::int64_t>(kv.second.value)));
                break;
            case PrefType::Float: {
                double d = std::get<double>(kv.second.value);
                std::uint64_t raw = 0;
                std::memcpy(&raw, &d, 8);
                append_u64(raw);
                break;
            }
            case PrefType::String:
                append_str(std::get<std::string>(kv.second.value));
                break;
            case PrefType::Bool:
                dst.push_back(std::get<bool>(kv.second.value) ? 1 : 0);
                break;
            case PrefType::Bytes: {
                const auto& b = std::get<std::vector<std::uint8_t>>(kv.second.value);
                append_u32(static_cast<std::uint32_t>(b.size()));
                dst.insert(dst.end(), b.begin(), b.end());
                break;
            }
            }
        }
    }
}

bool AppPreferencesManager::deserialize(const std::uint8_t*& src, std::size_t& remaining) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (remaining < 12) return false;

    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t num_apps = 0;

    std::memcpy(&magic, src, 4); src += 4; remaining -= 4;
    std::memcpy(&version, src, 4); src += 4; remaining -= 4;
    std::memcpy(&num_apps, src, 4); src += 4; remaining -= 4;

    if (magic != 0x50524546 || version != 1) return false;

    store_.clear();

    auto read_str = [&]() -> std::optional<std::string> {
        if (remaining < 4) return std::nullopt;
        std::uint32_t len = 0;
        std::memcpy(&len, src, 4); src += 4; remaining -= 4;
        if (remaining < len) return std::nullopt;
        std::string s(reinterpret_cast<const char*>(src), len);
        src += len; remaining -= len;
        return s;
    };

    for (std::uint32_t i = 0; i < num_apps; ++i) {
        auto app_id_opt = read_str();
        if (!app_id_opt || remaining < 4) return false;
        std::string app_id = *app_id_opt;

        std::uint32_t num_keys = 0;
        std::memcpy(&num_keys, src, 4); src += 4; remaining -= 4;

        for (std::uint32_t j = 0; j < num_keys; ++j) {
            auto key_opt = read_str();
            if (!key_opt || remaining < 1) return false;
            std::string key = *key_opt;

            auto ptype = static_cast<PrefType>(*src);
            src++; remaining--;

            switch (ptype) {
            case PrefType::Int: {
                if (remaining < 8) return false;
                std::int64_t val = 0;
                std::memcpy(&val, src, 8); src += 8; remaining -= 8;
                store_[app_id][key] = PrefValue::make_int(val);
                break;
            }
            case PrefType::Float: {
                if (remaining < 8) return false;
                double val = 0;
                std::memcpy(&val, src, 8); src += 8; remaining -= 8;
                store_[app_id][key] = PrefValue::make_float(val);
                break;
            }
            case PrefType::String: {
                auto s_opt = read_str();
                if (!s_opt) return false;
                store_[app_id][key] = PrefValue::make_string(*s_opt);
                break;
            }
            case PrefType::Bool: {
                if (remaining < 1) return false;
                bool b = (*src != 0);
                src++; remaining--;
                store_[app_id][key] = PrefValue::make_bool(b);
                break;
            }
            case PrefType::Bytes: {
                if (remaining < 4) return false;
                std::uint32_t b_len = 0;
                std::memcpy(&b_len, src, 4); src += 4; remaining -= 4;
                if (remaining < b_len) return false;
                std::vector<std::uint8_t> bytes(src, src + b_len);
                src += b_len; remaining -= b_len;
                store_[app_id][key] = PrefValue::make_bytes(bytes);
                break;
            }
            }
        }
    }
    return true;
}

} // namespace winios::kernel

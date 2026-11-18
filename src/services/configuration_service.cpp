#include "services/configuration_service.h"
#include <sstream>

namespace winios::services {

ConfigurationService::ConfigurationService(std::shared_ptr<vfs::VirtualFileSystem> vfs)
    : vfs_(std::move(vfs)) {}

bool ConfigurationService::set_string(const std::string& bundle_id, const std::string& key, const std::string& val) {
    if (bundle_id.empty() || key.empty() || key.size() > kMaxKeyLength || val.size() > kMaxValueLength) {
        return false;
    }
    std::lock_guard<std::mutex> guard(lock_);
    auto& app_store = store_[bundle_id];
    if (app_store.size() >= kMaxEntriesPerApp && app_store.find(key) == app_store.end()) {
        return false;
    }
    ConfigValue cv{};
    cv.type = ConfigType::String;
    cv.str_val = val;
    app_store[key] = std::move(cv);
    return true;
}

bool ConfigurationService::set_int(const std::string& bundle_id, const std::string& key, std::int64_t val) {
    if (bundle_id.empty() || key.empty() || key.size() > kMaxKeyLength) {
        return false;
    }
    std::lock_guard<std::mutex> guard(lock_);
    auto& app_store = store_[bundle_id];
    if (app_store.size() >= kMaxEntriesPerApp && app_store.find(key) == app_store.end()) {
        return false;
    }
    ConfigValue cv{};
    cv.type = ConfigType::Int64;
    cv.int_val = val;
    app_store[key] = std::move(cv);
    return true;
}

bool ConfigurationService::set_double(const std::string& bundle_id, const std::string& key, double val) {
    if (bundle_id.empty() || key.empty() || key.size() > kMaxKeyLength) {
        return false;
    }
    std::lock_guard<std::mutex> guard(lock_);
    auto& app_store = store_[bundle_id];
    if (app_store.size() >= kMaxEntriesPerApp && app_store.find(key) == app_store.end()) {
        return false;
    }
    ConfigValue cv{};
    cv.type = ConfigType::Double;
    cv.dbl_val = val;
    app_store[key] = std::move(cv);
    return true;
}

bool ConfigurationService::set_bool(const std::string& bundle_id, const std::string& key, bool val) {
    if (bundle_id.empty() || key.empty() || key.size() > kMaxKeyLength) {
        return false;
    }
    std::lock_guard<std::mutex> guard(lock_);
    auto& app_store = store_[bundle_id];
    if (app_store.size() >= kMaxEntriesPerApp && app_store.find(key) == app_store.end()) {
        return false;
    }
    ConfigValue cv{};
    cv.type = ConfigType::Bool;
    cv.bool_val = val;
    app_store[key] = std::move(cv);
    return true;
}

std::string ConfigurationService::get_string(const std::string& bundle_id, const std::string& key, const std::string& default_val) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto app_it = store_.find(bundle_id);
    if (app_it == store_.end()) return default_val;
    auto val_it = app_it->second.find(key);
    if (val_it == app_it->second.end()) return default_val;
    if (val_it->second.type != ConfigType::String) return default_val;
    return val_it->second.str_val;
}

std::int64_t ConfigurationService::get_int(const std::string& bundle_id, const std::string& key, std::int64_t default_val) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto app_it = store_.find(bundle_id);
    if (app_it == store_.end()) return default_val;
    auto val_it = app_it->second.find(key);
    if (val_it == app_it->second.end()) return default_val;
    if (val_it->second.type != ConfigType::Int64) return default_val;
    return val_it->second.int_val;
}

double ConfigurationService::get_double(const std::string& bundle_id, const std::string& key, double default_val) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto app_it = store_.find(bundle_id);
    if (app_it == store_.end()) return default_val;
    auto val_it = app_it->second.find(key);
    if (val_it == app_it->second.end()) return default_val;
    if (val_it->second.type != ConfigType::Double) return default_val;
    return val_it->second.dbl_val;
}

bool ConfigurationService::get_bool(const std::string& bundle_id, const std::string& key, bool default_val) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto app_it = store_.find(bundle_id);
    if (app_it == store_.end()) return default_val;
    auto val_it = app_it->second.find(key);
    if (val_it == app_it->second.end()) return default_val;
    if (val_it->second.type != ConfigType::Bool) return default_val;
    return val_it->second.bool_val;
}

bool ConfigurationService::has_key(const std::string& bundle_id, const std::string& key) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto app_it = store_.find(bundle_id);
    if (app_it == store_.end()) return false;
    return app_it->second.find(key) != app_it->second.end();
}

bool ConfigurationService::delete_key(const std::string& bundle_id, const std::string& key) {
    std::lock_guard<std::mutex> guard(lock_);
    auto app_it = store_.find(bundle_id);
    if (app_it == store_.end()) return false;
    return app_it->second.erase(key) > 0;
}

void ConfigurationService::clear(const std::string& bundle_id) {
    std::lock_guard<std::mutex> guard(lock_);
    store_.erase(bundle_id);
}

bool ConfigurationService::save_to_vfs(const std::string& bundle_id) {
    if (!vfs_) return false;
    std::lock_guard<std::mutex> guard(lock_);
    auto app_it = store_.find(bundle_id);
    if (app_it == store_.end()) return false;

    std::ostringstream ss;
    for (const auto& [k, v] : app_it->second) {
        ss << static_cast<int>(v.type) << ":" << k << "=";
        switch (v.type) {
        case ConfigType::String: ss << v.str_val; break;
        case ConfigType::Int64: ss << v.int_val; break;
        case ConfigType::Double: ss << v.dbl_val; break;
        case ConfigType::Bool: ss << (v.bool_val ? "1" : "0"); break;
        }
        ss << "\n";
    }

    const std::string path = "/data/" + bundle_id + "/config.kv";
    const std::string str = ss.str();
    if (!vfs_->exists(path)) {
        vfs_->create_file(path);
    }
    return vfs_->write_file(path, str.data(), str.size());
}

bool ConfigurationService::load_from_vfs(const std::string& bundle_id) {
    if (!vfs_) return false;
    const std::string path = "/data/" + bundle_id + "/config.kv";
    if (!vfs_->exists(path)) return false;

    std::vector<std::uint8_t> data;
    if (!vfs_->read_file(path, data)) return false;

    std::string text(reinterpret_cast<const char*>(data.data()), data.size());
    std::istringstream ss(text);
    std::string line;

    std::lock_guard<std::mutex> guard(lock_);
    auto& app_store = store_[bundle_id];
    app_store.clear();

    while (std::getline(ss, line)) {
        if (line.size() < 4) continue;
        const auto colon = line.find(':');
        const auto eq = line.find('=');
        if (colon == std::string::npos || eq == std::string::npos || colon > eq) continue;

        int type_int = std::stoi(line.substr(0, colon));
        std::string key = line.substr(colon + 1, eq - colon - 1);
        std::string val = line.substr(eq + 1);

        ConfigValue cv{};
        cv.type = static_cast<ConfigType>(type_int);
        if (cv.type == ConfigType::String) {
            cv.str_val = val;
        } else if (cv.type == ConfigType::Int64) {
            try { cv.int_val = std::stoll(val); } catch (...) {}
        } else if (cv.type == ConfigType::Double) {
            try { cv.dbl_val = std::stod(val); } catch (...) {}
        } else if (cv.type == ConfigType::Bool) {
            cv.bool_val = (val == "1" || val == "true");
        }
        app_store[key] = std::move(cv);
    }
    return true;
}

} // namespace winios::services

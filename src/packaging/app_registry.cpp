#include "packaging/app_registry.h"
#include "vfs/vfs.h"

#include <sstream>

namespace winios::packaging {

bool ApplicationRegistry::register_app(AppRegistration app) {
    if (app.bundle_id.empty() || app.name.empty() || app.exec_path.empty()) {
        return false;
    }
    if (app.display_name.empty()) {
        app.display_name = app.name;
    }
    std::lock_guard<std::mutex> guard(lock_);
    if (apps_.find(app.bundle_id) != apps_.end()) {
        return false; // Already registered
    }
    apps_[app.bundle_id] = std::move(app);
    return true;
}

bool ApplicationRegistry::unregister_app(const std::string& bundle_id) {
    std::lock_guard<std::mutex> guard(lock_);
    for (auto it = default_associations_.begin(); it != default_associations_.end(); ) {
        if (it->second == bundle_id) {
            it = default_associations_.erase(it);
        } else {
            ++it;
        }
    }
    return apps_.erase(bundle_id) > 0;
}

bool ApplicationRegistry::update_app(AppRegistration app) {
    if (app.bundle_id.empty() || app.name.empty() || app.exec_path.empty()) {
        return false;
    }
    if (app.display_name.empty()) {
        app.display_name = app.name;
    }
    std::lock_guard<std::mutex> guard(lock_);
    auto it = apps_.find(app.bundle_id);
    if (it == apps_.end()) {
        return false;
    }
    it->second = std::move(app);
    return true;
}

bool ApplicationRegistry::set_active(const std::string& bundle_id, bool active) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = apps_.find(bundle_id);
    if (it == apps_.end()) {
        return false;
    }
    it->second.is_active = active;
    return true;
}

bool ApplicationRegistry::set_installation_state(const std::string& bundle_id, InstallationState state) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = apps_.find(bundle_id);
    if (it == apps_.end()) {
        return false;
    }
    it->second.state = state;
    return true;
}

InstallationState ApplicationRegistry::get_installation_state(const std::string& bundle_id) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = apps_.find(bundle_id);
    if (it == apps_.end()) {
        return InstallationState::Uninstalled;
    }
    return it->second.state;
}

bool ApplicationRegistry::is_installed(const std::string& bundle_id) const {
    std::lock_guard<std::mutex> guard(lock_);
    return apps_.find(bundle_id) != apps_.end();
}

bool ApplicationRegistry::is_active(const std::string& bundle_id) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = apps_.find(bundle_id);
    if (it == apps_.end()) return false;
    return it->second.is_active;
}

std::optional<AppRegistration> ApplicationRegistry::get_app(const std::string& bundle_id) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = apps_.find(bundle_id);
    if (it != apps_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<AppRegistration> ApplicationRegistry::list_apps(bool active_only) const {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<AppRegistration> result;
    result.reserve(apps_.size());
    for (const auto& [_, app] : apps_) {
        if (!active_only || app.is_active) {
            result.push_back(app);
        }
    }
    return result;
}

std::size_t ApplicationRegistry::app_count(bool active_only) const {
    std::lock_guard<std::mutex> guard(lock_);
    if (!active_only) return apps_.size();
    std::size_t cnt = 0;
    for (const auto& [_, app] : apps_) {
        if (app.is_active) ++cnt;
    }
    return cnt;
}

bool ApplicationRegistry::verify_installed_app_integrity(const std::string& bundle_id, vfs::VirtualFileSystem& vfs) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = apps_.find(bundle_id);
    if (it == apps_.end()) {
        return false;
    }

    if (!vfs.exists(it->second.install_dir)) {
        it->second.state = InstallationState::Corrupted;
        return false;
    }

    const std::string exec_full = it->second.install_dir + "/" + it->second.exec_path;
    if (!vfs.exists(exec_full)) {
        it->second.state = InstallationState::Corrupted;
        return false;
    }

    std::vector<std::uint8_t> data;
    if (!vfs.read_file(exec_full, data) || data.empty()) {
        it->second.state = InstallationState::Corrupted;
        return false;
    }

    it->second.state = InstallationState::Installed;
    return true;
}

bool ApplicationRegistry::set_default_app_for_type(const std::string& doc_type, const std::string& bundle_id) {
    if (doc_type.empty() || bundle_id.empty()) {
        return false;
    }
    std::lock_guard<std::mutex> guard(lock_);
    auto it = apps_.find(bundle_id);
    if (it == apps_.end() || !it->second.is_active) {
        return false;
    }
    default_associations_[doc_type] = bundle_id;
    return true;
}

bool ApplicationRegistry::remove_default_app_for_type(const std::string& doc_type) {
    std::lock_guard<std::mutex> guard(lock_);
    return default_associations_.erase(doc_type) > 0;
}

std::string ApplicationRegistry::resolve_app_for_path(const std::string& file_path) const {
    if (file_path.empty()) return "";
    auto dot_pos = file_path.rfind('.');
    if (dot_pos == std::string::npos) return "";
    std::string ext = file_path.substr(dot_pos);
    return get_default_app_for_type(ext);
}

std::string ApplicationRegistry::get_default_app_for_type(const std::string& doc_type) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = default_associations_.find(doc_type);
    if (it != default_associations_.end()) {
        auto app_it = apps_.find(it->second);
        if (app_it != apps_.end() && app_it->second.is_active) {
            return it->second;
        }
    }
    // Fallback: search all apps that declare this type
    for (const auto& [bid, app] : apps_) {
        if (!app.is_active) continue;
        for (const auto& dt : app.supported_doc_types) {
            if (dt == doc_type) return bid;
        }
    }
    return "";
}

std::vector<AppRegistration> ApplicationRegistry::get_apps_for_type(const std::string& doc_type, bool active_only) const {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<AppRegistration> result;
    for (const auto& [_, app] : apps_) {
        if (active_only && !app.is_active) continue;
        for (const auto& dt : app.supported_doc_types) {
            if (dt == doc_type) {
                result.push_back(app);
                break;
            }
        }
    }
    return result;
}

void ApplicationRegistry::clear() {
    std::lock_guard<std::mutex> guard(lock_);
    apps_.clear();
    default_associations_.clear();
}

std::string ApplicationRegistry::serialize() const {
    std::lock_guard<std::mutex> guard(lock_);
    std::ostringstream ss;
    ss << "registry_count=" << apps_.size() << "\n";
    for (const auto& [bid, app] : apps_) {
        ss << "[app]\n";
        ss << "bundle_id=" << app.bundle_id << "\n";
        ss << "name=" << app.name << "\n";
        if (!app.display_name.empty()) ss << "display_name=" << app.display_name << "\n";
        ss << "version=" << app.version << "\n";
        ss << "package_version=" << app.package_version << "\n";
        ss << "min_sdk_version=" << app.min_sdk_version << "\n";
        ss << "target_abi=" << app.target_abi << "\n";
        ss << "state=" << static_cast<std::uint32_t>(app.state) << "\n";
        ss << "exec_path=" << app.exec_path << "\n";
        ss << "install_dir=" << app.install_dir << "\n";
        ss << "icon_path=" << app.icon_path << "\n";
        ss << "startup_config=" << app.startup_config << "\n";
        ss << "storage_quota_bytes=" << app.storage_quota_bytes << "\n";
        ss << "install_time_us=" << app.install_time_us << "\n";
        ss << "update_time_us=" << app.update_time_us << "\n";
        ss << "is_active=" << (app.is_active ? "1" : "0") << "\n";
        ss << "permissions=";
        for (std::size_t i = 0; i < app.permissions.size(); ++i) {
            if (i > 0) ss << ",";
            ss << app.permissions[i];
        }
        ss << "\n";
        ss << "optional_permissions=";
        for (std::size_t i = 0; i < app.optional_permissions.size(); ++i) {
            if (i > 0) ss << ",";
            ss << app.optional_permissions[i];
        }
        ss << "\n";
        ss << "supported_doc_types=";
        for (std::size_t i = 0; i < app.supported_doc_types.size(); ++i) {
            if (i > 0) ss << ",";
            ss << app.supported_doc_types[i];
        }
        ss << "\n";
    }
    for (const auto& [dtype, bid] : default_associations_) {
        ss << "[association]\n";
        ss << "doc_type=" << dtype << "\n";
        ss << "bundle_id=" << bid << "\n";
    }
    return ss.str();
}

bool ApplicationRegistry::deserialize(const std::string& data) {
    std::lock_guard<std::mutex> guard(lock_);
    apps_.clear();
    default_associations_.clear();
    std::istringstream ss(data);
    std::string line;
    AppRegistration current{};
    bool in_app = false;
    bool in_assoc = false;
    std::string assoc_type;
    std::string assoc_bid;

    auto finish_app = [&]() {
        if (in_app && !current.bundle_id.empty()) {
            if (current.display_name.empty()) current.display_name = current.name;
            apps_[current.bundle_id] = current;
        }
        current = AppRegistration{};
        in_app = false;
    };

    auto finish_assoc = [&]() {
        if (in_assoc && !assoc_type.empty() && !assoc_bid.empty()) {
            default_associations_[assoc_type] = assoc_bid;
        }
        assoc_type.clear();
        assoc_bid.clear();
        in_assoc = false;
    };

    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        if (line == "[app]") {
            finish_assoc();
            finish_app();
            in_app = true;
            continue;
        }
        if (line == "[association]") {
            finish_app();
            finish_assoc();
            in_assoc = true;
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);

        if (in_assoc) {
            if (key == "doc_type") assoc_type = val;
            else if (key == "bundle_id") assoc_bid = val;
            continue;
        }

        if (key == "bundle_id") current.bundle_id = val;
        else if (key == "name") current.name = val;
        else if (key == "display_name") current.display_name = val;
        else if (key == "version") current.version = val;
        else if (key == "package_version") current.package_version = val;
        else if (key == "min_sdk_version") {
            try { current.min_sdk_version = static_cast<std::uint32_t>(std::stoul(val)); } catch (...) {}
        }
        else if (key == "target_abi") current.target_abi = val;
        else if (key == "state") {
            try { current.state = static_cast<InstallationState>(std::stoul(val)); } catch (...) {}
        }
        else if (key == "exec_path") current.exec_path = val;
        else if (key == "install_dir") current.install_dir = val;
        else if (key == "icon_path") current.icon_path = val;
        else if (key == "startup_config") current.startup_config = val;
        else if (key == "storage_quota_bytes") {
            try { current.storage_quota_bytes = std::stoull(val); } catch (...) {}
        }
        else if (key == "install_time_us") {
            try { current.install_time_us = std::stoull(val); } catch (...) {}
        }
        else if (key == "update_time_us") {
            try { current.update_time_us = std::stoull(val); } catch (...) {}
        }
        else if (key == "is_active") current.is_active = (val == "1");
        else if (key == "permissions") {
            std::istringstream pss(val);
            std::string p;
            while (std::getline(pss, p, ',')) {
                if (!p.empty()) current.permissions.push_back(p);
            }
        }
        else if (key == "optional_permissions") {
            std::istringstream pss(val);
            std::string p;
            while (std::getline(pss, p, ',')) {
                if (!p.empty()) current.optional_permissions.push_back(p);
            }
        }
        else if (key == "supported_doc_types") {
            std::istringstream pss(val);
            std::string p;
            while (std::getline(pss, p, ',')) {
                if (!p.empty()) current.supported_doc_types.push_back(p);
            }
        }
    }
    finish_app();
    finish_assoc();
    return true;
}

} // namespace winios::packaging

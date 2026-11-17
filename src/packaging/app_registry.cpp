#include "packaging/app_registry.h"

#include <sstream>

namespace winios::packaging {

bool ApplicationRegistry::register_app(AppRegistration app) {
    if (app.bundle_id.empty() || app.exec_path.empty()) return false;
    std::lock_guard<std::mutex> guard(lock_);
    apps_[app.bundle_id] = std::move(app);
    return true;
}

bool ApplicationRegistry::unregister_app(const std::string& bundle_id) {
    std::lock_guard<std::mutex> guard(lock_);
    return apps_.erase(bundle_id) > 0;
}

bool ApplicationRegistry::is_installed(const std::string& bundle_id) const {
    std::lock_guard<std::mutex> guard(lock_);
    return apps_.find(bundle_id) != apps_.end();
}

std::optional<AppRegistration> ApplicationRegistry::get_app(const std::string& bundle_id) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = apps_.find(bundle_id);
    if (it != apps_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<AppRegistration> ApplicationRegistry::list_apps() const {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<AppRegistration> res;
    res.reserve(apps_.size());
    for (const auto& [id, reg] : apps_) {
        res.push_back(reg);
    }
    return res;
}

std::size_t ApplicationRegistry::app_count() const {
    std::lock_guard<std::mutex> guard(lock_);
    return apps_.size();
}

void ApplicationRegistry::clear() {
    std::lock_guard<std::mutex> guard(lock_);
    apps_.clear();
}

std::string ApplicationRegistry::serialize() const {
    std::lock_guard<std::mutex> guard(lock_);
    std::ostringstream ss;
    for (const auto& [id, app] : apps_) {
        ss << "[APP]\n";
        ss << "bundle_id=" << app.bundle_id << "\n";
        ss << "name=" << app.name << "\n";
        ss << "version=" << app.version << "\n";
        ss << "exec_path=" << app.exec_path << "\n";
        ss << "install_dir=" << app.install_dir << "\n";
        ss << "install_time_us=" << app.install_time_us << "\n";
        ss << "permissions=";
        for (std::size_t i = 0; i < app.permissions.size(); ++i) {
            if (i > 0) ss << ",";
            ss << app.permissions[i];
        }
        ss << "\n";
    }
    return ss.str();
}

bool ApplicationRegistry::deserialize(const std::string& data) {
    std::lock_guard<std::mutex> guard(lock_);
    apps_.clear();

    std::istringstream ss(data);
    std::string line;
    AppRegistration current{};
    bool in_app = false;

    auto finish_app = [&]() {
        if (in_app && !current.bundle_id.empty()) {
            apps_[current.bundle_id] = current;
        }
        current = AppRegistration{};
        in_app = false;
    };

    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        if (line == "[APP]") {
            finish_app();
            in_app = true;
            continue;
        }
        if (!in_app) continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);

        if (key == "bundle_id") current.bundle_id = val;
        else if (key == "name") current.name = val;
        else if (key == "version") current.version = val;
        else if (key == "exec_path") current.exec_path = val;
        else if (key == "install_dir") current.install_dir = val;
        else if (key == "install_time_us") current.install_time_us = std::stoull(val);
        else if (key == "permissions") {
            std::istringstream pss(val);
            std::string p;
            while (std::getline(pss, p, ',')) {
                if (!p.empty()) current.permissions.push_back(p);
            }
        }
    }
    finish_app();
    return true;
}

} // namespace winios::packaging

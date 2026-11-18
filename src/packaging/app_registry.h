#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace winios::packaging {

struct AppRegistration {
    std::string bundle_id;
    std::string name;
    std::string version;
    std::string exec_path; // Relative to /Applications/<bundle_id>.app/
    std::string install_dir; // Full VFS path
    std::string icon_path;
    std::string startup_config;
    std::uint64_t storage_quota_bytes{10 * 1024 * 1024};
    std::vector<std::string> permissions;
    std::vector<std::string> optional_permissions;
    std::uint64_t install_time_us{0};
    std::uint64_t update_time_us{0};
    bool is_active{true};
};

class ApplicationRegistry {
public:
    ApplicationRegistry() = default;
    ~ApplicationRegistry() = default;

    ApplicationRegistry(const ApplicationRegistry&) = delete;
    ApplicationRegistry& operator=(const ApplicationRegistry&) = delete;

    bool register_app(AppRegistration app);
    bool unregister_app(const std::string& bundle_id);
    bool update_app(AppRegistration app);
    bool set_active(const std::string& bundle_id, bool active);

    [[nodiscard]] bool is_installed(const std::string& bundle_id) const;
    [[nodiscard]] bool is_active(const std::string& bundle_id) const;
    [[nodiscard]] std::optional<AppRegistration> get_app(const std::string& bundle_id) const;
    [[nodiscard]] std::vector<AppRegistration> list_apps(bool active_only = false) const;
    [[nodiscard]] std::size_t app_count(bool active_only = false) const;

    void clear();

    // Persistence
    [[nodiscard]] std::string serialize() const;
    bool deserialize(const std::string& data);

private:
    mutable std::mutex                       lock_;
    std::map<std::string, AppRegistration>   apps_;
};

} // namespace winios::packaging

#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace winios::vfs {
class VirtualFileSystem;
}

namespace winios::packaging {

enum class InstallationState : std::uint32_t {
    Installed = 0,
    Upgrading = 1,
    Corrupted = 2,
    Uninstalled = 3
};

inline const char* installation_state_to_string(InstallationState state) noexcept {
    switch (state) {
    case InstallationState::Installed: return "Installed";
    case InstallationState::Upgrading: return "Upgrading";
    case InstallationState::Corrupted: return "Corrupted";
    case InstallationState::Uninstalled: return "Uninstalled";
    default: return "Unknown";
    }
}

struct AppRegistration {
    std::string bundle_id;
    std::string name;
    std::string display_name;
    std::string version;
    std::string package_version{"3.0"};
    std::uint32_t min_sdk_version{0x00020300};
    std::string target_abi{"2.4"};
    std::string exec_path; // Relative to /Applications/<bundle_id>.app/
    std::string install_dir; // Full VFS path
    std::string icon_path;
    std::string startup_config;
    std::uint64_t storage_quota_bytes{10 * 1024 * 1024};
    std::vector<std::string> permissions;
    std::vector<std::string> optional_permissions;
    std::vector<std::string> supported_doc_types; // e.g. "text/plain", ".txt", ".md"
    std::uint64_t install_time_us{0};
    std::uint64_t update_time_us{0};
    bool is_active{true};
    InstallationState state{InstallationState::Installed};
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
    bool set_installation_state(const std::string& bundle_id, InstallationState state);
    [[nodiscard]] InstallationState get_installation_state(const std::string& bundle_id) const;

    [[nodiscard]] bool is_installed(const std::string& bundle_id) const;
    [[nodiscard]] bool is_active(const std::string& bundle_id) const;
    [[nodiscard]] std::optional<AppRegistration> get_app(const std::string& bundle_id) const;
    [[nodiscard]] std::vector<AppRegistration> list_apps(bool active_only = false) const;
    [[nodiscard]] std::size_t app_count(bool active_only = false) const;

    // Integrity check against VFS
    bool verify_installed_app_integrity(const std::string& bundle_id, vfs::VirtualFileSystem& vfs);

    // Document type associations
    bool set_default_app_for_type(const std::string& doc_type, const std::string& bundle_id);
    bool remove_default_app_for_type(const std::string& doc_type);
    [[nodiscard]] std::string get_default_app_for_type(const std::string& doc_type) const;
    [[nodiscard]] std::string resolve_app_for_path(const std::string& file_path) const;
    [[nodiscard]] std::vector<AppRegistration> get_apps_for_type(const std::string& doc_type, bool active_only = true) const;

    void clear();

    // Persistence
    [[nodiscard]] std::string serialize() const;
    bool deserialize(const std::string& data);

private:
    mutable std::mutex                       lock_;
    std::map<std::string, AppRegistration>   apps_;
    std::map<std::string, std::string>       default_associations_;
};

} // namespace winios::packaging

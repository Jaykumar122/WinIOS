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
    std::vector<std::string> permissions;
    std::uint64_t install_time_us{0};
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

    [[nodiscard]] bool is_installed(const std::string& bundle_id) const;
    [[nodiscard]] std::optional<AppRegistration> get_app(const std::string& bundle_id) const;
    [[nodiscard]] std::vector<AppRegistration> list_apps() const;
    [[nodiscard]] std::size_t app_count() const;

    void clear();

    // Persistence
    [[nodiscard]] std::string serialize() const;
    bool deserialize(const std::string& data);

private:
    mutable std::mutex                       lock_;
    std::map<std::string, AppRegistration>   apps_;
};

} // namespace winios::packaging

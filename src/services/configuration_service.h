#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <memory>

#include "vfs/vfs.h"

namespace winios::services {

enum class ConfigType : std::uint8_t {
    String = 0,
    Int64  = 1,
    Double = 2,
    Bool   = 3
};

struct ConfigValue {
    ConfigType  type{ConfigType::String};
    std::string str_val;
    std::int64_t int_val{0};
    double      dbl_val{0.0};
    bool        bool_val{false};
};

class ConfigurationService {
public:
    static constexpr std::size_t kMaxKeyLength = 64;
    static constexpr std::size_t kMaxValueLength = 1024;
    static constexpr std::size_t kMaxEntriesPerApp = 128;

    explicit ConfigurationService(std::shared_ptr<vfs::VirtualFileSystem> vfs = nullptr);
    ~ConfigurationService() = default;

    // Typed Setters
    bool set_string(const std::string& bundle_id, const std::string& key, const std::string& val);
    bool set_int(const std::string& bundle_id, const std::string& key, std::int64_t val);
    bool set_double(const std::string& bundle_id, const std::string& key, double val);
    bool set_bool(const std::string& bundle_id, const std::string& key, bool val);

    // Typed Getters
    [[nodiscard]] std::string  get_string(const std::string& bundle_id, const std::string& key, const std::string& default_val = "") const;
    [[nodiscard]] std::int64_t get_int(const std::string& bundle_id, const std::string& key, std::int64_t default_val = 0) const;
    [[nodiscard]] double       get_double(const std::string& bundle_id, const std::string& key, double default_val = 0.0) const;
    [[nodiscard]] bool         get_bool(const std::string& bundle_id, const std::string& key, bool default_val = false) const;

    [[nodiscard]] bool has_key(const std::string& bundle_id, const std::string& key) const;
    bool delete_key(const std::string& bundle_id, const std::string& key);
    void clear(const std::string& bundle_id);

    // Persistence to/from VFS
    bool save_to_vfs(const std::string& bundle_id);
    bool load_from_vfs(const std::string& bundle_id);

private:
    std::shared_ptr<vfs::VirtualFileSystem> vfs_;
    mutable std::mutex                      lock_;
    std::unordered_map<std::string, std::unordered_map<std::string, ConfigValue>> store_;
};

} // namespace winios::services

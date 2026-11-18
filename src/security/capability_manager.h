#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace winios::security {

enum class Capability : std::uint32_t {
    None              = 0,
    DisplayWindow     = 1 << 0,
    StoragePrivate    = 1 << 1,
    StorageShared     = 1 << 2,
    ClipboardRead     = 1 << 3,
    ClipboardWrite    = 1 << 4,
    ProcessSpawn      = 1 << 5,
    FilesystemAccess  = 1 << 6,
    IpcAccess         = 1 << 7,
    TimerAccess       = 1 << 8,
    LoggingAccess     = 1 << 9,
    ResourceAccess    = 1 << 10,
    ConfigAccess      = 1 << 11,
    DiagnosticsAccess = 1 << 12,
    MetadataAccess    = 1 << 13,
    DeviceEnumerate   = 1 << 14,
    DeviceInput       = 1 << 15,
    AudioOutput       = 1 << 16,
    AudioInput        = 1 << 17,
    SensorAccess      = 1 << 18,
    NetworkAccess     = 1 << 19,
    NotificationPost  = 1 << 20,
    DocumentRead      = 1 << 21,
    DocumentWrite     = 1 << 22,
    DocumentCreate    = 1 << 23,
    DocumentDelete    = 1 << 24,
    DocumentShare     = 1 << 25,
    DocumentEnumerate = 1 << 26
};

[[nodiscard]] constexpr std::uint32_t operator|(Capability a, Capability b) noexcept {
    return static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b);
}

[[nodiscard]] constexpr std::uint32_t operator|(std::uint32_t a, Capability b) noexcept {
    return a | static_cast<std::uint32_t>(b);
}

struct AppIdentity {
    std::uint32_t pid{0};
    std::string   bundle_id;
    std::uint32_t uid{501};
    std::uint64_t security_token{0};
};

struct SecurityAuditStats {
    std::uint64_t total_checks{0};
    std::uint64_t total_grants{0};
    std::uint64_t total_denials{0};
};

class CapabilityManager {
public:
    CapabilityManager() = default;
    ~CapabilityManager() = default;

    CapabilityManager(const CapabilityManager&) = delete;
    CapabilityManager& operator=(const CapabilityManager&) = delete;

    // Identity binding
    void bind_identity(std::uint32_t pid, const std::string& bundle_id, std::uint32_t uid = 501);
    void unbind_identity(std::uint32_t pid);
    [[nodiscard]] std::optional<AppIdentity> get_identity(std::uint32_t pid) const;
    [[nodiscard]] std::string get_bundle_id(std::uint32_t pid) const;

    // Capabilities
    void grant(std::uint32_t pid, Capability cap);
    void grant_mask(std::uint32_t pid, std::uint32_t mask);
    void grant_named(std::uint32_t pid, const std::vector<std::string>& perm_names);
    void revoke(std::uint32_t pid, Capability cap);
    void revoke_all(std::uint32_t pid);

    [[nodiscard]] bool check(std::uint32_t pid, Capability cap) const noexcept;
    [[nodiscard]] bool check_storage_access(std::uint32_t pid, const std::string& target_bundle_id) const noexcept;
    [[nodiscard]] std::uint32_t get_mask(std::uint32_t pid) const noexcept;

    // Audit diagnostics
    [[nodiscard]] SecurityAuditStats get_audit_stats() const noexcept;
    void reset_audit_stats() noexcept;

    // Convert permission name string to Capability
    static Capability parse_capability_name(const std::string& name) noexcept;

private:
    mutable std::mutex lock_;
    std::map<std::uint32_t, std::uint32_t> process_caps_;
    std::map<std::uint32_t, AppIdentity>   identities_;
    mutable SecurityAuditStats             audit_stats_;
    std::uint64_t                          next_token_{1000};
};

} // namespace winios::security

#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace winios::security {

enum class Capability : std::uint32_t {
    None           = 0,
    DisplayWindow  = 1 << 0,
    StoragePrivate = 1 << 1,
    StorageShared  = 1 << 2,
    ClipboardRead  = 1 << 3,
    ClipboardWrite = 1 << 4,
    ProcessSpawn   = 1 << 5
};

[[nodiscard]] constexpr std::uint32_t operator|(Capability a, Capability b) noexcept {
    return static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b);
}

[[nodiscard]] constexpr std::uint32_t operator|(std::uint32_t a, Capability b) noexcept {
    return a | static_cast<std::uint32_t>(b);
}

class CapabilityManager {
public:
    CapabilityManager() = default;
    ~CapabilityManager() = default;

    CapabilityManager(const CapabilityManager&) = delete;
    CapabilityManager& operator=(const CapabilityManager&) = delete;

    void grant(std::uint32_t pid, Capability cap);
    void grant_mask(std::uint32_t pid, std::uint32_t mask);
    void grant_named(std::uint32_t pid, const std::vector<std::string>& perm_names);
    void revoke_all(std::uint32_t pid);

    [[nodiscard]] bool check(std::uint32_t pid, Capability cap) const noexcept;
    [[nodiscard]] std::uint32_t get_mask(std::uint32_t pid) const noexcept;

    // Convert permission name string to Capability
    static Capability parse_capability_name(const std::string& name) noexcept;

private:
    mutable std::mutex lock_;
    std::map<std::uint32_t, std::uint32_t> process_caps_;
};

} // namespace winios::security

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "kernel/bsd_process.h"
#include "kernel/ipc.h"

namespace winios::kernel {

// Standard Darwin bootstrap return codes (mach/bootstrap.h)
inline constexpr int kBootstrapSuccess            = 0;
inline constexpr int kBootstrapNotPrivileged      = 1100;
inline constexpr int kBootstrapNameInUse          = 1101;
inline constexpr int kBootstrapUnknownService     = 1102;
inline constexpr int kBootstrapServiceActive      = 1103;
inline constexpr int kBootstrapBadPrivilege       = 1104;

enum class ServiceState : std::uint8_t {
    Registered = 0,
    Active     = 1,
    Dead       = 2
};

struct ServiceRecord {
    std::string   name;
    std::uint32_t port_id{0};
    pid_t         owner_pid{0};
    ServiceState  state{ServiceState::Registered};
    bool          system_privileged{false};
    std::uint64_t lookup_count{0};
};

class BootstrapServiceRegistry {
public:
    static constexpr std::size_t kMaxServices = 256;

    BootstrapServiceRegistry() = default;
    ~BootstrapServiceRegistry() = default;

    // Check in / register a named service port
    int check_in(
        const std::string& name,
        std::uint32_t port_id,
        pid_t caller_pid,
        bool require_privilege = false);

    // Look up a named service port
    int look_up(
        const std::string& name,
        std::uint32_t& out_port_id,
        pid_t caller_pid);

    // Unregister a service by its owner
    int unregister(const std::string& name, pid_t caller_pid);

    // Notify registry of a terminated process to trigger dead-name handling
    void handle_process_termination(pid_t terminated_pid);

    // Inspect service record
    bool get_service_info(const std::string& name, ServiceRecord& out_record) const;

    [[nodiscard]] std::size_t service_count() const noexcept;
    [[nodiscard]] bool is_service_registered(const std::string& name) const;
    [[nodiscard]] bool is_service_active(const std::string& name) const;

    void serialize(std::vector<std::uint8_t>& dst) const;
    bool deserialize(const std::uint8_t*& src, std::size_t& remaining);

private:
    mutable std::mutex                              mutex_;
    std::unordered_map<std::string, ServiceRecord> services_;
    std::vector<std::string>                        dead_names_;
};

} // namespace winios::kernel

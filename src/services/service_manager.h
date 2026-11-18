#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <cstdint>

#include "kernel/bsd_process.h"

namespace winios::services {

enum class ServiceState : std::uint32_t {
    Stopped = 0,
    Starting = 1,
    Running = 2,
    Crashed = 3,
    Terminated = 4
};

struct ServiceDefinition {
    std::string name;
    std::string executable_path;
    std::string ipc_port_name;
    bool auto_restart{false};
    std::uint32_t max_restarts{3};
};

struct ServiceInstance {
    ServiceDefinition def;
    ServiceState state{ServiceState::Stopped};
    kernel::pid_t pid{0};
    std::uint32_t ipc_port{0};
    std::uint32_t restart_count{0};
    int last_exit_code{0};
};

class ServiceManager {
public:
    explicit ServiceManager(kernel::BsdProcessManager& proc_mgr);
    ~ServiceManager() = default;

    bool register_service(const ServiceDefinition& def);
    bool unregister_service(const std::string& name);

    bool start_service(const std::string& name, std::uint32_t assigned_port = 0);
    bool stop_service(const std::string& name);
    bool restart_service(const std::string& name);

    [[nodiscard]] const ServiceInstance* get_service(const std::string& name) const;
    [[nodiscard]] std::uint32_t resolve_service_port(const std::string& name) const;
    [[nodiscard]] std::size_t active_service_count() const;
    [[nodiscard]] std::vector<std::string> list_services() const;

    // Called on child process termination to handle auto-restart
    void on_process_terminated(kernel::pid_t pid, int exit_code);

private:
    kernel::BsdProcessManager& proc_mgr_;
    std::unordered_map<std::string, ServiceInstance> services_;
};

} // namespace winios::services

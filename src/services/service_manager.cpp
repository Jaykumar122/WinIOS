#include "services/service_manager.h"
#include <algorithm>

namespace winios::services {

ServiceManager::ServiceManager(kernel::BsdProcessManager& proc_mgr)
    : proc_mgr_(proc_mgr) {}

bool ServiceManager::register_service(const ServiceDefinition& def) {
    if (def.name.empty() || services_.find(def.name) != services_.end()) {
        return false;
    }
    ServiceInstance inst{};
    inst.def = def;
    inst.state = ServiceState::Stopped;
    services_[def.name] = std::move(inst);
    return true;
}

bool ServiceManager::unregister_service(const std::string& name) {
    auto it = services_.find(name);
    if (it == services_.end()) {
        return false;
    }
    if (it->second.state == ServiceState::Running) {
        stop_service(name);
    }
    services_.erase(it);
    return true;
}

bool ServiceManager::start_service(const std::string& name, std::uint32_t assigned_port) {
    auto it = services_.find(name);
    if (it == services_.end()) {
        return false;
    }
    auto& inst = it->second;
    if (inst.state == ServiceState::Running) {
        return true; // Already running
    }

    inst.state = ServiceState::Starting;

    // Spawn process under PID 1 (launchd supervisor)
    kernel::BsdProcess* proc = proc_mgr_.create_process(inst.def.name, 1);
    if (!proc) {
        inst.state = ServiceState::Crashed;
        return false;
    }

    inst.pid = proc->pid();
    inst.ipc_port = assigned_port;
    inst.state = ServiceState::Running;
    return true;
}

bool ServiceManager::stop_service(const std::string& name) {
    auto it = services_.find(name);
    if (it == services_.end()) {
        return false;
    }
    auto& inst = it->second;
    if (inst.state != ServiceState::Running) {
        return false;
    }

    if (inst.pid != 0) {
        proc_mgr_.terminate_process(inst.pid, 0);
        proc_mgr_.reap_process(inst.pid);
        inst.pid = 0;
    }
    inst.state = ServiceState::Stopped;
    return true;
}

bool ServiceManager::restart_service(const std::string& name) {
    auto it = services_.find(name);
    if (it == services_.end()) {
        return false;
    }
    const std::uint32_t port = it->second.ipc_port;
    stop_service(name);
    return start_service(name, port);
}

const ServiceInstance* ServiceManager::get_service(const std::string& name) const {
    auto it = services_.find(name);
    return it != services_.end() ? &it->second : nullptr;
}

std::uint32_t ServiceManager::resolve_service_port(const std::string& name) const {
    auto it = services_.find(name);
    if (it != services_.end() && it->second.state == ServiceState::Running) {
        return it->second.ipc_port;
    }
    return 0;
}

std::size_t ServiceManager::active_service_count() const {
    std::size_t count = 0;
    for (const auto& kv : services_) {
        if (kv.second.state == ServiceState::Running) {
            ++count;
        }
    }
    return count;
}

std::vector<std::string> ServiceManager::list_services() const {
    std::vector<std::string> names;
    names.reserve(services_.size());
    for (const auto& kv : services_) {
        names.push_back(kv.first);
    }
    std::sort(names.begin(), names.end());
    return names;
}

void ServiceManager::on_process_terminated(kernel::pid_t pid, int exit_code) {
    for (auto& kv : services_) {
        auto& inst = kv.second;
        if (inst.pid == pid) {
            inst.last_exit_code = exit_code;
            inst.pid = 0;

            if (exit_code != 0) {
                inst.state = ServiceState::Crashed;
                if (inst.def.auto_restart && inst.restart_count < inst.def.max_restarts) {
                    ++inst.restart_count;
                    start_service(inst.def.name, inst.ipc_port);
                }
            } else {
                inst.state = ServiceState::Terminated;
            }
            break;
        }
    }
}

} // namespace winios::services

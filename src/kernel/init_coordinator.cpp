#include "kernel/init_coordinator.h"

namespace winios::kernel {

InitCoordinator::InitCoordinator(
    BsdProcessManager& proc_mgr,
    BootstrapServiceRegistry& bootstrap_registry,
    KernelThreadManager& thread_mgr,
    memory::PhysicalPageManager& ppm,
    Memory& mem)
    : proc_mgr_(proc_mgr),
      bootstrap_registry_(bootstrap_registry),
      thread_mgr_(thread_mgr),
      ppm_(ppm),
      mem_(mem) {}

pid_t InitCoordinator::spawn_service(
    const std::string& name,
    const std::vector<std::uint8_t>& macho_binary,
    const std::vector<std::string>& argv,
    const std::vector<std::string>& envp,
    bool restart_on_crash) {
    SpawnConfig cfg;
    cfg.process_name = name;
    cfg.parent_pid = 1;
    cfg.argv = argv.empty() ? std::vector<std::string>{name} : argv;
    cfg.envp = envp;

    auto spawn_res = ProcessSpawner::spawn(macho_binary, cfg, proc_mgr_, thread_mgr_, ppm_, mem_);
    if (!spawn_res.success) {
        return -1;
    }

    SupervisedService svc{};
    svc.name = name;
    svc.pid = spawn_res.pid;
    svc.status = SupervisedServiceStatus::Running;
    svc.restart_on_crash = restart_on_crash;
    svc.restart_count = 0;
    svc.spawn_res = std::move(spawn_res);

    services_[name] = std::move(svc);
    return spawn_res.pid;
}

std::size_t InitCoordinator::supervise_step() {
    std::size_t events_handled = 0;

    // 1. Check status of all direct supervised child services using WNOHANG
    for (auto& [name, svc] : services_) {
        if (svc.status == SupervisedServiceStatus::Running) {
            int status = 0;
            const int res = proc_mgr_.wait_process(1 /* init pid */, svc.pid, &status, 1 /* WNOHANG */);
            if (res > 0) {
                // Process terminated and was reaped
                svc.exit_code = (status >> 8) & 0xFF;
                if (svc.exit_code == 0) {
                    svc.status = SupervisedServiceStatus::Exited;
                } else {
                    svc.status = SupervisedServiceStatus::Crashed;
                }
                bootstrap_registry_.handle_process_termination(svc.pid);
                ++events_handled;
            }
        }
    }

    // 2. Reap any reparented orphan zombies (waitpid(-1, &status, WNOHANG))
    int orphan_status = 0;
    while (true) {
        const int reaped_pid = proc_mgr_.wait_process(1, -1, &orphan_status, 1 /* WNOHANG */);
        if (reaped_pid <= 0) {
            break;
        }
        bootstrap_registry_.handle_process_termination(static_cast<pid_t>(reaped_pid));
        ++reaped_orphans_;
        ++events_handled;
    }

    return events_handled;
}

void InitCoordinator::shutdown_all() {
    for (auto& [name, svc] : services_) {
        if (svc.status == SupervisedServiceStatus::Running) {
            auto* proc = proc_mgr_.get_process(svc.pid);
            if (proc && !proc->is_terminated()) {
                proc->terminate(0);
            }
            svc.status = SupervisedServiceStatus::Terminated;
            bootstrap_registry_.handle_process_termination(svc.pid);
        }
    }
    supervise_step();
}

const SupervisedService* InitCoordinator::get_service(const std::string& name) const {
    auto it = services_.find(name);
    return (it != services_.end()) ? &it->second : nullptr;
}

std::size_t InitCoordinator::active_service_count() const noexcept {
    std::size_t count = 0;
    for (const auto& [name, svc] : services_) {
        if (svc.status == SupervisedServiceStatus::Running) {
            ++count;
        }
    }
    return count;
}

} // namespace winios::kernel

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "kernel/bootstrap_registry.h"
#include "kernel/bsd_process.h"
#include "kernel/process_spawner.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"

namespace winios::kernel {

enum class SupervisedServiceStatus : std::uint8_t {
    Inactive   = 0,
    Running    = 1,
    Exited     = 2,
    Crashed    = 3,
    Terminated = 4
};

struct SupervisedService {
    std::string             name;
    pid_t                   pid{0};
    SupervisedServiceStatus status{SupervisedServiceStatus::Inactive};
    int                     exit_code{0};
    bool                    restart_on_crash{false};
    std::size_t             restart_count{0};
    SpawnResult             spawn_res{};
};

class InitCoordinator {
public:
    InitCoordinator(
        BsdProcessManager& proc_mgr,
        BootstrapServiceRegistry& bootstrap_registry,
        KernelThreadManager& thread_mgr,
        memory::PhysicalPageManager& ppm,
        Memory& mem);
    ~InitCoordinator() = default;

    // Launch a supervised child process from Mach-O binary
    pid_t spawn_service(
        const std::string& name,
        const std::vector<std::uint8_t>& macho_binary,
        const std::vector<std::string>& argv = {},
        const std::vector<std::string>& envp = {},
        bool restart_on_crash = false);

    // Supervise loop step: polls children with WNOHANG, reaps zombies, reparents orphans
    std::size_t supervise_step();

    // Gracefully shut down all supervised services
    void shutdown_all();

    // Check service status
    [[nodiscard]] const SupervisedService* get_service(const std::string& name) const;
    [[nodiscard]] std::size_t active_service_count() const noexcept;
    [[nodiscard]] std::size_t reaped_orphan_count() const noexcept { return reaped_orphans_; }

private:
    BsdProcessManager&           proc_mgr_;
    BootstrapServiceRegistry&    bootstrap_registry_;
    KernelThreadManager&         thread_mgr_;
    memory::PhysicalPageManager& ppm_;
    Memory&                      mem_;
    std::unordered_map<std::string, SupervisedService> services_;
    std::size_t                  reaped_orphans_{0};
};

} // namespace winios::kernel

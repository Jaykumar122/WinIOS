#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"

namespace winios::runtime {

// -----------------------------------------------------------------------------
// launchd Job States & Configurations
// -----------------------------------------------------------------------------
enum class LaunchdJobState {
    Idle,
    Starting,
    Running,
    Crashed,
    Restarting,
    Stopped
};

[[nodiscard]] const char* launchd_state_string(LaunchdJobState state) noexcept;

struct LaunchdJobConfig {
    std::string              label;
    std::vector<std::string> program_arguments;
    bool                     run_at_load{true};
    bool                     keep_alive{true};
    std::vector<std::string> mach_services;
    std::vector<std::string> dependencies;
    std::string              user_name{"root"};
};

struct LaunchdJobInstance {
    LaunchdJobConfig config;
    LaunchdJobState  state{LaunchdJobState::Idle};
    std::uint32_t    pid{0};
    std::uint32_t    crash_count{0};
    int              last_exit_code{0};
};

// -----------------------------------------------------------------------------
// LaunchdDaemonManager: iOS Daemon Supervisor & Service Orchestrator
// -----------------------------------------------------------------------------
class LaunchdDaemonManager {
public:
    LaunchdDaemonManager();
    ~LaunchdDaemonManager() = default;

    // Load and register job configurations
    bool register_job(LaunchdJobConfig config);

    // Simple Property List XML parser for launchd .plist files
    bool load_job_from_plist(std::string_view plist_xml);

    // Populate standard Apple iOS system launch daemons
    void populate_default_ios_daemons();

    // Query job instances
    [[nodiscard]] bool has_job(std::string_view label) const noexcept;
    [[nodiscard]] const LaunchdJobInstance* get_job(std::string_view label) const noexcept;
    [[nodiscard]] std::vector<std::string> get_all_labels() const;

    // Service Lifecycle Management
    bool start_job(
        std::string_view label,
        DarwinBsdSubsystem& bsd,
        MachIpcSubsystem& ipc,
        Memory& memory);

    bool stop_job(
        std::string_view label,
        DarwinBsdSubsystem& bsd);

    // Child termination handler (called on waitpid / SIGCHLD)
    void handle_process_exit(
        std::uint32_t pid,
        int exit_code,
        DarwinBsdSubsystem& bsd,
        MachIpcSubsystem& ipc,
        Memory& memory);

    // Boot orchestration: Launches services in topological dependency order
    std::size_t launch_system_boot_daemons(
        DarwinBsdSubsystem& bsd,
        MachIpcSubsystem& ipc,
        Memory& memory);

private:
    mutable std::mutex                          lock_;
    std::map<std::string, LaunchdJobInstance>   jobs_;
};

} // namespace winios::runtime

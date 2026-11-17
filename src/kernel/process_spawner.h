#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "kernel/bsd_process.h"
#include "kernel/thread.h"
#include "kernel/user_stack.h"
#include "loader/macho_user_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

namespace winios::kernel {

struct SpawnConfig {
    std::string process_name;
    pid_t parent_pid{0};
    std::vector<std::string> argv{"app"};
    std::vector<std::string> envp{"PATH=/bin"};
    std::size_t max_stack_size{0x10000}; // 64 KiB
};

struct SpawnResult {
    bool success{false};
    pid_t pid{0};
    BsdProcess* process{nullptr};
    KernelThread* main_thread{nullptr};
    std::unique_ptr<memory::UserAddressSpace> address_space{nullptr};
    std::string error_message;
};

class ProcessSpawner {
public:
    // Spawns a new independent guest process from a validated Mach-O image
    static SpawnResult spawn(
        const std::vector<std::uint8_t>& binary,
        const SpawnConfig& config,
        BsdProcessManager& proc_mgr,
        KernelThreadManager& thread_mgr,
        memory::PhysicalPageManager& ppm,
        Memory& mem);

    // Completely destroys and cleans up a spawned process and its resources
    static bool destroy_process(
        pid_t pid,
        SpawnResult& spawn_res,
        BsdProcessManager& proc_mgr,
        KernelThreadManager& thread_mgr);
};

} // namespace winios::kernel

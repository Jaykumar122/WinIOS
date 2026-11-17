#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"
#include "kernel/thread.h"
#include "kernel/user_stack.h"
#include "kernel/user_syscall_dispatcher.h"
#include "loader/macho_user_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

namespace winios::runtime {

struct UserspaceTraceStep {
    std::size_t step_index{0};
    std::uint64_t pc{0};
    unsigned current_el{0};
    std::uint32_t instruction{0};
    bool is_syscall{false};
    std::uint64_t syscall_num{0};
    std::uint64_t syscall_ret{0};
};

struct UserspaceLaunchResult {
    bool success{false};
    kernel::pid_t pid{0};
    int exit_code{0};
    std::size_t instructions_retired{0};
    std::size_t syscalls_handled{0};
    std::string console_output;
    std::vector<UserspaceTraceStep> trace{};
    std::string error_message;
};

class UserspaceCoordinator {
public:
    // Launches and executes an ARM64 Mach-O user process end-to-end
    static UserspaceLaunchResult launch_and_run(
        const std::vector<std::uint8_t>& macho_binary,
        const std::string& process_name,
        const std::vector<std::string>& argv = {"app"},
        const std::vector<std::string>& envp = {"PATH=/bin"},
        std::size_t max_steps = 10000);

    // Launches and executes a dynamically linked ARM64 Mach-O process
    static UserspaceLaunchResult launch_and_run_dynamic(
        const std::vector<std::uint8_t>& macho_binary,
        const std::unordered_map<std::string, std::vector<std::uint8_t>>& dylib_registry,
        const std::string& process_name,
        const std::vector<std::string>& argv = {"app"},
        const std::vector<std::string>& envp = {"PATH=/bin"},
        std::size_t max_steps = 10000);

};

} // namespace winios::runtime

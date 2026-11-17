#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"
#include "memory/user_address_space.h"

namespace winios::kernel {

// Pinned Darwin/XNU Syscall Numbers
inline constexpr std::uint64_t kSysExit       = 1;
inline constexpr std::uint64_t kSysFork       = 2;
inline constexpr std::uint64_t kSysRead       = 3;
inline constexpr std::uint64_t kSysWrite      = 4;
inline constexpr std::uint64_t kSysGetPid     = 20;
inline constexpr std::uint64_t kSysMachMsg    = 31;
inline constexpr std::uint64_t kSysGetPpid    = 39;
inline constexpr std::uint64_t kSysSchedYield = 329;

class VirtualConsole {
public:
    VirtualConsole() = default;
    ~VirtualConsole() = default;

    void write_bytes(const char* data, std::size_t len) {
        if (data && len > 0) {
            output_.append(data, len);
        }
    }

    [[nodiscard]] const std::string& get_output() const noexcept { return output_; }
    void clear_output() noexcept { output_.clear(); }

private:
    std::string output_;
};

struct SyscallStats {
    std::uint64_t total_syscalls{0};
    std::uint64_t total_writes{0};
    std::uint64_t total_getpids{0};
    std::uint64_t total_yields{0};
    std::uint64_t total_exits{0};
    std::uint64_t total_ipc_traps{0};
    std::uint64_t total_invalid_syscalls{0};
    std::uint64_t total_invalid_pointers{0};
};

class UserSyscallDispatcher {
public:
    explicit UserSyscallDispatcher(VirtualConsole& console) : console_(console) {}
    ~UserSyscallDispatcher() = default;

    // Dispatches a system call invoked by an EL0 thread
    bool handle_syscall(CPU& cpu,
                        BsdProcess& proc,
                        memory::UserAddressSpace& uas,
                        PriorityScheduler& sched);

    [[nodiscard]] const SyscallStats& stats() const noexcept { return stats_; }
    [[nodiscard]] VirtualConsole& console() noexcept { return console_; }

    void serialize(std::vector<std::uint8_t>& dst) const;
    bool deserialize(const std::uint8_t*& src, std::size_t& remaining);

private:
    VirtualConsole& console_;
    SyscallStats stats_{};
};

} // namespace winios::kernel

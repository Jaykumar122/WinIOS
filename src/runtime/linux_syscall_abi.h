#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "cpu/cpu.h"

namespace winios::runtime {

// -----------------------------------------------------------------------------
// Linux ARM64 System Call Numbers (Standard Linux AArch64 / asm-generic/unistd.h)
// -----------------------------------------------------------------------------
namespace LinuxSyscalls {
    constexpr std::uint64_t kOpenAt         = 56;
    constexpr std::uint64_t kClose          = 57;
    constexpr std::uint64_t kRead           = 63;
    constexpr std::uint64_t kWrite          = 64;
    constexpr std::uint64_t kFstat          = 80;
    constexpr std::uint64_t kExit           = 93;
    constexpr std::uint64_t kExitGroup      = 94;
    constexpr std::uint64_t kSetTidAddress  = 96;
    constexpr std::uint64_t kRtSigaction    = 134;
    constexpr std::uint64_t kRtSigprocmask  = 135;
    constexpr std::uint64_t kRtSigreturn    = 139;
    constexpr std::uint64_t kUname          = 160;
    constexpr std::uint64_t kGetpid         = 172;
    constexpr std::uint64_t kGettid         = 178;
    constexpr std::uint64_t kBrk            = 214;
    constexpr std::uint64_t kMunmap         = 215;
    constexpr std::uint64_t kClone          = 220;
    constexpr std::uint64_t kExecve         = 221;
    constexpr std::uint64_t kMmap           = 222;
    constexpr std::uint64_t kGetrandom      = 278;
}

// -----------------------------------------------------------------------------
// LinuxSyscallContext: Captures and injects ARM64 Linux syscall ABI state
// -----------------------------------------------------------------------------
struct LinuxSyscallContext {
    std::uint64_t syscall_nr{0};
    std::uint64_t args[6]{0, 0, 0, 0, 0, 0};
    std::uint64_t pc{0};
    std::uint64_t sp{0};
    std::uint64_t tpidr_el0{0};

    static LinuxSyscallContext capture(const CPU& cpu);
    void commit_return(CPU& cpu, std::int64_t return_val);
};

// -----------------------------------------------------------------------------
// LinuxSyscallAbi: ABI validation and conformance utilities
// -----------------------------------------------------------------------------
class LinuxSyscallAbi {
public:
    [[nodiscard]] static const char* get_syscall_name(std::uint64_t nr) noexcept;
    [[nodiscard]] static bool is_valid_arm64_syscall(std::uint64_t nr) noexcept;
    [[nodiscard]] static bool is_error_return(std::int64_t val) noexcept;
    [[nodiscard]] static std::int32_t extract_errno(std::int64_t val) noexcept;
    [[nodiscard]] static bool is_sp_aligned(std::uint64_t sp) noexcept;

    // Emulates basic essential syscalls in synthetic/test environments
    static bool handle_synthetic_syscall(CPU& cpu, Memory& mem, std::string& console_capture);
};

} // namespace winios::runtime

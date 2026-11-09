#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "cpu/cpu.h"
#include "memory/memory.h"

namespace winios::runtime {

// Standard POSIX / Linux AArch64 syscall numbers
enum class StandardSyscall : std::uint64_t {
    Read          = 63,
    Write         = 64,
    Exit          = 93,
    ExitGroup     = 94,
    ClockGetTime  = 113,
    GetPid        = 172,
    Brk           = 214
};

// -----------------------------------------------------------------------------
// GuestSyscallDispatcher: Bounded host dispatch for guest supervisor calls
// -----------------------------------------------------------------------------
class GuestSyscallDispatcher {
public:
    using SyscallHandler = std::function<std::int64_t(CPU& cpu, Memory& memory)>;

    GuestSyscallDispatcher();

    // Handler registration
    void register_handler(std::uint64_t syscall_nr, SyscallHandler handler);
    [[nodiscard]] bool has_handler(std::uint64_t syscall_nr) const noexcept;

    // Dispatch invocation: reads syscall nr from X8 (or imm16 if provided)
    std::int64_t dispatch(CPU& cpu, Memory& memory, std::uint16_t imm16 = 0);

    // Diagnostics & Output
    [[nodiscard]] const std::string& stdout_buffer() const noexcept { return stdout_buffer_; }
    void clear_stdout() noexcept { stdout_buffer_.clear(); }
    [[nodiscard]] bool has_exited() const noexcept { return has_exited_; }
    [[nodiscard]] int exit_code() const noexcept { return exit_code_; }

    // Safe memory validation helper
    [[nodiscard]] static bool is_valid_user_range(const Memory& memory, std::uint64_t addr, std::size_t size) noexcept;

private:
    void setup_default_handlers();

    std::unordered_map<std::uint64_t, SyscallHandler> handlers_;
    std::string stdout_buffer_;
    bool has_exited_{false};
    int exit_code_{0};
    std::uint64_t current_brk_{0x50000000ULL};
};

} // namespace winios::runtime

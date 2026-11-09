#include "runtime/guest_syscall_dispatcher.h"

#include <cstring>

namespace winios::runtime {

constexpr std::int64_t kErrnoEFAULT = -14;
constexpr std::int64_t kErrnoEBADF  = -9;
constexpr std::int64_t kErrnoENOSYS = -38;
constexpr std::int64_t kErrnoEINVAL = -22;

GuestSyscallDispatcher::GuestSyscallDispatcher() {
    setup_default_handlers();
}

bool GuestSyscallDispatcher::is_valid_user_range(const Memory& memory, std::uint64_t addr, std::size_t size) noexcept {
    if (size == 0) return true;
    if (addr > memory.size()) return false;
    if (size > memory.size() - addr) return false;
    return true;
}

void GuestSyscallDispatcher::register_handler(std::uint64_t syscall_nr, SyscallHandler handler) {
    handlers_[syscall_nr] = std::move(handler);
}

bool GuestSyscallDispatcher::has_handler(std::uint64_t syscall_nr) const noexcept {
    return handlers_.find(syscall_nr) != handlers_.end();
}

void GuestSyscallDispatcher::setup_default_handlers() {
    // 1. Syscall Write (64): X0 = fd, X1 = buffer, X2 = count
    register_handler(static_cast<std::uint64_t>(StandardSyscall::Write),
                     [this](CPU& cpu, Memory& memory) -> std::int64_t {
        const std::uint64_t fd = cpu.read_register(0);
        const std::uint64_t buf = cpu.read_register(1);
        const std::uint64_t count = cpu.read_register(2);

        if (fd != 1 && fd != 2) {
            return kErrnoEBADF;
        }

        if (!is_valid_user_range(memory, buf, static_cast<std::size_t>(count))) {
            return kErrnoEFAULT;
        }

        for (std::size_t i = 0; i < count; ++i) {
            const char c = static_cast<char>(memory.read_u8(buf + i));
            stdout_buffer_.push_back(c);
        }

        return static_cast<std::int64_t>(count);
    });

    // 2. Syscall Exit / ExitGroup (93, 94): X0 = exit code
    auto exit_handler = [this](CPU& cpu, Memory&) -> std::int64_t {
        has_exited_ = true;
        exit_code_ = static_cast<int>(cpu.read_register(0));
        return 0;
    };
    register_handler(static_cast<std::uint64_t>(StandardSyscall::Exit), exit_handler);
    register_handler(static_cast<std::uint64_t>(StandardSyscall::ExitGroup), exit_handler);

    // 3. Syscall GetPid (172): returns simulated pid 100
    register_handler(static_cast<std::uint64_t>(StandardSyscall::GetPid),
                     [](CPU&, Memory&) -> std::int64_t {
        return 100;
    });

    // 4. Syscall ClockGetTime (113): X0 = clock_id, X1 = timespec struct (tv_sec, tv_nsec)
    register_handler(static_cast<std::uint64_t>(StandardSyscall::ClockGetTime),
                     [](CPU& cpu, Memory& memory) -> std::int64_t {
        const std::uint64_t tp = cpu.read_register(1);
        if (!is_valid_user_range(memory, tp, 16)) {
            return kErrnoEFAULT;
        }

        // Write deterministic simulated timestamp: 1000s, 0ns
        memory.write_u64(tp, 1000ULL);
        memory.write_u64(tp + 8, 0ULL);
        return 0;
    });

    // 5. Syscall Brk (214): X0 = new brk address
    register_handler(static_cast<std::uint64_t>(StandardSyscall::Brk),
                     [this](CPU& cpu, Memory&) -> std::int64_t {
        const std::uint64_t req = cpu.read_register(0);
        if (req != 0 && req >= 0x40000000ULL) {
            current_brk_ = req;
        }
        return static_cast<std::int64_t>(current_brk_);
    });
}

std::int64_t GuestSyscallDispatcher::dispatch(CPU& cpu, Memory& memory, std::uint16_t imm16) {
    // In AArch64 standard Linux ABI, syscall number is in X8.
    // If X8 is 0 and imm16 is non-zero, allow imm16 as fallback syscall number.
    std::uint64_t nr = cpu.read_register(8);
    if (nr == 0 && imm16 != 0) {
        nr = imm16;
    }

    const auto it = handlers_.find(nr);
    std::int64_t ret = 0;
    if (it != handlers_.end()) {
        ret = it->second(cpu, memory);
    } else {
        ret = kErrnoENOSYS;
    }

    // Place return code in X0
    cpu.write_register(0, static_cast<std::uint64_t>(ret));
    return ret;
}

} // namespace winios::runtime

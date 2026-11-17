#include "kernel/user_syscall_dispatcher.h"

#include <cstring>
#include <vector>

namespace winios::kernel {

bool UserSyscallDispatcher::handle_syscall(CPU& cpu,
                                          BsdProcess& proc,
                                          memory::UserAddressSpace& uas,
                                          PriorityScheduler& sched) {
    ++stats_.total_syscalls;

    // Detect calling convention:
    // Standard Darwin XNU convention: syscall number in X16, args in X0..X5
    // WinIOS fast ABI: syscall number in X0, args in X1..X5
    std::uint64_t sys_num = cpu.read_register(16);
    std::uint64_t arg0 = cpu.read_register(0);
    std::uint64_t arg1 = cpu.read_register(1);
    std::uint64_t arg2 = cpu.read_register(2);
    // std::uint64_t arg3 = cpu.read_register(3);

    if (sys_num == 0) {
        // Fallback to WinIOS fast ABI (X0 holds syscall number)
        sys_num = arg0;
        arg0 = arg1;
        arg1 = arg2;
        arg2 = cpu.read_register(3);
    }

    switch (sys_num) {
    case kSysExit: {
        const int exit_code = static_cast<int>(arg0);
        proc.terminate(exit_code);
        ++stats_.total_exits;
        cpu.write_register(0, 0);
        return true;
    }

    case kSysWrite: {
        const int fd = static_cast<int>(arg0);
        const std::uint64_t buf_va = arg1;
        const std::size_t nbyte = static_cast<std::size_t>(arg2);

        if (!proc.fd_table().is_valid_fd(fd)) {
            cpu.write_register(0, static_cast<std::uint64_t>(-9)); // EBADF
            return false;
        }

        if (nbyte > 0) {
            // Validate user pointer before accessing memory
            if (!uas.validate_user_buffer(buf_va, nbyte, false)) {
                ++stats_.total_invalid_pointers;
                cpu.write_register(0, static_cast<std::uint64_t>(-14)); // EFAULT
                return false;
            }

            std::vector<char> buffer(nbyte);
            if (!uas.copyout(reinterpret_cast<const void*>(buf_va), buffer.data(), nbyte)) {
                ++stats_.total_invalid_pointers;
                cpu.write_register(0, static_cast<std::uint64_t>(-14)); // EFAULT
                return false;
            }

            console_.write_bytes(buffer.data(), nbyte);
        }

        ++stats_.total_writes;
        cpu.write_register(0, nbyte);
        return true;
    }

    case kSysGetPid: {
        cpu.write_register(0, static_cast<std::uint64_t>(proc.pid()));
        ++stats_.total_getpids;
        return true;
    }

    case kSysGetPpid: {
        cpu.write_register(0, static_cast<std::uint64_t>(proc.ppid()));
        return true;
    }

    case kSysSchedYield: {
        sched.yield(cpu);
        ++stats_.total_yields;
        cpu.write_register(0, 0);
        return true;
    }

    case kSysMachMsg: {
        ++stats_.total_ipc_traps;
        cpu.write_register(0, 0); // KERN_SUCCESS
        return true;
    }

    default: {
        ++stats_.total_invalid_syscalls;
        cpu.write_register(0, static_cast<std::uint64_t>(-78)); // ENOSYS
        return false;
    }
    }
}

void UserSyscallDispatcher::serialize(std::vector<std::uint8_t>& dst) const {
    const auto* p_stats = reinterpret_cast<const std::uint8_t*>(&stats_);
    dst.insert(dst.end(), p_stats, p_stats + sizeof(stats_));

    const auto& out = console_.get_output();
    const std::uint32_t out_len = static_cast<std::uint32_t>(out.size());
    const auto* p_len = reinterpret_cast<const std::uint8_t*>(&out_len);
    dst.insert(dst.end(), p_len, p_len + sizeof(out_len));
    dst.insert(dst.end(), out.begin(), out.end());
}

bool UserSyscallDispatcher::deserialize(const std::uint8_t*& src, std::size_t& remaining) {
    if (remaining < sizeof(stats_) + sizeof(std::uint32_t)) return false;

    std::memcpy(&stats_, src, sizeof(stats_));
    src += sizeof(stats_);
    remaining -= sizeof(stats_);

    std::uint32_t out_len = 0;
    std::memcpy(&out_len, src, sizeof(out_len));
    src += sizeof(out_len);
    remaining -= sizeof(out_len);

    if (remaining < out_len) return false;
    std::string out(reinterpret_cast<const char*>(src), out_len);
    src += out_len;
    remaining -= out_len;

    console_.clear_output();
    console_.write_bytes(out.data(), out.size());
    return true;
}

} // namespace winios::kernel

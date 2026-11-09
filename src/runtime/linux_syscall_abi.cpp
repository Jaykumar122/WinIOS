#include "runtime/linux_syscall_abi.h"

#include <cstring>
#include "memory/memory.h"

namespace winios::runtime {

const char* LinuxSyscallAbi::get_syscall_name(std::uint64_t nr) noexcept {
    switch (nr) {
    case LinuxSyscalls::kOpenAt:        return "openat";
    case LinuxSyscalls::kClose:         return "close";
    case LinuxSyscalls::kRead:          return "read";
    case LinuxSyscalls::kWrite:         return "write";
    case LinuxSyscalls::kFstat:         return "fstat";
    case LinuxSyscalls::kExit:          return "exit";
    case LinuxSyscalls::kExitGroup:     return "exit_group";
    case LinuxSyscalls::kSetTidAddress: return "set_tid_address";
    case LinuxSyscalls::kRtSigaction:   return "rt_sigaction";
    case LinuxSyscalls::kRtSigprocmask: return "rt_sigprocmask";
    case LinuxSyscalls::kRtSigreturn:   return "rt_sigreturn";
    case LinuxSyscalls::kUname:         return "uname";
    case LinuxSyscalls::kGetpid:        return "getpid";
    case LinuxSyscalls::kGettid:        return "gettid";
    case LinuxSyscalls::kBrk:           return "brk";
    case LinuxSyscalls::kMunmap:        return "munmap";
    case LinuxSyscalls::kClone:         return "clone";
    case LinuxSyscalls::kExecve:        return "execve";
    case LinuxSyscalls::kMmap:          return "mmap";
    case LinuxSyscalls::kGetrandom:     return "getrandom";
    default:                            return "unknown";
    }
}

bool LinuxSyscallAbi::is_valid_arm64_syscall(std::uint64_t nr) noexcept {
    switch (nr) {
    case LinuxSyscalls::kOpenAt:
    case LinuxSyscalls::kClose:
    case LinuxSyscalls::kRead:
    case LinuxSyscalls::kWrite:
    case LinuxSyscalls::kFstat:
    case LinuxSyscalls::kExit:
    case LinuxSyscalls::kExitGroup:
    case LinuxSyscalls::kSetTidAddress:
    case LinuxSyscalls::kRtSigaction:
    case LinuxSyscalls::kRtSigprocmask:
    case LinuxSyscalls::kRtSigreturn:
    case LinuxSyscalls::kUname:
    case LinuxSyscalls::kGetpid:
    case LinuxSyscalls::kGettid:
    case LinuxSyscalls::kBrk:
    case LinuxSyscalls::kMunmap:
    case LinuxSyscalls::kClone:
    case LinuxSyscalls::kExecve:
    case LinuxSyscalls::kMmap:
    case LinuxSyscalls::kGetrandom:
        return true;
    default:
        return false;
    }
}

bool LinuxSyscallAbi::is_error_return(std::int64_t val) noexcept {
    // Standard Linux ABI: return value in [-4095, -1] indicates -errno
    return (val >= -4095 && val <= -1);
}

std::int32_t LinuxSyscallAbi::extract_errno(std::int64_t val) noexcept {
    if (is_error_return(val)) {
        return static_cast<std::int32_t>(-val);
    }
    return 0;
}

bool LinuxSyscallAbi::is_sp_aligned(std::uint64_t sp) noexcept {
    // AArch64 ABI: SP must be 16-byte aligned at all public call boundaries
    return (sp % 16u) == 0;
}

LinuxSyscallContext LinuxSyscallContext::capture(const CPU& cpu) {
    LinuxSyscallContext ctx{};
    ctx.syscall_nr = cpu.read_register(8); // X8 = syscall number
    ctx.args[0]    = cpu.read_register(0); // X0 = arg0
    ctx.args[1]    = cpu.read_register(1); // X1 = arg1
    ctx.args[2]    = cpu.read_register(2); // X2 = arg2
    ctx.args[3]    = cpu.read_register(3); // X3 = arg3
    ctx.args[4]    = cpu.read_register(4); // X4 = arg4
    ctx.args[5]    = cpu.read_register(5); // X5 = arg5

    ctx.pc = cpu.get_program_counter();
    ctx.sp = cpu.get_stack_pointer();
    ctx.tpidr_el0 = cpu.get_tpidr_el0();

    return ctx;
}

void LinuxSyscallContext::commit_return(CPU& cpu, std::int64_t return_val) {
    cpu.write_register(0, static_cast<std::uint64_t>(return_val));
}

bool LinuxSyscallAbi::handle_synthetic_syscall(CPU& cpu, Memory& mem, std::string& console_capture) {
    LinuxSyscallContext ctx = LinuxSyscallContext::capture(cpu);

    switch (ctx.syscall_nr) {
    case LinuxSyscalls::kGetpid:
        ctx.commit_return(cpu, 1); // init process PID is 1
        return true;

    case LinuxSyscalls::kGettid:
        ctx.commit_return(cpu, 1); // main thread TID is 1
        return true;

    case LinuxSyscalls::kSetTidAddress:
        ctx.commit_return(cpu, 1);
        return true;

    case LinuxSyscalls::kWrite: {
        std::uint64_t fd = ctx.args[0];
        std::uint64_t buf_addr = ctx.args[1];
        std::uint64_t count = ctx.args[2];

        if (fd == 1 || fd == 2) { // stdout or stderr
            if (buf_addr + count <= mem.size()) {
                std::string s;
                s.resize(count);
                for (std::size_t i = 0; i < count; ++i) {
                    s[i] = static_cast<char>(mem.read_u8(buf_addr + i));
                }
                console_capture += s;
                ctx.commit_return(cpu, static_cast<std::int64_t>(count));
                return true;
            }
        }
        ctx.commit_return(cpu, -9); // -EBADF
        return true;
    }

    case LinuxSyscalls::kBrk: {
        std::uint64_t new_brk = ctx.args[0];
        if (new_brk == 0) {
            // Query current break
            ctx.commit_return(cpu, 0x00420000ULL);
        } else if (new_brk <= mem.size()) {
            ctx.commit_return(cpu, static_cast<std::int64_t>(new_brk));
        } else {
            ctx.commit_return(cpu, -12); // -ENOMEM
        }
        return true;
    }

    case LinuxSyscalls::kGetrandom: {
        std::uint64_t buf_addr = ctx.args[0];
        std::uint64_t buflen = ctx.args[1];
        if (buf_addr + buflen <= mem.size()) {
            for (std::size_t i = 0; i < buflen; ++i) {
                mem.write_u8(buf_addr + i, static_cast<std::uint8_t>(0xA5u ^ i));
            }
            ctx.commit_return(cpu, static_cast<std::int64_t>(buflen));
            return true;
        }
        ctx.commit_return(cpu, -14); // -EFAULT
        return true;
    }

    case LinuxSyscalls::kExitGroup:
    case LinuxSyscalls::kExit: {
        cpu.set_halted(true);
        ctx.commit_return(cpu, 0);
        return true;
    }

    default:
        // Unknown or unimplemented syscall -> return -ENOSYS (-38)
        ctx.commit_return(cpu, -38);
        return false;
    }
}

} // namespace winios::runtime

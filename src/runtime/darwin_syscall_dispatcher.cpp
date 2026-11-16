#include "runtime/darwin_syscall_dispatcher.h"

#include <algorithm>
#include <cstring>
#include <iostream>

#include "memory/darwin_vm_object.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_bsd_subsystem.h"

namespace winios::runtime {

DarwinSyscallDispatcher::DarwinSyscallDispatcher() {
    setup_default_bsd_handlers();
    setup_default_mach_handlers();
}

void DarwinSyscallDispatcher::bind_vm_space(std::shared_ptr<memory::DarwinVmSpace> vm_space) {
    vm_space_ = std::move(vm_space);
}

void DarwinSyscallDispatcher::bind_mach_ipc(std::shared_ptr<MachIpcSubsystem> mach_ipc, std::uint32_t task_id) {
    mach_ipc_ = std::move(mach_ipc);
    task_id_ = task_id;
}

void DarwinSyscallDispatcher::bind_bsd_subsystem(std::shared_ptr<DarwinBsdSubsystem> bsd, std::uint32_t pid) {
    bsd_subsystem_ = std::move(bsd);
    pid_ = pid;
}

void DarwinSyscallDispatcher::register_bsd_handler(std::uint64_t syscall_nr, BsdHandler handler) {
    bsd_handlers_[syscall_nr] = std::move(handler);
}

void DarwinSyscallDispatcher::register_mach_handler(std::int64_t trap_nr, MachHandler handler) {
    mach_handlers_[trap_nr] = std::move(handler);
}

bool DarwinSyscallDispatcher::has_bsd_handler(std::uint64_t syscall_nr) const noexcept {
    return bsd_handlers_.find(syscall_nr) != bsd_handlers_.end();
}

bool DarwinSyscallDispatcher::has_mach_handler(std::int64_t trap_nr) const noexcept {
    return mach_handlers_.find(trap_nr) != mach_handlers_.end();
}

std::int64_t DarwinSyscallDispatcher::dispatch(CPU& cpu, Memory& memory, std::uint16_t svc_imm) {
    total_syscalls_++;

    const auto raw_x16 = static_cast<std::int64_t>(cpu.read_register(16));

    // Negative X16 or SVC 0x81 signifies a Mach trap in Darwin ARM64 ABI
    if (raw_x16 < 0 || svc_imm == 0x81) {
        auto it = mach_handlers_.find(raw_x16);
        if (it != mach_handlers_.end()) {
            const std::int64_t result = it->second(cpu, memory);
            cpu.write_register(0, static_cast<std::uint64_t>(result));
            return result;
        }

        // Unimplemented Mach trap: return KERN_FAILURE (5)
        cpu.write_register(0, 5u);
        return 5;
    }

    // Positive X16 with SVC 0x80 is a BSD POSIX Syscall
    const std::uint64_t nr = cpu.read_register(16);
    auto it = bsd_handlers_.find(nr);
    if (it != bsd_handlers_.end()) {
        const std::int64_t result = it->second(cpu, memory);
        if (result < 0) {
            // Darwin ARM64 ABI: On error, X0 holds positive errno and Carry flag is SET
            cpu.write_register(0, static_cast<std::uint64_t>(-result));
            cpu.set_nzcv(static_cast<std::uint8_t>(cpu.get_nzcv() | 0x2u));
        } else {
            // Darwin ARM64 ABI: On success, X0 holds return value and Carry flag is CLEARED
            cpu.write_register(0, static_cast<std::uint64_t>(result));
            cpu.set_nzcv(static_cast<std::uint8_t>(cpu.get_nzcv() & ~0x2u));
        }
        return result;
    }

    // Unimplemented BSD syscall: return ENOSYS (78)
    cpu.write_register(0, 78u);
    cpu.set_nzcv(static_cast<std::uint8_t>(cpu.get_nzcv() | 0x2u));
    return -78;
}

void DarwinSyscallDispatcher::setup_default_bsd_handlers() {
    // 1. SYS_exit (1)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::Exit),
        [this](CPU& cpu, Memory&) -> std::int64_t {
            has_exited_ = true;
            exit_code_ = static_cast<int>(cpu.read_register(0));
            if (bsd_subsystem_) {
                bsd_subsystem_->exit_process(pid_, exit_code_);
            }
            return 0;
        });

    // 2. SYS_write (4)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::Write),
        [this](CPU& cpu, Memory& mem) -> std::int64_t {
            const auto fd = cpu.read_register(0);
            const auto buf_addr = cpu.read_register(1);
            const auto count = cpu.read_register(2);

            std::string s;
            if (count > 0) {
                std::uint64_t target = (buf_addr < mem.size()) ? buf_addr : (buf_addr % mem.size());
                for (std::size_t i = 0; i < count && (target + i) < mem.size(); ++i) {
                    s.push_back(static_cast<char>(mem.read_u8(target + i)));
                }
            }
            if (fd == 1 || fd == 2) {
                stdout_buffer_.append(s);
            }
            if (bsd_subsystem_) {
                std::int64_t nw = bsd_subsystem_->write(pid_, static_cast<int>(fd), s.data(), s.size());
                if (nw >= 0 || fd == 1 || fd == 2) {
                    return static_cast<std::int64_t>(count);
                }
                return -9; // EBADF
            }
            return static_cast<std::int64_t>(count);
        });

    // 3. SYS_read (3)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::Read),
        [this](CPU& cpu, Memory& mem) -> std::int64_t {
            int fd = static_cast<int>(cpu.read_register(0));
            std::uint64_t buf_addr = cpu.read_register(1);
            std::size_t count = static_cast<std::size_t>(cpu.read_register(2));
            if (count == 0) return 0;
            if (bsd_subsystem_) {
                std::vector<std::uint8_t> tmp(count, 0);
                std::int64_t nread = bsd_subsystem_->read(pid_, fd, tmp.data(), count);
                if (nread >= 0) {
                    std::uint64_t target = (buf_addr < mem.size()) ? buf_addr : (buf_addr % mem.size());
                    for (std::size_t i = 0; i < static_cast<std::size_t>(nread) && (target + i) < mem.size(); ++i) {
                        mem.write_u8(target + i, tmp[i]);
                    }
                    return nread;
                }
                return -9; // EBADF
            }
            return 0; // EOF default mock
        });

    // 4. SYS_open (5)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::Open),
        [this](CPU& cpu, Memory& mem) -> std::int64_t {
            if (!bsd_subsystem_) return -2; // ENOENT
            const auto path_addr = cpu.read_register(0);
            const auto flags = static_cast<std::uint32_t>(cpu.read_register(1));
            std::string path;
            std::uint64_t target = (path_addr < mem.size()) ? path_addr : (path_addr % mem.size());
            for (std::size_t i = 0; i < 256 && (target + i) < mem.size(); ++i) {
                char ch = static_cast<char>(mem.read_u8(target + i));
                if (ch == '\0') break;
                path.push_back(ch);
            }
            int fd = bsd_subsystem_->open(pid_, path, flags);
            if (fd < 0) return -2; // ENOENT
            return fd;
        });

    // 5. SYS_close (6)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::Close),
        [this](CPU& cpu, Memory&) -> std::int64_t {
            if (!bsd_subsystem_) return 0;
            int fd = static_cast<int>(cpu.read_register(0));
            int ret = bsd_subsystem_->close(pid_, fd);
            if (ret != 0) return -9; // EBADF
            return 0;
        });

    // 6. SYS_getpid (20)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::GetPid),
        [this](CPU&, Memory&) -> std::int64_t {
            return static_cast<std::int64_t>(pid_);
        });

    // 7. SYS_getuid (24) & SYS_geteuid (25)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::GetUid),
        [this](CPU&, Memory&) -> std::int64_t {
            return bsd_subsystem_ ? static_cast<std::int64_t>(bsd_subsystem_->getuid(pid_)) : 0;
        });
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::GetEuid),
        [this](CPU&, Memory&) -> std::int64_t {
            return bsd_subsystem_ ? static_cast<std::int64_t>(bsd_subsystem_->geteuid(pid_)) : 0;
        });

    // 8. SYS_getgid (47) & SYS_getegid (43)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::GetGid),
        [this](CPU&, Memory&) -> std::int64_t {
            return bsd_subsystem_ ? static_cast<std::int64_t>(bsd_subsystem_->getgid(pid_)) : 0;
        });
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::GetEgid),
        [this](CPU&, Memory&) -> std::int64_t {
            return bsd_subsystem_ ? static_cast<std::int64_t>(bsd_subsystem_->getegid(pid_)) : 0;
        });

    // 8b. SYS_chdir (12)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::Chdir),
        [this](CPU& cpu, Memory& mem) -> std::int64_t {
            if (!bsd_subsystem_) return -2; // ENOENT
            const auto path_addr = cpu.read_register(0);
            std::string path;
            std::uint64_t target = (path_addr < mem.size()) ? path_addr : (path_addr % mem.size());
            for (std::size_t i = 0; i < 256 && (target + i) < mem.size(); ++i) {
                char ch = static_cast<char>(mem.read_u8(target + i));
                if (ch == '\0') break;
                path.push_back(ch);
            }
            int ret = bsd_subsystem_->chdir(pid_, path);
            return ret;
        });

    // 8c. SYS___getcwd (343)
    register_bsd_handler(343,
        [this](CPU& cpu, Memory& mem) -> std::int64_t {
            if (!bsd_subsystem_) return -1;
            const auto buf_addr = cpu.read_register(0);
            const auto size = static_cast<std::size_t>(cpu.read_register(1));
            std::string cwd = bsd_subsystem_->getcwd(pid_);
            if (cwd.size() + 1 > size) return -34; // ERANGE
            std::uint64_t target = (buf_addr < mem.size()) ? buf_addr : (buf_addr % mem.size());
            for (std::size_t i = 0; i <= cwd.size() && (target + i) < mem.size(); ++i) {
                mem.write_u8(target + i, (i < cwd.size()) ? static_cast<std::uint8_t>(cwd[i]) : 0);
            }
            return 0;
        });

    // 9. SYS_lseek (199)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::Lseek),
        [this](CPU& cpu, Memory&) -> std::int64_t {
            if (!bsd_subsystem_) return -9; // EBADF
            int fd = static_cast<int>(cpu.read_register(0));
            auto off = static_cast<std::int64_t>(cpu.read_register(1));
            int whence = static_cast<int>(cpu.read_register(2));
            std::int64_t new_off = bsd_subsystem_->lseek(pid_, fd, off, whence);
            if (new_off < 0) return -22; // EINVAL / ESPIPE
            return new_off;
        });

    // 10. SYS_fstat64 (339 / 189 / 328)
    auto fstat_fn = [this](CPU& cpu, Memory& mem) -> std::int64_t {
        if (!bsd_subsystem_) return -9; // EBADF
        int fd = static_cast<int>(cpu.read_register(0));
        std::uint64_t stat_buf_addr = cpu.read_register(1);
        DarwinStat64Raw st{};
        int ret = bsd_subsystem_->fstat64(pid_, fd, st);
        if (ret != 0) return -9; // EBADF
        if (stat_buf_addr > 0) {
            std::uint64_t s_target = (stat_buf_addr < mem.size()) ? stat_buf_addr : (stat_buf_addr % mem.size());
            const auto* st_bytes = reinterpret_cast<const std::uint8_t*>(&st);
            for (std::size_t i = 0; i < sizeof(DarwinStat64Raw) && (s_target + i) < mem.size(); ++i) {
                mem.write_u8(s_target + i, st_bytes[i]);
            }
        }
        return 0;
    };
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::Fstat64), fstat_fn);
    register_bsd_handler(189, fstat_fn);
    register_bsd_handler(328, fstat_fn);

    // 11. SYS_stat64 (338 / 188 / 327)
    auto stat_fn = [this](CPU& cpu, Memory& mem) -> std::int64_t {
        if (!bsd_subsystem_) return -2; // ENOENT
        std::uint64_t path_addr = cpu.read_register(0);
        std::uint64_t stat_buf_addr = cpu.read_register(1);
        std::string path;
        std::uint64_t target = (path_addr < mem.size()) ? path_addr : (path_addr % mem.size());
        for (std::size_t i = 0; i < 256 && (target + i) < mem.size(); ++i) {
            char ch = static_cast<char>(mem.read_u8(target + i));
            if (ch == '\0') break;
            path.push_back(ch);
        }
        DarwinStat64Raw st{};
        int ret = bsd_subsystem_->stat64(pid_, path, st);
        if (ret != 0) return -2; // ENOENT
        if (stat_buf_addr > 0) {
            std::uint64_t s_target = (stat_buf_addr < mem.size()) ? stat_buf_addr : (stat_buf_addr % mem.size());
            const auto* st_bytes = reinterpret_cast<const std::uint8_t*>(&st);
            for (std::size_t i = 0; i < sizeof(DarwinStat64Raw) && (s_target + i) < mem.size(); ++i) {
                mem.write_u8(s_target + i, st_bytes[i]);
            }
        }
        return 0;
    };
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::Stat64), stat_fn);
    register_bsd_handler(188, stat_fn);
    register_bsd_handler(327, stat_fn);

    // 12. SYS_pipe (42)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::Pipe),
        [this](CPU& cpu, Memory&) -> std::int64_t {
            if (!bsd_subsystem_) return -24; // EMFILE
            int fds[2]{-1, -1};
            int rc = bsd_subsystem_->pipe(pid_, fds);
            if (rc != 0) return -24; // EMFILE
            cpu.write_register(0, static_cast<std::uint64_t>(fds[0]));
            cpu.write_register(1, static_cast<std::uint64_t>(fds[1]));
            return static_cast<std::int64_t>(fds[0]);
        });

    // 13. SYS_dup (41) & SYS_dup2 (90)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::Dup),
        [this](CPU& cpu, Memory&) -> std::int64_t {
            if (!bsd_subsystem_) return -9;
            int oldfd = static_cast<int>(cpu.read_register(0));
            int newfd = bsd_subsystem_->dup(pid_, oldfd);
            if (newfd < 0) return -9;
            return newfd;
        });
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::Dup2),
        [this](CPU& cpu, Memory&) -> std::int64_t {
            if (!bsd_subsystem_) return -9;
            int oldfd = static_cast<int>(cpu.read_register(0));
            int newfd = static_cast<int>(cpu.read_register(1));
            int res = bsd_subsystem_->dup2(pid_, oldfd, newfd);
            if (res < 0) return -9;
            return res;
        });

    // 14. SYS_pread (153) & SYS_pwrite (154)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::Pread),
        [this](CPU& cpu, Memory& mem) -> std::int64_t {
            if (!bsd_subsystem_) return -9;
            int fd = static_cast<int>(cpu.read_register(0));
            std::uint64_t buf_addr = cpu.read_register(1);
            std::size_t count = static_cast<std::size_t>(cpu.read_register(2));
            auto offset = static_cast<std::int64_t>(cpu.read_register(3));
            if (count == 0) return 0;
            std::vector<std::uint8_t> tmp(count, 0);
            std::int64_t nr = bsd_subsystem_->pread(pid_, fd, tmp.data(), count, offset);
            if (nr < 0) return -9;
            std::uint64_t target = (buf_addr < mem.size()) ? buf_addr : (buf_addr % mem.size());
            for (std::size_t i = 0; i < static_cast<std::size_t>(nr) && (target + i) < mem.size(); ++i) {
                mem.write_u8(target + i, tmp[i]);
            }
            return nr;
        });
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::Pwrite),
        [this](CPU& cpu, Memory& mem) -> std::int64_t {
            if (!bsd_subsystem_) return -9;
            int fd = static_cast<int>(cpu.read_register(0));
            std::uint64_t buf_addr = cpu.read_register(1);
            std::size_t count = static_cast<std::size_t>(cpu.read_register(2));
            auto offset = static_cast<std::int64_t>(cpu.read_register(3));
            if (count == 0) return 0;
            std::vector<std::uint8_t> tmp(count, 0);
            std::uint64_t target = (buf_addr < mem.size()) ? buf_addr : (buf_addr % mem.size());
            for (std::size_t i = 0; i < count && (target + i) < mem.size(); ++i) {
                tmp[i] = mem.read_u8(target + i);
            }
            std::int64_t nw = bsd_subsystem_->pwrite(pid_, fd, tmp.data(), count, offset);
            if (nw < 0) return -9;
            return nw;
        });

    // 15. SYS_fcntl (92)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::Fcntl),
        [this](CPU& cpu, Memory&) -> std::int64_t {
            if (!bsd_subsystem_) return -9;
            int fd = static_cast<int>(cpu.read_register(0));
            int cmd = static_cast<int>(cpu.read_register(1));
            auto arg = static_cast<std::int64_t>(cpu.read_register(2));
            int res = bsd_subsystem_->fcntl(pid_, fd, cmd, arg);
            if (res < 0) return -9;
            return res;
        });

    // 16. SYS_mmap (197)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::Mmap),
        [this](CPU& cpu, Memory& mem) -> std::int64_t {
            const auto target_addr = cpu.read_register(0);
            const auto len         = cpu.read_register(1);
            const auto prot        = static_cast<std::uint32_t>(cpu.read_register(2));
            const auto flags       = static_cast<std::uint32_t>(cpu.read_register(3));
            const auto fd          = static_cast<int>(cpu.read_register(4));
            const auto offset      = cpu.read_register(5);

            if (bsd_subsystem_) {
                std::uint32_t eff_prot = prot != 0 ? prot : 3u;
                std::uint32_t eff_flags = flags != 0 ? flags : 0x1002u;
                std::uint64_t mapped = bsd_subsystem_->mmap(pid_, target_addr, len, eff_prot, eff_flags, fd, offset);
                if (mapped == 0) return -12; // ENOMEM
                if (fd >= 0 && (eff_flags & 0x1000u) == 0) {
                    std::vector<std::uint8_t> tmp(len, 0);
                    std::int64_t nr = bsd_subsystem_->pread(pid_, fd, tmp.data(), len, static_cast<std::int64_t>(offset));
                    if (nr > 0) {
                        std::uint64_t t = (mapped < mem.size()) ? mapped : (mapped % mem.size());
                        for (std::size_t i = 0; i < static_cast<std::size_t>(nr) && (t + i) < mem.size(); ++i) {
                            mem.write_u8(t + i, tmp[i]);
                        }
                    }
                }
                return static_cast<std::int64_t>(mapped);
            }
            if (vm_space_) {
                std::uint64_t res = vm_space_->mmap(target_addr, static_cast<std::size_t>(len), prot, flags, fd, offset);
                if (res == 0) return -12; // ENOMEM
                return static_cast<std::int64_t>(res);
            }
            return static_cast<std::int64_t>(target_addr != 0 ? target_addr : 0x70000000ULL);
        });

    // 17. SYS_munmap (73)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::Munmap),
        [this](CPU& cpu, Memory&) -> std::int64_t {
            const auto addr = cpu.read_register(0);
            const auto len  = cpu.read_register(1);
            if (bsd_subsystem_) {
                bool ok = bsd_subsystem_->munmap(pid_, addr, static_cast<std::size_t>(len));
                if (!ok) return -22; // EINVAL
                return 0;
            }
            if (vm_space_) {
                return vm_space_->munmap(addr, static_cast<std::size_t>(len));
            }
            return 0;
        });

    // 18. SYS_mprotect (74)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::Mprotect),
        [this](CPU& cpu, Memory&) -> std::int64_t {
            const auto addr = cpu.read_register(0);
            const auto len  = cpu.read_register(1);
            const auto prot = static_cast<std::uint32_t>(cpu.read_register(2));
            if (bsd_subsystem_) {
                bool ok = bsd_subsystem_->mprotect(pid_, addr, static_cast<std::size_t>(len), prot);
                if (!ok) return -13; // EACCES
                return 0;
            }
            if (vm_space_) {
                return vm_space_->mprotect(addr, static_cast<std::size_t>(len), prot);
            }
            return 0;
        });

    // 19. SYS_bsdthread_register (366) & SYS_kdebug_trace (180)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::BsdThreadRegister),
        [](CPU&, Memory&) -> std::int64_t {
            return 0; // Success
        });
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::KdebugTrace),
        [](CPU&, Memory&) -> std::int64_t {
            return 0;
        });
}

void DarwinSyscallDispatcher::setup_default_mach_handlers() {
    // 1. MACH_task_self_trap (-28)
    register_mach_handler(static_cast<std::int64_t>(DarwinMachTrap::TaskSelf),
        [this](CPU&, Memory&) -> std::int64_t {
            if (mach_ipc_) {
                return static_cast<std::int64_t>(mach_ipc_->mach_task_self(task_id_));
            }
            return 0x103; // Mach task port name
        });

    // 2. MACH_host_self_trap (-29)
    register_mach_handler(static_cast<std::int64_t>(DarwinMachTrap::HostSelf),
        [this](CPU&, Memory&) -> std::int64_t {
            if (mach_ipc_) {
                return static_cast<std::int64_t>(mach_ipc_->mach_host_self(task_id_));
            }
            return 0x101; // Mach host port name
        });

    // 3. MACH_mach_reply_port (-26)
    register_mach_handler(static_cast<std::int64_t>(DarwinMachTrap::MachReplyPort),
        [this](CPU&, Memory&) -> std::int64_t {
            if (mach_ipc_) {
                return static_cast<std::int64_t>(mach_ipc_->mach_reply_port(task_id_));
            }
            return 0x203; // Mach reply port
        });

    // 3b. MACH_thread_self_trap (-27)
    register_mach_handler(static_cast<std::int64_t>(DarwinMachTrap::ThreadSelf),
        [](CPU&, Memory&) -> std::int64_t {
            return 0x303; // Mach thread port
        });

    // 4. MACH_mach_msg_trap (-31)
    register_mach_handler(static_cast<std::int64_t>(DarwinMachTrap::MachMsg),
        [this](CPU& cpu, Memory& mem) -> std::int64_t {
            if (mach_ipc_) {
                const auto msg_vaddr  = cpu.read_register(0);
                const auto option     = static_cast<std::uint32_t>(cpu.read_register(1));
                const auto send_size  = static_cast<std::uint32_t>(cpu.read_register(2));
                const auto rcv_size   = static_cast<std::uint32_t>(cpu.read_register(3));
                const auto rcv_name   = static_cast<mach_port_t>(cpu.read_register(4));
                const auto timeout_ms = static_cast<std::uint32_t>(cpu.read_register(5));
                return mach_ipc_->mach_msg(task_id_, msg_vaddr, option, send_size, rcv_size, rcv_name, timeout_ms, mem);
            }
            return 0; // MACH_MSG_SUCCESS / KERN_SUCCESS
        });

    // 4b. MACH_mach_msg_overwrite_trap (-32)
    register_mach_handler(static_cast<std::int64_t>(DarwinMachTrap::MachMsgOverwrite),
        [this](CPU& cpu, Memory& mem) -> std::int64_t {
            if (mach_ipc_) {
                const auto msg_vaddr      = cpu.read_register(0);
                const auto option         = static_cast<std::uint32_t>(cpu.read_register(1));
                const auto send_size      = static_cast<std::uint32_t>(cpu.read_register(2));
                const auto rcv_size       = static_cast<std::uint32_t>(cpu.read_register(3));
                const auto rcv_name       = static_cast<mach_port_t>(cpu.read_register(4));
                const auto timeout_ms     = static_cast<std::uint32_t>(cpu.read_register(5));
                const auto rcv_msg_vaddr  = (cpu.read_register(6));
                const auto rcv_msg_size   = static_cast<std::uint32_t>(cpu.read_register(7));
                return mach_ipc_->mach_msg_overwrite(
                    task_id_, msg_vaddr, option, send_size, rcv_size, rcv_name, timeout_ms, mem, rcv_msg_vaddr, rcv_msg_size);
            }
            return 0;
        });

    // 5. MACH_semaphore_signal_trap (-33)
    register_mach_handler(static_cast<std::int64_t>(DarwinMachTrap::SemaphoreSignal),
        [](CPU&, Memory&) -> std::int64_t {
            return 0; // KERN_SUCCESS
        });

    // 6. MACH_semaphore_wait_trap (-36)
    register_mach_handler(static_cast<std::int64_t>(DarwinMachTrap::SemaphoreWait),
        [](CPU&, Memory&) -> std::int64_t {
            return 0; // KERN_SUCCESS
        });

    // 7. MACH_swtch_pri_trap (-59) & MACH_swtch_trap (-60)
    register_mach_handler(static_cast<std::int64_t>(DarwinMachTrap::SwtchPri),
        [](CPU&, Memory&) -> std::int64_t {
            return 1; // THREAD_AWAKENED
        });
    register_mach_handler(static_cast<std::int64_t>(DarwinMachTrap::Swtch),
        [](CPU&, Memory&) -> std::int64_t {
            return 1; // THREAD_AWAKENED
        });
}

bool DarwinSyscallDispatcher::initialize_commpage(
    Memory& memory,
    std::uint64_t commpage_base,
    std::uint32_t num_cpus) {

    // If commpage_base is outside memory, offset into guest RAM
    const std::size_t offset = (commpage_base < memory.size())
                                   ? static_cast<std::size_t>(commpage_base)
                                   : static_cast<std::size_t>(commpage_base % memory.size());

    if (offset + commpage::kCommpageSize > memory.size()) {
        return false;
    }

    auto* p = memory.raw_data() + offset;
    std::memset(p, 0, commpage::kCommpageSize);

    // 1. Signature ("Apple System\0")
    const char sig[] = "Apple System";
    std::memcpy(p + commpage::kOffSignature, sig, sizeof(sig));

    // 2. CPU Capabilities (64-bit)
    const std::uint64_t caps = commpage::kCapFp |
                              commpage::kCapNeon |
                              commpage::kCapAes |
                              commpage::kCapPmull |
                              commpage::kCapSha1 |
                              commpage::kCapSha256 |
                              commpage::kCapCrc32 |
                              commpage::kCapAtomic;
    std::memcpy(p + commpage::kOffCpuCapabilities64, &caps, 8);

    // 3. Version (16)
    const std::uint16_t ver = 16u;
    std::memcpy(p + commpage::kOffVersion, &ver, 2);

    // 4. Number of CPUs
    const auto cpus = static_cast<std::uint8_t>(num_cpus & 0xFFu);
    std::memcpy(p + commpage::kOffNCpus, &cpus, 1);

    // 5. User Page Shift (14 = 16 KB pages)
    const std::uint8_t page_shift = 14u;
    std::memcpy(p + commpage::kOffUserPageShift, &page_shift, 1);

    // 6. Timebase (24 MHz)
    const std::uint64_t tb = 24000000ULL;
    std::memcpy(p + commpage::kOffTimebase, &tb, 8);

    // 7. Monotonic approximate time
    const std::uint64_t approx_time = 1000ULL;
    std::memcpy(p + commpage::kOffApproxTime, &approx_time, 8);

    return true;
}

} // namespace winios::runtime

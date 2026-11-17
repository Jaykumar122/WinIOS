#include "kernel/user_syscall_dispatcher.h"
#include "kernel/pipe.h"
#include "vfs/vfs.h"

#include <cstring>
#include <vector>

namespace winios::kernel {

static bool read_user_string(memory::UserAddressSpace& uas, std::uint64_t va, std::string& out_str, std::size_t max_len = 1024) {
    out_str.clear();
    for (std::size_t i = 0; i < max_len; ++i) {
        char ch = 0;
        if (!uas.copyout(reinterpret_cast<const void*>(va + i), &ch, 1)) {
            return false;
        }
        if (ch == '\0') {
            return true;
        }
        out_str.push_back(ch);
    }
    return false;
}

bool UserSyscallDispatcher::handle_syscall(CPU& cpu,
                                          BsdProcess& proc,
                                          memory::UserAddressSpace& uas,
                                          PriorityScheduler& sched) {
    ++stats_.total_syscalls;

    std::uint64_t sys_num = 0;
    std::uint64_t arg0 = 0;
    std::uint64_t arg1 = 0;
    std::uint64_t arg2 = 0;
    // std::uint64_t arg3 = 0;

    bool is_darwin_strict = false;
    bool is_fast_abi = false;

    if (abi_mode_ == SyscallAbi::DarwinStrict) {
        is_darwin_strict = true;
        last_used_abi_ = SyscallAbi::DarwinStrict;
        sys_num = cpu.read_register(16);
        arg0 = cpu.read_register(0);
        arg1 = cpu.read_register(1);
        arg2 = cpu.read_register(2);
        // arg3 = cpu.read_register(3);
    } else if (abi_mode_ == SyscallAbi::WinIOSFast) {
        is_fast_abi = true;
        last_used_abi_ = SyscallAbi::WinIOSFast;
        sys_num = cpu.read_register(0);
        arg0 = cpu.read_register(1);
        arg1 = cpu.read_register(2);
        arg2 = cpu.read_register(3);
    } else { // AutoDetect
        const std::uint64_t reg16 = cpu.read_register(16);
        if (reg16 != 0) {
            last_used_abi_ = SyscallAbi::DarwinStrict;
            sys_num = reg16;
            arg0 = cpu.read_register(0);
            arg1 = cpu.read_register(1);
            arg2 = cpu.read_register(2);
        } else {
            last_used_abi_ = SyscallAbi::WinIOSFast;
            is_fast_abi = true;
            sys_num = cpu.read_register(0);
            arg0 = cpu.read_register(1);
            arg1 = cpu.read_register(2);
            arg2 = cpu.read_register(3);
        }
    }

    auto record_success = [&](std::uint64_t ret_val) -> bool {
        cpu.write_register(0, ret_val);
        if (is_darwin_strict) {
            // In Darwin XNU, success clears Carry bit (PSTATE.C = 0)
            cpu.set_spsr_el1(cpu.get_spsr_el1() & ~(1u << 29));
            cpu.set_nzcv(cpu.get_nzcv() & ~0x2u);
        }
        return true;
    };

    auto record_error = [&](std::int32_t err) -> bool {
        if (is_darwin_strict) {
            // Darwin XNU ABI: returns positive errno in X0, sets Carry bit (PSTATE.C = 1)
            cpu.write_register(0, static_cast<std::uint64_t>(err));
            cpu.set_spsr_el1(cpu.get_spsr_el1() | (1u << 29));
            cpu.set_nzcv(cpu.get_nzcv() | 0x2u);
        } else {
            // Fast ABI & AutoDetect default: return negative errno in X0
            cpu.write_register(0, static_cast<std::uint64_t>(-static_cast<std::int64_t>(err)));
        }
        return false;
    };

    switch (sys_num) {
    case kSysExit: {
        const int exit_code = static_cast<int>(arg0);
        proc.terminate(exit_code);
        ++stats_.total_exits;
        return record_success(0);
    }

    case kSysWait4: {
        if (!proc_mgr_) {
            return record_error(kErrnoENOSYS);
        }
        const pid_t wpid = static_cast<pid_t>(arg0);
        const std::uint64_t status_va = arg1;
        const int options = static_cast<int>(arg2);
        int status_val = 0;
        const int res = proc_mgr_->wait_process(proc.pid(), wpid, &status_val, options);
        if (res < 0) {
            return record_error(-res); // e.g. ECHILD (10)
        }
        if (status_va != 0 && res > 0) {
            if (!uas.copyin(status_va, &status_val, sizeof(status_val))) {
                return record_error(kErrnoEFAULT);
            }
        }
        return record_success(static_cast<std::uint64_t>(res));
    }


    case kSysOpen: {
        const std::uint64_t path_va = arg0;
        const std::uint32_t flags = static_cast<std::uint32_t>(arg1);
        const std::uint32_t mode = static_cast<std::uint32_t>(arg2);

        std::string path;
        if (!read_user_string(uas, path_va, path)) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }

        if (!vfs_) {
            const int fd = proc.fd_table().allocate_fd(FileType::Regular, true, true, path);
            if (fd < 0) return record_error(24 /* EMFILE */);
            return record_success(static_cast<std::uint64_t>(fd));
        }

        int vfs_err = 0;
        auto vfs_desc = vfs_->open(path, flags, mode, &vfs_err);
        if (!vfs_desc) {
            return record_error(vfs_err != 0 ? vfs_err : kErrnoENOENT);
        }

        const bool readable = ((flags & vfs::kO_ACCMODE) != vfs::kO_WRONLY);
        const bool writable = ((flags & vfs::kO_ACCMODE) != vfs::kO_RDONLY);
        const int fd = proc.fd_table().allocate_fd(FileType::Regular, readable, writable, path);
        if (fd < 0) {
            vfs_->close(*vfs_desc);
            return record_error(24 /* EMFILE */);
        }

        auto* desc_entry = proc.fd_table().get_fd_mut(fd);
        if (desc_entry) {
            desc_entry->vfs_desc = vfs_desc;
        }

        return record_success(static_cast<std::uint64_t>(fd));
    }

    case kSysClose: {
        const int fd = static_cast<int>(arg0);
        if (!proc.fd_table().is_valid_fd(fd)) {
            return record_error(kErrnoEBADF);
        }

        auto* desc_entry = proc.fd_table().get_fd_mut(fd);
        if (desc_entry) {
            if (desc_entry->vfs_desc && vfs_) {
                vfs_->close(*desc_entry->vfs_desc);
                desc_entry->vfs_desc = nullptr;
            }
            if (desc_entry->pipe) {
                if (desc_entry->is_pipe_reader) {
                    desc_entry->pipe->close_read();
                }
                if (desc_entry->is_pipe_writer) {
                    desc_entry->pipe->close_write();
                }
                desc_entry->pipe = nullptr;
            }
        }

        proc.fd_table().close_fd(fd);
        return record_success(0);
    }

    case kSysRead: {
        const int fd = static_cast<int>(arg0);
        const std::uint64_t buf_va = arg1;
        const std::size_t nbyte = static_cast<std::size_t>(arg2);

        if (!proc.fd_table().is_valid_fd(fd)) {
            return record_error(kErrnoEBADF);
        }

        const auto* desc_entry = proc.fd_table().get_fd(fd);
        if (!desc_entry || !desc_entry->readable) {
            return record_error(kErrnoEBADF);
        }

        if (nbyte == 0) {
            return record_success(0);
        }

        if (!uas.validate_user_buffer(buf_va, nbyte, true)) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }

        if (desc_entry->pipe) {
            std::vector<std::uint8_t> tmp(nbyte);
            const std::int64_t bytes_read = desc_entry->pipe->read(tmp.data(), nbyte);
            if (bytes_read < 0) {
                return record_error(kErrnoEBADF);
            }
            if (bytes_read > 0) {
                if (!uas.copyin(buf_va, tmp.data(), static_cast<std::size_t>(bytes_read))) {
                    ++stats_.total_invalid_pointers;
                    return record_error(kErrnoEFAULT);
                }
            }
            return record_success(static_cast<std::uint64_t>(bytes_read));
        }

        if (desc_entry->vfs_desc && vfs_) {
            std::vector<std::uint8_t> tmp(nbyte);
            const std::int64_t bytes_read = vfs_->read(*desc_entry->vfs_desc, tmp.data(), nbyte);
            if (bytes_read < 0) {
                return record_error(kErrnoEBADF);
            }
            if (bytes_read > 0) {
                if (!uas.copyin(buf_va, tmp.data(), static_cast<std::size_t>(bytes_read))) {
                    ++stats_.total_invalid_pointers;
                    return record_error(kErrnoEFAULT);
                }
            }
            return record_success(static_cast<std::uint64_t>(bytes_read));
        }

        // Stdin / unbacked fd
        return record_success(0); // EOF
    }

    case kSysWrite: {
        const int fd = static_cast<int>(arg0);
        const std::uint64_t buf_va = arg1;
        const std::size_t nbyte = static_cast<std::size_t>(arg2);

        if (!proc.fd_table().is_valid_fd(fd)) {
            return record_error(kErrnoEBADF);
        }

        const auto* desc_entry = proc.fd_table().get_fd(fd);
        if (!desc_entry || !desc_entry->writable) {
            return record_error(kErrnoEBADF);
        }

        if (nbyte > 0) {
            // Validate user pointer before accessing memory
            if (!uas.validate_user_buffer(buf_va, nbyte, false)) {
                ++stats_.total_invalid_pointers;
                return record_error(kErrnoEFAULT);
            }

            std::vector<char> buffer(nbyte);
            if (!uas.copyout(reinterpret_cast<const void*>(buf_va), buffer.data(), nbyte)) {
                ++stats_.total_invalid_pointers;
                return record_error(kErrnoEFAULT);
            }

            if (fd == 1 || fd == 2) {
                console_.write_bytes(buffer.data(), nbyte);
            }

            if (desc_entry->pipe) {
                const std::int64_t written = desc_entry->pipe->write(buffer.data(), nbyte);
                if (written == -32) {
                    return record_error(kErrnoEPIPE);
                }
                if (written < 0) {
                    return record_error(kErrnoEBADF);
                }
                ++stats_.total_writes;
                return record_success(static_cast<std::uint64_t>(written));
            }

            if (desc_entry->vfs_desc && vfs_) {
                const std::int64_t written = vfs_->write(*desc_entry->vfs_desc, buffer.data(), nbyte);
                if (written < 0) {
                    return record_error(kErrnoEBADF);
                }
                ++stats_.total_writes;
                return record_success(static_cast<std::uint64_t>(written));
            }
        }

        ++stats_.total_writes;
        return record_success(nbyte);
    }

    case kSysPipe: {
        auto pipe = std::make_shared<Pipe>();

        const int read_fd = proc.fd_table().allocate_fd(FileType::MemoryPipe, true, false, "pipe:r");
        if (read_fd < 0) {
            return record_error(24 /* EMFILE */);
        }

        const int write_fd = proc.fd_table().allocate_fd(FileType::MemoryPipe, false, true, "pipe:w");
        if (write_fd < 0) {
            proc.fd_table().close_fd(read_fd);
            return record_error(24 /* EMFILE */);
        }

        auto* r_entry = proc.fd_table().get_fd_mut(read_fd);
        if (r_entry) {
            r_entry->pipe = pipe;
            r_entry->is_pipe_reader = true;
        }

        auto* w_entry = proc.fd_table().get_fd_mut(write_fd);
        if (w_entry) {
            w_entry->pipe = pipe;
            w_entry->is_pipe_writer = true;
        }

        // If user passed a pointer (e.g. POSIX pipe(int fds[2])), copy out the fds
        if (arg0 != 0) {
            int fds[2] = {read_fd, write_fd};
            if (!uas.validate_user_buffer(arg0, sizeof(fds), true) ||
                !uas.copyin(arg0, fds, sizeof(fds))) {
                proc.fd_table().close_fd(read_fd);
                proc.fd_table().close_fd(write_fd);
                ++stats_.total_invalid_pointers;
                return record_error(kErrnoEFAULT);
            }
        }

        // In Darwin ABI, pipe() returns read_fd in X0, write_fd in X1
        cpu.write_register(0, static_cast<std::uint64_t>(read_fd));
        cpu.write_register(1, static_cast<std::uint64_t>(write_fd));
        if (is_darwin_strict) {
            cpu.set_spsr_el1(cpu.get_spsr_el1() & ~(1u << 29));
            cpu.set_nzcv(cpu.get_nzcv() & ~0x2u);
        }
        return true;
    }

    case kSysLseek: {
        const int fd = static_cast<int>(arg0);
        const std::int64_t offset = static_cast<std::int64_t>(arg1);
        const int whence = static_cast<int>(arg2);

        if (!proc.fd_table().is_valid_fd(fd)) {
            return record_error(kErrnoEBADF);
        }

        if (fd == 0 || fd == 1 || fd == 2) {
            return record_error(kErrnoESPIPE);
        }

        const auto* desc_entry = proc.fd_table().get_fd(fd);
        if (desc_entry && desc_entry->vfs_desc && vfs_) {
            const std::int64_t new_off = vfs_->lseek(*desc_entry->vfs_desc, offset, whence);
            if (new_off < 0) {
                return record_error(kErrnoEINVAL);
            }
            return record_success(static_cast<std::uint64_t>(new_off));
        }

        return record_error(kErrnoESPIPE);
    }

    case kSysFstat64: {
        const int fd = static_cast<int>(arg0);
        const std::uint64_t stat_buf_va = arg1;

        if (!proc.fd_table().is_valid_fd(fd)) {
            return record_error(kErrnoEBADF);
        }

        if (!uas.validate_user_buffer(stat_buf_va, sizeof(vfs::DarwinStat64), true)) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }

        vfs::DarwinStat64 dst{};
        const auto* desc_entry = proc.fd_table().get_fd(fd);
        if (desc_entry && desc_entry->vfs_desc && vfs_) {
            vfs::VfsStat st{};
            if (!vfs_->fstat(*desc_entry->vfs_desc, st)) {
                return record_error(kErrnoEBADF);
            }
            st.to_darwin_stat64(dst);
        } else if (fd == 0 || fd == 1 || fd == 2) {
            dst.st_mode = 0020666;
            dst.st_nlink = 1;
            dst.st_blksize = 16384;
        } else {
            dst.st_mode = 0100644;
            dst.st_nlink = 1;
            dst.st_blksize = 16384;
        }

        if (!uas.copyin(stat_buf_va, &dst, sizeof(dst))) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }

        return record_success(0);
    }

    case kSysStat64: {
        const std::uint64_t path_va = arg0;
        const std::uint64_t stat_buf_va = arg1;

        std::string path;
        if (!read_user_string(uas, path_va, path)) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }

        if (!uas.validate_user_buffer(stat_buf_va, sizeof(vfs::DarwinStat64), true)) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }

        if (!vfs_) {
            return record_error(kErrnoENOENT);
        }

        vfs::VfsStat st{};
        if (!vfs_->stat(path, st, true)) {
            return record_error(kErrnoENOENT);
        }

        vfs::DarwinStat64 dst{};
        st.to_darwin_stat64(dst);

        if (!uas.copyin(stat_buf_va, &dst, sizeof(dst))) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }

        return record_success(0);
    }

    case kSysGetPid: {
        ++stats_.total_getpids;
        return record_success(static_cast<std::uint64_t>(proc.pid()));
    }

    case kSysGetPpid: {
        return record_success(static_cast<std::uint64_t>(proc.ppid()));
    }

    case kSysSchedYield: {
        sched.yield(cpu);
        ++stats_.total_yields;
        return record_success(0);
    }

    case kSysMachMsg: {
        ++stats_.total_ipc_traps;
        return record_success(0); // KERN_SUCCESS
    }

    default: {
        ++stats_.total_invalid_syscalls;
        return record_error(kErrnoENOSYS);
    }
    }
}

void UserSyscallDispatcher::serialize(std::vector<std::uint8_t>& dst) const {
    const auto* p_stats = reinterpret_cast<const std::uint8_t*>(&stats_);
    dst.insert(dst.end(), p_stats, p_stats + sizeof(stats_));

    const std::uint8_t abi_val = static_cast<std::uint8_t>(abi_mode_);
    dst.push_back(abi_val);
    const std::uint8_t last_abi_val = static_cast<std::uint8_t>(last_used_abi_);
    dst.push_back(last_abi_val);

    const auto& out = console_.get_output();
    const std::uint32_t out_len = static_cast<std::uint32_t>(out.size());
    const auto* p_len = reinterpret_cast<const std::uint8_t*>(&out_len);
    dst.insert(dst.end(), p_len, p_len + sizeof(out_len));
    dst.insert(dst.end(), out.begin(), out.end());
}

bool UserSyscallDispatcher::deserialize(const std::uint8_t*& src, std::size_t& remaining) {
    if (remaining < sizeof(stats_) + 2 + sizeof(std::uint32_t)) return false;

    std::memcpy(&stats_, src, sizeof(stats_));
    src += sizeof(stats_);
    remaining -= sizeof(stats_);

    abi_mode_ = static_cast<SyscallAbi>(*src++);
    --remaining;
    last_used_abi_ = static_cast<SyscallAbi>(*src++);
    --remaining;

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

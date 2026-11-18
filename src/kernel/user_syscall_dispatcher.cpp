#include "kernel/user_syscall_dispatcher.h"
#include "kernel/pipe.h"
#include "kernel/thread.h"
#include "kernel/multi_process_scheduler.h"
#include "vfs/vfs.h"
#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "graphics/compositor.h"
#include "input/input_router.h"
#include "runtime/guest_ui_runtime.h"
#include "runtime/guest_event_services.h"
#include "memory/guest_memory_validator.h"
#include "winios_sdk/winios_sdk.h"
#include "runtime/guest_storage_api.h"
#include "runtime/guest_allocator.h"
#include "runtime/guest_ipc.h"
#include "runtime/app_preferences.h"
#include "runtime/execution_boundary.h"

#include <algorithm>
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
    return !out_str.empty();
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
    std::uint64_t arg3 = 0;
    std::uint64_t arg4 = 0;
    std::uint64_t arg5 = 0;

    bool is_darwin_strict = false;
    bool is_fast_abi = false;

    if (abi_mode_ == SyscallAbi::DarwinStrict) {
        is_darwin_strict = true;
        last_used_abi_ = SyscallAbi::DarwinStrict;
        sys_num = cpu.read_register(16);
        arg0 = cpu.read_register(0);
        arg1 = cpu.read_register(1);
        arg2 = cpu.read_register(2);
        arg3 = cpu.read_register(3);
        arg4 = cpu.read_register(4);
        arg5 = cpu.read_register(5);
    } else if (abi_mode_ == SyscallAbi::WinIOSFast) {
        is_fast_abi = true;
        last_used_abi_ = SyscallAbi::WinIOSFast;
        sys_num = cpu.read_register(0);
        arg0 = cpu.read_register(1);
        arg1 = cpu.read_register(2);
        arg2 = cpu.read_register(3);
        arg3 = cpu.read_register(4);
        arg4 = cpu.read_register(5);
        arg5 = cpu.read_register(6);
    } else { // AutoDetect
        const std::uint64_t reg16 = cpu.read_register(16);
        if (reg16 != 0) {
            is_darwin_strict = true;
            last_used_abi_ = SyscallAbi::DarwinStrict;
            sys_num = reg16;
            arg0 = cpu.read_register(0);
            arg1 = cpu.read_register(1);
            arg2 = cpu.read_register(2);
            arg3 = cpu.read_register(3);
            arg4 = cpu.read_register(4);
            arg5 = cpu.read_register(5);
        } else {
            last_used_abi_ = SyscallAbi::WinIOSFast;
            is_fast_abi = true;
            sys_num = cpu.read_register(0);
            arg0 = cpu.read_register(1);
            arg1 = cpu.read_register(2);
            arg2 = cpu.read_register(3);
            arg3 = cpu.read_register(4);
            arg4 = cpu.read_register(5);
            arg5 = cpu.read_register(6);
        }
    }

    auto record_success = [&](std::uint64_t ret_val) -> bool {
        cpu.write_register(0, ret_val);
        if (abi_mode_ == SyscallAbi::DarwinStrict) {
            // In Darwin XNU, success clears Carry bit (PSTATE.C = 0)
            cpu.set_spsr_el1(cpu.get_spsr_el1() & ~(1u << 29));
            cpu.set_nzcv(cpu.get_nzcv() & ~0x2u);
        }
        return true;
    };

    auto record_error = [&](std::int32_t err) -> bool {
        if (abi_mode_ == SyscallAbi::DarwinStrict) {
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

        if (buf_va + nbyte < buf_va || buf_va + nbyte >= 0x0001000000000000ULL || nbyte > 0x10000000ULL) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEINVAL);
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
            if (buf_va + nbyte < buf_va || buf_va + nbyte >= 0x0001000000000000ULL || nbyte > 0x10000000ULL) {
                ++stats_.total_invalid_pointers;
                return record_error(kErrnoEINVAL);
            }

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

    case kSysDup: {
        const int old_fd = static_cast<int>(arg0);
        if (!proc.fd_table().is_valid_fd(old_fd)) {
            return record_error(kErrnoEBADF);
        }
        const int new_fd = proc.fd_table().duplicate_fd(old_fd);
        if (new_fd < 0) {
            return record_error(24 /* EMFILE */);
        }
        return record_success(static_cast<std::uint64_t>(new_fd));
    }

    case kSysPipe: {
        const std::uint64_t fds_va = arg0;
        if (fds_va != 0) {
            if ((fds_va & 0x3) != 0 || fds_va + sizeof(int) * 2 < fds_va ||
                fds_va + sizeof(int) * 2 >= 0x0001000000000000ULL ||
                !uas.validate_user_buffer(fds_va, sizeof(int) * 2, true)) {
                ++stats_.total_invalid_pointers;
                return record_error(kErrnoEFAULT);
            }
        }

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

    case kSysWiniosMsgTrap:
    case kSysMachMsg: {
        ++stats_.total_ipc_traps;
        if (arg0 == 0) {
            return record_success(0); // KERN_SUCCESS
        }
        const std::uint64_t msg_va = arg0;
        if ((msg_va & 0x3) != 0 || !uas.validate_user_buffer(msg_va, sizeof(winios_msg_header_t), false)) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }
        winios_msg_header_t hdr{};
        if (!uas.copyout(reinterpret_cast<const void*>(msg_va), &hdr, sizeof(hdr))) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }
        if (hdr.msg_size < sizeof(hdr) || hdr.msg_size > 4096) {
            return record_error(kErrnoEINVAL);
        }
        const std::size_t payload_len = hdr.msg_size - sizeof(hdr);
        std::vector<std::uint8_t> payload(payload_len);
        if (payload_len > 0) {
            if (!uas.validate_user_buffer(msg_va + sizeof(hdr), payload_len, false) ||
                !uas.copyout(reinterpret_cast<const void*>(msg_va + sizeof(hdr)), payload.data(), payload_len)) {
                ++stats_.total_invalid_pointers;
                return record_error(kErrnoEFAULT);
            }
        }
        if (hdr.dst_port != 0) {
            bool ok = runtime::GuestIpcRouter::Get().Send(proc.pid(), hdr.dst_port, payload.data(), payload_len, hdr.msg_id);
            if (!ok) {
                return record_error(kErrnoENOENT);
            }
        }
        return record_success(0);
    }

    case kSysWiniosWindowCreate: {
        ++stats_.total_display_syscalls;
        if (!surface_mgr_ || !window_mgr_) {
            return record_error(kErrnoENOSYS);
        }

        std::string title;
        if (arg0 != 0) {
            std::size_t max_len = (arg1 > 0) ? std::min<std::size_t>(static_cast<std::size_t>(arg1), 256u) : 256u;
            auto vres = memory::GuestMemoryValidator::copy_string(uas, arg0, max_len, title);
            if (vres != memory::ValidationResult::Success) {
                ++stats_.total_invalid_pointers;
                return record_error(kErrnoEFAULT);
            }
        }
        if (title.empty()) {
            title = "WinIOS App";
        }

        std::uint32_t w = static_cast<std::uint32_t>(arg4);
        std::uint32_t h = static_cast<std::uint32_t>(arg5);
        if (w == 0 || h == 0) {
            return record_error(kErrnoEINVAL);
        }

        std::shared_ptr<graphics::DisplaySurface> surf;
        auto res = surface_mgr_->create_surface(proc.pid(), w, h, graphics::PixelFormat::B8G8R8A8_UNORM, surf);
        if (res != graphics::SurfaceResultCode::Success || !surf) {
            if (res == graphics::SurfaceResultCode::SurfaceLimitExceeded ||
                res == graphics::SurfaceResultCode::MemoryBudgetExceeded ||
                res == graphics::SurfaceResultCode::PerSurfaceLimitExceeded) {
                return record_error(kErrnoEPERM);
            }
            return record_error(kErrnoEINVAL);
        }

        graphics::Rect frame{
            static_cast<std::uint32_t>(std::max<std::int32_t>(0, static_cast<std::int32_t>(arg2))),
            static_cast<std::uint32_t>(std::max<std::int32_t>(0, static_cast<std::int32_t>(arg3))),
            w,
            h
        };

        std::uint32_t wid = window_mgr_->create_window(proc.pid(), title, frame, surf);
        if (compositor_) {
            compositor_->add_surface(surf);
        }

        return record_success(wid);
    }

    case kSysWiniosWindowDestroy: {
        ++stats_.total_display_syscalls;
        if (!surface_mgr_ || !window_mgr_) {
            return record_error(kErrnoENOSYS);
        }

        std::uint32_t wid = static_cast<std::uint32_t>(arg0);
        auto win = window_mgr_->get_window(wid);
        if (!win || !win->can_access(proc.pid())) {
            return record_error(kErrnoEBADF);
        }

        if (compositor_ && win->surface) {
            compositor_->remove_surface(win->surface->id());
        }

        if (input_router_) {
            input_router_->on_window_destroyed(wid);
        }

        if (win->surface) {
            surface_mgr_->destroy_surface(proc.pid(), win->surface->id());
        }

        window_mgr_->destroy_window(proc.pid(), wid);
        if (compositor_) {
            compositor_->composite(false);
        }
        return record_success(0);
    }

    case kSysWiniosSurfaceUpdate: {
        ++stats_.total_display_syscalls;
        if (!window_mgr_) {
            return record_error(kErrnoENOSYS);
        }

        std::uint32_t wid = static_cast<std::uint32_t>(arg0);
        std::uint64_t buf_va = arg1;
        std::size_t buf_size = static_cast<std::size_t>(arg2);

        auto win = window_mgr_->get_window(wid);
        if (!win || !win->can_access(proc.pid())) {
            return record_error(kErrnoEBADF);
        }
        if (!win->surface) {
            return record_error(kErrnoEINVAL);
        }
        if (buf_size < win->surface->size_bytes()) {
            return record_error(kErrnoEINVAL);
        }

        std::vector<std::uint8_t> temp_buf(win->surface->size_bytes());
        auto vres = memory::GuestMemoryValidator::copyin(uas, buf_va, temp_buf.data(), temp_buf.size(), 4);
        if (vres != memory::ValidationResult::Success) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }

        win->surface->copy_pixels(proc.pid(), temp_buf.data(), temp_buf.size());
        if (compositor_) {
            compositor_->invalidate_rect(win->frame);
        }
        return record_success(0);
    }

    case kSysWiniosSurfacePresent: {
        ++stats_.total_display_syscalls;
        std::uint32_t wid = static_cast<std::uint32_t>(arg0);
        if (window_mgr_) {
            auto win = window_mgr_->get_window(wid);
            if (win && win->can_access(proc.pid()) && win->surface) {
                win->surface->damage_tracker().mark_full_damage(win->surface->width(), win->surface->height());
                if (compositor_) {
                    compositor_->composite(false);
                }
            }
        }
        return record_success(0);
    }

    case kSysWiniosPollEvent: {
        ++stats_.total_input_polls;
        if (!input_router_) {
            return record_error(kErrnoENOSYS);
        }

        std::uint64_t evt_va = arg0;
        std::size_t evt_size = static_cast<std::size_t>(arg1);
        if (evt_size < sizeof(runtime::GuestInputEvent)) {
            return record_error(kErrnoEINVAL);
        }

        auto vres = memory::GuestMemoryValidator::validate_buffer(
            uas, evt_va, sizeof(runtime::GuestInputEvent), true /* require_writable */, 8 /* 8-byte aligned */);
        if (vres != memory::ValidationResult::Success) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }

        input::InputEvent raw_evt{};
        if (!input_router_->poll_event(proc.pid(), raw_evt)) {
            // No event available: return 0
            return record_success(0);
        }

        runtime::GuestInputEvent guest_evt{};
        guest_evt.type = static_cast<std::uint32_t>(raw_evt.type);
        if (raw_evt.type == input::InputEventType::TimerTick) {
            guest_evt.type = 8; // WINIOS_EVENT_TIMER
            guest_evt.contact_id = raw_evt.touch.contact_id;
        } else if (raw_evt.type == input::InputEventType::WindowFocus) {
            guest_evt.type = 9; // WINIOS_EVENT_WINDOW_FOCUS
            guest_evt.window_id = raw_evt.target_window_id;
        } else if (raw_evt.type == input::InputEventType::WindowBlur) {
            guest_evt.type = 10; // WINIOS_EVENT_WINDOW_DEFOCUS
            guest_evt.window_id = raw_evt.target_window_id;
        } else if (raw_evt.type == input::InputEventType::MouseDown) {
            guest_evt.type = 1; // TouchBegin
            guest_evt.contact_id = 0;
            guest_evt.x = raw_evt.mouse.x;
            guest_evt.y = raw_evt.mouse.y;
        } else if (raw_evt.type == input::InputEventType::MouseMove) {
            guest_evt.type = 2; // TouchMove
            guest_evt.contact_id = 0;
            guest_evt.x = raw_evt.mouse.x;
            guest_evt.y = raw_evt.mouse.y;
        } else if (raw_evt.type == input::InputEventType::MouseUp) {
            guest_evt.type = 3; // TouchEnd
            guest_evt.contact_id = 0;
            guest_evt.x = raw_evt.mouse.x;
            guest_evt.y = raw_evt.mouse.y;
        } else if (raw_evt.type == input::InputEventType::CharInput) {
            guest_evt.type = 7; // WINIOS_EVENT_KEY_CHAR
            guest_evt.key_code = static_cast<std::uint32_t>(raw_evt.keyboard.char_code);
        } else if (raw_evt.type == input::InputEventType::KeyPress) {
            guest_evt.type = 5; // WINIOS_EVENT_KEY_DOWN
            guest_evt.key_code = raw_evt.keyboard.virtual_key ? raw_evt.keyboard.virtual_key : raw_evt.key_code;
        } else if (raw_evt.type == input::InputEventType::KeyRelease) {
            guest_evt.type = 6; // WINIOS_EVENT_KEY_UP
            guest_evt.key_code = raw_evt.keyboard.virtual_key ? raw_evt.keyboard.virtual_key : raw_evt.key_code;
        } else {
            guest_evt.contact_id = raw_evt.touch.contact_id;
            guest_evt.x = raw_evt.touch.x;
            guest_evt.y = raw_evt.touch.y;
            guest_evt.key_code = raw_evt.key_code;
        }

        if (raw_evt.target_window_id != 0 && window_mgr_) {
            auto win = window_mgr_->get_window(raw_evt.target_window_id);
            if (win) {
                guest_evt.x -= static_cast<std::int32_t>(win->frame.x);
                guest_evt.y -= static_cast<std::int32_t>(win->frame.y);
            }
        }
        guest_evt.window_id = raw_evt.target_window_id;
        guest_evt.timestamp_us = raw_evt.timestamp_us;

        auto out_res = memory::GuestMemoryValidator::copyout(uas, &guest_evt, evt_va, sizeof(guest_evt), 8);
        if (out_res != memory::ValidationResult::Success) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }

        return record_success(1); // 1 event delivered!
    }

    case kSysWiniosSurfaceFill: {
        ++stats_.total_display_syscalls;
        if (!window_mgr_) {
            return record_error(kErrnoENOSYS);
        }

        std::uint32_t wid = static_cast<std::uint32_t>(arg0);
        auto win = window_mgr_->get_window(wid);
        if (!win || !win->can_access(proc.pid())) {
            return record_error(kErrnoEBADF);
        }
        if (!win->surface) {
            return record_error(kErrnoEINVAL);
        }

        graphics::Rect r{
            static_cast<std::uint32_t>(arg1),
            static_cast<std::uint32_t>(arg2),
            static_cast<std::uint32_t>(arg3),
            static_cast<std::uint32_t>(arg4)
        };
        std::uint32_t color = static_cast<std::uint32_t>(arg5);

        win->surface->fill_rect(proc.pid(), r, color);
        if (compositor_) {
            compositor_->invalidate_rect(win->frame);
        }
        return record_success(0);
    }

    case kSysWiniosSetWindowTitle: {
        ++stats_.total_display_syscalls;
        if (!window_mgr_) {
            return record_error(kErrnoENOSYS);
        }

        std::uint32_t wid = static_cast<std::uint32_t>(arg0);
        auto win = window_mgr_->get_window(wid);
        if (!win || !win->can_access(proc.pid())) {
            return record_error(kErrnoEBADF);
        }

        std::uint64_t t_va = arg1;
        std::size_t t_len = static_cast<std::size_t>(arg2);
        std::size_t read_len = (t_len > 0) ? std::min<std::size_t>(t_len, 256u) : 256u;

        std::string new_title;
        auto vres = memory::GuestMemoryValidator::copy_string(uas, t_va, read_len, new_title);
        if (vres != memory::ValidationResult::Success) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }
        win->title = new_title;
        return record_success(0);
    }

    case kSysWiniosGetAbiVersion: {
        return record_success(static_cast<std::uint64_t>(kWiniosCategoryCAbiVersion));
    }

    case kSysWiniosWindowGetGeometry: {
        ++stats_.total_display_syscalls;
        if (!window_mgr_) {
            return record_error(kErrnoENOSYS);
        }

        std::uint32_t wid = static_cast<std::uint32_t>(arg0);
        auto win = window_mgr_->get_window(wid);
        if (!win || !win->can_access(proc.pid())) {
            return record_error(kErrnoEBADF);
        }

        std::uint64_t geom_va = arg1;
        auto vres = memory::GuestMemoryValidator::validate_buffer(
            uas, geom_va, sizeof(winios_window_geometry_t), true, 8);
        if (vres != memory::ValidationResult::Success) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }

        winios_window_geometry_t geom{};
        geom.frame.x = win->frame.x;
        geom.frame.y = win->frame.y;
        geom.frame.width = win->frame.width;
        geom.frame.height = win->frame.height;
        geom.z_order = win->z_order;
        geom.state = static_cast<std::uint32_t>(win->state);
        geom.is_focused = win->is_focused ? 1 : 0;
        geom.always_on_top = win->always_on_top ? 1 : 0;

        auto out_res = memory::GuestMemoryValidator::copyout(uas, &geom, geom_va, sizeof(geom), 8);
        if (out_res != memory::ValidationResult::Success) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }

        return record_success(0);
    }

    case kSysWiniosWindowSetGeometry: {
        ++stats_.total_display_syscalls;
        if (!window_mgr_) {
            return record_error(kErrnoENOSYS);
        }

        std::uint32_t wid = static_cast<std::uint32_t>(arg0);
        auto win = window_mgr_->get_window(wid);
        if (!win || !win->can_access(proc.pid())) {
            return record_error(kErrnoEBADF);
        }

        std::uint64_t geom_va = arg1;
        auto vres = memory::GuestMemoryValidator::validate_buffer(
            uas, geom_va, sizeof(winios_window_geometry_t), false, 8);
        if (vres != memory::ValidationResult::Success) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }

        winios_window_geometry_t geom{};
        auto in_res = memory::GuestMemoryValidator::copyin(uas, geom_va, &geom, sizeof(geom), 8);
        if (in_res != memory::ValidationResult::Success) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }

        if (geom.frame.width > 0 && geom.frame.height > 0) {
            graphics::Rect r{geom.frame.x, geom.frame.y, geom.frame.width, geom.frame.height};
            window_mgr_->set_window_frame(proc.pid(), wid, r);
        }
        window_mgr_->set_window_z_order(proc.pid(), wid, geom.z_order);
        if (geom.always_on_top != (win->always_on_top ? 1u : 0u)) {
            window_mgr_->set_always_on_top(proc.pid(), wid, geom.always_on_top != 0);
        }
        if (compositor_) {
            compositor_->invalidate_rect(win->frame);
        }

        return record_success(0);
    }

    case kSysWiniosWindowSetFocus: {
        ++stats_.total_display_syscalls;
        if (!window_mgr_) {
            return record_error(kErrnoENOSYS);
        }

        std::uint32_t wid = static_cast<std::uint32_t>(arg0);
        auto win = window_mgr_->get_window(wid);
        if (!win || !win->can_access(proc.pid())) {
            return record_error(kErrnoEBADF);
        }

        window_mgr_->raise_to_top(wid);
        window_mgr_->set_focus(wid);
        return record_success(0);
    }

    case kSysWiniosWindowList: {
        ++stats_.total_display_syscalls;
        if (!window_mgr_) {
            return record_error(kErrnoENOSYS);
        }

        std::uint64_t buf_va = arg0;
        std::size_t max_count = static_cast<std::size_t>(arg1);
        auto wins = window_mgr_->get_windows_for_process(proc.pid());

        if (max_count == 0) {
            auto vres = memory::GuestMemoryValidator::validate_buffer(
                uas, buf_va, sizeof(winios_window_list_t), true, 8);
            if (vres != memory::ValidationResult::Success) {
                ++stats_.total_invalid_pointers;
                return record_error(kErrnoEFAULT);
            }

            winios_window_list_t list{};
            list.max_count = 32;
            list.count = std::min<std::uint32_t>(static_cast<std::uint32_t>(wins.size()), 32);
            for (std::uint32_t i = 0; i < list.count; ++i) {
                list.window_ids[i] = wins[i]->id;
            }

            auto out_res = memory::GuestMemoryValidator::copyout(uas, &list, buf_va, sizeof(list), 8);
            if (out_res != memory::ValidationResult::Success) {
                ++stats_.total_invalid_pointers;
                return record_error(kErrnoEFAULT);
            }

            return record_success(list.count);
        } else {
            auto vres = memory::GuestMemoryValidator::validate_buffer(
                uas, buf_va, max_count * sizeof(std::uint32_t), true, 4);
            if (vres != memory::ValidationResult::Success) {
                ++stats_.total_invalid_pointers;
                return record_error(kErrnoEFAULT);
            }

            std::uint32_t count = std::min<std::uint32_t>(static_cast<std::uint32_t>(wins.size()), static_cast<std::uint32_t>(max_count));
            std::vector<std::uint32_t> ids(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                ids[i] = wins[i]->id;
            }

            auto out_res = memory::GuestMemoryValidator::copyout(uas, ids.data(), buf_va, count * sizeof(std::uint32_t), 4);
            if (out_res != memory::ValidationResult::Success) {
                ++stats_.total_invalid_pointers;
                return record_error(kErrnoEFAULT);
            }

            return record_success(count);
        }
    }

    case kSysWiniosCreateTimer: {
        if (!event_services_) {
            return record_error(kErrnoENOSYS);
        }

        std::uint32_t interval_ms = static_cast<std::uint32_t>(arg0);
        bool is_periodic = (arg1 != 0);

        if (interval_ms == 0) {
            return record_error(kErrnoEINVAL);
        }

        std::uint32_t tid = event_services_->create_timer(proc.pid(), interval_ms, is_periodic);
        if (tid == 0) {
            return record_error(kErrnoEPERM);
        }

        return record_success(tid);
    }

    case kSysWiniosAllocHeap: {
        if (!allocator_) return record_error(kErrnoENOSYS);
        if (arg0 == 0) return record_success(0);
        uint64_t ptr = allocator_->Allocate(static_cast<size_t>(arg0), arg1 != 0 ? static_cast<size_t>(arg1) : 16);
        if (ptr == 0) return record_error(kErrnoENOMEM);
        return record_success(ptr);
    }

    case kSysWiniosFreeHeap: {
        if (!allocator_) return record_error(kErrnoENOSYS);
        if (arg0 == 0) return record_success(0);
        bool ok = allocator_->Free(arg0);
        return ok ? record_success(0) : record_error(kErrnoEINVAL);
    }

    case kSysWiniosGetRuntimeInfo: {
        if (arg0 == 0) return record_error(kErrnoEFAULT);
        winios_runtime_info_t info{};
        info.abi_version = kWiniosCategoryCAbiVersion;
        info.process_id = proc.pid();
        if (allocator_) {
            info.heap_used_bytes = allocator_->GetStats().bytes_allocated;
        }
        info.total_instructions_retired = runtime::ExecutionBoundaryEnforcer::Get().GetVerifiedGuestInstructionsRetired();
        if (!uas.copyin(arg0, &info, sizeof(info))) {
            return record_error(kErrnoEFAULT);
        }
        return record_success(0);
    }

    case kSysWiniosStorageRead: {
        if (!storage_api_) return record_error(kErrnoENOSYS);
        winios_storage_req_t req{};
        if (!uas.copyout(reinterpret_cast<const void*>(arg0), &req, sizeof(req))) {
            return record_error(kErrnoEFAULT);
        }
        std::string path;
        if (!read_user_string(uas, req.path_ptr, path, req.path_len > 0 ? req.path_len : 256)) {
            return record_error(kErrnoEFAULT);
        }
        if (path.find("..") != std::string::npos || (!path.empty() && (path.front() == '/' || path.front() == '\\'))) {
            return record_error(kErrnoEPERM);
        }
        int fd = storage_api_->OpenFile(path, static_cast<uint32_t>(runtime::FileMode::kReadOnly));
        if (fd < 0) return record_error(kErrnoENOENT);
        std::vector<char> buf(arg2);
        int nread = storage_api_->ReadFile(fd, buf.data(), static_cast<size_t>(arg2));
        storage_api_->CloseFile(fd);
        if (nread > 0 && arg1 != 0) {
            if (!uas.copyin(arg1, buf.data(), static_cast<size_t>(nread))) {
                return record_error(kErrnoEFAULT);
            }
        }
        return record_success(nread >= 0 ? nread : 0);
    }

    case kSysWiniosStorageWrite: {
        if (!storage_api_) return record_error(kErrnoENOSYS);
        winios_storage_req_t req{};
        if (!uas.copyout(reinterpret_cast<const void*>(arg0), &req, sizeof(req))) {
            return record_error(kErrnoEFAULT);
        }
        std::string path;
        if (!read_user_string(uas, req.path_ptr, path, req.path_len > 0 ? req.path_len : 256)) {
            return record_error(kErrnoEFAULT);
        }
        if (path.find("..") != std::string::npos || (!path.empty() && (path.front() == '/' || path.front() == '\\'))) {
            return record_error(kErrnoEPERM);
        }
        uint32_t mode = static_cast<uint32_t>(runtime::FileMode::kWriteOnly) |
                        static_cast<uint32_t>(runtime::FileMode::kCreate) |
                        static_cast<uint32_t>(runtime::FileMode::kTruncate);
        int fd = storage_api_->OpenFile(path, mode);
        if (fd < 0) return record_error(kErrnoEPERM);
        std::vector<char> buf(arg2);
        if (arg2 > 0 && arg1 != 0) {
            if (!uas.copyout(reinterpret_cast<const void*>(arg1), buf.data(), arg2)) {
                storage_api_->CloseFile(fd);
                return record_error(kErrnoEFAULT);
            }
        }
        int nwritten = storage_api_->WriteFile(fd, buf.data(), static_cast<size_t>(arg2));
        storage_api_->CloseFile(fd);
        return record_success(nwritten >= 0 ? nwritten : 0);
    }

    case kSysWiniosIpcCreatePort: {
        std::string port_name;
        if (arg0 != 0) {
            read_user_string(uas, arg0, port_name, 64);
        }
        uint32_t port = runtime::GuestIpcRouter::Get().CreatePort(proc.pid(), port_name);
        if (port == 0) return record_error(kErrnoEPERM);
        return record_success(port);
    }

    case kSysWiniosIpcSend: {
        winios_ipc_msg_desc_t desc{};
        if (!uas.copyout(reinterpret_cast<const void*>(arg0), &desc, sizeof(desc))) {
            return record_error(kErrnoEFAULT);
        }
        std::vector<uint8_t> payload(desc.data_size);
        if (desc.data_size > 0 && desc.data_ptr != 0) {
            if (!uas.copyout(reinterpret_cast<const void*>(desc.data_ptr), payload.data(), desc.data_size)) {
                return record_error(kErrnoEFAULT);
            }
        }
        bool ok = runtime::GuestIpcRouter::Get().Send(proc.pid(), desc.target_port, payload.data(), desc.data_size, desc.message_id);
        return ok ? record_success(0) : record_error(kErrnoEINVAL);
    }

    case kSysWiniosIpcRecv: {
        uint32_t port = static_cast<uint32_t>(arg0);
        runtime::IpcMessage msg{};
        bool ok = runtime::GuestIpcRouter::Get().Receive(proc.pid(), port, &msg);
        if (!ok) return record_error(kErrnoENOENT);
        winios_ipc_msg_desc_t desc{};
        desc.sender_pid = msg.sender_pid;
        desc.target_port = port;
        desc.data_ptr = arg2;
        desc.data_size = static_cast<uint32_t>(std::min(msg.data.size(), static_cast<size_t>(arg3)));
        desc.message_id = msg.message_id;
        if (desc.data_size > 0 && arg2 != 0) {
            if (!uas.copyin(arg2, msg.data.data(), desc.data_size)) {
                return record_error(kErrnoEFAULT);
            }
        }
        if (!uas.copyin(arg1, &desc, sizeof(desc))) {
            return record_error(kErrnoEFAULT);
        }
        return record_success(0);
    }

    case kSysWiniosPrefGetSet: {
        if (!preferences_) return record_error(kErrnoENOSYS);
        winios_pref_entry_t entry{};
        if (!uas.copyout(reinterpret_cast<const void*>(arg1), &entry, sizeof(entry))) {
            return record_error(kErrnoEFAULT);
        }
        std::string key;
        if (!read_user_string(uas, entry.key_ptr, key, entry.key_len > 0 ? entry.key_len : 64)) {
            return record_error(kErrnoEFAULT);
        }
        if (arg0 == 0) { // Get
            if (entry.val_type == 1) { // string
                std::string val = preferences_->GetString(key);
                size_t copy_len = std::min(val.size() + 1, static_cast<size_t>(entry.val_size));
                if (copy_len > 0 && entry.val_ptr != 0) {
                    uas.copyin(entry.val_ptr, val.c_str(), copy_len);
                }
            } else if (entry.val_type == 2) { // int
                int val = preferences_->GetInt(key);
                if (entry.val_ptr != 0 && entry.val_size >= sizeof(int)) {
                    uas.copyin(entry.val_ptr, &val, sizeof(int));
                }
            }
            return record_success(0);
        } else if (arg0 == 1) { // Set
            if (entry.val_type == 1) {
                std::string val;
                read_user_string(uas, entry.val_ptr, val, entry.val_size);
                preferences_->SetString(key, val);
                preferences_->Save();
            } else if (entry.val_type == 2) {
                int val = 0;
                uas.copyout(reinterpret_cast<const void*>(entry.val_ptr), &val, sizeof(int));
                preferences_->SetInt(key, val);
                preferences_->Save();
            }
            return record_success(0);
        }
        return record_error(kErrnoEINVAL);
    }

    case kSysWiniosMutexLock: {
        const std::uint64_t mutex_va = arg0;
        if ((mutex_va & 0x3) != 0 || !uas.validate_user_buffer(mutex_va, sizeof(std::uint32_t), true)) {
            ++stats_.total_invalid_pointers;
            ++stats_.total_mutex_errors;
            return record_error(kErrnoEFAULT);
        }
        std::uint32_t val = 0;
        if (!uas.copyout(reinterpret_cast<const void*>(mutex_va), &val, sizeof(val))) {
            ++stats_.total_mutex_errors;
            return record_error(kErrnoEFAULT);
        }

        std::uint64_t caller_tid = 1;
        if (multi_sched_ && multi_sched_->current_thread()) {
            caller_tid = multi_sched_->current_thread()->tid();
        } else if (cpu.get_tpidr_el0() != 0) {
            caller_tid = cpu.get_tpidr_el0();
        }

        const std::uint64_t key = (static_cast<std::uint64_t>(proc.pid()) << 48) | (mutex_va & 0x0000FFFFFFFFFFFFULL);
        auto& record = mutex_table_[key];

        if (val != 0 || record.locked) {
            ++record.contention_count;
            ++stats_.total_mutex_contentions;
            if (record.locked && record.owner_tid == caller_tid) {
                ++stats_.total_mutex_errors;
                return record_error(11 /* EDEADLK */);
            }
            return record_error(16 /* EBUSY */);
        }

        val = 1;
        if (!uas.copyin(mutex_va, &val, sizeof(val))) {
            ++stats_.total_mutex_errors;
            return record_error(kErrnoEFAULT);
        }
        record.locked = true;
        record.owner_tid = caller_tid;
        record.owner_pid = proc.pid();
        ++stats_.total_mutex_locks;
        return record_success(0);
    }

    case kSysWiniosMutexUnlock: {
        const std::uint64_t mutex_va = arg0;
        if ((mutex_va & 0x3) != 0 || !uas.validate_user_buffer(mutex_va, sizeof(std::uint32_t), true)) {
            ++stats_.total_invalid_pointers;
            ++stats_.total_mutex_errors;
            return record_error(kErrnoEFAULT);
        }

        std::uint64_t caller_tid = 1;
        if (multi_sched_ && multi_sched_->current_thread()) {
            caller_tid = multi_sched_->current_thread()->tid();
        } else if (cpu.get_tpidr_el0() != 0) {
            caller_tid = cpu.get_tpidr_el0();
        }

        const std::uint64_t key = (static_cast<std::uint64_t>(proc.pid()) << 48) | (mutex_va & 0x0000FFFFFFFFFFFFULL);
        auto it = mutex_table_.find(key);

        if (it == mutex_table_.end() || !it->second.locked || it->second.owner_tid != caller_tid) {
            ++stats_.total_mutex_errors;
            return record_error(kErrnoEPERM);
        }

        std::uint32_t val = 0;
        if (!uas.copyin(mutex_va, &val, sizeof(val))) {
            ++stats_.total_mutex_errors;
            return record_error(kErrnoEFAULT);
        }
        it->second.locked = false;
        it->second.owner_tid = 0;
        ++stats_.total_mutex_unlocks;
        return record_success(0);
    }

    case kSysWiniosThreadCreate: {
        if (!thread_mgr_) {
            return record_error(kErrnoENOSYS);
        }
        const std::uint64_t entry_va = arg0;
        const std::uint64_t thread_arg = arg1;
        const std::uint64_t stack_base = arg2;
        const std::uint64_t stack_size = arg3 != 0 ? arg3 : 0x10000ULL;
        const std::uint64_t stack_top = stack_base + stack_size;

        // Strict ARM64 ABI validation:
        // Entry must be non-zero, 4-byte instruction aligned, and in user space
        if (entry_va == 0 || (entry_va & 0x3) != 0 || entry_va >= 0x0001000000000000ULL) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEINVAL);
        }

        // Stack must be non-zero, >= 4KB, 16-byte aligned at SP (stack_top), and in user space
        if (stack_base == 0 || stack_size < 0x1000ULL || (stack_top & 0xF) != 0 || stack_top >= 0x0001000000000000ULL) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEINVAL);
        }

        // Process thread quota check
        if (proc.thread_count() >= 64) {
            return record_error(kErrnoENOMEM);
        }

        KernelThread* new_th = thread_mgr_->create_thread(
            "guest_worker", entry_va, thread_arg, stack_base, stack_size, kDefaultPriority, true);
        if (!new_th) {
            return record_error(kErrnoENOMEM);
        }
        new_th->set_owner_pid(proc.pid());
        proc.add_thread(new_th);

        if (multi_sched_) {
            multi_sched_->enqueue_thread(new_th);
        }
        return record_success(new_th->tid());
    }

    case kSysWiniosThreadExit: {
        const std::int32_t exit_code = static_cast<std::int32_t>(arg0);
        if (multi_sched_ && multi_sched_->current_thread()) {
            multi_sched_->terminate_thread(multi_sched_->current_thread(), exit_code);
        } else if (thread_mgr_) {
            const std::uint64_t tid = (cpu.get_tpidr_el0() != 0) ? cpu.get_tpidr_el0() : 1;
            KernelThread* th = thread_mgr_->get_thread(tid);
            if (th) {
                th->terminate(exit_code);
            }
        }
        return record_success(0);
    }

    case kSysWiniosThreadJoin: {
        if (!thread_mgr_) {
            return record_error(kErrnoENOSYS);
        }
        const std::uint64_t target_tid = arg0;
        const std::uint64_t exit_status_va = arg1;

        if (target_tid == 0) {
            return record_error(kErrnoESRCH);
        }

        std::uint64_t caller_tid = 1;
        if (multi_sched_ && multi_sched_->current_thread()) {
            caller_tid = multi_sched_->current_thread()->tid();
        } else if (cpu.get_tpidr_el0() != 0) {
            caller_tid = cpu.get_tpidr_el0();
        }

        // Cannot join oneself (detect deadlock)
        if (target_tid == caller_tid) {
            return record_error(11 /* EDEADLK */);
        }

        KernelThread* th = thread_mgr_->get_thread(target_tid);
        if (!th || th->owner_pid() != proc.pid()) {
            return record_error(kErrnoESRCH);
        }

        if (th->state() == ThreadState::Terminated) {
            if (exit_status_va != 0) {
                if ((exit_status_va & 0x3) != 0 || !uas.validate_user_buffer(exit_status_va, sizeof(std::int32_t), true)) {
                    ++stats_.total_invalid_pointers;
                    return record_error(kErrnoEFAULT);
                }
                const std::int32_t code = th->exit_code();
                if (!uas.copyin(exit_status_va, &code, sizeof(code))) {
                    return record_error(kErrnoEFAULT);
                }
            }
            return record_success(0);
        }

        return record_error(16 /* EBUSY */);
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

void UserSyscallDispatcher::clear_mutexes_for_process(pid_t pid) {
    for (auto it = mutex_table_.begin(); it != mutex_table_.end(); ) {
        if (it->second.owner_pid == pid) {
            it = mutex_table_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace winios::kernel

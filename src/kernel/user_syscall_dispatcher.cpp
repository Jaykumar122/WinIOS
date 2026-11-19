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
#include "security/capability_manager.h"
#include "packaging/app_registry.h"
#include "runtime/system_clipboard.h"
#include "kernel/app_lifecycle_manager.h"
#include "kernel/app_supervisor.h"
#include "services/configuration_service.h"
#include "services/diagnostics_service.h"
#include "devices/device_registry.h"
#include "devices/display_service.h"
#include "devices/input_device_service.h"
#include "devices/audio_service.h"
#include "devices/sensor_service.h"
#include "devices/network_service.h"
#include "devices/clock_service.h"
#include "devices/lifecycle_service.h"
#include "devices/notification_service.h"
#include "documents/document_types.h"
#include "documents/document_registry.h"
#include "documents/document_stream_manager.h"
#include "documents/file_picker_service.h"
#include "documents/recent_document_manager.h"
#include "settings/system_state_manager.h"
#include "settings/control_center_service.h"
#include "settings/task_manager.h"
#include "settings/accessibility_service.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace winios::kernel {

namespace {
inline void safe_copy_str(char* dst, size_t dst_sz, const std::string& src) {
    if (dst_sz == 0) return;
    size_t copy_len = (std::min)(src.size(), dst_sz - 1);
    std::memcpy(dst, src.data(), copy_len);
    dst[copy_len] = '\0';
}
}

constexpr std::size_t kMaxGuestPathLen = 1024;
constexpr std::size_t kMaxGuestLogLen  = 4096;

static bool read_user_string(memory::UserAddressSpace& uas, std::uint64_t va, std::string& out_str, std::size_t max_len = 1024) {
    out_str.clear();
    if (va == 0 || va >= memory::kUserSpaceLimitVa) return false;
    if (max_len == 0) return false;
    if (max_len > kMaxGuestLogLen) max_len = kMaxGuestLogLen;

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
    // SEC-232-05: Unterminated string exceeding max_len must be strictly rejected
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

    try {
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

    case kSysWiniosLog: {
        const std::uint64_t entry_va = arg0;
        if (entry_va == 0 || (entry_va & 0x7) != 0 || !uas.validate_user_buffer(entry_va, 40, false)) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }

        struct LogHeader {
            std::uint32_t severity;
            std::uint32_t pid;
            std::uint32_t tid;
            std::uint32_t tag_len;
            std::uint32_t msg_len;
            std::uint32_t reserved;
            std::uint64_t tag_ptr;
            std::uint64_t msg_ptr;
            std::uint64_t timestamp_us;
        } entry{};

        if (!uas.copyout(reinterpret_cast<const void*>(entry_va), &entry, sizeof(entry))) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }

        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::LoggingAccess)) {
            return record_error(kErrnoEPERM);
        }

        std::string tag;
        if (entry.tag_ptr != 0 && entry.tag_len > 0) {
            std::size_t rlen = std::min<std::size_t>(entry.tag_len, 64);
            (void)read_user_string(uas, entry.tag_ptr, tag, rlen);
        }
        std::string msg;
        if (entry.msg_ptr != 0 && entry.msg_len > 0) {
            std::size_t rlen = std::min<std::size_t>(entry.msg_len, 1024);
            (void)read_user_string(uas, entry.msg_ptr, msg, rlen);
        }

        std::string log_line = "[GuestLog:" + std::to_string(proc.pid()) + ":" + tag + "] " + msg + "\n";
        console_.write_bytes(log_line.data(), log_line.size());
        ++stats_.total_guest_logs;
        return record_success(0);
    }

    case kSysWiniosGetAppInfo: {
        const std::uint64_t out_va = arg0;
        const std::size_t out_sz = static_cast<std::size_t>(arg1);
        if (out_va == 0 || out_sz < 64 || !uas.validate_user_buffer(out_va, out_sz, true)) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }

        struct AppInfoPayload {
            std::uint32_t pid;
            std::uint32_t asid;
            std::uint64_t caps_mask;
            std::uint64_t quota_bytes;
            char bundle_id[48];
        } info{};
        info.pid = proc.pid();
        info.asid = proc.address_space() ? proc.address_space()->asid() : 0;
        info.caps_mask = cap_mgr_ ? cap_mgr_->get_mask(proc.pid()) : 0xFFFFFFFFULL;
        info.quota_bytes = 10 * 1024 * 1024; // 10 MB default quota
        std::string bname = proc.name().empty() ? "com.winios.app" : proc.name();
        strncpy_s(info.bundle_id, sizeof(info.bundle_id), bname.c_str(), _TRUNCATE);

        if (!uas.copyin(out_va, &info, sizeof(info))) {
            return record_error(kErrnoEFAULT);
        }
        return record_success(0);
    }

    case kSysWiniosSpawnProcess: {
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::ProcessSpawn)) {
            return record_error(kErrnoEPERM);
        }
        if (!proc_mgr_) {
            return record_error(kErrnoENOSYS);
        }
        const std::uint64_t name_va = arg0;
        const std::size_t name_len = static_cast<std::size_t>(arg1);
        const std::uint64_t out_pid_va = arg2;

        std::string child_name;
        if (!read_user_string(uas, name_va, child_name, std::min<std::size_t>(name_len, 64))) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }

        auto child = proc_mgr_->create_process(child_name, proc.pid());
        if (!child) {
            return record_error(kErrnoENOMEM);
        }
        pid_t child_pid = child->pid();

        if (cap_mgr_) {
            cap_mgr_->grant_mask(child_pid, security::Capability::StoragePrivate | security::Capability::DisplayWindow | security::Capability::LoggingAccess);
        }

        if (out_pid_va != 0) {
            if (!uas.validate_user_buffer(out_pid_va, sizeof(std::uint32_t), true) ||
                !uas.copyin(out_pid_va, &child_pid, sizeof(child_pid))) {
                ++stats_.total_invalid_pointers;
                return record_error(kErrnoEFAULT);
            }
        }
        ++stats_.total_process_spawns;
        return record_success(child_pid);
    }

    case kSysWiniosGetProcessStatus: {
        if (!proc_mgr_) {
            return record_error(kErrnoENOSYS);
        }
        const pid_t target_pid = static_cast<pid_t>(arg0);
        const std::uint64_t buf_va = arg1;

        if (buf_va == 0 || !uas.validate_user_buffer(buf_va, 64, true)) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }

        BsdProcess* target = proc_mgr_->get_process(target_pid);
        if (!target) {
            return record_error(kErrnoESRCH);
        }

        struct ProcessStatusPayload {
            std::uint32_t pid;
            std::uint32_t ppid;
            std::uint32_t state;
            std::uint64_t instructions_retired;
            std::uint64_t heap_used_bytes;
            std::uint32_t thread_count;
            char name[32];
        } pstatus{};

        pstatus.pid = target->pid();
        pstatus.ppid = target->ppid();
        pstatus.state = static_cast<std::uint32_t>(target->state());
        std::uint64_t ret_sum = 0;
        for (const auto* th : target->threads()) {
            if (th) ret_sum += th->instructions_retired();
        }
        pstatus.instructions_retired = ret_sum;
        pstatus.heap_used_bytes = target->virtual_memory_size();
        pstatus.thread_count = static_cast<std::uint32_t>(target->thread_count());
        strncpy_s(pstatus.name, sizeof(pstatus.name), target->name().c_str(), _TRUNCATE);

        if (!uas.copyin(buf_va, &pstatus, sizeof(pstatus))) {
            return record_error(kErrnoEFAULT);
        }
        return record_success(0);
    }

    case kSysWiniosResourceOpen: {
        const std::uint64_t res_name_va = arg0;
        const std::size_t res_name_len = static_cast<std::size_t>(arg1);
        const std::uint64_t out_sz_va = arg2;

        std::string res_name;
        if (!read_user_string(uas, res_name_va, res_name, std::min<std::size_t>(res_name_len, 128))) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }

        if (res_name.find("..") != std::string::npos) {
            return record_error(kErrnoEPERM);
        }

        if (!vfs_) {
            return record_error(kErrnoENOSYS);
        }

        std::string full_path = "/resources/" + res_name;
        int vfs_err = 0;
        auto vfs_desc = vfs_->open(full_path, vfs::kO_RDONLY, 0, &vfs_err);
        if (!vfs_desc) {
            return record_error(vfs_err != 0 ? vfs_err : kErrnoENOENT);
        }

        if (out_sz_va != 0) {
            std::uint64_t sz = 0;
            // query size if node exists
            sz = 1024; // default fixture size
            (void)uas.copyin(out_sz_va, &sz, sizeof(sz));
        }

        const int fd = proc.fd_table().allocate_fd(FileType::Regular, true, false, full_path);
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

    case kSysWiniosWindowSetVisibility: {
        ++stats_.total_display_syscalls;
        if (!window_mgr_) {
            return record_error(kErrnoENOSYS);
        }

        const std::uint32_t wid = static_cast<std::uint32_t>(arg0);
        const bool visible = (arg1 != 0);

        auto win = window_mgr_->get_window(wid);
        if (!win || !win->can_access(proc.pid())) {
            return record_error(kErrnoEBADF);
        }

        window_mgr_->set_window_state(proc.pid(), wid, visible ? graphics::WindowState::Normal : graphics::WindowState::Hidden);
        if (win->surface) {
            win->surface->set_visible(visible);
        }
        if (compositor_) {
            compositor_->invalidate_rect(win->frame);
        }
        return record_success(0);
    }

    case kSysWiniosClipboardGet: {
        if (!clipboard_) {
            return record_error(kErrnoENOSYS);
        }
        std::string text;
        bool ok = clipboard_->get_text(proc.pid(), text);
        if (!ok) {
            return record_error(kErrnoEPERM);
        }
        const std::uint64_t buf_va = arg0;
        const std::size_t max_len = static_cast<std::size_t>(arg1);
        if (buf_va == 0 || max_len == 0) {
            return record_success(text.size());
        }
        std::size_t copy_len = std::min(text.size(), max_len - 1);
        std::vector<char> out_buf(copy_len + 1, '\0');
        std::memcpy(out_buf.data(), text.data(), copy_len);
        auto vres = memory::GuestMemoryValidator::copyout(uas, out_buf.data(), buf_va, copy_len + 1, 1);
        if (vres != memory::ValidationResult::Success) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }
        return record_success(copy_len);
    }

    case kSysWiniosClipboardSet: {
        if (!clipboard_) {
            return record_error(kErrnoENOSYS);
        }
        const std::uint64_t text_va = arg0;
        const std::size_t text_len = static_cast<std::size_t>(arg1);
        if (text_len > runtime::SystemClipboard::kMaxClipboardBytes) {
            return record_error(kErrnoEINVAL);
        }
        std::string text;
        auto vres = memory::GuestMemoryValidator::copy_string(uas, text_va, text_len, text);
        if (vres != memory::ValidationResult::Success) {
            ++stats_.total_invalid_pointers;
            return record_error(kErrnoEFAULT);
        }
        bool ok = clipboard_->set_text(proc.pid(), text);
        if (!ok) {
            return record_error(kErrnoEPERM);
        }
        return record_success(0);
    }

    case kSysWiniosAppSupervisorNotify: {
        if (!lifecycle_mgr_) {
            return record_success(0);
        }
        std::uint32_t state_val = static_cast<std::uint32_t>(arg0);
        AppState new_state = AppState::Foreground;
        if (state_val == 2) new_state = AppState::Foreground;
        else if (state_val == 3) new_state = AppState::Background;
        else if (state_val == 4) new_state = AppState::Suspended;
        else if (state_val == 6) new_state = AppState::Terminating;
        else if (state_val == 7) new_state = AppState::Terminated;

        bool ok = lifecycle_mgr_->transition_to(proc.pid(), new_state);
        return record_success(ok ? 0 : 1);
    }

    case kSysWiniosConfigGet: {
        if (!config_service_) return record_error(kErrnoENOSYS);
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::ConfigAccess)) {
            return record_error(kErrnoEPERM);
        }
        winios_config_req_t req{};
        if (!uas.copyout(reinterpret_cast<const void*>(arg0), &req, sizeof(req))) {
            return record_error(kErrnoEFAULT);
        }
        std::string key;
        if (!read_user_string(uas, req.key_ptr, key, req.key_len > 0 ? req.key_len : 64)) {
            return record_error(kErrnoEFAULT);
        }
        std::string bundle = cap_mgr_ ? cap_mgr_->get_bundle_id(proc.pid()) : "";
        if (bundle.empty()) bundle = "com.winios.app";

        if (req.val_type == 0) { // String
            std::string val = config_service_->get_string(bundle, key, "");
            req.out_len = static_cast<uint32_t>(val.size());
            if (req.val_buf_ptr != 0 && req.val_buf_size > 0) {
                size_t to_copy = (val.size() < req.val_buf_size) ? val.size() : (req.val_buf_size - 1);
                uas.copyin(req.val_buf_ptr, val.data(), to_copy);
                char null_byte = 0;
                uas.copyin(req.val_buf_ptr + to_copy, &null_byte, 1);
            }
        } else if (req.val_type == 1) { // Int64
            int64_t val = config_service_->get_int(bundle, key, 0);
            req.out_len = sizeof(int64_t);
            if (req.val_buf_ptr != 0 && req.val_buf_size >= sizeof(int64_t)) {
                uas.copyin(req.val_buf_ptr, &val, sizeof(val));
            }
        } else if (req.val_type == 2) { // Double
            double val = config_service_->get_double(bundle, key, 0.0);
            req.out_len = sizeof(double);
            if (req.val_buf_ptr != 0 && req.val_buf_size >= sizeof(double)) {
                uas.copyin(req.val_buf_ptr, &val, sizeof(val));
            }
        } else if (req.val_type == 3) { // Bool
            bool val = config_service_->get_bool(bundle, key, false);
            uint32_t bval = val ? 1 : 0;
            req.out_len = sizeof(uint32_t);
            if (req.val_buf_ptr != 0 && req.val_buf_size >= sizeof(uint32_t)) {
                uas.copyin(req.val_buf_ptr, &bval, sizeof(bval));
            }
        }
        uas.copyin(arg0, &req, sizeof(req));
        return record_success(0);
    }

    case kSysWiniosConfigSet: {
        if (!config_service_) return record_error(kErrnoENOSYS);
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::ConfigAccess)) {
            return record_error(kErrnoEPERM);
        }
        winios_config_req_t req{};
        if (!uas.copyout(reinterpret_cast<const void*>(arg0), &req, sizeof(req))) {
            return record_error(kErrnoEFAULT);
        }
        std::string key;
        if (!read_user_string(uas, req.key_ptr, key, req.key_len > 0 ? req.key_len : 64)) {
            return record_error(kErrnoEFAULT);
        }
        std::string bundle = cap_mgr_ ? cap_mgr_->get_bundle_id(proc.pid()) : "";
        if (bundle.empty()) bundle = "com.winios.app";

        bool ok = false;
        if (req.val_type == 0) { // String
            std::string val;
            if (req.val_buf_ptr != 0 && read_user_string(uas, req.val_buf_ptr, val, req.val_buf_size > 0 ? req.val_buf_size : 1024)) {
                ok = config_service_->set_string(bundle, key, val);
            }
        } else if (req.val_type == 1) { // Int64
            int64_t val = 0;
            if (uas.copyout(reinterpret_cast<const void*>(req.val_buf_ptr), &val, sizeof(val))) {
                ok = config_service_->set_int(bundle, key, val);
            }
        } else if (req.val_type == 2) { // Double
            double val = 0.0;
            if (uas.copyout(reinterpret_cast<const void*>(req.val_buf_ptr), &val, sizeof(val))) {
                ok = config_service_->set_double(bundle, key, val);
            }
        } else if (req.val_type == 3) { // Bool
            uint32_t val = 0;
            if (uas.copyout(reinterpret_cast<const void*>(req.val_buf_ptr), &val, sizeof(val))) {
                ok = config_service_->set_bool(bundle, key, val != 0);
            }
        }
        return record_success(ok ? 0 : 1);
    }

    case kSysWiniosLogLeveled: {
        uint32_t lvl = static_cast<uint32_t>(arg0);
        std::string msg;
        if (!read_user_string(uas, arg1, msg, arg2 > 0 ? arg2 : 1024)) {
            return record_error(kErrnoEFAULT);
        }
        std::string bundle = cap_mgr_ ? cap_mgr_->get_bundle_id(proc.pid()) : "";
        if (diag_service_) {
            diag_service_->log(bundle, proc.pid(), static_cast<services::LogLevel>(lvl), msg);
        }
        console_.write_bytes(msg.data(), msg.size());
        console_.write_bytes("\n", 1);
        return record_success(0);
    }

    case kSysWiniosDiagSummary: {
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::DiagnosticsAccess)) {
            return record_error(kErrnoEPERM);
        }
        winios_diag_summary_t sum{};
        if (diag_service_) {
            auto s = diag_service_->get_summary();
            sum.active_processes = static_cast<uint32_t>(s.active_processes);
            sum.installed_apps = static_cast<uint32_t>(s.installed_apps);
            sum.open_windows = static_cast<uint32_t>(s.open_windows);
            sum.ipc_ports = static_cast<uint32_t>(s.ipc_ports);
            sum.total_crashes = static_cast<uint32_t>(s.total_crashes);
            sum.total_security_checks = static_cast<uint32_t>(s.security_stats.total_checks);
            sum.total_security_denials = static_cast<uint32_t>(s.security_stats.total_denials);
        }
        if (!uas.copyin(arg0, &sum, sizeof(sum))) {
            return record_error(kErrnoEFAULT);
        }
        return record_success(0);
    }

    case kSysWiniosIpcCreatePortV2: {
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::IpcAccess)) {
            return record_error(kErrnoEPERM);
        }
        std::string port_name;
        if (arg0 != 0) {
            read_user_string(uas, arg0, port_name, 64);
        }
        uint64_t handle = runtime::GuestIpcRouter::Get().CreatePortV2(proc.pid(), port_name);
        return record_success(handle);
    }

    case kSysWiniosIpcSendV2: {
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::IpcAccess)) {
            return record_error(kErrnoEPERM);
        }
        winios_ipc_msg_v2_t desc{};
        if (!uas.copyout(reinterpret_cast<const void*>(arg0), &desc, sizeof(desc))) {
            return record_error(kErrnoEFAULT);
        }
        if (desc.data_size > runtime::kMaxIpcMessageSize) {
            return record_error(kErrnoEINVAL);
        }
        std::vector<uint8_t> payload(desc.data_size);
        if (desc.data_size > 0 && desc.data_ptr != 0) {
            if (!uas.copyout(reinterpret_cast<const void*>(desc.data_ptr), payload.data(), desc.data_size)) {
                return record_error(kErrnoEFAULT);
            }
        }
        bool ok = runtime::GuestIpcRouter::Get().SendV2(proc.pid(), desc.target_port, payload.data(), payload.size(), desc.message_id);
        return record_success(ok ? 0 : 1);
    }

    case kSysWiniosIpcRecvV2: {
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::IpcAccess)) {
            return record_error(kErrnoEPERM);
        }
        uint64_t port_handle = arg0;
        uint64_t buf_ptr = arg1;
        uint32_t max_len = static_cast<uint32_t>(arg2);
        uint64_t out_desc_ptr = arg3;

        runtime::IpcMessage msg{};
        bool ok = runtime::GuestIpcRouter::Get().ReceiveV2(proc.pid(), port_handle, &msg);
        if (!ok) {
            return record_success(0); // Queue empty
        }
        uint32_t to_write = (msg.data_size < max_len) ? msg.data_size : max_len;
        if (to_write > 0 && buf_ptr != 0) {
            uas.copyin(buf_ptr, msg.data.data(), to_write);
        }
        if (out_desc_ptr != 0) {
            winios_ipc_msg_v2_t out_desc{};
            out_desc.target_port = msg.target_port;
            out_desc.sender_pid = msg.sender_pid;
            out_desc.message_id = msg.message_id;
            out_desc.data_size = to_write;
            uas.copyin(out_desc_ptr, &out_desc, sizeof(out_desc));
        }
        return record_success(to_write);
    }

    case kSysWiniosIpcLookupPort: {
        std::string name;
        if (!read_user_string(uas, arg0, name, 64)) {
            return record_error(kErrnoEFAULT);
        }
        uint64_t handle = runtime::GuestIpcRouter::Get().LookupPortName(name);
        return record_success(handle);
    }

    case kSysWiniosIpcRegisterPort: {
        uint64_t port_handle = arg0;
        std::string name;
        if (!read_user_string(uas, arg1, name, 64)) {
            return record_error(kErrnoEFAULT);
        }
        bool ok = runtime::GuestIpcRouter::Get().RegisterPortName(name, port_handle, proc.pid());
        return record_success(ok ? 0 : 1);
    }

    // -------------------------------------------------------------------------
    // Category C ABI v1.9 Extensions (Campaign 225)
    // -------------------------------------------------------------------------
    case kSysWiniosDeviceEnum: {
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::DeviceEnumerate)) {
            return record_error(kErrnoEPERM);
        }
        if (arg0 == 0) {
            return record_error(kErrnoEFAULT);
        }
        auto& reg = devices::DeviceRegistry::get();
        std::vector<devices::DeviceInfo> dev_list;
        if (arg2 == 0) {
            dev_list = reg.enumerate_all();
        } else {
            dev_list = reg.enumerate_by_class(static_cast<devices::DeviceClass>(arg2));
        }
        const uint32_t count_to_copy = static_cast<uint32_t>(
            std::min<std::size_t>(dev_list.size(), static_cast<std::size_t>(arg1)));
        for (uint32_t i = 0; i < count_to_copy; ++i) {
            const uint64_t entry_va = arg0 + (i * sizeof(devices::DeviceInfo));
            if (!uas.copyin(entry_va, &dev_list[i], sizeof(devices::DeviceInfo))) {
                return record_error(kErrnoEFAULT);
            }
        }
        if (arg3 != 0) {
            uas.copyin(arg3, &count_to_copy, sizeof(count_to_copy));
        }
        return record_success(count_to_copy);
    }

    case kSysWiniosDeviceGetInfo: {
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::DeviceEnumerate)) {
            return record_error(kErrnoEPERM);
        }
        if (arg1 == 0) {
            return record_error(kErrnoEFAULT);
        }
        auto dev = devices::DeviceRegistry::get().get_by_handle(arg0);
        if (!dev) {
            dev = devices::DeviceRegistry::get().get_device(static_cast<uint32_t>(arg0));
        }
        if (!dev) {
            return record_error(kErrnoENOENT);
        }
        devices::DeviceInfo info = dev->get_info();
        if (!uas.copyin(arg1, &info, sizeof(info))) {
            return record_error(kErrnoEFAULT);
        }
        return record_success(0);
    }

    case kSysWiniosDeviceOpen: {
        auto dev = devices::DeviceRegistry::get().get_device(static_cast<uint32_t>(arg0));
        if (!dev) {
            return record_error(kErrnoENOENT);
        }
        if (cap_mgr_ && dev->required_capability() != 0) {
            const uint32_t pmask = cap_mgr_->get_mask(proc.pid());
            if ((pmask & dev->required_capability()) != dev->required_capability()) {
                return record_error(kErrnoEPERM);
            }
        }
        return record_success(dev->handle());
    }

    case kSysWiniosDeviceClose: {
        if (!devices::DeviceRegistry::get().is_handle_valid(arg0)) {
            return record_error(kErrnoEBADF);
        }
        return record_success(0);
    }

    case kSysWiniosDisplayGetConfig: {
        if (arg0 == 0) {
            return record_error(kErrnoEFAULT);
        }
        auto dev = devices::DeviceRegistry::get().get_device(1);
        auto disp = std::dynamic_pointer_cast<devices::VirtualDisplayService>(dev);
        devices::DisplayConfigData cfg = disp ? disp->get_config() : devices::DisplayConfigData{};
        if (!uas.copyin(arg0, &cfg, sizeof(cfg))) {
            return record_error(kErrnoEFAULT);
        }
        return record_success(0);
    }

    case kSysWiniosDisplaySetConfig: {
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::DisplayWindow)) {
            return record_error(kErrnoEPERM);
        }
        if (arg0 == 0) {
            return record_error(kErrnoEFAULT);
        }
        devices::DisplayConfigData cfg{};
        if (!uas.copyout(reinterpret_cast<const void*>(arg0), &cfg, sizeof(cfg))) {
            return record_error(kErrnoEFAULT);
        }
        auto dev = devices::DeviceRegistry::get().get_device(1);
        auto disp = std::dynamic_pointer_cast<devices::VirtualDisplayService>(dev);
        bool ok = disp ? disp->set_config(cfg) : false;
        return record_success(ok ? 0 : 1);
    }

    case kSysWiniosAudioSubmit: {
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::AudioOutput)) {
            return record_error(kErrnoEPERM);
        }
        if (arg0 == 0 || arg1 == 0 || arg1 > devices::VirtualAudioService::kMaxSubmissionBytes) {
            return record_error(kErrnoEINVAL);
        }
        std::vector<uint8_t> pcm(arg1);
        if (!uas.copyout(reinterpret_cast<const void*>(arg0), pcm.data(), arg1)) {
            return record_error(kErrnoEFAULT);
        }
        auto dev = devices::DeviceRegistry::get().get_device(devices::VirtualAudioService::kDefaultAudioDeviceId);
        auto audio = std::dynamic_pointer_cast<devices::VirtualAudioService>(dev);
        bool ok = audio ? audio->submit_audio(pcm.data(), arg1) : false;
        return record_success(ok ? 0 : 1);
    }

    case kSysWiniosAudioGetStatus: {
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::AudioOutput)) {
            return record_error(kErrnoEPERM);
        }
        if (arg0 == 0) {
            return record_error(kErrnoEFAULT);
        }
        auto dev = devices::DeviceRegistry::get().get_device(devices::VirtualAudioService::kDefaultAudioDeviceId);
        auto audio = std::dynamic_pointer_cast<devices::VirtualAudioService>(dev);
        devices::AudioStatusData st = audio ? audio->get_status() : devices::AudioStatusData{};
        if (!uas.copyin(arg0, &st, sizeof(st))) {
            return record_error(kErrnoEFAULT);
        }
        return record_success(0);
    }

    case kSysWiniosSensorSample: {
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::SensorAccess)) {
            return record_error(kErrnoEPERM);
        }
        if (arg1 == 0) {
            return record_error(kErrnoEFAULT);
        }
        auto dev = devices::DeviceRegistry::get().get_device(devices::VirtualSensorService::kDefaultSensorDeviceId);
        auto sensor = std::dynamic_pointer_cast<devices::VirtualSensorService>(dev);
        devices::SensorSampleData sample{};
        bool ok = sensor ? sensor->get_latest_sample(static_cast<devices::SensorType>(arg0), &sample) : false;
        if (!ok) {
            return record_error(kErrnoEINVAL);
        }
        if (!uas.copyin(arg1, &sample, sizeof(sample))) {
            return record_error(kErrnoEFAULT);
        }
        return record_success(0);
    }

    case kSysWiniosSensorSetEnabled: {
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::SensorAccess)) {
            return record_error(kErrnoEPERM);
        }
        auto dev = devices::DeviceRegistry::get().get_device(devices::VirtualSensorService::kDefaultSensorDeviceId);
        auto sensor = std::dynamic_pointer_cast<devices::VirtualSensorService>(dev);
        bool ok = sensor ? sensor->set_enabled(static_cast<devices::SensorType>(arg0), arg1 != 0) : false;
        return record_success(ok ? 0 : 1);
    }

    case kSysWiniosNetSend: {
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::NetworkAccess)) {
            return record_error(kErrnoEPERM);
        }
        if (arg0 == 0 || arg1 == 0 || arg1 > devices::VirtualNetworkService::kMaxMTU) {
            return record_error(kErrnoEINVAL);
        }
        std::vector<uint8_t> pkt(arg1);
        if (!uas.copyout(reinterpret_cast<const void*>(arg0), pkt.data(), arg1)) {
            return record_error(kErrnoEFAULT);
        }
        auto dev = devices::DeviceRegistry::get().get_device(devices::VirtualNetworkService::kDefaultNetworkDeviceId);
        auto net = std::dynamic_pointer_cast<devices::VirtualNetworkService>(dev);
        bool ok = net ? net->send_packet(pkt.data(), arg1) : false;
        return record_success(ok ? 0 : 1);
    }

    case kSysWiniosNetRecv: {
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::NetworkAccess)) {
            return record_error(kErrnoEPERM);
        }
        if (arg0 == 0 || arg1 == 0) {
            return record_error(kErrnoEINVAL);
        }
        auto dev = devices::DeviceRegistry::get().get_device(devices::VirtualNetworkService::kDefaultNetworkDeviceId);
        auto net = std::dynamic_pointer_cast<devices::VirtualNetworkService>(dev);
        std::vector<uint8_t> tmp(arg1);
        size_t recvd = 0;
        bool ok = net ? net->receive_packet(tmp.data(), arg1, &recvd) : false;
        if (!ok) {
            if (arg2 != 0) {
                size_t zero = 0;
                uas.copyin(arg2, &zero, sizeof(zero));
            }
            return record_success(0);
        }
        if (!uas.copyin(arg0, tmp.data(), recvd)) {
            return record_error(kErrnoEFAULT);
        }
        if (arg2 != 0) {
            uas.copyin(arg2, &recvd, sizeof(recvd));
        }
        return record_success(recvd);
    }

    case kSysWiniosTimeGet: {
        if (arg0 == 0) {
            return record_error(kErrnoEFAULT);
        }
        auto dev = devices::DeviceRegistry::get().get_device(devices::VirtualClockService::kDefaultClockDeviceId);
        auto clock = std::dynamic_pointer_cast<devices::VirtualClockService>(dev);
        devices::TimeSpecData spec = clock ? clock->get_time_spec() : devices::TimeSpecData{};
        if (!uas.copyin(arg0, &spec, sizeof(spec))) {
            return record_error(kErrnoEFAULT);
        }
        return record_success(0);
    }

    case kSysWiniosNotificationPost: {
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::NotificationPost)) {
            return record_error(kErrnoEPERM);
        }
        std::string title, body, cat;
        if (!read_user_string(uas, arg0, title, 64) || !read_user_string(uas, arg1, body, 128)) {
            return record_error(kErrnoEFAULT);
        }
        if (arg2 != 0) {
            read_user_string(uas, arg2, cat, 32);
        }
        auto dev = devices::DeviceRegistry::get().get_device(devices::VirtualNotificationService::kDefaultNotificationDeviceId);
        auto notif = std::dynamic_pointer_cast<devices::VirtualNotificationService>(dev);
        uint32_t nid = notif ? notif->post_notification(proc.pid(), title.c_str(), body.c_str(), cat.c_str(), static_cast<uint32_t>(arg3)) : 0;
        return record_success(nid);
    }

    case kSysWiniosDeviceDiagSummary: {
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::DiagnosticsAccess)) {
            return record_error(kErrnoEPERM);
        }
        if (arg0 == 0) {
            return record_error(kErrnoEFAULT);
        }
        services::DeviceDiagnosticsSummary sum{};
        if (diag_service_) {
            sum = diag_service_->get_device_summary();
        }
        if (!uas.copyin(arg0, &sum, sizeof(sum))) {
            return record_error(kErrnoEFAULT);
        }
        return record_success(0);
    }

    // -------------------------------------------------------------------------
    // Category C ABI v2.0 Extensions (Syscalls 561..570)
    // -------------------------------------------------------------------------
    case kSysWiniosWindowCreateV2: {
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::DisplayWindow)) {
            return record_error(kErrnoEPERM);
        }
        std::string title;
        if (!read_user_string(uas, arg0, title, 128)) {
            return record_error(kErrnoEFAULT);
        }
        const uint32_t x = static_cast<uint32_t>(arg1);
        const uint32_t y = static_cast<uint32_t>(arg2);
        const uint32_t w = static_cast<uint32_t>(arg3);
        const uint32_t h = static_cast<uint32_t>(arg4);

        if (w == 0 || h == 0 || w > 3840 || h > 3840) {
            return record_error(kErrnoEINVAL);
        }

        std::shared_ptr<graphics::DisplaySurface> surface = nullptr;
        if (surface_mgr_) {
            auto code = surface_mgr_->create_surface(
                proc.pid(), w, h, graphics::PixelFormat::B8G8R8A8_UNORM, surface);
            if (code != graphics::SurfaceResultCode::Success || !surface) {
                return record_error(kErrnoENOMEM);
            }
        }

        if (!window_mgr_) {
            return record_error(kErrnoENODEV);
        }

        uint64_t handle = window_mgr_->create_window_v2(
            proc.pid(), title, graphics::Rect{x, y, w, h}, surface);
        return record_success(handle);
    }

    case kSysWiniosWindowDestroyV2: {
        if (!window_mgr_) {
            return record_error(kErrnoENODEV);
        }
        if (!window_mgr_->is_handle_valid(arg0)) {
            return record_error(kErrnoEBADF);
        }
        bool ok = window_mgr_->destroy_window_by_handle(proc.pid(), arg0);
        if (!ok) {
            return record_error(kErrnoEPERM);
        }
        return record_success(0);
    }

    case kSysWiniosWindowSetState: {
        if (!window_mgr_) {
            return record_error(kErrnoENODEV);
        }
        if (!window_mgr_->is_handle_valid(arg0)) {
            return record_error(kErrnoEBADF);
        }
        const uint32_t target_state = static_cast<uint32_t>(arg1);
        bool ok = false;
        if (target_state == 1) { // Minimize
            ok = window_mgr_->minimize_window(proc.pid(), arg0);
        } else if (target_state == 4) { // Maximize
            ok = window_mgr_->maximize_window(proc.pid(), arg0, graphics::Rect{0, 0, 1290, 2796});
        } else if (target_state == 0) { // Restore / Normal
            ok = window_mgr_->restore_window(proc.pid(), arg0);
        } else if (target_state == 2) { // Hide
            auto win = window_mgr_->get_window_by_handle(arg0);
            if (win && win->can_access(proc.pid())) {
                ok = window_mgr_->set_window_state(proc.pid(), win->id, graphics::WindowState::Hidden);
            }
        } else {
            return record_error(kErrnoEINVAL);
        }
        return record_success(ok ? 0 : 1);
    }

    case kSysWiniosWindowGetState: {
        if (!window_mgr_) {
            return record_error(kErrnoENODEV);
        }
        if (arg1 == 0) {
            return record_error(kErrnoEFAULT);
        }
        auto win = window_mgr_->get_window_by_handle(arg0);
        if (!win || !win->can_access(proc.pid())) {
            return record_error(kErrnoEBADF);
        }
        winios_window_state_data_t st{};
        st.handle = win->handle();
        st.owner_pid = win->owner_pid;
        st.state = static_cast<uint32_t>(win->state);
        st.z_order = win->z_order;
        st.is_focused = win->is_focused ? 1 : 0;
        st.frame.x = win->frame.x;
        st.frame.y = win->frame.y;
        st.frame.width = win->frame.width;
        st.frame.height = win->frame.height;
        auto cf = win->client_frame();
        st.client_frame.x = cf.x;
        st.client_frame.y = cf.y;
        st.client_frame.width = cf.width;
        st.client_frame.height = cf.height;

        if (!uas.copyin(arg1, &st, sizeof(st))) {
            return record_error(kErrnoEFAULT);
        }
        return record_success(0);
    }

    case kSysWiniosWindowInvalidateRect: {
        if (!window_mgr_) {
            return record_error(kErrnoENODEV);
        }
        auto win = window_mgr_->get_window_by_handle(arg0);
        if (!win || !win->can_access(proc.pid())) {
            return record_error(kErrnoEBADF);
        }
        graphics::Rect dmg = win->client_frame();
        if (arg1 != 0) {
            winios_rect_t r{};
            if (!uas.copyout(reinterpret_cast<const void*>(arg1), &r, sizeof(r))) {
                return record_error(kErrnoEFAULT);
            }
            dmg = graphics::Rect{r.x, r.y, r.width, r.height};
        }
        window_mgr_->invalidate_window_rect(proc.pid(), arg0, dmg);
        return record_success(0);
    }

    case kSysWiniosEventPollV2: {
        if (!window_mgr_) {
            return record_error(kErrnoENODEV);
        }
        if (arg1 == 0) {
            return record_error(kErrnoEFAULT);
        }
        graphics::DesktopEvent dev{};
        bool got = false;
        if (arg0 != 0) {
            got = window_mgr_->poll_event(proc.pid(), arg0, dev);
        } else {
            auto proc_wins = window_mgr_->get_windows_for_process(proc.pid());
            for (const auto& w : proc_wins) {
                if (w->event_queue.pop(dev)) {
                    got = true;
                    break;
                }
            }
        }

        if (!got) {
            return record_success(0);
        }

        winios_desktop_event_t out_ev{};
        out_ev.version = WINIOS_CATEGORY_C_ABI_VERSION_2_0;
        out_ev.size = sizeof(winios_desktop_event_t);
        out_ev.event_type = dev.event_type;
        out_ev.flags = dev.flags;
        out_ev.timestamp_us = dev.timestamp_us;
        out_ev.window_handle = dev.window_handle;
        out_ev.x = dev.x;
        out_ev.y = dev.y;
        out_ev.screen_x = dev.screen_x;
        out_ev.screen_y = dev.screen_y;
        out_ev.button_or_key = dev.button_or_key;
        out_ev.wheel_delta = dev.wheel_delta;
        out_ev.modifiers = dev.modifiers;
        out_ev.payload_len = dev.payload_len;
        std::memcpy(out_ev.payload, dev.payload, sizeof(out_ev.payload));

        if (!uas.copyin(arg1, &out_ev, sizeof(out_ev))) {
            return record_error(kErrnoEFAULT);
        }
        return record_success(1);
    }

    case kSysWiniosEventWait: {
        if (!window_mgr_) {
            return record_error(kErrnoENODEV);
        }
        if (arg1 == 0) {
            return record_error(kErrnoEFAULT);
        }
        graphics::DesktopEvent dev{};
        bool got = false;
        if (arg0 != 0) {
            got = window_mgr_->poll_event(proc.pid(), arg0, dev);
        } else {
            auto proc_wins = window_mgr_->get_windows_for_process(proc.pid());
            for (const auto& w : proc_wins) {
                if (w->event_queue.pop(dev)) {
                    got = true;
                    break;
                }
            }
        }
        if (got) {
            winios_desktop_event_t out_ev{};
            out_ev.version = WINIOS_CATEGORY_C_ABI_VERSION_2_0;
            out_ev.size = sizeof(winios_desktop_event_t);
            out_ev.event_type = dev.event_type;
            out_ev.flags = dev.flags;
            out_ev.timestamp_us = dev.timestamp_us;
            out_ev.window_handle = dev.window_handle;
            out_ev.x = dev.x;
            out_ev.y = dev.y;
            out_ev.screen_x = dev.screen_x;
            out_ev.screen_y = dev.screen_y;
            out_ev.button_or_key = dev.button_or_key;
            out_ev.wheel_delta = dev.wheel_delta;
            out_ev.modifiers = dev.modifiers;
            out_ev.payload_len = dev.payload_len;
            std::memcpy(out_ev.payload, dev.payload, sizeof(out_ev.payload));
            if (!uas.copyin(arg1, &out_ev, sizeof(out_ev))) {
                return record_error(kErrnoEFAULT);
            }
            return record_success(1);
        }
        return record_success(0);
    }

    case kSysWiniosTextInputSubmit: {
        if (!window_mgr_) {
            return record_error(kErrnoENODEV);
        }
        if (arg1 == 0 || arg2 == 0) {
            return record_error(kErrnoEINVAL);
        }
        const std::size_t tlen = std::min<std::size_t>(arg2, 64);
        std::vector<char> buf(tlen + 1, 0);
        if (!uas.copyout(reinterpret_cast<const void*>(arg1), buf.data(), tlen)) {
            return record_error(kErrnoEFAULT);
        }
        graphics::DesktopEvent dev{};
        dev.event_type = static_cast<uint32_t>(graphics::DesktopEventType::TextInput);
        dev.window_handle = arg0;
        dev.payload_len = static_cast<uint32_t>(tlen);
        std::memcpy(dev.payload, buf.data(), tlen);
        dev.payload[tlen] = 0;
        if (arg0 != 0) {
            window_mgr_->post_event(arg0, dev);
        } else {
            window_mgr_->post_event_to_focused(dev);
        }
        return record_success(0);
    }

    case kSysWiniosClipboardGetV2: {
        if (!clipboard_) {
            return record_error(kErrnoENODEV);
        }
        if (arg0 == 0 || arg1 == 0) {
            return record_error(kErrnoEINVAL);
        }
        std::string clip_text;
        bool ok = clipboard_->get_text(proc.pid(), clip_text);
        if (!ok) {
            return record_error(kErrnoEPERM);
        }
        if (clip_text.empty()) {
            if (arg2 != 0) {
                size_t zero = 0;
                uas.copyin(arg2, &zero, sizeof(zero));
            }
            return record_success(0);
        }
        const std::size_t copy_len = std::min<std::size_t>(clip_text.size(), arg1);
        if (!uas.copyin(arg0, clip_text.data(), copy_len)) {
            return record_error(kErrnoEFAULT);
        }
        if (arg2 != 0) {
            uas.copyin(arg2, &copy_len, sizeof(copy_len));
        }
        return record_success(copy_len);
    }

    case kSysWiniosClipboardSetV2: {
        if (!clipboard_) {
            return record_error(kErrnoENODEV);
        }
        if (arg0 == 0 || arg1 == 0 || arg1 > 65536) {
            return record_error(kErrnoEINVAL);
        }
        std::vector<char> buf(arg1 + 1, 0);
        if (!uas.copyout(reinterpret_cast<const void*>(arg0), buf.data(), arg1)) {
            return record_error(kErrnoEFAULT);
        }
        std::string clip_str(buf.data(), arg1);
        bool ok = clipboard_->set_text(proc.pid(), clip_str);
        if (!ok) {
            return record_error(kErrnoEPERM);
        }
        return record_success(0);
    }

    // -------------------------------------------------------------------------
    // Category C ABI v2.1 Document Services (Syscalls 571..580)
    // -------------------------------------------------------------------------
    case kSysWiniosDocOpenPicker: {
        if (!file_picker_svc_) {
            return record_error(kErrnoENODEV);
        }
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::DocumentRead)) {
            return record_error(kErrnoEPERM);
        }
        std::string filter_ext;
        if (arg1 != 0) {
            read_user_string(uas, arg1, filter_ext, 32);
        }
        std::uint32_t req_id = 0;
        if (!file_picker_svc_->request_open(proc.pid(), arg0, filter_ext, req_id)) {
            return record_error(kErrnoEINVAL);
        }
        if (arg2 != 0) {
            if (!uas.copyin(arg2, &req_id, sizeof(req_id))) {
                return record_error(kErrnoEFAULT);
            }
        }
        return record_success(req_id);
    }

    case kSysWiniosDocSavePicker: {
        if (!file_picker_svc_) {
            return record_error(kErrnoENODEV);
        }
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::DocumentWrite)) {
            return record_error(kErrnoEPERM);
        }
        std::string default_name;
        if (arg1 != 0) {
            read_user_string(uas, arg1, default_name, 64);
        }
        std::string filter_ext;
        if (arg2 != 0) {
            read_user_string(uas, arg2, filter_ext, 32);
        }
        std::uint32_t req_id = 0;
        if (!file_picker_svc_->request_save(proc.pid(), arg0, default_name, filter_ext, req_id)) {
            return record_error(kErrnoEINVAL);
        }
        if (arg3 != 0) {
            if (!uas.copyin(arg3, &req_id, sizeof(req_id))) {
                return record_error(kErrnoEFAULT);
            }
        }
        return record_success(req_id);
    }

    case kSysWiniosDocGetMetadata: {
        if (!doc_registry_) {
            return record_error(kErrnoENODEV);
        }
        if (arg0 == 0 || arg1 == 0) {
            return record_error(kErrnoEINVAL);
        }
        if (!doc_registry_->check_access(arg0, proc.pid(), documents::DocumentAccessMode::Read)) {
            return record_error(kErrnoEPERM);
        }
        auto meta = doc_registry_->get_metadata(arg0);
        if (!meta) {
            return record_error(kErrnoENOENT);
        }
        winios_doc_metadata_t out_meta{};
        out_meta.doc_handle = meta->handle;
        out_meta.owner_pid = meta->owner_pid;
        out_meta.is_read_only = meta->is_read_only ? 1 : 0;
        out_meta.file_size = meta->file_size;
        out_meta.created_time_us = meta->created_time_us;
        out_meta.modified_time_us = meta->modified_time_us;
        strncpy_s(out_meta.display_name, sizeof(out_meta.display_name), meta->display_name.c_str(), _TRUNCATE);
        strncpy_s(out_meta.canonical_path, sizeof(out_meta.canonical_path), meta->canonical_path.c_str(), _TRUNCATE);
        strncpy_s(out_meta.mime_type, sizeof(out_meta.mime_type), meta->mime_type.c_str(), _TRUNCATE);

        if (!uas.copyin(arg1, &out_meta, sizeof(out_meta))) {
            return record_error(kErrnoEFAULT);
        }
        return record_success(0);
    }

    case kSysWiniosDocStreamOpen: {
        if (!doc_stream_mgr_) {
            return record_error(kErrnoENODEV);
        }
        if (arg0 == 0 || arg2 == 0) {
            return record_error(kErrnoEINVAL);
        }
        auto mode = static_cast<documents::DocumentAccessMode>(arg1);
        documents::StreamHandle stream_h = doc_stream_mgr_->open_stream(arg0, proc.pid(), mode);
        if (stream_h == documents::kInvalidStreamHandle) {
            return record_error(kErrnoEPERM);
        }
        if (!uas.copyin(arg2, &stream_h, sizeof(stream_h))) {
            doc_stream_mgr_->close_stream(stream_h, proc.pid());
            return record_error(kErrnoEFAULT);
        }
        return record_success(0);
    }

    case kSysWiniosDocStreamRead: {
        if (!doc_stream_mgr_) {
            return record_error(kErrnoENODEV);
        }
        if (arg0 == 0 || arg1 == 0 || arg2 == 0) {
            return record_error(kErrnoEINVAL);
        }
        if (arg2 > documents::kMaxDocumentSizeBytes) {
            return record_error(kErrnoEINVAL);
        }
        std::vector<std::uint8_t> tmp_buf(arg2);
        std::size_t bytes_read = 0;
        if (!doc_stream_mgr_->read_stream(arg0, proc.pid(), tmp_buf.data(), arg2, bytes_read)) {
            return record_error(kErrnoEIO);
        }
        if (bytes_read > 0) {
            if (!uas.copyin(arg1, tmp_buf.data(), bytes_read)) {
                return record_error(kErrnoEFAULT);
            }
        }
        if (arg3 != 0) {
            std::uint64_t bread64 = bytes_read;
            uas.copyin(arg3, &bread64, sizeof(bread64));
        }
        return record_success(bytes_read);
    }

    case kSysWiniosDocStreamWrite: {
        if (!doc_stream_mgr_) {
            return record_error(kErrnoENODEV);
        }
        if (arg0 == 0 || arg1 == 0 || arg2 == 0) {
            return record_error(kErrnoEINVAL);
        }
        if (arg2 > documents::kMaxDocumentSizeBytes) {
            return record_error(kErrnoEINVAL);
        }
        std::vector<std::uint8_t> tmp_buf(arg2);
        if (!uas.copyout(reinterpret_cast<const void*>(arg1), tmp_buf.data(), arg2)) {
            return record_error(kErrnoEFAULT);
        }
        std::size_t bytes_written = 0;
        if (!doc_stream_mgr_->write_stream(arg0, proc.pid(), tmp_buf.data(), arg2, bytes_written)) {
            return record_error(kErrnoEIO);
        }
        if (arg3 != 0) {
            std::uint64_t bwritten64 = bytes_written;
            uas.copyin(arg3, &bwritten64, sizeof(bwritten64));
        }
        return record_success(bytes_written);
    }

    case kSysWiniosDocStreamSeek: {
        if (!doc_stream_mgr_) {
            return record_error(kErrnoENODEV);
        }
        if (arg0 == 0) {
            return record_error(kErrnoEINVAL);
        }
        std::uint64_t new_cursor = 0;
        if (!doc_stream_mgr_->seek_stream(arg0, proc.pid(), arg1, new_cursor)) {
            return record_error(kErrnoEINVAL);
        }
        if (arg2 != 0) {
            uas.copyin(arg2, &new_cursor, sizeof(new_cursor));
        }
        return record_success(0);
    }

    case kSysWiniosDocStreamClose: {
        if (!doc_stream_mgr_) {
            return record_error(kErrnoENODEV);
        }
        if (arg0 == 0) {
            return record_error(kErrnoEINVAL);
        }
        if (!doc_stream_mgr_->close_stream(arg0, proc.pid())) {
            return record_error(kErrnoEBADF);
        }
        return record_success(0);
    }

    case kSysWiniosDocGrantAccess: {
        if (!doc_registry_) {
            return record_error(kErrnoENODEV);
        }
        if (arg0 == 0 || arg3 == 0) {
            return record_error(kErrnoEINVAL);
        }
        auto mode = static_cast<documents::DocumentAccessMode>(arg2);
        auto grant = doc_registry_->create_grant(arg0, proc.pid(), static_cast<std::uint32_t>(arg1), "", mode, 0);
        if (grant.grant_token == 0) {
            return record_error(kErrnoEPERM);
        }
        if (!uas.copyin(arg3, &grant.grant_token, sizeof(grant.grant_token))) {
            return record_error(kErrnoEFAULT);
        }
        return record_success(0);
    }

    case kSysWiniosDocGetRecent: {
        if (!recent_doc_mgr_) {
            return record_error(kErrnoENODEV);
        }
        if (arg0 == 0 || arg1 == 0) {
            return record_error(kErrnoEINVAL);
        }
        std::string bid = cap_mgr_ ? cap_mgr_->get_bundle_id(proc.pid()) : "";
        if (bid.empty()) bid = proc.name();
        auto recents = recent_doc_mgr_->get_recent(bid);
        const std::size_t count = std::min<std::size_t>(recents.size(), arg1);
        std::vector<winios_recent_doc_t> out_list(count);
        for (std::size_t i = 0; i < count; ++i) {
            out_list[i].doc_handle = recents[i].doc_handle;
            out_list[i].last_opened_time_us = recents[i].last_opened_time_us;
            strncpy_s(out_list[i].display_name, sizeof(out_list[i].display_name), recents[i].display_name.c_str(), _TRUNCATE);
            strncpy_s(out_list[i].canonical_path, sizeof(out_list[i].canonical_path), recents[i].canonical_path.c_str(), _TRUNCATE);
        }
        if (count > 0) {
            if (!uas.copyin(arg0, out_list.data(), count * sizeof(winios_recent_doc_t))) {
                return record_error(kErrnoEFAULT);
            }
        }
        if (arg2 != 0) {
            std::uint32_t cnt32 = static_cast<std::uint32_t>(count);
            uas.copyin(arg2, &cnt32, sizeof(cnt32));
        }
        return record_success(count);
    }

    // -------------------------------------------------------------------------
    // Category C ABI v2.2 Settings, Multitasking & Accessibility (Syscalls 581..590)
    // -------------------------------------------------------------------------
    case kSysWiniosSettingsGet: {
        if (arg0 == 0) {
            return record_error(kErrnoEINVAL);
        }
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::SettingsRead)) {
            return record_error(kErrnoEPERM);
        }
        winios_settings_snapshot_t snap{};
        if (state_mgr_) {
            auto s = state_mgr_->get_settings();
            snap.theme_mode = static_cast<std::uint32_t>(s.appearance.theme);
            snap.accent_color = static_cast<std::uint32_t>(s.appearance.accent);
            snap.display_width = s.display.width;
            snap.display_height = s.display.height;
            snap.display_scale = static_cast<std::uint32_t>(s.display.scale);
            snap.display_orientation = static_cast<std::uint32_t>(s.display.orientation);
            snap.master_volume = s.audio.master_volume;
            snap.is_muted = s.audio.is_muted ? 1 : 0;
            snap.low_power_mode = s.power.low_power_mode ? 1 : 0;
            snap.text_scale_percent = s.accessibility.text_scale_percent;
            snap.high_contrast = s.accessibility.high_contrast ? 1 : 0;
            snap.network_enabled = s.network.adapter_enabled ? 1 : 0;
        } else {
            snap.theme_mode = 1; // Dark
            snap.accent_color = 0; // Blue
            snap.display_width = 800;
            snap.display_height = 600;
            snap.display_scale = 100;
            snap.display_orientation = 0;
            snap.master_volume = 80;
            snap.is_muted = 0;
            snap.low_power_mode = 0;
            snap.text_scale_percent = 100;
            snap.high_contrast = 0;
            snap.network_enabled = 1;
        }
        if (!uas.copyin(arg0, &snap, sizeof(snap))) {
            return record_error(kErrnoEFAULT);
        }
        return record_success(0);
    }

    case kSysWiniosSettingsSet: {
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::SettingsWrite)) {
            return record_error(kErrnoEPERM);
        }
        if (!state_mgr_) {
            return record_error(kErrnoENODEV);
        }
        const std::uint32_t key = static_cast<std::uint32_t>(arg0);
        const std::uint64_t val = arg1;
        bool ok = false;
        switch (key) {
            case 1: // Theme
                if (val <= 2) ok = state_mgr_->set_theme(static_cast<settings::ThemeMode>(val));
                break;
            case 2: // Accent
                if (val <= 7) ok = state_mgr_->set_accent(static_cast<settings::AccentColor>(val));
                break;
            case 3: // Display Scale
                if (val == 100 || val == 125 || val == 150 || val == 200) {
                    ok = state_mgr_->set_display_scale(static_cast<settings::DisplayScale>(val));
                }
                break;
            case 4: // Display Orientation
                if (val <= 1) ok = state_mgr_->set_display_orientation(static_cast<settings::DisplayOrientation>(val));
                break;
            case 5: // Volume
                if (val <= 100) ok = state_mgr_->set_audio_volume(static_cast<std::uint32_t>(val));
                break;
            case 6: // Mute
                ok = state_mgr_->set_audio_muted(val != 0);
                break;
            case 7: // Low Power Mode
                ok = state_mgr_->set_low_power_mode(val != 0);
                break;
            case 8: // Network Adapter
                ok = state_mgr_->set_network_adapter_enabled(val != 0);
                break;
            case 9: // Text Scale
                if (val >= 100 && val <= 200) ok = state_mgr_->set_text_scale(static_cast<std::uint32_t>(val));
                break;
            case 10: // High Contrast
                ok = state_mgr_->set_high_contrast(val != 0);
                break;
            default:
                return record_error(kErrnoEINVAL);
        }
        return ok ? record_success(0) : record_error(kErrnoEINVAL);
    }

    case kSysWiniosThemeGet: {
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::SettingsRead)) {
            return record_error(kErrnoEPERM);
        }
        if (!state_mgr_) {
            return record_error(kErrnoENODEV);
        }
        auto app = state_mgr_->get_appearance();
        if (arg0 != 0) {
            std::uint32_t th = static_cast<std::uint32_t>(app.theme);
            if (!uas.copyin(arg0, &th, sizeof(th))) return record_error(kErrnoEFAULT);
        }
        if (arg1 != 0) {
            std::uint32_t ac = static_cast<std::uint32_t>(app.accent);
            if (!uas.copyin(arg1, &ac, sizeof(ac))) return record_error(kErrnoEFAULT);
        }
        return record_success(0);
    }

    case kSysWiniosThemeSet: {
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::SettingsWrite)) {
            return record_error(kErrnoEPERM);
        }
        if (!state_mgr_) {
            return record_error(kErrnoENODEV);
        }
        if (arg0 > 2 || arg1 > 7) {
            return record_error(kErrnoEINVAL);
        }
        state_mgr_->set_theme(static_cast<settings::ThemeMode>(arg0));
        state_mgr_->set_accent(static_cast<settings::AccentColor>(arg1));
        return record_success(0);
    }

    case kSysWiniosControlCenterToggle: {
        if (!cc_svc_) {
            return record_error(kErrnoENODEV);
        }
        std::uint32_t action = static_cast<std::uint32_t>(arg0);
        switch (action) {
            case 0: cc_svc_->toggle(); break;
            case 1: cc_svc_->show(); break;
            case 2: cc_svc_->hide(); break;
            case 3: break; // Query only
            default: return record_error(kErrnoEINVAL);
        }
        return record_success(cc_svc_->is_visible() ? 1 : 0);
    }

    case kSysWiniosTaskEnumerate: {
        if (arg0 == 0 || arg1 == 0) {
            return record_error(kErrnoEINVAL);
        }
        std::vector<settings::TaskInfo> tasks;
        if (task_mgr_) {
            if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::TaskManage)) {
                // Without TaskManage capability, restrict enumeration to caller task only
                settings::TaskInfo self_t{};
                if (task_mgr_->get_task(proc.pid(), self_t)) {
                    tasks.push_back(self_t);
                } else {
                    self_t.pid = proc.pid();
                    self_t.name = proc.name();
                    self_t.state = settings::TaskState::Running;
                    self_t.is_foreground = true;
                    tasks.push_back(self_t);
                }
            } else {
                tasks = task_mgr_->enumerate_tasks();
            }
        } else {
            // Fallback: report calling process
            settings::TaskInfo self_t{};
            self_t.pid = proc.pid();
            self_t.name = proc.name();
            self_t.state = settings::TaskState::Running;
            self_t.is_foreground = true;
            tasks.push_back(self_t);
        }

        const std::size_t count = std::min<std::size_t>(tasks.size(), arg1);
        std::vector<winios_task_entry_t> out_list(count);
        for (std::size_t i = 0; i < count; ++i) {
            out_list[i].pid = tasks[i].pid;
            out_list[i].state = static_cast<std::uint32_t>(tasks[i].state);
            out_list[i].is_foreground = tasks[i].is_foreground ? 1 : 0;
            out_list[i].pad = 0;
            out_list[i].primary_window = tasks[i].primary_window;
            strncpy_s(out_list[i].name, sizeof(out_list[i].name), tasks[i].name.c_str(), _TRUNCATE);
            strncpy_s(out_list[i].bundle_id, sizeof(out_list[i].bundle_id), tasks[i].bundle_id.c_str(), _TRUNCATE);
        }
        if (count > 0) {
            if (!uas.copyin(arg0, out_list.data(), count * sizeof(winios_task_entry_t))) {
                return record_error(kErrnoEFAULT);
            }
        }
        if (arg2 != 0) {
            std::uint32_t cnt32 = static_cast<std::uint32_t>(count);
            if (!uas.copyin(arg2, &cnt32, sizeof(cnt32))) {
                return record_error(kErrnoEFAULT);
            }
        }
        return record_success(count);
    }

    case kSysWiniosTaskSwitch: {
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::TaskManage)) {
            return record_error(kErrnoEPERM);
        }
        if (!task_mgr_) {
            return record_error(kErrnoENODEV);
        }
        std::uint32_t target_pid = static_cast<std::uint32_t>(arg0);
        if (target_pid == 0) {
            // Cycle switcher
            task_mgr_->cycle_next();
            task_mgr_->activate_selected_task();
            return record_success(task_mgr_->get_selected_pid());
        }
        if (!task_mgr_->set_foreground_task(target_pid)) {
            return record_error(kErrnoESRCH);
        }
        return record_success(target_pid);
    }

    case kSysWiniosTaskTerminate: {
        std::uint32_t target_pid = static_cast<std::uint32_t>(arg0);
        bool graceful = (arg1 != 0);
        if (target_pid == 0) {
            return record_error(kErrnoEINVAL);
        }
        if (target_pid != static_cast<std::uint32_t>(proc.pid()) && cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::TaskManage)) {
            return record_error(kErrnoEPERM);
        }
        if (!task_mgr_) {
            return record_error(kErrnoENODEV);
        }
        if (!task_mgr_->terminate_task(target_pid, graceful)) {
            return record_error(kErrnoESRCH);
        }
        return record_success(0);
    }

    case kSysWiniosAccessibilityGet: {
        if (arg0 == 0) {
            return record_error(kErrnoEINVAL);
        }
        winios_accessibility_info_t a11y{};
        if (a11y_svc_) {
            a11y.text_scale_percent = a11y_svc_->get_text_scale_percent();
            a11y.high_contrast = a11y_svc_->is_high_contrast() ? 1 : 0;
            a11y.reduce_motion = a11y_svc_->is_reduce_motion() ? 1 : 0;
            a11y.focus_indicator_visible = a11y_svc_->is_focus_indicator_visible() ? 1 : 0;
            a11y.screen_reader_active = 0;
        } else if (state_mgr_) {
            auto a = state_mgr_->get_accessibility();
            a11y.text_scale_percent = a.text_scale_percent;
            a11y.high_contrast = a.high_contrast ? 1 : 0;
            a11y.reduce_motion = a.reduce_motion ? 1 : 0;
            a11y.focus_indicator_visible = a.focus_indicator_visible ? 1 : 0;
            a11y.screen_reader_active = 0;
        } else {
            a11y.text_scale_percent = 100;
            a11y.high_contrast = 0;
            a11y.reduce_motion = 0;
            a11y.focus_indicator_visible = 1;
            a11y.screen_reader_active = 0;
        }
        if (!uas.copyin(arg0, &a11y, sizeof(a11y))) {
            return record_error(kErrnoEFAULT);
        }
        return record_success(0);
    }

    case kSysWiniosAccessibilitySet: {
        if (cap_mgr_ && !cap_mgr_->check(proc.pid(), security::Capability::AccessibilityAccess)) {
            return record_error(kErrnoEPERM);
        }
        std::uint32_t text_scale = static_cast<std::uint32_t>(arg0);
        bool high_contrast = (arg1 != 0);
        if (text_scale != 0 && (text_scale < 100 || text_scale > 200)) {
            return record_error(kErrnoEINVAL);
        }
        if (a11y_svc_) {
            if (text_scale != 0) a11y_svc_->set_text_scale(text_scale);
            a11y_svc_->set_high_contrast(high_contrast);
        } else if (state_mgr_) {
            if (text_scale != 0) state_mgr_->set_text_scale(text_scale);
            state_mgr_->set_high_contrast(high_contrast);
        } else {
            return record_error(kErrnoENODEV);
        }
        return record_success(0);
    }

    case kSysWiniosDiagEventQuery: {
        if (!diag_service_) {
            return record_error(kErrnoENODEV);
        }
        if (arg0 == 0) {
            return record_error(kErrnoEFAULT);
        }
        winios_diag_query_req_t req{};
        if (!uas.copyout(reinterpret_cast<const void*>(arg0), &req, sizeof(req))) {
            return record_error(kErrnoEFAULT);
        }

        bool privileged = false;
        if (cap_mgr_ && cap_mgr_->check(proc.pid(), security::Capability::DiagnosticsAccess)) {
            privileged = true;
        }

        const uint32_t bounded_max = std::min<uint32_t>(req.max_entries, 128);
        if (req.max_entries > 0 && req.buffer_va == 0) {
            return record_error(kErrnoEFAULT);
        }

        auto events = diag_service_->query_events(
            req.after_sequence,
            req.category_filter,
            req.min_level,
            bounded_max,
            proc.pid(),
            privileged);

        req.returned_entries = static_cast<uint32_t>(events.size());

        if (!events.empty() && req.buffer_va != 0) {
            if (!uas.copyin(req.buffer_va, events.data(), events.size() * sizeof(winios_diag_event_record_t))) {
                return record_error(kErrnoEFAULT);
            }
        }

        if (!uas.copyin(arg0, &req, sizeof(req))) {
            return record_error(kErrnoEFAULT);
        }

        return record_success(events.size());
    }

    case kSysWiniosAppInfo: {
        if (!app_reg_) {
            return record_error(kErrnoENODEV);
        }
        if (arg0 == 0 || arg1 == 0) {
            return record_error(kErrnoEFAULT);
        }
        char bundle_buf[64]{0};
        if (!uas.copyout(reinterpret_cast<const void*>(arg0), bundle_buf, sizeof(bundle_buf) - 1)) {
            return record_error(kErrnoEFAULT);
        }
        bundle_buf[sizeof(bundle_buf) - 1] = '\0';
        std::string bid(bundle_buf);

        auto app_opt = app_reg_->get_app(bid);
        if (!app_opt.has_value()) {
            return record_error(kErrnoENOENT);
        }

        winios_app_info_t info{};
        safe_copy_str(info.bundle_id, sizeof(info.bundle_id), app_opt->bundle_id);
        safe_copy_str(info.display_name, sizeof(info.display_name), app_opt->display_name);
        safe_copy_str(info.version, sizeof(info.version), app_opt->version);
        info.min_sdk_version = app_opt->min_sdk_version;
        info.state = static_cast<uint32_t>(app_opt->state);
        info.storage_quota_bytes = app_opt->storage_quota_bytes;

        if (!uas.copyin(arg1, &info, sizeof(info))) {
            return record_error(kErrnoEFAULT);
        }
        return record_success(0);
    }

    case kSysWiniosAppList: {
        if (!app_reg_) {
            return record_error(kErrnoENODEV);
        }
        if (arg0 == 0) {
            return record_error(kErrnoEFAULT);
        }
        winios_app_list_req_t req{};
        if (!uas.copyout(reinterpret_cast<const void*>(arg0), &req, sizeof(req))) {
            return record_error(kErrnoEFAULT);
        }
        auto apps = app_reg_->list_apps(req.active_only != 0);
        uint32_t to_return = std::min(req.max_entries, static_cast<uint32_t>(apps.size()));
        req.returned_entries = to_return;

        if (to_return > 0 && req.buffer_va != 0) {
            std::vector<winios_app_info_t> out_list(to_return);
            for (uint32_t i = 0; i < to_return; ++i) {
                safe_copy_str(out_list[i].bundle_id, sizeof(out_list[i].bundle_id), apps[i].bundle_id);
                safe_copy_str(out_list[i].display_name, sizeof(out_list[i].display_name), apps[i].display_name);
                safe_copy_str(out_list[i].version, sizeof(out_list[i].version), apps[i].version);
                out_list[i].min_sdk_version = apps[i].min_sdk_version;
                out_list[i].state = static_cast<uint32_t>(apps[i].state);
                out_list[i].storage_quota_bytes = apps[i].storage_quota_bytes;
            }
            if (!uas.copyin(req.buffer_va, out_list.data(), to_return * sizeof(winios_app_info_t))) {
                return record_error(kErrnoEFAULT);
            }
        }

        if (!uas.copyin(arg0, &req, sizeof(req))) {
            return record_error(kErrnoEFAULT);
        }
        return record_success(to_return);
    }

        default: {
            ++stats_.total_invalid_syscalls;
            return record_error(kErrnoENOSYS);
        }
        }
    } catch (const std::exception&) {
        return record_error(kErrnoEINVAL);
    } catch (...) {
        return record_error(kErrnoEINVAL);
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

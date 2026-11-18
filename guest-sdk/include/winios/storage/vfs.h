#ifndef WINIOS_GUEST_SDK_VFS_H
#define WINIOS_GUEST_SDK_VFS_H

#include "winios/winios_types.h"
#include "winios/system/syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WINIOS_O_RDONLY 0x0000
#define WINIOS_O_WRONLY 0x0001
#define WINIOS_O_RDWR   0x0002
#define WINIOS_O_CREAT  0x0200
#define WINIOS_O_TRUNC  0x0400
#define WINIOS_O_APPEND 0x0008

// Standard POSIX/Darwin VFS wrappers
static inline int32_t winios_vfs_open(const char* path, int32_t flags, int32_t mode) {
    return (int32_t)winios_syscall3(WINIOS_SYS_OPEN, (uint64_t)path, (uint64_t)flags, (uint64_t)mode);
}

static inline int64_t winios_vfs_read(int32_t fd, void* buf, size_t count) {
    return winios_syscall3(WINIOS_SYS_READ, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}

static inline int64_t winios_vfs_write(int32_t fd, const void* buf, size_t count) {
    return winios_syscall3(WINIOS_SYS_WRITE, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}

static inline int32_t winios_vfs_close(int32_t fd) {
    return (int32_t)winios_syscall1(WINIOS_SYS_CLOSE, (uint64_t)fd);
}

static inline int32_t winios_vfs_pipe(int32_t fds[2]) {
    return (int32_t)winios_syscall1(WINIOS_SYS_PIPE, (uint64_t)fds);
}

#ifdef __cplusplus
}
#endif

#endif // WINIOS_GUEST_SDK_VFS_H
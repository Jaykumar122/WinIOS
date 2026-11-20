#include "winios/winios.h"

int main(int argc, const char* const* argv) {
    (void)argc;
    (void)argv;

    // 1. Query negotiated ABI version via Category C syscall 507
    // In Fast ABI, syscall number in X0; in DarwinStrict, in X16.
    // winios_syscall1 invokes SVC #0x80 with syscall in X16 and arg in X0.
    int64_t abi_ver = winios_syscall1(WINIOS_SYS_GET_ABI_VERSION, 0);
    if ((abi_ver & 0xFFFF0000) != 0x00010000) {
        return 1;
    }

    // 2. Query process ID via SYS_getpid (20)
    uint32_t pid = winios_get_pid();
    if (pid == 0) {
        return 2;
    }

    // 3. Query user ID via SYS_getuid (24)
    int64_t uid = winios_syscall0(WINIOS_SYS_GETUID);
    if (uid < 0) {
        return 3;
    }

    // Expected success status: 77
    return 77;
}

#ifndef WINIOS_GUEST_SDK_PROCESS_H
#define WINIOS_GUEST_SDK_PROCESS_H

#include "winios/winios_types.h"
#include "winios/system/syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t pid;
    uint32_t ppid;
    uint32_t state;
    uint64_t instructions_retired;
    uint64_t heap_used_bytes;
    uint32_t thread_count;
    char name[32];
} winios_process_info_t;

// Process management APIs
int32_t winios_spawn_process(const char* exec_path, const char* const argv[], uint32_t* out_pid);
int32_t winios_waitpid(uint32_t pid, int32_t* out_status);
int32_t winios_get_process_info(uint32_t pid, winios_process_info_t* out_info);
int32_t winios_terminate_process(uint32_t pid, int32_t exit_code);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_GUEST_SDK_PROCESS_H
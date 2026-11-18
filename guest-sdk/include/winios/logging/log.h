#ifndef WINIOS_GUEST_SDK_LOG_H
#define WINIOS_GUEST_SDK_LOG_H

#include "winios/winios_types.h"
#include "winios/system/syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WINIOS_LOG_DEBUG = 0,
    WINIOS_LOG_INFO  = 1,
    WINIOS_LOG_WARN  = 2,
    WINIOS_LOG_ERROR = 3,
    WINIOS_LOG_FATAL = 4
} winios_log_level_t;

#pragma pack(push, 8)
typedef struct {
    uint32_t severity;
    uint32_t pid;
    uint32_t tid;
    uint32_t tag_len;
    uint32_t msg_len;
    uint32_t reserved;
    uint64_t tag_ptr;
    uint64_t msg_ptr;
    uint64_t timestamp_us;
} winios_log_entry_t;
#pragma pack(pop)

int32_t winios_log(winios_log_level_t level, const char* tag, const char* msg);
int32_t winios_log_debug(const char* tag, const char* msg);
int32_t winios_log_info(const char* tag, const char* msg);
int32_t winios_log_warn(const char* tag, const char* msg);
int32_t winios_log_error(const char* tag, const char* msg);
int32_t winios_log_fatal(const char* tag, const char* msg);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_GUEST_SDK_LOG_H
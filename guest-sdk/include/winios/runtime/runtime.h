#ifndef WINIOS_GUEST_SDK_RUNTIME_H
#define WINIOS_GUEST_SDK_RUNTIME_H

#include "winios/winios_types.h"
#include "winios/system/syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 8)
typedef struct {
    uint32_t contract_version;    // 0x00010500 (v1.5)
    uint32_t process_id;          // Process ID
    uint64_t app_base_address;    // Base address where Mach-O is mapped
    uint64_t stack_base;          // Stack bottom address
    uint64_t stack_size;          // Allocated stack size
    uint64_t heap_base;           // Heap starting address
    uint64_t heap_limit;          // Heap maximum address
    uint32_t argc;                // Argument count
    uint32_t envc;                // Environment variable count
    uint64_t argv_ptrs;           // Guest pointer to argv array
    uint64_t envp_ptrs;           // Guest pointer to envp array
    uint32_t initial_window_id;   // Default window ID created for app (or 0)
    uint32_t bundle_storage_id;   // Isolated storage partition ID
    uint32_t reserved[4];         // Padding
} winios_app_arg_block_t;
#pragma pack(pop)

typedef enum {
    WINIOS_APP_STATE_CREATED      = 0,
    WINIOS_APP_STATE_LOADING      = 1,
    WINIOS_APP_STATE_INITIALIZING = 2,
    WINIOS_APP_STATE_RUNNING      = 3,
    WINIOS_APP_STATE_SUSPENDED    = 4,
    WINIOS_APP_STATE_RESUMING     = 5,
    WINIOS_APP_STATE_TERMINATING  = 6,
    WINIOS_APP_STATE_EXITED       = 7,
    WINIOS_APP_STATE_FAILED       = 8
} winios_app_state_t;

// Runtime global initialization (called by crt0 or entry point)
void winios_runtime_init(const winios_app_arg_block_t* args);

// Accessors
const winios_app_arg_block_t* winios_get_arg_block(void);
uint32_t winios_get_pid(void);
uint32_t winios_get_argc(void);
const char* const* winios_get_argv(void);
const char* winios_getenv(const char* name);

// Freestanding Memory Operations
void*  winios_memcpy(void* dest, const void* src, size_t n);
void*  winios_memmove(void* dest, const void* src, size_t n);
void*  winios_memset(void* s, int c, size_t n);
int    winios_memcmp(const void* s1, const void* s2, size_t n);

// Freestanding String Operations
size_t winios_strlen(const char* s);
int    winios_strcmp(const char* s1, const char* s2);
int    winios_strncmp(const char* s1, const char* s2, size_t n);
char*  winios_strcpy(char* dest, const char* src);
char*  winios_strncpy(char* dest, const char* src, size_t n);

// Formatting & Conversions
void    winios_itoa(int32_t value, char* str, int base);
int32_t winios_atoi(const char* str);
void    winios_u64_to_hex(uint64_t val, char* out_buf);

// Exit process
void winios_exit(int status);

// Scheduling
void winios_sched_yield(void);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_GUEST_SDK_RUNTIME_H
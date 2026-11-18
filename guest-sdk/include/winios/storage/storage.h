#ifndef WINIOS_GUEST_SDK_STORAGE_H
#define WINIOS_GUEST_SDK_STORAGE_H

#include "winios/winios_types.h"
#include "winios/system/syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 8)
typedef struct {
    uint64_t path_ptr;
    uint32_t path_len;
    uint32_t flags;
} winios_storage_req_t;
#pragma pack(pop)

// Isolated per-app storage operations
int32_t winios_storage_write_file(const char* rel_path, const void* data, size_t size);
int32_t winios_storage_read_file(const char* rel_path, void* buf, size_t buf_size, size_t* out_read_size);
int32_t winios_storage_file_exists(const char* rel_path);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_GUEST_SDK_STORAGE_H
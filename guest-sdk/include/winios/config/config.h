#ifndef WINIOS_GUEST_SDK_CONFIG_H
#define WINIOS_GUEST_SDK_CONFIG_H

#include <stddef.h>
#include <stdint.h>
#include "winios/winios_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 8)
typedef struct {
    uint64_t key_ptr;      // Pointer to UTF-8 key string
    uint32_t key_len;      // Key string length
    uint32_t val_type;     // 0=String, 1=Int64, 2=Double, 3=Bool
    uint64_t val_buf_ptr;  // Pointer to buffer
    uint32_t val_buf_size; // Buffer size in bytes
    uint32_t out_len;      // Output data length
} winios_config_req_t;
#pragma pack(pop)

// Typed Configuration C API (SDK v1.8)
int winios_config_get_string(const char* key, char* out_buf, size_t max_len, const char* default_val);
int winios_config_set_string(const char* key, const char* val);

int64_t winios_config_get_int(const char* key, int64_t default_val);
int winios_config_set_int(const char* key, int64_t val);

double winios_config_get_double(const char* key, double default_val);
int winios_config_set_double(const char* key, double val);

int winios_config_get_bool(const char* key, int default_val);
int winios_config_set_bool(const char* key, int val);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_GUEST_SDK_CONFIG_H

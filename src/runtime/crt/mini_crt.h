#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdarg>

#ifdef __cplusplus
extern "C" {
#endif

// WinIOS Minimal C Runtime (Campaign 217)
void*  w_memcpy(void* dest, const void* src, size_t n);
void*  w_memset(void* s, int c, size_t n);
void*  w_memmove(void* dest, const void* src, size_t n);
int    w_memcmp(const void* s1, const void* s2, size_t n);

size_t w_strlen(const char* s);
int    w_strcmp(const char* s1, const char* s2);
int    w_strncmp(const char* s1, const char* s2, size_t n);
char*  w_strcpy(char* dest, const char* src);
char*  w_strncpy(char* dest, const char* src, size_t n);
char*  w_strcat(char* dest, const char* src);

int    w_vsnprintf(char* buf, size_t size, const char* fmt, va_list args);
int    w_snprintf(char* buf, size_t size, const char* fmt, ...);
int    w_atoi(const char* s);
char*  w_itoa(int value, char* buf, int base);

#ifdef __cplusplus
}
#endif

#include "runtime/crt/mini_crt.h"
#include <cstdio>

extern "C" {

void* w_memcpy(void* dest, const void* src, size_t n) {
    auto* d = static_cast<unsigned char*>(dest);
    const auto* s = static_cast<const unsigned char*>(src);
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dest;
}

void* w_memset(void* s, int c, size_t n) {
    auto* p = static_cast<unsigned char*>(s);
    const auto v = static_cast<unsigned char>(c);
    for (size_t i = 0; i < n; ++i) {
        p[i] = v;
    }
    return s;
}

void* w_memmove(void* dest, const void* src, size_t n) {
    auto* d = static_cast<unsigned char*>(dest);
    const auto* s = static_cast<const unsigned char*>(src);
    if (d == s || n == 0) return dest;
    if (d < s) {
        for (size_t i = 0; i < n; ++i) {
            d[i] = s[i];
        }
    } else {
        for (size_t i = n; i > 0; --i) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}

int w_memcmp(const void* s1, const void* s2, size_t n) {
    const auto* p1 = static_cast<const unsigned char*>(s1);
    const auto* p2 = static_cast<const unsigned char*>(s2);
    for (size_t i = 0; i < n; ++i) {
        if (p1[i] != p2[i]) {
            return (p1[i] < p2[i]) ? -1 : 1;
        }
    }
    return 0;
}

size_t w_strlen(const char* s) {
    if (!s) return 0;
    size_t len = 0;
    while (s[len] != '\0') {
        ++len;
    }
    return len;
}

int w_strcmp(const char* s1, const char* s2) {
    if (!s1 || !s2) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (*s1 && (*s1 == *s2)) {
        ++s1;
        ++s2;
    }
    return static_cast<int>(static_cast<unsigned char>(*s1)) - static_cast<int>(static_cast<unsigned char>(*s2));
}

int w_strncmp(const char* s1, const char* s2, size_t n) {
    if (n == 0) return 0;
    if (!s1 || !s2) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (n > 1 && *s1 && (*s1 == *s2)) {
        ++s1;
        ++s2;
        --n;
    }
    return static_cast<int>(static_cast<unsigned char>(*s1)) - static_cast<int>(static_cast<unsigned char>(*s2));
}

char* w_strcpy(char* dest, const char* src) {
    if (!dest || !src) return dest;
    char* d = dest;
    while ((*d++ = *src++) != '\0') {}
    return dest;
}

char* w_strncpy(char* dest, const char* src, size_t n) {
    if (!dest || !src || n == 0) return dest;
    size_t i = 0;
    for (; i < n && src[i] != '\0'; ++i) {
        dest[i] = src[i];
    }
    for (; i < n; ++i) {
        dest[i] = '\0';
    }
    return dest;
}

char* w_strcat(char* dest, const char* src) {
    if (!dest || !src) return dest;
    char* d = dest;
    while (*d != '\0') {
        ++d;
    }
    while ((*d++ = *src++) != '\0') {}
    return dest;
}

int w_vsnprintf(char* buf, size_t size, const char* fmt, va_list args) {
    if (!buf || size == 0 || !fmt) return -1;
#if defined(_MSC_VER)
    return vsnprintf_s(buf, size, _TRUNCATE, fmt, args);
#else
    return vsnprintf(buf, size, fmt, args);
#endif
}

int w_snprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const int ret = w_vsnprintf(buf, size, fmt, args);
    va_end(args);
    return ret;
}

int w_atoi(const char* s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
        ++s;
    }
    int sign = 1;
    if (*s == '-') {
        sign = -1;
        ++s;
    } else if (*s == '+') {
        ++s;
    }
    int val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        ++s;
    }
    return sign * val;
}

char* w_itoa(int value, char* buf, int base) {
    if (!buf || base < 2 || base > 36) return buf;
    char* p = buf;
    char* p1;
    char* p2;
    unsigned int uval = static_cast<unsigned int>(value);
    if (base == 10 && value < 0) {
        *p++ = '-';
        uval = static_cast<unsigned int>(-value);
    }
    p1 = p;
    do {
        const unsigned int remainder = uval % static_cast<unsigned int>(base);
        *p++ = (remainder < 10) ? static_cast<char>('0' + remainder) : static_cast<char>('a' + remainder - 10);
        uval /= static_cast<unsigned int>(base);
    } while (uval > 0);
    *p = '\0';
    p2 = p - 1;
    while (p1 < p2) {
        char tmp = *p1;
        *p1 = *p2;
        *p2 = tmp;
        ++p1;
        --p2;
    }
    return buf;
}

} // extern "C"

#include "winios/winios.h"

// -----------------------------------------------------------------------------
// Global Runtime State
// -----------------------------------------------------------------------------
static winios_app_arg_block_t g_arg_block;
static int g_runtime_initialized = 0;
static const char* g_dummy_argv[] = { "winios_app", NULL };

void winios_runtime_init(const winios_app_arg_block_t* args) {
    if (args != NULL) {
        // Copy argument block safely
        const uint8_t* src = (const uint8_t*)args;
        uint8_t* dst = (uint8_t*)&g_arg_block;
        for (size_t i = 0; i < sizeof(winios_app_arg_block_t); ++i) {
            dst[i] = src[i];
        }
    } else {
        g_arg_block.contract_version = WINIOS_SDK_VERSION;
        g_arg_block.process_id = 1;
        g_arg_block.argc = 1;
        g_arg_block.argv_ptrs = (uint64_t)g_dummy_argv;
    }
    g_runtime_initialized = 1;
}

const winios_app_arg_block_t* winios_get_arg_block(void) {
    return &g_arg_block;
}

uint32_t winios_get_pid(void) {
    if (!g_runtime_initialized) {
        return (uint32_t)winios_syscall0(WINIOS_SYS_GETPID);
    }
    return g_arg_block.process_id;
}

uint32_t winios_get_argc(void) {
    return g_arg_block.argc;
}

const char* const* winios_get_argv(void) {
    if (g_arg_block.argv_ptrs == 0) {
        return g_dummy_argv;
    }
    return (const char* const*)g_arg_block.argv_ptrs;
}

void winios_exit(int status) {
    winios_syscall1(WINIOS_SYS_EXIT, (uint64_t)status);
    for (;;) {
#if defined(__aarch64__)
        __asm__ volatile("wfe");
#endif
    }
}

// -----------------------------------------------------------------------------
// Freestanding Memory Operations
// -----------------------------------------------------------------------------
void* winios_memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;

    // Word-aligned fast path
    if ((((uintptr_t)d | (uintptr_t)s) & 0x7) == 0) {
        while (n >= 8) {
            *(uint64_t*)d = *(const uint64_t*)s;
            d += 8;
            s += 8;
            n -= 8;
        }
    }

    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void* winios_memmove(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;

    if (d == s || n == 0) {
        return dest;
    }

    if (d < s) {
        // Forward copy
        while (n--) {
            *d++ = *s++;
        }
    } else {
        // Backward copy for overlapping regions
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dest;
}

void* winios_memset(void* s, int c, size_t n) {
    uint8_t* p = (uint8_t*)s;
    const uint8_t val = (uint8_t)c;

    // Word-aligned fill
    if (((uintptr_t)p & 0x7) == 0 && n >= 8) {
        uint64_t word = val;
        word |= (word << 8);
        word |= (word << 16);
        word |= (word << 32);
        while (n >= 8) {
            *(uint64_t*)p = word;
            p += 8;
            n -= 8;
        }
    }

    while (n--) {
        *p++ = val;
    }
    return s;
}

int winios_memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    while (n--) {
        if (*p1 != *p2) {
            return (int)*p1 - (int)*p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

#if defined(__aarch64__)
void* memcpy(void* dest, const void* src, size_t n) {
    return winios_memcpy(dest, src, n);
}
void* memmove(void* dest, const void* src, size_t n) {
    return winios_memmove(dest, src, n);
}
void* memset(void* s, int c, size_t n) {
    return winios_memset(s, c, n);
}
int memcmp(const void* s1, const void* s2, size_t n) {
    return winios_memcmp(s1, s2, n);
}
#endif

// -----------------------------------------------------------------------------
// Freestanding String Operations
// -----------------------------------------------------------------------------
size_t winios_strlen(const char* s) {
    if (!s) return 0;
    const char* p = s;
    while (*p) {
        p++;
    }
    return (size_t)(p - s);
}

#if defined(__aarch64__)
size_t strlen(const char* s) {
    return winios_strlen(s);
}
#endif

int winios_strcmp(const char* s1, const char* s2) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int winios_strncmp(const char* s1, const char* s2, size_t n) {
    if (n == 0) return 0;
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    while (n > 0 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

char* winios_strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++) != '\0') {}
    return dest;
}

char* winios_strncpy(char* dest, const char* src, size_t n) {
    char* d = dest;
    while (n > 0 && *src != '\0') {
        *d++ = *src++;
        n--;
    }
    while (n > 0) {
        *d++ = '\0';
        n--;
    }
    return dest;
}

// -----------------------------------------------------------------------------
// String Conversion & Formatting
// -----------------------------------------------------------------------------
void winios_itoa(int32_t value, char* str, int base) {
    char* p = str;
    int is_negative = 0;

    if (value == 0) {
        *p++ = '0';
        *p = '\0';
        return;
    }

    if (value < 0 && base == 10) {
        is_negative = 1;
        value = -value;
    }

    char buf[32];
    int idx = 0;
    while (value != 0) {
        int rem = value % base;
        buf[idx++] = (rem > 9) ? (char)((rem - 10) + 'a') : (char)(rem + '0');
        value /= base;
    }

    if (is_negative) {
        *p++ = '-';
    }

    while (idx > 0) {
        *p++ = buf[--idx];
    }
    *p = '\0';
}

int32_t winios_atoi(const char* str) {
    if (!str) return 0;
    int32_t res = 0;
    int sign = 1;
    while (*str == ' ' || *str == '\t') str++;
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return res * sign;
}

void winios_u64_to_hex(uint64_t val, char* out_buf) {
    static const char hex_digits[] = "0123456789abcdef";
    out_buf[0] = '0';
    out_buf[1] = 'x';
    for (int i = 0; i < 16; ++i) {
        int shift = (15 - i) * 4;
        out_buf[2 + i] = hex_digits[(val >> shift) & 0xFULL];
    }
    out_buf[18] = '\0';
}

// -----------------------------------------------------------------------------
// Environment Variables
// -----------------------------------------------------------------------------
const char* winios_getenv(const char* name) {
    if (!name || g_arg_block.envp_ptrs == 0 || g_arg_block.envc == 0) {
        return NULL;
    }
    const char* const* envp = (const char* const*)g_arg_block.envp_ptrs;
    size_t name_len = winios_strlen(name);

    for (uint32_t i = 0; i < g_arg_block.envc; ++i) {
        const char* entry = envp[i];
        if (!entry) break;
        if (winios_strncmp(entry, name, name_len) == 0 && entry[name_len] == '=') {
            return entry + name_len + 1;
        }
    }
    return NULL;
}

// -----------------------------------------------------------------------------
// Logging Implementation
// -----------------------------------------------------------------------------
int32_t winios_log(winios_log_level_t level, const char* tag, const char* msg) {
    winios_log_entry_t entry;
    entry.severity = (uint32_t)level;
    entry.pid = winios_get_pid();
    entry.tid = 1;
    entry.tag_ptr = (uint64_t)tag;
    entry.tag_len = (uint32_t)winios_strlen(tag);
    entry.msg_ptr = (uint64_t)msg;
    entry.msg_len = (uint32_t)winios_strlen(msg);
    entry.timestamp_us = 0;
    entry.reserved = 0;

    return (int32_t)winios_syscall1(WINIOS_SYS_LOG, (uint64_t)&entry);
}

int32_t winios_log_debug(const char* tag, const char* msg) { return winios_log(WINIOS_LOG_DEBUG, tag, msg); }
int32_t winios_log_info(const char* tag, const char* msg)  { return winios_log(WINIOS_LOG_INFO, tag, msg); }
int32_t winios_log_warn(const char* tag, const char* msg)  { return winios_log(WINIOS_LOG_WARN, tag, msg); }
int32_t winios_log_error(const char* tag, const char* msg) { return winios_log(WINIOS_LOG_ERROR, tag, msg); }
int32_t winios_log_fatal(const char* tag, const char* msg) { return winios_log(WINIOS_LOG_FATAL, tag, msg); }

// -----------------------------------------------------------------------------
// UI & Graphics Implementation
// -----------------------------------------------------------------------------
winios_wid_t winios_window_create(const char* title, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    uint32_t title_len = (uint32_t)winios_strlen(title);
    return (winios_wid_t)winios_syscall6(WINIOS_SYS_WINDOW_CREATE, (uint64_t)title, (uint64_t)title_len, (uint64_t)x, (uint64_t)y, (uint64_t)width, (uint64_t)height);
}

int32_t winios_window_destroy(winios_wid_t wid) {
    return (int32_t)winios_syscall1(WINIOS_SYS_WINDOW_DESTROY, (uint64_t)wid);
}

int32_t winios_window_set_title(winios_wid_t wid, const char* title) {
    uint32_t title_len = (uint32_t)winios_strlen(title);
    return (int32_t)winios_syscall3(WINIOS_SYS_SET_WINDOW_TITLE, (uint64_t)wid, (uint64_t)title, (uint64_t)title_len);
}

int32_t winios_window_get_geometry(winios_wid_t wid, winios_window_geometry_t* geom) {
    return (int32_t)winios_syscall2(WINIOS_SYS_WINDOW_GET_GEOMETRY, (uint64_t)wid, (uint64_t)geom);
}

int32_t winios_window_set_focus(winios_wid_t wid) {
    return (int32_t)winios_syscall1(WINIOS_SYS_WINDOW_SET_FOCUS, (uint64_t)wid);
}

int32_t winios_poll_event(winios_event_t* out_event) {
    return (int32_t)winios_syscall1(WINIOS_SYS_POLL_EVENT, (uint64_t)out_event);
}

int32_t winios_surface_fill(winios_wid_t wid, uint32_t x, uint32_t y, uint32_t width, uint32_t height, winios_color_t color) {
    return (int32_t)winios_syscall6(WINIOS_SYS_SURFACE_FILL, (uint64_t)wid, (uint64_t)x, (uint64_t)y, (uint64_t)width, (uint64_t)height, (uint64_t)color);
}

int32_t winios_surface_update(winios_wid_t wid, const void* pixels, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    return (int32_t)winios_syscall6(WINIOS_SYS_SURFACE_UPDATE, (uint64_t)wid, (uint64_t)pixels, (uint64_t)x, (uint64_t)y, (uint64_t)width, (uint64_t)height);
}

int32_t winios_surface_present(winios_wid_t wid) {
    return (int32_t)winios_syscall1(WINIOS_SYS_SURFACE_PRESENT, (uint64_t)wid);
}

uint32_t winios_create_timer(uint32_t interval_ms) {
    return (uint32_t)winios_syscall1(WINIOS_SYS_CREATE_TIMER, (uint64_t)interval_ms);
}

// -----------------------------------------------------------------------------
// Isolated Storage APIs
// -----------------------------------------------------------------------------
int32_t winios_storage_write_file(const char* rel_path, const void* data, size_t size) {
    winios_storage_req_t req;
    req.path_ptr = (uint64_t)rel_path;
    req.path_len = (uint32_t)winios_strlen(rel_path);
    req.flags = 0x02; // Write mode

    int64_t written = winios_syscall3(WINIOS_SYS_STORAGE_WRITE, (uint64_t)&req, (uint64_t)data, (uint64_t)size);
    return (int32_t)written;
}

int32_t winios_storage_read_file(const char* rel_path, void* buf, size_t buf_size, size_t* out_read_size) {
    winios_storage_req_t req;
    req.path_ptr = (uint64_t)rel_path;
    req.path_len = (uint32_t)winios_strlen(rel_path);
    req.flags = 0x01; // Read mode

    int64_t bytes_read = winios_syscall3(WINIOS_SYS_STORAGE_READ, (uint64_t)&req, (uint64_t)buf, (uint64_t)buf_size);
    if (out_read_size) {
        *out_read_size = (bytes_read >= 0) ? (size_t)bytes_read : 0;
    }
    return (int32_t)bytes_read;
}

int32_t winios_storage_file_exists(const char* rel_path) {
    uint8_t tmp[1];
    size_t read_bytes = 0;
    int32_t res = winios_storage_read_file(rel_path, tmp, 1, &read_bytes);
    return res >= 0 ? 1 : 0;
}

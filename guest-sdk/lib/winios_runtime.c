#include "winios/winios.h"
#include "winios/ui/font8x8.h"

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

int32_t winios_window_set_geometry(winios_wid_t wid, const winios_window_geometry_t* geom) {
    return (int32_t)winios_syscall2(WINIOS_SYS_WINDOW_SET_GEOMETRY, (uint64_t)wid, (uint64_t)geom);
}

int32_t winios_window_request_focus(winios_wid_t wid) {
    return winios_window_set_focus(wid);
}

int32_t winios_window_set_visibility(winios_wid_t wid, uint32_t visible) {
    return (int32_t)winios_syscall2(WINIOS_SYS_WINDOW_SET_VISIBILITY, (uint64_t)wid, (uint64_t)visible);
}

int32_t winios_window_get_state(winios_wid_t wid, uint32_t* out_state) {
    if (!out_state) return -1;
    winios_window_geometry_t geom;
    int32_t ret = winios_window_get_geometry(wid, &geom);
    if (ret == 0) {
        *out_state = geom.state;
    }
    return ret;
}

int32_t winios_poll_event(winios_event_t* out_event) {
    if (!out_event) return -1;
    return (int32_t)winios_syscall2(WINIOS_SYS_POLL_EVENT, (uint64_t)out_event, (uint64_t)sizeof(winios_event_t));
}

int32_t winios_poll_event_v2(winios_event_v2_t* out_event) {
    if (!out_event) return -1;
    return (int32_t)winios_syscall2(WINIOS_SYS_POLL_EVENT, (uint64_t)out_event, (uint64_t)sizeof(winios_event_v2_t));
}

void winios_sched_yield(void) {
    winios_syscall0(WINIOS_SYS_SCHED_YIELD);
}

int32_t winios_wait_event(winios_event_t* out_event, uint32_t timeout_ms) {
    if (!out_event) return -1;
    uint32_t elapsed = 0;
    while (elapsed <= timeout_ms) {
        int32_t ret = winios_poll_event(out_event);
        if (ret > 0) return ret;
        winios_sched_yield();
        elapsed += 1;
    }
    return 0;
}

int32_t winios_wait_event_v2(winios_event_v2_t* out_event, uint32_t timeout_ms) {
    if (!out_event) return -1;
    uint32_t elapsed = 0;
    while (elapsed <= timeout_ms) {
        int32_t ret = winios_poll_event_v2(out_event);
        if (ret > 0) return ret;
        winios_sched_yield();
        elapsed += 1;
    }
    return 0;
}

#ifndef WINIOS_HOST_TEST_OVERRIDE
int32_t winios_surface_fill(winios_wid_t wid, uint32_t x, uint32_t y, uint32_t width, uint32_t height, winios_color_t color) {
    return (int32_t)winios_syscall6(WINIOS_SYS_SURFACE_FILL, (uint64_t)wid, (uint64_t)x, (uint64_t)y, (uint64_t)width, (uint64_t)height, (uint64_t)color);
}

int32_t winios_surface_update(winios_wid_t wid, const void* pixels, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    return (int32_t)winios_syscall6(WINIOS_SYS_SURFACE_UPDATE, (uint64_t)wid, (uint64_t)pixels, (uint64_t)x, (uint64_t)y, (uint64_t)width, (uint64_t)height);
}
#endif

int32_t winios_surface_present(winios_wid_t wid) {
    return (int32_t)winios_syscall1(WINIOS_SYS_SURFACE_PRESENT, (uint64_t)wid);
}

int32_t winios_draw_pixel(winios_wid_t wid, int32_t x, int32_t y, winios_color_t color) {
    if (x < 0 || y < 0) return 0;
    return winios_surface_fill(wid, (uint32_t)x, (uint32_t)y, 1, 1, color);
}

int32_t winios_draw_line(winios_wid_t wid, int32_t x0, int32_t y0, int32_t x1, int32_t y1, winios_color_t color) {
    int32_t dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int32_t dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx - dy;

    if (y0 == y1) {
        int32_t min_x = (x0 < x1) ? x0 : x1;
        uint32_t len = (uint32_t)(dx + 1);
        if (min_x >= 0 && y0 >= 0) {
            return winios_surface_fill(wid, (uint32_t)min_x, (uint32_t)y0, len, 1, color);
        }
    }
    if (x0 == x1) {
        int32_t min_y = (y0 < y1) ? y0 : y1;
        uint32_t len = (uint32_t)(dy + 1);
        if (x0 >= 0 && min_y >= 0) {
            return winios_surface_fill(wid, (uint32_t)x0, (uint32_t)min_y, 1, len, color);
        }
    }

    while (1) {
        winios_draw_pixel(wid, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
    return 0;
}

int32_t winios_draw_rect(winios_wid_t wid, int32_t x, int32_t y, uint32_t w, uint32_t h, winios_color_t color, uint32_t filled) {
    if (x < 0 || y < 0 || w == 0 || h == 0) return 0;
    if (filled) {
        return winios_surface_fill(wid, (uint32_t)x, (uint32_t)y, w, h, color);
    }
    winios_surface_fill(wid, (uint32_t)x, (uint32_t)y, w, 1, color);
    if (h > 1) {
        winios_surface_fill(wid, (uint32_t)x, (uint32_t)(y + h - 1), w, 1, color);
    }
    if (h > 2) {
        winios_surface_fill(wid, (uint32_t)x, (uint32_t)(y + 1), 1, h - 2, color);
        if (w > 1) {
            winios_surface_fill(wid, (uint32_t)(x + w - 1), (uint32_t)(y + 1), 1, h - 2, color);
        }
    }
    return 0;
}

int32_t winios_draw_text(winios_wid_t wid, int32_t x, int32_t y, const char* text, winios_color_t color) {
    if (!text || x < 0 || y < 0) return -1;
    int32_t cur_x = x;
    while (*text) {
        uint8_t ch = (uint8_t)*text++;
        if (ch == '\n') {
            y += 10;
            cur_x = x;
            continue;
        }
        if (ch >= 32 && ch <= 126) {
            const uint8_t* glyph = winios_font8x8_data[ch - 32];
            for (int r = 0; r < 8; ++r) {
                uint8_t row = glyph[r];
                for (int c = 0; c < 8; ++c) {
                    if (row & (1 << (7 - c))) {
                        winios_draw_pixel(wid, cur_x + c, y + r, color);
                    }
                }
            }
        }
        cur_x += 8;
    }
    return 0;
}

int32_t winios_blit(winios_wid_t wid, const void* src_pixels, int32_t dst_x, int32_t dst_y, uint32_t w, uint32_t h, uint32_t src_stride) {
    (void)src_stride;
    if (!src_pixels || dst_x < 0 || dst_y < 0 || w == 0 || h == 0) return -1;
    return winios_surface_update(wid, src_pixels, (uint32_t)dst_x, (uint32_t)dst_y, w, h);
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

// -----------------------------------------------------------------------------
// UI Toolkit Implementation
// -----------------------------------------------------------------------------
static int point_in_rect(int32_t px, int32_t py, const winios_rect_t* r) {
    if (!r) return 0;
    return (px >= (int32_t)r->x && px < (int32_t)(r->x + r->width) &&
            py >= (int32_t)r->y && py < (int32_t)(r->y + r->height));
}

static void draw_fb_pixel(uint32_t* fb, uint32_t stride, int32_t x, int32_t y, uint32_t color) {
    if (x >= 0 && y >= 0 && (uint32_t)x < stride) {
        fb[(uint32_t)y * stride + (uint32_t)x] = color;
    }
}

static void draw_fb_line(uint32_t* fb, uint32_t stride, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    int32_t dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int32_t dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx - dy;

    while (1) {
        draw_fb_pixel(fb, stride, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void draw_fb_rect(uint32_t* fb, uint32_t stride, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, int filled) {
    if (!fb || stride == 0 || w == 0 || h == 0) return;
    if (filled) {
        for (uint32_t row = 0; row < h; ++row) {
            uint32_t* dst = fb + (y + row) * stride + x;
            for (uint32_t col = 0; col < w; ++col) {
                dst[col] = color;
            }
        }
    } else {
        for (uint32_t col = 0; col < w; ++col) {
            draw_fb_pixel(fb, stride, (int32_t)(x + col), (int32_t)y, color);
            if (h > 1) draw_fb_pixel(fb, stride, (int32_t)(x + col), (int32_t)(y + h - 1), color);
        }
        for (uint32_t row = 1; row + 1 < h; ++row) {
            draw_fb_pixel(fb, stride, (int32_t)x, (int32_t)(y + row), color);
            if (w > 1) draw_fb_pixel(fb, stride, (int32_t)(x + w - 1), (int32_t)(y + row), color);
        }
    }
}

static void draw_fb_text(uint32_t* fb, uint32_t stride, uint32_t x, uint32_t y, const char* text, uint32_t color) {
    if (!fb || !text || stride == 0) return;
    uint32_t cur_x = x;
    while (*text) {
        uint8_t ch = (uint8_t)*text++;
        if (ch == '\n') {
            y += 10;
            cur_x = x;
            continue;
        }
        if (ch >= 32 && ch <= 126) {
            const uint8_t* glyph = winios_font8x8_data[ch - 32];
            for (int r = 0; r < 8; ++r) {
                uint8_t row = glyph[r];
                for (int c = 0; c < 8; ++c) {
                    if (row & (1 << (7 - c))) {
                        draw_fb_pixel(fb, stride, (int32_t)(cur_x + c), (int32_t)(y + r), color);
                    }
                }
            }
        }
        cur_x += 8;
    }
}

void winios_button_init(winios_button_t* btn, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* text) {
    if (!btn) return;
    btn->bounds.x = x;
    btn->bounds.y = y;
    btn->bounds.width = w;
    btn->bounds.height = h;
    btn->text = text;
    btn->bg_color = 0xFF3A3A3CU;
    btn->hover_bg_color = 0xFF48484AU;
    btn->pressed_bg_color = 0xFF2C2C2EU;
    btn->text_color = 0xFFFFFFFFU;
    btn->border_color = 0xFF636366U;
    btn->is_pressed = 0;
    btn->is_hovered = 0;
}

int winios_button_handle_event(winios_button_t* btn, const winios_event_v2_t* evt) {
    if (!btn || !evt) return 0;
    int inside = point_in_rect(evt->x, evt->y, &btn->bounds);

    if (evt->type == WINIOS_EVENT_V2_MOUSE_MOVE || evt->type == WINIOS_EVENT_V2_TOUCH_MOVE) {
        btn->is_hovered = (uint8_t)inside;
        return 0;
    }
    if (evt->type == WINIOS_EVENT_V2_MOUSE_DOWN || evt->type == WINIOS_EVENT_V2_TOUCH_BEGIN) {
        if (inside) {
            btn->is_pressed = 1;
        }
        return 0;
    }
    if (evt->type == WINIOS_EVENT_V2_MOUSE_UP || evt->type == WINIOS_EVENT_V2_TOUCH_END) {
        if (btn->is_pressed && inside) {
            btn->is_pressed = 0;
            return 1; // Click triggered!
        }
        btn->is_pressed = 0;
        return 0;
    }
    return 0;
}

void winios_button_render(uint32_t* fb, uint32_t stride, const winios_button_t* btn) {
    if (!fb || !btn || btn->bounds.width == 0 || btn->bounds.height == 0) return;
    uint32_t bg = btn->is_pressed ? btn->pressed_bg_color : (btn->is_hovered ? btn->hover_bg_color : btn->bg_color);
    draw_fb_rect(fb, stride, btn->bounds.x, btn->bounds.y, btn->bounds.width, btn->bounds.height, bg, 1);
    draw_fb_rect(fb, stride, btn->bounds.x, btn->bounds.y, btn->bounds.width, btn->bounds.height, btn->border_color, 0);

    if (btn->text) {
        size_t len = winios_strlen(btn->text);
        uint32_t text_w = (uint32_t)len * 8;
        uint32_t text_h = 8;
        uint32_t tx = (btn->bounds.width > text_w) ? (btn->bounds.x + (btn->bounds.width - text_w) / 2) : (btn->bounds.x + 2);
        uint32_t ty = (btn->bounds.height > text_h) ? (btn->bounds.y + (btn->bounds.height - text_h) / 2) : (btn->bounds.y + 2);
        draw_fb_text(fb, stride, tx, ty, btn->text, btn->text_color);
    }
}

void winios_label_init(winios_label_t* lbl, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* text) {
    if (!lbl) return;
    lbl->bounds.x = x;
    lbl->bounds.y = y;
    lbl->bounds.width = w;
    lbl->bounds.height = h;
    lbl->text = text;
    lbl->text_color = 0xFFFFFFFFU;
    lbl->bg_color = 0x00000000U; // transparent
}

void winios_label_render(uint32_t* fb, uint32_t stride, const winios_label_t* lbl) {
    if (!fb || !lbl) return;
    if (lbl->bg_color != 0) {
        draw_fb_rect(fb, stride, lbl->bounds.x, lbl->bounds.y, lbl->bounds.width, lbl->bounds.height, lbl->bg_color, 1);
    }
    if (lbl->text) {
        draw_fb_text(fb, stride, lbl->bounds.x, lbl->bounds.y, lbl->text, lbl->text_color);
    }
}

void winios_textinput_init(winios_textinput_t* ti, uint32_t x, uint32_t y, uint32_t w, uint32_t h, char* buffer, size_t capacity) {
    if (!ti) return;
    ti->bounds.x = x;
    ti->bounds.y = y;
    ti->bounds.width = w;
    ti->bounds.height = h;
    ti->buffer = buffer;
    ti->capacity = capacity;
    ti->length = buffer ? winios_strlen(buffer) : 0;
    ti->cursor_pos = ti->length;
    ti->is_focused = 0;
    ti->bg_color = 0xFF1C1C1EU;
    ti->text_color = 0xFFFFFFFFU;
    ti->border_color = 0xFF3A3A3CU;
    ti->focus_border_color = 0xFF007AFFU;
}

int winios_textinput_handle_event(winios_textinput_t* ti, const winios_event_v2_t* evt) {
    if (!ti || !evt || !ti->buffer || ti->capacity == 0) return 0;

    if (evt->type == WINIOS_EVENT_V2_MOUSE_DOWN || evt->type == WINIOS_EVENT_V2_TOUCH_BEGIN) {
        int inside = point_in_rect(evt->x, evt->y, &ti->bounds);
        ti->is_focused = (uint8_t)inside;
        if (inside) {
            int32_t rel_x = evt->x - (int32_t)ti->bounds.x - 4;
            if (rel_x <= 0) {
                ti->cursor_pos = 0;
            } else {
                size_t p = (size_t)(rel_x / 8);
                ti->cursor_pos = (p > ti->length) ? ti->length : p;
            }
        }
        return 0;
    }

    if (!ti->is_focused) return 0;

    if (evt->type == WINIOS_EVENT_V2_KEY_CHAR) {
        if (evt->key_code >= 32 && evt->key_code <= 126) {
            if (ti->length + 1 < ti->capacity) {
                for (size_t i = ti->length; i > ti->cursor_pos; --i) {
                    ti->buffer[i] = ti->buffer[i - 1];
                }
                ti->buffer[ti->cursor_pos] = (char)evt->key_code;
                ti->cursor_pos++;
                ti->length++;
                ti->buffer[ti->length] = '\0';
                return 1;
            }
        } else if (evt->key_code == 8) { // Backspace
            if (ti->cursor_pos > 0) {
                for (size_t i = ti->cursor_pos - 1; i < ti->length; ++i) {
                    ti->buffer[i] = ti->buffer[i + 1];
                }
                ti->cursor_pos--;
                ti->length--;
                ti->buffer[ti->length] = '\0';
                return 1;
            }
        }
    } else if (evt->type == WINIOS_EVENT_V2_KEY_DOWN) {
        if (evt->key_code == 0x25) { // Left arrow
            if (ti->cursor_pos > 0) ti->cursor_pos--;
        } else if (evt->key_code == 0x27) { // Right arrow
            if (ti->cursor_pos < ti->length) ti->cursor_pos++;
        }
    }

    return 0;
}

void winios_textinput_render(uint32_t* fb, uint32_t stride, const winios_textinput_t* ti) {
    if (!fb || !ti) return;
    draw_fb_rect(fb, stride, ti->bounds.x, ti->bounds.y, ti->bounds.width, ti->bounds.height, ti->bg_color, 1);
    uint32_t bc = ti->is_focused ? ti->focus_border_color : ti->border_color;
    draw_fb_rect(fb, stride, ti->bounds.x, ti->bounds.y, ti->bounds.width, ti->bounds.height, bc, 0);

    uint32_t ty = ti->bounds.y + (ti->bounds.height > 8 ? (ti->bounds.height - 8) / 2 : 2);
    if (ti->buffer) {
        draw_fb_text(fb, stride, ti->bounds.x + 4, ty, ti->buffer, ti->text_color);
    }

    if (ti->is_focused) {
        uint32_t cx = ti->bounds.x + 4 + (uint32_t)ti->cursor_pos * 8;
        if (cx < ti->bounds.x + ti->bounds.width - 2) {
            uint32_t cy0 = ti->bounds.y + 4;
            uint32_t cy1 = (ti->bounds.height > 8) ? (ti->bounds.y + ti->bounds.height - 4) : (ti->bounds.y + 8);
            draw_fb_line(fb, stride, (int32_t)cx, (int32_t)cy0, (int32_t)cx, (int32_t)cy1, ti->focus_border_color);
        }
    }
}

void winios_checkbox_init(winios_checkbox_t* cb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* label, uint8_t initial_state) {
    if (!cb) return;
    cb->bounds.x = x;
    cb->bounds.y = y;
    cb->bounds.width = w;
    cb->bounds.height = h;
    cb->label = label;
    cb->checked = initial_state;
    cb->text_color = 0xFFFFFFFFU;
    cb->box_color = 0xFF8E8E93U;
    cb->check_color = 0xFF34C759U;
}

int winios_checkbox_handle_event(winios_checkbox_t* cb, const winios_event_v2_t* evt) {
    if (!cb || !evt) return 0;
    if (evt->type == WINIOS_EVENT_V2_MOUSE_UP || evt->type == WINIOS_EVENT_V2_TOUCH_END) {
        if (point_in_rect(evt->x, evt->y, &cb->bounds)) {
            cb->checked = !cb->checked;
            return 1;
        }
    }
    return 0;
}

void winios_checkbox_render(uint32_t* fb, uint32_t stride, const winios_checkbox_t* cb) {
    if (!fb || !cb) return;
    uint32_t box_sz = 14;
    uint32_t by = cb->bounds.y + (cb->bounds.height > box_sz ? (cb->bounds.height - box_sz) / 2 : 0);
    draw_fb_rect(fb, stride, cb->bounds.x, by, box_sz, box_sz, cb->box_color, 0);

    if (cb->checked) {
        draw_fb_rect(fb, stride, cb->bounds.x + 3, by + 3, box_sz - 6, box_sz - 6, cb->check_color, 1);
    }
    if (cb->label) {
        draw_fb_text(fb, stride, cb->bounds.x + box_sz + 6, by + (box_sz - 8) / 2, cb->label, cb->text_color);
    }
}

void winios_progressbar_init(winios_progressbar_t* pb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t min_val, uint32_t max_val, uint32_t cur_val) {
    if (!pb) return;
    pb->bounds.x = x;
    pb->bounds.y = y;
    pb->bounds.width = w;
    pb->bounds.height = h;
    pb->min_val = min_val;
    pb->max_val = max_val;
    pb->current_val = cur_val;
    pb->fill_color = 0xFF007AFFU;
    pb->bg_color = 0xFF2C2C2EU;
    pb->border_color = 0xFF3A3A3CU;
}

void winios_progressbar_set_value(winios_progressbar_t* pb, uint32_t val) {
    if (!pb) return;
    pb->current_val = val;
}

void winios_progressbar_render(uint32_t* fb, uint32_t stride, const winios_progressbar_t* pb) {
    if (!fb || !pb || pb->bounds.width <= 2 || pb->bounds.height <= 2) return;
    draw_fb_rect(fb, stride, pb->bounds.x, pb->bounds.y, pb->bounds.width, pb->bounds.height, pb->bg_color, 1);
    draw_fb_rect(fb, stride, pb->bounds.x, pb->bounds.y, pb->bounds.width, pb->bounds.height, pb->border_color, 0);

    if (pb->max_val > pb->min_val && pb->current_val >= pb->min_val) {
        uint32_t val = (pb->current_val > pb->max_val) ? pb->max_val : pb->current_val;
        uint32_t range = pb->max_val - pb->min_val;
        uint32_t avail_w = pb->bounds.width - 2;
        uint32_t fill_w = (avail_w * (val - pb->min_val)) / range;
        if (fill_w > 0) {
            draw_fb_rect(fb, stride, pb->bounds.x + 1, pb->bounds.y + 1, fill_w, pb->bounds.height - 2, pb->fill_color, 1);
        }
    }
}

// -----------------------------------------------------------------------------
// System Clipboard API Implementation
// -----------------------------------------------------------------------------
int winios_clipboard_get(char* buffer, size_t max_len) {
    int64_t res = winios_syscall2(WINIOS_SYS_CLIPBOARD_GET, (uint64_t)buffer, (uint64_t)max_len);
    return (int)res;
}

int winios_clipboard_set(const char* text, size_t len) {
    int64_t res = winios_syscall2(WINIOS_SYS_CLIPBOARD_SET, (uint64_t)text, (uint64_t)len);
    return (int)res;
}


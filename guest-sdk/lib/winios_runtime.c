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
    volatile uint8_t* d = (volatile uint8_t*)dest;
    const volatile uint8_t* s = (const volatile uint8_t*)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void* winios_memmove(void* dest, const void* src, size_t n) {
    volatile uint8_t* d = (volatile uint8_t*)dest;
    const volatile uint8_t* s = (const volatile uint8_t*)src;

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
    volatile uint8_t* p = (volatile uint8_t*)s;
    const uint8_t val = (uint8_t)c;
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
#ifndef WINIOS_WINDOW_TEST_OVERRIDE
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
#endif

#ifndef WINIOS_HOST_TEST_OVERRIDE
int32_t winios_surface_fill(winios_wid_t wid, uint32_t x, uint32_t y, uint32_t width, uint32_t height, winios_color_t color) {
    return (int32_t)winios_syscall6(WINIOS_SYS_SURFACE_FILL, (uint64_t)wid, (uint64_t)x, (uint64_t)y, (uint64_t)width, (uint64_t)height, (uint64_t)color);
}

int32_t winios_surface_update(winios_wid_t wid, const void* pixels, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    return (int32_t)winios_syscall6(WINIOS_SYS_SURFACE_UPDATE, (uint64_t)wid, (uint64_t)pixels, (uint64_t)x, (uint64_t)y, (uint64_t)width, (uint64_t)height);
}
#endif

#ifndef WINIOS_WINDOW_TEST_OVERRIDE
int32_t winios_surface_present(winios_wid_t wid) {
    return (int32_t)winios_syscall1(WINIOS_SYS_SURFACE_PRESENT, (uint64_t)wid);
}
#endif

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

// -----------------------------------------------------------------------------
// Application Framework v2 Implementation
// -----------------------------------------------------------------------------
int winios_app_init(winios_app_t* app, const winios_app_config_t* config) {
    if (!app || !config) return -1;
    winios_memset(app, 0, sizeof(*app));
    app->config = *config;
    app->width = config->window_width ? config->window_width : 400;
    app->height = config->window_height ? config->window_height : 300;
    app->needs_redraw = 1;
    app->is_running = 0;
    return 0;
}

int winios_app_run(winios_app_t* app) {
    if (!app) return -1;
    
    // 1. Create main window
    const char* title = app->config.title ? app->config.title : "WinIOS Application";
    app->main_window_id = winios_window_create(title, app->config.window_x, app->config.window_y, app->width, app->height);
    if (app->main_window_id == 0) {
        winios_log_error("APP", "Failed to create main application window");
        return -1;
    }

    app->is_running = 1;
    app->is_foreground = 1;

    // 2. Initial lifecycle callback
    if (app->config.on_init) {
        app->config.on_init(app, app->config.user_data);
    }

    // 3. Main event & presentation loop
    while (app->is_running) {
        winios_event_v2_t evt;
        int has_event = 0;

        // Non-blocking poll for incoming events
        while (winios_poll_event_v2(&evt) > 0) {
            has_event = 1;
            if (evt.type == WINIOS_EVENT_V2_WINDOW_CLOSE_REQUEST) {
                app->is_running = 0;
                break;
            }

            if (app->config.on_event && !app->is_dispatching) {
                app->is_dispatching = 1;
                if (app->config.on_event(app, &evt, app->config.user_data)) {
                    app->needs_redraw = 1;
                }
                app->is_dispatching = 0;
            }
        }

        if (!app->is_running) break;

        // Periodic update callback
        if (app->config.on_update) {
            app->config.on_update(app, 16666, app->config.user_data);
        }

        // Render pass if invalidated
        if (app->needs_redraw) {
            if (app->config.on_render) {
                app->config.on_render(app, NULL, app->width, app->config.user_data);
            }
            winios_surface_present(app->main_window_id);
            app->needs_redraw = 0;
        }

        // Relinquish remaining timeslice if idle
        if (!has_event) {
            winios_sched_yield();
        }
    }

    // 4. Shutdown callback
    if (app->config.on_shutdown) {
        app->config.on_shutdown(app, app->config.user_data);
    }

    // 5. Cleanup window
    winios_window_destroy(app->main_window_id);
    app->main_window_id = 0;
    return app->exit_code;
}

void winios_app_request_exit(winios_app_t* app, int32_t exit_code) {
    if (!app) return;
    app->is_running = 0;
    app->exit_code = exit_code;
}

void winios_app_invalidate(winios_app_t* app) {
    if (!app) return;
    app->needs_redraw = 1;
}

winios_wid_t winios_app_get_window_id(const winios_app_t* app) {
    return app ? app->main_window_id : 0;
}

// -----------------------------------------------------------------------------
// Layout Engine v1 Implementation
// -----------------------------------------------------------------------------
void winios_layout_init_node(winios_layout_node_t* node, winios_layout_type_t type) {
    if (!node) return;
    winios_memset(node, 0, sizeof(*node));
    node->type = type;
    node->align_items = WINIOS_ALIGN_START;
}

int winios_layout_add_child(winios_layout_node_t* parent, winios_layout_node_t* child) {
    if (!parent || !child || parent->child_count >= WINIOS_LAYOUT_MAX_CHILDREN) return 0;
    parent->children[parent->child_count++] = child;
    return 1;
}

void winios_layout_set_padding(winios_layout_node_t* node, int32_t top, int32_t bottom, int32_t left, int32_t right) {
    if (!node) return;
    node->padding.top = top;
    node->padding.bottom = bottom;
    node->padding.left = left;
    node->padding.right = right;
}

void winios_layout_set_margin(winios_layout_node_t* node, int32_t top, int32_t bottom, int32_t left, int32_t right) {
    if (!node) return;
    node->margin.top = top;
    node->margin.bottom = bottom;
    node->margin.left = left;
    node->margin.right = right;
}

void winios_layout_calculate(winios_layout_node_t* root, winios_rect_t container_bounds) {
    if (!root) return;
    root->bounds = container_bounds;

    int32_t in_x = container_bounds.x + root->margin.left + root->padding.left;
    int32_t in_y = container_bounds.y + root->margin.top + root->padding.top;
    int32_t in_w = (int32_t)container_bounds.width - (root->margin.left + root->margin.right + root->padding.left + root->padding.right);
    int32_t in_h = (int32_t)container_bounds.height - (root->margin.top + root->margin.bottom + root->padding.top + root->padding.bottom);
    if (in_w < 0) in_w = 0;
    if (in_h < 0) in_h = 0;

    if (root->child_count == 0) return;

    if (root->type == WINIOS_LAYOUT_COLUMN) {
        int32_t fixed_height_sum = 0;
        uint32_t total_flex = 0;
        int32_t total_spacing = (int32_t)(root->child_count > 1 ? (root->child_count - 1) * root->spacing : 0);

        for (uint32_t i = 0; i < root->child_count; ++i) {
            winios_layout_node_t* ch = root->children[i];
            if (ch->flex_weight > 0) {
                total_flex += ch->flex_weight;
            } else {
                fixed_height_sum += (ch->bounds.height > 0 ? (int32_t)ch->bounds.height : (int32_t)ch->min_height);
            }
        }

        int32_t avail_for_flex = in_h - fixed_height_sum - total_spacing;
        if (avail_for_flex < 0) avail_for_flex = 0;

        int32_t cur_y = in_y;
        for (uint32_t i = 0; i < root->child_count; ++i) {
            winios_layout_node_t* ch = root->children[i];
            int32_t ch_h = 0;
            if (ch->flex_weight > 0 && total_flex > 0) {
                ch_h = (avail_for_flex * (int32_t)ch->flex_weight) / (int32_t)total_flex;
            } else {
                ch_h = (ch->bounds.height > 0 ? (int32_t)ch->bounds.height : (int32_t)ch->min_height);
            }

            int32_t ch_w = in_w;
            int32_t ch_x = in_x;
            if (root->align_items == WINIOS_ALIGN_CENTER && ch->bounds.width > 0 && (int32_t)ch->bounds.width < in_w) {
                ch_w = (int32_t)ch->bounds.width;
                ch_x = in_x + (in_w - ch_w) / 2;
            } else if (root->align_items == WINIOS_ALIGN_END && ch->bounds.width > 0 && (int32_t)ch->bounds.width < in_w) {
                ch_w = (int32_t)ch->bounds.width;
                ch_x = in_x + in_w - ch_w;
            } else if (ch->bounds.width > 0 && root->align_items != WINIOS_ALIGN_STRETCH) {
                ch_w = (int32_t)ch->bounds.width;
            }

            winios_rect_t ch_bounds;
            ch_bounds.x = (uint32_t)(ch_x > 0 ? ch_x : 0);
            ch_bounds.y = (uint32_t)(cur_y > 0 ? cur_y : 0);
            ch_bounds.width = (uint32_t)(ch_w > 0 ? ch_w : 0);
            ch_bounds.height = (uint32_t)(ch_h > 0 ? ch_h : 0);
            winios_layout_calculate(ch, ch_bounds);
            cur_y += ch_h + root->spacing;
        }
    } else if (root->type == WINIOS_LAYOUT_ROW) {
        int32_t fixed_width_sum = 0;
        uint32_t total_flex = 0;
        int32_t total_spacing = (int32_t)(root->child_count > 1 ? (root->child_count - 1) * root->spacing : 0);

        for (uint32_t i = 0; i < root->child_count; ++i) {
            winios_layout_node_t* ch = root->children[i];
            if (ch->flex_weight > 0) {
                total_flex += ch->flex_weight;
            } else {
                fixed_width_sum += (ch->bounds.width > 0 ? (int32_t)ch->bounds.width : (int32_t)ch->min_width);
            }
        }

        int32_t avail_for_flex = in_w - fixed_width_sum - total_spacing;
        if (avail_for_flex < 0) avail_for_flex = 0;

        int32_t cur_x = in_x;
        for (uint32_t i = 0; i < root->child_count; ++i) {
            winios_layout_node_t* ch = root->children[i];
            int32_t ch_w = 0;
            if (ch->flex_weight > 0 && total_flex > 0) {
                ch_w = (avail_for_flex * (int32_t)ch->flex_weight) / (int32_t)total_flex;
            } else {
                ch_w = (ch->bounds.width > 0 ? (int32_t)ch->bounds.width : (int32_t)ch->min_width);
            }

            int32_t ch_h = in_h;
            int32_t ch_y = in_y;
            if (root->align_items == WINIOS_ALIGN_CENTER && ch->bounds.height > 0 && (int32_t)ch->bounds.height < in_h) {
                ch_h = (int32_t)ch->bounds.height;
                ch_y = in_y + (in_h - ch_h) / 2;
            } else if (ch->bounds.height > 0 && root->align_items != WINIOS_ALIGN_STRETCH) {
                ch_h = (int32_t)ch->bounds.height;
            }

            winios_rect_t ch_bounds;
            ch_bounds.x = (uint32_t)(cur_x > 0 ? cur_x : 0);
            ch_bounds.y = (uint32_t)(ch_y > 0 ? ch_y : 0);
            ch_bounds.width = (uint32_t)(ch_w > 0 ? ch_w : 0);
            ch_bounds.height = (uint32_t)(ch_h > 0 ? ch_h : 0);
            winios_layout_calculate(ch, ch_bounds);
            cur_x += ch_w + root->spacing;
        }
    } else {
        // Absolute: Calculate each child within available bounds
        for (uint32_t i = 0; i < root->child_count; ++i) {
            winios_layout_node_t* ch = root->children[i];
            winios_layout_calculate(ch, ch->bounds);
        }
    }
}

// -----------------------------------------------------------------------------
// UI TextArea Component Implementation
// -----------------------------------------------------------------------------
void winios_textarea_init(winios_textarea_t* ta, uint32_t x, uint32_t y, uint32_t w, uint32_t h, char* buffer, size_t capacity) {
    if (!ta) return;
    winios_memset(ta, 0, sizeof(*ta));
    ta->bounds.x = (int32_t)x;
    ta->bounds.y = (int32_t)y;
    ta->bounds.width = w;
    ta->bounds.height = h;
    ta->buffer = buffer;
    ta->capacity = capacity;
    if (buffer && capacity > 0) {
        ta->length = winios_strlen(buffer);
        ta->cursor_pos = ta->length;
    }
    ta->bg_color = 0xFFFFFFFFU;
    ta->text_color = 0xFF1C1C1EU;
    ta->border_color = 0xFFC7C7CCU;
    ta->sel_color = 0xFFB3D7FFU;
}

int winios_textarea_handle_event(winios_textarea_t* ta, const winios_event_v2_t* evt) {
    if (!ta || !evt) return 0;

    if (evt->type == WINIOS_EVENT_V2_MOUSE_DOWN || evt->type == WINIOS_EVENT_V2_TOUCH_BEGIN) {
        int inside = point_in_rect(evt->x, evt->y, &ta->bounds);
        ta->is_focused = (uint8_t)inside;
        if (inside) {
            ta->sel_start = ta->sel_end = 0; // Clear selection
            return 1;
        }
        return 0;
    }

    if (!ta->is_focused) return 0;

    if (evt->type == WINIOS_EVENT_V2_KEY_CHAR) {
        uint32_t ch = evt->key_code;
        if (ch == 8) { // Backspace
            if (ta->cursor_pos > 0 && ta->length > 0) {
                winios_memmove(&ta->buffer[ta->cursor_pos - 1], &ta->buffer[ta->cursor_pos], ta->length - ta->cursor_pos + 1);
                ta->cursor_pos--;
                ta->length--;
                return 1;
            }
        } else if (ch == 10 || ch == 13) { // Enter/Newline
            if (ta->length + 1 < ta->capacity) {
                winios_memmove(&ta->buffer[ta->cursor_pos + 1], &ta->buffer[ta->cursor_pos], ta->length - ta->cursor_pos + 1);
                ta->buffer[ta->cursor_pos] = '\n';
                ta->cursor_pos++;
                ta->length++;
                return 1;
            }
        } else if (ch >= 32 && ch <= 126) {
            if (ta->length + 1 < ta->capacity) {
                winios_memmove(&ta->buffer[ta->cursor_pos + 1], &ta->buffer[ta->cursor_pos], ta->length - ta->cursor_pos + 1);
                ta->buffer[ta->cursor_pos] = (char)ch;
                ta->cursor_pos++;
                ta->length++;
                return 1;
            }
        }
    } else if (evt->type == WINIOS_EVENT_V2_KEY_DOWN) {
        if (evt->key_code == 37) { // Left arrow
            if (ta->cursor_pos > 0) { ta->cursor_pos--; return 1; }
        } else if (evt->key_code == 39) { // Right arrow
            if (ta->cursor_pos < ta->length) { ta->cursor_pos++; return 1; }
        } else if (evt->key_code == 36) { // Home
            ta->cursor_pos = 0; return 1;
        } else if (evt->key_code == 35) { // End
            ta->cursor_pos = ta->length; return 1;
        }
    }
    return 0;
}

void winios_textarea_render(uint32_t* fb, uint32_t stride, const winios_textarea_t* ta) {
    if (!fb || !ta || ta->bounds.width <= 2 || ta->bounds.height <= 2) return;
    draw_fb_rect(fb, stride, ta->bounds.x, ta->bounds.y, ta->bounds.width, ta->bounds.height, ta->bg_color, 1);
    uint32_t border = ta->is_focused ? 0xFF007AFFU : ta->border_color;
    draw_fb_rect(fb, stride, ta->bounds.x, ta->bounds.y, ta->bounds.width, ta->bounds.height, border, 0);

    if (ta->buffer && ta->length > 0) {
        int32_t cur_x = ta->bounds.x + 6;
        int32_t cur_y = ta->bounds.y + 6;
        const char* p = ta->buffer;
        while (*p) {
            if (*p == '\n') {
                cur_y += 12;
                cur_x = ta->bounds.x + 6;
            } else {
                char temp[2] = { *p, 0 };
                draw_fb_text(fb, stride, cur_x, cur_y, temp, ta->text_color);
                cur_x += 8;
                if (cur_x + 8 > (int32_t)ta->bounds.x + (int32_t)ta->bounds.width - 6) {
                    cur_y += 12;
                    cur_x = ta->bounds.x + 6;
                }
            }
            p++;
        }
    }
}

// -----------------------------------------------------------------------------
// UI Slider Component Implementation
// -----------------------------------------------------------------------------
void winios_slider_init(winios_slider_t* sl, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int32_t min_v, int32_t max_v, int32_t cur_v) {
    if (!sl) return;
    winios_memset(sl, 0, sizeof(*sl));
    sl->bounds.x = (int32_t)x;
    sl->bounds.y = (int32_t)y;
    sl->bounds.width = w;
    sl->bounds.height = h;
    sl->min_val = min_v;
    sl->max_val = max_v;
    sl->current_val = (cur_v < min_v) ? min_v : ((cur_v > max_v) ? max_v : cur_v);
    sl->track_color = 0xFFE5E5EAU;
    sl->fill_color = 0xFF007AFFU;
    sl->thumb_color = 0xFFFFFFFFU;
}

int winios_slider_handle_event(winios_slider_t* sl, const winios_event_v2_t* evt) {
    if (!sl || !evt) return 0;
    if (evt->type == WINIOS_EVENT_V2_MOUSE_DOWN || evt->type == WINIOS_EVENT_V2_TOUCH_BEGIN) {
        if (point_in_rect(evt->x, evt->y, &sl->bounds)) {
            sl->is_dragging = 1;
            int32_t rel_x = evt->x - sl->bounds.x;
            if (rel_x < 0) rel_x = 0;
            if (rel_x > (int32_t)sl->bounds.width) rel_x = (int32_t)sl->bounds.width;
            int32_t range = sl->max_val - sl->min_val;
            if (range > 0 && sl->bounds.width > 0) {
                sl->current_val = sl->min_val + (rel_x * range) / (int32_t)sl->bounds.width;
            }
            return 1;
        }
    } else if (evt->type == WINIOS_EVENT_V2_MOUSE_MOVE || evt->type == WINIOS_EVENT_V2_TOUCH_MOVE) {
        if (sl->is_dragging) {
            int32_t rel_x = evt->x - sl->bounds.x;
            if (rel_x < 0) rel_x = 0;
            if (rel_x > (int32_t)sl->bounds.width) rel_x = (int32_t)sl->bounds.width;
            int32_t range = sl->max_val - sl->min_val;
            if (range > 0 && sl->bounds.width > 0) {
                sl->current_val = sl->min_val + (rel_x * range) / (int32_t)sl->bounds.width;
            }
            return 1;
        }
    } else if (evt->type == WINIOS_EVENT_V2_MOUSE_UP || evt->type == WINIOS_EVENT_V2_TOUCH_END) {
        if (sl->is_dragging) {
            sl->is_dragging = 0;
            return 1;
        }
    }
    return 0;
}

void winios_slider_render(uint32_t* fb, uint32_t stride, const winios_slider_t* sl) {
    if (!fb || !sl || sl->bounds.width <= 4 || sl->bounds.height <= 4) return;
    int32_t track_y = sl->bounds.y + (int32_t)sl->bounds.height / 2 - 2;
    draw_fb_rect(fb, stride, sl->bounds.x, track_y, sl->bounds.width, 4, sl->track_color, 1);

    int32_t range = sl->max_val - sl->min_val;
    int32_t fill_w = 0;
    if (range > 0) {
        fill_w = ((sl->current_val - sl->min_val) * (int32_t)sl->bounds.width) / range;
        if (fill_w > 0) {
            draw_fb_rect(fb, stride, sl->bounds.x, track_y, (uint32_t)fill_w, 4, sl->fill_color, 1);
        }
    }

    // Thumb circle/box
    int32_t thumb_x = sl->bounds.x + fill_w - 6;
    if (thumb_x < (int32_t)sl->bounds.x) thumb_x = (int32_t)sl->bounds.x;
    if (thumb_x > (int32_t)sl->bounds.x + (int32_t)sl->bounds.width - 12) thumb_x = (int32_t)sl->bounds.x + (int32_t)sl->bounds.width - 12;
    int32_t thumb_y = sl->bounds.y + (int32_t)sl->bounds.height / 2 - 6;
    draw_fb_rect(fb, stride, thumb_x, thumb_y, 12, 12, sl->thumb_color, 1);
    draw_fb_rect(fb, stride, thumb_x, thumb_y, 12, 12, 0xFF8E8E93U, 0);
}

// -----------------------------------------------------------------------------
// UI Dialog Component Implementation
// -----------------------------------------------------------------------------
void winios_dialog_init(winios_dialog_t* dlg, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* title, const char* msg, int has_cancel) {
    if (!dlg) return;
    winios_memset(dlg, 0, sizeof(*dlg));
    dlg->bounds.x = (int32_t)x;
    dlg->bounds.y = (int32_t)y;
    dlg->bounds.width = w;
    dlg->bounds.height = h;
    dlg->title = title;
    dlg->message = msg;
    dlg->ok_label = "OK";
    dlg->cancel_label = "Cancel";
    dlg->is_visible = 1;
    dlg->has_cancel = (uint8_t)has_cancel;
    dlg->result = WINIOS_DIALOG_NONE;
}

int winios_dialog_handle_event(winios_dialog_t* dlg, const winios_event_v2_t* evt) {
    if (!dlg || !dlg->is_visible || !evt) return 0;
    if (evt->type == WINIOS_EVENT_V2_MOUSE_UP || evt->type == WINIOS_EVENT_V2_TOUCH_END) {
        int32_t btn_y = dlg->bounds.y + (int32_t)dlg->bounds.height - 38;
        // OK Button: bounds at right
        int32_t ok_x = dlg->bounds.x + (int32_t)dlg->bounds.width - 80;
        winios_rect_t ok_r;
        ok_r.x = (uint32_t)(ok_x > 0 ? ok_x : 0);
        ok_r.y = (uint32_t)(btn_y > 0 ? btn_y : 0);
        ok_r.width = 70;
        ok_r.height = 28;
        if (point_in_rect(evt->x, evt->y, &ok_r)) {
            dlg->result = WINIOS_DIALOG_OK;
            dlg->is_visible = 0;
            return 1;
        }
        if (dlg->has_cancel) {
            int32_t cancel_x = ok_x - 80;
            winios_rect_t cancel_r;
            cancel_r.x = (uint32_t)(cancel_x > 0 ? cancel_x : 0);
            cancel_r.y = (uint32_t)(btn_y > 0 ? btn_y : 0);
            cancel_r.width = 70;
            cancel_r.height = 28;
            if (point_in_rect(evt->x, evt->y, &cancel_r)) {
                dlg->result = WINIOS_DIALOG_CANCEL;
                dlg->is_visible = 0;
                return 1;
            }
        }
    }
    return 0;
}

void winios_dialog_render(uint32_t* fb, uint32_t stride, const winios_dialog_t* dlg) {
    if (!fb || !dlg || !dlg->is_visible) return;
    // Dialog backdrop panel
    draw_fb_rect(fb, stride, dlg->bounds.x, dlg->bounds.y, dlg->bounds.width, dlg->bounds.height, 0xFFF2F2F7U, 1);
    draw_fb_rect(fb, stride, dlg->bounds.x, dlg->bounds.y, dlg->bounds.width, dlg->bounds.height, 0xFF8E8E93U, 0);

    // Title & Message
    if (dlg->title) {
        draw_fb_text(fb, stride, dlg->bounds.x + 16, dlg->bounds.y + 14, dlg->title, 0xFF000000U);
    }
    if (dlg->message) {
        draw_fb_text(fb, stride, dlg->bounds.x + 16, dlg->bounds.y + 40, dlg->message, 0xFF3C3C43U);
    }

    int32_t btn_y = dlg->bounds.y + (int32_t)dlg->bounds.height - 38;
    int32_t ok_x = dlg->bounds.x + (int32_t)dlg->bounds.width - 80;
    draw_fb_rect(fb, stride, ok_x, btn_y, 70, 28, 0xFF007AFFU, 1);
    draw_fb_text(fb, stride, ok_x + 24, btn_y + 8, dlg->ok_label ? dlg->ok_label : "OK", 0xFFFFFFFFU);

    if (dlg->has_cancel) {
        int32_t cancel_x = ok_x - 80;
        draw_fb_rect(fb, stride, cancel_x, btn_y, 70, 28, 0xFFE5E5EAU, 1);
        draw_fb_text(fb, stride, cancel_x + 12, btn_y + 8, dlg->cancel_label ? dlg->cancel_label : "Cancel", 0xFF000000U);
    }
}

// -----------------------------------------------------------------------------
// UI Menu Component Implementation
// -----------------------------------------------------------------------------
void winios_menu_init(winios_menu_t* menu, uint32_t x, uint32_t y, uint32_t w) {
    if (!menu) return;
    winios_memset(menu, 0, sizeof(*menu));
    menu->bounds.x = (int32_t)x;
    menu->bounds.y = (int32_t)y;
    menu->bounds.width = w;
    menu->bounds.height = 4;
    menu->selected_action = -1;
    menu->hover_index = -1;
    menu->is_open = 1;
}

int winios_menu_add_item(winios_menu_t* menu, const char* label, int32_t action_id) {
    if (!menu || menu->item_count >= WINIOS_MENU_MAX_ITEMS) return 0;
    menu->items[menu->item_count].label = label;
    menu->items[menu->item_count].action_id = action_id;
    menu->items[menu->item_count].is_enabled = 1;
    menu->item_count++;
    menu->bounds.height = menu->item_count * 24 + 4;
    return 1;
}

int winios_menu_handle_event(winios_menu_t* menu, const winios_event_v2_t* evt) {
    if (!menu || !menu->is_open || !evt) return 0;
    if (point_in_rect(evt->x, evt->y, &menu->bounds)) {
        int32_t idx = (evt->y - menu->bounds.y - 2) / 24;
        if (idx >= 0 && idx < (int32_t)menu->item_count) {
            menu->hover_index = idx;
            if (evt->type == WINIOS_EVENT_V2_MOUSE_UP || evt->type == WINIOS_EVENT_V2_TOUCH_END) {
                if (menu->items[idx].is_enabled) {
                    menu->selected_action = menu->items[idx].action_id;
                    menu->is_open = 0;
                    return 1;
                }
            }
        }
    }
    return 0;
}

void winios_menu_render(uint32_t* fb, uint32_t stride, const winios_menu_t* menu) {
    if (!fb || !menu || !menu->is_open) return;
    draw_fb_rect(fb, stride, menu->bounds.x, menu->bounds.y, menu->bounds.width, menu->bounds.height, 0xFFFFFFFFU, 1);
    draw_fb_rect(fb, stride, menu->bounds.x, menu->bounds.y, menu->bounds.width, menu->bounds.height, 0xFFD1D1D6U, 0);

    for (uint32_t i = 0; i < menu->item_count; ++i) {
        int32_t item_y = menu->bounds.y + 2 + (int32_t)i * 24;
        if ((int32_t)i == menu->hover_index) {
            draw_fb_rect(fb, stride, menu->bounds.x + 1, item_y, menu->bounds.width - 2, 24, 0xFF007AFFU, 1);
            draw_fb_text(fb, stride, menu->bounds.x + 8, item_y + 6, menu->items[i].label, 0xFFFFFFFFU);
        } else {
            draw_fb_text(fb, stride, menu->bounds.x + 8, item_y + 6, menu->items[i].label, 0xFF1C1C1EU);
        }
    }
}

// -----------------------------------------------------------------------------
// UI Focus Manager Implementation
// -----------------------------------------------------------------------------
void winios_focus_init(winios_focus_manager_t* fm) {
    if (!fm) return;
    winios_memset(fm, 0, sizeof(*fm));
}

int winios_focus_register(winios_focus_manager_t* fm, void* control, uint8_t type) {
    if (!fm || !control || fm->count >= WINIOS_FOCUS_MAX_ITEMS) return 0;
    fm->controls[fm->count] = control;
    fm->types[fm->count] = type;
    fm->count++;
    return 1;
}

void winios_focus_next(winios_focus_manager_t* fm) {
    if (!fm || fm->count == 0) return;
    fm->active_index = (fm->active_index + 1) % fm->count;
}

void winios_focus_prev(winios_focus_manager_t* fm) {
    if (!fm || fm->count == 0) return;
    fm->active_index = (fm->active_index + fm->count - 1) % fm->count;
}

void* winios_focus_get_active(const winios_focus_manager_t* fm) {
    if (!fm || fm->count == 0 || fm->active_index >= fm->count) return NULL;
    return fm->controls[fm->active_index];
}

// -----------------------------------------------------------------------------
// SDK v1.8 Configuration API Implementation
// -----------------------------------------------------------------------------
int winios_config_get_string(const char* key, char* out_buf, size_t max_len, const char* default_val) {
    if (!key || !out_buf || max_len == 0) return -1;
    winios_config_req_t req;
    winios_memset(&req, 0, sizeof(req));
    req.key_ptr = (uint64_t)(uintptr_t)key;
    req.key_len = (uint32_t)winios_strlen(key);
    req.val_type = 0; // String
    req.val_buf_ptr = (uint64_t)(uintptr_t)out_buf;
    req.val_buf_size = (uint32_t)max_len;

    int64_t ret = (int64_t)winios_syscall1(WINIOS_SYS_CONFIG_GET, (uint64_t)(uintptr_t)&req);
    if (ret != 0 || req.out_len == 0) {
        if (default_val) {
            size_t dlen = winios_strlen(default_val);
            if (dlen >= max_len) dlen = max_len - 1;
            winios_memcpy(out_buf, default_val, dlen);
            out_buf[dlen] = '\0';
        } else {
            out_buf[0] = '\0';
        }
        return -1;
    }
    return 0;
}

int winios_config_set_string(const char* key, const char* val) {
    if (!key || !val) return -1;
    winios_config_req_t req;
    winios_memset(&req, 0, sizeof(req));
    req.key_ptr = (uint64_t)(uintptr_t)key;
    req.key_len = (uint32_t)winios_strlen(key);
    req.val_type = 0;
    req.val_buf_ptr = (uint64_t)(uintptr_t)val;
    req.val_buf_size = (uint32_t)winios_strlen(val);
    return (int)winios_syscall1(WINIOS_SYS_CONFIG_SET, (uint64_t)(uintptr_t)&req);
}

int64_t winios_config_get_int(const char* key, int64_t default_val) {
    if (!key) return default_val;
    int64_t val = 0;
    winios_config_req_t req;
    winios_memset(&req, 0, sizeof(req));
    req.key_ptr = (uint64_t)(uintptr_t)key;
    req.key_len = (uint32_t)winios_strlen(key);
    req.val_type = 1; // Int64
    req.val_buf_ptr = (uint64_t)(uintptr_t)&val;
    req.val_buf_size = sizeof(val);

    int64_t ret = (int64_t)winios_syscall1(WINIOS_SYS_CONFIG_GET, (uint64_t)(uintptr_t)&req);
    if (ret != 0 || req.out_len == 0) return default_val;
    return val;
}

int winios_config_set_int(const char* key, int64_t val) {
    if (!key) return -1;
    winios_config_req_t req;
    winios_memset(&req, 0, sizeof(req));
    req.key_ptr = (uint64_t)(uintptr_t)key;
    req.key_len = (uint32_t)winios_strlen(key);
    req.val_type = 1;
    req.val_buf_ptr = (uint64_t)(uintptr_t)&val;
    req.val_buf_size = sizeof(val);
    return (int)winios_syscall1(WINIOS_SYS_CONFIG_SET, (uint64_t)(uintptr_t)&req);
}

double winios_config_get_double(const char* key, double default_val) {
    if (!key) return default_val;
    double val = 0.0;
    winios_config_req_t req;
    winios_memset(&req, 0, sizeof(req));
    req.key_ptr = (uint64_t)(uintptr_t)key;
    req.key_len = (uint32_t)winios_strlen(key);
    req.val_type = 2; // Double
    req.val_buf_ptr = (uint64_t)(uintptr_t)&val;
    req.val_buf_size = sizeof(val);

    int64_t ret = (int64_t)winios_syscall1(WINIOS_SYS_CONFIG_GET, (uint64_t)(uintptr_t)&req);
    if (ret != 0 || req.out_len == 0) return default_val;
    return val;
}

int winios_config_set_double(const char* key, double val) {
    if (!key) return -1;
    winios_config_req_t req;
    winios_memset(&req, 0, sizeof(req));
    req.key_ptr = (uint64_t)(uintptr_t)key;
    req.key_len = (uint32_t)winios_strlen(key);
    req.val_type = 2;
    req.val_buf_ptr = (uint64_t)(uintptr_t)&val;
    req.val_buf_size = sizeof(val);
    return (int)winios_syscall1(WINIOS_SYS_CONFIG_SET, (uint64_t)(uintptr_t)&req);
}

int winios_config_get_bool(const char* key, int default_val) {
    if (!key) return default_val;
    uint32_t val = 0;
    winios_config_req_t req;
    winios_memset(&req, 0, sizeof(req));
    req.key_ptr = (uint64_t)(uintptr_t)key;
    req.key_len = (uint32_t)winios_strlen(key);
    req.val_type = 3; // Bool
    req.val_buf_ptr = (uint64_t)(uintptr_t)&val;
    req.val_buf_size = sizeof(val);

    int64_t ret = (int64_t)winios_syscall1(WINIOS_SYS_CONFIG_GET, (uint64_t)(uintptr_t)&req);
    if (ret != 0 || req.out_len == 0) return default_val;
    return val != 0 ? 1 : 0;
}

int winios_config_set_bool(const char* key, int val) {
    if (!key) return -1;
    uint32_t bval = val ? 1 : 0;
    winios_config_req_t req;
    winios_memset(&req, 0, sizeof(req));
    req.key_ptr = (uint64_t)(uintptr_t)key;
    req.key_len = (uint32_t)winios_strlen(key);
    req.val_type = 3;
    req.val_buf_ptr = (uint64_t)(uintptr_t)&bval;
    req.val_buf_size = sizeof(bval);
    return (int)winios_syscall1(WINIOS_SYS_CONFIG_SET, (uint64_t)(uintptr_t)&req);
}

// -----------------------------------------------------------------------------
// SDK v1.8 Diagnostics API Implementation
// -----------------------------------------------------------------------------
int winios_diagnostics_get_summary(winios_platform_diagnostics_t* out_summary) {
    if (!out_summary) return -1;
    return (int)winios_syscall1(WINIOS_SYS_DIAG_SUMMARY, (uint64_t)(uintptr_t)out_summary);
}

// -----------------------------------------------------------------------------
// SDK v1.8 IPC v2 API Implementation
// -----------------------------------------------------------------------------
uint64_t winios_ipc_port_create_v2(const char* name) {
    uint64_t name_ptr = name ? (uint64_t)(uintptr_t)name : 0;
    return (uint64_t)winios_syscall1(WINIOS_SYS_IPC_PORT_CREATE_V2, name_ptr);
}

int32_t winios_ipc_send_v2(uint64_t target_port, uint32_t message_id, const void* data, size_t size) {
    winios_ipc_msg_v2_t msg;
    winios_memset(&msg, 0, sizeof(msg));
    msg.target_port = target_port;
    msg.message_id = message_id;
    msg.data_ptr = (uint64_t)(uintptr_t)data;
    msg.data_size = (uint32_t)size;
    return (int32_t)winios_syscall1(WINIOS_SYS_IPC_SEND_V2, (uint64_t)(uintptr_t)&msg);
}

int32_t winios_ipc_recv_v2(uint64_t port_handle, void* buf, size_t buf_size, uint32_t* out_sender_pid, uint32_t* out_msg_id) {
    winios_ipc_msg_v2_t desc;
    winios_memset(&desc, 0, sizeof(desc));
    int64_t ret = (int64_t)winios_syscall4(WINIOS_SYS_IPC_RECV_V2, port_handle, (uint64_t)(uintptr_t)buf, (uint64_t)buf_size, (uint64_t)(uintptr_t)&desc);
    if (ret <= 0) return 0;
    if (out_sender_pid) *out_sender_pid = desc.sender_pid;
    if (out_msg_id) *out_msg_id = desc.message_id;
    return (int32_t)ret;
}

uint64_t winios_ipc_lookup_port(const char* name) {
    if (!name) return 0;
    return (uint64_t)winios_syscall1(WINIOS_SYS_IPC_LOOKUP_PORT, (uint64_t)(uintptr_t)name);
}

int32_t winios_ipc_register_port(uint64_t port_handle, const char* name) {
    if (!name) return -1;
    return (int32_t)winios_syscall2(WINIOS_SYS_IPC_REGISTER_PORT, port_handle, (uint64_t)(uintptr_t)name);
}

// -----------------------------------------------------------------------------
// SDK v1.9 Device & Hardware Abstraction Services
// -----------------------------------------------------------------------------
int32_t winios_device_enum(winios_device_info_t* buf, uint32_t max_count, uint32_t class_filter, uint32_t* out_count) {
    if (!buf || max_count == 0) return -1;
    return (int32_t)winios_syscall4(WINIOS_SYS_DEVICE_ENUM,
                                   (uint64_t)(uintptr_t)buf,
                                   (uint64_t)max_count,
                                   (uint64_t)class_filter,
                                   (uint64_t)(uintptr_t)out_count);
}

int32_t winios_device_get_info(uint64_t handle_or_id, winios_device_info_t* out_info) {
    if (!out_info) return -1;
    return (int32_t)winios_syscall2(WINIOS_SYS_DEVICE_GET_INFO,
                                   handle_or_id,
                                   (uint64_t)(uintptr_t)out_info);
}

uint64_t winios_device_open(uint32_t device_id, uint32_t flags) {
    return (uint64_t)winios_syscall2(WINIOS_SYS_DEVICE_OPEN, (uint64_t)device_id, (uint64_t)flags);
}

int32_t winios_device_close(uint64_t handle) {
    return (int32_t)winios_syscall1(WINIOS_SYS_DEVICE_CLOSE, handle);
}

int32_t winios_display_get_config(winios_display_config_t* out_cfg) {
    if (!out_cfg) return -1;
    return (int32_t)winios_syscall1(WINIOS_SYS_DISPLAY_GET_CONFIG, (uint64_t)(uintptr_t)out_cfg);
}

int32_t winios_display_set_config(const winios_display_config_t* in_cfg) {
    if (!in_cfg) return -1;
    return (int32_t)winios_syscall1(WINIOS_SYS_DISPLAY_SET_CONFIG, (uint64_t)(uintptr_t)in_cfg);
}

int32_t winios_audio_submit(const void* pcm_data, size_t size_bytes) {
    if (!pcm_data || size_bytes == 0) return -1;
    return (int32_t)winios_syscall2(WINIOS_SYS_AUDIO_SUBMIT,
                                   (uint64_t)(uintptr_t)pcm_data,
                                   (uint64_t)size_bytes);
}

int32_t winios_audio_get_status(winios_audio_status_t* out_status) {
    if (!out_status) return -1;
    return (int32_t)winios_syscall1(WINIOS_SYS_AUDIO_GET_STATUS, (uint64_t)(uintptr_t)out_status);
}

int32_t winios_sensor_sample(uint32_t sensor_type, winios_sensor_sample_t* out_sample) {
    if (!out_sample) return -1;
    return (int32_t)winios_syscall2(WINIOS_SYS_SENSOR_SAMPLE,
                                   (uint64_t)sensor_type,
                                   (uint64_t)(uintptr_t)out_sample);
}

int32_t winios_sensor_set_enabled(uint32_t sensor_type, int32_t enabled) {
    return (int32_t)winios_syscall2(WINIOS_SYS_SENSOR_SET_ENABLED,
                                   (uint64_t)sensor_type,
                                   (uint64_t)enabled);
}

int32_t winios_net_send(const void* data, size_t length) {
    if (!data || length == 0) return -1;
    return (int32_t)winios_syscall2(WINIOS_SYS_NET_SEND,
                                   (uint64_t)(uintptr_t)data,
                                   (uint64_t)length);
}

int64_t winios_net_recv(void* buf, size_t max_len, size_t* out_len) {
    if (!buf || max_len == 0) return -1;
    return (int64_t)winios_syscall3(WINIOS_SYS_NET_RECV,
                                   (uint64_t)(uintptr_t)buf,
                                   (uint64_t)max_len,
                                   (uint64_t)(uintptr_t)out_len);
}

int32_t winios_time_get(winios_timespec_t* out_spec) {
    if (!out_spec) return -1;
    return (int32_t)winios_syscall1(WINIOS_SYS_TIME_GET, (uint64_t)(uintptr_t)out_spec);
}

uint32_t winios_notification_post(const char* title, const char* body, const char* category, uint32_t duration_ms) {
    if (!title || !body) return 0;
    return (uint32_t)winios_syscall4(WINIOS_SYS_NOTIFICATION_POST,
                                    (uint64_t)(uintptr_t)title,
                                    (uint64_t)(uintptr_t)body,
                                    (uint64_t)(uintptr_t)category,
                                    (uint64_t)duration_ms);
}

int32_t winios_device_diag_summary(winios_device_diag_t* out_diag) {
    if (!out_diag) return -1;
    return (int32_t)winios_syscall1(WINIOS_SYS_DEVICE_DIAG_SUMMARY, (uint64_t)(uintptr_t)out_diag);
}




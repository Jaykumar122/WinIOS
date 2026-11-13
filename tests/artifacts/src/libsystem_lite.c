/*
 * libsystem_lite.c - Original Minimal Darwin C Runtime / libSystem-Lite for WinIOS
 * Campaign 191-U: STAGE U2 & U3
 *
 * Compiled into tests/artifacts/bin/libSystem.B.dylib (-install_name /usr/lib/libSystem.B.dylib)
 * Provides raw SVC #0x80 BSD syscall and Mach trap wrappers, string/memory primitives,
 * mmap-backed heap allocator, formatted stdio (printf/puts/putchar/snprintf),
 * and stack protector runtime.
 *
 * NOTE: Original freestanding code; contains zero Apple/APSL source code.
 */

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;
typedef unsigned long      size_t;
typedef long               ssize_t;
typedef unsigned long long uintptr_t;

#define NULL ((void*)0)
#define PROT_READ  0x01
#define PROT_WRITE 0x02
#define MAP_PRIVATE 0x0002
#define MAP_ANON    0x1000
#define MAP_FAILED  ((void*)(-1LL))

/* -------------------------------------------------------------------------
 * Raw Darwin ARM64 BSD Syscalls (SVC #0x80)
 * ------------------------------------------------------------------------- */
void _exit(int status) {
    register int64_t x0 __asm__("x0") = status;
    register int64_t x16 __asm__("x16") = 1; /* SYS_exit */
    __asm__ volatile("svc #0x80" : : "r"(x0), "r"(x16) : "memory");
    __builtin_unreachable();
}

void exit(int status) {
    _exit(status);
}

ssize_t read(int fd, void* buf, size_t count) {
    register int64_t x0 __asm__("x0") = fd;
    register void* x1 __asm__("x1") = buf;
    register size_t x2 __asm__("x2") = count;
    register int64_t x16 __asm__("x16") = 3; /* SYS_read */
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory");
    return (ssize_t)x0;
}

ssize_t write(int fd, const void* buf, size_t count) {
    register int64_t x0 __asm__("x0") = fd;
    register const void* x1 __asm__("x1") = buf;
    register size_t x2 __asm__("x2") = count;
    register int64_t x16 __asm__("x16") = 4; /* SYS_write */
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory");
    return (ssize_t)x0;
}

int open(const char* path, int flags, ...) {
    register const char* x0 __asm__("x0") = path;
    register int64_t x1 __asm__("x1") = flags;
    register int64_t x2 __asm__("x2") = 0;
    register int64_t x16 __asm__("x16") = 5; /* SYS_open */
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory");
    return (int)(int64_t)x0;
}

int close(int fd) {
    register int64_t x0 __asm__("x0") = fd;
    register int64_t x16 __asm__("x16") = 6; /* SYS_close */
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x16) : "memory");
    return (int)x0;
}

int getpid(void) {
    register int64_t x0 __asm__("x0") = 0;
    register int64_t x16 __asm__("x16") = 20; /* SYS_getpid */
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x16) : "memory");
    return (int)x0;
}

void* mmap(void* addr, size_t len, int prot, int flags, int fd, int64_t offset) {
    register void* x0 __asm__("x0") = addr;
    register size_t x1 __asm__("x1") = len;
    register int64_t x2 __asm__("x2") = prot;
    register int64_t x3 __asm__("x3") = flags;
    register int64_t x4 __asm__("x4") = fd;
    register int64_t x5 __asm__("x5") = offset;
    register int64_t x16 __asm__("x16") = 197; /* SYS_mmap */
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x16) : "memory");
    return x0;
}

int munmap(void* addr, size_t len) {
    register void* x0 __asm__("x0") = addr;
    register size_t x1 __asm__("x1") = len;
    register int64_t x16 __asm__("x16") = 73; /* SYS_munmap */
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x16) : "memory");
    return (int)(int64_t)x0;
}

void abort(void) {
    static const char msg[] = "libSystem-Lite: abort()\n";
    write(2, msg, sizeof(msg) - 1);
    _exit(134);
}

/* -------------------------------------------------------------------------
 * Raw Darwin ARM64 Mach Traps (SVC #0x80 with negative X16, XNU syscall_sw.c)
 * ------------------------------------------------------------------------- */
uint32_t mach_reply_port(void) {
    register int64_t x0 __asm__("x0") = 0;
    register int64_t x16 __asm__("x16") = -26; /* mach_reply_port */
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x16) : "memory");
    return (uint32_t)x0;
}

uint32_t thread_self_trap(void) {
    register int64_t x0 __asm__("x0") = 0;
    register int64_t x16 __asm__("x16") = -27; /* thread_self_trap */
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x16) : "memory");
    return (uint32_t)x0;
}

uint32_t mach_thread_self(void) {
    return thread_self_trap();
}

uint32_t task_self_trap(void) {
    register int64_t x0 __asm__("x0") = 0;
    register int64_t x16 __asm__("x16") = -28; /* task_self_trap */
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x16) : "memory");
    return (uint32_t)x0;
}

uint32_t mach_task_self(void) {
    return task_self_trap();
}

__attribute__((aligned(16))) uint32_t mach_task_self_ = 0;

__attribute__((constructor))
static void _libsystem_lite_init(void) {
    mach_task_self_ = task_self_trap();
}

int mach_msg_trap(void* msg, uint32_t option, uint32_t send_size,
                  uint32_t rcv_size, uint32_t rcv_name,
                  uint32_t timeout, uint32_t notify) {
    register void* x0 __asm__("x0") = msg;
    register uint64_t x1 __asm__("x1") = option;
    register uint64_t x2 __asm__("x2") = send_size;
    register uint64_t x3 __asm__("x3") = rcv_size;
    register uint64_t x4 __asm__("x4") = rcv_name;
    register uint64_t x5 __asm__("x5") = timeout;
    register uint64_t x6 __asm__("x6") = notify;
    register int64_t x16 __asm__("x16") = -31; /* mach_msg_trap */
    __asm__ volatile("svc #0x80" : "+r"(x0)
                     : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x6), "r"(x16)
                     : "memory");
    return (int)(int64_t)x0;
}

int mach_msg(void* msg, uint32_t option, uint32_t send_size,
             uint32_t rcv_size, uint32_t rcv_name,
             uint32_t timeout, uint32_t notify) {
    return mach_msg_trap(msg, option, send_size, rcv_size, rcv_name, timeout, notify);
}

/* -------------------------------------------------------------------------
 * String & Memory Functions
 * ------------------------------------------------------------------------- */
size_t strlen(const char* s) {
    size_t len = 0;
    if (!s) return 0;
    while (s[len]) len++;
    return len;
}

int strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) { a++; b++; }
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

int strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i] || a[i] == '\0') {
            return (unsigned char)a[i] - (unsigned char)b[i];
        }
    }
    return 0;
}

char* strcpy(char* dst, const char* src) {
    char* ret = dst;
    while (*src) *dst++ = *src++;
    *dst = '\0';
    return ret;
}

char* strncpy(char* dst, const char* src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; ++i) dst[i] = src[i];
    for (; i < n; ++i) dst[i] = '\0';
    return dst;
}

void* memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s) {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; --i) d[i - 1] = s[i - 1];
    }
    return dst;
}

void* memset(void* dst, int c, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    uint8_t val = (uint8_t)c;
    for (size_t i = 0; i < n; ++i) d[i] = val;
    return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    for (size_t i = 0; i < n; ++i) {
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

void bzero(void* s, size_t n) {
    memset(s, 0, n);
}

int tolower(int c) {
    return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

int __tolower(int c) {
    return tolower(c);
}

/* -------------------------------------------------------------------------
 * Mmap-Backed Heap Allocator (16-byte aligned)
 * ------------------------------------------------------------------------- */
#define ALLOC_MAGIC 0x414C4C4F434D454DULL /* "ALLOCMEM" */

struct __attribute__((aligned(16))) AllocHeader {
    size_t size;
    size_t magic;
};

__attribute__((aligned(16))) static uint8_t* g_heap_cur = (uint8_t*)0;
__attribute__((aligned(16))) static uint8_t* g_heap_end = (uint8_t*)0;

void* malloc(size_t size) {
    if (size == 0) size = 16;
    size_t aligned_size = (size + 15ULL) & ~15ULL;
    size_t total = sizeof(struct AllocHeader) + aligned_size;

    if (total > 32768ULL) {
        /* Large allocation: dedicated mmap */
        size_t page_aligned = (total + 16383ULL) & ~16383ULL;
        void* p = mmap(NULL, page_aligned, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p == MAP_FAILED || p == NULL) return NULL;
        struct AllocHeader* hdr = (struct AllocHeader*)p;
        hdr->size = aligned_size;
        hdr->magic = ALLOC_MAGIC;
        return (void*)(hdr + 1);
    }

    if (g_heap_cur == NULL || (size_t)(g_heap_end - g_heap_cur) < total) {
        size_t chunk = 65536ULL; /* 64 KB (4 x 16KB Darwin pages) */
        void* mem = mmap(NULL, chunk, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (mem == MAP_FAILED || mem == NULL) return NULL;
        g_heap_cur = (uint8_t*)mem;
        g_heap_end = g_heap_cur + chunk;
    }

    struct AllocHeader* hdr = (struct AllocHeader*)g_heap_cur;
    hdr->size = aligned_size;
    hdr->magic = ALLOC_MAGIC;
    g_heap_cur += total;
    return (void*)(hdr + 1);
}

void free(void* ptr) {
    if (!ptr) return;
    struct AllocHeader* hdr = ((struct AllocHeader*)ptr) - 1;
    if (hdr->magic == ALLOC_MAGIC) {
        size_t total = sizeof(struct AllocHeader) + hdr->size;
        hdr->magic = 0;
        if (total > 32768ULL) {
            size_t page_aligned = (total + 16383ULL) & ~16383ULL;
            munmap((void*)hdr, page_aligned);
        }
    }
}

void* calloc(size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0) return malloc(16);
    if (nmemb > (~(size_t)0) / size) return NULL;
    size_t total = nmemb * size;
    void* p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void* realloc(void* ptr, size_t new_size) {
    if (!ptr) return malloc(new_size);
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    struct AllocHeader* hdr = ((struct AllocHeader*)ptr) - 1;
    size_t old_size = (hdr->magic == ALLOC_MAGIC) ? hdr->size : 0;
    if (old_size >= new_size) return ptr;

    void* np = malloc(new_size);
    if (np && old_size > 0) {
        memcpy(np, ptr, old_size < new_size ? old_size : new_size);
        free(ptr);
    }
    return np;
}

/* -------------------------------------------------------------------------
 * Formatted Stdio (putchar, puts, vsnprintf, snprintf, printf)
 * ------------------------------------------------------------------------- */
int putchar(int c) {
    char ch = (char)c;
    if (write(1, &ch, 1) == 1) return (unsigned char)ch;
    return -1;
}

int puts(const char* s) {
    if (!s) s = "(null)";
    size_t len = strlen(s);
    write(1, s, len);
    write(1, "\n", 1);
    return (int)len + 1;
}

static void fmt_emit_char(char* out, size_t max_len, size_t* pos, char ch) {
    if (*pos + 1 < max_len) {
        out[*pos] = ch;
    }
    (*pos)++;
}

static void fmt_emit_str(char* out, size_t max_len, size_t* pos, const char* s) {
    if (!s) s = "(null)";
    while (*s) {
        fmt_emit_char(out, max_len, pos, *s++);
    }
}

static void fmt_emit_uint(char* out, size_t max_len, size_t* pos,
                          uint64_t val, unsigned base, int uppercase,
                          int min_width, char pad_char) {
    char tmp[32];
    int idx = 0;
    const char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    if (val == 0) {
        tmp[idx++] = '0';
    } else {
        while (val > 0) {
            tmp[idx++] = digits[val % base];
            val /= base;
        }
    }
    for (int w = idx; w < min_width; ++w) {
        fmt_emit_char(out, max_len, pos, pad_char);
    }
    for (int i = idx - 1; i >= 0; --i) {
        fmt_emit_char(out, max_len, pos, tmp[i]);
    }
}

static void fmt_emit_double(char* out, size_t max_len, size_t* pos,
                            double val, char spec, int has_prec, int prec) {
    uint64_t bits = 0;
    memcpy(&bits, &val, sizeof(bits));
    int is_neg = (bits >> 63) != 0;
    uint64_t exp_bits = (bits >> 52) & 0x7FFu;
    uint64_t frac_bits = bits & 0x000FFFFFFFFFFFFFULL;

    if (exp_bits == 0x7FFu) {
        if (is_neg && frac_bits == 0) fmt_emit_char(out, max_len, pos, '-');
        fmt_emit_str(out, max_len, pos, frac_bits ? "nan" : "inf");
        return;
    }
    if (is_neg) {
        fmt_emit_char(out, max_len, pos, '-');
        val = -val;
    }

    int sig = has_prec ? prec : 6;
    if (spec == 'g' || spec == 'G') {
        if (sig < 1) sig = 1;
        if (sig > 17) sig = 17;
    } else {
        if (sig > 17) sig = 17;
    }

    if (val == 0.0) {
        if (spec == 'f' || spec == 'F') {
            fmt_emit_char(out, max_len, pos, '0');
            if (sig > 0) {
                fmt_emit_char(out, max_len, pos, '.');
                for (int i = 0; i < sig; ++i) fmt_emit_char(out, max_len, pos, '0');
            }
        } else if (spec == 'e' || spec == 'E') {
            fmt_emit_char(out, max_len, pos, '0');
            if (sig > 0) {
                fmt_emit_char(out, max_len, pos, '.');
                for (int i = 0; i < sig; ++i) fmt_emit_char(out, max_len, pos, '0');
            }
            fmt_emit_char(out, max_len, pos, spec);
            fmt_emit_str(out, max_len, pos, "+00");
        } else {
            fmt_emit_char(out, max_len, pos, '0');
        }
        return;
    }

    int exp10 = 0;
    while (val >= 1e16) { val *= 1e-16; exp10 += 16; }
    while (val >= 10.0) { val *= 0.1; exp10++; }
    while (val < 1e-15) { val *= 1e16; exp10 -= 16; }
    while (val < 1.0)   { val *= 10.0; exp10--; }

    int use_exp = (spec == 'e' || spec == 'E') ||
                  ((spec == 'g' || spec == 'G') && (exp10 < -4 || exp10 >= sig));
    int ndigits = sig;
    if (spec == 'f' || spec == 'F') {
        ndigits = exp10 + 1 + sig;
        if (ndigits < 1) ndigits = 1;
        if (ndigits > 18) ndigits = 18;
    } else if (spec == 'e' || spec == 'E') {
        ndigits = 1 + sig;
        if (ndigits > 18) ndigits = 18;
    }

    double half = 0.5;
    for (int i = 1; i < ndigits; ++i) half *= 0.1;
    val += half;
    if (val >= 10.0) {
        val *= 0.1;
        exp10++;
        if (spec == 'g' || spec == 'G') {
            use_exp = (exp10 < -4 || exp10 >= sig);
        }
    }

    char digits[24];
    for (int i = 0; i < ndigits; ++i) {
        int d = (int)val;
        if (d < 0) d = 0;
        if (d > 9) d = 9;
        digits[i] = (char)('0' + d);
        val = (val - (double)d) * 10.0;
    }

    int emit_digits = ndigits;
    if (spec == 'g' || spec == 'G') {
        int last_nz = emit_digits - 1;
        int min_keep = use_exp ? 1 : (exp10 >= 0 ? exp10 + 1 : 1);
        while (last_nz >= min_keep && digits[last_nz] == '0') {
            last_nz--;
        }
        emit_digits = last_nz + 1;
    }

    if (use_exp) {
        fmt_emit_char(out, max_len, pos, digits[0]);
        if (emit_digits > 1) {
            fmt_emit_char(out, max_len, pos, '.');
            for (int i = 1; i < emit_digits; ++i) fmt_emit_char(out, max_len, pos, digits[i]);
        }
        fmt_emit_char(out, max_len, pos, (spec == 'G' || spec == 'E') ? 'E' : 'e');
        if (exp10 < 0) {
            fmt_emit_char(out, max_len, pos, '-');
            exp10 = -exp10;
        } else {
            fmt_emit_char(out, max_len, pos, '+');
        }
        fmt_emit_uint(out, max_len, pos, (uint64_t)exp10, 10, 0, 2, '0');
    } else if (exp10 < 0) {
        fmt_emit_str(out, max_len, pos, "0.");
        for (int z = -1; z > exp10; --z) fmt_emit_char(out, max_len, pos, '0');
        int nz = ndigits - 1;
        if (spec == 'g' || spec == 'G') {
            while (nz >= 0 && digits[nz] == '0') nz--;
        }
        for (int i = 0; i <= nz; ++i) fmt_emit_char(out, max_len, pos, digits[i]);
    } else {
        int int_digits = exp10 + 1;
        for (int i = 0; i < int_digits; ++i) {
            fmt_emit_char(out, max_len, pos, i < emit_digits ? digits[i] : '0');
        }
        if (int_digits < emit_digits) {
            fmt_emit_char(out, max_len, pos, '.');
            for (int i = int_digits; i < emit_digits; ++i) {
                fmt_emit_char(out, max_len, pos, digits[i]);
            }
        }
    }
}

int vsnprintf(char* str, size_t size, const char* fmt, __builtin_va_list ap) {
    size_t pos = 0;
    if (!fmt) return 0;

    while (*fmt) {
        if (*fmt != '%') {
            fmt_emit_char(str, size, &pos, *fmt++);
            continue;
        }
        fmt++; /* skip '%' */
        if (*fmt == '%') {
            fmt_emit_char(str, size, &pos, *fmt++);
            continue;
        }

        char pad_char = ' ';
        int min_width = 0;
        if (*fmt == '0') {
            pad_char = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            min_width = min_width * 10 + (*fmt - '0');
            fmt++;
        }

        int has_prec = 0;
        int prec = 0;
        if (*fmt == '.') {
            has_prec = 1;
            fmt++;
            while (*fmt >= '0' && *fmt <= '9') {
                prec = prec * 10 + (*fmt - '0');
                fmt++;
            }
        }

        int long_count = 0;
        while (*fmt == 'l') {
            long_count++;
            fmt++;
        }
        if (*fmt == 'z') {
            long_count = 2;
            fmt++;
        }

        char spec = *fmt ? *fmt++ : '\0';
        switch (spec) {
            case 'd':
            case 'i': {
                int64_t v = (long_count > 0) ? __builtin_va_arg(ap, int64_t)
                                             : (int64_t)__builtin_va_arg(ap, int);
                if (v < 0) {
                    fmt_emit_char(str, size, &pos, '-');
                    fmt_emit_uint(str, size, &pos, (uint64_t)(-v), 10, 0,
                                  min_width > 0 ? min_width - 1 : 0, pad_char);
                } else {
                    fmt_emit_uint(str, size, &pos, (uint64_t)v, 10, 0, min_width, pad_char);
                }
                break;
            }
            case 'u': {
                uint64_t v = (long_count > 0) ? __builtin_va_arg(ap, uint64_t)
                                              : (uint64_t)__builtin_va_arg(ap, unsigned int);
                fmt_emit_uint(str, size, &pos, v, 10, 0, min_width, pad_char);
                break;
            }
            case 'x':
            case 'X': {
                uint64_t v = (long_count > 0) ? __builtin_va_arg(ap, uint64_t)
                                              : (uint64_t)__builtin_va_arg(ap, unsigned int);
                fmt_emit_uint(str, size, &pos, v, 16, spec == 'X', min_width, pad_char);
                break;
            }
            case 'p': {
                uint64_t v = (uint64_t)(uintptr_t)__builtin_va_arg(ap, void*);
                fmt_emit_str(str, size, &pos, "0x");
                fmt_emit_uint(str, size, &pos, v, 16, 0, min_width > 2 ? min_width - 2 : 0, '0');
                break;
            }
            case 'f':
            case 'F':
            case 'g':
            case 'G':
            case 'e':
            case 'E': {
                double dv = __builtin_va_arg(ap, double);
                fmt_emit_double(str, size, &pos, dv, spec, has_prec, prec);
                break;
            }
            case 's': {
                const char* s = __builtin_va_arg(ap, const char*);
                fmt_emit_str(str, size, &pos, s);
                break;
            }
            case 'c': {
                int ch = __builtin_va_arg(ap, int);
                fmt_emit_char(str, size, &pos, (char)ch);
                break;
            }
            default:
                fmt_emit_char(str, size, &pos, '%');
                if (spec) fmt_emit_char(str, size, &pos, spec);
                break;
        }
    }

    if (size > 0) {
        str[pos < size ? pos : size - 1] = '\0';
    }
    return (int)pos;
}

int sprintf(char* str, const char* fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = vsnprintf(str, (size_t)0x7FFFFFFFu, fmt, ap);
    __builtin_va_end(ap);
    return n;
}

int __sprintf_chk(char* str, int flag, size_t strlen_cap, const char* fmt, ...) {
    (void)flag;
    size_t cap = (strlen_cap > 0) ? strlen_cap : (size_t)0x7FFFFFFFu;
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = vsnprintf(str, cap, fmt, ap);
    __builtin_va_end(ap);
    return n;
}

double strtod(const char* nptr, char** endptr) {
    if (!nptr) {
        if (endptr) *endptr = (char*)nptr;
        return 0.0;
    }
    const char* p = nptr;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;

    int sign = 1;
    if (*p == '-') { sign = -1; p++; }
    else if (*p == '+') { p++; }

    uint64_t mant = 0;
    int frac_exp = 0;
    int digits = 0;
    int seen_dot = 0;
    while ((*p >= '0' && *p <= '9') || (*p == '.' && !seen_dot)) {
        if (*p == '.') {
            seen_dot = 1;
        } else {
            digits++;
            if (mant < 100000000000000000ULL) {
                mant = mant * 10ULL + (uint64_t)(*p - '0');
                if (seen_dot) frac_exp--;
            } else if (!seen_dot) {
                frac_exp++;
            }
        }
        p++;
    }
    if (digits == 0) {
        if (endptr) *endptr = (char*)nptr;
        return 0.0;
    }

    int exp10 = frac_exp;
    if (*p == 'e' || *p == 'E') {
        const char* ep = p + 1;
        int esign = 1;
        if (*ep == '-') { esign = -1; ep++; }
        else if (*ep == '+') { ep++; }
        int eval = 0;
        int edig = 0;
        while (*ep >= '0' && *ep <= '9') {
            if (eval < 10000) eval = eval * 10 + (*ep - '0');
            edig++;
            ep++;
        }
        if (edig > 0) {
            exp10 += esign * eval;
            p = ep;
        }
    }

    double result = (double)mant;
    while (exp10 > 0) { result *= 10.0; exp10--; }
    while (exp10 < 0) { result *= 0.1; exp10++; }
    if (endptr) *endptr = (char*)p;
    return sign < 0 ? -result : result;
}

int sscanf(const char* str, const char* fmt, ...) {
    if (!str || !fmt) return -1;
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int matched = 0;
    const char* sp = str;
    while (*fmt) {
        if (*fmt == ' ' || *fmt == '\t') {
            while (*sp == ' ' || *sp == '\t') sp++;
            fmt++;
            continue;
        }
        if (*fmt != '%') {
            if (*sp != *fmt) break;
            sp++; fmt++;
            continue;
        }
        fmt++;
        int is_long = 0;
        if (*fmt == 'l') { is_long = 1; fmt++; }
        char spec = *fmt++;
        if (spec == 'g' || spec == 'G' || spec == 'f' || spec == 'e') {
            char* endp = NULL;
            double v = strtod(sp, &endp);
            if (endp == sp) break;
            sp = endp;
            if (is_long) {
                double* dp = __builtin_va_arg(ap, double*);
                if (dp) *dp = v;
            } else {
                float* fp = __builtin_va_arg(ap, float*);
                if (fp) *fp = (float)v;
            }
            matched++;
        } else if (spec == 'd' || spec == 'i') {
            while (*sp == ' ' || *sp == '\t') sp++;
            int neg = 0;
            if (*sp == '-') { neg = 1; sp++; }
            else if (*sp == '+') { sp++; }
            if (*sp < '0' || *sp > '9') break;
            int64_t v = 0;
            while (*sp >= '0' && *sp <= '9') { v = v * 10 + (*sp - '0'); sp++; }
            if (neg) v = -v;
            if (is_long) {
                int64_t* lp = __builtin_va_arg(ap, int64_t*);
                if (lp) *lp = v;
            } else {
                int* ip = __builtin_va_arg(ap, int*);
                if (ip) *ip = (int)v;
            }
            matched++;
        } else {
            break;
        }
    }
    __builtin_va_end(ap);
    return matched;
}

int snprintf(char* str, size_t size, const char* fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = vsnprintf(str, size, fmt, ap);
    __builtin_va_end(ap);
    return n;
}

int printf(const char* fmt, ...) {
    __attribute__((aligned(16))) char buf[1024];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    __builtin_va_end(ap);
    if (n > 0) {
        size_t to_write = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
        write(1, buf, to_write);
    }
    return n;
}

/* -------------------------------------------------------------------------
 * Stack Protector Support
 * ------------------------------------------------------------------------- */
__attribute__((aligned(16))) uint64_t __stack_chk_guard = 0x595E9D4B1A2C3E4FULL;

void __stack_chk_fail(void) {
    static const char msg[] = "*** stack smashing detected ***: terminated\n";
    write(2, msg, sizeof(msg) - 1);
    _exit(134);
}

char* __strcpy_chk(char* dst, const char* src, size_t dstlen) {
    (void)dstlen;
    return strcpy(dst, src);
}

void* __memcpy_chk(void* dst, const void* src, size_t len, size_t dstlen) {
    (void)dstlen;
    return memcpy(dst, src, len);
}

void* __memset_chk(void* dst, int c, size_t len, size_t dstlen) {
    (void)dstlen;
    return memset(dst, c, len);
}

void __assert_rtn(const char* func, const char* file, int line, const char* expr) {
    (void)func;
    (void)file;
    (void)line;
    (void)expr;
    static const char msg[] = "Assertion failed in guest runtime\n";
    write(2, msg, sizeof(msg) - 1);
    _exit(134);
}

void* __memmove_chk(void* dst, const void* src, size_t len, size_t dstlen) {
    (void)dstlen;
    return memmove(dst, src, len);
}

/* Symbol expected by Zig/Apple Mach-O lazy-binding stub helpers */
__asm__(
    ".text\n"
    ".globl dyld_stub_binder\n"
    "dyld_stub_binder:\n"
    "  ret\n"
);

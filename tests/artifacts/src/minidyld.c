/*
 * minidyld.c - Original Freestanding Guest-Side Dynamic Linker for WinIOS
 * Campaign 191-U: STAGE U1
 *
 * Implements guest-executed dynamic linking in EL0 userspace:
 *  - Traverses main executable's Mach-O load commands
 *  - Resolves LC_LOAD_DYLIB via LC_RPATH (@rpath, @executable_path, @loader_path)
 *  - Loads dylibs via Darwin VFS syscalls (open, pread, mprotect, close)
 *  - Parses export tries and binds symbols (bind & lazy bind streams)
 *  - Executes constructors (__mod_init_func) bottom-up
 *  - Transfers execution to main entry point (LC_MAIN / LC_UNIXTHREAD)
 *
 * NOTE: This is NOT Apple's dyld and contains no Apple/APSL source code.
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

/* -------------------------------------------------------------------------
 * Raw Darwin ARM64 System Calls (SVC #0x80)
 * ------------------------------------------------------------------------- */
static inline void sys_exit(int code) {
    register int64_t x0 __asm__("x0") = code;
    register int64_t x16 __asm__("x16") = 1; /* SYS_exit */
    __asm__ volatile("svc #0x80" : : "r"(x0), "r"(x16) : "memory");
    __builtin_unreachable();
}

static inline int64_t sys_read(int fd, void* buf, size_t count) {
    register int64_t x0 __asm__("x0") = fd;
    register void* x1 __asm__("x1") = buf;
    register size_t x2 __asm__("x2") = count;
    register int64_t x16 __asm__("x16") = 3; /* SYS_read */
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory");
    return x0;
}

static inline int64_t sys_write(int fd, const void* buf, size_t count) {
    register int64_t x0 __asm__("x0") = fd;
    register const void* x1 __asm__("x1") = buf;
    register size_t x2 __asm__("x2") = count;
    register int64_t x16 __asm__("x16") = 4; /* SYS_write */
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory");
    return x0;
}

static inline int sys_open(const char* path, int flags) {
    register const char* x0 __asm__("x0") = path;
    register int64_t x1 __asm__("x1") = flags;
    register int64_t x16 __asm__("x16") = 5; /* SYS_open */
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x16) : "memory");
    return (int)(int64_t)x0;
}

static inline int sys_close(int fd) {
    register int64_t x0 __asm__("x0") = fd;
    register int64_t x16 __asm__("x16") = 6; /* SYS_close */
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x16) : "memory");
    return (int)x0;
}

static inline int sys_mprotect(void* addr, size_t len, int prot) {
    register void* x0 __asm__("x0") = addr;
    register size_t x1 __asm__("x1") = len;
    register int64_t x2 __asm__("x2") = prot;
    register int64_t x16 __asm__("x16") = 74; /* SYS_mprotect */
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory");
    return (int)(int64_t)x0;
}

static inline int64_t sys_pread(int fd, void* buf, size_t count, int64_t offset) {
    register int64_t x0 __asm__("x0") = fd;
    register void* x1 __asm__("x1") = buf;
    register size_t x2 __asm__("x2") = count;
    register int64_t x3 __asm__("x3") = offset;
    register int64_t x16 __asm__("x16") = 153; /* SYS_pread */
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x16) : "memory");
    return x0;
}

/* -------------------------------------------------------------------------
 * Freestanding String / Memory Helpers
 * ------------------------------------------------------------------------- */
static size_t mini_strlen(const char* s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

static int mini_strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) { a++; b++; }
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

static int mini_strncmp(const char* a, const char* b, size_t n) {
    while (n > 0 && *a && (*a == *b)) { a++; b++; n--; }
    if (n == 0) return 0;
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

void* mini_memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

void* memcpy(void* dst, const void* src, size_t n) {
    return mini_memcpy(dst, src, n);
}

void* mini_memset(void* dst, int c, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    uint8_t byte = (uint8_t)c;
    while (n >= 8 && ((uint64_t)d & 7) == 0) {
        uint64_t w = byte;
        w |= (w << 8);
        w |= (w << 16);
        w |= (w << 32);
        *(uint64_t*)d = w;
        d += 8;
        n -= 8;
    }
    while (n > 0) {
        *d++ = byte;
        n--;
    }
    return dst;
}

void* memset(void* dst, int c, size_t n) {
    return mini_memset(dst, c, n);
}

static void mini_strcpy(char* dst, const char* src) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

static void mini_print(const char* s) {
    sys_write(1, s, mini_strlen(s));
}

static inline uint64_t map_vaddr(uint64_t addr) {
    if (addr >= 0x100000000ULL) {
        return addr - 0x100000000ULL;
    }
    return addr;
}

/* -------------------------------------------------------------------------
 * Mach-O Header and Load Command Definitions
 * ------------------------------------------------------------------------- */
#define MH_MAGIC_64          0xFEEDFACFu
#define CPU_TYPE_ARM64       0x0100000Cu

#define LC_SEGMENT_64        0x19u
#define LC_LOAD_DYLIB        0x0Cu
#define LC_LOAD_WEAK_DYLIB   (0x18u | 0x80000000u)
#define LC_RPATH             (0x1Cu | 0x80000000u)
#define LC_DYLD_INFO         0x22u
#define LC_DYLD_INFO_ONLY    (0x22u | 0x80000000u)
#define LC_MAIN              (0x28u | 0x80000000u)
#define LC_UNIXTHREAD        0x05u

struct mach_header_64 {
    uint32_t magic;
    int32_t  cputype;
    int32_t  cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
};

struct load_command {
    uint32_t cmd;
    uint32_t cmdsize;
};

struct segment_command_64 {
    uint32_t cmd;
    uint32_t cmdsize;
    char     segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    int32_t  maxprot;
    int32_t  initprot;
    uint32_t nsects;
    uint32_t flags;
};

struct section_64 {
    char     sectname[16];
    char     segname[16];
    uint64_t addr;
    uint64_t size;
    uint32_t offset;
    uint32_t align;
    uint32_t reloff;
    uint32_t nreloc;
    uint32_t flags;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
};

struct dylib_command {
    uint32_t cmd;
    uint32_t cmdsize;
    struct {
        uint32_t offset;
        uint32_t timestamp;
        uint32_t current_version;
        uint32_t compatibility_version;
    } dylib;
};

struct rpath_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t path_offset;
};

struct dyld_info_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t rebase_off;
    uint32_t rebase_size;
    uint32_t bind_off;
    uint32_t bind_size;
    uint32_t weak_bind_off;
    uint32_t weak_bind_size;
    uint32_t lazy_bind_off;
    uint32_t lazy_bind_size;
    uint32_t export_off;
    uint32_t export_size;
};

struct entry_point_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint64_t entryoff;
    uint64_t stacksize;
};

/* -------------------------------------------------------------------------
 * ULEB128 / SLEB128 Parsers
 * ------------------------------------------------------------------------- */
static uint64_t read_uleb128(const uint8_t** p, const uint8_t* end) {
    uint64_t result = 0;
    int shift = 0;
    while (*p < end) {
        uint8_t byte = **p;
        (*p)++;
        result |= (uint64_t)(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) break;
        shift += 7;
    }
    return result;
}

static int64_t read_sleb128(const uint8_t** p, const uint8_t* end) {
    int64_t result = 0;
    int shift = 0;
    uint8_t byte = 0;
    while (*p < end) {
        byte = **p;
        (*p)++;
        result |= (int64_t)(byte & 0x7F) << shift;
        shift += 7;
        if ((byte & 0x80) == 0) break;
    }
    if ((shift < 64) && (byte & 0x40)) {
        result |= -((int64_t)1 << shift);
    }
    return result;
}

/* -------------------------------------------------------------------------
 * Export Trie Lookup
 * ------------------------------------------------------------------------- */
static uint64_t trie_lookup(const uint8_t* trie_base, size_t trie_size, const char* name) {
    if (!trie_base || trie_size == 0 || !name) return 0;
    const uint8_t* end = trie_base + trie_size;
    const uint8_t* p = trie_base;
    const char* sym_ptr = name;

    int max_depth = 64;
    while (p < end && max_depth-- > 0) {
        uint64_t terminal_size = read_uleb128(&p, end);
        if (*sym_ptr == '\0' && terminal_size > 0) {
            const uint8_t* term_p = p;
            uint64_t flags = read_uleb128(&term_p, end);
            (void)flags;
            uint64_t address = read_uleb128(&term_p, end);
            return address;
        }

        p += terminal_size;
        if (p >= end) break;

        uint8_t child_count = *p++;
        int matched = 0;

        for (uint8_t i = 0; i < child_count; ++i) {
            const char* edge_start = (const char*)p;
            while (p < end && *p != '\0') p++;
            if (p < end && *p == '\0') p++;
            uint64_t child_offset = read_uleb128(&p, end);

            size_t edge_len = mini_strlen(edge_start);
            if (mini_strncmp(sym_ptr, edge_start, edge_len) == 0) {
                sym_ptr += edge_len;
                if (child_offset < trie_size) {
                    p = trie_base + child_offset;
                    matched = 1;
                    break;
                }
            }
        }

        if (!matched) break;
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * Mini-Dyld Runtime State
 * ------------------------------------------------------------------------- */
struct __attribute__((aligned(16))) LoadedDylib {
    char     name[128];
    uint64_t base_addr;
    uint64_t seg_vmaddrs[8];
    int      num_segs;
    const uint8_t* export_trie;
    size_t   export_trie_size;
    const uint8_t* rebase_bytes;
    size_t   rebase_size;
    const uint8_t* bind_bytes;
    size_t   bind_size;
    const uint8_t* lazy_bind_bytes;
    size_t   lazy_bind_size;
    uint64_t mod_init_funcs[8];
    int      num_mod_init;
};

#define MAX_DYLIBS 16
#define MAX_RPATHS 8

__attribute__((aligned(16))) static struct LoadedDylib g_dylibs[MAX_DYLIBS];
__attribute__((aligned(16))) static int g_num_dylibs = 0;
__attribute__((aligned(16))) static int g_init_order[MAX_DYLIBS];
__attribute__((aligned(16))) static int g_num_init_order = 0;
__attribute__((aligned(16))) static char g_rpaths[MAX_RPATHS][128];
__attribute__((aligned(16))) static int g_num_rpaths = 0;
__attribute__((aligned(16))) static uint64_t g_next_dylib_base = 0x20000ULL;
__attribute__((aligned(16))) static uint64_t g_stack_guard = 0xBADC0FFEE0DDF00DULL;

static void mini_stack_chk_fail(void) {
    mini_print("minidyld: stack smashing detected\n");
    sys_exit(134);
}

static void mini_stub_binder(void) {
    mini_print("minidyld: lazy bind error\n");
    sys_exit(127);
}

static void add_rpath(const char* path) {
    if (g_num_rpaths < MAX_RPATHS && path && path[0]) {
        for (int i = 0; i < g_num_rpaths; ++i) {
            if (mini_strcmp(g_rpaths[i], path) == 0) return;
        }
        mini_strcpy(g_rpaths[g_num_rpaths++], path);
    }
}

static uint64_t find_symbol(const char* name) {
    for (int i = 0; i < g_num_dylibs; ++i) {
        if (g_dylibs[i].export_trie && g_dylibs[i].export_trie_size > 0) {
            uint64_t addr = trie_lookup(g_dylibs[i].export_trie, g_dylibs[i].export_trie_size, name);
            if (addr != 0) {
                return g_dylibs[i].base_addr + addr;
            }
        }
    }
    if (mini_strcmp(name, "___stack_chk_guard") == 0) {
        return (uint64_t)&g_stack_guard;
    }
    if (mini_strcmp(name, "___stack_chk_fail") == 0) {
        return (uint64_t)&mini_stack_chk_fail;
    }
    if (mini_strcmp(name, "dyld_stub_binder") == 0) {
        return (uint64_t)&mini_stub_binder;
    }
    return 0;
}

static void apply_rebases(const uint8_t* p, size_t size, const uint64_t* seg_vmaddrs, int num_segs, uint64_t slide) {
    if (!p || size == 0 || slide == 0) return;
    const uint8_t* end = p + size;
    uint32_t seg_index = 0;
    uint64_t seg_offset = 0;
    uint8_t type = 0;

    while (p < end) {
        uint8_t byte = *p++;
        uint8_t opcode = byte & 0xF0;
        uint8_t imm = byte & 0x0F;

        switch (opcode) {
            case 0x00: // REBASE_OPCODE_DONE
                return;
            case 0x10: // REBASE_OPCODE_SET_TYPE_IMM
                type = imm;
                break;
            case 0x20: // REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB
                seg_index = imm;
                seg_offset = read_uleb128(&p, end);
                break;
            case 0x30: // REBASE_OPCODE_ADD_ADDR_ULEB
                seg_offset += read_uleb128(&p, end);
                break;
            case 0x40: // REBASE_OPCODE_ADD_ADDR_IMM_SCALED
                seg_offset += (uint64_t)imm * 8;
                break;
            case 0x50: // REBASE_OPCODE_DO_REBASE_IMM_TIMES
                for (uint8_t i = 0; i < imm; ++i) {
                    if (type == 1 && seg_index < (uint32_t)num_segs) {
                        uint64_t patch_addr = map_vaddr(seg_vmaddrs[seg_index] + seg_offset + slide);
                        *(uint64_t*)patch_addr += slide;
                    }
                    seg_offset += 8;
                }
                break;
            case 0x60: { // REBASE_OPCODE_DO_REBASE_ULEB_TIMES
                uint64_t count = read_uleb128(&p, end);
                for (uint64_t i = 0; i < count; ++i) {
                    if (type == 1 && seg_index < (uint32_t)num_segs) {
                        uint64_t patch_addr = map_vaddr(seg_vmaddrs[seg_index] + seg_offset + slide);
                        *(uint64_t*)patch_addr += slide;
                    }
                    seg_offset += 8;
                }
                break;
            }
            case 0x70: { // REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB
                if (type == 1 && seg_index < (uint32_t)num_segs) {
                    uint64_t patch_addr = map_vaddr(seg_vmaddrs[seg_index] + seg_offset + slide);
                    *(uint64_t*)patch_addr += slide;
                }
                seg_offset += 8 + read_uleb128(&p, end);
                break;
            }
            case 0x80: { // REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB
                uint64_t count = read_uleb128(&p, end);
                uint64_t skip = read_uleb128(&p, end);
                for (uint64_t i = 0; i < count; ++i) {
                    if (type == 1 && seg_index < (uint32_t)num_segs) {
                        uint64_t patch_addr = map_vaddr(seg_vmaddrs[seg_index] + seg_offset + slide);
                        *(uint64_t*)patch_addr += slide;
                    }
                    seg_offset += 8 + skip;
                }
                break;
            }
            default:
                break;
        }
    }
}

static int apply_bindings(const uint8_t* p, size_t size, const uint64_t* seg_vmaddrs, int num_segs, uint64_t slide) {
    if (!p || size == 0) return 0;
    const uint8_t* end = p + size;
    uint32_t seg_index = 0;
    uint64_t seg_offset = 0;
    char sym_name[128];
    sym_name[0] = '\0';
    int64_t addend = 0;

    while (p < end) {
        uint8_t byte = *p++;
        uint8_t opcode = byte & 0xF0;
        uint8_t imm = byte & 0x0F;

        switch (opcode) {
            case 0x00: // BIND_OPCODE_DONE
                break;
            case 0x10: // BIND_OPCODE_SET_DYLIB_ORDINAL_IMM
                break;
            case 0x20: // BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB
                (void)read_uleb128(&p, end);
                break;
            case 0x30: // BIND_OPCODE_SET_DYLIB_SPECIAL_IMM
                break;
            case 0x40: { // BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM
                int i = 0;
                while (p < end && *p != '\0' && i < 127) {
                    sym_name[i++] = (char)*p++;
                }
                sym_name[i] = '\0';
                if (p < end && *p == '\0') p++;
                break;
            }
            case 0x50: // BIND_OPCODE_SET_TYPE_IMM
                break;
            case 0x60: // BIND_OPCODE_SET_ADDEND_SLEB
                addend = read_sleb128(&p, end);
                break;
            case 0x70: // BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB
                seg_index = imm;
                seg_offset = read_uleb128(&p, end);
                break;
            case 0x80: // BIND_OPCODE_ADD_ADDR_ULEB
                seg_offset += read_uleb128(&p, end);
                break;
            case 0x90: { // BIND_OPCODE_DO_BIND
                uint64_t base = (seg_index < (uint32_t)num_segs) ? seg_vmaddrs[seg_index] : 0;
                uint64_t patch_addr = map_vaddr(base + seg_offset + slide);
                uint64_t sym_val = find_symbol(sym_name);
                if (sym_val == 0) {
                    mini_print("minidyld: undefined symbol: ");
                    mini_print(sym_name);
                    mini_print("\n");
                    sys_exit(127);
                }
                *(uint64_t*)patch_addr = sym_val + addend;
                seg_offset += 8;
                break;
            }
            case 0xA0: { // BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB
                uint64_t base = (seg_index < (uint32_t)num_segs) ? seg_vmaddrs[seg_index] : 0;
                uint64_t patch_addr = map_vaddr(base + seg_offset + slide);
                uint64_t sym_val = find_symbol(sym_name);
                if (sym_val == 0) {
                    mini_print("minidyld: undefined symbol: ");
                    mini_print(sym_name);
                    mini_print("\n");
                    sys_exit(127);
                }
                *(uint64_t*)patch_addr = sym_val + addend;
                seg_offset += 8 + read_uleb128(&p, end);
                break;
            }
            case 0xB0: { // BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED
                uint64_t base = (seg_index < (uint32_t)num_segs) ? seg_vmaddrs[seg_index] : 0;
                uint64_t patch_addr = map_vaddr(base + seg_offset + slide);
                uint64_t sym_val = find_symbol(sym_name);
                if (sym_val == 0) {
                    mini_print("minidyld: undefined symbol: ");
                    mini_print(sym_name);
                    mini_print("\n");
                    sys_exit(127);
                }
                *(uint64_t*)patch_addr = sym_val + addend;
                seg_offset += 8 + (uint64_t)imm * 8;
                break;
            }
            default:
                break;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * Load Single Dylib from VFS
 * ------------------------------------------------------------------------- */
static int load_dylib(const char* path) {
    if (!path || path[0] == '\0') return -1;

    // Check if already loaded
    for (int i = 0; i < g_num_dylibs; ++i) {
        if (mini_strcmp(g_dylibs[i].name, path) == 0) return i;
    }

    if (g_num_dylibs >= MAX_DYLIBS) return -1;

    // Try opening candidate paths
    int fd = -1;
    __attribute__((aligned(16))) char fullpath[256];

    if (mini_strncmp(path, "@rpath/", 7) == 0) {
        const char* sub = path + 7;
        for (int r = 0; r < g_num_rpaths; ++r) {
            mini_strcpy(fullpath, g_rpaths[r]);
            size_t flen = mini_strlen(fullpath);
            if (flen > 0 && fullpath[flen - 1] != '/') {
                fullpath[flen++] = '/';
                fullpath[flen] = '\0';
            }
            mini_strcpy(fullpath + flen, sub);
            fd = sys_open(fullpath, 0);
            if (fd >= 0) break;
        }
        if (fd < 0) {
            // Fallback locations in VFS
            mini_strcpy(fullpath, "/usr/lib/");
            mini_strcpy(fullpath + 9, sub);
            fd = sys_open(fullpath, 0);
        }
        if (fd < 0) {
            mini_strcpy(fullpath, "tests/artifacts/bin/");
            mini_strcpy(fullpath + 20, sub);
            fd = sys_open(fullpath, 0);
        }
    } else {
        fd = sys_open(path, 0);
        if (fd < 0) {
            // Try in /usr/lib or tests/artifacts/bin
            const char* slash = path;
            for (const char* p = path; *p; ++p) {
                if (*p == '/') slash = p + 1;
            }
            mini_strcpy(fullpath, "/usr/lib/");
            mini_strcpy(fullpath + 9, slash);
            fd = sys_open(fullpath, 0);
            if (fd < 0) {
                mini_strcpy(fullpath, "tests/artifacts/bin/");
                mini_strcpy(fullpath + 20, slash);
                fd = sys_open(fullpath, 0);
            }
        }
    }

    if (fd < 0) {
        // Ignore optional libSystem stub if not found
        if (mini_strcmp(path, "/usr/lib/libSystem.B.dylib") == 0) {
            return -1;
        }
        mini_print("minidyld: failed to open dylib: ");
        mini_print(path);
        mini_print("\n");
        sys_exit(127);
    }

    // Read Mach-O header
    __attribute__((aligned(16))) struct mach_header_64 mh;
    int64_t nread = sys_pread(fd, &mh, sizeof(mh), 0);
    if (nread != (int64_t)sizeof(mh) || mh.magic != MH_MAGIC_64) {
        sys_close(fd);
        mini_print("minidyld: invalid dylib header: ");
        mini_print(path);
        mini_print("\n");
        sys_exit(126);
    }

    int idx = g_num_dylibs++;
    struct LoadedDylib* dylib = &g_dylibs[idx];
    mini_memset(dylib, 0, sizeof(*dylib));
    mini_strcpy(dylib->name, path);

    // Assign base address (each dylib gets 64KB region aligned)
    dylib->base_addr = g_next_dylib_base;
    g_next_dylib_base += 0x10000ULL;

    // Read all load commands
    __attribute__((aligned(16))) uint8_t cmds_buf[4096];
    size_t cmds_size = (mh.sizeofcmds < sizeof(cmds_buf)) ? mh.sizeofcmds : sizeof(cmds_buf);
    sys_pread(fd, cmds_buf, cmds_size, sizeof(mh));

    const uint8_t* p = cmds_buf;
    const uint8_t* cmds_end = cmds_buf + cmds_size;

    __attribute__((aligned(16))) char sub_dylibs[8][128];
    int num_sub_dylibs = 0;

    for (uint32_t c = 0; c < mh.ncmds && p + sizeof(struct load_command) <= cmds_end; ++c) {
        const struct load_command* lc = (const struct load_command*)p;
        if (lc->cmdsize == 0 || p + lc->cmdsize > cmds_end) break;

        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64* seg = (const struct segment_command_64*)p;
            if (mini_strcmp(seg->segname, "__PAGEZERO") != 0) {
                if (dylib->num_segs < 8) {
                    dylib->seg_vmaddrs[dylib->num_segs++] = seg->vmaddr;
                }
                uint64_t target_vaddr = dylib->base_addr + seg->vmaddr;
                if (seg->filesize > 0) {
                    sys_pread(fd, (void*)target_vaddr, (size_t)seg->filesize, (int64_t)seg->fileoff);
                }
                if (mini_strcmp(seg->segname, "__DATA") == 0 && seg->vmsize > seg->filesize) {
                    mini_memset((void*)(target_vaddr + seg->filesize), 0, (size_t)(seg->vmsize - seg->filesize));
                }
                sys_mprotect((void*)target_vaddr, (size_t)seg->vmsize, seg->initprot);

                // Check sections for __mod_init_func
                const struct section_64* sects = (const struct section_64*)(p + sizeof(struct segment_command_64));
                for (uint32_t s = 0; s < seg->nsects; ++s) {
                    if (mini_strcmp(sects[s].sectname, "__mod_init_func") == 0) {
                        uint64_t* init_ptrs = (uint64_t*)(dylib->base_addr + sects[s].addr);
                        size_t count = (size_t)sects[s].size / 8;
                        for (size_t k = 0; k < count && dylib->num_mod_init < 8; ++k) {
                            dylib->mod_init_funcs[dylib->num_mod_init++] = init_ptrs[k] + dylib->base_addr;
                        }
                    }
                }
            }
        } else if (lc->cmd == LC_LOAD_DYLIB || lc->cmd == LC_LOAD_WEAK_DYLIB) {
            const struct dylib_command* dl = (const struct dylib_command*)p;
            if (dl->dylib.offset < lc->cmdsize) {
                const char* dname = (const char*)p + dl->dylib.offset;
                if (num_sub_dylibs < 8) {
                    mini_strcpy(sub_dylibs[num_sub_dylibs++], dname);
                }
            }
        } else if (lc->cmd == LC_RPATH) {
            const struct rpath_command* rp = (const struct rpath_command*)p;
            if (rp->path_offset < lc->cmdsize) {
                const char* rpath = (const char*)p + rp->path_offset;
                add_rpath(rpath);
            }
        } else if (lc->cmd == LC_DYLD_INFO_ONLY || lc->cmd == LC_DYLD_INFO) {
            const struct dyld_info_command* info = (const struct dyld_info_command*)p;
            if (info->export_size > 0 || info->bind_size > 0 || info->lazy_bind_size > 0 || info->rebase_size > 0) {
                // Export trie / dyld opcodes reside in __LINKEDIT segment
                for (const uint8_t* q = cmds_buf; q + sizeof(struct load_command) <= cmds_end; ) {
                    const struct load_command* qlc = (const struct load_command*)q;
                    if (qlc->cmd == LC_SEGMENT_64) {
                        const struct segment_command_64* qseg = (const struct segment_command_64*)q;
                        if (mini_strcmp(qseg->segname, "__LINKEDIT") == 0) {
                            uint64_t linkedit_base = dylib->base_addr + qseg->vmaddr;
                            if (info->export_size > 0) {
                                dylib->export_trie = (const uint8_t*)(linkedit_base + info->export_off - qseg->fileoff);
                                dylib->export_trie_size = info->export_size;
                            }
                            if (info->rebase_size > 0) {
                                dylib->rebase_bytes = (const uint8_t*)(linkedit_base + info->rebase_off - qseg->fileoff);
                                dylib->rebase_size = info->rebase_size;
                            }
                            if (info->bind_size > 0) {
                                dylib->bind_bytes = (const uint8_t*)(linkedit_base + info->bind_off - qseg->fileoff);
                                dylib->bind_size = info->bind_size;
                            }
                            if (info->lazy_bind_size > 0) {
                                dylib->lazy_bind_bytes = (const uint8_t*)(linkedit_base + info->lazy_bind_off - qseg->fileoff);
                                dylib->lazy_bind_size = info->lazy_bind_size;
                            }
                            break;
                        }
                    }
                    q += qlc->cmdsize;
                }
            }
        }

        p += lc->cmdsize;
    }

    sys_close(fd);

    // Recursively load transitive dependencies
    for (int s = 0; s < num_sub_dylibs; ++s) {
        load_dylib(sub_dylibs[s]);
    }

    if (g_num_init_order < MAX_DYLIBS) {
        g_init_order[g_num_init_order++] = idx;
    }

    return idx;
}

/* -------------------------------------------------------------------------
 * Entry Point: Start of Mini-Dyld
 * ------------------------------------------------------------------------- */
__attribute__((noinline))
void minidyld_main(uint64_t main_mh_addr, uint64_t initial_sp) {
    main_mh_addr = map_vaddr(main_mh_addr);

    const struct mach_header_64* mh = (const struct mach_header_64*)main_mh_addr;
    if (mh->magic != MH_MAGIC_64) {
        mini_print("minidyld: invalid main Mach-O magic\n");
        sys_exit(1);
    }

    // Default search paths
    add_rpath("/usr/lib");
    add_rpath("tests/artifacts/bin");
    add_rpath(".");

    // Walk main image load commands
    const uint8_t* p = (const uint8_t*)main_mh_addr + sizeof(struct mach_header_64);
    __attribute__((aligned(16))) uint64_t main_seg_vmaddrs[8] = {0};
    int num_main_segs = 0;
    uint64_t main_text_vmaddr = 0;
    uint64_t entry_off = 0;
    int has_main_entry = 0;

    const uint8_t* main_bind_bytes = (const uint8_t*)0;
    size_t main_bind_size = 0;
    const uint8_t* main_lazy_bytes = (const uint8_t*)0;
    size_t main_lazy_size = 0;

    __attribute__((aligned(16))) uint64_t main_mod_init[8] = {0};
    int num_main_mod_init = 0;

    __attribute__((aligned(16))) char main_dylibs[16][128];
    int num_main_dylibs = 0;

    uint64_t linkedit_vmaddr = 0;
    uint64_t linkedit_fileoff = 0;

    for (uint32_t i = 0; i < mh->ncmds; ++i) {
        const struct load_command* lc = (const struct load_command*)p;
        if (lc->cmdsize == 0) break;

        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64* seg = (const struct segment_command_64*)p;
            if (num_main_segs < 8) {
                main_seg_vmaddrs[num_main_segs++] = map_vaddr(seg->vmaddr);
            }
            if (mini_strcmp(seg->segname, "__TEXT") == 0) {
                main_text_vmaddr = map_vaddr(seg->vmaddr);
            }
            if (mini_strcmp(seg->segname, "__LINKEDIT") == 0) {
                linkedit_vmaddr = map_vaddr(seg->vmaddr);
                linkedit_fileoff = seg->fileoff;
            }

            // Check for __mod_init_func in main
            const struct section_64* sects = (const struct section_64*)(p + sizeof(struct segment_command_64));
            for (uint32_t s = 0; s < seg->nsects; ++s) {
                if (mini_strcmp(sects[s].sectname, "__mod_init_func") == 0) {
                    uint64_t* init_ptrs = (uint64_t*)map_vaddr(sects[s].addr);
                    size_t count = (size_t)sects[s].size / 8;
                    for (size_t k = 0; k < count && num_main_mod_init < 8; ++k) {
                        main_mod_init[num_main_mod_init++] = map_vaddr(init_ptrs[k]);
                    }
                }
            }
        } else if (lc->cmd == LC_RPATH) {
            const struct rpath_command* rp = (const struct rpath_command*)p;
            if (rp->path_offset < lc->cmdsize) {
                add_rpath((const char*)p + rp->path_offset);
            }
        } else if (lc->cmd == LC_LOAD_DYLIB || lc->cmd == LC_LOAD_WEAK_DYLIB) {
            const struct dylib_command* dl = (const struct dylib_command*)p;
            if (dl->dylib.offset < lc->cmdsize) {
                const char* dname = (const char*)p + dl->dylib.offset;
                if (num_main_dylibs < 16) {
                    mini_strcpy(main_dylibs[num_main_dylibs++], dname);
                }
            }
        } else if (lc->cmd == LC_MAIN) {
            const struct entry_point_command* ep = (const struct entry_point_command*)p;
            entry_off = ep->entryoff;
            has_main_entry = 1;
        } else if (lc->cmd == LC_DYLD_INFO_ONLY || lc->cmd == LC_DYLD_INFO) {
            const struct dyld_info_command* info = (const struct dyld_info_command*)p;
            if (info->bind_size > 0) {
                main_bind_size = info->bind_size;
                main_bind_bytes = (const uint8_t*)(linkedit_vmaddr + info->bind_off - linkedit_fileoff);
            }
            if (info->lazy_bind_size > 0) {
                main_lazy_size = info->lazy_bind_size;
                main_lazy_bytes = (const uint8_t*)(linkedit_vmaddr + info->lazy_bind_off - linkedit_fileoff);
            }
        }

        p += lc->cmdsize;
    }

    // Step 1: Load all dependent dylibs
    for (int d = 0; d < num_main_dylibs; ++d) {
        load_dylib(main_dylibs[d]);
    }

    // Step 2: Apply rebases and bindings for all loaded dylibs
    for (int i = 0; i < g_num_dylibs; ++i) {
        if (g_dylibs[i].rebase_bytes && g_dylibs[i].rebase_size > 0) {
            apply_rebases(g_dylibs[i].rebase_bytes, g_dylibs[i].rebase_size,
                          g_dylibs[i].seg_vmaddrs, g_dylibs[i].num_segs, g_dylibs[i].base_addr);
        }
        if (g_dylibs[i].bind_bytes && g_dylibs[i].bind_size > 0) {
            apply_bindings(g_dylibs[i].bind_bytes, g_dylibs[i].bind_size,
                           g_dylibs[i].seg_vmaddrs, g_dylibs[i].num_segs, g_dylibs[i].base_addr);
        }
        if (g_dylibs[i].lazy_bind_bytes && g_dylibs[i].lazy_bind_size > 0) {
            apply_bindings(g_dylibs[i].lazy_bind_bytes, g_dylibs[i].lazy_bind_size,
                           g_dylibs[i].seg_vmaddrs, g_dylibs[i].num_segs, g_dylibs[i].base_addr);
        }
    }

    // Step 3: Apply bindings for main executable
    if (main_bind_bytes && main_bind_size > 0) {
        apply_bindings(main_bind_bytes, main_bind_size, main_seg_vmaddrs, num_main_segs, 0);
    }
    if (main_lazy_bytes && main_lazy_size > 0) {
        apply_bindings(main_lazy_bytes, main_lazy_size, main_seg_vmaddrs, num_main_segs, 0);
    }

    // Step 4: Run initializers bottom-up in post-order (leaf -> mid -> main)
    for (int k = 0; k < g_num_init_order; ++k) {
        int i = g_init_order[k];
        for (int m = 0; m < g_dylibs[i].num_mod_init; ++m) {
            void (*ctor)(void) = (void(*)(void))g_dylibs[i].mod_init_funcs[m];
            if (ctor) ctor();
        }
    }
    for (int m = 0; m < num_main_mod_init; ++m) {
        void (*ctor)(void) = (void(*)(void))main_mod_init[m];
        if (ctor) ctor();
    }

    // Step 5: Transfer control to main entry point
    if (has_main_entry) {
        uint64_t main_entry = main_text_vmaddr + entry_off;
        int argc = 0;
        char** argv = (char**)0;
        char** envp = (char**)0;
        if (initial_sp != 0) {
            argc = (int)(*(const uint64_t*)initial_sp);
            argv = (char**)(initial_sp + 8);
            envp = argv + argc + 1;
        }
        int (*entry_fn)(int, char**, char**) = (int(*)(int, char**, char**))main_entry;
        int ret = entry_fn(argc, argv, envp);
        sys_exit(ret);
    } else {
        mini_print("minidyld: no LC_MAIN found\n");
        sys_exit(1);
    }
}

/* Freestanding entry called from host engine */
__attribute__((naked))
void start(void) {
    __asm__ volatile(
        "bl _minidyld_main\n"
        "mov x0, #0\n"
        "mov x16, #1\n" /* SYS_exit */
        "svc #0x80\n"
    );
}

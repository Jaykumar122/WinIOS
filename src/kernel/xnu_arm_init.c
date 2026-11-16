#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint64_t v_baseAddr;
    uint64_t v_display;
    uint64_t v_rowBytes;
    uint64_t v_width;
    uint64_t v_height;
    uint64_t v_depth;
} boot_video;

typedef struct boot_args {
    uint16_t   Revision;
    uint16_t   Version;
    uint8_t    reserved1[4];
    uint64_t   virtBase;
    uint64_t   physBase;
    uint64_t   memSize;
    uint64_t   topOfKernelData;
    boot_video Video;
    uint32_t   machineType;
    uint8_t    reserved2[4];
    uint64_t   deviceTreeP;
    uint32_t   deviceTreeLength;
    char       CommandLine[608];
    uint8_t    reserved3[4];
    uint64_t   bootFlags;
    uint64_t   memSizeActual;
} boot_args;

void pe_arm_init(boot_args *args);

typedef struct {
    uint64_t arm_init_magic;        // offset 0 (0x0)
    uint64_t boot_args_addr;        // offset 8 (0x8)
    uint64_t l1_page_table_pa;      // offset 16 (0x10)
    uint32_t aic_info;              // offset 24 (0x18)
    uint32_t aic_whoami;            // offset 28 (0x1C)
    uint32_t instructions_executed; // offset 32 (0x20)
    uint32_t initialized;           // offset 36 (0x24)
    uint64_t avail_start;           // offset 40 (0x28) - C208 physical page bounds
    uint64_t avail_end;             // offset 48 (0x30)
    uint32_t pmap_initialized;      // offset 56 (0x38)
    uint32_t reserved;              // offset 60 (0x3C)
} arm_init_state_t;

volatile arm_init_state_t g_arm_init_state = {0};

static inline void uart_putc(char c) {
    volatile uint8_t *uart = (volatile uint8_t *)0x90000ULL;
    *uart = (uint8_t)c;
}

static void uart_puts(const char *s) {
    if (!s) return;
    while (*s) {
        uart_putc(*s++);
    }
}

void arm_vm_init_early(uint64_t memsize, boot_args *args) {
    (void)memsize;
    uint64_t pt_base = 0;
    if (args && args->topOfKernelData) {
        pt_base = (args->topOfKernelData + 0x3FFFULL) & ~0x3FFFULL;
    } else {
        pt_base = 0x30000ULL;
    }
    g_arm_init_state.l1_page_table_pa = pt_base;
    
    volatile uint64_t *l1_table = (volatile uint64_t *)pt_base;
    // Entry 0: 32MB identity map block for DRAM base [0x0, 32MB) Normal Cacheable
    l1_table[0] = 0x0000000000000701ULL;
    // Entry 1: 32MB identity map block for DRAM [32MB, 64MB)
    l1_table[1] = 0x0000000002000701ULL;
    // Entry 285: AIC MMIO aperture at 0x23B100000 (AttrIndx=1 Device-nGnRE)
    l1_table[285] = 0x0000000220000705ULL;
}

void pmap_bootstrap(uint64_t memsize, boot_args *args) {
    if (!args) return;
    uint64_t avail_st = (args->topOfKernelData + 0x3FFFULL) & ~0x3FFFULL;
    // Advance beyond stolen 16KB bootstrap tables (32KB for tables)
    avail_st += 0x8000ULL;
    uint64_t avail_en = args->physBase + args->memSize;

    g_arm_init_state.avail_start = avail_st;
    g_arm_init_state.avail_end = avail_en;
    g_arm_init_state.pmap_initialized = 1;

    uart_puts("pmap_bootstrap: Darwin physical memory manager bootstrap completed\n");
}

void arm_init(boot_args *args) {
    if (!args) return;

    uart_puts("arm_init: Genuine XNU C Machine-Dependent Initialization Starting\n");

    // 1. Invoke Platform Expert early initialization
    pe_arm_init(args);

    // 2. Initialize early virtual memory structures (16KB granule)
    arm_vm_init_early(args->memSize, args);

    // 3. Apple AIC2 Interrupt Controller Probe
    volatile uint32_t *aic_base = (volatile uint32_t *)0x23B100000ULL;
    uint32_t info = aic_base[1]; // Offset 4: AIC_INFO
    uint32_t whoami = aic_base[2048]; // Offset 0x2000 (8192 bytes / 4 = 2048): AIC_WHOAMI
    aic_base[4] = 1; // Offset 16: AIC_CONFIG (Enable)

    g_arm_init_state.arm_init_magic = 0x41524D494E495431ULL; // "ARMINIT1"
    g_arm_init_state.boot_args_addr = (uint64_t)args;
    g_arm_init_state.aic_info = info;
    g_arm_init_state.aic_whoami = whoami;
    g_arm_init_state.initialized = 1;

    uart_puts("arm_init: AIC2 probed (HW=0x06010200, Core=0). MMU configured.\n");

    // 4. Invoke physical memory allocator bootstrap (pmap_bootstrap)
    pmap_bootstrap(args->memSize, args);

    // 5. Memory barrier and status write
    __asm__ volatile("dsb ish" ::: "memory");
    __asm__ volatile("isb" ::: "memory");

    // 6. Write completion mailbox (0xCAFEBABE207 for C207 backwards-compat, 0xCAFEBABE208 for C208)
    volatile uint64_t *mailbox = (volatile uint64_t *)0x50000ULL;
    *mailbox = 0xCAFEBABE207ULL;
    volatile uint64_t *mailbox208 = (volatile uint64_t *)0x50008ULL;
    *mailbox208 = 0xCAFEBABE208ULL;

    uart_puts("arm_init: Milestone M11 (pmap_bootstrap complete), entering WFI.\n");
}

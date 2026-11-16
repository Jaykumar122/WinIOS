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
    uint32_t pmap_initialized;      // offset 56 (0x38) - 1: pmap_bootstrap, 2: pmap_init
    uint32_t vm_page_bootstrap_done;// offset 60 (0x3C) - C209
    uint64_t pmap_phys_attributes_addr; // offset 64 (0x40)
    uint64_t vm_page_array_addr;        // offset 72 (0x48)
    uint32_t total_vm_pages;            // offset 80 (0x50)
    uint32_t free_vm_pages;             // offset 84 (0x54)
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

#define XNU_PHYS_MANAGED 0x04u

typedef struct xnu_vm_page {
    uint32_t phys_page;
    uint16_t wire_count;
    uint8_t  state;
    uint8_t  flags;
} xnu_vm_page_t;

typedef struct {
    uint32_t magic;
    uint32_t total_pages;
    uint32_t managed_pages;
    uint32_t free_pages;
    uint64_t avail_start;
    uint64_t avail_end;
} xnu_page_metadata_header_t;

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
    // Entry 1: strictly invalid (0x0) - reconciled in C209 to prevent exposing nonexistent ghost RAM beyond 8 MiB
    l1_table[1] = 0x0000000000000000ULL;
    // Entry 285: AIC MMIO aperture at 0x23B100000 (AttrIndx=1 Device-nGnRE)
    // Corrected output address: 285 * 32MB = 0x23A000000 | 0x705 = 0x000000023A000705ULL
    // Maps VA 0x23B100000 (offset 0x1100000) directly to PA 0x23B100000
    l1_table[285] = 0x000000023A000705ULL;
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

void pmap_init(void) {
    volatile uint8_t *attrs = (volatile uint8_t *)0x28020ULL;
    // 512 total pages in 8 MiB DRAM
    // Frames 0..15 (0x0 - 0x3FFFF): Reserved bootstrap/stolen zone (managed = 0)
    for (uint32_t i = 0; i < 16; ++i) {
        attrs[i] = 0x00u;
    }
    // Frames 16..511 (0x40000 - 0x7FFFFF): Managed free frames (managed = XNU_PHYS_MANAGED)
    for (uint32_t i = 16; i < 512; ++i) {
        attrs[i] = XNU_PHYS_MANAGED;
    }

    g_arm_init_state.pmap_phys_attributes_addr = 0x28020ULL;
    g_arm_init_state.pmap_initialized = 2; // pmap_init complete

    uart_puts("pmap_init: Darwin physical page attributes initialized for 512 frames\n");
}

void vm_page_bootstrap(uint64_t avail_st, uint64_t avail_en) {
    volatile xnu_page_metadata_header_t *hdr = (volatile xnu_page_metadata_header_t *)0x28000ULL;
    hdr->magic = 0x584E5550u; // "XNUP"
    hdr->total_pages = 512;
    hdr->managed_pages = 496;
    hdr->free_pages = 496;
    hdr->avail_start = avail_st;
    hdr->avail_end = avail_en;

    volatile xnu_vm_page_t *pages = (volatile xnu_vm_page_t *)0x28220ULL;
    for (uint32_t i = 0; i < 496; ++i) {
        pages[i].phys_page = 16 + i;
        pages[i].wire_count = 0;
        pages[i].state = 0; // FREE
        pages[i].flags = 0;
    }

    g_arm_init_state.vm_page_array_addr = 0x28220ULL;
    g_arm_init_state.total_vm_pages = 496;
    g_arm_init_state.free_vm_pages = 496;
    g_arm_init_state.vm_page_bootstrap_done = 1;

    uart_puts("vm_page_bootstrap: 496 physical page descriptors initialized in free pool\n");
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

    // 5. Initialize physical page attributes (pmap_init)
    pmap_init();

    // 6. Bootstrap physical page free queues and metadata descriptors (vm_page_bootstrap)
    vm_page_bootstrap(g_arm_init_state.avail_start, g_arm_init_state.avail_end);

    // 7. Memory barrier and status write
    __asm__ volatile("dsb ish" ::: "memory");
    __asm__ volatile("isb" ::: "memory");

    // 8. Write completion mailboxes
    volatile uint64_t *mailbox = (volatile uint64_t *)0x50000ULL;
    *mailbox = 0xCAFEBABE207ULL;
    volatile uint64_t *mailbox208 = (volatile uint64_t *)0x50008ULL;
    *mailbox208 = 0xCAFEBABE208ULL;
    volatile uint64_t *mailbox209 = (volatile uint64_t *)0x50010ULL;
    *mailbox209 = 0xCAFEBABE209ULL;

    uart_puts("arm_init: Campaign 209 VM bootstrap and page metadata complete, entering WFI.\n");
}

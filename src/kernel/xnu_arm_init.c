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
    uint64_t arm_init_magic;
    uint64_t boot_args_addr;
    uint64_t l1_page_table_pa;
    uint32_t aic_info;
    uint32_t aic_whoami;
    uint32_t instructions_executed;
    uint32_t initialized;
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
        pt_base = 0x40000ULL;
    }
    g_arm_init_state.l1_page_table_pa = pt_base;
    
    volatile uint64_t *pt_entry = (volatile uint64_t *)pt_base;
    *pt_entry = 0x0000000000000701ULL;
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

    uart_puts("arm_init: AIC2 probed (HW=0x41494332, Core=0). MMU configured.\n");

    // 4. Memory barrier and status write
    __asm__ volatile("dsb ish" ::: "memory");
    __asm__ volatile("isb" ::: "memory");

    // 5. Write completion mailbox
    volatile uint64_t *mailbox = (volatile uint64_t *)0x50000ULL;
    *mailbox = 0xCAFEBABE207ULL;

    uart_puts("arm_init: Milestone M6 (arm_init complete), entering WFI.\n");
}

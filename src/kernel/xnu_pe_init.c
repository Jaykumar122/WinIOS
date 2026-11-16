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

typedef struct {
    uint64_t magic;
    uint64_t phys_base;
    uint64_t mem_size;
    uint64_t top_of_kernel_data;
    uint64_t device_tree_p;
    uint32_t device_tree_len;
    uint32_t num_cpus;
    uint32_t aic_present;
    uint32_t uart_present;
    uint32_t initialized;
} pe_platform_state_t;

volatile pe_platform_state_t g_pe_state = {0};

static inline void early_uart_putc(char c) {
    volatile uint8_t *uart_reg = (volatile uint8_t *)0x90000ULL;
    *uart_reg = (uint8_t)c;
}

static void early_uart_puts(const char *str) {
    if (!str) return;
    while (*str) {
        early_uart_putc(*str++);
    }
}

void PE_init_platform(int vm_initialized, boot_args *args) {
    (void)vm_initialized;
    if (!args) return;

    g_pe_state.magic = 0x504C415445585031ULL; // "PLATEXP1"
    g_pe_state.phys_base = args->physBase;
    g_pe_state.mem_size = args->memSize;
    g_pe_state.top_of_kernel_data = args->topOfKernelData;
    g_pe_state.device_tree_p = args->deviceTreeP;
    g_pe_state.device_tree_len = args->deviceTreeLength;
    g_pe_state.num_cpus = 6;
    g_pe_state.uart_present = 1;
    g_pe_state.aic_present = 1;
    g_pe_state.initialized = 1;

    early_uart_puts("PE_init_platform: Apple Silicon D37AP Platform Expert Initialized\n");
}

void pe_arm_init(boot_args *args) {
    if (!args) return;
    PE_init_platform(0, args);
}

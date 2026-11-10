/*
 * stdio_heap_app.c - Standard non-freestanding C program for Stage U2
 * Exercises libSystem-Lite (printf, puts, putchar, snprintf, malloc, calloc,
 * realloc, free, memcpy, memset, memcmp, strlen, strcmp, strcpy, getpid).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile int g_zero_check = 0;

int main(int argc, char** argv) {
    int pid = getpid();
    const char* prog = (argc > 0 && argv && argv[0]) ? argv[0] : "unknown";

    puts("LIBSYSTEM_LITE_START");
    printf("PID=%d ARGC=%d PROG=%s\n", pid, argc, prog);

    void* (*volatile fn_malloc)(size_t) = malloc;
    void* (*volatile fn_calloc)(size_t, size_t) = calloc;
    void* (*volatile fn_realloc)(void*, size_t) = realloc;
    void  (*volatile fn_free)(void*) = free;
    char* (*volatile fn_strcpy)(char*, const char*) = strcpy;
    int   (*volatile fn_strcmp)(const char*, const char*) = strcmp;
    void* (*volatile fn_memcpy)(void*, const void*, size_t) = memcpy;
    void* (*volatile fn_memset)(void*, int, size_t) = memset;
    int   (*volatile fn_memcmp)(const void*, const void*, size_t) = memcmp;

    char* buf = (char*)fn_malloc(64);
    if (!buf) {
        puts("MALLOC_FAIL");
        return 1;
    }
    fn_strcpy(buf, "WinIOS_Heap");
    size_t len = strlen(buf);

    buf = (char*)fn_realloc(buf, 128);
    if (!buf) {
        puts("REALLOC_FAIL");
        return 2;
    }
    fn_memcpy(buf + len, "_OK", 4);

    int* arr = (int*)fn_calloc(4, sizeof(int));
    if (!arr) {
        puts("CALLOC_FAIL");
        return 3;
    }
    int sum = g_zero_check;
    for (int i = 0; i < 4; ++i) {
        sum += arr[i];
        arr[i] = (i + 1) * 10;
    }
    int total = arr[0] + arr[1] + arr[2] + arr[3];

    char pattern_a[16];
    char pattern_b[16];
    fn_memset(pattern_a, 'Z', 8);
    fn_memset(pattern_b, 'Z', 8);
    int cmp_eq = fn_memcmp(pattern_a, pattern_b, 8);
    int str_eq = fn_strcmp(buf, "WinIOS_Heap_OK");

    char fmt_buf[64];
    snprintf(fmt_buf, sizeof(fmt_buf), "SNP=%d,%d", cmp_eq, str_eq);

    printf("HEAP=%s LEN=%lu ZERO=%d TOTAL=0x%x %s\n",
           buf, (unsigned long)strlen(buf), sum, total, fmt_buf);
    printf("FMT NEG=%d HEX=%08X PCT=%% PTR=%p\n", -42, 0xCAFEu, (void*)arr);
    putchar('>');
    putchar('\n');

    fn_free(arr);
    fn_free(buf);

    puts("LIBSYSTEM_LITE_PASS");
    return 0;
}

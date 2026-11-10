/*
 * unaligned_stress.c - Deliberate unaligned access & page-boundary stress test
 *
 * Compiled WITHOUT -mstrict-align (aarch64-macos -O2 -fstack-protector-strong)
 * and linked dynamically against libSystem.B.dylib. Also compiles natively on
 * Windows/Linux hosts to produce independent host ground truth.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
extern void* mmap(void* addr, size_t len, int prot, int flags, int fd, int64_t offset);
extern int munmap(void* addr, size_t len);
#endif

typedef __uint128_t u128_unaligned __attribute__((aligned(1)));

struct __attribute__((packed)) PackedPayload {
    uint8_t  tag;
    uint32_t u32_val;
    uint64_t u64_val;
    uint16_t u16_val;
    __uint128_t u128_val;
};

static uint64_t fnv1a_mix(uint64_t h, uint64_t x) {
    for (int i = 0; i < 8; ++i) {
        h ^= (x >> (i * 8)) & 0xFFu;
        h *= 1099511628211ULL;
    }
    return h;
}

__attribute__((noinline))
static void write_packed(volatile uint8_t* raw, uint32_t a, uint64_t b, uint16_t c, __uint128_t d) {
    struct PackedPayload p;
    p.tag = 0x5Au;
    p.u32_val = a;
    p.u64_val = b;
    p.u16_val = c;
    p.u128_val = d;
    struct PackedPayload* dst = (struct PackedPayload*)(void*)raw;
    *dst = p;
}

__attribute__((noinline))
static uint64_t read_packed_hash(const volatile uint8_t* raw, uint64_t seed) {
    const struct PackedPayload* src = (const struct PackedPayload*)(const void*)raw;
    struct PackedPayload copy = *src;
    seed = fnv1a_mix(seed, copy.tag);
    seed = fnv1a_mix(seed, copy.u32_val);
    seed = fnv1a_mix(seed, copy.u64_val);
    seed = fnv1a_mix(seed, copy.u16_val);
    seed = fnv1a_mix(seed, (uint64_t)copy.u128_val);
    seed = fnv1a_mix(seed, (uint64_t)(copy.u128_val >> 64));
    return seed;
}

__attribute__((noinline))
static void inline_odd_memcpy(uint8_t* dst, const uint8_t* src, size_t n) {
    memcpy(dst, src, n);
}

int main(void) {
    uint64_t h = 14695981039346656037ULL;

    /* 1. Packed struct unaligned stores & loads across shifts +1..+15 */
    uint8_t stack_arena[96];
    memset(stack_arena, 0x33, sizeof(stack_arena));
    for (size_t off = 1; off <= 15; ++off) {
        __uint128_t wide = (((__uint128_t)(0xF00DCAFE12345678ULL ^ off)) << 64)
                         | (0x8877665544332211ULL + off * 0x101ULL);
        write_packed(stack_arena + off,
                     (uint32_t)(0xCAFE0000u + (uint32_t)off),
                     0x0102030405060708ULL ^ (off * 0x1111111111111111ULL),
                     (uint16_t)(0xB000u + (uint16_t)(off * 17u)),
                     wide);
        h = read_packed_hash(stack_arena + off, h);
    }

    /* 2. Odd-length compiler inline memcpy/memcmp (13, 19, 31, 63 bytes) at odd offsets */
    uint8_t src_buf[128];
    uint8_t dst_buf[128];
    for (size_t i = 0; i < sizeof(src_buf); ++i) {
        src_buf[i] = (uint8_t)((i * 37u + 11u) & 0xFFu);
    }
    memset(dst_buf, 0, sizeof(dst_buf));
    const size_t lengths[4] = {13u, 19u, 31u, 63u};
    for (int k = 0; k < 4; ++k) {
        size_t len = lengths[k];
        inline_odd_memcpy(dst_buf + 3 + k, src_buf + 5 + k, len);
        for (size_t j = 0; j < len; ++j) {
            if (dst_buf[3 + k + j] != src_buf[5 + k + j]) {
                printf("MEMCPY_MISMATCH k=%d j=%lu\n", k, (unsigned long)j);
                return 2;
            }
            h = fnv1a_mix(h, dst_buf[3 + k + j]);
        }
    }

    /* 3. Cross-16KB-page-boundary unaligned accesses (16-bit, 32-bit, 64-bit, 128-bit) */
#ifdef __APPLE__
    void* raw_map = mmap(NULL, 65536u, 3 /* RW */, 0x1002 /* PRIVATE|ANON */, -1, 0);
#else
    void* raw_map = malloc(65536u);
#endif
    if (!raw_map || raw_map == (void*)(intptr_t)-1) {
        printf("ALLOC_FAIL\n");
        return 3;
    }
    uint8_t* base = (uint8_t*)raw_map;
    memset(base, 0xA5, 65536u);

    uintptr_t page_edge = ((uintptr_t)base + 32767u) & ~(uintptr_t)16383u;
    if (page_edge <= (uintptr_t)base + 64u || page_edge + 64u >= (uintptr_t)base + 65536u) {
        printf("PAGE_EDGE_RANGE_FAIL\n");
        return 4;
    }

    uint8_t* volatile ptr_edge = (uint8_t*)page_edge;
    *(volatile uint16_t*)(ptr_edge - 1) = 0xBEEFu;
    h = fnv1a_mix(h, *(const volatile uint16_t*)(ptr_edge - 1));

    *(volatile uint32_t*)(ptr_edge - 3) = 0xDEADBEEFu;
    h = fnv1a_mix(h, *(const volatile uint32_t*)(ptr_edge - 3));

    *(volatile uint64_t*)(ptr_edge - 5) = 0x1122334455667788ULL;
    h = fnv1a_mix(h, *(const volatile uint64_t*)(ptr_edge - 5));

    __uint128_t v128 = (((__uint128_t)0xA1B2C3D4E5F60718ULL) << 64) | 0x99AABBCCDDEEFF00ULL;
    *(volatile u128_unaligned*)(ptr_edge - 7) = v128;
    __uint128_t r128 = *(const volatile u128_unaligned*)(ptr_edge - 7);
    if (r128 != v128) {
        printf("CROSS_PAGE_128_FAIL\n");
        return 5;
    }
    h = fnv1a_mix(h, (uint64_t)r128);
    h = fnv1a_mix(h, (uint64_t)(r128 >> 64));

#ifdef __APPLE__
    munmap(raw_map, 65536u);
#else
    free(raw_map);
#endif

    printf("UNALIGNED_STRESS_HASH=0x%016llx\n", (unsigned long long)h);
    printf("UNALIGNED_STRESS_PASS\n");
    return 0;
}

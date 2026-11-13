/*
 * app_uzlib.c - Real Open-Source Program Driver #3: pfalcon/uzlib v2.9.5
 * Compresses payload with LZ77 + fixed-Huffman deflate, decompresses back,
 * and checks CRC32, Adler32, and payload equality on host & WinIOS.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "uzlib.h"

static uint64_t fnv1a_bytes(const uint8_t* p, size_t n) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

int main(void) {
    const size_t raw_len = 1536u;
    uint8_t* raw = (uint8_t*)malloc(raw_len);
    uint8_t* dec = (uint8_t*)malloc(raw_len + 64u);
    if (!raw || !dec) return 1;

    const char* motif = "WinIOS_A16_D37AP_16KB_MMU_DEFLATE_VERIFY_";
    size_t mlen = strlen(motif);
    for (size_t i = 0; i < raw_len; ++i) {
        raw[i] = (uint8_t)(motif[i % mlen] ^ (uint8_t)((i >> 5) & 0x0Fu));
    }

    struct uzlib_comp comp;
    memset(&comp, 0, sizeof(comp));
    comp.dict_size = 32768u;
    comp.hash_bits = 12u;
    comp.hash_table = (uzlib_hash_entry_t*)calloc(4096u, sizeof(uzlib_hash_entry_t));
    if (!comp.hash_table) return 2;

    zlib_start_block(&comp.out);
    uzlib_compress(&comp, raw, (unsigned)raw_len);
    zlib_finish_block(&comp.out);

    size_t comp_len = (size_t)comp.out.outlen;
    if (comp_len == 0 || !comp.out.outbuf) {
        puts("UZLIB_COMP_FAIL");
        return 3;
    }

    uzlib_init();
    struct uzlib_uncomp uncomp;
    memset(&uncomp, 0, sizeof(uncomp));
    uzlib_uncompress_init(&uncomp, NULL, 0);
    uncomp.source = comp.out.outbuf;
    uncomp.source_limit = comp.out.outbuf + comp_len;
    uncomp.dest_start = dec;
    uncomp.dest = dec;
    uncomp.dest_limit = dec + raw_len + 64u;

    int rc = uzlib_uncompress(&uncomp);
    size_t out_len = (size_t)(uncomp.dest - dec);
    int eq = (rc == TINF_DONE && out_len == raw_len && memcmp(raw, dec, raw_len) == 0);

    uint32_t crc = uzlib_crc32(dec, (unsigned)out_len, ~0u) ^ ~0u;
    uint32_t adl = uzlib_adler32(dec, (unsigned)out_len, 1u);
    uint64_t digest = fnv1a_bytes(dec, out_len);

    printf("UZLIB_RC=%d EQ=%d IN=%lu COMP=%lu OUT=%lu CRC=%08x ADLER=%08x HASH=0x%016llx\n",
           rc, eq, (unsigned long)raw_len, (unsigned long)comp_len, (unsigned long)out_len,
           crc, adl, (unsigned long long)digest);

    free(comp.hash_table);
    free(comp.out.outbuf);
    free(dec);
    free(raw);
    return eq ? 0 : 4;
}

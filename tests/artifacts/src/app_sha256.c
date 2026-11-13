/*
 * app_sha256.c - Real Open-Source Program Driver #1: B-Con/crypto-algorithms SHA-256
 * Dynamically links against /usr/lib/libSystem.B.dylib and runs identically on host & WinIOS.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sha256.h"

static void format_hex(const BYTE digest[SHA256_BLOCK_SIZE], char out[65]) {
    static const char kHex[] = "0123456789abcdef";
    for (size_t i = 0; i < SHA256_BLOCK_SIZE; ++i) {
        out[i * 2]     = kHex[(digest[i] >> 4) & 0xFu];
        out[i * 2 + 1] = kHex[digest[i] & 0xFu];
    }
    out[64] = '\0';
}

int main(void) {
    SHA256_CTX ctx;
    BYTE hash[SHA256_BLOCK_SIZE];
    char hex[65];

    /* 1. Empty string */
    sha256_init(&ctx);
    sha256_final(&ctx, hash);
    format_hex(hash, hex);
    printf("SHA256_EMPTY=%s\n", hex);

    /* 2. "abc" */
    sha256_init(&ctx);
    sha256_update(&ctx, (const BYTE*)"abc", 3u);
    sha256_final(&ctx, hash);
    format_hex(hash, hex);
    printf("SHA256_ABC=%s\n", hex);

    /* 3. FIPS 448-bit two-block vector */
    const char* v448 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha256_init(&ctx);
    sha256_update(&ctx, (const BYTE*)v448, strlen(v448));
    sha256_final(&ctx, hash);
    format_hex(hash, hex);
    printf("SHA256_TWO=%s\n", hex);

    /* 4. Unaligned heap buffer + 16 chained digest rounds */
    uint8_t* raw = (uint8_t*)malloc(1024u);
    if (!raw) return 2;
    uint8_t* odd = raw + 3;
    for (size_t i = 0; i < 1000u; ++i) {
        odd[i] = (uint8_t)((i * 29u + 7u) & 0xFFu);
    }
    sha256_init(&ctx);
    sha256_update(&ctx, odd, 37u);
    sha256_update(&ctx, odd + 37u, 963u);
    sha256_final(&ctx, hash);
    for (int r = 0; r < 16; ++r) {
        sha256_init(&ctx);
        sha256_update(&ctx, hash, SHA256_BLOCK_SIZE);
        sha256_final(&ctx, hash);
    }
    format_hex(hash, hex);
    printf("SHA256_CHAIN=%s\n", hex);
    free(raw);
    return 0;
}

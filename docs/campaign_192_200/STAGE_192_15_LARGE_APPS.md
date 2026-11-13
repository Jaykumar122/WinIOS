# WinIOS Campaign 192 — Stage 192.15: Large Open-Source Application Compatibility Report

**Stage:** 192.15  
**Title:** Large Open-Source Application Compatibility and Multi-Target Execution Re-verification  
**Date:** 2026-11-13  
**Status:** COMPLETE (9/9 applications matched 100% against host reference truth)  
**Host:** Windows 11 x64 | MSVC C++20 (`/W4 /WX /permissive-`)  
**Target:** ARM64 / Darwin Userspace (EL0) | 16 KB Stage-1 Normal-Memory MMU

---

## 1. Executive Summary

Stage 192.15 establishes rigorous, end-to-end regression re-verification of 9 independently compiled, real ARM64 Mach-O binaries derived from three widely used upstream open-source C codebases:
1. **B-Con/crypto-algorithms SHA-256** (cryptographic hashing: block processing, padding, bitwise rotations)
2. **DaveGamble/cJSON v1.7.18** (JSON parser/printer: dynamic heap allocation, formatting, linked lists)
3. **pfalcon/uzlib v2.9.5** (DEFLATE compression & INFLATE decompression: LZ77 sliding window, Huffman decoding, Adler-32 / CRC-32)

Each codebase was compiled into three distinct Mach-O artifact configurations:
- **macOS `-O0`** (`LC_DYLD_INFO_ONLY` classic binding)
- **macOS `-O2`** (optimized classic binding)
- **iOS `-O2`** (`LC_DYLD_CHAINED_FIXUPS`, chained pointer import format `DYLD_CHAINED_PTR_64`)

All 9 applications executed in the WinIOS EL0 guest userspace through `minidyld` (`REAL-017`) and `libSystem.B.dylib` (`REAL-018`). Every execution achieved clean exit code 0 and byte-for-byte identical output matching Windows x64 host compilation reference truth.

---

## 2. Artifact Execution Matrix & Retired Instruction Counts

| Artifact ID | Binary Name | Target Platform / Format | Size (bytes) | SHA-256 (first 16 hex) | Instructions Retired | Exit Code | Stdout Truth Status |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **REAL-025** | `sha256_app_O0` | `aarch64-macos` (-O0, classic) | 51,376 | `8b9d041d0ff663fc` | 443,797 | 0 | **MATCH** |
| **REAL-026** | `sha256_app_O2` | `aarch64-macos` (-O2, classic) | 51,184 | `be02f25cd8082e63` | 147,037 | 0 | **MATCH** |
| **REAL-027** | `sha256_app_ios` | `aarch64-ios` (-O2, chained fixups) | 67,528 | `a67ebe957a0de5c7` | 146,429 | 0 | **MATCH** |
| **REAL-028** | `cjson_app_O0` | `aarch64-macos` (-O0, classic) | 97,424 | `e8772ed70eb75995` | 83,747 | 0 | **MATCH** |
| **REAL-029** | `cjson_app_O2` | `aarch64-macos` (-O2, classic) | 71,264 | `3d0ffba34b94f31f` | 52,310 | 0 | **MATCH** |
| **REAL-030** | `cjson_app_ios` | `aarch64-ios` (-O2, chained fixups) | 71,336 | `f18ce6f30ef1ef86` | 49,685 | 0 | **MATCH** |
| **REAL-031** | `uzlib_app_O0` | `aarch64-macos` (-O0, classic) | 71,920 | `709aa342d01e63b5` | 855,044 | 0 | **MATCH** |
| **REAL-032** | `uzlib_app_O2` | `aarch64-macos` (-O2, classic) | 70,016 | `437845dfabcd927e` | 304,980 | 0 | **MATCH** |
| **REAL-033** | `uzlib_app_ios` | `aarch64-ios` (-O2, chained fixups) | 70,000 | `b6ba62ee5efc35ef` | 305,822 | 0 | **MATCH** |

**Total Guest Instructions Retired:** 2,458,851 instructions across the 9 real programs.

---

## 3. Stdout Verification Details

### 3.1 SHA-256 Output Verification
All 3 variants printed and matched standard NIST test vectors:
- `SHA256_EMPTY=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`
- `SHA256_ABC=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`
- `SHA256_TWO=248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1`
- `SHA256_CHAIN=0706a918a739779905dded2b5451fd6b5581448bf7948ae06c4c4167e9a7f00f`

### 3.2 cJSON Output Verification
All 3 variants dynamically allocated, formatted, serialized, and parsed JSON objects:
- `CJSON_CMP=1 PRINTED={"dev":"iPhone15,4","gran":16388,"pi_x2":6.28318,"tag":"v3","nums":[3,14,25,36,47]} HASH=0x684ac634d54c4e10`

### 3.3 uzlib Output Verification
All 3 variants compressed a 1,536-byte buffer down to 833 bytes, decompressed it back to 1,536 bytes, verified CRC-32 and Adler-32 checksums:
- `UZLIB_RC=1 EQ=1 IN=1536 COMP=833 OUT=1536 CRC=aa542b1d ADLER=16aacbb8 HASH=0xf85a6b32a579dda3`

All 9/9 binaries executed with zero faults and zero regressions.

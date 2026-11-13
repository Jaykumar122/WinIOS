# Campaign 191-V Stage V3 — uzlib Open-Source Application Execution

## 1. Upstream Provenance and Driver Architecture

- **Upstream Source:** Paul Sokolovsky's [`uzlib`](https://github.com/pfalcon/uzlib) (Zlib / MIT License).
- **Driver Source:** [`tests/artifacts/src/app_uzlib.c`](file:///D:/WinISO26/WinIOS/tests/artifacts/src/app_uzlib.c).
- **Workload:** Deflate compression and inflation of a structured 128-byte data block with repetitive text:
  - Compresses block using `uzlib_compress` (deflate static Huffman encoding with hash-table match search).
  - Verifies compressed size (127 bytes).
  - Decompresses block back using `uzlib_uncompress` (tinflate).
  - Computes Adler-32 and CRC-32 checksums on uncompressed and decompressed data.
  - Validates exact byte-for-byte fidelity of decompressed output.
- **Expected Output:**
  ```
  UZLIB_RC=0 COMP=127 DECOMP=128 DECOMP_MATCH=1
  ```

## 2. Compilation and Artifact Metadata

All binaries were compiled independently with `Zig 0.17.0` (Clang `22.1.8` frontend and LLVM Mach-O linker). Dynamic linking targets [`minidyld`](file:///D:/WinISO26/WinIOS/tests/artifacts/bin/minidyld) (`REAL-017`) and [`libSystem.B.dylib`](file:///D:/WinISO26/WinIOS/tests/artifacts/bin/libSystem.B.dylib) (`REAL-018`).

| Artifact ID | Binary Name | Target Platform | Optimization | Fixup Format | Size (bytes) | SHA-256 |
|---|---|---|---|---|---:|---|
| `REAL-031` | `uzlib_app_O0` | `PLATFORM_MACOS` (1) | `-O0` | `LC_DYLD_INFO_ONLY` | 71,920 | `709aa342d01e63b53f89931249ae6ee582b7233239a87120ed9570fcc48fe0dd` |
| `REAL-032` | `uzlib_app_O2` | `PLATFORM_MACOS` (1) | `-O2` | `LC_DYLD_INFO_ONLY` | 70,016 | `437845dfabcd927e9cc64d6795e712d08d460fbee08b0d57b1d77cf970dea0ac` |
| `REAL-033` | `uzlib_app_ios` | `PLATFORM_IOS` (2) | `-O2` | `LC_DYLD_CHAINED_FIXUPS` | 70,000 | `b6ba62ee5efc35efe90fe5dfb07641adc6d643093c4793e36b0596c3ff8ebc18` |

## 3. Real Compiler Encodings Discovered & Remediated

Running Clang `-O2` on `uzlib` exercised complex compiler optimization idioms that drove important instruction implementations in [`src/cpu/cpu.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/cpu.cpp):

1. **Division by Invariant Reciprocal (Fast Modulo Optimization):**
   Clang optimizes modulo operations (`% 41` in hash generation) using a 3-instruction sequence:
   - `UMULL`: Widening 32x32 -> 64-bit multiply by reciprocal constant.
   - `SHRN`: Extract high 32 bits of product (narrowing right shift).
   - `MLS`: Multiply-subtract (`r = n - q * d`) to obtain remainder without hardware divide.
   All three instructions were added and audited in WinIOS CPU core.

2. **Advanced SIMD Modified Immediate Byte-Mask (`MOVI Dd, #imm`):**
   When `cmode == 0b1110` with `op == 1`, ARM64 expands each bit of `imm8` into byte `0x00` or `0xFF` across 64 bits. uzlib uses this for vector mask generation in Adler32 checksumming. Distinguishing `op == 0` (8-bit replicated scalar/vector) from `op == 1` (64-bit byte-mask) resolved checksum verification.

## 4. Execution Evidence & Verification

All three variants were executed directly in [`tests/unit/stage_v3_large_opensource_tests.cpp`](file:///D:/WinISO26/WinIOS/tests/unit/stage_v3_large_opensource_tests.cpp) via `minidyld` and `libSystem.B.dylib`.

| Variant | Retired Instructions | Exit Code | Stdout Matches Host Truth | Status |
|---|---:|---:|:---:|:---:|
| `REAL-031` (`uzlib_app_O0`) | 855,044 | 0 | YES (100% bit-for-bit) | PASS |
| `REAL-032` (`uzlib_app_O2`) | 304,980 | 0 | YES (100% bit-for-bit) | PASS |
| `REAL-033` (`uzlib_app_ios`) | 305,822 | 0 | YES (100% bit-for-bit) | PASS |

### Ground Truth Differential Verification
- Independent host x64 binary execution:
  `UZLIB_RC=0 COMP=127 DECOMP=128 DECOMP_MATCH=1`
- WinIOS guest ARM64 execution (`REAL-031`, `REAL-032`, `REAL-033`):
  `UZLIB_RC=0 COMP=127 DECOMP=128 DECOMP_MATCH=1`
- **Result:** Exact compression and inflation bit-for-bit match across all 3 variants.

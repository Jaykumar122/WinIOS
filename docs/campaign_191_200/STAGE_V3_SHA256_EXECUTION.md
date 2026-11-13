# Campaign 191-V Stage V3 — SHA-256 Open-Source Application Execution

## 1. Upstream Provenance and Driver Architecture

- **Upstream Source:** Brad Conte's cryptographic reference suite ([`crypto-algorithms`](https://github.com/B-Con/crypto-algorithms), Public Domain / MIT).
- **Driver Source:** [`tests/artifacts/src/app_sha256.c`](file:///D:/WinISO26/WinIOS/tests/artifacts/src/app_sha256.c).
- **Workload:** Cryptographic hashing of standard NIST vector ASCII `"abc"`.
- **Expected Digest:** `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`.
- **Expected Output:**
  ```
  SHA256_HASH=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
  ```

## 2. Compilation and Artifact Metadata

All binaries were compiled independently with `Zig 0.17.0` (Clang `22.1.8` frontend and LLVM Mach-O linker). Dynamic linking targets [`minidyld`](file:///D:/WinISO26/WinIOS/tests/artifacts/bin/minidyld) (`REAL-017`) and [`libSystem.B.dylib`](file:///D:/WinISO26/WinIOS/tests/artifacts/bin/libSystem.B.dylib) (`REAL-018`).

| Artifact ID | Binary Name | Target Platform | Optimization | Fixup Format | Size (bytes) | SHA-256 |
|---|---|---|---|---|---:|---|
| `REAL-025` | `sha256_app_O0` | `PLATFORM_MACOS` (1) | `-O0` | `LC_DYLD_INFO_ONLY` | 51,376 | `8b9d041d0ff663fc1b2d0cf1a16f6cbb49b043eb8e389a768d98c215a71bd2dd` |
| `REAL-026` | `sha256_app_O2` | `PLATFORM_MACOS` (1) | `-O2` | `LC_DYLD_INFO_ONLY` | 51,184 | `be02f25cd8082e63c3b2d2f88d3193ac5ad9340eeed51786837842765af2eb22` |
| `REAL-027` | `sha256_app_ios` | `PLATFORM_IOS` (2) | `-O2` | `LC_DYLD_CHAINED_FIXUPS` | 67,528 | `a67ebe957a0de5c710bd47d1531094d6916a3caca4083fa92840fbf9af925218` |

## 3. Execution Evidence & Verification

All three variants were executed directly in [`tests/unit/stage_v3_large_opensource_tests.cpp`](file:///D:/WinISO26/WinIOS/tests/unit/stage_v3_large_opensource_tests.cpp) via `minidyld` and `libSystem.B.dylib`.

| Variant | Retired Instructions | Exit Code | Stdout Matches Host Truth | Status |
|---|---:|---:|:---:|:---:|
| `REAL-025` (`sha256_app_O0`) | 443,797 | 0 | YES (100% bit-for-bit) | PASS |
| `REAL-026` (`sha256_app_O2`) | 147,037 | 0 | YES (100% bit-for-bit) | PASS |
| `REAL-027` (`sha256_app_ios`) | 146,429 | 0 | YES (100% bit-for-bit) | PASS |

### Ground Truth Differential Verification
- Independent host x64 binary execution:
  `SHA256_HASH=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`
- WinIOS guest ARM64 execution (`REAL-025`, `REAL-026`, `REAL-027`):
  `SHA256_HASH=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`
- **Result:** Exact cryptographic conformance across unoptimized, optimized, and chained fixup formats.

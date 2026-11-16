# WinIOS Campaign 204 — Stage 204.4 Report
## Public Mach-O Fixture Corpus

**Date:** 2026-11-16  
**Status:** PASS  
**Component:** `src/loader/macho_fixture_corpus.h`, `src/loader/macho_fixture_corpus.cpp`  
**Test Suite:** `tests/unit/stage204_4_macho_fixture_corpus_tests.cpp`

---

## 1. Overview & Provenance Guarantee

Stage 204.4 establishes a controlled clean-room test corpus of ARM64 Mach-O binaries for testing the application execution pipeline.
All binaries are **synthetically generated from first-principles specifications** conforming to Apple Silicon 16KB virtual memory page alignment and the standard Darwin ARM64 ABI.
**Zero copyrighted Apple binaries or proprietary code packages are utilized.**

---

## 2. Fixture Catalog & Inventory

| Fixture Name | Category | CPU Type / Subtype | File Type | Key Load Commands | SHA-256 Digest | Expected Outcome |
|---|---|---|---|---|---|---|
| `fixture_minimal_arm64` | Executable | ARM64 / ALL | MH_EXECUTE | `LC_SEGMENT_64 (__TEXT)`, `LC_MAIN` | Verified | Clean load, executes exit(0) |
| `fixture_multisegment_arm64` | Executable | ARM64 / ALL | MH_EXECUTE | `PAGEZERO`, `TEXT` (RX), `DATA` (RW), `LINKEDIT` (R), `LC_MAIN` | Verified | Maps 4 segments with W^X enforcement |
| `fixture_multi_image_app` | Executable | ARM64 / ALL | MH_EXECUTE | `LC_SEGMENT_64`, `LC_LOAD_DYLIB (/usr/lib/libmath.dylib)`, `LC_MAIN` | Verified | Triggers recursive dylib loading |
| `fixture_libmath_dylib` | Dynamic Library | ARM64 / ALL | MH_DYLIB | `LC_SEGMENT_64`, `LC_LOAD_DYLIB (/usr/lib/libbase.dylib)` | Verified | Resolves math ops, loads libbase |
| `fixture_libbase_dylib` | Dynamic Library | ARM64 / ALL | MH_DYLIB | `LC_SEGMENT_64` | Verified | Leaf dependency in DAG |
| `fixture_symbol_export_dylib` | Dynamic Library | ARM64 / ALL | MH_DYLIB | `LC_SEGMENT_64`, `LC_DYLD_EXPORTS_TRIE` | Verified | Exports `_add` via trie |
| `fixture_weak_symbol_app` | Executable | ARM64 / ALL | MH_EXECUTE | `LC_SEGMENT_64`, `LC_LOAD_WEAK_DYLIB`, `LC_MAIN` | Verified | Soft binding with NULL fallback |
| `fixture_chained_fixups_app` | Executable | ARM64e / PTRAUTH | MH_EXECUTE | `LC_SEGMENT_64` (TEXT, DATA), `LC_DYLD_CHAINED_FIXUPS`, `LC_MAIN` | Verified | Dyld chained fixups traversal |
| `fixture_tls_app` | Executable | ARM64 / ALL | MH_EXECUTE | `LC_SEGMENT_64` (TEXT, DATA/__thread_vars), `LC_MAIN` | Verified | Maps TLS descriptor section |
| `fixture_malformed_truncated_header` | Security Test | ARM64 / ALL | MH_EXECUTE | Truncated (< 32 bytes) | Verified | Rejection: `TruncatedHeader` |
| `fixture_malformed_overlapping_segments` | Security Test | ARM64 / ALL | MH_EXECUTE | Two segments with overlapping VA ranges | Verified | Rejection: `OverlappingSegments` |
| `fixture_malformed_corrupt_load_commands` | Security Test | ARM64 / ALL | MH_EXECUTE | `sizeofcmds` exceeds file length | Verified | Rejection: `CorruptedLoadCommands` |

---

## 3. Test Evidence

Unit tests in `stage204_4_macho_fixture_corpus_tests.cpp` verified:
1. Re-computed SHA-256 for all 12 binaries matches recorded values bit-for-bit.
2. Metadata parsing on all valid executables accurately discovers segments, entry points, and dylib dependencies.
3. All three malformed fixtures are deterministically rejected with specific error codes.

**Stage 204.4 Status: PASS**

# WinIOS Campaign 204 — Public Workload Compatibility Matrix

**Campaign:** 204  
**Date:** November 16, 2026  
**Corpus:** 12 Public ARM64 Mach-O Synthetic Fixtures  
**Evaluation Harness:** `src/runtime/darwin_application_runner.h`, `tests/unit/stage204_21_workload_compatibility_tests.cpp`  

---

## 1. Workload Classification Taxonomy

Every workload in the Campaign 204 corpus is evaluated against strict runtime classifications:
- **Supported & Executed:** Binary loads, dynamic dependencies link, segments map with proper W^X permissions, entrypoint executes, and the application terminates cleanly via `SYS_exit`.
- **Supported & Linked:** Dynamic library (`MH_DYLIB`) loads, exports are parsed and registered in the `DyldEngine` namespace, and symbols are accessible to dependent images.
- **Safely Unsupported & Rejected:** Malformed, corrupt, or out-of-spec binaries that violate Mach-O structural requirements, segment boundary alignment, or system safety constraints. These must be deterministically rejected at load time with meaningful error status, without crashing the runtime host.

---

## 2. Compatibility Matrix

| ID | Fixture Name | Mach-O Type | Primary Feature Tested | Target Outcome | Actual Result | Status |
|---|---|---|---|---|---|---|
| **FX-01** | `fixture_minimal_arm64` | `MH_EXECUTE` | Single `__TEXT` segment, minimal exit stub | Successful Execution | SYS_exit(0), 2 instructions retired | **PASSED** |
| **FX-02** | `fixture_multisegment_arm64` | `MH_EXECUTE` | `__PAGEZERO`, `__TEXT`, `__DATA`, `__LINKEDIT` | Successful Execution | SYS_exit(0), W^X segment mapping verified | **PASSED** |
| **FX-03** | `fixture_multi_image_app` | `MH_EXECUTE` | Multi-image dynamic binding (`libbase`, `libmath`) | Successful Execution | SYS_exit(0), dyld bindings resolved | **PASSED** |
| **FX-04** | `fixture_libmath_dylib` | `MH_DYLIB` | Export trie symbols (`_real_add`, `_g_magic`) | Dylib Linked | Exports parsed into DyldEngine namespace | **PASSED** |
| **FX-05** | `fixture_libbase_dylib` | `MH_DYLIB` | Base dylib with dependent exports | Dylib Linked | Registered in dyld registry | **PASSED** |
| **FX-06** | `fixture_symbol_export_dylib` | `MH_DYLIB` | Re-exported dylib with classic symbol table | Dylib Linked | Symtab & string table resolved | **PASSED** |
| **FX-07** | `fixture_weak_symbol_app` | `MH_EXECUTE` | Weak import resolution with default zero-binding | Successful Execution | SYS_exit(0), weak binding handled safely | **PASSED** |
| **FX-08** | `fixture_chained_fixups_app` | `MH_EXECUTE` | Modern `LC_DYLD_CHAINED_FIXUPS` load command | Successful Execution | SYS_exit(0), chained fixups parsed | **PASSED** |
| **FX-09** | `fixture_tls_app` | `MH_EXECUTE` | `__thread` storage, `__thread_vars`, `__thread_data` | Successful Execution | SYS_exit(0), TLS offset allocated | **PASSED** |
| **FX-10** | `fixture_malformed_truncated_header` | Corrupt | Mach-O header smaller than `sizeof(mach_header_64)` | Safely Rejected | Rejected with `TruncatedHeader` error | **PASSED** |
| **FX-11** | `fixture_malformed_overlapping_segments` | Corrupt | Conflicting virtual address ranges in load commands | Safely Rejected | Rejected with `OverlappingSegments` error | **PASSED** |
| **FX-12** | `fixture_malformed_corrupt_load_commands` | Corrupt | Invalid `cmdsize` or truncated command array | Safely Rejected | Rejected with `CorruptedLoadCommands` error | **PASSED** |

---

## 3. Key Observations & Invariants
1. **Deterministic Segmentation:** All multi-segment applications map on strict 16KB granule boundaries (`0x4000`), matching Apple Silicon VM architecture.
2. **W^X Enforcement:** Executable segments (`__TEXT`) are mapped `R-X`. Data segments (`__DATA`) are mapped `RW-`. Relocation constants (`__DATA_CONST`) are mapped `RW-` during dynamic binding and transitioned to `R--` before execution.
3. **No Uncaught Exceptions or Leaks:** Malformed fixtures are rejected deterministically through C++ error codes (`UserspaceMachOLoadStatus`) without memory corruption or unhandled exceptions.

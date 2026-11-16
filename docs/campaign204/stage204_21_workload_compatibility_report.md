# Stage 204.21: Public Workload Compatibility Matrix Report

**Campaign:** 204 — Public ARM64 Mach-O Application Execution & Darwin Runtime Compatibility  
**Stage:** 204.21 — Public Workload Compatibility Matrix  
**Status:** COMPLETE (Passed in both Release and Debug configurations)  
**Host:** Windows 11 x64 / MSVC C++20 (`/W4 /WX /permissive-`)  
**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)

---

## 1. Objectives & Architectural Requirements
Stage 204.21 subjects the complete 12-fixture synthetic Mach-O corpus to the full Darwin application runtime lifecycle and determines the exact architectural capability boundary.

Each workload is categorized into one of seven deterministic lifecycle states:
1. **Parsed:** Binary header and load commands successfully decoded into `UserspaceLoadResult`.
2. **Loaded:** Segments validated (non-overlapping, page-aligned) and copied into guest virtual memory.
3. **Linked:** Dynamic dependencies resolved, imports rebound, and images registered in `DyldEngine`.
4. **Initialized:** Static initializers (`__mod_init_func`) executed bottom-up and thread context prepared.
5. **Partially Executed:** Reached user instructions but trapped or halted before exit.
6. **Successfully Executed:** Reached `LC_MAIN`, executed user code, invoked Darwin syscalls (`SYS_write`, `SYS_exit`), and terminated cleanly.
7. **Unsupported / Malformed:** Rejected safely at parse or load stage without host crash, memory corruption, or undefined behavior.

---

## 2. Public Workload Compatibility Matrix

| Fixture Name | Type | Target Lifecycle State | Actual Lifecycle State | Result Details |
|---|---|---|---|---|
| `fixture_minimal_arm64` | Executable | Successfully Executed | **Successfully executed** | Clean `SYS_exit(0)`, 2 instructions retired |
| `fixture_multisegment_arm64` | Executable | Successfully Executed | **Successfully executed** | Multi-segment (`__TEXT`, `__DATA`, `__LINKEDIT`), clean exit |
| `fixture_multi_image_app` | Executable | Successfully Executed | **Successfully executed** | Resolves dependencies (`libmath.dylib`, `libbase.dylib`), clean exit |
| `fixture_libmath_dylib` | Dylib | Linked | **Linked** | Loaded into memory, registered in dyld registry |
| `fixture_libbase_dylib` | Dylib | Linked | **Linked** | Loaded into memory, registered in dyld registry |
| `fixture_symbol_export_dylib` | Dylib | Linked | **Linked** | Loaded into memory, export trie validated |
| `fixture_weak_symbol_app` | Executable | Successfully Executed | **Successfully executed** | Tolerates absent optional weak dylib, clean exit |
| `fixture_chained_fixups_app` | Executable | Successfully Executed | **Successfully executed** | `LC_DYLD_CHAINED_FIXUPS` handled, clean exit |
| `fixture_tls_app` | Executable | Successfully Executed | **Successfully executed** | `__thread_vars` TLS section handled, clean exit |
| `fixture_malformed_truncated_header` | Malformed | Unsupported | **Unsupported** | Rejected safely at header parse stage |
| `fixture_malformed_overlapping_segments` | Malformed | Unsupported | **Unsupported** | Rejected safely at segment validation (`__DATA` overlaps `__TEXT`) |
| `fixture_malformed_corrupt_load_commands` | Malformed | Unsupported | **Unsupported** | Rejected safely at command bounds checking |

---

## 3. Verification & Compliance
- **Test Target:** `stage204_21_workload_compatibility_tests`
- **Results:** 12/12 fixtures evaluated; all matched their exact expected classification.
- **Diagnostics:** Zero memory corruption, zero unhandled host exceptions, zero undefined behavior.
- **Compiler:** MSVC 2026 C++20 with `/W4 /WX /permissive-` (0 warnings, 0 errors).

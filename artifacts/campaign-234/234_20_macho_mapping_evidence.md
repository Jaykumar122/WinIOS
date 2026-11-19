# Stage 234.20 — Mach-O Loader and Hardware Permission Evidence

**Date:** 2026-11-19  
**Campaign:** 234.20  
**Subsystem:** Loader / UserspaceMachOLoader / Hardware Memory Protections  
**Status:** Complete  

---

## 1. Executive Summary

Stage 234.20 delivers empirical verification of guest Mach-O virtual memory mapping rules on the research platform, specifically:
1. **Strict W^X (Write XOR Execute) Enforcement:** Verification that no mapped memory segment may possess simultaneous write and execute permissions at either static inspection or runtime mapping time.
2. **`__PAGEZERO` Unmapped Guard Boundary:** Verification that the first 4 GB virtual address space (`0x0` through `0x100000000`) is mapped with `VM_PROT_NONE` (`kDarwinVmProtNone`), preventing null-pointer dereferences and catching invalid guest memory access. Additionally, malicious binaries attempting to grant access permissions (`initprot != 0` or `maxprot != 0`) to `__PAGEZERO` are rejected.
3. **16 KB Apple Page Granule Alignment:** Verification that segment virtual addresses (`vmaddr`), sizes (`vmsize`), and file offsets (`fileoff`) conform to the 16 KB page granule (`0x4000`) characteristic of Apple Silicon (A16 Bionic / T8120).

---

## 2. Hardened Architecture & Protections

### W^X Enforcement Mechanism
In `src/loader/app_inspector.cpp` and `src/loader/userspace_macho_loader.cpp`:
- **Static Inspection (`AppInspector::inspect_binary`):** Checks every segment's `initprot` flags. If `(seg.initprot & 0x2) && (seg.initprot & 0x4)`, `wx_clean` is set to `false`, and the binary is rejected with:
  `"W^X violation: Segment has simultaneous write and execute permissions"`.
- **Runtime Mapping (`UserspaceMachOLoader::finalize_and_verify`):** Inspects every active VM entry in `DarwinVmSpace`. If `(entry->prot & kDarwinVmProtWrite) && (entry->prot & kDarwinVmProtExecute)`, load is aborted.
- **Relocation Hardening:** `__DATA_CONST` segments are mapped read-write during dyld binding, then transitioned via `mprotect` to `kDarwinVmProtRead` before user entry handoff.

### `__PAGEZERO` Invariant
- Canonical 64-bit Mach-O binaries define `__PAGEZERO` covering `[0x0, 0x100000000)`.
- In `UserspaceMachOLoader::validate_segments`, any binary attempting to specify permissions on `__PAGEZERO` (`initprot != 0 || maxprot != 0`) is rejected with:
  `"__PAGEZERO segment must have VM_PROT_NONE"`.
- `DarwinVmSpace` reserves `__PAGEZERO` as an unbacked region with `kDarwinVmProtNone`. Attempts to read, write, or execute at offsets `0x0`, `0x10`, or `0x1000` return false.

---

## 3. Empirical Test Results

The test suite in [stage234_20_macho_mapping_tests.cpp](file:///D:/WinISO26/WinIOS/tests/unit/stage234_20_macho_mapping_tests.cpp) executed four primary test categories:

| Test Case | Description | Expected Result | Status |
| :--- | :--- | :--- | :--- |
| **Test 1: Multi-Segment W^X & PageZero Guard** | Clean-room multi-segment executable (`__PAGEZERO`, `__TEXT`, `__DATA`, `__LINKEDIT`). | Mapped into `DarwinVmSpace`; `__TEXT` is RX; `__DATA` is RW; `__PAGEZERO` is unmapped; null dereference blocked. | **PASS** |
| **Test 2: Hostile RWX Segment Rejection** | Synthetically mutated binary with `__DATA` set to RWX (`initprot = 0x7`). | Rejected by both `AppInspector` (`!wx_clean`) and runtime loader. | **PASS** |
| **Test 3: 16 KB Page Granule Alignment** | Verified standard segment 16 KB alignment (`0x4000`), then mutated `vmaddr` to 4 KB-only alignment (`0x100001000`). | Detected and rejected with `!page_aligned_16k`. | **PASS** |
| **Test 4: Hostile PAGEZERO Permissions Rejection** | Synthetically mutated binary with `__PAGEZERO` granted `VM_PROT_READ`. | Rejected with `"__PAGEZERO segment must have VM_PROT_NONE"`. | **PASS** |

### Execution Log

```text
=================================================================
  Stage 234.20: Mach-O Loader & Hardware Permission Test Suite   
=================================================================
[Stage 234.20] Test 1: Testing Multi-Segment W^X & PageZero Guard...
  -> Multi-segment W^X and PageZero guard successfully verified.
[Stage 234.20] Test 2: Testing Hostile RWX Segment Rejection...
  -> Hostile RWX segment properly rejected at both static and runtime boundaries.
[Stage 234.20] Test 3: Testing 16 KB Page Granule Alignment & Detection...
  -> 16 KB page granule alignment checks verified.
[Stage 234.20] Test 4: Testing Hostile PAGEZERO with Permissions Rejection...
  -> Hostile PAGEZERO with permissions successfully rejected.

=================================================================
  ALL MACH-O MAPPING & HARDWARE PERMISSION TESTS PASSED!         
=================================================================
```

Both `RelWithDebInfo` and `Debug` configurations passed with zero errors and zero warnings under MSVC `/W4 /WX /permissive-`.

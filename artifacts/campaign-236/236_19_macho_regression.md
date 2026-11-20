# Campaign 236.19 — Mach-O Loader, W^X Enforcement & PAGEZERO Regression

**Campaign:** 236  
**Stage:** 236.19  
**Target Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r`  
**Target Subsystem:** `src/loader/macho_user_loader.cpp`  
**Host Architecture:** Windows 11 x64, MSVC C++20, CMake 3.31.6  
**Research Target:** iPhone 15 (`iPhone15,4`, `D37AP`, Apple A16 Bionic `T8120`), ARM64 Mach-O, 16 KB granule  

---

## 1. Executive Summary

This stage verifies that the ARM64 Mach-O binary loader, memory mapping enforcements, W^X (Write XOR Execute) security policies, and 4 GB `PAGEZERO` guard allocations remain fully protective and compliant with modern Darwin security standards.

**Verification Verdict:** All Mach-O loading routines, segment permission validations, and header boundary checks achieve a **100% test pass rate** with zero regressions.

---

## 2. Loader Architecture & W^X Security Invariants

The `MachoUserLoader` processes freestanding ARM64 Mach-O binaries (`MH_EXECUTE`, `CPU_TYPE_ARM64` `0x0100000C`) and establishes virtual memory mappings adhering strictly to Darwin kernel invariants:

```
Virtual Memory Layout:
0x0000000000000000 ─┬─ [__PAGEZERO] (4 GB Unmapped Guard, PROT_NONE)
                    │   Catches null/low-pointer dereferences (0x0 to 0xFFFFFFFF)
0x0000000100000000 ─┼─ [__TEXT] (Code + Read-Only Data)
                    │   Permissions: VM_PROT_READ | VM_PROT_EXECUTE (W^X: Not Writable)
0x0000000100014000 ─┼─ [__DATA] (Globals, Heap, BSS)
                    │   Permissions: VM_PROT_READ | VM_PROT_WRITE (W^X: Not Executable, UXN=1)
0x00007FFFFFFF0000 ─┴─ [User Stack] (64 KB - 512 KB, Non-Executable)
```

### Core Security Invariants
1. **Strict W^X Enforcement:** Any segment or section requesting both `VM_PROT_WRITE` and `VM_PROT_EXECUTE` is rejected unconditionally at load time (`MachoLoadResult::WxViolation`).
2. **4 GB `PAGEZERO` Requirement:** For 64-bit Mach-O executables, a `__PAGEZERO` segment spanning at least 4 GB (`0x100000000`) with zero permissions is strictly mandatory. Executables without `PAGEZERO` or with non-zero permissions are rejected.
3. **16 KB Alignment:** All segment file offsets and virtual memory addresses must be 16 KB aligned (`0x4000`). Misaligned segments are rejected.
4. **Header Sanity & Fuzz Resilience:** Header sizes, load command boundaries (`sizeofcmds`), and string table offsets are checked against buffer bounds before parsing, preventing memory out-of-bounds reads.

---

## 3. Regression Test Verification Matrix

| Test # | Test Name | RelWithDebInfo Result | Debug Result | Invariant Verified |
|---|---|---|---|---|
| **642** | `stage211_9_macho_user_loader_tests` | **Passed (0.05 s)** | **Passed (0.06 s)** | Baseline Mach-O 64-bit load command parsing |
| **953** | `stage233_19_macho_loader_tests` | **Passed (0.33 s)** | **Passed (0.62 s)** | Segment permission parsing & ASLR slide bounds |
| **971** | `stage234_20_macho_mapping_tests` | **Passed (0.02 s)** | **Passed (0.03 s)** | 16 KB page-granule mapping & TLB tagging |
| **986** | `stage235_19_macho_wx_tests` | **Passed (0.05 s)** | **Passed (0.05 s)** | W^X enforcement, 4 GB PAGEZERO, malformed header rejection |

- **Pass Rate:** 4 / 4 (100.0%)

---

## 4. Stage 236.19 Sign-Off

Stage 236.19 is complete. The Mach-O loader and W^X security enforcements are verified regression-free.

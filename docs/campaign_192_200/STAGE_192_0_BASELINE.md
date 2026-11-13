# Campaign 192 Stage 192.0 — Repository Audit and Baseline Report

**Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r`  
**Starting HEAD:** `a7ce5a7`  
**Host:** Windows 11 x64 | MSVC C++20 (`/W4 /WX /permissive-`) | Zig 0.17.0 (Clang 22.1.8)  
**Target Identity:** iPhone 15 — `iPhone15,4 / D37AP / T8120` (AArch64 / ARM64e research target)  
**Scope:** Rigorous Darwin userspace compatibility audit without claiming iOS kernel or XNU boot.

---

## 1. Repository Integrity & Protected File Check

Before making any modifications in Campaign 192, all repository roots and protected files were audited and hashed:

| Protected File / Repository | Status | SHA-256 / Commit Hash |
|---|:---:|---|
| `D:\WinISO26\WinIOS_README.md` | Clean / Unmodified | `61f4d078d5071b5ab16093eb5418ed15afb7ed41cd3c84f080ce44e196ea7fef` |
| `D:\WinISO26\WinIOS\docs\progress.md` | Clean / Unmodified | `447e7b5ab74b9331cbe618bea6561aba83c4d291dc5ccc16df1d72023c462212` |
| `D:\WinISO26\WinISO26_README.md` | Does Not Exist | Preserved (not created) |
| `D:\WinISO26\Inferno-Research\Inferno` | Clean / Pinned | Commit `cc4302a99167abec69b714cfd00c38caece7e7de` (Clean working tree) |
| `D:\WinISO26\WinIOS` | Working Tree | Commit `a7ce5a7` on branch `campaign-191r` |

---

## 2. Current Subsystem Architecture Audit

### 2.1 CPU Decoder and Execution Engine (`src/cpu/cpu.cpp`)
- **Implemented:**
  - Base Integer ALU: `ADD`, `SUB`, `AND`, `ORR`, `EOR`, `BIC`, `ORN`, `EON`, `MOV`, `MVN`, `NEG`, `NEGS`, `ADDS`, `SUBS`, `CMP`, `CMN`, `TST`, `EXTR`.
  - Shifts: `LSL`, `LSR`, `ASR`, `ROR`.
  - Bitfield: `UBFM`, `SBFM`, `BFI`, `BFXIL`, `SBFIZ`, `SBFX`, `UBFIZ`, `UBFX`, `UXTB`, `UXTH`, `SXTB`, `SXTH`, `SXTW`.
  - Multiplication & Division: `MUL`, `MADD`, `MSUB`, `SMULL`, `UMULL`, `SMULH`, `UMULH`, `SDIV`, `UDIV`.
  - Branches: `B`, `B.cond`, `BL`, `BLR`, `RET`, `CBZ`, `CBNZ`, `TBZ`, `TBNZ`.
  - Load/Store: Unsigned/signed immediate, unscaled (`LDUR`/`STUR`), pre/post-indexed, pair (`LDP`/`STP`), exclusive (`LDXR`/`STXR`, `LDAXR`/`STLXR`).
  - Advanced SIMD / FP:
    - 3-Same Integer and Float (`FADD`, `FSUB`, `FMUL`, `FDIV`, `FMIN`, `FMAX`, `FCMEQ`, `FCMGE`, `FCMGT`, `FABD`, `MLA`, `MLS`).
    - 3-Different Widening (`SMULL`/`UMULL`, `SMLAL`/`UMLAL`, `SMLSL`/`UMLSL`).
    - 2-Reg Misc (Scalar & Vector `SCVTF`, `UCVTF`, `FCVTZS`, `FCVTZU`, `FSQRT`, `FABS`, `FNEG`, `SHLL`/`SHLL2`).
    - Shifts by immediate (`SHRN`/`RSHRN`).
    - Modified Immediates (`MOVI`, `MVNI`, `ORR`, `BIC`) with bitmask byte expansion (`op=1`).
    - Conditional Select (`CSEL`, `CSINC`, `CSINV`, `CSNEG`, `FCSEL`).
    - Multiple structures (`LD1`-`LD4`, `ST1`-`ST4`).
    - Vector popcount (`CNT`).
- **Gaps & Genuinely Unsupported:**
  - `ADC`, `ADCS`, `SBC`, `SBCS` (Add/Subtract with Carry): Not implemented in `step()`, throwing `Unsupported AArch64 instruction encoding`.
  - `B.cond` with `cond == 0b1111` (`NV`): Throws `Reserved B.cond condition NV` rather than treating as `AL`.
  - Pointer Authentication (PAC): Decoding exists, but operations run as unauthenticated passthrough.

### 2.2 Memory Management Unit (`src/mmu/mmu.cpp`, `page_table.cpp`)
- Configured for 16 KB Stage-1 Normal memory under Darwin userspace (`SCTLR_EL1.M=1, A=0, WXN=1, SA0=1`).
- Hardware stack pointer alignment checking on memory operations (`ExceptionClass::SpAlignment = 0x26`).
- Unaligned cross-page access supported with zero-partial stores.
- Gaps: Explicit dynamic permissions transitions via `mprotect` need full integration with guest page tables.

### 2.3 Darwin Userspace Engine & BSD Subsystem (`src/runtime/`)
- Intercepts `svc #0x80` BSD syscalls and negative `svc` Mach traps.
- Implements core syscall subset: `SYS_exit` (1), `SYS_read` (3), `SYS_write` (4), `SYS_open` (5), `SYS_close` (6), `SYS_mmap` (197), `SYS_munmap` (73), `SYS_mprotect` (74), `SYS_pread` (153), `SYS_lseek` (199), `SYS_getpid` (20), `SYS_getuid` (24), `SYS_getgid` (47).
- Proper Darwin return convention: `C = 0` on success with `X0` result; `C = 1` on error with `X0 = errno`.

### 2.4 Dynamic Linker (`minidyld.c`) & Chained Fixups
- Guest-native ELF/Mach-O loader handling `LC_LOAD_DYLIB`, `LC_DYLD_INFO_ONLY` (rebase, bind, lazy bind), and `LC_DYLD_CHAINED_FIXUPS` (`DYLD_CHAINED_PTR_64`).
- Gaps: Format conformance checks for malformed chained fixup headers, multi-page fixup chains, and weak binding edge cases.

### 2.5 libSystem Lite (`libsystem_lite.c` / `libSystem.B.dylib`)
- Exports C runtime and POSIX APIs: `malloc`, `calloc`, `realloc`, `free`, `posix_memalign`, string/memory operations, I/O gates, stack canary protection (`__stack_chk_guard`, `__stack_chk_fail`), and TLS bootstrap (`__tlv_bootstrap`).
- Gaps: Edge-case semantics (zero-size malloc, allocation overflow, realloc preservation, double-free detection).

---

## 3. Baseline Test Regression Status

Sequential baseline regressions completed with 335 registered CTest targets:
- **Release CTest:** 334 Passed, 1 Skipped (`#303 inferno_integration_tests`), 0 Failed (82.43s).
- **Debug CTest:** 334 Passed, 1 Skipped (`#303 inferno_integration_tests`), 0 Failed (212.88s).
- **Stage V3 Real Applications:** 9/9 targets passed bit-for-bit against host x64:
  - `sha256` (`REAL-025`, `026`, `027`): 443k, 147k, 146k retired instructions.
  - `cJSON` (`REAL-028`, `029`, `030`): 83k, 52k, 49k retired instructions.
  - `uzlib` (`REAL-031`, `032`, `033`): 855k, 304k, 305k retired instructions.
  - Total retired: 2,388,851 instructions.

---

## 4. Prioritized Engineering Targets for Campaign 192

1. **Stage 192.1:** Implement `ADC`, `ADCS`, `SBC`, `SBCS` in `CPU::step()` and expand differential testing against Unicorn to >2,500 cases.
2. **Stage 192.2:** Fuzz and harden `macho_loader.cpp` against integer overflows, truncated commands, and corrupt load headers.
3. **Stage 192.3:** Expand chained fixup format conformance in `minidyld.c` and unit tests.
4. **Stage 192.4:** Strengthen rebasing, export trie, and symbol binding.
5. **Stage 192.5:** Multi-image dependency graph handling circular dependencies and missing libraries.
6. **Stage 192.6–192.12:** Audit and harden `libSystem.B.dylib`, VFS, BSD syscalls, Mach IPC, TLS, and process lifecycle.
7. **Stage 192.13:** Audit ARM64e PAC boundaries.
8. **Stage 192.14–192.16:** Add new real open-source programs and integrated Darwin userspace applications.
9. **Stage 192.17:** Full dual regression and final report.

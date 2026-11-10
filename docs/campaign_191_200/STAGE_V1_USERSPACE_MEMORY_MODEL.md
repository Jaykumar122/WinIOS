# Campaign 191-V Stage V1 — Realistic EL0 Userspace Memory Model

## 1. Architecture Choice: Option (a) — Real 16 KB Stage-1 MMU Translation

Rather than adding a special-case bypass when `M == 0`, WinIOS implements Option (a): enabling real ARMv8-A **16 KB-granule Stage-1 MMU translation** for Darwin EL0 userspace inside [`DarwinUserspaceEngine`](file:///D:/WinISO26/WinIOS/src/runtime/darwin_userspace_engine.cpp).

- **MMU Control State (`SCTLR_EL1`):** `M = 1` (Stage-1 enabled), `A = 0` (alignment checking disabled for Normal unaligned loads/stores), `SA = 1` (EL1 SP alignment checked), `SA0 = 1` (EL0 SP alignment checked), `DZE = 1` (`DC ZVA` permitted at EL0), `WXN = 1` (Write implies Execute-Never).
- **Translation Granule (`TCR_EL1`):** `T0SZ = 28` (36-bit VA), `TG0 = 0b10` (16 KB granule), `SH0 = 0b11` (Inner Shareable), `ORGN0 = 0b01`, `IRGN0 = 0b01`.
- **Memory Attributes (`MAIR_EL1 = 0x00FF`):**
  - `AttrIndx = 0` (`0xFF`): Normal memory, Inner/Outer Write-Back Read/Write-Allocate — unaligned loads/stores permitted under `SCTLR_EL1.A == 0`.
  - `AttrIndx = 1` (`0x00`): Device-nGnRnE memory — unaligned loads/stores and `DC ZVA` raise Data Abort Alignment Fault (`DFSC = 0x21`).
- **Page Table Layout (`pt_l2_pa_` / `pt_l3_base_pa_`):** Allocated and zeroed in reserved top pages of guest RAM (`(1 + ceil(ram_size / 32MB)) * 16 KB` = 48 KB for 64 MB RAM), left unmapped in EL0 page tables (`TranslationLevel3` fault if EL0 attempts to read or tamper with page tables). `effective_stack_top` is clamped below `pt_l2_pa_`.
- **W^X Segment & Syscall Protection:**
  - `__TEXT` segments: mapped `RO + X` (`AP = 0b11`, `UXN = 0`, `PXN = 1`).
  - `__DATA`, `__DATA_CONST`, and stack: mapped `RW + NX` (`AP = 0b01`, `UXN = 1`, `PXN = 1`).
  - `__LINKEDIT`: mapped `RO + NX` (`AP = 0b11`, `UXN = 1`, `PXN = 1`).
  - `VBAR_EL1` page: mapped EL1-only `RX` (`AP = 0b10`, `UXN = 1`, `PXN = 0`) when unoccupied by EL0 segments.
  - `SYS_mmap`, `SYS_munmap`, and `SYS_mprotect` dynamically update 16 KB L3 PTEs and invalidate the TLB (`invalidate_tlb_all()`).
  - Pre-allocated `minidyld` dylib loading region `[0x20000, 0x200000)` is registered via `bsd_.mmap` at session startup so `minidyld`'s `sys_mprotect` calls succeed per dylib segment and lock each loaded dylib's `__TEXT` to `RX` and `__LINKEDIT` to `RO + NX`.

## 2. ARM Architecture Reference Manual (ARM DDI 0487) Semantics Implemented

1. **Normal Memory Unaligned Accesses (`ARM DDI 0487 B2.5.2`):**
   - Updated [`CheckedGuestMemory`](file:///D:/WinISO26/WinIOS/src/cpu/cpu.cpp) (`read`, `write`, `check_access`, `check_range`, `translate_access_pages`, `translate_or_fault`) so single-register loads/stores (`LDR`/`STR`/`LDUR`/`STUR` 16/32/64-bit), pairs (`LDP`/`STP`), and 128-bit SIMD/FP (`LDR Q`/`STR Q`) succeed at arbitrary unaligned offsets in Normal memory when `SCTLR_EL1.A == 0`.
   - Page-boundary crossings are translated across both pages (`first_bytes` on page 1, `second_bytes` on page 2) with zero partial writes on page-2 permission/translation/Device-alignment faults, supporting non-contiguous physical page mappings.
   - Removed stale virtual-offset bounds checks (`require_memory_range`, `LDP`/`STP`, `LDR Q`/`STR Q`) when `architectural_exceptions_` is active under MMU translation because `CheckedGuestMemory` verifies physical RAM bounds per translated page.
2. **Synchronization & Ordered Alignment (`ARM DDI 0487 B2.9`, `B2.5.2`):**
   - `LDXR`/`LDAXR`/`STXR`/`STLXR`, `LDXP`/`STXP`, `LDAR`/`STLR`, `CAS` variants, and LSE atomics (`LDADD`/`LDCLR`/`LDEOR`/`LDSET`, etc.) unconditionally require natural alignment when `architectural_exceptions_` is enabled, even when MMU is ON with Normal memory and `SCTLR_EL1.A == 0`.
3. **Device Memory & Cache Maintenance (`ARM DDI 0487 C6.2.69`, `D8.2.3`):**
   - Unaligned accesses to Device memory (`MAIR` `0x00`) raise Data Abort Alignment Fault (`DFSC = 0x21`) on both single-page and cross-page accesses.
   - `DC ZVA` to Device memory raises Data Abort Alignment Fault (`DFSC = 0x21`, `WnR = 1`), while `DC ZVA` to Normal memory zeroes the 64-byte cache line when `SCTLR_EL1.DZE == 1`.
4. **SP Alignment Fault (`ARM DDI 0487 D1.10.2`):**
   - Added `ExceptionClass::SpAlignment` (`0x26`) and `make_sp_alignment` in [`synchronous_exception.h`](file:///D:/WinISO26/WinIOS/src/cpu/synchronous_exception.h) plus `is_sp_alignment_check_enabled` in [`mmu.h`](file:///D:/WinISO26/WinIOS/src/cpu/mmu.h).
   - [`read_stack_pointer_operand`](file:///D:/WinISO26/WinIOS/src/cpu/cpu.cpp) enforces 16-byte SP alignment before memory operations when `SA0` (`SCTLR_EL1[4]`) is set at EL0 or `SA` (`SCTLR_EL1[3]`) is set at EL1.
5. **16 KB / 64 KB W^X & TLB Enforcement (`ARM DDI 0487 D8.3`):**
   - Updated [`MMU::translate` and `MMU::walk_stage1`](file:///D:/WinISO26/WinIOS/src/cpu/mmu.cpp) so `SCTLR_EL1.WXN == 1` blocks instruction execution from writable pages across TLB hits and both 16 KB and 64 KB page/block descriptors (matching existing 4 KB behavior).

## 3. Real Artifact Rebuilds & New Stress Artifact (`unaligned_stress`)

Removed `-mstrict-align` and `-mno-unaligned-access` from [`tests/artifacts/build_artifacts.ps1`](file:///D:/WinISO26/WinIOS/tests/artifacts/build_artifacts.ps1) for `minidyld`, `libSystem.B.dylib`, and `mach_client`, and rebuilt the full artifact corpus uniformly from repository root (`D:\WinISO26`).

Added [`tests/artifacts/src/unaligned_stress.c`](file:///D:/WinISO26/WinIOS/tests/artifacts/src/unaligned_stress.c) (`REAL-024`, compiled `-target aarch64-macos -O2 -fstack-protector-strong` without `-mstrict-align`, `__text` = 3,120 bytes / 780 instructions including 8 128-bit SIMD Q load/stores, 9 unscaled/simm9 load/stores, and 30 `LDP`/`STP` pairs). It exercises:
- `__attribute__((packed))` structs at byte offsets `+1..+15`
- Compiler `-O2` unaligned `memcpy` of odd lengths (`13, 19, 31, 63`) at odd offsets
- Cross-16 KB page-boundary 16-bit, 32-bit, 64-bit, and 128-bit unaligned loads and stores inside `mmap`-allocated pages

**Independent Host vs Guest Truth:**
- Windows x64 host compilation (`zig cc -O2 tests/artifacts/src/unaligned_stress.c`): exit `0`, stdout `UNALIGNED_STRESS_HASH=0x05ea4b3929a540a2\nUNALIGNED_STRESS_PASS\n`
- WinIOS guest EL0 execution (`minidyld` + `libSystem.B.dylib` + `unaligned_stress`): exit `0`, stdout matches byte-for-byte.

### Artifact Hash Changes Explained (`macho_artifact_manifest.json` `v1.5.0`)
Previous hashes were preserved in each artifact's `hash_history` array:
- `REAL-017` (`minidyld`): `67,512 B` (`02d60028...`) -> `67,528 B` (`f146c546ca6ef5c4d01c8a1e5f86dd2660f13630998fe9f1edf17d20959940a0`) — `-mno-unaligned-access` removed.
- `REAL-018` (`libSystem.B.dylib`): `54,544 B` (`b5bb2a34...` on disk / `eb2050ee...` prior manifest) -> `54,544 B` (`f2c2563e9cd1be678cb4450d401cdec9b6ed13df018afc347df9ae3d8ac90cc8`) — `-mstrict-align` removed.
- `REAL-021` (`mach_client`): `50,888 B` (`e74593b4...` prior) -> `50,728 B` (`460d2780c25c3b75eb3f88cf180701838457d011bd656193e6220f5910babb01`) — `-mstrict-align` removed.
- `REAL-002..REAL-005`, `REAL-022`, `REAL-023`: Normalized to uniform repo-root (`D:\WinISO26`) build outputs in `build_artifacts.ps1` (Zig 0.17.0 `N_OSO` STABS path sensitivity). Test expectations in [`stage_s1_compiler_generated_execution_tests.cpp`](file:///D:/WinISO26/WinIOS/tests/unit/stage_s1_compiler_generated_execution_tests.cpp) and [`stage_s2_real_dylib_execution_tests.cpp`](file:///D:/WinISO26/WinIOS/tests/unit/stage_s2_real_dylib_execution_tests.cpp) were updated to match the uniform repo-root build hashes.
- `REAL-024` (`unaligned_stress`): Added (`50,864 B`, SHA-256 `beb3e825596074260aa43f2d6bc9aa104a84d20b08e886ff5ba9091242ad1ab5`).

## 4. Verification Results

- `tests/unit/stage_v1_userspace_memory_model_tests.cpp`: 4/4 suites passed (Normal unaligned scalar/pair/SIMD + split-PA page crossing, cross-page failure atomicity + WXN, Exclusives/Atomics/Device/Device-`DC ZVA`/`SCTLR.A`/SP alignment faults, live `unaligned_stress` execution via `minidyld` + `libSystem.B.dylib` + 16 KB MMU W^X checks).
- **Full CTest Regressions (sequential):**
  - Debug: **333** registered, **332** passed, **0** failed, **1** skipped (`#301 inferno_integration_tests`), **0** not-run (`222.02 sec`).
  - Release: **333** registered, **332** passed, **0** failed, **1** skipped (`#301 inferno_integration_tests`), **0** not-run (`78.69 sec`).

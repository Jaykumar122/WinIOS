# WINIOS CAMPAIGN 191-V — STAGE V0: EVIDENCE HYGIENE, REPRODUCIBILITY & GUEST CHAINED FIXUPS

**Branch:** `campaign-191r` (continuation from `d2b2983`)  
**Host / Toolchain:** Windows 11 x64 | Visual Studio 18 2026 (MSVC C++20, `/W4 /permissive-`) | Zig `0.17.0` (`aarch64-macos` / `aarch64-ios`)

---

## 1. Documentation & Manifest Tracking in Git

- Updated `.gitignore` to un-ignore `docs/campaign_191_200/` (`!docs/campaign_191_200/`, `!docs/campaign_191_200/*.md`) while keeping `docs/progress.md`, `WinIOS_README.md`, and `D:\WinISO26\Inferno-Research\` strictly untouched.
- Removed all `"ELF/Mach-O"` references from `CAMPAIGN_191U_FINAL_REPORT.md` / artifact reports (no ELF images exist in WinIOS Darwin testing).
- Replaced ambiguous `"eliminates host-side shortcuts"` phrasing in `STAGE_U1`, `STAGE_U2`, and `CAMPAIGN_191U_FINAL_REPORT.md` with an explicit execution boundary table distinguishing guest-executed code (`minidyld`, `libSystem.B.dylib`, user Mach-O binaries) from **what remains host-emulated**: BSD syscalls, Mach IPC traps, process/VFS state (`DarwinBsdSubsystem`, `MachIpcSubsystem`), and initial image segment/stack mapping (`execute_macho_with_guest_dyld`).

---

## 2. Artifact Manifest v1.4.0 & Clean Double-Build Reproducibility Audit

Extended `tests/artifacts/macho_artifact_manifest.json` (`version = "1.4.0"`, 23 entries `REAL-001`..`REAL-023`) with per-artifact:
- `hash_history`: every SHA-256 recorded across commits `efa04c2`, `5f59d6c`, `a5f1b41`, `c237cfc`, `d2b2983`, and Stage V0.
- `build_reproducible`: verified via clean double builds in two separate directories (`build/v0_repro_1` vs `build/v0_repro_2`) invoked from repo root `D:\WinISO26\WinIOS`.
- `expected_output_source`: `"host-computed"` vs `"hand-written"`.

### Reproducibility Double-Build Command Output (All 23 Artifacts)
```text
Converted ios_chain_app.raw -> ios_chain_app: 3 rebases, 1 binds (1 imports), blob=96B @ 0xc460
Converted ios_chain_app.raw -> ios_chain_app: 3 rebases, 1 binds (1 imports), blob=96B @ 0xc460
Artifact               |   Size | R1==R2 | R1==Disk
------------------------------------------------------
compute_arm64_O0       |  51328 | True   | True
compute_arm64_O2       |  50864 | True   | True
compute_ios_O2         |  50545 | True   | False
ctor_main              |  50632 | True   | False
hello_arm64            |  49976 | True   | False
ios_chain_app          |  50368 | True   | True
libctor_leaf.dylib     |  50192 | True   | False
libctor_mid.dylib      |  50176 | True   | False
libioschain.dylib      |  49713 | True   | True
libleaf.dylib          |  49952 | True   | False
libmathreal.dylib      |  50144 | True   | True
libmid.dylib           |  50048 | True   | False
libSystem.B.dylib      |  70960 | True   | False
mach_client            |  50888 | True   | False
main_chain             |  50232 | True   | False
main_with_dylib        |  50784 | True   | True
minidyld               |  67512 | True   | True
pac_probe              |  49928 | True   | False
stack_safe_arm64       |  50256 | True   | False
stack_smash_arm64      |  50272 | True   | False
stack_smash_libc_app   |  50392 | True   | False
stdio_heap_app         |  51024 | True   | False
tlv_main               |  50376 | True   | False
```

**Forensic Explanation of `R1==R2: True` vs `R1==Disk`:**
1. All 23 artifacts (`R1==R2: True`) produce identical SHA-256 hashes across two separate output directories when built from the same working directory (`D:\WinISO26\WinIOS`). Zig `0.17.0`'s Mach-O linker computes deterministic content-based `LC_UUID` and `LC_CODE_SIGNATURE` hashes and accepts `-no_uuid` / `-fixup_chains` neither (rejected as `unsupported linker arg`).
2. When built from a **different working directory** or with **different source path spelling** (`D:\WinISO26` vs `D:\WinISO26\WinIOS`, as happened when running individual commands from workspace root vs `WinIOS/`), Mach-O `LC_SYMTAB` STABS `N_OSO` debug strings embed `CWD + <zig_cache_object>` (`strsz=381` vs `strsz=374`), which shifts `__LINKEDIT` offsets and alters the content-hashed `LC_UUID` (`0x1B`) and `LC_CODE_SIGNATURE` (`0x1D`).
3. Fixed `tests/artifacts/build_artifacts.ps1` for `stack_safe_arm64` / `stack_smash_arm64` (`-Wl,-e,__start`, because C `int _start(void)` emits Mach-O symbol `__start`), and added Stage U1–U3 + V0 build steps.

---

## 3. Audit Table: Every Test Assertion Changed Since `cc24f5e`

All changes in `git diff cc24f5e..d2b2983 -- tests/unit/` (plus `stage_t4_arm64e_probe_tests.cpp` in `c237cfc`):

| # | Test File | Scope Changed | Classification | Exact Rationale / Reference |
| :---: | :--- | :--- | :--- | :--- |
| 1 | `darwin_sysreg_and_trap_tests.cpp` | `MachReplyPort` `-27->-26`, `MachMsg` `-26->-31` | Convention fix with source reference | XNU `osfmk/kern/syscall_sw.c` trap table (`-26` = `mach_reply_port`, `-31` = `mach_msg_trap`) |
| 2 | `stage130_end_to_end_pipeline_tests.cpp` | `movn x16` constants (`-27->-26`, `-26->-31`) | Convention fix with source reference | XNU `osfmk/kern/syscall_sw.c` trap table (`0x92800330`, `0x928003D0`) |
| 3 | `stage134_mach_bsd_integration_tests.cpp` | `movn x16` constants in port alloc & msg send | Convention fix with source reference | XNU `osfmk/kern/syscall_sw.c` (`-26`, `-31`) |
| 4 | `stage143_svc_exception_tests.cpp` | `test_darwin_xnu_negative_x16_mach_routing` (`-27->-26`) | Convention fix with source reference | XNU `osfmk/kern/syscall_sw.c` (`-26 = mach_reply_port`) |
| 5 | `stage145_mach_ipc_abi_tests.cpp` | Tests 1–5 trap numbers (`-27->-26`, `-26->-31`) | Convention fix with source reference | XNU `osfmk/kern/syscall_sw.c` (`-26`, `-31`) |
| 6 | `stage154_end_to_end_execution_tests.cpp` | `test_pipeline_mach_ipc` (`-26->-31`) | Convention fix with source reference | XNU `osfmk/kern/syscall_sw.c` (`-31 = mach_msg_trap`) |
| 7 | `stage158_bsd_syscall_conformance_tests.cpp` | Test 5 explicitly writes `X0=mapped_addr, X1=16384` before `SYS_munmap` | Argument fix due to behavior change | `SYS_munmap (73)` replaced unconditional `0` no-op stub with `bsd_.munmap`; test previously called `cpu.reset()` and invoked `SYS_munmap` with `X0=0, X1=0` |
| 8 | `stage159_mach_ipc_conformance_tests.cpp` | Tests 3–4 (`-26->-31`) | Convention fix with source reference | XNU `osfmk/kern/syscall_sw.c` (`-31 = mach_msg_trap`) |
| 9 | `stage169_end_to_end_conformance_tests.cpp` | `test_mach_trap_dispatch` (`-27->-26`) | Convention fix with source reference | XNU `osfmk/kern/syscall_sw.c` (`-26 = mach_reply_port`) |
| 10 | `stage172_macho_corpus_tests.cpp` | Added `e.classification == "SYNTHETIC-GUEST-VERIFIED"` check | Other | Tightened corpus label hygiene (stricter assertion, prevents synthetic builders claiming `REAL-ARTIFACT-EXECUTED`) |
| 11 | `stage173_real_entry_execution_tests.cpp` | Test 5 description/comment (`-27->-26`) | Convention fix with source reference | XNU `osfmk/kern/syscall_sw.c` (`-26 = mach_reply_port`) |
| 12 | `stage179_minimal_darwin_runtime_tests.cpp` | `_mach_msg_trap` expected instruction (`0x92800330->0x928003D0`) | Convention fix with source reference | XNU `osfmk/kern/syscall_sw.c` (`~(-31) = 30`) |
| 13 | `stage181_mach_ipc_guest_integration_tests.cpp` | `test_guest_instruction_mach_trap_dispatch` (`-27->-26`) | Convention fix with source reference | XNU `osfmk/kern/syscall_sw.c` (`-26 = mach_reply_port`) |
| 14 | `cpu_tests.cpp` | Appended `campaign_191u_ins_element_and_sshll_tests()` | Other | Added unit tests for new SIMD instructions (`INS element`, `SSHLL/USHLL/SXTL/UXTL`); 0 existing assertions altered |
| 15 | `adr_adrp_conformance_tests.cpp` | New file added in `efa04c2` | Other | Added 9 new ADR/ADRP conformance tests (no prior test existed) |
| 16 | `stage_t4_arm64e_probe_tests.cpp` | Added Test 4 in `c237cfc` (`test_pac_corrupted_pointer_nop_hint`) | Other | Added negative test proving `AUTIASP` currently acts in NOP-hint space (`PARTIAL` verdict) |

**Result:** 0 test assertions were weakened or deleted across any legacy test file.

---

## 4. Guest-Side Chained Fixups (`DYLD_CHAINED_PTR_64`) Proof

- **Guest implementation (`tests/artifacts/src/minidyld.c`):** Added `apply_chained_fixups(...)` supporting `LC_DYLD_CHAINED_FIXUPS` (`0x80000034`) `DYLD_CHAINED_PTR_64` (`pointer_format = 2`), walking `dyld_chained_starts_in_image`, `dyld_chained_starts_in_segment`, `page_starts`, chained rebase (`bind = 0`) and bind (`bind = 1`) nodes, `imports` (`lib_ordinal`, `weak_import`, `name_offset`), and `symbols` pool on the EL0 guest CPU. Non-`DYLD_CHAINED_PTR_64` formats deterministically exit with code `125` and are explicitly marked **UNTESTED**.
- **Real iOS-target Artifact Chain:**
  - `tests/artifacts/src/libioschain.c` -> `tests/artifacts/bin/libioschain.dylib` (`49,713 B`, `PLATFORM_IOS = 2`, SHA-256 `a599f9544df847682c77b95cdb30ea79a1537df5c9b1de5e34fe31f767948422`).
  - `tests/artifacts/src/ios_chain_app.c` -> compiled with `zig cc -target aarch64-ios -O2` and translated from compiler `LC_DYLD_INFO_ONLY` records into `LC_DYLD_CHAINED_FIXUPS` (`DYLD_CHAINED_PTR_64`) via `tests/tools/convert_to_chained_fixups.py`, zeroing `LC_DYLD_INFO_ONLY` `rebase/bind/lazy` (`50,368 B`, SHA-256 `5f9aac3db3b3c672d13c6dab3d8ea4437e8d28ebda22d8fab03a1727ab3f841e`).
- **Measured `stage_v0_chained_fixups_tests` Command Output (3/3 PASS):**
```text
=========================================================
STAGE V0: Guest Chained Fixups (DYLD_CHAINED_PTR_64) Tests
=========================================================
[Test 1] ios_chain_app & libioschain.dylib Provenance + Chained Fixup Structure...
  ios_chain_app: 50368 B, SHA-256=5f9aac3db3b3c672d13c6dab3d8ea4437e8d28ebda22d8fab03a1727ab3f841e
  libioschain:   49713 B, SHA-256=a599f9544df847682c77b95cdb30ea79a1537df5c9b1de5e34fe31f767948422
  fixups blob:   96 B @ 0xc460, rebases=3, binds=1
[PASS] Provenance & Chained Fixups Payload
[Test 2] Guest EL0 Chained Fixups Execution + Differential Proof vs Host DyldEngine...
  Guest retired=4578, exited=true, code=0, stdout=IOS_CHAIN: 107,214,656,23
  REBASE slot @ 0x8010: guest=0x8008 host=0x8008 expected=0x8008
  REBASE slot @ 0x8018: guest=0x4000 host=0x4000 expected=0x4000
  REBASE slot @ 0x8020: guest=0x4008 host=0x4008 expected=0x4008
  BIND   slot @ 0x8028 (_ios_chain_add): guest=0x24000 host=0x24000 expected=0x24000
[PASS] Guest Execution + Host Differential Proof
[Test 3] Unsupported Chained Pointer Format (ARM64E) Cleanly Rejected...
  Guest exit_code=125, stdout=minidyld: unsupported chained pointer format
[PASS] Unsupported Pointer Format Guard (ARM64E UNTESTED)

Results: 3 passed, 0 failed
```

| Pointer Format | Guest (`minidyld`) Status | Host (`DyldEngine`) Status |
| :--- | :--- | :--- |
| `DYLD_CHAINED_PTR_64 (2)` | **VERIFIED** (3 rebases + 1 bind in `ios_chain_app`, differential match) | **VERIFIED** (`stage176` + `stage_v0` differential) |
| `DYLD_CHAINED_PTR_64_OFFSET (6)` | **UNTESTED** (exits `125`) | Engine-level (`stage176`) |
| `DYLD_CHAINED_PTR_ARM64E (1, 9, 12)` | **UNTESTED** (exits `125`) | Engine-level (`stage176`) |
| `DYLD_CHAINED_PTR_32* (3, 4, 5)` | **UNTESTED** (exits `125`) | **UNTESTED** |

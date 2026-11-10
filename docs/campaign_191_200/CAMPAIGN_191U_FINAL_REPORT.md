# WINIOS CAMPAIGN 191-U — FINAL VERIFICATION & ENGINEERING REPORT

**Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r` (continuation from `c237cfc` / `a5f1b41`)  
**Host / Toolchain:** Windows 11 x64 | Visual Studio 18 2026, MSVC C++20 (`/W4 /WX /permissive-`) | Zig 0.17.0 (`aarch64-macos` / `aarch64-ios`)  
**Scope:** Close all 191-T gaps, replace host dyld and libc stubs with guest-executed binaries (`minidyld`, `libSystem.B.dylib`), and reconcile & execute real compiled XNU Mach trap client workloads (`mach_client`).

---

## 1. Executive Summary

Campaign 191-U achieves four concrete milestones with **330 passed, 1 skipped (`#299 inferno_integration_tests`), 0 failed, 0 not-run** out of `331` registered CTest targets in both **Debug** and **Release**:

**Execution Boundary (Guest-Executed vs Host-Emulated):**
- **Guest-Executed (EL0):** Dynamic library loading, relocation, export-trie lookup, GOT binding, and constructor dispatch (`minidyld`), C runtime / allocator / stack-protector (`libSystem.B.dylib`), and user application code (`stdio_heap_app`, `stack_smash_libc_app`, `mach_client`).
- **Remains Host-Emulated:** BSD syscalls (`DarwinBsdSubsystem` + `DarwinUserspaceEngine::handle_bsd_syscall`), Mach IPC traps (`MachIpcSubsystem` + `DarwinUserspaceEngine::handle_mach_trap`), process/VFS tables, and mapping of the initial images (pre-mapping main Mach-O + `minidyld` segments and building the initial Darwin stack frame before jumping to `minidyld`).

1. **Stage U0 — 191-T Remediation & Evidence Reconciliation (commit `c237cfc`):**
   - Repaired all 7 failing CTest targets (`stage60_mmu_tests`, `stage61_tlb_tests`, `stage95_boot_artifact_inventory_tests`, `stage131_artifact_admission_tests`, `stage184`, `stage186`, `stage188`) restoring 327/328 green baseline.
   - Resolved all artifact SHA-256 and instruction-count conflicts (`REAL-002` verified at 830 retired instructions; `REAL-003..REAL-005` ephemeral `LC_UUID` rebuild vs repository disk binaries).
   - Downgraded over-claimed verdicts honestly: `ARM64e / PAC` -> `PARTIAL` (NOP-hint compatibility confirmed by negative test in `stage_t4_arm64e_probe_tests`), `TLS / TLV` -> `PARTIAL` (single-thread host-assisted layout), `Chained Fixups` -> `YES (Engine) / PARTIAL (Real Artifacts)`.
2. **Stage U1 — Freestanding Guest-Side `minidyld` (`stage_u1_guest_minidyld_tests`, 9/9 PASS):**
   - Compiled an original freestanding ARM64 dynamic linker (`tests/artifacts/bin/minidyld`, `67,320 B`) executed in **EL0** via `execute_macho_with_guest_dyld`.
   - Guest `minidyld` reads dylibs via `SYS_open` / `SYS_pread (153)` / `SYS_mmap (197)`, parses Mach-O segments and `LC_DYLD_INFO_ONLY` rebase/bind/export tries, resolves `@rpath` / `@loader_path` / `@executable_path`, patches GOT/lazy pointers in guest memory, invokes `__mod_init_func` constructors in post-order dependency sequence, and transfers control to `LC_MAIN` with authentic `(argc, argv, envp)`.
3. **Stage U2 — Compiled `libSystem.B.dylib` (`libSystem-Lite`) & Standard C Apps (`stage_u2_libsystem_lite_tests`, 4/4 PASS):**
   - Compiled `tests/artifacts/bin/libSystem.B.dylib` (`70,960 B`) exporting 44 POSIX/Mach/stdio/heap/`__stack_chk_*` symbols (`-mstrict-align`, `-nostdlib`).
   - Compiled standard non-freestanding `<stdio.h>/<stdlib.h>/<string.h>/<unistd.h>` apps (`stdio_heap_app`, `stack_smash_libc_app`) dynamically linked in EL0 by `minidyld`.
   - Verified separated `stdout_output` (`fd=1`) and `stderr_output` (`fd=2`), real `SYS_mmap`-backed heap allocation (`28,101` guest insns in `stdio_heap_app`), real compiled `___stack_chk_fail` termination with exit `134` (`8,031` guest insns), and GOT provenance into loaded `libSystem.B.dylib` (`0x2c010`, eliminating host-synthesized stack-protector stubs).
4. **Stage U3 — XNU Mach Trap Reconciliation & Real Compiled `mach_client` (`stage_u3_real_mach_trap_tests`, 3/3 PASS):**
   - Audited and reconciled WinIOS Mach traps against public XNU (`osfmk/kern/syscall_sw.c`): `-26 = mach_reply_port`, `-27 = thread_self_trap`, `-28 = task_self_trap`, `-29 = host_self_trap`, `-31 = mach_msg_trap`, `-32 = mach_msg_overwrite_trap` across 6 runtime/builder files and 11 unit tests.
   - Executed `tests/artifacts/bin/mach_client` (`50,888 B`, compiled against `<mach/mach.h>`, `<mach/message.h>`, `<mach/mach_traps.h>`) in EL0 for `22,285` guest instructions, `12` real Mach traps, and `18` BSD syscalls, completing a full request/reply Mach IPC round-trip with reply right translation and XNU error propagation (`0x10000003`, `0x10004003`).

---

## 2. Manifest & Real Artifact Ledger (`macho_artifact_manifest.json` v1.3.0)

All 21 binaries in `tests/artifacts/bin/` were cryptographically verified on disk against `tests/artifacts/macho_artifact_manifest.json` (`version = 1.3.0`, exact byte sizes and SHA-256 hashes):

| ID | Binary | Size (B) | SHA-256 (first 16 hex) | Classification | Verified Outcome |
| :--- | :--- | :---: | :--- | :--- | :--- |
| `REAL-001`..`014` | Pre-191U Corpus + `stack_safe` / `stack_smash` | 49,928–51,328 | Matches disk 100% | `REAL-ARTIFACT-EXECUTED` | All legacy real-artifact tests PASS |
| `REAL-015` | `stack_safe_arm64` | 50,256 | `41975920f085ab59...` | `REAL-ARTIFACT-EXECUTED` | `-fstack-protector-all` safe path PASS |
| `REAL-016` | `stack_smash_arm64` | 50,272 | `ee4621fb8237ed50...` | `REAL-ARTIFACT-EXECUTED` | `-fstack-protector-all` smash detect PASS |
| `REAL-017` | `minidyld` | 67,320 | `ca28f68b19f6d615...` | `REAL-ARTIFACT-EXECUTED` | Links multi-image chains & `libSystem.B.dylib` in EL0 (`9/9` tests PASS) |
| `REAL-018` | `libSystem.B.dylib` | 70,960 | `eb2050ee18f1dd86...` | `REAL-ARTIFACT-EXECUTED` | Loaded by `minidyld`; GOT canary at `0x2c010` verified |
| `REAL-019` | `stdio_heap_app` | 51,024 | `2442509602e4faa2...` | `REAL-ARTIFACT-EXECUTED` | `28,101` insns, 24 BSD syscalls, `malloc`/`printf`/`argc`, exit `0` |
| `REAL-020` | `stack_smash_libc_app` | 50,392 | `be8983f853fe8f36...` | `REAL-ARTIFACT-EXECUTED` | `8,031` insns, stderr canary abort, exit `134` |
| `REAL-021` | `mach_client` | 50,888 | `e74593b477900d9d...` | `REAL-ARTIFACT-EXECUTED` | `22,285` insns, 12 Mach traps, round-trip + reply rights, exit `0` |

**Honest Evidence Labels (What Remains `PARTIAL` or Synthetic):**
- **ARM64e PAC Enforcement:** `PARTIAL` — `PACIASP`/`AUTIASP` execute as NOP-hint compatibility instructions without pointer-authentication cryptographic trap on corrupted LR.
- **TLV / Thread-Local Storage:** `PARTIAL` — Single-thread `__tlv_bootstrap`; multi-thread per-thread TLV slots are not yet implemented.
- **Chained Fixups (`LC_DYLD_CHAINED_FIXUPS`):** Engine-verified (`YES`); real Zig/Clang Mach-O artifacts still emit `LC_DYLD_INFO_ONLY` opcode streams (`PARTIAL` for disk artifacts).
- **MMU Mode in `DarwinUserspaceEngine`:** EL0 userspace engine executes with Stage-1 MMU disabled (flat physical RAM), enforcing strict natural alignment (`-mstrict-align` required for SIMD memory accesses emitted by Clang `-O2`).

---

## 3. CPU & Runtime Gaps Surfaced and Fixed During 191-U

Every CPU/runtime change in 191-U was driven directly by real compiler-generated binaries:

| Subsystem | Real Failure Observed | Root Cause & Architectural Fix | Independent Unit Test |
| :--- | :--- | :--- | :--- |
| **CPU (`cpu.cpp`)** | `0x6F00E400` (`MOVI V0.16B, #0`), `0xAD...` (`STP Q0, Q0`) in `minidyld`; `0xEB28C33F` (`CMP X25, W8, SXTW`) in `ctor_main` | Implemented SIMD modified-immediate (`MOVI`/`MVNI`/`ORR`/`BIC`), `STP`/`LDP` for `Q`/`D`/`S`, and `kAddSubExtendedRegister` (all 8 UXTB..SXTX extensions). | `stage_u1_guest_minidyld_tests` Test 9 |
| **CPU (`cpu.cpp`)** | `0x6E180420` (`INS V0.D[1], V1.D[0]`), `0x0F10A420` (`SXTL V0.4S, V1.4H`) in `mach_client` | Implemented `INS (element)` (`bit29=1`, `Q=1`, `imm5`/`imm4`) and `SSHLL`/`USHLL`/`SXTL`/`UXTL` (`opcode == 0b10100`, `esz < 3`) + disassembly. | `cpu_tests`: `campaign_191u_ins_element_and_sshll_tests` (1,898/1,898) |
| **Runtime (`darwin_userspace_engine.cpp`)** | Legacy `execute_macho` (`brk_curr = 0x180000000`) wiped `0x1000` guest code during `SYS_mmap` zero-fill | `0x180000000 % 64MB == 0`; restricted anonymous zero-init to `mapped < memory_.size()`. | `stage130`, `stage173`, `stage184` |
| **Runtime & Tests** | `stage158` Test 5 failed after real `SYS_munmap (73)` replaced no-op stub | `stage158` called `setup_guest_cpu` (`cpu.reset()`) without reloading `X0=mapped_addr, X1=16384`. Fixed `stage158` to pass arguments explicitly. | `stage158_bsd_syscall_conformance_tests` (7/7 PASS) |
| **BSD (`darwin_bsd_subsystem.cpp`)** | Need non-advancing Mach-O header/segment/linkedit reads in `minidyld` | Implemented `DarwinBsdSubsystem::pread` and wired `SYS_pread (153)`, `SYS_munmap (73)`, `SYS_mprotect (74)`. | `stage_u1_guest_minidyld_tests` Test 1 |
| **Mach ABI (`darwin_syscall_dispatcher.*`, `darwin_userspace_engine.cpp`)** | WinIOS mapped `-26=MachMsg, -27=MachReplyPort` (inverted vs XNU) | Reconciled to XNU `osfmk/kern/syscall_sw.c` (`-26`..`-32`) and multi-port `mach_reply_port` allocation across 6 runtime/builder files & 11 test suites. | `stage_u3_real_mach_trap_tests` (3/3 PASS) |

---

## 4. Full CTest Regression Results (Final Post-Edit Verification)

Sequential `ctest` runs executed after all final source, artifact, manifest, and test edits:

| Configuration | Registered | Passed | Failed | Skipped | Not Run | Wall Time |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Debug** | 331 | **330** | **0** | 1 (`#299 inferno_integration_tests`) | 0 | 210.69 s |
| **Release** | 331 | **330** | **0** | 1 (`#299 inferno_integration_tests`) | 0 | 84.36 s |

- Stage reports archived in `docs/campaign_191_200/`:
  - `STAGE_U0_REPAIR_AND_RECONCILIATION.md`
  - `STAGE_U1_GUEST_MINIDYLD_PROOF.md`
  - `STAGE_U2_LIBSYSTEM_LITE.md`
  - `STAGE_U3_REAL_MACH_TRAP_PROOF.md`
  - `CAMPAIGN_191U_FINAL_REPORT.md`
- Protected files (`WinIOS_README.md`, `WinIOS/docs/progress.md`, `D:\WinISO26\Inferno-Research\`) verified completely untouched.

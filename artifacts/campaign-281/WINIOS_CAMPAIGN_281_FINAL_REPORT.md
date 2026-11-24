# WINIOS CAMPAIGN 281 — FINAL REPORT

**Genuine XNU Context-Switch Integration, ARM64 `FEAT_SSBS` System-Register Semantics, Mach-O `MH_OBJECT` Relocation Infrastructure, Bounded Linked Execution & Timer Regression Resolution**

- **Campaign:** 281 (Stages `281.0`–`281.24`)
- **Repository:** `D:\WinISO26\WinIOS\`
- **Branch:** `campaign-191r`
- **Baseline HEAD (Campaign 280 closeout):** `5da5404547413f04d93e411f3685fe2d5143ab91`
- **Parent (Campaign 279 closeout):** `390ef04dbc6feb5b186ea5dd12e0400c66059529`
- **Grandparent (Campaign 278 closeout):** `a5ffb712b0b6423f7d297e9178b8b53499ac5a4c`
- **Pinned Public Upstream XNU:** `xnu-10002.1.13` (`https://github.com/apple-oss-distributions/xnu`, commit `1031c584a5e37aff177559b9f69dbd3c8c3fd30a`, primary build target `ARM64_BOARD_CONFIG_T8103`, `H13`, `MacOSX`, `arm64e`)
- **Host Environment:** Windows 11 x64 (`10.0.26300`), Visual Studio 18 2026 (`18.10.x`), MSVC C++20 (`/W4 /permissive- /FS`), CMake `4.4.4`, Zig `0.17.0` / Clang `22.1.8`

---

## 1. Executive Summary

Campaign 281 resolved the pre-existing CTest timer regression, implemented full architectural ARM64 `FEAT_SSBS` (`PSTATE.SSBS`) semantics across execution, exception entry/`ERET`, sysreg metadata, hashing, and snapshots, built a separate bounded ARM64 Mach-O `MH_OBJECT` linker/relocator/W^X stager with strict provenance accounting, and executed the unmodified Campaign 280 upstream XNU `cswitch.o` `_machine_load_context` routine across the former `0x07C` (`MSR SSBS, X2`) blocker all the way through callee-saved `d8..d15`, `FPCR`, `SP`, `TPIDR_*`, and `0x0B4` (`RETAA`):

1. **Timer Regression Resolved (`#1123` Fixed-Rate Alignment):** Confirmed that Campaign 262 intentionally changed `DeterministicVirtualTimer::rearm_periodic` to fixed-rate anti-drift scheduling (`n = floor((C - D) / P) + 1`, `next_deadline = D + n * P`, clamped on uint64 overflow). Updated the stale pre-C262 assertion at `tests/unit/stage260_16_timer_architecture_and_delivery_tests.cpp:110` from `3500` to `3000` (`1000 + 1 * 2000`) while adding dedicated catch-up/coalescing/saturation/replay coverage in `stage281_timer_and_ssbs_tests`.
2. **ARM64 `FEAT_SSBS` Implemented End-to-End:** Added `SysRegId::SSBS` (`0x5A16` = `(3,3,4,2,6)`), `MSR SSBS, Xt`, `MRS Xt, SSBS` (bit 12 only; RES0 mask `0xFFFFFFFFFFFFEFFF`), `MSR SSBS, #0` / `MSR SSBS, #1` (`op1=3, op2=1, CRm <= 1`) at EL0 & EL1+ (trapping reserved `CRm >= 2`), `SPSR_ELx[12]` save/restore across synchronous/asynchronous exception entry and `ERET`, `SCTLR_EL1.DSSBS` (bit 44) handling on EL1 entry, `SmpCoreState::pstate_ssbs`, `ReplayStateHash::hash_cpu` (`pstate` bit 13), and backward-compatible `1,128`-byte `snapshot_serializer` round-trip persistence.
3. **Bounded `MH_OBJECT` Relocation & Integration (`MachOObjectIntegrator`):** Implemented `src/loader/macho_object_integrator.h/.cpp` cleanly separated from `KernelImageParser` and `MachoUserLoader` (both of which continue rejecting `MH_OBJECT`). Enforces Mach-O64 ARM64 `MH_OBJECT` parsing, `__DWARF` debug-section separation, `LC_SYMTAB` validation, explicit `SymbolProvenanceClassification` (`GenuineUpstream`, `WinIOSArchitectural`, `TestHarnessStub`, `UnresolvedExternal`, `UnsupportedSymbol`) with anti-spoofing guards, strict `ARM64_RELOC_BRANCH26` relocation encoding/range checking, W^X (`R|X`) RX staging, and standalone `MH_EXECUTE` container construction.
4. **Expanded Genuine `cswitch.o` Execution & Dual-Config Full Pass:** Executed actual C280 `RELEASE_ARM64_OUT\cswitch.o` (`3,336` B, SHA-256 `784CE07C...`) routines `_machine_load_context`, `_Idle_load_context`, `_Switch_context`, `_machine_set_current_thread`, and `_Call_continuation` (`x3=0` and `x3=1`, both unlinked honest-stop at `+0x114` and explicitly linked to `TestHarnessStub`s). Both **RelWithDebInfo** (`189.76s`) and **Debug** (`1171.06s`) complete sequential unfiltered CTest runs achieved **`1,240 Passed, 1 Skipped (#383 inferno_integration_tests), 0 Failed` out of `1,241` tests**.

---

## 2. Campaign Certification Outcome

| Certification Gate | Verdict | Rationale |
|---|---|---|
| **Timer Regression Resolution (`#1123`)** | **AWARDED** | Preserved C262 fixed-rate `D + n*P` semantics; fixed stale C260 assertion; `1,240 / 1,240` runnable CTest tests pass in both build configs. |
| **ARM64 `FEAT_SSBS` Architecture & Persistence** | **AWARDED** | `MRS`/`MSR`/`MSR #imm`/`SPSR[12]`/`ERET`/`SCTLR_EL1.DSSBS`/`SmpCoreState`/`ReplayStateHash`/`snapshot_serializer` (`1128` B) implemented & verified. |
| **Bounded `MH_OBJECT` Relocation Infrastructure** | **AWARDED** | `MachOObjectIntegrator` parses `cswitch.o`, resolves `ARM64_RELOC_BRANCH26`, enforces W^X and provenance isolation, and preserves loader rejection of `MH_OBJECT`. |
| **Genuine `_machine_load_context` Execution** | **AWARDED** | Executed unmodified `cswitch.o` `_machine_load_context` across `0x07C MSR SSBS` through `d8..d15`, `FPCR`, `SP`, `TPIDR_*`, `cpu_active_thread`, returning cleanly at `0x0B4 RETAA` with `CPACR_EL1.FPEN=0b11`. |
| **Level 3 — Bounded early XNU execution & modeled integration** | **RETAINED** | Embedded `MH_EXECUTE` kernel, bounded `MH_OBJECT` linking, and isolated Category B/C/stub verification remain strictly scoped and honestly audited. |
| **Level 4 — Full genuine upstream XNU kernel linking & boot** | **REJECTED** | Explicitly rejected: `cswitch.o` still requires 4 external kernel routines (`_ml_set_interrupts_enabled`, `_thread_terminate`, `_cpu_doshutdown`, `_cpu_idle`); `PACIASP`/`AUTIASP` hints remain NOPs in base emulator mode; no upstream `kernel_bootstrap`, `sched_init`, `vm_init`, `ipc_init`, or `bsd_init` kernel image exists. |

---

## 3. Baseline and Git Ancestry

- **Repository:** `D:\WinISO26\WinIOS\`
- **Branch:** `campaign-191r`
- **Campaign 280 Parent Commit (`HEAD` at C281 start):** `5da5404547413f04d93e411f3685fe2d5143ab91`
- **Campaign 279 Parent:** `390ef04dbc6feb5b186ea5dd12e0400c66059529`
- **Campaign 278 Grandparent:** `a5ffb712b0b6423f7d297e9178b8b53499ac5a4c`
- **External Pinned XNU Source:** `D:\WinISO26\xnu-10002.1.13\` at `1031c584a5e37aff177559b9f69dbd3c8c3fd30a` (clean working tree)

---

## 4. Protected Repository Invariants

| Protected Asset | Requirement | Observed Size | Observed SHA-256 | Status |
|---|---|---|---|---|
| `D:\WinISO26\WinISO26_README.md` | Must NOT exist | N/A (`False`) | N/A | **VERIFIED ABSENT** |
| `D:\WinISO26\WinIOS_README.md` | Bit-identical | `11,054` B | `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | **VERIFIED INTACT** |
| `D:\WinISO26\WinIOS\docs\progress.md` | Bit-identical | `22,363` B | `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | **VERIFIED INTACT** |
| `D:\WinISO26\Inferno-Research\` | Strictly isolated | Exists (`True`) | Uninspected & untouched | **VERIFIED ISOLATED** |

---

## 5. C280 Artifact & Source Provenance Verification

Verified unchanged external Campaign 280 upstream & generated baselines before and after Campaign 281 execution:
- `D:\WinISO26\xnu-10002.1.13\osfmk\arm64\cswitch.s`: `11,571` B, SHA-256 `D95EC6DF06995170AAFD6946073D4B20D4DCC0F281A5F9CF3AFBB6400EF42DEF`
- `D:\WinISO26\xnu_probe_isolated\gen_build\RELEASE_ARM64_OUT\assym.s`: `8,632` B, SHA-256 `C7434D10684EF3A11DF04A2AC454EE9CBBC07614B78385632154D7B157FA13F6`
- `D:\WinISO26\xnu_probe_isolated\gen_build\RELEASE_ARM64_OUT\cswitch.o`: `3,336` B, SHA-256 `784CE07CE532916FC5897A866F673A182C6DE5F5AD2A9E6478D44BB8B7AB4382`

---

## 6. Timer Regression Investigation & Resolution (Objective A)

- **Root Cause:** In Campaign 262 (`src/cpu/deterministic_virtual_timer.cpp:97-160`), `DeterministicVirtualTimer::rearm_periodic` was intentionally converted from fixed-delay (`current_counter + interval`) to fixed-rate anti-drift periodic scheduling (`n = ((C - D) / P) + 1`, `next_deadline = D + n * P`, returning `Success` or `DeadlineSaturated`). Subsequent suites (`stage262_18`, `stage262_21`, `stage263_20`, `stage264_20`, `stage264_24`, `stage265_21`) depend on this exact contract.
- **Defect:** `tests/unit/stage260_16_timer_architecture_and_delivery_tests.cpp:110` retained an outdated pre-C262 assertion expecting `comparator == 3500ULL` (`1500 + 2000`) instead of fixed-rate `3000ULL` (`1000 + 1 * 2000`).
- **Fix & Regression Coverage:** Updated `stage260_16_timer_architecture_and_delivery_tests.cpp:106-115` to assert fixed-rate `next_deadline == 3000ULL`, `elapsed_periods == 1ULL`, and `missed_ticks == 0ULL`, and added multi-period coalescing, early rearm (`C < D`), exact deadline boundary, `DeadlineSaturated` overflow clamping, serialize/deserialize parity, and deterministic replay tests in `tests/unit/stage281_timer_and_ssbs_tests.cpp`.

---

## 7. ARM64 `FEAT_SSBS` Implementation Summary (Objective B)

| Component | File(s) | Exact Implemented Semantics |
|---|---|---|
| CPU state & accessors | `src/cpu/cpu.h`, `src/cpu/cpu.cpp` | Added `bool pstate_ssbs_{false}` initialized `false` on `reset()`, `get_pstate_ssbs()`, `set_pstate_ssbs(bool)`. |
| System register metadata | `src/cpu/sysreg.h`, `src/cpu/sysreg.cpp` | Added `SysRegId::SSBS`, `"SSBS"`, key `0x5A16` (`op0=3, op1=3, CRn=4, CRm=2, op2=6`), EL0+ RW (`min_el=0`), `res0_mask = 0xFFFFFFFFFFFFEFFFULL`. |
| `MRS`/`MSR` & `MSR #imm1` | `src/cpu/cpu.cpp` | Disassembles and executes `MRS Xt, SSBS`, `MSR SSBS, Xt` (bit 12 only writable; EL0 permitted via `can_access_el0`), and `MSR SSBS, #0`/`#1` (`op1=3, op2=1, CRm <= 1`) at EL0 & EL1+. Reserved `CRm >= 2` remains unallocated/trapped. |
| Exception entry & `ERET` | `src/cpu/cpu.cpp` | `encode_spsr` captures `pstate_ssbs_` into bit 12; `deliver_architectural_exception` sets `pstate_ssbs_ = (exc.target_el == 1) && (((mmu_.get_sctlr_el1() >> 44u) & 1u) != 0u)` (`SCTLR_EL1.DSSBS`); `ERET` restores `pstate_ssbs_` from `spsr[12]`. |
| SMP, hashing & snapshots | `src/cpu/smp_core_state.h/.cpp`, `src/replay/replay_state_hash.cpp`, `src/snapshot/snapshot_serializer.cpp` | Added `SmpCoreState::pstate_ssbs` synced in `capture_from_cpu`/`apply_to_cpu`/`operator==`/hash; hashed `pstate` bit 13 in `ReplayStateHash::hash_cpu`; stored `pstate_ssbs` at Section 10 padding byte `+6`, preserving exact `1,128`-byte legacy snapshot compatibility (`1,136` with retirement count). |

---

## 8. Existing Relocation & Loader Boundaries Audit

- Audited `src/loader/apple_kernel_relocator.h/.cpp`: only processes ASLR slide offsets (`__LINKEDIT` / chained fixups) on `MH_EXECUTE` kernel images; contains zero `MH_OBJECT` or `ARM64_RELOC_BRANCH26` relocation support.
- Audited `src/loader/kernel_image_parser.h/.cpp` and `src/loader/macho_user_loader.h/.cpp`: both reject `MH_OBJECT` inputs (`KernelImageParseStatus::UnsupportedFileType` / `LoadedUserImage::valid == false`). This invariant was strictly preserved and re-tested.

---

## 9. Bounded `MH_OBJECT` Relocation & Integration (`MachOObjectIntegrator`, Objective C)

Created `src/loader/macho_object_integrator.h` (`8,773` B) and `src/loader/macho_object_integrator.cpp` (`43,138` B), registered in `winios_core`:
- **Strict Parsing (`parse_object` / `parse_object_file`):** Validates `MH_MAGIC_64` (`0xFEEDFACF`), `CPU_TYPE_ARM64` (`0x0100000C`), `MH_OBJECT` (`filetype == 1`), load-command bounds (`checked_add_u64`), `LC_SEGMENT_64` / `section_64` ranges, separates `__DWARF` debug sections (`S_ATTR_DEBUG`), parses all `LC_SYMTAB` `nlist_64` entries with string-table bounds checking, and rejects scattered relocations (`R_SCATTERED`).
- **Provenance Accounting & Anti-Spoofing (`SymbolProvenanceClassification`):** Explicitly separates `GenuineUpstream`, `WinIOSArchitectural`, `TestHarnessStub`, `UnresolvedExternal`, and `UnsupportedSymbol`. Rejects attempts to attach synthetic stub machine instructions under `GenuineUpstream`. Computes per-region provenance spans (`__TEXT,__text` vs `__STUB_TEXT`).
- **`ARM64_RELOC_BRANCH26` Linking (`link_object`):** Enforces 4-byte aligned `__TEXT,__text`, 16 KiB aligned non-overlapping `__STUB_TEXT`, rejects non-extern / non-`BRANCH26` / OOB / negative offsets, rejects non-branch opcodes at relocation sites, enforces exact 26-bit signed `B`/`BL` range (`[-128 MiB, +128 MiB - 4]`), and enforces `require_all_relocations_resolved` when unresolved externals remain.
- **W^X Staging & Container Builder (`stage_in_memory`, `build_bounded_macho_execute`):** Maps staged RX regions (`vm_prot = R | X = 5`, enforcing `!(W && X)`) into guest `Memory` / optional `GuestMemoryMap`, and builds standalone parseable `MH_EXECUTE` images (`__TEXT` + optional `__STUB_TEXT` + `LC_MAIN`) passing `MachOLoader::parse_metadata`.

---

## 10. Genuine `cswitch.o` Execution Findings (Objective D)

Verified directly against compiled C280 `RELEASE_ARM64_OUT\cswitch.o` (`303` instructions, `1,212` B `__TEXT,__text`, `assym.s` offsets + ARM64e PAC keys `APDA + 0x6D42`, `APIA + SP`, `APIA + 0xD507`):
1. **`_machine_load_context` (`0x000..0x0B4`) Unblocked:** With `CPACR_EL1.FPEN = 0b11` (`3ULL << 20`) enabled (required by `0x044 mrs x19, fpcr` and `0x080 ldr d8`), genuine `_machine_load_context` now executes completely past `0x07C msr ssbs, x2` (for both `saved_ssbs = 1` and `1 -> 0` transitions), restores `FPCR`, `x19..x29`, `SP`, `d8..d15`, sets `TPIDR_EL1 = thread_va`, `cpu_data[+0x28] = thread_va`, `TPIDR_EL0`, `TPIDRRO_EL0`, sets `x0 = 0`, and returns cleanly via `0x0B4 retaa` (trapping cleanly on unsigned or wrong-SP `LR`, or corrupted `TH_KSTACKPTR`). When `CPACR_EL1.FPEN == 0`, `_machine_load_context` still updates `PSTATE.SSBS` at `0x07C` and traps cleanly at `0x080 ldr d8`.
2. **`_Idle_load_context` (`0x400..0x498`) & `_machine_set_current_thread` (`0x49C..0x4B8`):** `_Idle_load_context` crosses `0x464 msr ssbs, x2` -> `0x498 retaa`; `_machine_set_current_thread` sets `TPIDR_EL1`, `cpu_active_thread`, `TPIDR_EL0`, `TPIDRRO_EL0` and returns via `ret`.
3. **`_Switch_context` (`0x140..0x274`):** Executes full save (`mrs x4, ssbs` at `0x190` storing into `[x3, #0x69]`, GPRs, `d8..d15`, `FPCR`) + restore (`0x240 msr ssbs, x4` -> `0x274 retaa`). Note: `0x168 paciasp` executes as an architectural HINT NOP in WinIOS base CPU (preserving existing `stage270` contract), so `old_thread`'s saved `x30` is unsigned while `new_thread`'s `APIA`-signed `x30` passes `0x274 retaa`.
4. **`_Call_continuation` (`0x0C0..0x12C`) Bounded Linkage:**
   - **Unlinked (`x3 != 0`):** Honestly stops at unpatched `0x0114` (`bl #0`) when `_ml_set_interrupts_enabled` is `UnresolvedExternal`.
   - **Explicit `TestHarnessStub` Link (`x3 != 0`):** Relocates `0x0114` (`bl _ml_set_interrupts_enabled`) and `0x012C` (`b _thread_terminate`) into separate `__STUB_TEXT` RX page, runs `_ml_set_interrupts_enabled(1)` stub -> dispatches continuation via `blraa x20, x21` (`0xD507`) -> enters `_thread_terminate(thread)` stub, accounting `GenuineUpstream` vs `TestHarnessStub` execution steps separately.
   - **Upstream C Boundary Inspection:** Confirmed `xnu-10002.1.13/osfmk/arm/machine_routines_common.c:1089-1112` delegates `_ml_set_interrupts_enabled` to `ml_set_interrupts_enabled_with_debug` referencing `cpu_int_state`, `kdebug`, and per-CPU data structures across external TUs; therefore explicit `TestHarnessStub` or `UnresolvedExternal` gating remains the honest boundary.

---

## 11. Adversarial Testing (`stage281_adversarial_verification_tests.cpp`)

Implemented and executed **65 real deterministic adversarial checks (`A01`–`A64` including `A56b`)** across all Campaign 281 subsystems (0 warnings, 0 mocks/tautologies):
- **`A01`–`A03`:** Genuine `cswitch.o` (`3,336` B) discovery & structural parse verification.
- **`A04`–`A15`:** Timer invalid core, zero period, fixed-rate `3000` regression, multi-period catch-up, core isolation, corrupted snapshot rejection, and `UINT64_MAX` deadline saturation.
- **`A16`–`A33`:** Reset SSBS=0, all 14 reserved `MSR SSBS, #2..#15` traps, non-bit12 RES0 smearing attack, EL0 privilege enforcement (`SPSel` trapped vs `SSBS` allowed), `SCTLR_EL1.DSSBS` `0`/`1` & `SPSR_EL1[12]` / `ERET` transitions, `1,128`/`1,136` snapshot exactness & `1,135`-byte truncation rejection, `ReplayStateHash` & `SmpCoreState` per-core isolation.
- **`A34`–`A55`:** Empty Mach-O, bad magic, `x86_64` cputype, non-`MH_OBJECT` rejected, cmdsize overflow, scattered relocation rejection, OOB `r_address`, non-`BRANCH26` relocation type, opcode mismatch (`NOP` at relocation site), exact `+128 MiB - 4` / `-128 MiB` limits and overflow rejections, misaligned target/stub base, overlapping RX stub vs `__TEXT`, insufficient `Memory`, provenance spoofing (`GenuineUpstream` stub rejected), empty symbol name, and preserved `KernelImageParser` & `MachoUserLoader` `MH_OBJECT` rejections.
- **`A56`–`A64`:** Genuine `cswitch.o` `TH_KSTACKPTR` `APDA` tampering trap, unsigned `LR` trap at `0x0B4 RETAA`, wrong-`SP` modifier trap at `0x0B4 RETAA`, wrong continuation modifier at `blraa`, no-stub audit accuracy (`has_test_harness_stubs == false`), distinct provenance labels, and explicit Level 4 rejection due to 4 unresolved upstream externals.

---

## 12. Dual-Configuration Build & Unfiltered Sequential CTest Results

All Campaign 281 files compiled cleanly with **0 compiler warnings** under `/W4 /permissive- /FS` in both configurations.

| Configuration | Build (`ALL_BUILD`) | Total Discovered | Passed | Skipped | Failed | Duration | Exit Code |
|---|---|---|---|---|---|---|---|
| **`RelWithDebInfo`** | Exit `0` (0 C281 warnings) | `1,241` | **`1,240`** | `1` (`#383 inferno_integration_tests`) | **`0`** | `189.76 s` CTest (`190.49 s` total) | **`0`** |
| **`Debug`** | Exit `0` (0 C281 warnings) | `1,241` | **`1,240`** | `1` (`#383 inferno_integration_tests`) | **`0`** | `1171.06 s` CTest (`1171.78 s` total) | **`0`** |

**Campaign 281 Suite Results (`#1238`–`#1241`, plus fixed `#1123` and updated `#1236`):**
- `#1123 stage260_16_timer_architecture_and_delivery_tests`: **Passed** in both `RelWithDebInfo` and `Debug`
- `#1236 stage280_reconciliation_loader_and_exec_tests`: **Passed** (`0.08 s` RelWithDebInfo, `0.06 s` Debug)
- `#1238 stage281_timer_and_ssbs_tests`: **Passed** (`0.03 s` RelWithDebInfo, `0.02 s` Debug)
- `#1239 stage281_macho_object_reloc_and_linker_tests`: **Passed** (`0.03 s` RelWithDebInfo, `0.03 s` Debug)
- `#1240 stage281_xnu_cswitch_execution_tests`: **Passed** (`0.03 s` RelWithDebInfo, `0.02 s` Debug)
- `#1241 stage281_adversarial_verification_tests`: **Passed (`65 checks passed`)** (`0.24 s` RelWithDebInfo, `0.29 s` Debug)

**Exact Test-Count Reconciliation:** `1,237` (C280) + `4` (`stage281_*` `#1238`–`#1241`) = **`1,241`** discovered tests (`CMakeLists.txt`: `8,789` lines, `514,805` B, SHA-256 `28783263...`). Zero tests were disabled, skipped artificially, or removed.

---

## 13. All 25 Sequential Stage Outcomes (`281.0`–`281.24`)

| Stage | Outcome | Evidence Summary |
|---|---|---|
| `281.0` Baseline & protected-file gate | **PASS** | Verified `5da5404` on `campaign-191r`, C280/C279/C278 ancestry, `WinIOS_README.md` (`61F4...`), `docs\progress.md` (`447E...`), `WinISO26_README.md` absent, `Inferno-Research` isolated. |
| `281.1` Upstream XNU & C280 artifact check | **PASS** | Verified `cswitch.s` (`D95EC6DF...`), `assym.s` (`C7434D10...`), and `cswitch.o` (`3,336` B, `784CE07C...`). |
| `281.2` Timer `#1123` root-cause investigation | **PASS** | Traced C262 fixed-rate coalescing in `deterministic_virtual_timer.cpp:97-160` and 6 downstream campaign contracts. |
| `281.3` Stale `stage260_16` assertion fix | **PASS** | Reconciled `stage260_16_timer_architecture_and_delivery_tests.cpp:110` to expect `3000ULL` (`1000 + 2000`), preserving fixed-rate semantics. |
| `281.4` Timer regression & coalescing tests | **PASS** | Added multi-period catch-up, saturation, snapshot round-trip & determinism checks in `stage281_timer_and_ssbs_tests.cpp`. |
| `281.5` `SysRegId::SSBS` & `PSTATE.SSBS` core state | **PASS** | Added `SSBS` (`0x5A16`) metadata in `sysreg.h/.cpp` and `pstate_ssbs_` accessors/reset in `cpu.h/.cpp`. |
| `281.6` `MRS`/`MSR`/`MSR #imm1` & EL0/EL1 decode/exec | **PASS** | Implemented EL0+ RW access, bit-12 RES0 masking, `MSR SSBS, #0/#1`, disassembly, and reserved `#2..15` traps. |
| `281.7` Exception entry/`ERET`/`SCTLR.DSSBS` semantics | **PASS** | Wired `SPSR[12]` save/restore and `SCTLR_EL1.DSSBS` bit 44 in `encode_spsr`, exception entry, and `ERET`. |
| `281.8` Snapshots, `SmpCoreState` & `ReplayStateHash` | **PASS** | Added `pstate_ssbs` to `SmpCoreState`, `ReplayStateHash::hash_cpu`, and Section 10 padding preserving exact `1,128` B snapshots. |
| `281.9` Existing relocation & loader boundary audit | **PASS** | Confirmed `AppleKernelRelocator` is `MH_EXECUTE`-only and `KernelImageParser`/`MachoUserLoader` reject `MH_OBJECT`. |
| `281.10` `MachOObjectIntegrator` parser & provenance types | **PASS** | Implemented strict Mach-O64 ARM64 `MH_OBJECT` parser, `__DWARF` separation, `LC_SYMTAB`, and `SymbolProvenanceClassification`. |
| `281.11` External binding & anti-spoofing policies | **PASS** | Implemented `UnresolvedExternal` strict gating and rejection of synthetic stubs masquerading as `GenuineUpstream`. |
| `281.12` `ARM64_RELOC_BRANCH26` relocation engine | **PASS** | Implemented bounds/alignment/opcode/`[-128 MiB, +128 MiB - 4]` range-checked `B`/`BL` instruction patching. |
| `281.13` Bounded W^X guest staging & `MH_EXECUTE` builder | **PASS** | Implemented RX (`R|X`) `Memory` + `GuestMemoryMap` staging and standalone `MH_EXECUTE` container build verified by `MachOLoader`. |
| `281.14` Linker & loader-rejection verification | **PASS** | `stage281_macho_object_reloc_and_linker_tests` verified real `cswitch.o` parse/link/container & preserved `KernelImageParser`/`MachoUserLoader` rejections. |
| `281.15` Genuine `_machine_load_context` execution | **PASS** | Stepped actual `cswitch.o` across `0x07C MSR SSBS` -> `d8..d15`, `FPCR`, `SP`, `TPIDR_*`, `cpu_active_thread` -> `0x0B4 RETAA` (`CPACR_EL1.FPEN=3`). |
| `281.16` `_Idle_load_context` & `_machine_set_current_thread` | **PASS** | Verified genuine `_Idle_load_context` (`0x464` SSBS -> `0x498` `RETAA`) and `_machine_set_current_thread` (`ret`). |
| `281.17` Genuine `_Switch_context` execution & audit | **PASS** | Verified `0x190 MRS X4, SSBS` save + `0x240 MSR SSBS` restore + `RETAA`, documenting `0x168 PACIASP` HINT-NOP status honestly. |
| `281.18` External XNU C dependency audit (`_ml_set_interrupts_enabled`) | **PASS** | Audited `machine_routines_common.c:1089-1112`; confirmed cross-TU dependencies prevent standalone unmodified linking. |
| `281.19` `_Call_continuation` unlinked vs `TestHarnessStub` | **PASS** | Proved `x3=1` stops honestly at unlinked `0x114 bl #0` AND executes end-to-end when explicitly bound to audited `__STUB_TEXT` stubs. |
| `281.20` C280 Profile D2 reconciliation | **PASS** | Reconciled stale C280 Profile D2 assumption (`stage280_reconciliation_loader_and_exec_tests.cpp:390-415`) to `0x07C` SSBS pass + `0x080 CPACR_EL1.FPEN` stop. |
| `281.21` CMake integration & warning hygiene | **PASS** | Added `macho_object_integrator.cpp` and 4 `stage281_*` suites; `0` compiler warnings in all Campaign 281 targets across both configs. |
| `281.22` Adversarial verification suite (`A01`–`A64`) | **PASS** | All `65` adversarial checks in `stage281_adversarial_verification_tests.cpp` passed in both `RelWithDebInfo` and `Debug`. |
| `281.23` Dual-config unfiltered sequential CTest | **PASS** | `RelWithDebInfo` (`189.76 s`) & `Debug` (`1171.06 s`): both `1,240 Passed, 1 Skipped (#383), 0 Failed` out of `1,241` (`exit 0`). |
| `281.24` Forensic report, manifest & local commit | **PASS** | Wrote only 2 deliverables in `artifacts\campaign-281\`, computed exact SHA-256 manifest, verified protected files & Inferno isolation, committed locally. |

---

## 14. Blockers, Scope Limits & Artifact Manifest

1. **Level 4 Remains Explicitly Rejected:** Single-TU `cswitch.o` cannot execute an upstream XNU boot without linked Mach/BSD subsystems (`_ml_set_interrupts_enabled`, `_thread_terminate`, `_cpu_doshutdown`, `_cpu_idle`, `kernel_bootstrap`, `sched_init`, `vm_init`, `ipc_init`, `bsd_init`).
2. **`T8120` OSS Redaction & `PACIASP` Scope:** Unchanged from Campaign 280 (`ARM64_BOARD_CONFIG_T8120` absent in public `xnu-10002.1.13`); `PACIASP`/`AUTIASP` (`HINT #25`/`#29`) remain architectural NOPs in base emulator mode per `stage270`/`stage_t4` compatibility contracts.
3. **Artifacts Delivered (`D:\WinISO26\WinIOS\artifacts\campaign-281\` — ONLY two files):**
   - `WINIOS_CAMPAIGN_281_FINAL_REPORT.md`
   - `WINIOS_CAMPAIGN_281_SHA256_MANIFEST.txt`

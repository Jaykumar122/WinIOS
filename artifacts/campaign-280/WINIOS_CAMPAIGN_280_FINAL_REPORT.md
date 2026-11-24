# WINIOS CAMPAIGN 280 — FINAL REPORT

**Authentic Upstream XNU Source Acquisition, Genuine `genassym` Execution, Compiler-Derived ARM64 ABI, Unchanged `cswitch.s` Compilation & Mach-O Provenance Certification**

- **Campaign:** 280 (Stages `280.0`–`280.24`)
- **Repository:** `D:\WinISO26\WinIOS\`
- **Branch:** `campaign-191r`
- **Baseline HEAD (Campaign 279 closeout):** `390ef04dbc6feb5b186ea5dd12e0400c66059529`
- **Campaign 279 Parent (Campaign 278 closeout):** `a5ffb712b0b6423f7d297e9178b8b53499ac5a4c`
- **Pinned Public Upstream:** Apple Public XNU `xnu-10002.1.13` (`https://github.com/apple-oss-distributions/xnu`, commit `1031c584a5e37aff177559b9f69dbd3c8c3fd30a`, tag object `a653a70d861014a544bffa506779088605bbabac`)
- **Host Environment:** Windows 11 x64 (`10.0.26300`), Visual Studio 18 2026 (`18.10.2`), MSVC `cl.exe 19.51.36260` (C++20, `CMakeLists.txt` `winios_set_warnings` `/W4 /permissive- /FS`), CMake `4.4.4`, Ninja `1.13.0.git.kitware.jobserver-pipe-1`, Git `2.56.0.windows.4`, Python `3.12.10` (`capstone 5.0.7`), MSYS `bison 2.4.2`, `flex 2.5.35`
- **Cross-Toolchain:** Zig `0.17.0` (bundled Clang `22.1.8` & LLD)

---

## 1. Executive Summary

Campaign 280 established end-to-end, reproducible Windows-hosted compilation of the **unmodified** upstream Apple XNU context-switch assembly (`osfmk/arm64/cswitch.s`, `xnu-10002.1.13`) using authentic upstream headers, upstream `SETUP/config` (`config.exe` built from source), genuine `osfmk/arm64/genassym.c` output filtered to `assym.s`, and LLVM/Zig Darwin ARM64e assembly compilation, while exposing and correcting four historical misconceptions from Campaigns 278–279:

1. **Upstream `T8120` Redaction:** Public `xnu-10002.1.13` redacts `ARM64_BOARD_CONFIG_T8120` in `pexpert/pexpert/arm64/board_config.h` and rejects unconfigured targets with `osfmk/arm64/proc_reg.h:226:2: error: processor not supported`. Compilation succeeds authentically against `ARM64_BOARD_CONFIG_T8103` (`H13.h`, `MacOSX`, `arm64e`, `-mcpu=apple_a14`, `-DARM_LARGE_MEMORY=1`, `-D__PTRAUTH_INTRINSICS__=1`).
2. **Compiler-Derived ABI vs. Campaign 279 Structural Offsets:** Genuine `assym.s` (312 `#define` lines, 8,632 bytes) and independent Clang `_Static_assert` / `-fdump-record-layouts` proofs establish that `xnu-10002.1.13` stores `thread_t` (not `cpu_data_t *`) in `TPIDR_EL1`, puts `ACT_CPUDATAP` at `+440 (0x1B8)`, `TH_KSTACKPTR` at `+344 (0x158)` (PAC-signed with `APDAKey` and `0x6D42`), `TH_CTH_SELF` at `+328 (0x148)`, `CPU_ACTIVE_THREAD` at `+40 (0x28)`, `CPU_TPIDR_EL0` at `+920 (0x398)`, `thread.continuation` at `+216 (0xD8)`, `thread.parameter` at `+224 (0xE0)`, and exports `_machine_load_context` / `_Call_continuation` (not `_Load_context`).
3. **Genuine `cswitch.o` Relocations vs. Campaign 279 Object:** Campaign 279's `build/load_context_darwin.o` has `nreloc = 0` in `__TEXT,__text` only, while carrying **5** relocations across `__DWARF` sections. Genuine compiled `cswitch.o` (3,336 bytes) has **4** `ARM64_RELOC_BRANCH26` relocations in `__TEXT,__text` (`_ml_set_interrupts_enabled`, `_thread_terminate`, `_cpu_doshutdown`, `_cpu_idle`), **11** `__DWARF` relocations (**15** total), and **4** undefined external symbols.
4. **Unfiltered Dual-Config CTest Ground Truth:** Complete sequential unfiltered CTest runs (`1,237` discovered tests each) in both `Debug` and `RelWithDebInfo` recorded **1,235 Passed**, **1 Skipped** (`#383 inferno_integration_tests`, `SKIP_RETURN_CODE 77`), and **1 pre-existing Failure** (`#1123 stage260_16_timer_architecture_and_delivery_tests`, where Campaign 260 expects `comparator == 3500` after Campaign 262 changed `DeterministicVirtualTimer::rearm_periodic` to advance from the previous deadline `1000 + 2000 = 3000`). All 5 new Campaign 280 verification suites (`#1233`–`#1237`) passed in both configurations with 0 warnings in Campaign 280 files.

---

## 2. Campaign Certification Outcome

| Certification Gate | Verdict | Justification |
|---|---|---|
| **Level 3 — Bounded early XNU execution & modeled integration** | **RETAINED** | Embedded `MH_EXECUTE` kernel parsing and isolated Category C / B / C-trap guest CPU profiles remain bounded and strictly separated. |
| **Genuine upstream assembly milestone** | **AWARDED (`T8103` / `MacOSX` / `arm64e`)** | Unchanged `xnu-10002.1.13/osfmk/arm64/cswitch.s` preprocessed (`cswitch.i`, 23,863 B) and compiled (`cswitch.o`, 3,336 B; `__TEXT,__text` 1,212 B, bit-for-bit reproducible) against authentic headers and generated `assym.s`. `T8120` remains honestly **BLOCKED** by upstream OSS redaction. |
| **Compiler-derived ABI milestone** | **AWARDED (`T8103` / `MacOSX`)** | Genuine `osfmk/arm64/genassym.c` compiled with `-S -fno-integrated-as` (`genassym.o`, 44,409 B) and converted by Git `sed 4.9` via `Makefile.template:433` into `assym.s` (8,632 B, reproduced bit-for-bit from scratch), cross-checked via Clang `_Static_assert` and AST record layouts. |
| **Level 4 — Genuine upstream context-transition integration** | **REJECTED** | `cswitch.o` is an unrelocated `MH_OBJECT` with 4 undefined externals (`_ml_set_interrupts_enabled`, `_thread_terminate`, `_cpu_doshutdown`, `_cpu_idle`); `KernelImageParser` and `MachoUserLoader` refuse `MH_OBJECT`; `_machine_load_context` halts on unimplemented `msr ssbs`; no linked Mach/BSD kernel (`kernel_bootstrap`, `thread_init`, `sched_init`, `vm_init`, `ipc_init`, `clock_init`, `bsd_init`) exists. |

**Mandatory Certification Statement (Retained):**
> *"Campaign 272 verified the upstream `Load_context` contract and executed a WinIOS adaptation in the guest CPU engine. It did not establish execution of the exact upstream XNU context-switch implementation or genuine upstream `kernel_bootstrap` continuation."*

---

## 3. Baseline and Git Ancestry

Verified on repository `D:\WinISO26\WinIOS\` before any Campaign 280 edits:
- **Branch:** `campaign-191r` (`git rev-parse --abbrev-ref HEAD`)
- **Baseline HEAD (C279):** `390ef04dbc6feb5b186ea5dd12e0400c66059529` (`2026-11-24 22:05:14 +0530`)
- **Parent (C278):** `a5ffb712b0b6423f7d297e9178b8b53499ac5a4c` (`2026-11-24 21:33:28 +0530`)
- **Grandparent (C277):** `2dcf630e3a7a5e8f38f8b8789c4668e271d7b550`
- **Ancestors C276–C272:**
  - `5b80b47af1a7246816746a0ad2031d0b2e8d87dd` (C276)
  - `cf4b9ef74b5b219086821bebe2d5d253eb332d92` (C275)
  - `b2b41f6f000037f3612caf395f013378f5079cd2` (C274)
  - `cb75a40b527e390f8837814e71fbfe665248b365` (C273)
  - `3cecb2030db5fc8c0923d12ebf3db797d9ad3877` (C272)
- **Initial Working Tree:** Clean (`nothing to commit, working tree clean`; pre-existing ignored files preserved).

---

## 4. Protected Repository Invariants

| Protected Asset | Expected Condition | Observed Disk Size | Observed SHA-256 | Status |
|---|---|---|---|---|
| `D:\WinISO26\WinISO26_README.md` | Must NOT exist | N/A (`False`) | N/A | **VERIFIED ABSENT** |
| `D:\WinISO26\WinIOS_README.md` | Unmodified | `11,054` B | `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | **VERIFIED INTACT** |
| `D:\WinISO26\WinIOS\docs\progress.md` | Unmodified | `22,363` B | `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | **VERIFIED INTACT** |
| `D:\WinISO26\Inferno-Research\` | Strictly untouched & uninspected | Exists (`True`) | Not inspected | **VERIFIED ISOLATED** |

**Historical `progress.md` Hash Typo Reconciliation:** Disk `docs\progress.md` (`447E...CBE618...`) matches Git blob `d79f162d7a51a2700cabd9f1c2b05cbe5e226c36` untouched since commit `830e73eeaabc2776a737779242525d83aa8fe03d` (2026-10-28); the string `BCE618` in historical prompts is the `CBE618`/`BCE618` 2-character transposition reconciled in Campaign 278.

---

## 5. Campaign 279 Artifact Reconciliation

Campaign 279's historical deliverables were preserved unmodified and verified on disk:
- `WINIOS_CAMPAIGN_279_FINAL_REPORT.md`: `24,517` B, SHA-256 `4910385696522AF14581701C00C676D2D93D2154CFC17D89764350747B2442CD`.
- `WINIOS_CAMPAIGN_279_SHA256_MANIFEST.txt`: `2,142` B, SHA-256 `79A7CBB7B059AB63236F8A0661D0333F8187854C70C3AFCBC4EB6AE0F1947A25`.
- `build/load_context_darwin.s` (**Category C**): `217` B, SHA-256 `66E86105F3EB0035DE36A62391523D60E5654A020E35EC3490F7CA70CCA49ACC`.
- `build/load_context_darwin.o` (**Category C**, Mach-O64): `1,088` B, SHA-256 `63AAAAE9BD3417BD070ACB231593F6004C7751FA2FDBF78EE04A08576724ECFF`:
  - `MH_MAGIC_64` (`0xFEEDFACF`), `CPU_TYPE_ARM64` (`0x0100000C`), `cpusubtype = 0x0`, `MH_OBJECT` (`1`), `ncmds = 4`, `sizeofcmds = 600`.
  - Section 0 `__TEXT,__text`: `32` B at file offset `632`, `nreloc = 0`, opcodes SHA-256 `F79F9246C4AFEBF7132B63D7A1F91BD605359866C197D35A1FA1FF09973AAD2F`.
  - Debug sections `__DWARF`: `__debug_info` (`133` B, `nreloc = 3`), `__debug_abbrev` (`33` B, `nreloc = 0`), `__debug_aranges` (`48` B, `nreloc = 1`), `__debug_line` (`85` B, `nreloc = 1`).
  - **Correction:** `load_context_darwin.o` has `nreloc = 0` in `__TEXT,__text` only; total object relocations = **5** (all `__DWARF`), `nsyms = 3` (`ltmp0`, `ltmp1`, `_Load_context`), `nundefsym = 0`. Additionally, C279 Section 3.2 misstated `WinIOS_README.md` (`1,986 B` vs actual `11,054 B`) and `progress.md` (`25,627 B` vs actual `22,363 B`) sizes while quoting accurate SHA-256 values.

---

## 6. CMakeLists Integrity Investigation

- **Baseline `CMakeLists.txt` (`390ef04`):** `510,341` B, `8,714` lines (including trailing blank lines at EOF), SHA-256 `C558F8DF0CD3D176374C7E980797A42D48657A0D96F950E44D1E8BB404A2C00A`.
- **Integrity finding:** Verified `git diff a5ffb71..390ef04 -- CMakeLists.txt` (`+40` lines adding only the 5 `stage279_*` test executables/registrations). Size stems legitimately from 1,189 pre-existing test executables and 1,230 single `add_test` lines plus the 3-item `foreach(suite success failures arguments)` loop (yielding `1,232` baseline CTest entries). No corrupted or duplicated content exists.
- **Campaign 280 update:** Appended only the 5 `stage280_*` targets (`+41` lines; resulting `CMakeLists.txt` is `512,871` B, `8,755` lines, SHA-256 `52ED2175D2BD04F0DE1F547E15413B9431C74039E2AB0E5FC579822A30A93EAF`, `1,235` `add_test(` lines, `1,237` total `ctest -N` tests).

---

## 7. Upstream XNU Source Acquisition

- **Repository:** `D:\WinISO26\xnu-10002.1.13\` (isolated shallow clone outside WinIOS repo; never bundled inside WinIOS).
- **Remote:** `origin https://github.com/apple-oss-distributions/xnu`
- **Tag:** `xnu-10002.1.13` (annotated tag object `a653a70d861014a544bffa506779088605bbabac`)
- **Commit:** `1031c584a5e37aff177559b9f69dbd3c8c3fd30a`
- **Integrity:** `git status --short` is empty (completely unmodified Category A checkout).
- **License Note:** Governed by Apple Public Source License (APSL 2.0) and CMU Mach license headers; kept externally in `D:\WinISO26\xnu-10002.1.13\` without copying APSL source files into WinIOS.

---

## 8. Exact `cswitch.s` Provenance

| Attribute | Verified Value |
|---|---|
| Repository / Revision | `apple-oss-distributions/xnu` @ `xnu-10002.1.13` (`1031c584a5e37aff177559b9f69dbd3c8c3fd30a`) |
| Path | `D:\WinISO26\xnu-10002.1.13\osfmk\arm64\cswitch.s` |
| File Size / Lines | `11,571` B on disk (CRLF checkout of `11,174` B LF blob), `397` lines |
| SHA-256 (Disk) | `D95EC6DF06995170AAFD6946073D4B20D4DCC0F281A5F9CF3AFBB6400EF42DEF` |
| SHA-256 (Git Blob) | `D1D6AE65CDEDEC7CA17F56B0AA1464706F5C06BB7ABCB5DAA0EB76C326D767B5` |
| Unmodified | Yes (**Category A — Verified upstream**) |
| Direct `#include`s | `<machine/asm.h>`, `<arm64/machine_machdep.h>`, `<arm64/machine_routines_asm.h>`, `<arm64/pac_asm.h>`, `<arm64/proc_reg.h>`, `"assym.s"` |
| Defined Exported Symbols (`7`) | `machine_load_context`, `Call_continuation`, `Switch_context`, `Shutdown_context`, `Idle_context`, `Idle_load_context`, `machine_set_current_thread` |
| Absent Legacy Name | `Load_context` does NOT appear anywhere in `cswitch.s` |
| External Branch Symbols (`4`) | `ml_set_interrupts_enabled`, `thread_terminate`, `cpu_doshutdown`, `cpu_idle` |

---

## 9. Authentic Dependency Graph

Every header included transitively during `cswitch.i` expansion and `genassym.c` compilation was traced directly from preprocessor line markers:

| Path | Size (B) | Lines | SHA-256 | Category |
|---|---|---|---|---|
| `osfmk/arm64/cswitch.s` | 11,571 | 397 | `D95EC6DF06995170AAFD6946073D4B20D4DCC0F281A5F9CF3AFBB6400EF42DEF` | A |
| `osfmk/arm64/genassym.c` | 16,619 | 364 | `BB97784C7B5CBB7F9959CA9B279B92B1516068A6073B8453603CAB5BDC0975FC` | A |
| `osfmk/machine/asm.h` | 1,657 | 41 | `FE8110CC0C9A90B60463A33F3C920B35C2AF5A1C3CD78C530E5034754073143A` | A |
| `osfmk/arm64/asm.h` | 5,407 | 208 | `6B326E30CD2A38642A13B4674B6F427AAE5E2E51DC5E39F90AA7253CF88297CE` | A |
| `osfmk/arm/arch.h` | 2,583 | — | `5731531BA71299946AFC97B4D6CEC06E02D2A7DB9DD0E857D0B269283ED22CC0` | A |
| `osfmk/arm64/machine_machdep.h` | 2,710 | 69 | `4273BDDDECBF1862901923610F21AF987AB549736B5D99D430FF05F2A0A23B4C` | A |
| `osfmk/arm64/machine_routines_asm.h` | 5,858 | 201 | `643D5913FDB33DA36B28C20C38C03565A8DDA0E356A39CF6660126B02CFFB516` | A |
| `osfmk/arm64/pac_asm.h` | 5,545 | 167 | `DB7ED9B58B7069F5AF7B91184448E69FA0C8B3B423A60E6238843AA7A3A59A8C` | A |
| `osfmk/arm64/proc_reg.h` | 104,133 | 2,709 | `18B06CC8F1E6EDF76EABF913B7288D6E3C9A53948147738F9CD02B15A617A99F` | A |
| `osfmk/mach/machine.h` | 18,546 | — | `3185B7F08B76F3ED7F11C8769B76685A1D4EB5FF8CED9758F936B2A23244E405` | A |
| `pexpert/pexpert/arm64/board_config.h` | 4,206 | 155 | `D6B226011C92EFA39FE8835E599AC302C8EFB496EC32DE622CCF14F5452367CC` | A |
| `pexpert/pexpert/arm64/H13.h` | 4,307 | 88 | `BCFC196BEAC9DCC7775B9877C99B84A3D3BE49AFEDDD1CBBA792485C28839F8E` | A |
| `osfmk/kern/thread.h` | 72,534 | 1,808 | `EDF6E913386598968D77BDEE94B33A50EF22323E99C00751BE74F06E46348687` | A |
| `osfmk/arm/thread.h` | 8,369 | 209 | `0DAA8653E7CED4131A0FC79A5FB1503558425B484B9900C248E62A3A751F728B` | A |
| `osfmk/arm/cpu_data.h` | 4,573 | 136 | `BE6899A5A0A9FDC0B3941AC0AAC40A0421955937A5B937925CB5EC45D24D6A26` | A |
| `osfmk/arm/cpu_data_internal.h` | 10,663 | 298 | `E6C64759C28C89AC6BF453103D542A02A0E1B3DE7AE9B8F9CE63842836876AE8` | A |
| `EXTERNAL_HEADERS/ptrauth.h` | 17,918 | 384 | `868A0AAF9234E4B84E9320961B306EA916D4D705DD695983E29DCFF97AFB2424` | A |
| `EXTERNAL_HEADERS/stdint.h` | 7,037 | 239 | `0F0F300426B000F0B292C9E1A1AE467ACB24E14403B68DCBDB0E3EFFE4730D15` | A |
| `RELEASE_ARM64_OUT/meta_features.h` | 1,082 | 41 | `A28DC0FD4BA33A96FF16ACC70495CD59B4285F29A902A5693FCC6DDDF9A6A8BF` | B |
| `RELEASE_ARM64_OUT/assym.s` | 8,632 | 312 | `C7434D10684EF3A11DF04A2AC454EE9CBBC07614B78385632154D7B157FA13F6` | B |

---

## 10. Header and Macro Verification

- **`osfmk/arm64/asm.h`:** Defines `EXT(x)` -> `_ ## x`, `LEXT(x)` -> `_ ## x ## :`, `ENTRY`, `ENTRY2`, `READ_THREAD` (`mrs $0, TPIDR_EL1`), `BRANCH_EXTERN`, `CALL_EXTERN`, `MOV64`, `MOV32`, `ARM64_STACK_PROLOG` (`pacibsp` when `__has_feature(ptrauth_returns)`), and `ARM64_STACK_EPILOG` (`retab`).
- **`T8120` Public OSS Redaction:** `pexpert/pexpert/arm64/board_config.h` defines `ARM64_BOARD_CONFIG_T8101` and `ARM64_BOARD_CONFIG_T8103` (`H13.h`), followed by empty redacted lines before `BCM2837`. `ARM64_BOARD_CONFIG_T8120` is absent. Preprocessing `cswitch.s` with `-DARM64_BOARD_CONFIG_T8120=1` fails with exit code `1`: `osfmk/arm64/proc_reg.h:226:2: error: processor not supported`.
- **PAC Intrinsics Macro Requirement:** Unlike Apple Clang, upstream LLVM/Clang `22.1.8` bundled with Zig `0.17.0` does not predefine `__PTRAUTH_INTRINSICS__`. Omitting `-D__PTRAUTH_INTRINSICS__=1` causes `EXTERNAL_HEADERS/ptrauth.h:362` to fall back to `({ (void)__string; (int)0; })`, aborting compilation of `genassym.c` at `ccdrbg_impl.h:32:11: error: statement expression not allowed at file scope` (`rc=1`, `20 errors generated`).

---

## 11. Toolchain Capability

| Tool | Exact Version & Path | Verified Capability / Limitation |
|---|---|---|
| `zig` | `0.17.0` (`...\zig.EXE`) | Drives Mach-O64 ARM64/ARM64e preprocessing, C compilation (`-S -fno-integrated-as`), and assembly (`-target aarch64-macos -arch arm64e`) natively on Windows 11. |
| Bundled `clang` | `clang version 22.1.8` (`Target: x86_64-unknown-windows-gnu`) | Supports `-mcpu=apple_a14` (underscore required; rejects hyphenated `apple-a14`), `-Xclang -fptrauth-intrinsics`, `-Xclang -fptrauth-function-pointer-type-discrimination`. Emits unused-arg warnings for `-Xclangas` and `-c` during driver translation. |
| CMake / Ninja | `cmake version 4.4.4`, `ninja 1.13.0.git.kitware.jobserver-pipe-1` | Configures Visual Studio 18 2026 (`18.10.2`, MSVC `19.51.36260`) cleanly. |
| Git / MSYS tools | `git 2.56.0.windows.4`, Git `/usr/bin/sed 4.9`, MSYS `bison 2.4.2`, `flex 2.5.35` | Compiled `SETUP/config/` (`config.exe`) and ran exact `Makefile.template:433` `sed` pipeline via Git `sh.exe`. |

---

## 12. Build Configuration Reconstruction

Authentic assembly and generator flags were reconstructed from `config/MASTER`, `config/MASTER.arm64`, `config/MASTER.arm64.MacOSX`, `osfmk/conf/Makefile.template`, `osfmk/conf/Makefile.arm64`, and `makedefs/MakeInc.def`:
- **Include order (`MakeInc.def:904-911`):** `EXPORT_HDRS/bsd` MUST precede `osfmk/libsa` so BSD integer types shadow standalone Mach libsa headers; `-I` also includes `RELEASE_ARM64_OUT`, `osfmk`, `EXPORT_HDRS/osfmk`, `EXPORT_HDRS`, `EXPORT_HDRS/iokit`, `libkern`, `pexpert`, `san`, `security`, and `EXTERNAL_HEADERS`.
- **Key defines (`MakeInc.def:335`, `Makefile.template:22-23`):** `-DAPPLE -DKERNEL -DKERNEL_PRIVATE -DXNU_KERNEL_PRIVATE -DMACH_KERNEL -DMACH_KERNEL_PRIVATE -DPRIVATE -D__MACHO__=1 -D__arm64__=1 -DARM64 -DARM64_BOARD_CONFIG_T8103=1 -DH13=1 -DXNU_TARGET_OS_OSX=1 -DARM_LARGE_MEMORY=1 -D__PTRAUTH_INTRINSICS__=1 -include meta_features.h`, plus `CONFIG_DEFINES` emitted by `config.exe` in `RELEASE_ARM64_OUT/Makefile`.
- **Reproducible Deviation Log:** Target switched from redacted `T8120` (`iPhoneOS`) to authentic `T8103` (`H13`, `MacOSX`) with `-DARM_LARGE_MEMORY=1`; `-mcpu=apple_a14` syntax adapted for Zig CLI; `-D__PTRAUTH_INTRINSICS__=1` passed explicitly for Clang 22.1.8; `MAKE` rules bypassed in favor of direct isolated commands without modifying any WinIOS or XNU source file.

---

## 13. Genuine `genassym` Source Audit

Inspected `D:\WinISO26\xnu-10002.1.13\osfmk\arm64\genassym.c` (`16,619` B, `364` lines, SHA-256 `BB97784C7B5CBB7F9959CA9B279B92B1516068A6073B8453603CAB5BDC0975FC`):
- Includes `<kern/thread.h>`, `<arm/thread.h>`, `<arm/cpu_data_internal.h>`, and emits inline-asm markers (`DEFINITION__define__<NAME>:\t .ascii "<VALUE>"`) inside `_main`.
- **Not a host executable:** Per `osfmk/conf/Makefile.template:428-433`, XNU compiles `genassym.c` to target-ABI **assembly text** (`-S -fno-integrated-as -o genassym.o`) and extracts `assym.s` via `sed`, never executing `genassym` on the host.

---

## 14. Generator Compilation (`config.exe` & `genassym.o`)

1. **Upstream `config.exe`:** Built from `SETUP/config/` (`main.c`, `mkheaders.c`, `config.h`, `config.y`, `config.l`) via MSYS `bison 2.4.2` / `flex 2.5.35` -> `D:\WinISO26\xnu_probe_isolated\config_build\config.exe` (`937,472` B, PE `MZ` header, SHA-256 `8B994EFCAB292184544ADD91626A38A5074870C30EA39DDCFB30C4769F1FDCE7`). Executed against `RELEASE_ARM64` (`5,300` B, `340ED84D...`) -> emitted `43` files in `RELEASE_ARM64_OUT\` (`Makefile`, `meta_features.h`, `mach_ldebug.h`, `mach_assert.h`, `debug.h`, `ioconf.c`, etc.).
2. **Upstream `genassym.c` compilation (`Category B`):**
   - **Command (verified in isolated clean directory `repro_verify`):** `zig cc -target aarch64-macos -arch arm64e -mcpu=apple_a14 -Xclang -fptrauth-intrinsics -Xclang -fptrauth-function-pointer-type-discrimination -fno-integrated-as -fno-builtin -nostdinc -std=gnu11 -include meta_features.h -S -O2 -o <out>/genassym.o <CONFIG_DEFINES + ARM64/OSX/T8103/ARM_LARGE_MEMORY/__PTRAUTH_INTRINSICS__ defines> <INCFLAGS> D:\WinISO26\xnu-10002.1.13\osfmk\arm64\genassym.c`
   - **Exit code:** `0`
   - **Primary output:** `RELEASE_ARM64_OUT\genassym.o` (`44,409` B, SHA-256 `02EA59FB860A87A09F8542FB6CB76560547E138E67DB8ACB999E19FB49425951`; textual ARM64 assembly emitting all `DEFINITION__define__*` directives on single lines).

---

## 15. Actual `assym.s` Generation

- **Command (`Makefile.template:433` via Git `sh.exe` + `/usr/bin/sed` 4.9):**
  `/usr/bin/sed -e '/^[[:space:]]*DEFINITION__define__/!d;{N;s/\n//;}' -e 's/^[[:space:]]*DEFINITION__define__\([^:]*\):.*ascii.*\"[\$]*\([-0-9\#]*\)\".*$/#define \1 \2/' -e 'p' -e 's/#//2' -e 's/^[[:space:]]*#define \([A-Za-z0-9_]*\)[[:space:]]*[\$#]*\([-0-9]*\).*$/#define \1_NUM \2/' genassym.o > assym.s`
- **Exit code:** `0`
- **Output (`Category B`):** `8,632` B, `312` lines (`156` symbol + `_NUM` pairs), SHA-256 `C7434D10684EF3A11DF04A2AC454EE9CBBC07614B78385632154D7B157FA13F6`.
- **Bit-for-Bit Reproduction Proof:** Re-running `genassym.c` -> `sed` inside clean `D:\WinISO26\xnu_probe_isolated\gen_build\repro_verify\` produced `repro_verify\assym.s` of **8,632 bytes** with identical SHA-256 `C7434D10684EF3A11DF04A2AC454EE9CBBC07614B78385632154D7B157FA13F6` (`MATCH: True`).
- **Critical Generated Offsets (`assym.s`):** `TH_KSTACKPTR = 344 (0x158)`, `TH_KSTACKPTR_DIVERSIFIER = 27970 (0x6D42)`, `ACT_CPUDATAP = 440 (0x1B8)`, `TH_CTH_SELF = 328 (0x148)`, `CPU_ACTIVE_THREAD = 40 (0x28)`, `CPU_ISTACKPTR = 16 (0x10)`, `CPU_ISTACKPTR_DIVERSIFIER = 59725 (0xE94D)`, `CPU_RESET_HANDLER = 168 (0xA8)`, `CPU_TPIDR_EL0 = 920 (0x398)`, `cdSize = 928`, `ARM_KERNEL_CONTEXT_SIZE = 192`, `SS64_KERNEL_SP = 96`, `PGBYTES = 16384`, `VM_MIN_KERNEL_ADDRESS = -2199023255552 (0xFFFFFE0000000000)`, `THREAD_CONTINUE_T_DISC = 54535 (0xD507)`. (`TH_CONTINUATION` and `TH_PARAMETER` are absent from `assym.s` because XNU passes/reads them in C, not assembly.)

---

## 16. Independent ABI Verification

Independently verified via two C probes against `xnu-10002.1.13` headers with Zig/Clang `22.1.8`:
1. **`abi_static_assert_probe.c` -> `abi_static_assert_probe.o` (`1,880` B, SHA-256 `A278C4EAB78D022526C1BE62F0EF2D94F27E8642883F6EE7C0D57DDC45CB3C69`, `rc=0`):**
   Compile-time `_Static_assert`s passed on `offsetof(struct thread, machine.kstackptr) == 344`, `machine.CpuDatap == 440`, `machine.cthread_self == 328`, `machine.upcb == 272`, `machine.arm_machine_flags == 340`, `thread_id == 1176`, `offsetof(cpu_data_t, cpu_active_thread) == 40`, `istackptr == 16`, `cpu_tpidr_el0 == 920`, `sizeof(arm_kernel_context_t) == 192`, `ss.fp == 80`, `ss.lr == 88`, `ss.sp == 96`.
2. **`abi_layouts_probe.c` -> `abi_layouts_probe.s` (`17,368` B, SHA-256 `72D7B808A4E36FA6795A345AD307BFFC5CA598A17E26A154E1F5A6DDE3C2FEB8`) & Clang `-fdump-record-layouts` (`record_layouts.txt`, `144,784` B):**
   Confirmed C-side offsets `offsetof(struct thread, continuation) == 216 (0xD8)`, `offsetof(struct thread, parameter) == 224 (0xE0)`, `sizeof(struct thread) == 1328 (0x530)`, and `sizeof(struct cpu_data) == 928 (0x3A0)`.

---

## 17. Unchanged Upstream Preprocessing

- **Command:** `zig cc -E -o <out>/cswitch.i -target aarch64-macos -arch arm64e -mcpu=apple_a14 -Xclang -fptrauth-intrinsics -Xclang -fptrauth-function-pointer-type-discrimination -D__ASSEMBLER__=1 -DASSEMBLER=1 -x assembler-with-cpp -nostdinc -include meta_features.h <CONFIG_DEFINES + ARM64/OSX/T8103/ARM_LARGE_MEMORY/__PTRAUTH_INTRINSICS__> <INCFLAGS> D:\WinISO26\xnu-10002.1.13\osfmk\arm64\cswitch.s`
- **Exit code:** `0` (warnings only for `-Xclangas` / unused assembler flags; zero errors).
- **Output (`Category B`):** `RELEASE_ARM64_OUT\cswitch.i`, `23,863` B, `921` lines, SHA-256 `E7E62C5CCC5D58D75FEBD2B45106D0227352E3F2FEF76CC9BE47D424561D4C65`.
- **Input Integrity:** `D:\WinISO26\xnu-10002.1.13\osfmk\arm64\cswitch.s` remained 100% unchanged (`D95EC6DF...`).

---

## 18. Expanded-Source Integrity

Audited `cswitch.i`:
- Pulls `RELEASE_ARM64_OUT\meta_features.h` + 27 feature headers generated by `config.exe`.
- Expands `osfmk/machine/asm.h` -> `osfmk/arm64/asm.h` (`ENTRY`, `READ_THREAD`, `ARM64_STACK_PROLOG`, etc.), `bsd/arm/types.h` (prioritized ahead of `libsa`), `pexpert/arm64/H13.h`, `proc_reg.h` (`CMSR`, `SANITIZE_FPCR`), `pac_asm.h`, and generated `assym.s`.
- Emits genuine ARM64e PAC instructions (`autda`, `xpacd`, `paciasp`, `blraa`, `retaa`), discriminator constants `0x6D42` (`27970`) and `0xD507` (`54535`), and SSBS/FPCR/floating-point context save/restore sequences.

---

## 19. Unchanged Upstream Assembly Compilation

- **Command:** Same invocation as Stage 280.13 with `-c -g -o <out>/cswitch.o` instead of `-E`.
- **Exit code:** `0`
- **Output (`Category B`):** `RELEASE_ARM64_OUT\cswitch.o`, `3,336` B, SHA-256 `784CE07CE532916FC5897A866F673A182C6DE5F5AD2A9E6478D44BB8B7AB4382`.
- **Closed-Loop Reproducibility Check:** Compiling `cswitch.s` inside clean `repro_verify/` using freshly regenerated `repro_verify\assym.s` succeeded (`rc=0`, `3,328` B due only to shorter `__DWARF` dir path `repro_verify` vs `RELEASE_ARM64_OUT`) and yielded a **100% bit-for-bit identical 1,212-byte `__TEXT,__text` payload** (`SHA-256 5019D4A62ACDEC0D824F05156F7A1924C52FE234463F92CB9901CBF4C535705B`).

---

## 20. Mach-O Structural Inspection

| Field | `cswitch.o` (**Category B**, Upstream XNU) | `load_context_darwin.o` (**Category C**, C279) |
|---|---|---|
| Magic | `0xFEEDFACF` (`MH_MAGIC_64`) | `0xFEEDFACF` (`MH_MAGIC_64`) |
| CPU Type / Subtype | `0x0100000C` (`ARM64`) / `0x80000002` (`ARM64E + PTRAUTH`) | `0x0100000C` (`ARM64`) / `0x0` (`ARM64_ALL`) |
| File Type | `1` (`MH_OBJECT`) | `1` (`MH_OBJECT`) |
| Load Commands | `4` (`sizeofcmds = 600`) | `4` (`sizeofcmds = 600`) |
| `LC_SEGMENT_64` | `vm=0x0+0x843`, `file=0x278+0x843`, `prot=7/7`, `nsects=5` | `vm=0x0+0x14B`, `file=0x278+0x14B`, `prot=7/7`, `nsects=5` |
| `__TEXT,__text` | `1,212` B (`303` insns) @ off `632`, `align=2^5`, `nreloc=4` | `32` B (`8` insns) @ off `632`, `align=2^2`, `nreloc=0` |
| `__DWARF` sections | 4 sections (`__debug_info` 416 B, `__debug_abbrev` 33 B, `__debug_aranges` 48 B, `__debug_line` 406 B) | 4 sections (`__debug_info` 133 B, `__debug_abbrev` 33 B, `__debug_aranges` 48 B, `__debug_line` 85 B) |
| `LC_BUILD_VERSION` | `platform=1 (macOS)`, `minos=0xF0000 (15.0.0)` | `platform=1 (macOS)`, `minos=0xF0000 (15.0.0)` |
| Total Size / SHA-256 | `3,336` B / `784CE07C...AB4382` | `1,088` B / `63AAAAE9...24ECFF` |

---

## 21. Complete Symbol and Relocation Audit

**`LC_SYMTAB` (`nsyms = 14`, `strsize = 240`) & `LC_DYSYMTAB` (`iloc=0/3, iextdef=3/7, iundef=10/4`) of `cswitch.o`:**
- **Local symbols (`3`):** `ltmp0` (`sect=1, 0x0000`), `BRK_AUTDA_FAILURE` (`N_ABS, sect=0, val=0xC472`), `ltmp1` (`sect=4, 0x067D`).
- **Defined External `__TEXT,__text` symbols (`7`):** `_machine_load_context` (`0x0000`), `_Call_continuation` (`0x00C0`), `_Switch_context` (`0x0140`), `_Shutdown_context` (`0x0280`), `_Idle_context` (`0x0340`), `_Idle_load_context` (`0x0400`), `_machine_set_current_thread` (`0x049C`).
- **Undefined External symbols (`4`):** `_cpu_doshutdown`, `_cpu_idle`, `_ml_set_interrupts_enabled`, `_thread_terminate`.

**Complete Relocation Inventory (`15` total across all sections):**
- **`__TEXT,__text` (`4` external `ARM64_RELOC_BRANCH26`, `extern=1, pcrel=1, len=4, type=2`):**
  1. `0x0114` (`bl #0`, `0x94000000`) -> `_ml_set_interrupts_enabled`
  2. `0x012C` (`b #0`, `0x14000000`) -> `_thread_terminate`
  3. `0x0334` (`b #0`, `0x14000000`) -> `_cpu_doshutdown`
  4. `0x03F0` (`b #0`, `0x14000000`) -> `_cpu_idle`
- **`__DWARF` (`11` section-relative `type=0`, `len=8`):** `__debug_info` (`9`), `__debug_aranges` (`1`), `__debug_line` (`1`), `__debug_abbrev` (`0`).

Unresolved externals prevent standalone execution; full Mach/BSD kernel linking (`ld64`) is required to resolve these symbols.

---

## 22. Genuine Disassembly

Disassembled `cswitch.o` (`cswitch_disasm.txt`, `12,932` B, `310` lines, SHA-256 `D57630231B18F919377BF90BD5D8D3D9CB67B945F90D054307EA0F1A47D97750`) using Python `capstone 5.0.7` (`CS_ARCH_ARM64, CS_MODE_ARM`) over all `303` instructions (`0x000..0x4B8`):
- `_machine_load_context` (`0x000..0x0B4`): Sets `TPIDR_EL1 = x0` (`0xD518D080`), updates `cpu_data[+0x28]`, loads `TPIDR_EL0` & `TPIDRRO_EL0`, authenticates `thread[+0x158]` via `movk x2, #0x6d42, lsl #48; autda x1, x2; xpacd` + `brk #0xc472` guard, restores `FPCR`, `x19..x30`, `SP`, `SSBS`, `d8..d15`, zeroes `x0`, returns via `retaa` (`0xD65F0BFF`).
- `_Call_continuation` (`0x0C0..0x12C`): Reads `thread = TPIDR_EL1`, authenticates `TH_KSTACKPTR` (`+0x158`, `APDA` + `0x6D42`), sets `SP` and `x29 = 0`, optionally calls `bl _ml_set_interrupts_enabled` (`0x114` when `x3 != 0`), loads `x0=param, x1=wait_result, x21=#0xd507`, dispatches via `blraa x20, x21` (`0xD73F0A95`), falls through to `b _thread_terminate` (`0x12C`).
- `_Switch_context` (`0x140..0x274`): `paciasp` prologue (`0x168`), saves/restores callee-saved GPRs/FPRs/SSBS/FPCR, updates `TPIDR_EL1`/`cpu_active_thread`/`TPIDR_EL0`/`TPIDRRO_EL0`, returns via `retaa`.
- `_Shutdown_context` (`0x280..0x334`) & `_Idle_context` (`0x340..0x3F0`): Saves kernel context, switches to `CPU_ISTACKPTR` (`cpu_data+0x10`, `APDA + 0xE94D`), branches to `_cpu_doshutdown` / `_cpu_idle`.
- `_Idle_load_context` (`0x400..0x498`) & `_machine_set_current_thread` (`0x49C..0x4B8`): Restores idle context via `retaa` / updates `TPIDR_EL1`, `cpu_active_thread`, `TPIDR_EL0`, `TPIDRRO_EL0` (`0x49C..0x4B8`, ending in `ret`).

---

## 23. Adaptation Reconciliation

| Dimension | Campaign 279 Category C Adaptation (`_Load_context`) | Actual `xnu-10002.1.13` (`cswitch.o`) | Classification |
|---|---|---|---|
| `TPIDR_EL1` | Stores `cpu_data_t *` | Stores `thread_t` (`0x000: msr tpidr_el1, x0`) | **Adapted correspondence** |
| `cpu_active_thread` | Stored at `cpu_data + 0x10` | Stored at `cpu_data + 0x28` (`CPU_ACTIVE_THREAD=40`, `cpu_data` via `thread+0x1B8`) | **Adapted correspondence** |
| `TH_KSTACKPTR` | Offset `+0x00`, unsigned | Offset `+0x158` (`344`), PAC-signed (`APDA + 0x6D42`) + `xpacd` check | **Adapted correspondence** |
| Stack Pointer (`SP`) | `mov sp, x2` | Loaded from `[x1, #0x60]` in `machine_load_context`, `TH_KSTACKPTR` in `Call_continuation` | **Adapted correspondence** |
| Continuation & Param | Loaded from `thread + 0x40`/`0x48` in asm | Handled in C (`+0xD8`/`+0xE0`); passed as `x0`/`x1`/`x2` into `Call_continuation` | **Adapted correspondence** |
| Branch / Return / LR | `mov lr, #0; br x1` | `retaa` (`machine_load_context`), `blraa x20, #0xd507` (`Call_continuation`) | **Missing from adaptation** |
| PAC, `SSBS`, `FPCR`, `d8..d15`, `TPIDR_EL0` | Absent | Present and actively enforced | **Missing from adaptation** |

Campaign 279's claim of "100% Faithful" alignment across these fields is formally superseded by **Adapted correspondence** and **Missing from adaptation**.

---

## 24. Loader and Execution Boundaries

Verified in `stage280_reconciliation_loader_and_exec_tests.cpp` across **4 strictly separated profiles**:
- **Loader Contract (`Stage 280.20`):** `KernelImageParser::parse` accepts `kEmbeddedXnuImage210Data` (`MH_EXECUTE`, `Success`), while rejecting both genuine `cswitch.o` and C279 `load_context_darwin.o` with `KernelImageParseStatus::UnsupportedFileType`. `MachoUserLoader::load_image` rejects both `MH_OBJECT` files (`valid == false`). Direct `MH_OBJECT` execution by loaders is prohibited.
- **Profile A (WinIOS Category C Adaptation):** Extracted `load_context_darwin.o` `__TEXT,__text` (32 B) into `Memory`, executed all 8 instructions in `CPU`; verified `cpu_data[+0x10]`, `SP`, `X0`, `LR=0`, and landing `PC`.
- **Profile B (Genuine Compiled `cswitch.o` Subroutines under `assym.s` + PAC):** Loaded compiled `cswitch.o` `__TEXT,__text` (303 words) into `Memory`, configured `PacKeyRing`, populated `thread_t` / `cpu_data_t` at exact `assym.s` offsets, signed `TH_KSTACKPTR` (`APDA + 0x6D42`) and continuation (`APIA + 0xD507`), executed real `_machine_set_current_thread` (`+0x49C`) and `_Call_continuation` (`+0x0C0`, `x3=0` path) in `CPU`, reaching continuation via `blraa x20, x21` with `SP`, `X29=0`, `X0=param`, `X1=wait_result` verified. (Note: the continuation target remains a bounded test landing address; upstream `kernel_bootstrap` subsystems are unexecuted.)
- **Profile C (PAC Tampering Protection):** Corrupted `TH_KSTACKPTR` tag (`APDA`) and wrong continuation type discriminator (`!= 0xD507`) both trap cleanly in `Arm64ePointerAuth` (`has_fault() == true`) before branch/stack switch.
- **Profile D (Honest Unlinked Relocation & Sysreg Blocking Boundary):** `_Call_continuation` with `x3=1` halts cleanly at the unlinked `0x0114` `bl #0` (`0x94000000`) `_ml_set_interrupts_enabled` relocation site; `_machine_load_context` halts cleanly at `0x007C` (`msr ssbs, x2`). Neither `kernel_bootstrap`, `thread_init`, `sched_init`, `vm_init`, `ipc_init`, `clock_init`, nor `bsd_init` was executed.

---

## 25. Adversarial Testing

Implemented `tests/unit/stage280_adversarial_provenance_tests.cpp` executing **60 real deterministic adversarial tests (`D01`–`D60`)** against live WinIOS parsers, loaders, Mach-O auditors, `assym.s` validators, PAC engine, and file SHA-256 checks (0 mocked tautologies):
- **`D01`–`D10`:** Null/truncated kernels, bad magic, `x86_64` CPU type, invalid subtype, `MH_OBJECT` passed to `KernelImageParser` (`cswitch.o`, `load_context_darwin.o`), cmdsize overflow, truncated segment.
- **`D11`–`D20`:** Direct `MH_OBJECT` rejected by `MachoUserLoader`, user Mach-O magic/`cputype` corruption, `cswitch.o` missing `arm64e` subtype, truncated `cswitch.o`, forged OOB `nreloc`, forged 0-reloc/0-undef non-XNU object, OOB symbol `n_strx`, cmdsize > filesize.
- **`D21`–`D32`:** `assym.s` drift detection (`TH_KSTACKPTR`, `27970` diversifier, `ACT_CPUDATAP`, `CPU_ACTIVE_THREAD` `40` vs `16` confusion, `TH_CTH_SELF`, `54535`, `16384` vs `4096` granule), `cswitch.s` 1-bit tamper hash check, `board_config.h` `T8120` redaction enforcement, C279 `_Load_context` vs `_machine_load_context` distinction.
- **`D33`–`D45`:** `autda` wrong/zero discriminator, wrong slot address, wrong key (`APDB`), unsigned pointer, canonical bit flip, tag bit flip, `blraa` wrong `0xD507` discriminator / wrong `APIB` key / unsigned fn, `retaa` shifted `SP` / unsigned `LR`, `CorruptUpperBits` error-bit + `xpacd != autda` check.
- **`D46`–`D60`:** Protected file SHA-256 tamper rejections, `WinISO26_README.md` guard, `load_context_darwin.o` `0` text relocs vs `5` total object relocs false-claim rejection, 5 Level 4 inflation gate rejections, all 4 unlinked `BRANCH26` opcodes in `cswitch.o`, and 4-byte misaligned segment on 16 KiB granule.
- **Outcome:** `60 / 60` (`D01`–`D60`) **PASSED** in both `Debug` and `RelWithDebInfo`.

---

## 26. Full Debug CTest

- **Configure:** `cmake -S D:\WinISO26\WinIOS -B D:\WinISO26\WinIOS\build` -> `0` (`Configuring done (0.4s)`)
- **Build:** `cmake --build D:\WinISO26\WinIOS\build --config Debug -- /m` -> `0` (`1,195` linked outputs, `0` errors; `0` warnings across all Campaign 280 targets; `2` pre-existing `C4189` warnings in `stage207_17` and `stage209_21`)
- **Unfiltered CTest Command:** `ctest --test-dir D:\WinISO26\WinIOS\build -C Debug --output-on-failure`
- **Total Discovered:** `1,237`
- **Passed:** `1,235` (including `5 / 5` `stage280_*` `#1233`–`#1237` in `0.28s`)
- **Skipped:** `1` (`#383 - inferno_integration_tests`, `SKIP_RETURN_CODE 77`, isolated Inferno env vars unset)
- **Failed:** `1` (`#1123 - stage260_16_timer_architecture_and_delivery_tests`, pre-existing assertion at `tests\unit\stage260_16_timer_architecture_and_delivery_tests.cpp:110`: `timer.get_comparator(0) == 3500ULL` vs C262 coalesced `3000ULL`)
- **Not Run (excluding Skipped):** `0`
- **CTest Exit Code:** `8`
- **Elapsed Time:** `1118.40 sec` CTest total (`1119.22 sec` wrapper wall clock)

---

## 27. Full RelWithDebInfo CTest

- **Build:** `cmake --build D:\WinISO26\WinIOS\build --config RelWithDebInfo -- /m` -> `0` (`1,195` linked outputs, `0` errors; `0` warnings across Campaign 280 targets; `2` pre-existing `C4189` warnings in `stage207_17` and `stage209_21`)
- **Unfiltered CTest Command:** `ctest --test-dir D:\WinISO26\WinIOS\build -C RelWithDebInfo --output-on-failure`
- **Total Discovered:** `1,237`
- **Passed:** `1,235` (including `5 / 5` `stage280_*` `#1233`–`#1237` in `0.25s`)
- **Skipped:** `1` (`#383 - inferno_integration_tests`, `SKIP_RETURN_CODE 77`)
- **Failed:** `1` (`#1123 - stage260_16_timer_architecture_and_delivery_tests`, same pre-existing line 110 assertion)
- **Not Run (excluding Skipped):** `0`
- **CTest Exit Code:** `8`
- **Elapsed Time:** `177.56 sec` CTest total (`178.43 sec` wrapper wall clock)

*(Note: A parallel `-j 8` `RelWithDebInfo` run failed 4 additional `stage44_lse_tests.cpp:142` snapshot tests due to shared filename contention; all 4 passed cleanly in sequential execution.)*

---

## 28. Exact Test-Count Reconciliation

- **Campaign 279 Baseline Reconciled:** Campaign 279 `CMakeLists.txt` had `1,230` single `add_test(` lines plus `foreach(suite success failures arguments)` at line 1835 (`+2` net, total `1,232` CTest entries). C279's `"1,231 / 1,231 Passed"` + `"1 expected skip"` resulted from passing `-E 'inferno_integration_tests'` to CTest, excluding `#383` rather than running unfiltered CTest, and left pre-existing failure `#1123` (`stage260_16`) undocumented.
- **Campaign 280 Unfiltered Accounting:** Adding `5` Campaign 280 suites increases `ctest -N` discovery from `1,232` to `1,237`: `1,235 Passed + 1 Skipped (#383) + 1 Failed (#1123, pre-existing) = 1,237`. Zero tests were removed, disabled, renamed, or weakened.

---

## 29. All 25 Stage Outcomes

| Stage | Outcome | Evidence Summary |
|---|---|---|
| `280.0` Baseline & protection gate | **PASS** | HEAD `390ef04`, parent `a5ffb71`, `campaign-191r`, clean tree, all 4 protected assets verified, tool versions recorded. |
| `280.1` C279 artifact audit | **PASS** | `load_context_darwin.s` (217 B) & `.o` (1,088 B, Category C) verified; `__TEXT` `nreloc=0` vs total `5` `__DWARF` relocs reconciled. |
| `280.2` CMakeLists integrity | **PASS** | `510,341` B baseline clean (`+40` C279 diff), `1,232` baseline tests (`1,237` after C280). |
| `280.3` Official XNU acquisition | **PASS** | External `xnu-10002.1.13` (`1031c584a5e37aff177559b9f69dbd3c8c3fd30a`), unmodified Category A. |
| `280.4` Exact `cswitch.s` check | **PASS** | `11,571` B (`D95EC6DF...`), 397 lines, 7 defined symbols (`machine_load_context`, `Call_continuation`, etc.), no `Load_context`. |
| `280.5` Dependency graph | **PASS** | Complete transitive header graph from `cswitch.i` & `genassym.c` hashed. |
| `280.6` Header & macro audit | **PASS** | `asm.h` macros traced; `T8120` OSS redaction (`proc_reg.h:226`) & `__PTRAUTH_INTRINSICS__` failure reproduced. |
| `280.7` Toolchain verification | **PASS** | Zig `0.17.0` / Clang `22.1.8` Darwin ARM64/ARM64e verified (`apple_a14`, `-D__PTRAUTH_INTRINSICS__=1`). |
| `280.8` Build config reconstruction | **PASS** | `config/MASTER.arm64.MacOSX`, `MakeInc.def:335`, `Makefile.template:430-433` reconstructed with exact `EXPORT_HDRS` include order. |
| `280.9` `genassym` source audit | **PASS** | `genassym.c` (16,619 B) verified; confirmed `-S` assembly emitter per XNU Makefile rules. |
| `280.10` `genassym` compilation | **PASS** | `config.exe` built & run; `genassym.c` compiled to `genassym.o` (44,409 B, `rc=0`). |
| `280.11` `assym.s` generation | **PASS** | `assym.s` (`8,632` B, `312` lines, `C7434D10...`) generated via Git `sed 4.9` and reproduced bit-for-bit. |
| `280.12` Independent ABI check | **PASS** | `abi_static_assert_probe.o` (`rc=0`) & `abi_layouts_probe.s` verified `TH_KSTACKPTR=344`, `ACT_CPUDATAP=440`, `continuation=216`, `parameter=224`. |
| `280.13` Upstream preprocessing | **PASS** | Unchanged `cswitch.s` -> `cswitch.i` (`23,863` B, `rc=0`, `E7E62C5C...`). |
| `280.14` Expanded-source audit | **PASS** | Confirmed `meta_features.h`, `assym.s`, `H13.h`, and expanded PAC/SSBS/FPCR sequences in `cswitch.i`. |
| `280.15` Upstream asm compile | **PASS** | Unchanged `cswitch.s` -> `cswitch.o` (`3,336` B, `rc=0`, `784CE07C...`); `__TEXT,__text` reproduced bit-for-bit. |
| `280.16` Mach-O inspection | **PASS** | Verified `MH_MAGIC_64`, `ARM64E | PTRAUTH`, `MH_OBJECT`, `5` sections, `LC_BUILD_VERSION`, `LC_SYMTAB`, `LC_DYSYMTAB`. |
| `280.17` Symbol & reloc audit | **PASS** | All `14` symbols (`3` local, `7` def ext, `4` undef ext) and all `15` relocations (`4` `__TEXT` + `11` `__DWARF`) audited. |
| `280.18` Genuine disassembly | **PASS** | Capstone `5.0.7` disassembled all `303` instructions (`cswitch_disasm.txt`, `D5763023...`). |
| `280.19` Adaptation reconciliation | **PASS** | Reclassified C279 offsets/TPIDR_EL1/LR-branch from `"100% Faithful"` to **Adapted correspondence** / **Missing**. |
| `280.20` Loader & boundaries | **PASS** | Proved `KernelImageParser` & `MachoUserLoader` accept `MH_EXECUTE` and reject `MH_OBJECT` (`cswitch.o` & C279). |
| `280.21` Execution & continuation | **PASS** | Verified Profiles A, B (`_machine_set_current_thread` & `_Call_continuation` w/ PAC), C (traps), and D (`msr ssbs` / unlinked `bl` stops). |
| `280.22` Adversarial provenance | **PASS** | All `60 / 60` (`D01`–`D60`) tests in `stage280_adversarial_provenance_tests.cpp` passed. |
| `280.23` Dual-config CTest | **PASS (C280) / HONEST FAILURE (#1123)** | Unfiltered `Debug` (`1118.40s`) & `RelWithDebInfo` (`177.56s`): `1,235` Passed, `1` Skipped (`#383`), `1` pre-existing Failed (`#1123`). |
| `280.24` Forensic closeout | **PASS** | Exactly 2 deliverables authored & hashed; protected invariants re-verified; local closeout commit created (no push). |

---

## 30. Blockers and Limitations

1. **`T8120` Hardware Target Blocker:** Public `xnu-10002.1.13` omits `ARM64_BOARD_CONFIG_T8120` in `board_config.h` and errors out at `proc_reg.h:226:2`. Only public `T8103` (`H13`, `MacOSX`) compiles authentically without source modification.
2. **Unlinked Kernel Subsystems & Execution Boundary:** Compiled `cswitch.o` leaves 4 undefined external references (`_ml_set_interrupts_enabled`, `_thread_terminate`, `_cpu_doshutdown`, `_cpu_idle`), `_machine_load_context` touches `msr ssbs` (unimplemented in `CPU`), and `kernel_bootstrap` / Mach / BSD / VM / IPC / clock / scheduler modules remain unbuilt and unlinked.
3. **Pre-existing CTest Baseline Regression (`#1123`):** `stage260_16_timer_architecture_and_delivery_tests` fails deterministically at line 110 in both `Debug` and `RelWithDebInfo` due to Campaign 262's timer coalescing change in `deterministic_virtual_timer.cpp`. Preserved untouched per strict no-test-weakening rules.
4. **`inferno_integration_tests` (`#383`):** Skipped (`exit 77`) because `D:\WinISO26\Inferno-Research` remains strictly isolated.

---

## 31. Final Git Identity and Parent

- **Repository:** `D:\WinISO26\WinIOS\`
- **Branch:** `campaign-191r`
- **Actual Parent Commit (C279 Baseline):** `390ef04dbc6feb5b186ea5dd12e0400c66059529`
- **Deliverables Committed:** `CMakeLists.txt`, `tests/unit/stage280_*.cpp` (5 files), `artifacts/campaign-280/WINIOS_CAMPAIGN_280_FINAL_REPORT.md`, `artifacts/campaign-280/WINIOS_CAMPAIGN_280_SHA256_MANIFEST.txt`
- **Local Commit Hash Note:** A committed file cannot embed the SHA-1 of the commit containing itself; the exact post-commit hash and verified clean `git status` are recorded directly from `git rev-parse HEAD` immediately after commit execution (zero remote pushes performed).

---

## 32. Artifact Hashes and Manifest

Exact Campaign 280 artifact directory: `D:\WinISO26\WinIOS\artifacts\campaign-280\` containing **only two files**:
1. `WINIOS_CAMPAIGN_280_FINAL_REPORT.md` (this report)
2. `WINIOS_CAMPAIGN_280_SHA256_MANIFEST.txt`

Primary verified repository & external SHA-256 checksums (`CMakeLists.txt` `512,871` B `52ED2175...`, 5 `stage280_*.cpp` suites, `cswitch.s` `D95EC6DF...`, `assym.s` `C7434D10...`, `cswitch.o` `784CE07C...`, protected files, and report checksum) are tabulated in `WINIOS_CAMPAIGN_280_SHA256_MANIFEST.txt`.

---

## 33. Explicit Certification Decision

1. **Outcome Classification:** **Outcome A** for public `xnu-10002.1.13` `T8103` (`MacOSX` / `arm64e`, with `-D__PTRAUTH_INTRINSICS__=1`), paired with **Outcome B** documented blockers for redacted `T8120` and full linked `kernel_bootstrap` execution.
2. **Genuine upstream assembly milestone:** **AWARDED**.
3. **Compiler-derived ABI milestone:** **AWARDED**.
4. **WinIOS System Maturity Certification:** **RETAINED AT LEVEL 3; LEVEL 4 EXPLICITLY REJECTED.**

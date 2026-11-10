# WINIOS CAMPAIGN 191-S FINAL ENGINEERING REPORT
## COMPILER-GENERATED CODE, FIRST REAL DYLIB & DYNAMIC BINDING CONFORMANCE

**Repository:** `D:\WinISO26\WinIOS\`  
**Host:** Windows 11 Pro x64  
**Toolchain:** Visual Studio 18 2026, MSVC C++20 (`/W4 /WX /permissive-`), Zig 0.17.0 (Clang 22.1.8 + lld/Mach-O backend)  
**Target Architecture:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120` / ARM64e)  
**Branch:** `campaign-191r` (Commit `efa04c2` for Stage S0; Stages S1–S3 uncommitted for review)  
**Date:** 2026-11-09  

---

## 1. EXECUTIVE SUMMARY

Campaign 191-S advanced the WinIOS execution pipeline beyond hand-written assembly (`hello_arm64`) to independently compiler-generated C code and genuine dynamic library linking on the virtual ARM64 guest CPU (`CPU` in `src/cpu/cpu.h`).

### Key Milestones Delivered:
1. **Stage S0 Landing & Full CTest Regression:** Staged and committed Campaign 191-R remediation and 9-assertion ADR/ADRP architectural edge case unit tests to local branch `campaign-191r` (`efa04c2`). Verified zero regressions across 321 CTest targets.
2. **Stage S1 Compiler-Generated C Execution:** Built freestanding C benchmark `compute_arm64.c` at `-O0` (`REAL-002`) and `-O2` (`REAL-003`) containing stack recursion, 32-bit & 64-bit signed division (`sdiv`), modulus (`msub`), and indirect branch calls via function pointers (`blr`). Both executed to clean termination on the guest CPU with exact verified checksum stdout `"COMPUTE_OK: 0xCAFEBABE13438009\n"`.
3. **Stage S2 First Real Dylib Execution:** Built `libmathreal.dylib` (`REAL-004`) and linked executable `main_with_dylib` (`REAL-005`). Discovered ground truth fixup formats: the compiler emits standard `LC_DYLD_INFO_ONLY` binding opcode streams and export tries rather than chained fixups. Implemented a full dyld opcode stream parser (`DyldEngine::parse_binding_stream`). Executed `main_with_dylib` through `DyldEngine` and `DarwinUserspaceEngine`, verifying cross-binary branches (`BR X16`) into the dylib, dylib global memory reads, and clean exit 0 with exact stdout `"DYLIB_OK: 0x0A11C001CAFEBAE8\n"`.
4. **Stage S3 Full Regression:** 25/25 Darwin targets passed cleanly in Release and Debug builds. Protected files and submodules remain 100% unaltered.

---

## 2. ARTIFACT CORPUS INVENTORY: SYNTHETIC VS REAL

### A. Synthetic Builder Artifacts (`src/loader/macho_corpus_builder.cpp`)
All 7 builder-constructed fixtures remain rigorously categorized as `SYNTHETIC-GUEST-VERIFIED`:
- `corpus_hello_world` (SHA-256: `64fa3682...`)
- `corpus_math_compute` (SHA-256: `bbdb7fcb...`)
- `corpus_file_io` (SHA-256: `955d7a78...`)
- `corpus_mmap_alloc` (SHA-256: `ad3350ea...`)
- `corpus_mach_ipc_client` (SHA-256: `a94f6521...`)
- `corpus_chained_fixup` (SHA-256: `687f89b9...`)
- `corpus_fairplay_encrypted` (SHA-256: `a78f2445...`)

### B. Genuine Independently-Compiled Artifacts (`tests/artifacts/macho_artifact_manifest.json`)
All 5 real artifacts are compiled via Zig 0.17.0 on disk, tracked in the dedicated real artifact manifest, and verified on the guest CPU:

| ID | File Name | Size | SHA-256 Hash | Classification | Instructions Retired | Exit Code | Verified Stdout |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `REAL-001` | `hello_arm64` | 49,976 B | `63db88c178786e58321279ca993ef936802231768954bb5d655cb4a45c9c8e9c` | `REAL-ARTIFACT-EXECUTED` | 9 | 0 | `"Hello from real Mach-O\n"` |
| `REAL-002` | `compute_arm64_O0` | 51,328 B | `9f196ce18d96a7574cb7c117505042b4e69be4924769cf2d3926eb36115f6652` | `REAL-ARTIFACT-EXECUTED` | 830 | 0 | `"COMPUTE_OK: 0xCAFEBABE13438009\n"` |
| `REAL-003` | `compute_arm64_O2` | 50,864 B | `c0d45d62d1fbe2fb7ae0fec179301c70b07e2fd4b76fe29dbf9716209c091dd7` | `REAL-ARTIFACT-EXECUTED` | 161 | 0 | `"COMPUTE_OK: 0xCAFEBABE13438009\n"` |
| `REAL-004` | `libmathreal.dylib` | 50,144 B | `5d6be8ac5379b472c955350591f839903a700bccc8cd6bdd185ac06f8ead8331` | `REAL-ARTIFACT-EXECUTED` | N/A (dylib) | N/A | Exports: `_real_add`, `_real_read_global`, `_g_real_magic` |
| `REAL-005` | `main_with_dylib` | 50,784 B | `f4ea6b73091a142636200fdf999f665f06ff8c0db4b3618d76b2b8af94a2cc44` | `REAL-ARTIFACT-EXECUTED` | 494 | 0 | `"DYLIB_OK: 0x0A11C001CAFEBAE8\n"` |

---

## 3. FAILURES ENCOUNTERED & CONCRETE REMEDIATIONS

In strict adherence to the campaign rules, every modification was driven by concrete failure analysis:

### 1. Zig Freestanding UBSan Emission in C code
- **Failure:** Compiling `compute_arm64.c` at `-O0` produced linker errors for undefined symbols `___ubsan_handle_add_overflow`, `___ubsan_handle_divrem_overflow`, etc.
- **Remediation:** Zig cc enables UBSan by default in debug modes. Added `-fno-sanitize=undefined` to freestanding compilation commands in `build_artifacts.ps1`.

### 2. CPACR_EL1 NEON/FP Exception in `compute_arm64_O2` (EC = 0x07)
- **Failure:** At `-O2`, LLVM auto-vectorized array zeroing and literal loading into 128-bit SIMD registers (`LDR Q0, [X8, #0x840]`, opcode `0x3dc21100`). The guest CPU halted after 8 retired instructions with `ESR_EL1 = 0x1e000000`, `EC = 0x07` (`Access to SIMD or floating-point functionality trapped by CPACR_EL1.FPEN`).
- **Remediation:** `DarwinUserspaceEngine::execute_macho` called `cpu_.reset()`, leaving `CPACR_EL1` at 0. Configured `cpu_.set_cpacr_el1(0x00300000)` (`CPACR_FPEN_FULL = (3 << 20)`) to enable NEON/SIMD for EL0 userspace in conformance with Apple public XNU kernel source (`xnu/osfmk/arm64/locore.s`, `msr CPACR_EL1, x0` where `x0 = CPACR_FPEN_FULL`; see also `xnu/osfmk/arm64/status.c`).
- **Verification:** Re-running `compute_arm64_O2` retired 161 instructions, cleanly exiting with code 0.

### 3. Missing Real Fixup Stream Parser in `DyldEngine`
- **Failure:** Binary inspection of `main_with_dylib` showed that the compiler emitted `LC_DYLD_INFO_ONLY` binding opcode streams (`0x73`, `0x40`, `0x12`, `0x90`, `0x00`), whereas `DyldEngine` only supported manual `DyldBindingRecord` lists and synthetic chained fixups.
- **Remediation:** Designed and implemented `DyldEngine::parse_binding_stream` in `src/loader/dyld_engine.cpp` with support for all 13 standard dyld binding opcodes, ULEB128/SLEB128 decoding, and segment address translation.
- **Verification:** Tested on `main_with_dylib` lazy bind stream, successfully extracting `___stack_chk_fail`, `_real_add`, and `_real_read_global`.

### 4. Dynamic Symbol Binding Hook Timing in Userspace Pipeline
- **Failure:** `DarwinUserspaceEngine::execute_macho` mapped segments and entered instruction execution immediately, before external dylib bindings could be applied to guest memory.
- **Remediation:** Extended `DarwinPipelineConfig` with `pre_execution_hook(Memory& memory)` executed after memory mapping and CPU initialization, but immediately prior to the first EL0 instruction fetch.
- **Verification:** `main_with_dylib` executed stubs, fetched target function pointers from GOT, branched to `libmathreal.dylib`, and exited cleanly with 494 retired instructions.

---

## 4. CATEGORIZED VERDICTS

| Evaluation Category | Verdict | Rationale & Evidence |
| :--- | :--- | :--- |
| **Independent compiler-generated Mach-O execution** | **YES** | Proven with `REAL-002` (`compute_arm64_O0`, 830 instructions) and `REAL-003` (`compute_arm64_O2`, 161 instructions) compiled via Zig 0.17.0. Verified stack frames, signed division, modulus, and function pointer indirect branches. |
| **Real dylib resolution and execution** | **YES** | Proven with `REAL-004` (`libmathreal.dylib`) and `REAL-005` (`main_with_dylib`). Dynamic symbols resolved via export trie and bound into GOT slots, executing cross-binary branches and global state reads. *(Architecture note: Dylib binding is **HOST-EMULATED** via `pre_execution_hook` prior to guest CPU execution; no guest `/usr/lib/dyld` binary is executed).* |
| **Chained fixup real support** | **PARTIAL** | WinIOS has complete synthetic support and unit tests for `LC_DYLD_CHAINED_FIXUPS` (Stage 176), but actual freestanding Mach-O binaries emitted by Clang/Zig use `LC_DYLD_INFO_ONLY` binding opcodes. WinIOS now supports BOTH formats. |
| **Two-image dynamic linking conformance** | **YES** | Proven with multi-image execution pipeline: main executable and dynamic library loaded concurrently into guest memory, symbols resolved and bound, executing cross-binary calls cleanly. |

---

## 5. PROTECTED FILES AUDIT

- `D:\WinISO26\WinIOS_README.md`: SHA-256 `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` (VERIFIED UNCHANGED)
- `D:\WinISO26\WinIOS\docs\progress.md`: SHA-256 `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` (VERIFIED UNCHANGED)
- `D:\WinISO26\Inferno-Research\`: Submodule commit `cc4302a99167abec69b714cfd00c38caece7e7de` (VERIFIED UNTOUCHED)
- Git Status: Stage S0 committed to `campaign-191r` (`efa04c2`). Stages S1–S3 remain uncommitted on working tree ready for review.

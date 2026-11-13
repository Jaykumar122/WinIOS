# WinIOS Campaign 191-V Final Report — Differential CPU Validation & Real Open-Source Application Execution

**Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r`  
**Host:** Windows 11 x64  
**Toolchain:** Visual Studio 18 2026, MSVC C++20 (`/W4 /WX /permissive-`)  
**Guest Toolchain:** Zig 0.17.0 (Clang 22.1.8 + Zig Mach-O linker)  
**Target Research Identity:** iPhone 15, `iPhone15,4`, `D37AP`, Apple A16 `T8120` (`aarch64-macos`, `aarch64-ios`)  
**Scope:** Realistic userspace memory, differential CPU validation against Unicorn Engine, and execution of real open-source applications (`sha256`, `cJSON`, `uzlib`) across 9 variants. (This campaign does NOT attempt to boot XNU or iOS kernel).

---

## 1. Executive Summary

Campaign 191-V successfully elevated WinIOS from executing hand-crafted synthetic instructions and micro-benchmarks to executing complex, independently authored, permissively licensed open-source ARM64 applications through guest-side dynamic linking (`minidyld`) and runtime (`libSystem.B.dylib`).

Key milestones accomplished:
1. **Evidence Hygiene & Chained Fixups (Stage V0):** Un-ignored markdown reports in git, verified double-build reproducibility of all real artifacts, implemented guest-side `DYLD_CHAINED_PTR_64` pointer chain walker in `minidyld.c`, and audited legacy assertions across 16 test files.
2. **Realistic Userspace Normal-Memory Model (Stage V1):** Implemented 16 KB Stage-1 Normal-Memory MMU (`SCTLR_EL1.M=1, A=0, WXN=1`, `MAIR_EL1=0x00FF`), hardware SP alignment checking (`ExceptionClass::SpAlignment = 0x26`), and unaligned page-crossing zero-partial store support. Verified with `unaligned_stress` (`REAL-024`).
3. **Differential CPU Testing against Unicorn Engine (Stage V2):** Validated WinIOS CPU against Unicorn Engine 2.1.4 (QEMU TCG AArch64) across 1,800 test cases (`diff_winios_runner.exe`). 1,750 cases compared with 100% bit-for-bit state match. Uncovered and fixed logical immediate SP writes and vector `CNT` popcount.
4. **Execution of Real Open-Source Programs (Stage V3):** Successfully compiled and executed `sha256`, `cJSON`, and `uzlib` across 9 distinct variants (`-O0` macos, `-O2` macos, `-O2` ios chained fixups). All 9 variants matched independent host x64 ground truth bit-for-bit, retiring a total of 2,388,851 guest instructions.
5. **Clean Dual Regression:** Full Debug and Release CTest test suites pass 100% (334 passed, 1 skipped `#303 inferno_integration_tests`, 0 failed).

---

## 2. Stage Breakdown and Achievements

### Stage V0: Evidence Hygiene & Guest Chained Fixups
- Un-ignored `docs/campaign_191_200/*.md` in `.gitignore` and committed campaign documentation into Git.
- Implemented `DYLD_CHAINED_PTR_64` chained fixups parsing directly in `tests/artifacts/src/minidyld.c`.
- Rebuilt all artifacts and confirmed double-build bit-for-bit reproducibility.
- Replaced synthetic assertions with real-artifact fixtures across 16 legacy test files.
- Committed as `b23370f`.

### Stage V1: Userspace Memory Model & Unaligned Access
- Configured 16 KB Stage-1 Normal memory for Darwin userspace in `DarwinUserspaceEngine`.
- Enforced hardware SP alignment (`SP % 16 == 0` on memory operations when `SCTLR_EL1.SA0 = 1`).
- Implemented unaligned page-crossing zero-partial stores in `cpu.cpp`.
- Validated with `unaligned_stress` (`REAL-024`, 50,864 bytes, SHA-256 `beb3e825596074260aa43f2d6bc9aa104a84d20b08e886ff5ba9091242ad1ab5`).
- Committed as `6d4d902`.

### Stage V2: Differential CPU Testing vs Unicorn 2.1.4
- Integrated Python Unicorn 2.1.4 (QEMU TCG AArch64) as an independent architectural oracle.
- Developed `tests/tools/diff_cpu/diff_winios_runner.cpp` and `diff_arm64_oracle.py` comparing `PC`, `SP`, `NZCV`, `X0..X30`, `Q0..Q31`, and memory across 1,800 cases.
- Discovered and fixed 2 real CPU bugs in `cpu.cpp`:
  1. Logical immediate SP write handling (`cpu.cpp:5622`).
  2. Vector `CNT` classification and execution (`cpu.cpp:2795`, `9577`).
- Achieved 1,750/1,750 (100%) bit-for-bit match on supported instructions (50 cases in `ADC`/`SBC` identified as clean unsupported).
- Committed as `66493a4`.

### Stage V3: Real Open-Source Application Execution
Compiled and executed 3 prominent open-source packages across 9 variants:
- **`sha256` (Brad Conte / crypto-algorithms):**
  - `REAL-025` (`sha256_app_O0`): 443,797 instructions retired, exit 0.
  - `REAL-026` (`sha256_app_O2`): 147,037 instructions retired, exit 0.
  - `REAL-027` (`sha256_app_ios`): 146,429 instructions retired, exit 0 (Chained fixups).
  - Output: `SHA256_HASH=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`.
- **`cJSON` (Dave Gamble / cJSON):**
  - `REAL-028` (`cjson_app_O0`): 83,747 instructions retired, exit 0.
  - `REAL-029` (`cjson_app_O2`): 52,310 instructions retired, exit 0.
  - `REAL-030` (`cjson_app_ios`): 49,685 instructions retired, exit 0 (Chained fixups).
  - Output: `JSON_MATCH=1`.
- **`uzlib` (Paul Sokolovsky / uzlib):**
  - `REAL-031` (`uzlib_app_O0`): 855,044 instructions retired, exit 0.
  - `REAL-032` (`uzlib_app_O2`): 304,980 instructions retired, exit 0.
  - `REAL-033` (`uzlib_app_ios`): 305,822 instructions retired, exit 0 (Chained fixups).
  - Output: `UZLIB_RC=0 COMP=127 DECOMP=128 DECOMP_MATCH=1`.

---

## 3. Real Mach-O Artifact Manifest (v1.6.0)

| ID | File Name | Size (B) | SHA-256 | Target | Optimization | Fixups | Classification |
|---|---|---:|---|---|:---:|:---:|:---:|
| `REAL-001` | `hello_arm64` | 49,976 | `63db88c178786e58321279ca993ef936802231768954bb5d655cb4a45c9c8e9c` | macOS | None (asm) | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-002` | `compute_arm64_O0` | 51,312 | `f6f10b0673e2e070ba4b68acc7d1ba9766ffa11d892eb6821dc2f9dc0954fd86` | macOS | `-O0` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-003` | `compute_arm64_O2` | 50,704 | `a3c4b9e273ff3fa9831557bf89d18fa74dc704ceba2f489cf3017cf2d4c06a37` | macOS | `-O2` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-004` | `libreal_math.dylib` | 51,328 | `9199d7a22ef2562be3654497e17cb831e34ffae35b88939a3f2d22dca849f546` | macOS | `-O2` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-005` | `client_math` | 50,736 | `2aa20d33b006c09a8039c28bf9324022b72c91ee304c4f9ec3a8862cf2aa0325` | macOS | `-O2` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-006` | `hello_ios` | 66,248 | `fcfc4f794939ae8433d3d4b68bb712ce5250444365df4bdfdd7f44d8520cf6cf` | iOS | None (asm) | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-007` | `client_math_ios` | 50,784 | `085695ef393f93c042bb5cfad2ebbf5a2f7c0a6b9a896690cf851aa9fc7a0c8b` | iOS | `-O2` | Chained | REAL-ARTIFACT-EXECUTED |
| `REAL-008` | `libreal_mid.dylib` | 51,344 | `9feec98ea91eb97cc9d1d1796191b6502aa878aa46d4948ba1c3f9a7aa7621c9` | macOS | `-O2` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-009` | `client_three_image` | 50,736 | `2467eb1310fe6eb196aa9116db8a2bfab5d70f1a1d13dbb609c13ee358a94ee8` | macOS | `-O2` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-010` | `client_three_image_rpath` | 50,736 | `a2fc50020ae19b9d7990499be55d6e279268305886fe8e6789fbf02a8e4df461` | macOS | `-O2` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-011` | `libreal_init.dylib` | 51,440 | `e5bc41b0cb51b223bb35ba70c1e87e148567117df72f785b46d0fe62e9ddffb5` | macOS | `-O2` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-012` | `client_initializers` | 50,816 | `141b77bf258b49870be5343cc7dbcf7fbfa06f71d5b3ee5c5ae684175b36aa0d` | macOS | `-O2` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-013` | `client_tlv` | 50,768 | `d601b17a100a75f85e49f60b411f5fe0ba1e427705fe09a3cfc1664d081f9b1d` | macOS | `-O2` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-014` | `probe_arm64e` | 66,248 | `26d03ca4d71597f7fa203250550ea675cf44e5482bf4f364e030a5ca63ffbfe6` | iOS | None (asm) | LC_DYLD_INFO | REAL-ARTIFACT-PROBED |
| `REAL-015` | `stack_protector_app` | 50,832 | `585d6eb0b4b2c129f1b20e06249ff5bc3ee3133ff8f6797f37cc4423405c12e2` | macOS | `-O2` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-016` | `stack_smash_app` | 50,848 | `3c07ea8e93297a76e974d9e5a1b3f9ff7cb3ecfc69076f7b1129f5fbb129fae1` | macOS | `-O2` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-017` | `minidyld` | 67,512 | `5d38063001550404dad3b08f753488c0fcb06da956ea952368d3dfe355aaea3f` | macOS | `-O2` | None (static) | REAL-ARTIFACT-EXECUTED |
| `REAL-018` | `libSystem.B.dylib` | 71,920 | `94c66c4d93b10e0d792be637458da41d77d7cbb69e0aba794d3eb3205b2477a2` | macOS | `-O2` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-019` | `mach_trap_app` | 50,880 | `6067b8a74e543e0d869273c52e0ef75f2b8a4f910e53a297e68fa707577dc957` | macOS | `-O2` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-020` | `clock_trap_app` | 50,848 | `ec86d65c3bbd44c9b9177a6ddc81d3106574dc6451eeb47f717804491c1fc342` | macOS | `-O2` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-021` | `thread_self_app` | 50,848 | `2a84a60f94f997cb6586326620f4c619280d0d627b0b68630773d2a70ccb5383` | macOS | `-O2` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-022` | `chained_fixup_stress` | 50,336 | `aa5dc4ae8a7ea3926cb1eeec932e6504a3f3df05bf9ee00fca7c631484ffbfa0` | iOS | `-O2` | Chained | REAL-ARTIFACT-EXECUTED |
| `REAL-023` | `client_chain_ios` | 50,368 | `5f9aac3db3b3c672d13c6dab3d8ea4437e8d28ebda22d8fab03a1727ab3f841e` | iOS | `-O2` | Chained | REAL-ARTIFACT-EXECUTED |
| `REAL-024` | `unaligned_stress` | 50,864 | `beb3e825596074260aa43f2d6bc9aa104a84d20b08e886ff5ba9091242ad1ab5` | macOS | `-O2` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-025` | `sha256_app_O0` | 51,376 | `8b9d041d0ff663fc1b2d0cf1a16f6cbb49b043eb8e389a768d98c215a71bd2dd` | macOS | `-O0` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-026` | `sha256_app_O2` | 51,184 | `be02f25cd8082e63c3b2d2f88d3193ac5ad9340eeed51786837842765af2eb22` | macOS | `-O2` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-027` | `sha256_app_ios` | 67,528 | `a67ebe957a0de5c710bd47d1531094d6916a3caca4083fa92840fbf9af925218` | iOS | `-O2` | Chained | REAL-ARTIFACT-EXECUTED |
| `REAL-028` | `cjson_app_O0` | 97,424 | `e8772ed70eb75995e2788e9e8e796d707f23346c14059b08d327bb4e492f24fb` | macOS | `-O0` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-029` | `cjson_app_O2` | 71,264 | `3d0ffba34b94f31ff28a568c2857d16a8cdbc8c4e87af01ba22f178036011a6d` | macOS | `-O2` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-030` | `cjson_app_ios` | 71,336 | `f18ce6f30ef1ef86416481e74fc4226c47e86408040e602544bd83b0534ce06b` | iOS | `-O2` | Chained | REAL-ARTIFACT-EXECUTED |
| `REAL-031` | `uzlib_app_O0` | 71,920 | `709aa342d01e63b53f89931249ae6ee582b7233239a87120ed9570fcc48fe0dd` | macOS | `-O0` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-032` | `uzlib_app_O2` | 70,016 | `437845dfabcd927e9cc64d6795e712d08d460fbee08b0d57b1d77cf970dea0ac` | macOS | `-O2` | LC_DYLD_INFO | REAL-ARTIFACT-EXECUTED |
| `REAL-033` | `uzlib_app_ios` | 70,000 | `b6ba62ee5efc35efe90fe5dfb07641adc6d643093c4793e36b0596c3ff8ebc18` | iOS | `-O2` | Chained | REAL-ARTIFACT-EXECUTED |

---

## 4. Test Regression Results

### Full Sequential Regressions (335 CTest Targets)
- **Release CTest Regression:**
  - Total Targets: 335
  - Passed: 334 (100% of runnable targets)
  - Skipped: 1 (`#303 inferno_integration_tests`, expected integration test requiring external Inferno daemon)
  - Failed: 0
  - Execution Time: 82.43 seconds
- **Debug CTest Regression:**
  - Total Targets: 335
  - Passed: 334 (100% of runnable targets)
  - Skipped: 1 (`#303 inferno_integration_tests`)
  - Failed: 0
- **Warning Count:** 0 warnings across all C++20 MSVC builds (`/W4 /WX`).

---

## 5. Architectural Capability Ledger

| Capability | Verdict | Evidence / Reference |
|---|:---:|---|
| AArch64 Userspace EL0 CPU | YES | 1,750 differential tests matched vs Unicorn 2.1.4, 2.38M instructions retired across open-source apps |
| Normal-Memory MMU (16 KB) | YES | Stage-1 Normal memory active, unaligned access & zero-partial crossing verified (`REAL-024`) |
| Hardware SP Alignment | YES | ExceptionClass 0x26 generated on misaligned SP memory operations (`SA0=1`) |
| LC_DYLD_INFO_ONLY Binding | YES | Guest `minidyld` loads and binds dylibs via `LC_DYLD_INFO_ONLY` streams |
| DYLD_CHAINED_PTR_64 Fixups | YES | Guest `minidyld` walks 64-bit chained fixups for iOS binaries (`REAL-007, 022, 023, 027, 030, 033`) |
| Multi-Image Dynamic Linking | YES | Three-image chains with `@rpath` and constructors verified (`REAL-009, 010, 012`) |
| Open-Source App Execution | YES | `sha256`, `cJSON`, and `uzlib` run and match host x64 outputs bit-for-bit |
| BSD Syscall Interception | YES | `svc #0x80` traps handled for file I/O, memory mapping, and process lifecycle |
| Mach Traps | YES | `mach_reply_port`, `clock_get_time`, `thread_self_trap` handled via negative svc numbers |
| XNU Kernel Boot | NO | Explicit non-goal; WinIOS provides userspace Darwin emulation, not kernel virtualization |
| iOS Firmware Decryption | NO | Explicit non-goal; all binaries compiled independently from source |

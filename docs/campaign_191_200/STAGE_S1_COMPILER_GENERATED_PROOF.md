# STAGE S1: SECOND REAL ARTIFACT: COMPILER-GENERATED C PROGRAM PROOF

**Campaign:** WINIOS CAMPAIGN 191-S  
**Stage:** S1 — Compiler-Generated Code Execution  
**Host:** Windows 11 Pro x64  
**Toolchain:** Zig 0.17.0 (Clang 22.1.8 + lld/Mach-O backend)  
**Target Architecture:** ARM64 (`cputype = 0x0100000C`, `platform = PLATFORM_MACOS 1`)  
**Date:** 2026-11-09  

---

## 1. OBJECTIVE & REQUIREMENTS

Stage S1 transitions WinIOS from hand-written assembly execution (`hello_arm64`) to genuine compiler-generated C programs compiled with standard optimization levels:
- `REAL-002`: `tests/artifacts/bin/compute_arm64_O0` (compiled with `-O0`)
- `REAL-003`: `tests/artifacts/bin/compute_arm64_O2` (compiled with `-O2`)

### Freestanding Program Requirements (`compute_arm64.c`):
1. **Freestanding:** No C standard library runtime (no libc headers or CRT).
2. **System Call Helpers:** Inline assembly wrappers for Darwin ARM64 `SYS_write` (syscall 4) and `SYS_exit` (syscall 1).
3. **Memory Segments:**
   - `.data`: Initialized global `g_init_val = 0xCAFEBABE12345678ULL`, `g_call_count = 1`.
   - `.bss`: Zero-initialized `g_bss_result`, `g_bss_status`.
   - `.rodata`: Prefix strings, hex lookup table, newline.
4. **Three Non-Inlined Functions (`__attribute__((noinline))`):**
   - `compute_fib_sum(10)`: Local stack frame array allocation, loop computing Fibonacci series and sum (`0x8F`).
   - `compute_arithmetic(-105, 10, 1000000000LL, -7LL)`: 32-bit and 64-bit signed division (`sdiv`), modulus (`msub`), shifts (`lsl`, `lsr`), and bitwise XOR (`0x1772C44`).
   - `apply_operator(op_xor_add, ...)`: Indirect branch and link via register (`blr`) through a function pointer.
5. **Entry Point & Verification:**
   - Evaluates all results against expected constants.
   - Computes checksum: `g_bss_result + g_call_count = 0xCAFEBABE13438009ULL`.
   - Formats and outputs exact string: `"COMPUTE_OK: 0xCAFEBABE13438009\n"`.
   - Cleanly calls `sys_exit(0)`.

---

## 2. ARTIFACT PROVENANCE & BUILD DATA

| Artifact ID | Binary Name | Size | SHA-256 Hash | Compiler Command |
| :--- | :--- | :--- | :--- | :--- |
| `REAL-002` | `compute_arm64_O0` | 51,328 B | `9f196ce18d96a7574cb7c117505042b4e69be4924769cf2d3926eb36115f6652` | `zig cc -target aarch64-macos -nostdlib -O0 -fno-sanitize=undefined -Wl,-e,_start tests/artifacts/src/compute_arm64.c -o tests/artifacts/bin/compute_arm64_O0` |
| `REAL-003` | `compute_arm64_O2` | 50,864 B | `c0d45d62d1fbe2fb7ae0fec179301c70b07e2fd4b76fe29dbf9716209c091dd7` | `zig cc -target aarch64-macos -nostdlib -O2 -Wl,-e,_start tests/artifacts/src/compute_arm64.c -o tests/artifacts/bin/compute_arm64_O2` |

Rebuild automation is integrated in `tests/artifacts/build_artifacts.ps1`.

---

## 3. CONCRETE FAILURE PROTOCOL: CPACR_EL1 FP/SIMD TRAP IN -O2

During initial execution of `REAL-003` (`compute_arm64_O2`), the guest process halted after 8 retired instructions without exiting.

### Diagnostic Evidence:
- **Faulting PC:** `0x158c` (within `_compute_fib_sum`)
- **Faulting Instruction Word:** `0x3dc21100` (`LDR Q0, [X8, #0x840]`, 128-bit SIMD vector load)
- **CPU Exception State:**
  - `ESR_EL1`: `0x1e000000`
  - `EC`: `0x07` (`0b000111` - Access to SIMD or floating-point functionality trapped by `CPACR_EL1.FPEN`)
  - `ELR_EL1`: `0x158c`

### Root Cause Analysis:
At `-O2`, LLVM automatically vectorized stack array initialization in `_compute_fib_sum`, emitting 128-bit vector loads/stores to initialize/zero the `table[16]` array.
In ARMv8-A architecture (DDI 0487), access to NEON/SIMD/FP registers from EL0 is controlled by `CPACR_EL1.FPEN` (bits 21:20):
- `0b00`: Traps all EL0 and EL1 NEON/FP operations.
- `0b11`: Full access enabled without trapping.

While `VirtualPlatform`, `VirtualFirmware`, and `AppleKernelExecutionEngine` initialized `CPACR_EL1` to `0x00300000`, `DarwinUserspaceEngine::execute_macho` called `cpu_.reset()`, leaving `CPACR_EL1` at 0.

### Remediation:
In `src/runtime/darwin_userspace_engine.cpp`:
Configured `cpu_.set_cpacr_el1(0x00300000)` (`CPACR_FPEN_FULL = (3 << 20)`) during userspace process initialization, directly conforming with Apple public XNU kernel source (`xnu/osfmk/arm64/locore.s`, `msr CPACR_EL1, x0` where `x0 = CPACR_FPEN_FULL`; see also `xnu/osfmk/arm64/status.c`).

---

## 4. EXECUTION RESULTS

Following the CPACR_EL1 remediation, both compiler-generated artifacts executed cleanly to completion through `DarwinUserspaceEngine` and the guest CPU:

### REAL-002 (-O0):
- **Guest Instructions Retired:** 830
- **Process Termination:** Clean `SYS_exit(0)`
- **Syscalls Handled:** 4 (1x `SYS_write` prefix, 1x `SYS_write` hex, 1x `SYS_write` newline, 1x `SYS_exit`)
- **Captured stdout:** `"COMPUTE_OK: 0xCAFEBABE13438009\n"`
- **Result:** PASSED

### REAL-003 (-O2):
- **Guest Instructions Retired:** 161 (5.15x instruction reduction due to compiler optimization and NEON vectorization)
- **Process Termination:** Clean `SYS_exit(0)`
- **Syscalls Handled:** 4
- **Captured stdout:** `"COMPUTE_OK: 0xCAFEBABE13438009\n"`
- **Result:** PASSED

### Test Suite (`stage_s1_compiler_generated_execution_tests.cpp`):
1. `test_artifacts_disk_load_and_sha256`: PASSED
2. `test_execute_compute_arm64_o0`: PASSED
3. `test_execute_compute_arm64_o2`: PASSED
4. `test_compiler_artifact_tamper_rejection`: PASSED

**Status:** Both `REAL-002` and `REAL-003` are upgraded to `REAL-ARTIFACT-EXECUTED`.

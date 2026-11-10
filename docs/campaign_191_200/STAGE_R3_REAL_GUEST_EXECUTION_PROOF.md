# WinIOS Campaign 191-R: Stage R3 Real Guest Execution Proof

**Artifact:** `tests/artifacts/bin/hello_arm64`  
**Classification:** `REAL-ARTIFACT-EXECUTED`  
**Execution Engine:** `DarwinUserspaceEngine` (`src/runtime/darwin_userspace_engine.cpp`) driving `CPU` (`src/cpu/cpu.cpp`)  
**Test Suite:** `tests/unit/stage_r3_real_artifact_execution_tests.cpp`  
**Target:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120` / ARM64e)  
**Host Toolchain:** Windows 11 x64 / MSVC C++20 (`/W4 /WX /permissive-`)  

---

## 1. Executive Summary

Stage R3 delivers concrete, non-synthetic verification of real ARM64 Mach-O execution within the WinIOS virtual platform.

### Evidence Standard Adhered To:
1. **Physical Binary Ingestion:** The executable `tests/artifacts/bin/hello_arm64` was read directly from disk via filesystem I/O (`std::ifstream`), completely bypassing `MachOCorpusRegistry` and in-memory fixture generators.
2. **Cryptographic Validation Gate:** The file's SHA-256 digest (`63db88c178786e58321279ca993ef936802231768954bb5d655cb4a45c9c8e9c`) and length (49,976 bytes) were verified against `tests/artifacts/macho_artifact_manifest.json` prior to execution.
3. **Pure EL0 Virtual CPU Execution:** Instructions were stepped through `CPU::step` at EL0. Instruction retirement strictly records guest CPU steps, completely decoupled from host loader or setup activities.
4. **Darwin ABI Conformance:** The guest successfully issued `SVC #0x80` traps for Darwin BSD `SYS_write` (syscall 4) and `SYS_exit` (syscall 1).

---

## 2. Step-by-Step Guest CPU Instruction Execution Trace

The execution loop stepped through the binary starting at entry point `0x1000013f8` (resolved from `LC_MAIN.entryoff = 0x13f8`):

```text
Step  Virtual PC   Opcode       Disassembly            Architectural State Evolution
[1]   0x1000013f8  0xd2800020   MOV X0, #1             X0 = 0x0000000000000001 (stdout)
[2]   0x1000013fc  0xf0000021   ADRP X1, 8             X1 = 0x0000000100008000 (Base of __DATA page)
[3]   0x100001400  0x91002021   ADD X1, X1, #8         X1 = 0x0000000100008008 ("Hello from real Mach-O\n")
[4]   0x100001404  0xd28002e2   MOV X2, #23            X2 = 0x0000000000000017 (23 bytes count)
[5]   0x100001408  0xd2800090   MOV X16, #4            X16 = 0x0000000000000004 (SYS_write)
[6]   0x10000140c  0xd4001001   SVC #0x80              --> Trap Taken: BSD SYS_write
                                                       FD: 1, Buffer: 0x100008008, Count: 23
                                                       Bytes: "Hello from real Mach-O\n"
                                                       Return: X0 = 23, NZCV Carry = 0 (Success)
[7]   0x100001410  0xd2800000   MOV X0, #0             X0 = 0x0000000000000000 (Exit status 0)
[8]   0x100001414  0xd2800030   MOV X16, #1            X16 = 0x0000000000000001 (SYS_exit)
[9]   0x100001418  0xd4001001   SVC #0x80              --> Trap Taken: BSD SYS_exit
                                                       Exit Code: 0, Process Terminated
```

### 2.1 Instruction Accounting
- **Total Guest Instructions Retired:** **9 instructions** (derived strictly from `CPU::step` iterations).
- **Host Loader Overhead:** 0 instructions counted in retirement.
- **Process Status:** Exited cleanly (`process_exited = true`, `exit_code = 0`).
- **Captured Output:** Exact string `"Hello from real Mach-O\n"` captured in `result.stdout_output`.

---

## 3. Negative & Adversarial Validation Results

| Test Case | Method | Expected Result | Actual Result | Status |
| :--- | :--- | :--- | :--- | :---: |
| **SHA-256 Mutation** | Bit flipped at offset `0x13f8` | Computed hash diverges, admission rejected | SHA mismatch detected, gated out | **PASS** |
| **File Truncation** | Truncated to 16 bytes | Loader rejects with `TruncatedHeader` | Rejected cleanly (`TruncatedHeader`) | **PASS** |
| **Invalid Architecture** | CPU type changed to 0x7 (x86) | Loader rejects with `UnsupportedCpuType` | Rejected cleanly (`UnsupportedCpuType`) | **PASS** |

---

## 4. Test Execution Log (`stage_r3_real_artifact_execution_tests.exe`)

```text
=================================================================
WinIOS Stage R3: Genuine Mach-O Guest CPU Execution Verification
Target: iPhone 15 (iPhone15,4 / D37AP / Apple A16 Bionic T8120 / ARM64e)
Artifact: tests/artifacts/bin/hello_arm64 (Independently Compiled)
=================================================================

[Test 1] Load Real Binary from Disk and Verify Authenticity...
  Loaded: tests/artifacts/bin/hello_arm64 (49976 bytes)
  SHA-256: 63db88c178786e58321279ca993ef936802231768954bb5d655cb4a45c9c8e9c
[PASS] Real Binary Disk Load and SHA-256 Gate

[Test 2] Real Mach-O Guest CPU Execution with Instruction Retirement...
  Guest instructions retired: 9
  Process exited: true, Exit code: 0
  Captured stdout: "Hello from real Mach-O\n"
  BSD syscalls handled: 2
[PASS] Real Mach-O Guest CPU Instruction Execution

[Test 3] Adversarial Tampered Binary Rejection (SHA-256 Mutation)...
[PASS] Adversarial Tampered Binary Rejection

[Test 4] Adversarial Truncated Binary Rejection...
[PASS] Adversarial Truncated Binary Rejection

[Test 5] Adversarial Wrong CPU Type Rejection...
[PASS] Adversarial Wrong CPU Type Rejection

=================================================================
Stage R3 Test Summary: 5 passed, 0 failed
=================================================================
```

---

## 5. Artifact Status Upgrade

With physical file loading from disk, cryptographic hash verification, and verified guest CPU instruction stepping at EL0, the artifact `REAL-001` (`tests/artifacts/bin/hello_arm64`) is officially upgraded to:
**`REAL-ARTIFACT-EXECUTED`**
